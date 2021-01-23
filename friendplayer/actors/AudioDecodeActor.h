#pragma once

#include "actors/TimerActor.h"

#include "streamer/AudioStreamer.h"

class AudioDecodeActor : public TimerActor {
public:
    AudioDecodeActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name);

    void OnInit(const std::optional<any_msg>& init_msg) override;
    void OnMessage(const any_msg& msg) override;
    void OnTimerFire() override;

private:
    void OnAudioFrame(const fp_actor::AudioData& audio_data);

    AudioStreamer audio_streamer;
    uint32_t stream_num;
};

DEFINE_ACTOR_GENERATOR(AudioDecodeActor)