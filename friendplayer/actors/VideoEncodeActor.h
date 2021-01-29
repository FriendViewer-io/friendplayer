#pragma once

#include "actors/TimerActor.h"

class VideoStreamer;

class VideoEncodeActor : public TimerActor {
public:
    VideoEncodeActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name);

    virtual ~VideoEncodeActor();

    void OnInit(const std::optional<any_msg>& init_msg) override;
    void OnMessage(const any_msg& msg) override;
    void OnTimerFire() override;

private:
    std::unique_ptr<VideoStreamer> host_streamer;
    bool idr_requested;
    bool pps_sps_requested;
    uint32_t stream_num;
};

DEFINE_ACTOR_GENERATOR(VideoEncodeActor)