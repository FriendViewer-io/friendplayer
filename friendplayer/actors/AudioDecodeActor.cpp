#include "actors/AudioDecodeActor.h"

#include "actors/CommonActorNames.h"
#include "protobuf/network_messages.pb.h"

#include "common/Log.h"

AudioDecodeActor::AudioDecodeActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
    : TimerActor(actor_map, buffer_map, std::move(name)), stream_num(-1) {}

void AudioDecodeActor::OnInit(const std::optional<any_msg>& init_msg) {
    /*audio_streamer.InitDecoder();
    if (init_msg) {
        fp_actor::VideoDecodeInit decode_init_msg;
        init_msg->UnpackTo(&decode_init_msg);
        stream_num = decode_init_msg.stream_num();
    }*/
}

void AudioDecodeActor::OnMessage(const any_msg& msg) {
    /*if (msg.Is<fp_actor::VideoData>()) {
        fp_actor::VideoData video_data;
        msg.UnpackTo(&video_data);
        OnVideoFrame(video_data);
    } else {
        TimerActor::OnMessage(msg);
    }*/
}

void AudioDecodeActor::OnAudioFrame(const fp_actor::AudioData& audio_data) {
    if (audio_data.stream_num() != stream_num) {
        LOG_ERROR("Data frame sent to wrong stream, received on {} but expected {}", stream_num, audio_data.stream_num());
        return;
    } 
    std::string* data = buffer_map.GetBuffer(audio_data.handle());
    //audio_streamer.DecodeAudio()
    buffer_map.Decrement(audio_data.handle());
    
    //audio_streamer.PlayAudio();
    // Stop the existing timer and reset to the next 50ms interval
    //SetTimerInternal(50, false);
}

void AudioDecodeActor::OnTimerFire() {
    fp_actor::VideoDataRequest request;
    SendTo(HOST_ACTOR_NAME, request);
}