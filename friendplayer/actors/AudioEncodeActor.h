#pragma once

#include "actors/TimerActor.h"

class AudioStreamer;

class AudioEncodeActor : public TimerActor {
public:
    AudioEncodeActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name);

    virtual ~AudioEncodeActor();

    void OnInit(const std::optional<any_msg>& init_msg) override;
    void OnTimerFire() override;

private:
    std::unique_ptr<AudioStreamer> audio_streamer;
    uint32_t stream_num;
};

DEFINE_ACTOR_GENERATOR(AudioEncodeActor)