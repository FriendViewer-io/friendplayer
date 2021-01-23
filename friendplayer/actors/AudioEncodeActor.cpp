#include "actors/AudioEncodeActor.h"

#include "streamer/AudioStreamer.h"

#include "actors/CommonActorNames.h"

AudioEncodeActor::AudioEncodeActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
    : TimerActor(actor_map, buffer_map, std::move(name)), stream_num(-1) {
    audio_streamer = std::make_unique<AudioStreamer>();
}

void AudioEncodeActor::OnInit(const std::optional<any_msg>& init_msg) {
    audio_streamer->InitEncoder(64000);
    SetTimerInternal(20, true);
    if (init_msg) {
        fp_actor::AudioEncodeInit encode_init_msg;
        init_msg->UnpackTo(&encode_init_msg);
        stream_num = encode_init_msg.stream_num();
    }
}

void AudioEncodeActor::OnTimerFire() {
    std::string raw_frame;
    std::string* enc_frame = new std::string();
    audio_streamer->CaptureAudio(raw_frame);
    if (audio_streamer->EncodeAudio(raw_frame, *enc_frame)) {
        fp_actor::AudioData audio_data;
        audio_data.set_stream_num(stream_num);
        audio_data.set_handle(buffer_map.Wrap(enc_frame));
        SendTo(CLIENT_MANAGER_ACTOR_NAME, audio_data);
    }
}
