#include "actors/VideoDecodeActor.h"

#include "protobuf/network_messages.pb.h"

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
    }
}

void VideoDecodeActor::OnVideoFrame(const fp_actor::VideoData& video_data) {
    std::string* data = buffer_map.GetBuffer(video_data.handle());
    video_streamer.Decode(data);
    // TODO: Does this fuck up decoder???
    buffer_map.Decrement(video_data.handle());
    if (!video_streamer.IsDisplayInit()) {
        video_streamer.InitDisplay();

        // Have host start sending video data after receiving PPS SPS
        fp_actor::VideoDecoderReady ready_msg;
        SendTo("Host", ready_msg);

        // Ask for frames at ~60FPS
        SetTimerInternal(17, true);
    }
    video_streamer.PresentVideo();
}

void VideoDecodeActor::OnTimerFire() {
    fp_actor::VideoDataRequest request;
    SendTo("Host", request);
}