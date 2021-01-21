#pragma once

#include "actors/ProtocolActor.h"

class ClientActor : public ProtocolActor {
private:
    static constexpr size_t BLOCK_SIZE = 16;
    // maximum chunk size over UDP accounding for proto overhead
    // and AES block encryption
    static constexpr size_t MAX_DATA_CHUNK = 476 - (476 % BLOCK_SIZE);
public:
    ClientActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name);

    void OnMessage(const any_msg& msg) override;

private:
    bool audio_enabled;
    bool keyboard_enabled;
    bool mouse_enabled;
    bool controller_enabled;

//    InputStreamer input_streamer;

    uint32_t video_stream_point;
    uint32_t audio_stream_point;
    uint32_t audio_frame_num;
    uint32_t video_frame_num;
    uint32_t sequence_number;

    enum StreamState : uint32_t {
        UNINITIALIZED = 0,
        WAITING_FOR_VIDEO = 1,
        READY,
        DISCONNECTED,
    };
    StreamState stream_state;
    
    bool OnHandshakeMessage(const fp_network::Handshake& msg) override;
    void OnDataMessage(const fp_network::Data& msg) override;
    void OnStateMessage(const fp_network::State& msg) override;

    void OnHostRequest(const fp_network::RequestToHost& msg);
    void OnKeyboardFrame(const fp_network::KeyboardFrame& msg);
    void OnMouseFrame(const fp_network::MouseFrame& msg);
    void OnControllerFrame(const fp_network::ControllerFrame& msg);

};

DEFINE_ACTOR_GENERATOR(ClientActor)