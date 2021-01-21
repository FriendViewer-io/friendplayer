#include "actors/VideoEncodeActor.h"

#include "actors/ClientManagerActor.h"
#include "encoder/DDAImpl.h"

void VideoEncodeActor::OnInit(const std::optional<any_msg>& init_msg) {
    host_streamer.InitEncode();
    SetTimerInternal(16, true);
    TimerActor::OnInit(init_msg);
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
    }
}

void VideoEncodeActor::OnTimerFire() {
    std::string* data = new std::string();
    host_streamer.Encode(idr_requested, pps_sps_requested, *data);
    uint64_t handle = buffer_map.Wrap(data);
    fp_actor::VideoData video_data;
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