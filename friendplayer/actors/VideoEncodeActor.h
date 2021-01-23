#pragma once

#include "actors/TimerActor.h"

#include "streamer/VideoStreamer.h"

class VideoEncodeActor : public TimerActor {
public:
    VideoEncodeActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
      : TimerActor(actor_map, buffer_map, std::move(name)),
        idr_requested(false),
        pps_sps_requested(false) {}

    void OnInit(const std::optional<any_msg>& init_msg) override;
    void OnMessage(const any_msg& msg) override;
    void OnTimerFire() override;

private:
    VideoStreamer host_streamer;
    bool idr_requested;
    bool pps_sps_requested;
    uint32_t stream_num;
};

DEFINE_ACTOR_GENERATOR(VideoEncodeActor)