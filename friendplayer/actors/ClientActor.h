#pragma once

#include "actors/ProtocolActor.h"
#include "protobuf/actor_messages.pb.h"

#include <vector>

class ClientActor : public ProtocolActor {
private:
    // maximum chunk size over UDP accounding for proto overhead
    // and AES block encryption
    static constexpr size_t MAX_DATA_CHUNK = 476;
public:
    ClientActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name);

    virtual ~ClientActor();

    void OnInit(const std::optional<any_msg>& init_msg) override;
    void OnMessage(const any_msg& msg) override;
    void OnFinish() override;

private:
    bool audio_enabled;
    bool keyboard_enabled;
    bool mouse_enabled;
    bool controller_enabled;

    enum StreamState : uint32_t {
        UNINITIALIZED = 0,
        WAITING_FOR_VIDEO = 1,
        READY,
        DISCONNECTED,
    };

    struct StreamInfo {
        StreamState stream_state = StreamState::UNINITIALIZED;
        uint32_t frame_num = 0;
        std::string actor_name;
    };
    std::vector<StreamInfo> video_streams;
    std::vector<StreamInfo> audio_streams;

    uint32_t sequence_number;

    // Internal messages
    void OnVideoData(const fp_actor::VideoData& msg);
    void OnAudioData(const fp_actor::AudioData& msg);
    void OnActorState(const fp_actor::ChangeClientActorState& msg);

    // Network messages
    bool OnHandshakeMessage(const fp_network::Handshake& msg) override;
    void OnDataMessage(const fp_network::Data& msg) override;
    void OnStateMessage(const fp_network::State& msg) override;

    void OnHostRequest(const fp_network::RequestToHost& msg);
    void OnKeyboardFrame(const fp_network::KeyboardFrame& msg);
    void OnMouseFrame(const fp_network::MouseFrame& msg);
    void OnControllerFrame(const fp_network::ControllerFrame& msg);
};

DEFINE_ACTOR_GENERATOR(ClientActor)