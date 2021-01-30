#pragma once

#include "actors/ProtocolActor.h"
#include "common/FrameRingBuffer.h"

#include "protobuf/actor_messages.pb.h"
#include "protobuf/network_messages.pb.h"

#include <thread>

class FramePresenterGL;
class InputStreamer;

class HostActor : public ProtocolActor {
private:
    static constexpr size_t VIDEO_FRAME_BUFFER = 10;
    static constexpr size_t AUDIO_FRAME_BUFFER = 5;
    // Guess values, tune or scale these?
    static constexpr size_t VIDEO_FRAME_SIZE = 20000;
    static constexpr size_t AUDIO_FRAME_SIZE = 1795;

public:
    HostActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name);

    virtual ~HostActor();

    void OnInit(const std::optional<any_msg>& init_msg) override;
    void OnMessage(const any_msg& msg) override;

    void OnKeyPress(int key, bool pressed);
    void OnMouseMove(int stream, int x, int y);
    void OnMousePress(int stream, int x, int y, int button, bool pressed);
    void OnMouseScroll(int stream, int x, int y, double x_offset, double y_offset);
    void OnWindowClosed();
    void MuteState(bool state);

private:
    void SendVideoFrameToDecoder(uint32_t stream_num);
    void SendAudioFrameToDecoder(uint32_t stream_num);

    void EncryptAndSendDataFrame(const fp_network::ClientDataFrameInner& cdf);

    std::vector<std::unique_ptr<FrameRingBuffer>> video_streams;
    std::vector<std::unique_ptr<FrameRingBuffer>> audio_streams;

    std::map<uint32_t, std::string> audio_stream_num_to_name;
    std::map<uint32_t, std::string> video_stream_num_to_name;
    std::map<std::string, uint32_t> name_to_stream_num;
    uint32_t frame_id_counter;

    bool OnHandshakeMessage(const fp_network::Handshake& msg) override;
    void OnDataMessage(const fp_network::Data& msg) override;
    void OnStateMessage(const fp_network::State& msg) override;
    void OnStreamInfoMessage(const fp_network::StreamInfo& msg) override;

    void ControllerCaptureThread(int poll_rate);

    void OnVideoFrame(const fp_network::HostDataFrame& msg);
    void OnAudioFrame(const fp_network::HostDataFrame& msg);

    std::unique_ptr<FramePresenterGL> presenter;
    std::unique_ptr<InputStreamer> input_streamer;
    std::unique_ptr<std::thread> controller_capture_thread;
};

DEFINE_ACTOR_GENERATOR(HostActor)