#include "common/ClientProtocolHandler.h"

#include "common/Socket.h"
#include "common/Log.h"

#include <asio/buffer.hpp>

using namespace std::chrono_literals;

ClientProtocolHandler::ClientProtocolHandler()
    : parent_socket(nullptr),
      async_send_worker(nullptr),
      async_recv_worker(nullptr),
      frame_window_start(0),
      send_sequence_number(0),
      video_buffer("VideoBuffer", VIDEO_FRAME_BUFFER, VIDEO_FRAME_SIZE),
      audio_buffer("AudioBuffer", AUDIO_FRAME_BUFFER, AUDIO_FRAME_SIZE),
      protocol_state(HS_UNINITIALIZED),
      highest_acked_seqnum(0) {
    state = std::make_unique<std::atomic<ClientState>>(ClientState::UNINITIALIZED);

    send_message_queue_m = std::make_unique<std::mutex>();
    send_message_queue_cv = std::make_unique<std::condition_variable>();

    recv_message_queue_m = std::make_unique<std::mutex>();
    recv_message_queue_cv = std::make_unique<std::condition_variable>();

    shared_data_m = std::make_unique<std::mutex>();
    shared_data_cv = std::make_unique<std::condition_variable>();
    waiting_for_ack = std::make_unique<std::atomic_bool>(false);
}

bool ClientProtocolHandler::DoHandshake() {
    bool got_msg;

    fp_proto::Message handshake_request;
    *handshake_request.mutable_hs_msg() = fp_proto::HandshakeMessage();
    handshake_request.mutable_hs_msg()->set_magic(0x46524E44504C5952ull);

    EnqueueSendMessage(std::move(handshake_request));

    std::unique_lock<std::mutex> lock(*recv_message_queue_m);
    got_msg = recv_message_queue_cv->wait_for(lock, 5s, [this] { return !recv_message_queue.empty(); });
    if (!got_msg) {
        return false;
    }
    fp_proto::Message incoming_msg = std::move(recv_message_queue.front());
    recv_message_queue.pop_front();
    if (incoming_msg.Payload_case() != fp_proto::Message::kHsMsg) {
        return false;
    }
    if (incoming_msg.hs_msg().magic() != 0x46524E44504C5953ull) {
        return false;
    }

    *handshake_request.mutable_hs_msg() = fp_proto::HandshakeMessage();
    handshake_request.mutable_hs_msg()->set_magic(0x46524E44504C5954ull);

    EnqueueSendMessage(std::move(handshake_request));

    return true;
}

void ClientProtocolHandler::ClientRecvWorker() {
    // Do the handshake
    // Setup keys
    std::list<fp_proto::DataMessage> reordered_msg_queue;
    
    const auto process_queue_cons = [this, &reordered_msg_queue] {
        while (!reordered_msg_queue.empty() && (reordered_msg_queue.front().sequence_number() == frame_window_start)) {
            //process p_msg
            fp_proto::DataMessage p_msg = std::move(reordered_msg_queue.front());
            OnDataFrame(p_msg);
            reordered_msg_queue.pop_front();
            //handle this frame
            LOG_INFO("Setting frame_window_start to {} + 1", p_msg.sequence_number());
            frame_window_start = p_msg.sequence_number() + 1;
        }
    };
    
    if (!DoHandshake()) {
        LOG_INFO("Dropping client, failed to pass handshake");
        state->store(DISCONNECTED);
        return;
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
                LOG_INFO("Received handshake during stream");
                break;
            case fp_proto::Message::kDataMsg: {
                const fp_proto::DataMessage& data_msg = msg.data_msg();
                uint32_t seq_num = data_msg.sequence_number();
                
                std::lock_guard<std::mutex> sh_lock(*shared_data_m);
                if (blocking_acks.size() > 0) {
                    LOG_INFO("Dropping DataMsg; seq num={}, waiting for ack", seq_num);
                    continue;
                }

                fp_proto::Message ack;
                *ack.mutable_ack_msg() = fp_proto::AckMessage();
                ack.mutable_ack_msg()->set_sequence_ack(seq_num);
                EnqueueSendMessage(std::move(ack));

                if (seq_num < frame_window_start) {
                    LOG_INFO("Ack back to server was dropped, resending ack; seq num={}, window={}", seq_num, frame_window_start);
                    continue;
                }
                //place in reordered queue for processing on 2nd loop
                auto it = reordered_msg_queue.begin();
                for(; it != reordered_msg_queue.end(); it++) {   
                    if (seq_num < it->sequence_number()) {
                        break;
                    }
                }
                reordered_msg_queue.emplace(it, data_msg);
                LOG_TRACE("Got data frame, seqnum={}", data_msg.sequence_number());
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
                    LOG_INFO("Got heartbeat response, RTT={}", RTT_milliseconds);
                } else {
                    msg.mutable_hb_msg()->set_is_response(true);
                    EnqueueSendMessage(std::move(msg));
                }
                break;
            }
        }

        process_queue_cons();
        // window is a bit behind, dropped packets perhaps?
        while (!reordered_msg_queue.empty() && reordered_msg_queue.back().sequence_number() > frame_window_start + RECV_DROP_WINDOW) {
            frame_window_start = reordered_msg_queue.front().sequence_number();
            LOG_CRITICAL("Updated frame window start={}", frame_window_start);
            process_queue_cons();
        }
    }
    // std::unique_lock<std::mutex> lock(*recv_message_queue_m);
    // LOG_INFO("Message worker for client {} exiting, timeout={}", client_id, IsConnectionValid());
}

void ClientProtocolHandler::ClientSendWorker() {
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
            case fp_proto::Message::kAckMsg:
                parent_socket->MessageSend(to_send);
                break;
            case fp_proto::Message::kDataMsg:
                SendDataMessage_internal(to_send);
                break;
            case fp_proto::Message::kHbMsg:
                if (!to_send.hb_msg().is_response()) {
                    auto cur_time = clock::now();
                    to_send.mutable_hb_msg()->set_timestamp(cur_time.time_since_epoch().count());
                }
                parent_socket->MessageSend(to_send);
                break;
            default:
                LOG_WARNING("Unexpected message type {} for send message queue, ignoring", to_send.Payload_case());
                break;
            }
        } else if (protocol_state == HandshakeState::HS_WAITING_SHAKE_ACK) {
            if (to_send.Payload_case() != fp_proto::Message::kHsMsg) {
                continue;
            }
            parent_socket->MessageSend(std::move(to_send));
            protocol_state = HS_READY;
        } else if (protocol_state == HandshakeState::HS_UNINITIALIZED) {
            if (to_send.Payload_case() != fp_proto::Message::kHsMsg) {
                continue;
            }
            parent_socket->MessageSend(std::move(to_send));
            protocol_state = HS_WAITING_SHAKE_ACK;
        }
    }
}

void ClientProtocolHandler::ClientRetransmitWorker() {
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

void ClientProtocolHandler::SendDataMessage_internal(fp_proto::Message& msg) {
    fp_proto::DataMessage& data_msg = *msg.mutable_data_msg();
    data_msg.set_sequence_number(send_sequence_number);
    if (data_msg.needs_ack()) {
        unacked_messages.emplace(send_sequence_number, SavedDataMessage(msg.data_msg(), clock::now()));
        waiting_for_ack->store(true);
    }
    send_sequence_number++;

    parent_socket->MessageSend(msg);
}

void ClientProtocolHandler::DoSlowRetransmission() {
    for (auto&& [seqnum, saved_msg] : unacked_messages) {
        // fast retransmit logic
        bool needs_srt = saved_msg.NeedsSlowRetransmit(RTT_milliseconds);
        if (needs_srt) {
            // if doing a slow RT without fast RT, just say we did a fast to reduce bandwidth
            saved_msg.did_fast_retransmit = true;
            saved_msg.last_send_ts = clock::now();
            fp_proto::Message retransmitted_msg;
            *retransmitted_msg.mutable_data_msg() = saved_msg.msg;
            parent_socket->MessageSend(retransmitted_msg);
            auto blocking_acks_it = std::find(blocking_acks.begin(), blocking_acks.end(), seqnum);
            // ensure we don't have to re-add the blocking ack
            if (blocking_acks_it == blocking_acks.end()) {
                blocking_acks.emplace_back(seqnum);
            }
        }
    }
}

void ClientProtocolHandler::EnqueueRecvMessage(fp_proto::Message&& message) {
    std::lock_guard<std::mutex> lock(*recv_message_queue_m);
 
    recv_message_queue.emplace_back(std::move(message));
    recv_message_queue_cv->notify_one();
}

void ClientProtocolHandler::EnqueueSendMessage(fp_proto::Message&& message) {
    std::lock_guard<std::mutex> lock(*send_message_queue_m);

    send_message_queue.emplace_back(std::move(message));
    send_message_queue_cv->notify_all();
}

void ClientProtocolHandler::OnDataFrame(const fp_proto::DataMessage& msg) {
    switch(msg.Payload_case()){
        case fp_proto::DataMessage::kClientFrame: {
            LOG_WARNING("Client frame recievied from host side...???");
        }
        break;
        case fp_proto::DataMessage::kHostFrame: {
            const fp_proto::HostDataFrame& c_msg = msg.host_frame();
            switch(c_msg.DataFrame_case()) {
                case fp_proto::HostDataFrame::kAudio:
                    OnAudioFrame(c_msg);
                    break;
                case fp_proto::HostDataFrame::kVideo:
                    OnVideoFrame(c_msg);
                    break;
            }
        }
        break;
    }
}

void ClientProtocolHandler::OnAcknowledge(const fp_proto::AckMessage& msg) {
    //lock associated with both blocking_seq_number and waiting for ack, makes sure that blocking_seq_number doesn't get modified 
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
    LOG_INFO("Received acknowledge, highest seqnum={}", highest_acked_seqnum);
}

void ClientProtocolHandler::OnVideoFrame(const fp_proto::HostDataFrame& msg) {
    video_buffer.AddFrameChunk(msg);
}

void ClientProtocolHandler::OnAudioFrame(const fp_proto::HostDataFrame& msg) {
    audio_buffer.AddFrameChunk(msg);
}

uint32_t ClientProtocolHandler::GetVideoFrame(RetrievedBuffer& buf_in) {
    return video_buffer.GetFront(buf_in);
}

uint32_t ClientProtocolHandler::GetAudioFrame(RetrievedBuffer& buf_in) {
    return audio_buffer.GetFront(buf_in);
    // Run decryption
}