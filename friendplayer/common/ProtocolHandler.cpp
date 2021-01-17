#include "common/ProtocolHandler.h"

#include "common/Socket.h"
#include "common/Log.h"

#include <asio/buffer.hpp>

using namespace std::chrono_literals;

ProtocolHandler::ProtocolHandler(int client_id, const asio_endpoint& target_endpoint)
    : client_id(client_id),
      endpoint(target_endpoint),
      parent_socket(nullptr),
      async_send_worker(nullptr),
      async_recv_worker(nullptr),
      frame_window_start(0),
      send_sequence_number(0),
      protocol_state(HS_UNINITIALIZED),
      highest_acked_seqnum(0) {
    state = std::make_unique<std::atomic<StreamState>>(StreamState::UNINITIALIZED);

    send_message_queue_m = std::make_unique<std::mutex>();
    send_message_queue_cv = std::make_unique<std::condition_variable>();

    recv_message_queue_m = std::make_unique<std::mutex>();
    recv_message_queue_cv = std::make_unique<std::condition_variable>();

    shared_data_m = std::make_unique<std::mutex>();
    shared_data_cv = std::make_unique<std::condition_variable>();
    waiting_for_ack = std::make_unique<std::atomic_bool>(false);

    handshake_m = std::make_unique<std::mutex>();
    handshake_signal = std::make_unique<std::condition_variable>();

    UpdateHeartbeat();
}

bool ProtocolHandler::BlockForHandshake() {
    std::unique_lock<std::mutex> hs_lock(*handshake_m);
    handshake_signal->wait(hs_lock, [this] {
        return protocol_state == HS_READY ||
               protocol_state == HS_FAILED;
    });
    return protocol_state == HS_READY;
}

void ProtocolHandler::KillAllThreads() {
    Transition(DISCONNECTED);

    send_message_queue_cv->notify_one();
    recv_message_queue_cv->notify_one();
    shared_data_cv->notify_one();

    async_send_worker->join();
    async_recv_worker->join();
    async_retransmit_worker->join();
}

void ProtocolHandler::EnqueueRecvMessage(fp_proto::Message&& message) {
    std::lock_guard<std::mutex> lock(*recv_message_queue_m);
 
    recv_message_queue.emplace_back(std::move(message));
    recv_message_queue_cv->notify_one();
}

void ProtocolHandler::EnqueueSendMessage(fp_proto::Message&& message) {
    std::lock_guard<std::mutex> lock(*send_message_queue_m);

    send_message_queue.emplace_back(std::move(message));
    send_message_queue_cv->notify_all();
}

void ProtocolHandler::RecvWorker() {
    // Do the handshake
    // Setup keys
    std::list<fp_proto::DataMessage> reordered_msg_queue;
    
    const auto process_queue_cons = [this, &reordered_msg_queue] {
        while (!reordered_msg_queue.empty() && (reordered_msg_queue.front().sequence_number() == frame_window_start)) {
            //process p_msg
            fp_proto::DataMessage p_msg = std::move(reordered_msg_queue.front());
            OnDataFrame(p_msg);
            reordered_msg_queue.pop_front();
            frame_window_start = p_msg.sequence_number() + 1;
        }
    };

    {
        std::lock_guard<std::mutex> hs_lock(*handshake_m);
        if (!DoHandshake()) {
            if (client_id == -1) {
                LOG_INFO("Dropping connection to host, failed to pass handshake");
            } else {
                LOG_INFO("Dropping connection from client {}, failed to pass handshake", client_id);
            }
            Transition(DISCONNECTED);
            protocol_state = HS_FAILED;
            handshake_signal->notify_one();
            return;
        }
        handshake_signal->notify_one();
    }

    // Begin streaming!
    while (true) {
        std::unique_lock<std::mutex> lock(*recv_message_queue_m);
        recv_message_queue_cv->wait(lock, [this] { return !recv_message_queue.empty() || !IsConnectionValid(); });

        if (!IsConnectionValid()) {
            break;
        }
        
        while (!recv_message_queue.empty()) {
            fp_proto::Message msg = std::move(recv_message_queue.front());
            recv_message_queue.pop_front();

            switch (msg.Payload_case()) {
            case fp_proto::Message::kHsMsg:
                if (client_id == -1) {
                    LOG_INFO("Host sent handshake during stream");
                } else {
                    LOG_INFO("Client {} sent handshake during stream", client_id);
                }
                break;
            case fp_proto::Message::kDataMsg: {
                const fp_proto::DataMessage& data_msg = msg.data_msg();
                uint32_t seq_num = data_msg.sequence_number();
                
                std::lock_guard<std::mutex> sh_lock(*shared_data_m);
                if (blocking_acks.size() > 0) {
                    if (client_id == -1) {
                        LOG_INFO("Dropping DataMessage from host, blocking on slow resend; seq num={}", seq_num);
                    } else {
                        LOG_INFO("Dropping DataMessage from client {}, blocking on slow resend; seq num={}", client_id, seq_num);
                    }
                    continue;
                }
                
                // Ack packet, stream is functioning normally
                fp_proto::Message ack;
                *ack.mutable_ack_msg() = fp_proto::AckMessage();
                ack.mutable_ack_msg()->set_sequence_ack(seq_num);
                EnqueueSendMessage(std::move(ack));
                if (seq_num < frame_window_start) {
                    if (client_id == -1) {
                        LOG_INFO("Ack to host was dropped, ignoring DataMessage; seq num={}, window={}", seq_num, frame_window_start);
                    } else {
                        LOG_INFO("Ack to client {} was dropped, ignoring DataMessage; seq num={}, window={}", client_id, seq_num, frame_window_start);
                    }
                    continue;
                }

                auto it = reordered_msg_queue.begin();
                for(; it != reordered_msg_queue.end(); it++) {   
                    if (seq_num < it->sequence_number()) {
                        break;
                    }
                }
                reordered_msg_queue.emplace(it, data_msg);
                break;
            }
            case fp_proto::Message::kAckMsg:
                OnAcknowledge(msg.ack_msg());
                break;
            case fp_proto::Message::kHbMsg:
                if (msg.hb_msg().is_response()) {
                    auto arrival_time = clock::now();
                    std::lock_guard<std::mutex> sh_lock(*shared_data_m);
                    RTT_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(arrival_time.time_since_epoch() - clock::duration(msg.hb_msg().timestamp())).count();
                    // Notify the heartbeat manager that we're still alive
                    UpdateHeartbeat();
                    if (client_id == -1) {
                        LOG_INFO("Got heartbeat response from host, RTT={}", RTT_milliseconds);
                    } else {
                        LOG_INFO("Got heartbeat response from client {}, RTT={}", client_id, RTT_milliseconds);
                    }
                } else {
                    // Reply with the same heartbeat
                    msg.mutable_hb_msg()->set_is_response(true);
                    EnqueueSendMessage(std::move(msg));
                }
                break;
            case fp_proto::Message::kStateMsg:
                OnStateMessage(msg.state_msg());
                break;
            default:
                if (client_id == -1) {
                    LOG_INFO("Unknown message type {} received from host", msg.Payload_case());
                } else {
                    LOG_INFO("Unknown message type {} received from client {}", msg.Payload_case(), client_id);
                }
                break;
            }
        }

        process_queue_cons();
        // window is a bit behind, dropped packets perhaps?
        while (!reordered_msg_queue.empty() && reordered_msg_queue.back().sequence_number() > frame_window_start + RECV_DROP_WINDOW) {
            frame_window_start = reordered_msg_queue.front().sequence_number();
            process_queue_cons();
        }
    }
    std::unique_lock<std::mutex> lock(*recv_message_queue_m);
    if (client_id == -1) {
        LOG_INFO("Message worker for host exiting, timeout={}", IsConnectionValid());
    } else {
        LOG_INFO("Message worker for client {} exiting, timeout={}", client_id, IsConnectionValid());
    }
}

void ProtocolHandler::SendWorker() {
    while (true) {
        std::unique_lock<std::mutex> lock(*send_message_queue_m);
        send_message_queue_cv->wait(lock, [this] { return !send_message_queue.empty() || !IsConnectionValid(); });

        if (!IsConnectionValid()) {
            break;
        }
        fp_proto::Message to_send = std::move(send_message_queue.front());
        send_message_queue.pop_front();

        if (protocol_state == HandshakeState::HS_READY) {
            switch (to_send.Payload_case()) {
            case fp_proto::Message::kHsMsg:
                parent_socket->MessageSend(to_send, endpoint);
                break;
            case fp_proto::Message::kAckMsg:
                parent_socket->MessageSend(to_send, endpoint);
                break;
            case fp_proto::Message::kStateMsg:
                parent_socket->MessageSend(to_send, endpoint);
                break;
            case fp_proto::Message::kDataMsg:
                SendDataMessage_internal(to_send);
                break;
            case fp_proto::Message::kHbMsg:
                if (!to_send.hb_msg().is_response()) {
                    auto cur_time = clock::now();
                    to_send.mutable_hb_msg()->set_timestamp(cur_time.time_since_epoch().count());
                }
                parent_socket->MessageSend(to_send, endpoint);
                break;
            default:
                if (client_id == -1) {
                    LOG_WARNING("Unexpected message type {} for send message queue of host, ignoring", to_send.Payload_case());
                } else {
                    LOG_WARNING("Unexpected message type {} for send message queue of client {}, ignoring", to_send.Payload_case(), client_id);
                }
                break;
            }
        } else if (protocol_state == HandshakeState::HS_WAITING_SHAKE_ACK) {
            if (to_send.Payload_case() != fp_proto::Message::kHsMsg) {
                continue;
            }
            parent_socket->MessageSend(std::move(to_send), endpoint);
        } else if (protocol_state == HandshakeState::HS_UNINITIALIZED) {}
    }
}

void ProtocolHandler::RetransmitWorker() {
    while (true) {
        std::unique_lock<std::mutex> lock(*shared_data_m);
        shared_data_cv->wait(lock, [this] { return waiting_for_ack->load() || !IsConnectionValid(); });

        if (!IsConnectionValid()) {
            break;
        }
        auto sleep_point = unacked_messages.begin()->second.last_send_ts + std::chrono::milliseconds(RTT_milliseconds * 2);
        lock.unlock();
        std::this_thread::sleep_until(sleep_point);
        lock.lock();
        DoSlowRetransmission();
    }
}

void ProtocolHandler::SendDataMessage_internal(fp_proto::Message& msg) {
    fp_proto::DataMessage& data_msg = *msg.mutable_data_msg();
    data_msg.set_sequence_number(send_sequence_number);
    if (data_msg.needs_ack()) {
        unacked_messages.emplace(send_sequence_number, SavedDataMessage(msg.data_msg(), clock::now()));
        waiting_for_ack->store(true);
    }
    send_sequence_number++;

    parent_socket->MessageSend(msg, endpoint);
}

void ProtocolHandler::DoSlowRetransmission() {
    for (auto&& [seqnum, saved_msg] : unacked_messages) {
        // fast retransmit logic
        bool needs_srt = saved_msg.NeedsSlowRetransmit(RTT_milliseconds);
        if (needs_srt) {
            // if doing a slow RT without fast RT, just say we did a fast to reduce bandwidth
            saved_msg.did_fast_retransmit = true;
            saved_msg.last_send_ts = clock::now();
            fp_proto::Message retransmitted_msg;
            *retransmitted_msg.mutable_data_msg() = saved_msg.msg;
            parent_socket->MessageSend(retransmitted_msg, endpoint);
            auto blocking_acks_it = std::find(blocking_acks.begin(), blocking_acks.end(), seqnum);
            // ensure we don't have to re-add the blocking ack
            if (blocking_acks_it == blocking_acks.end()) {
                blocking_acks.emplace_back(seqnum);
            }
        }
    }
}

void ProtocolHandler::OnAcknowledge(const fp_proto::AckMessage& msg) {
    // lock associated with both blocking_seq_number and waiting for ack, makes sure that blocking_seq_number doesn't get modified 
    std::unique_lock<std::mutex> lock(*shared_data_m);
    if (!unacked_messages.empty()) {
        auto remove_seqnum_it = unacked_messages.find(msg.sequence_ack());
        auto remove_blocking_ack_it = std::find(blocking_acks.begin(), blocking_acks.end(), msg.sequence_ack());
        if (remove_seqnum_it != unacked_messages.end()) {
            unacked_messages.erase(remove_seqnum_it);
            if (unacked_messages.empty()) {
                waiting_for_ack->store(false);
            }
        }
        if (remove_blocking_ack_it != blocking_acks.end()) {
            blocking_acks.erase(remove_blocking_ack_it);
        }
    }
    highest_acked_seqnum = std::max(highest_acked_seqnum, msg.sequence_ack());
}