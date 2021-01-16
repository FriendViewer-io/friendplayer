#include "common/HostProtocolHandler.h"

#include "common/Socket.h"
#include "common/Log.h"
#include "common/ClientManager.h"
#include "common/HeartbeatManager.h"

#include <asio/buffer.hpp>

using namespace std::chrono_literals;

HostProtocolHandler::HostProtocolHandler(int id, asio_endpoint endpoint)
    : client_id(id),
      audio_enabled(true),
      keyboard_enabled(false),
      mouse_enabled(false),
      controller_enabled(true),
      parent_socket(nullptr),
      client_endpoint(endpoint),
      video_stream_point(0),
      audio_stream_point(0),
      audio_frame_num(0),
      video_frame_num(0),
      pps_sps_version(-1),
      async_send_worker(nullptr),
      async_recv_worker(nullptr),
      frame_window_start(0),
      send_sequence_number(0),
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

void HostProtocolHandler::KillAllThreads() {
    state->store(DISCONNECTED);

    send_message_queue_cv->notify_one();
    recv_message_queue_cv->notify_one();
    shared_data_cv->notify_one();

    async_send_worker->join();
    async_recv_worker->join();
    async_retransmit_worker->join();
}

void HostProtocolHandler::EnqueueRecvMessage(fp_proto::Message&& message) {
    std::lock_guard<std::mutex> lock(*recv_message_queue_m);
 
    recv_message_queue.emplace_back(std::move(message));
    recv_message_queue_cv->notify_one();
}

void HostProtocolHandler::EnqueueSendMessage(fp_proto::Message&& message) {
    std::lock_guard<std::mutex> lock(*send_message_queue_m);

    send_message_queue.emplace_back(std::move(message));
    send_message_queue_cv->notify_all();
}

void HostProtocolHandler::ClientRecvWorker() {
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
                LOG_INFO("Client {} sent handshake during stream", client_id);
                break;
            case fp_proto::Message::kDataMsg: {
                const fp_proto::DataMessage& data_msg = msg.data_msg();
                uint32_t seq_num = data_msg.sequence_number();
                
                std::lock_guard<std::mutex> sh_lock(*shared_data_m);
                if (blocking_acks.size() > 0) {
                    LOG_INFO("Dropping DataMessage for client {}, blocking on slow resend; seq num={}", client_id, seq_num);
                    continue;
                }
                
                // Ack packet, stream is functioning normally
                fp_proto::Message ack;
                *ack.mutable_ack_msg() = fp_proto::AckMessage();
                ack.mutable_ack_msg()->set_sequence_ack(seq_num);
                EnqueueSendMessage(std::move(ack));
                if (seq_num < frame_window_start) {
                    LOG_INFO("Ack back to client {} was dropped, ignoring DataMessage; seq num={}, window={}", client_id, seq_num, frame_window_start);
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
                    heartbeat_manager->UpdateClient(client_id);
                    LOG_INFO("Got heartbeat response, RTT={}", RTT_milliseconds);
                } else {
                    // Reply with the same heartbeat
                    msg.mutable_hb_msg()->set_is_response(true);
                    EnqueueSendMessage(std::move(msg));
                }
                break;
            }
        }

        process_queue_cons();
        if (!reordered_msg_queue.empty()) {
            // window is a bit behind, dropped packets perhaps?
            while (reordered_msg_queue.back().sequence_number() > frame_window_start + RECV_DROP_WINDOW) {
                frame_window_start = reordered_msg_queue.front().sequence_number();
                process_queue_cons();
            }
        }
    }
    std::unique_lock<std::mutex> lock(*recv_message_queue_m);
    LOG_INFO("Message worker for client {} exiting, timeout={}", client_id, IsConnectionValid());
}

void HostProtocolHandler::ClientSendWorker() {
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
                parent_socket->MessageSend(to_send, client_endpoint);
                break;
            case fp_proto::Message::kDataMsg:
                SendDataMessage_internal(to_send);
                break;
            case fp_proto::Message::kHbMsg:
                if (!to_send.hb_msg().is_response()) {
                    auto cur_time = clock::now();
                    to_send.mutable_hb_msg()->set_timestamp(cur_time.time_since_epoch().count());
                }
                parent_socket->MessageSend(to_send, client_endpoint);
                break;
            default:
                LOG_WARNING("Unexpected message type {} for send message queue of client {}, ignoring", to_send.Payload_case(), client_id);
                break;
            }
        } else if (protocol_state == HandshakeState::HS_WAITING_SHAKE_ACK) {
            if (to_send.Payload_case() != fp_proto::Message::kHsMsg) {
                continue;
            }
            parent_socket->MessageSend(std::move(to_send), client_endpoint);
        } else if (protocol_state == HandshakeState::HS_UNINITIALIZED) {
            // Ignore everything
        }
    }
}

void HostProtocolHandler::ClientRetransmitWorker() {
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

void HostProtocolHandler::SendDataMessage_internal(fp_proto::Message& msg) {
    fp_proto::DataMessage& data_msg = *msg.mutable_data_msg();
    data_msg.set_sequence_number(send_sequence_number);
    if (data_msg.needs_ack()) {
        unacked_messages.emplace(send_sequence_number, SavedDataMessage(msg.data_msg(), clock::now()));
        waiting_for_ack->store(true);
    }
    send_sequence_number++;

    parent_socket->MessageSend(msg, client_endpoint);
}

void HostProtocolHandler::DoSlowRetransmission() {
    for (auto&& [seqnum, saved_msg] : unacked_messages) {
        // fast retransmit logic
        bool needs_srt = saved_msg.NeedsSlowRetransmit(RTT_milliseconds);
        if (needs_srt) {
            // if doing a slow RT without fast RT, just say we did a fast to reduce bandwidth
            saved_msg.did_fast_retransmit = true;
            saved_msg.last_send_ts = clock::now();
            fp_proto::Message retransmitted_msg;
            *retransmitted_msg.mutable_data_msg() = saved_msg.msg;
            parent_socket->MessageSend(retransmitted_msg, client_endpoint);
            auto blocking_acks_it = std::find(blocking_acks.begin(), blocking_acks.end(), seqnum);
            // ensure we don't have to re-add the blocking ack
            if (blocking_acks_it == blocking_acks.end()) {
                blocking_acks.emplace_back(seqnum);
            }
        }
    }
}

bool HostProtocolHandler::DoHandshake() {
    bool got_msg;

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
    if (incoming_msg.hs_msg().magic() != 0x46524E44504C5952ull) {
        return false;
    }

    protocol_state = HS_WAITING_SHAKE_ACK;

    fp_proto::Message handshake_response;
    *handshake_response.mutable_hs_msg() = fp_proto::HandshakeMessage();
    handshake_response.mutable_hs_msg()->set_magic(0x46524E44504C5953ull);

    EnqueueSendMessage(std::move(handshake_response));

    got_msg = recv_message_queue_cv->wait_for(lock, 5s, [this] { return !recv_message_queue.empty(); });
    if (!got_msg) {
        return false;
    }
    incoming_msg = std::move(recv_message_queue.front());
    recv_message_queue.pop_front();
    if (incoming_msg.Payload_case() != fp_proto::Message::kHsMsg) {
        return false;
    }
    if (incoming_msg.hs_msg().magic() != 0x46524E44504C5954ull) {
        return false;
    }
    protocol_state = HS_READY;
    return true;
}

void HostProtocolHandler::OnAcknowledge(const fp_proto::AckMessage& msg) {
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

void HostProtocolHandler::OnClientState(const fp_proto::ClientDataFrame& msg) {
    const auto& cl_state = msg.client_state();
    LOG_INFO("Client state = {}", static_cast<int>(cl_state.state()));
    switch(cl_state.state()) {
        case fp_proto::ClientState::READY_FOR_PPS_SPS_IDR: {
            Transition(ClientState::WAITING_FOR_VIDEO);
            parent_socket->SetNeedIDR(true);
        }
        break;
        case fp_proto::ClientState::READY_FOR_VIDEO: {
            Transition(ClientState::READY);
        }
        break;
        case fp_proto::ClientState::DISCONNECTING: {
            heartbeat_manager->UnregisterClient(client_id);
            std::thread async_destroy_client([] (std::shared_ptr<ClientManager> client_mgr, int id) {
                client_mgr->DestroyClient(id);
            }, client_manager, client_id);
            async_destroy_client.detach();
        }
        break;
        default: {
            LOG_ERROR("Client sent unknown state: {}", static_cast<int>(cl_state.state()));
        }
        break;
    }
}

void HostProtocolHandler::OnHostRequest(const fp_proto::ClientDataFrame& msg) {
    const auto& request = msg.host_request();
    LOG_INFO("Client request to host = {}", static_cast<int>(request.type()));
    switch(request.type()) {
        case fp_proto::RequestToHost::SEND_IDR: {
            parent_socket->SetNeedIDR(true);
        }
        break;
        case fp_proto::RequestToHost::MUTE_AUDIO: {
            SetAudio(false);
        }
        break;
        case fp_proto::RequestToHost::PLAY_AUDIO: {
            SetAudio(true);
        }
    }
}

void HostProtocolHandler::OnDataFrame(const fp_proto::DataMessage& msg) {
    switch(msg.Payload_case()){
        case fp_proto::DataMessage::kClientFrame: {
            const fp_proto::ClientDataFrame& c_msg = msg.client_frame();
            switch(c_msg.DataFrame_case()) {
                case fp_proto::ClientDataFrame::kKeyboard:
                    OnKeyboardFrame(c_msg);
                    break;
                case fp_proto::ClientDataFrame::kMouse:
                    OnMouseFrame(c_msg);
                    break;
                case fp_proto::ClientDataFrame::kController:
                    OnControllerFrame(c_msg);
                    break;
                case fp_proto::ClientDataFrame::kHostRequest:
                    OnHostRequest(c_msg);
                    break;
                case fp_proto::ClientDataFrame::kClientState:
                    OnClientState(c_msg);
                    break;
            }
        }
        break;
        case fp_proto::DataMessage::kHostFrame: {
            LOG_WARNING("Host frame recievied from client side...???");
        }
        break;
    }
}

void HostProtocolHandler::OnKeyboardFrame(const fp_proto::ClientDataFrame& msg) {

}

void HostProtocolHandler::OnMouseFrame(const fp_proto::ClientDataFrame& msg) {

}

void HostProtocolHandler::OnControllerFrame(const fp_proto::ClientDataFrame& msg) {

}

void HostProtocolHandler::SendAudioData(const std::vector<uint8_t>& data) {
    if (state->load() != ClientState::READY) {
        return;
    }

    for (size_t chunk_offset = 0; chunk_offset < data.size(); chunk_offset += MAX_DATA_CHUNK) {
        fp_proto::Message msg;
        fp_proto::DataMessage& data_msg = *msg.mutable_data_msg();
        data_msg.set_needs_ack(false);
        fp_proto::HostDataFrame& host_frame = *data_msg.mutable_host_frame();
        
        const size_t chunk_end = std::min(chunk_offset + MAX_DATA_CHUNK, data.size());
        
        host_frame.set_frame_size(data.size());
        host_frame.set_frame_num(audio_frame_num);
        host_frame.set_stream_point(audio_stream_point);
        host_frame.mutable_audio()->set_chunk_offset(static_cast<uint32_t>(chunk_offset));
        host_frame.mutable_audio()->set_data(data.data() + chunk_offset, chunk_end - chunk_offset);

        EnqueueSendMessage(std::move(msg));
    }

    audio_stream_point += data.size();
    audio_frame_num++;
}

void HostProtocolHandler::SendVideoData(const std::vector<uint8_t>& data, fp_proto::VideoFrame_FrameType type) {
    // Only send if ready or if 
    if (state->load() != ClientState::READY
        && (state->load() != ClientState::WAITING_FOR_VIDEO || type != fp_proto::VideoFrame::IDR)) {
        return;
    }
    
    std::vector<uint8_t> buffered_data;

    bool sending_pps_sps = false;
    // Handle PPS SPS sending
    if (pps_sps_version != parent_socket->GetPPSSPSVersion()) {
        sending_pps_sps = true;
        buffered_data = parent_socket->GetPPSSPS();
        buffered_data.insert(buffered_data.end(), data.begin(), data.end());
        pps_sps_version = parent_socket->GetPPSSPSVersion();
    } else {
        buffered_data = data;
    }

    for (size_t chunk_offset = 0; chunk_offset < buffered_data.size(); chunk_offset += MAX_DATA_CHUNK) {
        fp_proto::Message msg;
        fp_proto::DataMessage& data_msg = *msg.mutable_data_msg();
        data_msg.set_needs_ack(sending_pps_sps);
        sending_pps_sps = false;
        fp_proto::HostDataFrame& host_frame = *data_msg.mutable_host_frame();
        
        const size_t chunk_end = std::min(chunk_offset + MAX_DATA_CHUNK, buffered_data.size());
        
        host_frame.set_frame_size(buffered_data.size());
        host_frame.set_frame_num(video_frame_num);
        host_frame.set_stream_point(video_stream_point);
        host_frame.mutable_video()->set_chunk_offset(static_cast<uint32_t>(chunk_offset));
        host_frame.mutable_video()->set_data(buffered_data.data() + chunk_offset, chunk_end - chunk_offset);
        host_frame.mutable_video()->set_frame_type(type);

        EnqueueSendMessage(std::move(msg));
    }
    
    video_stream_point += buffered_data.size();
    video_frame_num++;
}