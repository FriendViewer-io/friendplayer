#pragma once

#include "actors/ProtocolActor.h"
#include "common/FrameRingBuffer.h"

#include "protobuf/actor_messages.pb.h"
#include "protobuf/network_messages.pb.h"

class FramePresenterGLUT;

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

    void OnKeyPress(int key, bool pressed);
    void OnMouseMove(int stream, int x, int y);
    void OnMousePress(int stream, int x, int y, int button, bool pressed);

private:
//    InputStreamer input_streamer;

    void SendVideoFrameToDecoder(uint32_t stream_num);
    void SendAudioFrameToDecoder(uint32_t stream_num);

    std::vector<std::unique_ptr<FrameRingBuffer>> video_streams;
    std::vector<std::unique_ptr<FrameRingBuffer>> audio_streams;

    std::map<uint32_t, std::string> audio_stream_num_to_name;
    std::map<uint32_t, std::string> video_stream_num_to_name;
    std::map<std::string, uint32_t> name_to_stream_num;

    bool OnHandshakeMessage(const fp_network::Handshake& msg) override;
    void OnDataMessage(const fp_network::Data& msg) override;
    void OnStateMessage(const fp_network::State& msg) override;
    void OnStreamInfoMessage(const fp_network::StreamInfo& msg) override;

    void OnVideoFrame(const fp_network::HostDataFrame& msg);
    void OnAudioFrame(const fp_network::HostDataFrame& msg);
    void OnHostState(const fp_network::HostState& msg);

    FramePresenterGLUT* presenter;

};

DEFINE_ACTOR_GENERATOR(HostActor)