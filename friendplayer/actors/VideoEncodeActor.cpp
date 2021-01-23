#include "actors/VideoEncodeActor.h"

#include "actors/CommonActorNames.h"
#include "streamer/VideoStreamer.h"
#include "encoder/DDAImpl.h"
#include "common/Log.h"

VideoEncodeActor::VideoEncodeActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name) 
    : TimerActor(actor_map, buffer_map, std::move(name)),
      idr_requested(false),
      pps_sps_requested(false) {
    host_streamer = std::make_unique<VideoStreamer>();
}

void VideoEncodeActor::OnInit(const std::optional<any_msg>& init_msg) {
    TimerActor::OnInit(init_msg);
    if (init_msg) {
        if (init_msg->Is<fp_actor::VideoEncodeInit>()) {
            fp_actor::VideoEncodeInit encode_init_msg;
            init_msg->UnpackTo(&encode_init_msg);
            host_streamer->InitEncode(static_cast<int>(encode_init_msg.monitor_idx()));
            stream_num = encode_init_msg.stream_num();
            LOG_INFO("Created encoder {} for monitor {}", stream_num, encode_init_msg.monitor_idx());
        }
    } else {
        host_streamer->InitEncode(0);
        stream_num = 0;
    }
    SetTimerInternal(16, true);
}

void VideoEncodeActor::OnMessage(const any_msg& msg) {
    if (msg.Is<fp_actor::SpecialFrameRequest>()) {
        fp_actor::SpecialFrameRequest req;
        msg.UnpackTo(&req);
        if (req.type() == fp_actor::SpecialFrameRequest::IDR) {
            idr_requested = true;
        } else if (req.type() == fp_actor::SpecialFrameRequest::PPS_SPS) {
            idr_requested = true;
            pps_sps_requested = true;
        }
    } else {
        TimerActor::OnMessage(msg);
    }
}

void VideoEncodeActor::OnTimerFire() {
    std::string* data = new std::string();
    host_streamer->Encode(idr_requested, pps_sps_requested, *data);
    uint64_t handle = buffer_map.Wrap(data);
    fp_actor::VideoData video_data;
    video_data.set_stream_num(stream_num);
    video_data.set_handle(handle);
    if (pps_sps_requested) {
        video_data.set_type(fp_actor::VideoData::PPS_SPS);
    } else if (idr_requested) {
        video_data.set_type(fp_actor::VideoData::IDR);
    } else {
        video_data.set_type(fp_actor::VideoData::NORMAL);
    }
    
    idr_requested = false;
    pps_sps_requested = false;
    SendTo(CLIENT_MANAGER_ACTOR_NAME, video_data);
}