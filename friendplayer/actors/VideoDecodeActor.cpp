#include "actors/VideoDecodeActor.h"

#include "actors/CommonActorNames.h"
#include "common/Log.h"
#include "protobuf/network_messages.pb.h"
#include "streamer/VideoStreamer.h"

VideoDecodeActor::VideoDecodeActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
    : TimerActor(actor_map, buffer_map, std::move(name)), stream_num(-1) {
    video_streamer = std::make_unique<VideoStreamer>();
}

void VideoDecodeActor::OnInit(const std::optional<any_msg>& init_msg) {
    video_streamer->InitDecode();
    if (init_msg) {
        fp_actor::VideoDecodeInit decode_init_msg;
        init_msg->UnpackTo(&decode_init_msg);
        stream_num = decode_init_msg.stream_num();
    }
}

void VideoDecodeActor::OnMessage(const any_msg& msg) {
    if (msg.Is<fp_actor::VideoData>()) {
        fp_actor::VideoData video_data;
        msg.UnpackTo(&video_data);
        OnVideoFrame(video_data);
    } else {
        TimerActor::OnMessage(msg);
    }
}

void VideoDecodeActor::OnVideoFrame(const fp_actor::VideoData& video_data) {
    if (video_data.stream_num() != stream_num) {
        LOG_ERROR("Data frame sent to wrong stream, received on {} but expected {}", stream_num, video_data.stream_num());
        return;
    } 
    std::string* data = buffer_map.GetBuffer(video_data.handle());
    video_streamer->Decode(data);
    buffer_map.Decrement(video_data.handle());
    if (!video_streamer->IsDisplayInit()) {
        video_streamer->InitDisplay(stream_num);
        LOG_INFO("Ready for video");
        // Have host start sending video data after receiving PPS SPS
        fp_actor::VideoDecoderReady ready_msg;
        ready_msg.set_stream_num(stream_num);
        SendTo(HOST_ACTOR_NAME, ready_msg);
    }
    video_streamer->PresentVideo();
    // Stop the existing timer and reset to the next 50ms interval
    SetTimerInternal(150, false);
}

void VideoDecodeActor::OnTimerFire() {
    fp_actor::VideoDataRequest request;
    SendTo(HOST_ACTOR_NAME, request);
}