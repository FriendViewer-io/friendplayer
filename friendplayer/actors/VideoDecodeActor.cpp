#include "actors/VideoDecodeActor.h"

#include "actors/CommonActorNames.h"
#include "protobuf/network_messages.pb.h"

#include "common/Log.h"

VideoDecodeActor::VideoDecodeActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
    : TimerActor(actor_map, buffer_map, std::move(name)) {}

void VideoDecodeActor::OnInit(const std::optional<any_msg>& init_msg) {
    video_streamer.InitDecode();
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
    std::string* data = buffer_map.GetBuffer(video_data.handle());
    video_streamer.Decode(data);
    buffer_map.Decrement(video_data.handle());
    if (!video_streamer.IsDisplayInit()) {
        video_streamer.InitDisplay();

        // Have host start sending video data after receiving PPS SPS
        fp_actor::VideoDecoderReady ready_msg;
        SendTo(HOST_ACTOR_NAME, ready_msg);
    }
    video_streamer.PresentVideo();
    // Stop the existing timer and reset to the next 50ms interval
    SetTimerInternal(50, false);
}

void VideoDecodeActor::OnTimerFire() {
    fp_actor::VideoDataRequest request;
    SendTo(HOST_ACTOR_NAME, request);
}