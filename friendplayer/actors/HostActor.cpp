#include "actors/HostActor.h"

#include "actors/ClientManagerActor.h"
#include "common/Log.h"


HostActor::HostActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
    : ProtocolActor(actor_map, buffer_map, std::move(name)),
      video_buffer("VideoBuffer", VIDEO_FRAME_BUFFER, VIDEO_FRAME_SIZE),
      audio_buffer("AudioBuffer", AUDIO_FRAME_BUFFER, AUDIO_FRAME_SIZE) {}

void HostActor::OnInit(const std::optional<any_msg>& init_msg) {
    fp_network::Network send_handshake_msg;
    send_handshake_msg.mutable_hs_msg()->set_magic(0x46524E44504C5952ull);
    SendToSocket(send_handshake_msg);
    protocol_state = HandshakeState::HS_WAITING_SHAKE_ACK;
}

void HostActor::OnMessage(const any_msg& msg) {
    if (msg.Is<fp_actor::VideoDecoderReady>()) {
        // Tell host to start sending video frames
        fp_network::Network network_msg;
        network_msg.mutable_state_msg()->mutable_client_state()->set_state(fp_network::ClientState::READY_FOR_VIDEO);
        SendTo("Host", network_msg);
    } else if (msg.Is<fp_actor::VideoDataRequest>()) {
        std::string* video_frame = new std::string();
        video_buffer.GetFront(*video_frame);
        fp_actor::VideoData video_data;
        video_data.set_handle(buffer_map.Wrap(video_frame));
        SendTo("VideoDecodeActor", video_data);
    }
    ProtocolActor::OnMessage(msg);
}

bool HostActor::OnHandshakeMessage(const fp_network::Handshake& msg) {
    bool handshake_success = false;
    if (protocol_state == HandshakeState::HS_UNINITIALIZED) {
        LOG_WARNING("Received handshake before ready");
    } else if (protocol_state == HandshakeState::HS_WAITING_SHAKE_ACK) {
        if (msg.magic() == 0x46524E44504C5953ull) {
            protocol_state = HandshakeState::HS_READY;

            fp_network::Network send_handshake_msg;
            send_handshake_msg.mutable_hs_msg()->set_magic(0x46524E44504C5954ull);
            SendToSocket(send_handshake_msg);

            handshake_success = true;

            // Ask host for a PPS SPS
            fp_network::Network ready_msg;
            ready_msg.mutable_state_msg()->mutable_client_state()->set_state(fp_network::ClientState::READY_FOR_PPS_SPS_IDR);
            SendToSocket(ready_msg);
        } else {
            LOG_ERROR("Invalid handshake magic in state HS_WAITING_SHAKE_ACK: {}", msg.magic());
        }
    } else {
        LOG_WARNING("Got handshake message after finishing handshake");
    }
    return handshake_success;
}

void HostActor::OnStateMessage(const fp_network::State& msg) {

}

void HostActor::OnDataMessage(const fp_network::Data& msg) {
    if (msg.Payload_case() == fp_network::Data::kClientFrame) {
        LOG_ERROR("Got client frame from client side");
        return;
    }
    auto& h_msg = msg.host_frame();
    switch(h_msg.DataFrame_case()) {
        case fp_network::HostDataFrame::kVideo: {
            OnVideoFrame(h_msg);
        }
        break;
        case fp_network::HostDataFrame::kAudio: {
            OnAudioFrame(h_msg);
        }
        break;
    }
}

void HostActor::OnVideoFrame(const fp_network::HostDataFrame& msg) {
    fp_network::HostDataFrame frame;
    uint64_t handle = msg.video().data_handle();
    frame.CopyFrom(msg);
    frame.mutable_video()->clear_DataBacking();
    frame.mutable_video()->set_allocated_data(buffer_map.GetBuffer(handle));
    // COPIES DATA!!
    video_buffer.AddFrameChunk(frame);
    frame.mutable_video()->release_data();
    buffer_map.Decrement(handle);
}

void HostActor::OnAudioFrame(const fp_network::HostDataFrame& msg) {
    fp_network::HostDataFrame frame;
    uint64_t handle = msg.video().data_handle();
    frame.CopyFrom(msg);
    frame.mutable_audio()->clear_DataBacking();
    frame.mutable_audio()->set_allocated_data(buffer_map.GetBuffer(handle));
    // COPIES DATA!!
    audio_buffer.AddFrameChunk(frame);
    frame.mutable_audio()->release_data();
    buffer_map.Decrement(handle);
}

void HostActor::OnHostState(const fp_network::HostState& msg) {

}