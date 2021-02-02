#include "actors/ClientActor.h"

#include "actors/CommonActorNames.h"

#include "common/Log.h"
#include "common/Crypto.h"
#include "protobuf/actor_messages.pb.h"
#include "protobuf/network_messages.pb.h"

#include <fmt/format.h>


ClientActor::ClientActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
    : ProtocolActor(actor_map, buffer_map, std::move(name)),
      audio_enabled(true),
      keyboard_enabled(false),
      mouse_enabled(false),
      controller_enabled(false)
      //input_streamer()
      { }

ClientActor::~ClientActor() {}

void ClientActor::OnInit(const std::optional<any_msg>& init_msg) {
    if (init_msg) {
        if (init_msg->Is<fp_actor::ClientProtocolInit>()) {
            fp_actor::ClientProtocolInit client_init_msg;
            init_msg->UnpackTo(&client_init_msg);

            video_streams.resize(client_init_msg.video_stream_count());
            for (uint32_t i = 0; i < client_init_msg.video_stream_count(); i++) {
                video_streams[i].actor_name = fmt::format(VIDEO_ENCODER_ACTOR_NAME_FORMAT, i);
            }
            audio_streams.resize(client_init_msg.audio_stream_count());
            for (uint32_t i = 0; i < client_init_msg.audio_stream_count(); i++) {
                audio_streams[i].actor_name = fmt::format(AUDIO_ENCODER_ACTOR_NAME_FORMAT, i);
            }
            // base class init
            any_msg base_msg;
            base_msg.PackFrom(client_init_msg.base_init());
            ProtocolActor::OnInit(base_msg);

            fp_actor::AddClientSettings add_client_msg;
            add_client_msg.set_actor_name(GetName());
            add_client_msg.set_address(address);
            SendTo(SETTINGS_ACTOR_NAME, add_client_msg);
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
        } else if (msg.Is<fp_actor::ChangeClientActorState>()) {
            fp_actor::ChangeClientActorState change_msg;
            msg.UnpackTo(&change_msg);
            OnActorState(change_msg);
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
    } else if (msg.Is<fp_actor::ChangeClientActorState>()) {
        fp_actor::ChangeClientActorState change_msg;
        msg.UnpackTo(&change_msg);
        OnActorState(change_msg);
    } else if (msg.Is<fp_actor::ClientKick>()) {
        fp_network::Network dc_msg;
        dc_msg.mutable_state_msg()->mutable_host_state()->set_state(fp_network::HostState::DISCONNECTING);
        SendToSocket(dc_msg);
    } else {
        ProtocolActor::OnMessage(msg);
    }
}

void ClientActor::OnFinish() {
    fp_actor::UnregisterInputUser unregister_msg;
    unregister_msg.set_actor_name(GetName());
    SendTo(INPUT_ACTOR_NAME, unregister_msg);

    fp_actor::RemoveClientSettings remove_client_msg;
    remove_client_msg.set_actor_name(GetName());
    SendTo(SETTINGS_ACTOR_NAME, remove_client_msg);

    ProtocolActor::OnFinish();
}

void ClientActor::OnVideoData(const fp_actor::VideoData& data_msg) {
    uint32_t stream_num = data_msg.stream_num();

    if (stream_num > video_streams.size()) {
        LOG_WARNING("Received video data from invalid stream number {}", stream_num);
        return;
    }

    StreamInfo& stream_info = video_streams[stream_num];

    if ((stream_info.stream_state == StreamState::WAITING_FOR_VIDEO && data_msg.type() == fp_actor::VideoData::PPS_SPS)
        || stream_info.stream_state == StreamState::READY) {
        std::string* handle_data = buffer_map.GetBuffer(data_msg.handle());

        std::string encrypted_buf;
        crypto_impl->Encrypt(*handle_data, encrypted_buf);

        fp_network::Network network_msg;
        network_msg.mutable_data_msg()->mutable_host_frame()->set_frame_num(stream_info.frame_num);
        network_msg.mutable_data_msg()->mutable_host_frame()->set_frame_size(static_cast<uint32_t>(encrypted_buf.size()));
        network_msg.mutable_data_msg()->mutable_host_frame()->set_stream_num(stream_num);
        
        if (data_msg.type() == fp_actor::VideoData::PPS_SPS) {
            network_msg.mutable_data_msg()->mutable_host_frame()->mutable_video()->set_frame_type(fp_network::VideoFrame::PPS_SPS);
        } else if (data_msg.type() == fp_actor::VideoData::IDR) {
            network_msg.mutable_data_msg()->mutable_host_frame()->mutable_video()->set_frame_type(fp_network::VideoFrame::IDR);
        } else {
            network_msg.mutable_data_msg()->mutable_host_frame()->mutable_video()->set_frame_type(fp_network::VideoFrame::NORMAL);
        }

        for (size_t chunk_offset = 0; chunk_offset < encrypted_buf.size(); chunk_offset += MAX_DATA_CHUNK) {
            const size_t chunk_end = std::min(chunk_offset + MAX_DATA_CHUNK, encrypted_buf.size());
            uint64_t handle = buffer_map.Create(encrypted_buf.data() + chunk_offset, chunk_end - chunk_offset);
            network_msg.mutable_data_msg()->mutable_host_frame()->mutable_video()->set_chunk_offset(static_cast<uint32_t>(chunk_offset));
            network_msg.mutable_data_msg()->mutable_host_frame()->mutable_video()->set_data_handle(handle);
            SendToSocket(network_msg);
        }
        stream_info.frame_num++;
    }
    buffer_map.Decrement(data_msg.handle());
}

void ClientActor::OnAudioData(const fp_actor::AudioData& data_msg) {
    uint32_t stream_num = data_msg.stream_num();

    if (stream_num > audio_streams.size()) {
        LOG_WARNING("Received audio data from invalid stream number {}", stream_num);
        return;
    }

    StreamInfo& stream_info = audio_streams[stream_num];

    if (stream_info.stream_state == StreamState::READY && audio_enabled) {
        std::string* handle_data = buffer_map.GetBuffer(data_msg.handle());

        std::string encrypted_buf;
        crypto_impl->Encrypt(*handle_data, encrypted_buf);

        fp_network::Network network_msg;
        network_msg.mutable_data_msg()->mutable_host_frame()->set_frame_num(stream_info.frame_num);
        network_msg.mutable_data_msg()->mutable_host_frame()->set_frame_size(static_cast<uint32_t>(encrypted_buf.size()));
        network_msg.mutable_data_msg()->mutable_host_frame()->set_stream_num(stream_num);
        
        for (size_t chunk_offset = 0; chunk_offset < encrypted_buf.size(); chunk_offset += MAX_DATA_CHUNK) {
            const size_t chunk_end = std::min(chunk_offset + MAX_DATA_CHUNK, encrypted_buf.size());
            uint64_t handle = buffer_map.Create(encrypted_buf.data() + chunk_offset, chunk_end - chunk_offset);
            network_msg.mutable_data_msg()->mutable_host_frame()->mutable_audio()->set_chunk_offset(static_cast<uint32_t>(chunk_offset));
            network_msg.mutable_data_msg()->mutable_host_frame()->mutable_audio()->set_data_handle(handle);
            SendToSocket(network_msg);
        }
        stream_info.frame_num++;
    }
    buffer_map.Decrement(data_msg.handle());
}

void ClientActor::OnActorState(const fp_actor::ChangeClientActorState& msg) {
    keyboard_enabled = msg.keyboard_enabled();
    mouse_enabled = msg.mouse_enabled();
    controller_enabled = msg.controller_enabled();
}

bool ClientActor::OnHandshakeMessage(const fp_network::Handshake& msg) {
    bool handshake_success = false;
    LOG_INFO("Client actor {} received handshake, current state={}", GetName(), protocol_state);
    if (protocol_state == HandshakeState::HS_UNINITIALIZED) {
        if (msg.has_phase1() && msg.phase1().magic() == 0x46524E44504C5952ull) {
            LOG_INFO("Client actor {} received first handshake", GetName());
            client_name = msg.phase1().client_name();

            crypto_impl = std::make_unique<Crypto>(1024);
            
            fp_network::Network send_handshake_msg;
            send_handshake_msg.mutable_hs_msg()->mutable_phase2()->set_p(crypto_impl->P());
            send_handshake_msg.mutable_hs_msg()->mutable_phase2()->set_q(crypto_impl->Q());
            send_handshake_msg.mutable_hs_msg()->mutable_phase2()->set_g(crypto_impl->G());
            send_handshake_msg.mutable_hs_msg()->mutable_phase2()->set_pubkey(crypto_impl->GetPublicKey());
            
            SendToSocket(send_handshake_msg);
            protocol_state = HandshakeState::HS_WAITING_SHAKE_ACK;
            handshake_success = true;
        } else {
            LOG_ERROR("Invalid handshake magic in state HS_UNINITIALIZED");
        }
    } else if (protocol_state == HandshakeState::HS_WAITING_SHAKE_ACK) {
        if (msg.has_phase3()) {
            LOG_INFO("Client actor {} received last handshake", GetName());
            crypto_impl->SharedKeyAgreement(msg.phase3().pubkey());

            fp_network::Network stream_info_msg;
            stream_info_msg.mutable_info_msg()->set_num_video_streams(static_cast<uint32_t>(video_streams.size()));
            stream_info_msg.mutable_info_msg()->set_num_audio_streams(static_cast<uint32_t>(audio_streams.size()));
            SendToSocket(stream_info_msg);            
            protocol_state = HandshakeState::HS_READY;
            handshake_success = true;

            fp_actor::UpdateClientSetting update_msg;
            update_msg.set_actor_name(GetName());
            update_msg.set_client_name(client_name);
            update_msg.set_finished_handshake(true);
            SendTo(SETTINGS_ACTOR_NAME, update_msg);
        } else {
            LOG_ERROR("Invalid handshake magic in state HS_WAITING_SHAKE_ACK");
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
                    fp_actor::ClientDisconnected disconnect;
                    disconnect.set_client_name(GetName());
                    SendTo(CLIENT_MANAGER_ACTOR_NAME, disconnect);

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

    std::string df_decrypted_serial;
    crypto_impl->Decrypt(c_msg.encrypted_data_frame(), df_decrypted_serial);
    fp_network::ClientDataFrameInner df_decrypted;
    df_decrypted.ParseFromArray(df_decrypted_serial.data(), df_decrypted_serial.size());

    switch(df_decrypted.Frame_case()) {
        case fp_network::ClientDataFrameInner::kKeyboard:
            OnKeyboardFrame(df_decrypted.keyboard());
            break;
        case fp_network::ClientDataFrameInner::kMouse:
            OnMouseFrame(df_decrypted.mouse());
            break;
        case fp_network::ClientDataFrameInner::kController:
            OnControllerFrame(df_decrypted.controller());
            break;
        case fp_network::ClientDataFrameInner::kHostRequest:
            OnHostRequest(df_decrypted.host_request());
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
        case fp_network::RequestToHost::MUTE_AUDIO:
            audio_enabled = false;
            break;
        case fp_network::RequestToHost::PLAY_AUDIO:
            audio_enabled = true;
            break;
    }
}

void ClientActor::OnKeyboardFrame(const fp_network::KeyboardFrame& msg) {
    if (!keyboard_enabled) {
        return;
    }
    fp_actor::InputData input_msg;
    input_msg.set_actor_name(GetName());
    input_msg.mutable_keyboard()->CopyFrom(msg);
    SendTo(INPUT_ACTOR_NAME, input_msg);
}

void ClientActor::OnMouseFrame(const fp_network::MouseFrame& msg) {
    if (!mouse_enabled) {
        return;
    }
    fp_actor::InputData input_msg;
    input_msg.set_actor_name(GetName());
    input_msg.mutable_mouse()->CopyFrom(msg);
    SendTo(INPUT_ACTOR_NAME, input_msg);
}

void ClientActor::OnControllerFrame(const fp_network::ControllerFrame& msg) {
    if (!controller_enabled) {
        return;
    }
    fp_actor::InputData input_msg;
    input_msg.set_actor_name(GetName());
    input_msg.mutable_controller()->CopyFrom(msg);
    SendTo(INPUT_ACTOR_NAME, input_msg);
}