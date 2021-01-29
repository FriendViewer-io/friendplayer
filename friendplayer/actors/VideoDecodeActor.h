#pragma once

#include "actors/TimerActor.h"

class VideoStreamer;

class VideoDecodeActor : public TimerActor {
public:
    VideoDecodeActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name);

    virtual ~VideoDecodeActor();

    void OnInit(const std::optional<any_msg>& init_msg) override;
    void OnMessage(const any_msg& msg) override;
    void OnTimerFire() override;

private:
    void OnVideoFrame(const fp_actor::VideoData& video_data);

    std::unique_ptr<VideoStreamer> video_streamer;
    uint32_t stream_num;
};

DEFINE_ACTOR_GENERATOR(VideoDecodeActor)