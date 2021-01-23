#pragma once

#include "actors/TimerActor.h"

#include "streamer/VideoStreamer.h"

class VideoDecodeActor : public TimerActor {
public:
    VideoDecodeActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name);

    void OnInit(const std::optional<any_msg>& init_msg) override;
    void OnMessage(const any_msg& msg) override;
    void OnTimerFire() override;

private:
    void OnVideoFrame(const fp_actor::VideoData& video_data);

    VideoStreamer video_streamer;
    uint32_t stream_num;
};

DEFINE_ACTOR_GENERATOR(VideoDecodeActor)