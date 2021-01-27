#include "actors/ClientActor.h"

#include "actors/CommonActorNames.h"

#include "common/Log.h"
#include "protobuf/actor_messages.pb.h"
#include "protobuf/network_messages.pb.h"

#include <fmt/format.h>


ClientActor::ClientActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
    : ProtocolActor(actor_map, buffer_map, std::move(name)),
      audio_enabled(true),
      keyboard_enabled(false),
      mouse_enabled(false),
      controller_enabled(true)
      //input_streamer()
      { }

void ClientActor::OnInit(const std::optional<any_msg>& init_msg) {
    if (init_msg) {
        if (init_msg->Is<fp_actor::ClientProtocolInit>()) {
            fp_actor::ClientProtocolInit client_init_msg;
            init_msg->UnpackTo(&client_init_msg);

            video_streams.resize(client_init_msg.video_stream_count());
            for (int i = 0; i < client_init_msg.video_stream_count(); i++) {
                video_streams[i].actor_name = fmt::format(VIDEO_ENCODER_ACTOR_NAME_FORMAT, i);
            }
            audio_streams.resize(client_init_msg.audio_stream_count());
            for (int i = 0; i < client_init_msg.audio_stream_count(); i++) {
                audio_streams[i].actor_name = fmt::format(AUDIO_ENCODER_ACTOR_NAME_FORMAT, i);
            }
            // base class init
            any_msg base_msg;
            base_msg.PackFrom(client_init_msg.base_init());
            ProtocolActor::OnInit(base_msg);
        } else {
            LOG_CRITICAL("ClientActor being initialized with unhandled init_msg type {}!", init_msg->type_url());
        }
    } else {
        LOG_CRITICAL("ClientActor being initialized with empty init_msg!");
    }
}

void ClientActor::OnMessage(const any_msg& msg) {
    if (protocol_state != HandshakeState::HS_READY) {
        if (msg.Is<fp_actor::VideoData>()) {
            fp_actor::VideoData data_msg;
            msg.UnpackTo(&data_msg);
            buffer_map.Decrement(data_msg.handle());
        } else if (msg.Is<fp_actor::AudioData>()) {
            fp_actor::AudioData data_msg;
            msg.UnpackTo(&data_msg);
            buffer_map.Decrement(data_msg.handle());
        } else {
            ProtocolActor::OnMessage(msg);
        }
        return;
    }
    if (msg.Is<fp_actor::VideoData>()) {
        fp_actor::VideoData data_msg;
        msg.UnpackTo(&data_msg);
        OnVideoData(data_msg);
    } else if (msg.Is<fp_actor::AudioData>()) {
        fp_actor::AudioData data_msg;
        msg.UnpackTo(&data_msg);
        OnAudioData(data_msg);
    } else {
        // pass it up to parent class
        ProtocolActor::OnMessage(msg);
    }
}

void ClientActor::OnVideoData(const fp_actor::VideoData& data_msg) {
    // INSERT ENCRYPTION HERE!!
    // INSERT ENCRYPTION HERE!!

    uint32_t stream_num = data_msg.stream_num();

    if (stream_num > video_streams.size()) {
        LOG_WARNING("Received video data from invalid stream number {}", stream_num);
        return;
    }

    StreamInfo& stream_info = video_streams[stream_num];

    if ((stream_info.stream_state == StreamState::WAITING_FOR_VIDEO && data_msg.type() == fp_actor::VideoData::PPS_SPS)
        || stream_info.stream_state == StreamState::READY) {
        std::string* handle_data = buffer_map.GetBuffer(data_msg.handle());
        fp_network::Network out_msg;
        *out_msg.mutable_data_msg() = fp_network::Data();
        out_msg.mutable_data_msg()->set_sequence_number(sequence_number);
        *out_msg.mutable_data_msg()->mutable_host_frame() = fp_network::HostDataFrame();
        out_msg.mutable_data_msg()->mutable_host_frame()->set_frame_num(stream_info.frame_num);
        out_msg.mutable_data_msg()->mutable_host_frame()->set_frame_size(handle_data->size());
        out_msg.mutable_data_msg()->mutable_host_frame()->set_stream_point(stream_info.stream_point);
        out_msg.mutable_data_msg()->mutable_host_frame()->set_stream_num(stream_num);
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
        sequence_number += ((handle_data->size() + MAX_DATA_CHUNK - 1) / MAX_DATA_CHUNK);
        stream_info.stream_point += handle_data->size();
        stream_info.frame_num++;

        buffer_map.Increment(data_msg.handle());
        SendToSocket(out_msg);
    }
    buffer_map.Decrement(data_msg.handle());
}

void ClientActor::OnAudioData(const fp_actor::AudioData& data_msg) {
    // INSERT ENCRYPTION HERE!!
    // INSERT ENCRYPTION HERE!!

    uint32_t stream_num = data_msg.stream_num();

    if (stream_num > audio_streams.size()) {
        LOG_WARNING("Received audio data from invalid stream number {}", stream_num);
        return;
    }

    StreamInfo& stream_info = audio_streams[stream_num];

    if (stream_info.stream_state == StreamState::READY) {
        std::string* handle_data = buffer_map.GetBuffer(data_msg.handle());
        fp_network::Network out_msg;
        *out_msg.mutable_data_msg() = fp_network::Data();
        out_msg.mutable_data_msg()->set_sequence_number(sequence_number);
        *out_msg.mutable_data_msg()->mutable_host_frame() = fp_network::HostDataFrame();
        out_msg.mutable_data_msg()->mutable_host_frame()->set_frame_num(stream_info.frame_num);
        out_msg.mutable_data_msg()->mutable_host_frame()->set_frame_size(handle_data->size());
        out_msg.mutable_data_msg()->mutable_host_frame()->set_stream_point(stream_info.stream_point);
        out_msg.mutable_data_msg()->mutable_host_frame()->set_stream_num(stream_num);
        *out_msg.mutable_data_msg()->mutable_host_frame()->mutable_audio() = fp_network::AudioFrame();
        out_msg.mutable_data_msg()->mutable_host_frame()->mutable_audio()->set_data_handle(data_msg.handle());
        out_msg.mutable_data_msg()->mutable_host_frame()->mutable_audio()->set_chunk_offset(0);
    
        sequence_number += ((handle_data->size() + MAX_DATA_CHUNK - 1) / MAX_DATA_CHUNK);
        stream_info.stream_point += handle_data->size();
        stream_info.frame_num++;
        buffer_map.Increment(data_msg.handle());
        SendToSocket(out_msg);
    }
    buffer_map.Decrement(data_msg.handle());
}

bool ClientActor::OnHandshakeMessage(const fp_network::Handshake& msg) {
    bool handshake_success = false;
    LOG_INFO("Client actor {} received handshake, current state={}", GetName(), protocol_state);
    if (protocol_state == HandshakeState::HS_UNINITIALIZED) {
        if (msg.magic() == 0x46524E44504C5952ull) {
            LOG_INFO("Client actor {} received first handshake", GetName());
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
            LOG_INFO("Client actor {} received last handshake", GetName());
            fp_network::Network stream_info_msg;
            stream_info_msg.mutable_info_msg()->set_num_video_streams(video_streams.size());
            stream_info_msg.mutable_info_msg()->set_num_audio_streams(audio_streams.size());
            SendToSocket(stream_info_msg);            
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
    switch (msg.State_case()) {
        case fp_network::State::kClientStreamState: {
            auto& state = msg.client_stream_state();
            switch (state.state()) {
                case fp_network::ClientStreamState::READY_FOR_PPS_SPS_IDR: {
                    LOG_INFO("Received ready for PPSSPS from {}", state.stream_num());
                    fp_actor::SpecialFrameRequest req;
                    req.set_type(fp_actor::SpecialFrameRequest::PPS_SPS);
                    if (state.stream_num() > video_streams.size()) {
                        LOG_WARNING("Received video data from invalid stream number {}", state.stream_num());
                        return;
                    }
                    StreamInfo& info = video_streams[state.stream_num()];
                    SendTo(info.actor_name, req);
                    info.stream_state = StreamState::WAITING_FOR_VIDEO;
                    break;
                }
                case fp_network::ClientStreamState::READY_FOR_VIDEO: {
                    LOG_INFO("Received ready for video from {}", state.stream_num());
                    fp_actor::SpecialFrameRequest req;
                    req.set_type(fp_actor::SpecialFrameRequest::IDR);
                    StreamInfo& info = video_streams[state.stream_num()];
                    SendTo(info.actor_name, req);
                    info.stream_state = StreamState::READY;
                    for (auto& audio_stream : audio_streams) {
                        audio_stream.stream_state = StreamState::READY;
                    }
                    break;
                }
            }
            break;
        }
        case fp_network::State::kClientState: {
            auto& state = msg.client_state();
            switch (state.state()) {
                case fp_network::ClientState::DISCONNECTING: {
                    fp_actor::ClientDisconnect disconnect;
                    disconnect.set_client_name(GetName());
                    SendTo(CLIENT_MANAGER_ACTOR_NAME, disconnect);

                    fp_actor::Kill kill;
                    any_msg any_kill;
                    any_kill.PackFrom(kill);
                    EnqueueMessage(std::move(any_kill));

                    for (auto& video_stream : video_streams) {
                        video_stream.stream_state = StreamState::DISCONNECTED;
                    }
                    for (auto& audio_stream : audio_streams) {
                        audio_stream.stream_state = StreamState::DISCONNECTED;
                    }
                }
            }
            break;
        }
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
        case fp_network::ClientDataFrame::kMouseButton:
            OnMouseButtonFrame(c_msg.mouse_button());
            break;
        case fp_network::ClientDataFrame::kMouseMotion:
            OnMouseMotionFrame(c_msg.mouse_motion());
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
    LOG_INFO("Client {} request to host = {}", GetName(), static_cast<int>(msg.type()));
    switch(msg.type()) {
        case fp_network::RequestToHost::SEND_IDR: {
            fp_actor::SpecialFrameRequest sfr;
            sfr.set_type(fp_actor::SpecialFrameRequest::IDR);
            for (const StreamInfo& stream : video_streams) {
                SendTo(stream.actor_name, sfr);
            }
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

void ClientActor::OnMouseButtonFrame(const fp_network::MouseButtonFrame& msg) {

}

void ClientActor::OnMouseMotionFrame(const fp_network::MouseMotionFrame& msg) {

}

void ClientActor::OnControllerFrame(const fp_network::ControllerFrame& msg) {
    fp_actor::InputData input_msg;
    input_msg.set_actor_name(GetName());
    input_msg.mutable_controller()->CopyFrom(msg);
    SendTo(INPUT_ACTOR_NAME, input_msg);
}