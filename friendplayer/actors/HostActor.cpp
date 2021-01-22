#include "actors/HostActor.h"

#include "actors/CommonActorNames.h"
#include "common/Log.h"

HostActor::HostActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
    : ProtocolActor(actor_map, buffer_map, std::move(name)),
      video_buffer("VideoBuffer", VIDEO_FRAME_BUFFER, VIDEO_FRAME_SIZE),
      audio_buffer("AudioBuffer", AUDIO_FRAME_BUFFER, AUDIO_FRAME_SIZE),
      has_completed_video_frame(false),
      idr_send_timeout(-1) {}

void HostActor::OnInit(const std::optional<any_msg>& init_msg) {
    ProtocolActor::OnInit(init_msg);
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
        SendToSocket(network_msg);
    } else if (msg.Is<fp_actor::VideoDataRequest>()) {
        if (!has_completed_video_frame) {
            SendFrameToDecoder();
        } else {
            LOG_INFO("Frame was already done");
        }
        has_completed_video_frame = false;
    } else {
        ProtocolActor::OnMessage(msg);
    }
}

bool HostActor::OnHandshakeMessage(const fp_network::Handshake& msg) {
    bool handshake_success = false;
    if (protocol_state == HandshakeState::HS_UNINITIALIZED) {
        LOG_WARNING("Received handshake before ready");
    } else if (protocol_state == HandshakeState::HS_WAITING_SHAKE_ACK) {
        LOG_INFO("Received hs while waiting for ack");
        if (msg.magic() == 0x46524E44504C5953ull) {
            protocol_state = HandshakeState::HS_READY;
            LOG_INFO("Magic good, HS ready sending back");
            fp_network::Network send_handshake_msg;
            send_handshake_msg.mutable_hs_msg()->set_magic(0x46524E44504C5954ull);
            SendToSocket(send_handshake_msg);

            handshake_success = true;

            LOG_INFO("sending PPS_SPS ready");
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
    if (video_buffer.AddFrameChunk(frame)) {
        SendFrameToDecoder();
        has_completed_video_frame = true;
    }
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

void HostActor::SendFrameToDecoder() {
    std::string* video_frame = new std::string();
    bool was_full_frame;
    uint32_t size_to_decrypt = video_buffer.GetFront(*video_frame, was_full_frame);

    // Run decryption
    if (size_to_decrypt != 0 || video_frame->size() == 0 || !was_full_frame) {
        LOG_INFO("Asking for IDR {} {} {}", size_to_decrypt, video_frame->size(), !was_full_frame);
        idr_send_timeout = IDR_SEND_TIMEOUT;
    } else if (idr_send_timeout >= 0) {
        if (idr_send_timeout == 0) {
            fp_network::Network idr_req_msg;
            idr_req_msg.mutable_data_msg()->mutable_client_frame()->mutable_host_request()->set_type(fp_network::RequestToHost::SEND_IDR);
            SendToSocket(idr_req_msg);
            LOG_INFO("Requesting IDR from host");
        }
        idr_send_timeout--;
    }
    fp_actor::VideoData video_data;
    video_data.set_handle(buffer_map.Wrap(video_frame));
    SendTo(VIDEO_DECODER_ACTOR_NAME, video_data);
}