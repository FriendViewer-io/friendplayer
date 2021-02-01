#include "actors/HostActor.h"

#include "decoder/FramePresenterGL.h"

#include "streamer/InputStreamer.h"
#include "actors/CommonActorNames.h"
#include "common/Crypto.h"
#include "common/Log.h"

HostActor::HostActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
    : ProtocolActor(actor_map, buffer_map, std::move(name)), presenter(nullptr) {
    input_streamer = std::make_unique<InputStreamer>();
}

HostActor::~HostActor() {
    controller_capture_thread->join();
}

void HostActor::OnInit(const std::optional<any_msg>& init_msg) {
    if (init_msg) {
        if (init_msg->Is<fp_actor::HostProtocolInit>()) {
            fp_actor::HostProtocolInit host_init_msg;
            init_msg->UnpackTo(&host_init_msg);
            any_msg base_msg;
            base_msg.PackFrom(host_init_msg.base_init());
            ProtocolActor::OnInit(base_msg);

            fp_network::Network send_handshake_msg;
            send_handshake_msg.mutable_hs_msg()->mutable_phase1()->set_magic(0x46524E44504C5952ull);
            if (host_init_msg.has_token()) {
                send_handshake_msg.mutable_hs_msg()->mutable_phase1()->set_token(host_init_msg.token());
            }
            if (host_init_msg.has_client_identity()) {
                send_handshake_msg.mutable_hs_msg()->mutable_phase1()->set_client_name(host_init_msg.client_identity());
            }
            SendToSocket(send_handshake_msg);
        } else {
            LOG_CRITICAL("HostActor being initialized with unhandled init_msg type {}!", init_msg->type_url());
        }
    } else {
        LOG_CRITICAL("HostActor being initialized with empty init_msg!");
    }
    protocol_state = HandshakeState::HS_WAITING_SHAKE_ACK;
}

void HostActor::OnMessage(const any_msg& msg) {
    if (msg.Is<fp_actor::VideoDecoderReady>()) {
        fp_actor::VideoDecoderReady ready_msg;
        msg.UnpackTo(&ready_msg);
        // Tell host to start sending video frames
        fp_network::Network network_msg;
        network_msg.mutable_state_msg()->mutable_client_stream_state()->set_state(fp_network::ClientStreamState::READY_FOR_VIDEO);
        network_msg.mutable_state_msg()->mutable_client_stream_state()->set_stream_num(ready_msg.stream_num());
        SendToSocket(network_msg);
    }
    else if (msg.Is<fp_actor::VideoDataRequest>()) {
        fp_actor::VideoDataRequest video_request_msg;
        msg.UnpackTo(&video_request_msg);
        LOG_INFO("Frame for decoder {} timed out waiting for frame", video_request_msg.stream_num());
        SendVideoFrameToDecoder(video_request_msg.stream_num());
    } else if (msg.Is<fp_actor::CreateFinish>()) {
        fp_actor::CreateFinish create_finish_msg;
        msg.UnpackTo(&create_finish_msg);
        // Only video streams need pps sps
        if (create_finish_msg.actor_type_name() == "VideoDecodeActor") {
            uint32_t stream_num = name_to_stream_num[create_finish_msg.actor_name()];
            LOG_INFO("Requesting PPSSPS ready for decoder {}", stream_num);
            // Ask host for a PPS SPS
            fp_network::Network ready_msg;
            ready_msg.mutable_state_msg()->mutable_client_stream_state()->set_state(fp_network::ClientStreamState::READY_FOR_PPS_SPS_IDR);
            ready_msg.mutable_state_msg()->mutable_client_stream_state()->set_stream_num(stream_num);
            SendToSocket(ready_msg);
        }
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
        if (msg.has_phase2()) {
            crypto_impl = std::make_unique<Crypto>(msg.phase2().p(), msg.phase2().q(), msg.phase2().g());
            crypto_impl->SharedKeyAgreement(msg.phase2().pubkey());

            protocol_state = HandshakeState::HS_READY;
            LOG_INFO("Magic good, HS ready sending back");
            fp_network::Network send_handshake_msg;
            send_handshake_msg.mutable_hs_msg()->mutable_phase3()->set_pubkey(crypto_impl->GetPublicKey());
            SendToSocket(send_handshake_msg);
            handshake_success = true;
        } else {
            LOG_ERROR("Invalid handshake phase2 in state HS_WAITING_SHAKE_ACK");
        }
    } else {
        LOG_WARNING("Got handshake message after finishing handshake");
    }
    return handshake_success;
}

void HostActor::OnStateMessage(const fp_network::State& msg) {
    if (msg.State_case() == fp_network::State::kClientState || msg.State_case() == fp_network::State::kClientStreamState) {
        LOG_ERROR("Got client state from client side");
        return;
    }
    auto& h_msg = msg.host_state();
    switch(h_msg.state()) {
        case fp_network::HostState::DISCONNECTING: {
            fp_actor::ClientDisconnected dc_msg;
            dc_msg.set_client_name(HOST_ACTOR_NAME);
            SendTo(CLIENT_MANAGER_ACTOR_NAME, dc_msg);
            break;
        }
    }
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
    if (video_streams[msg.stream_num()]->AddFrameChunk(frame)) {
        SendVideoFrameToDecoder(msg.stream_num());
    }
    frame.mutable_video()->release_data();
    buffer_map.Decrement(handle);
}

void HostActor::OnAudioFrame(const fp_network::HostDataFrame& msg) {
    fp_network::HostDataFrame frame;
    uint64_t handle = msg.audio().data_handle();
    frame.CopyFrom(msg);
    frame.mutable_audio()->clear_DataBacking();
    frame.mutable_audio()->set_allocated_data(buffer_map.GetBuffer(handle));
    // COPIES DATA!!
    if (audio_streams[msg.stream_num()]->AddFrameChunk(frame)) {
        SendAudioFrameToDecoder(msg.stream_num());
    }
    frame.mutable_audio()->release_data();
    buffer_map.Decrement(handle);
}

void HostActor::OnStreamInfoMessage(const fp_network::StreamInfo& msg) {
    for (uint32_t i = 0; i < msg.num_audio_streams(); ++i) {
        std::string actor_name = fmt::format(AUDIO_DECODER_ACTOR_NAME_FORMAT, i);
        audio_stream_num_to_name[i] = actor_name;
        name_to_stream_num[actor_name] = i;
        fp_actor::Create create_msg;
        create_msg.set_actor_type_name("AudioDecodeActor");
        create_msg.set_response_actor(GetName());
        create_msg.set_actor_name(actor_name);
        fp_actor::AudioDecodeInit audio_init;
        audio_init.set_stream_num(i);
        *create_msg.mutable_init_msg() = google::protobuf::Any();
        create_msg.mutable_init_msg()->PackFrom(audio_init);
        SendTo(ADMIN_ACTOR_NAME, create_msg);
        audio_streams.push_back(std::move(std::make_unique<FrameRingBuffer>(fmt::format("AudioBuffer{}", i), AUDIO_FRAME_BUFFER, AUDIO_FRAME_SIZE)));
    }
    presenter = std::make_unique<FramePresenterGL>(this, msg.num_video_streams());
    for (uint32_t i = 0; i < msg.num_video_streams(); ++i) {
        std::string actor_name = fmt::format(VIDEO_DECODER_ACTOR_NAME_FORMAT, i);
        video_stream_num_to_name[i] = actor_name;
        name_to_stream_num[actor_name] = i;
        fp_actor::Create create_msg;
        create_msg.set_actor_type_name("VideoDecodeActor");
        create_msg.set_response_actor(GetName());
        create_msg.set_actor_name(actor_name);
        fp_actor::VideoDecodeInit video_init;
        video_init.set_stream_num(i);
        *create_msg.mutable_init_msg() = google::protobuf::Any();
        create_msg.mutable_init_msg()->PackFrom(video_init);
        SendTo(ADMIN_ACTOR_NAME, create_msg);
        video_streams.push_back(std::move(std::make_unique<FrameRingBuffer>(fmt::format("VideoBuffer{}", i), VIDEO_FRAME_BUFFER, VIDEO_FRAME_SIZE)));
    }
    controller_capture_thread = std::make_unique<std::thread>(&HostActor::ControllerCaptureThread, this, 16);
}

void HostActor::SendVideoFrameToDecoder(uint32_t stream_num) {
    std::string* video_frame = new std::string();
    bool needs_idr = video_streams[stream_num]->GetFront(*video_frame);
    if (video_frame->size() > 0) {
        crypto_impl->DecryptInPlace(*video_frame);
    }
    
    if (needs_idr) {
        fp_network::ClientDataFrameInner idr_req_msg;
        idr_req_msg.mutable_host_request()->set_type(fp_network::RequestToHost::SEND_IDR);
        EncryptAndSendDataFrame(idr_req_msg);
        LOG_INFO("Requesting IDR from host");
    }
    fp_actor::VideoData video_data;
    video_data.set_handle(buffer_map.Wrap(video_frame));
    video_data.set_stream_num(stream_num);
    SendTo(video_stream_num_to_name[stream_num], video_data);
}

void HostActor::SendAudioFrameToDecoder(uint32_t stream_num) {
    std::string* audio_frame = new std::string();
    bool corrupt_frame = audio_streams[stream_num]->GetFront(*audio_frame);

    if (audio_frame->size() > 0 && !corrupt_frame) {
        crypto_impl->DecryptInPlace(*audio_frame);
    } else {
        delete audio_frame;
        return;
    }

    fp_actor::AudioData audio_data;
    audio_data.set_handle(buffer_map.Wrap(audio_frame));
    audio_data.set_stream_num(stream_num);
    SendTo(audio_stream_num_to_name[stream_num], audio_data);
}

void HostActor::OnWindowClosed() {
    fp_actor::Kill kill_msg;
    SendTo(CLIENT_MANAGER_ACTOR_NAME, kill_msg);
}

void HostActor::MuteState(bool state) {
    fp_network::ClientDataFrameInner mute_msg;
    if (state) {
        mute_msg.mutable_host_request()->set_type(fp_network::RequestToHost::MUTE_AUDIO);
    } else {
        mute_msg.mutable_host_request()->set_type(fp_network::RequestToHost::PLAY_AUDIO);
    }
    EncryptAndSendDataFrame(mute_msg);
}

void HostActor::EncryptAndSendDataFrame(const fp_network::ClientDataFrameInner& cdf) {
    std::string* encrypted_pkt = new std::string();
    crypto_impl->Encrypt(cdf.SerializeAsString(), *encrypted_pkt);

    fp_network::Network net_msg;
    net_msg.mutable_data_msg()->set_needs_ack(false);
    net_msg.mutable_data_msg()->mutable_client_frame()->set_frame_id(frame_id_counter++);
    net_msg.mutable_data_msg()->mutable_client_frame()->set_allocated_encrypted_data_frame(encrypted_pkt);
    SendToSocket(net_msg);
}

void HostActor::OnKeyPress(int key, bool pressed) {
    fp_network::ClientDataFrameInner keyboard_press_msg;
    keyboard_press_msg.mutable_keyboard()->set_key(key);
    keyboard_press_msg.mutable_keyboard()->set_pressed(pressed);
    EncryptAndSendDataFrame(keyboard_press_msg);
}

void HostActor::OnMousePress(int stream, int x, int y, int button, bool pressed) {
    fp_network::ClientDataFrameInner mouse_press_msg;
    mouse_press_msg.mutable_mouse()->set_mouse_x(x);
    mouse_press_msg.mutable_mouse()->set_mouse_y(y);
    mouse_press_msg.mutable_mouse()->set_stream_num(stream);
    mouse_press_msg.mutable_mouse()->set_button(static_cast<fp_network::MouseFrame_MouseButtons>(button));
    mouse_press_msg.mutable_mouse()->set_pressed(pressed);
    EncryptAndSendDataFrame(mouse_press_msg);
}

void HostActor::OnMouseMove(int stream, int x, int y) {
    fp_network::ClientDataFrameInner mouse_move_msg;
    mouse_move_msg.mutable_mouse()->set_mouse_x(x);
    mouse_move_msg.mutable_mouse()->set_mouse_y(y);
    mouse_move_msg.mutable_mouse()->set_stream_num(stream);
    EncryptAndSendDataFrame(mouse_move_msg);
}

void HostActor::OnMouseScroll(int stream, int x, int y, double x_offset, double y_offset) {
    fp_network::ClientDataFrameInner mouse_scroll_msg;
    
    mouse_scroll_msg.mutable_mouse()->set_mouse_x(x);
    mouse_scroll_msg.mutable_mouse()->set_mouse_y(y);
    mouse_scroll_msg.mutable_mouse()->set_stream_num(stream);
    mouse_scroll_msg.mutable_mouse()->set_mouse_wheel_x(x_offset);
    mouse_scroll_msg.mutable_mouse()->set_mouse_wheel_y(y_offset);
    EncryptAndSendDataFrame(mouse_scroll_msg);
}

void HostActor::VolumeState(float volume) {
    fp_actor::AudioDecodeVolume volume_msg;
    volume_msg.set_volume(volume);
    SendTo(fmt::format(AUDIO_DECODER_ACTOR_NAME_FORMAT, 0), volume_msg);
}

void HostActor::ControllerCaptureThread(int poll_rate) {
    input_streamer->RegisterPhysicalController(0);
    while (is_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_rate));
        auto controller_capture = input_streamer->CapturePhysicalController();
        if (controller_capture) {
            fp_network::ClientDataFrameInner controller_msg;
            controller_msg.mutable_controller()->CopyFrom(*controller_capture);
            EncryptAndSendDataFrame(controller_msg);
        }
    }
}