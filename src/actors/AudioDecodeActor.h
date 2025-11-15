#pragma once

#include "actors/TimerActor.h"

class AudioStreamer;

class AudioDecodeActor : public Actor {
public:
    AudioDecodeActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name);

    virtual ~AudioDecodeActor();

    void OnInit(const std::optional<any_msg>& init_msg) override;
    void OnMessage(const any_msg& msg) override;

private:
    void OnAudioFrame(const fp_actor::AudioData& audio_data);

    std::unique_ptr<AudioStreamer> audio_streamer;
    uint32_t stream_num;
};

DEFINE_ACTOR_GENERATOR(AudioDecodeActor)