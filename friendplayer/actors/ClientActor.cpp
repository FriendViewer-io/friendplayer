#include "actors/ClientActor.h"

#include "actors/ClientManagerActor.h"
#include "common/Log.h"
#include "protobuf/actor_messages.pb.h"
#include "protobuf/network_messages.pb.h"


ClientActor::ClientActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
    : ProtocolActor(actor_map, buffer_map, std::move(name)),
      audio_enabled(true),
      keyboard_enabled(false),
      mouse_enabled(false),
      controller_enabled(true),
      video_stream_point(0),
      audio_stream_point(0),
      audio_frame_num(0),
      video_frame_num(0),
      stream_state(StreamState::UNINITIALIZED)
      //input_streamer()
      { }

void ClientActor::OnMessage(const any_msg& msg) {
    if (protocol_state != HandshakeState::HS_READY) {
        ProtocolActor::OnMessage(msg);
        return;
    }
    if (msg.Is<fp_actor::VideoData>()) {
        // INSERT ENCRYPTION HERE!!
        // INSERT ENCRYPTION HERE!!

        fp_actor::VideoData data_msg;
        msg.UnpackTo(&data_msg);
        
        std::string* handle_data = buffer_map.GetBuffer(data_msg.handle());
        fp_network::Network out_msg;
        *out_msg.mutable_data_msg() = fp_network::Data();
        out_msg.mutable_data_msg()->set_sequence_number(sequence_number);
        *out_msg.mutable_data_msg()->mutable_host_frame() = fp_network::HostDataFrame();
        out_msg.mutable_data_msg()->mutable_host_frame()->set_frame_num(video_frame_num);
        out_msg.mutable_data_msg()->mutable_host_frame()->set_frame_size(handle_data->size());
        out_msg.mutable_data_msg()->mutable_host_frame()->set_stream_point(video_stream_point);
        *out_msg.mutable_data_msg()->mutable_host_frame()->mutable_video() = fp_network::VideoFrame();
        out_msg.mutable_data_msg()->mutable_host_frame()->mutable_video()->set_data_handle(data_msg.handle());
        out_msg.mutable_data_msg()->mutable_host_frame()->mutable_video()->set_chunk_offset(0);
        
        if (data_msg.type() == fp_actor::VideoData::PPS_SPS) {
            out_msg.mutable_data_msg()->mutable_host_frame()->mutable_video()->set_frame_type(fp_network::VideoFrame::PPS_SPS);
            out_msg.mutable_data_msg()->set_needs_ack(true);
        } else if (data_msg.type() == fp_actor::VideoData::IDR) {
            out_msg.mutable_data_msg()->mutable_host_frame()->mutable_video()->set_frame_type(fp_network::VideoFrame::IDR);
        } else {
            out_msg.mutable_data_msg()->mutable_host_frame()->mutable_video()->set_frame_type(fp_network::VideoFrame::NORMAL);
        }
        if ((stream_state == StreamState::WAITING_FOR_VIDEO && data_msg.type() == fp_actor::VideoData::PPS_SPS)
            || stream_state == StreamState::READY) {
            sequence_number += ((handle_data->size() + MAX_DATA_CHUNK - 1) / MAX_DATA_CHUNK);
            video_stream_point += handle_data->size();
            video_frame_num++;

            buffer_map.Increment(data_msg.handle());
            SendToSocket(out_msg);
        }
        buffer_map.Decrement(data_msg.handle());
    }
    ProtocolActor::OnMessage(msg);
}

bool ClientActor::OnHandshakeMessage(const fp_network::Handshake& msg) {
    bool handshake_success = false;
    if (protocol_state == HandshakeState::HS_UNINITIALIZED) {
        if (msg.magic() == 0x46524E44504C5952ull) {
            fp_network::Network send_handshake_msg;
            send_handshake_msg.mutable_hs_msg()->set_magic(0x46524E44504C5953ull);
            SendToSocket(send_handshake_msg);
            protocol_state = HandshakeState::HS_WAITING_SHAKE_ACK;
            handshake_success = true;
        } else {
            LOG_ERROR("Invalid handshake magic in state HS_UNINITIALIZED: {}", msg.magic());
        }
    } else if (protocol_state == HandshakeState::HS_WAITING_SHAKE_ACK) {
        if (msg.magic() == 0x46524E44504C5954ull) {
            protocol_state = HandshakeState::HS_READY;

            handshake_success = true;
        } else {
            LOG_ERROR("Invalid handshake magic in state HS_WAITING_SHAKE_ACK: {}", msg.magic());
        }
    } else {
        LOG_WARNING("Got handshake message after finishing handshake");
    }
    return handshake_success;
}

void ClientActor::OnStateMessage(const fp_network::State& msg) {
    auto& state = msg.client_state();
    switch (state.state()) {
        case fp_network::ClientState::READY_FOR_PPS_SPS_IDR: {
            fp_actor::SpecialFrameRequest req;
            req.set_type(fp_actor::SpecialFrameRequest::PPS_SPS);
            SendTo("VideoEncodeActor", req);
            stream_state = StreamState::WAITING_FOR_VIDEO;
        }
        break;
        case fp_network::ClientState::READY_FOR_VIDEO: {
            stream_state = StreamState::READY;
        }
        break;
        case fp_network::ClientState::DISCONNECTING: {
            // deal with me
        }
        break;
    }
}

void ClientActor::OnDataMessage(const fp_network::Data& msg) {
    if (msg.Payload_case() == fp_network::Data::kHostFrame) {
        LOG_ERROR("Got host frame from host side");
        return;
    }
    auto& c_msg = msg.client_frame();
    switch(c_msg.DataFrame_case()) {
        case fp_network::ClientDataFrame::kKeyboard:
            OnKeyboardFrame(c_msg.keyboard());
            break;
        case fp_network::ClientDataFrame::kMouse:
            OnMouseFrame(c_msg.mouse());
            break;
        case fp_network::ClientDataFrame::kController:
            OnControllerFrame(c_msg.controller());
            break;
        case fp_network::ClientDataFrame::kHostRequest:
            OnHostRequest(c_msg.host_request());
            break;
    }
}

void ClientActor::OnHostRequest(const fp_network::RequestToHost& msg) {
    LOG_INFO("Client request to host = {}", static_cast<int>(msg.type()));
    switch(msg.type()) {
        case fp_network::RequestToHost::SEND_IDR: {
            //host_socket->SetNeedIDR(true);
        }
        break;
        case fp_network::RequestToHost::MUTE_AUDIO: {
            //SetAudio(false);
        }
        break;
        case fp_network::RequestToHost::PLAY_AUDIO: {
            //SetAudio(true);
        }
    }
}

void ClientActor::OnKeyboardFrame(const fp_network::KeyboardFrame& msg) {

}

void ClientActor::OnMouseFrame(const fp_network::MouseFrame& msg) {

}

void ClientActor::OnControllerFrame(const fp_network::ControllerFrame& msg) {
    /*const auto& frame = msg.controller();
    if(!input_streamer.is_virtual_controller_registered()) {
        //TODO: add protocol logic to do this? maybe this won't work with vigem client handles
        input_streamer.RegisterVirtualController();
    }
    input_streamer.UpdateVirtualController(frame);*/
}