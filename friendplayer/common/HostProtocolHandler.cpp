#include "common/HostProtocolHandler.h"

#include "common/Socket.h"
#include "common/Log.h"
#include "common/ProtocolManager.h"

#include <asio/buffer.hpp>

using namespace std::chrono_literals;

HostProtocolHandler::HostProtocolHandler(int id, asio_endpoint endpoint)
    : ProtocolHandler(id, endpoint),
      audio_enabled(true),
      keyboard_enabled(false),
      mouse_enabled(false),
      controller_enabled(true),
      video_stream_point(0),
      audio_stream_point(0),
      audio_frame_num(0),
      video_frame_num(0),
      pps_sps_version(-1),
      input_streamer() { }

bool HostProtocolHandler::DoHandshake() {
    bool got_msg;

    std::unique_lock<std::mutex> lock(*recv_message_queue_m);
    LOG_INFO("Waiting for first handshake");
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

    protocol_state = HandshakeState::HS_WAITING_SHAKE_ACK;

    fp_proto::Message handshake_response;
    *handshake_response.mutable_hs_msg() = fp_proto::HandshakeMessage();
    handshake_response.mutable_hs_msg()->set_magic(0x46524E44504C5953ull);
    LOG_INFO("Sending back first handshake");
    EnqueueSendMessage(std::move(handshake_response));

    LOG_INFO("Waiting for second handshake");
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
    protocol_state = HandshakeState::HS_READY;
    LOG_INFO("Done with handshake");
    return true;
}

void HostProtocolHandler::OnClientState(const fp_proto::ClientState& cl_state) {
    LOG_INFO("Client state = {}", static_cast<int>(cl_state.state()));
    switch(cl_state.state()) {
        case fp_proto::ClientState::READY_FOR_PPS_SPS_IDR: {
            Transition(StreamState::WAITING_FOR_VIDEO);
            host_socket->SetNeedIDR(true);
        }
        break;
        case fp_proto::ClientState::READY_FOR_VIDEO: {
            Transition(StreamState::READY);
        }
        break;
        case fp_proto::ClientState::DISCONNECTING: {
            Transition(StreamState::DISCONNECTED);
        }
        break;
        default: {
            LOG_ERROR("Client sent unknown state: {}", static_cast<int>(cl_state.state()));
        }
        break;
    }
}

void HostProtocolHandler::OnStateMessage(const fp_proto::StateMessage& msg) {
    switch (msg.State_case()) {
    case fp_proto::StateMessage::kClientState:
        OnClientState(msg.client_state());
        break;
    case fp_proto::StateMessage::kHostState:
        LOG_WARNING("Received HostState message from client, ignoring");
        break;
    }
}

void HostProtocolHandler::OnHostRequest(const fp_proto::ClientDataFrame& msg) {
    const auto& request = msg.host_request();
    LOG_INFO("Client request to host = {}", static_cast<int>(request.type()));
    switch(request.type()) {
        case fp_proto::RequestToHost::SEND_IDR: {
            host_socket->SetNeedIDR(true);
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

void HostProtocolHandler::OnControllerFrame(const fp_proto::ClientDataFrame& msg) 
{
    const auto& frame = msg.controller();
    if(!input_streamer.is_virtual_controller_registered()) {
        //TODO: add protocol logic to do this? maybe this won't work with vigem client handles
        input_streamer.RegisterVirtualController();
    }
    input_streamer.UpdateVirtualController(frame);
}

void HostProtocolHandler::SendAudioData(const std::vector<uint8_t>& data) {
    if (state->load() != StreamState::READY) {
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
    if (state->load() != StreamState::READY
        && (state->load() != StreamState::WAITING_FOR_VIDEO || type != fp_proto::VideoFrame::IDR)) {
        return;
    }
    
    std::vector<uint8_t> buffered_data;

    bool sending_pps_sps = false;
    // Handle PPS SPS sending
    if (pps_sps_version != host_socket->GetPPSSPSVersion()) {
        sending_pps_sps = true;
        buffered_data = host_socket->GetPPSSPS();
        buffered_data.insert(buffered_data.end(), data.begin(), data.end());
        pps_sps_version = host_socket->GetPPSSPSVersion();
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