#pragma once

#include "actors/ProtocolActor.h"
#include "common/FrameRingBuffer.h"

#include "protobuf/actor_messages.pb.h"
#include "protobuf/network_messages.pb.h"


class HostActor : public ProtocolActor {
private:
    static constexpr size_t VIDEO_FRAME_BUFFER = 5;
    static constexpr size_t AUDIO_FRAME_BUFFER = 5;
    // Guess values, tune or scale these?
    static constexpr size_t VIDEO_FRAME_SIZE = 20000;
    static constexpr size_t AUDIO_FRAME_SIZE = 1795;

public:
    HostActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name);

    void OnInit(const std::optional<any_msg>& init_msg) override;

    void OnMessage(const any_msg& msg) override;

private:
//    InputStreamer input_streamer;

    FrameRingBuffer video_buffer;
    FrameRingBuffer audio_buffer;

    bool OnHandshakeMessage(const fp_network::Handshake& msg) override;
    void OnDataMessage(const fp_network::Data& msg) override;
    void OnStateMessage(const fp_network::State& msg) override;

    void OnVideoFrame(const fp_network::HostDataFrame& msg);
    void OnAudioFrame(const fp_network::HostDataFrame& msg);
    void OnHostState(const fp_network::HostState& msg);

};

DEFINE_ACTOR_GENERATOR(HostActor)