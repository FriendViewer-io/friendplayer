#include "actors/HeartbeatActor.h"

#include "actors/ClientManagerActor.h"
#include "protobuf/actor_messages.pb.h"


void HeartbeatActor::OnInit(const std::optional<any_msg>& init_msg) {
    TimerActor::OnInit(init_msg);
    if (init_msg) {
        if (init_msg->Is<fp_actor::HeartbeatActorInit>()) {
            fp_actor::HeartbeatActorInit init;
            init_msg->UnpackTo(&init);
            SetTimerInternal(init.heartbeat_send_ms(), true);
            timeout_ms = std::chrono::milliseconds(init.timeout_ms());
        }
    }
}

void HeartbeatActor::OnMessage(const any_msg& msg) {
    if (msg.Is<fp_actor::ClientActorHeartbeatState>()) {
        fp_actor::ClientActorHeartbeatState response;
        msg.UnpackTo(&response);
        if (response.disconnected()) {
            auto it = heartbeat_map.find(response.client_actor_name());
            if (it != heartbeat_map.end()) {
                heartbeat_map.erase(it);
            }
        } else {
            heartbeat_map[response.client_actor_name()] = clock::now();
        }
    }
}

void HeartbeatActor::OnTimerFire() {
    auto fire_time = clock::now();
    for (auto it = heartbeat_map.begin(); it != heartbeat_map.end(); it++) {
        if (it->second + timeout_ms < fire_time) {
            fp_actor::ClientTimeoutNotify timeout_msg;
            timeout_msg.set_client_actor_name(it->first);
            SendTo(CLIENT_MANAGER_ACTOR_NAME, timeout_msg);
            heartbeat_map.erase(it);
        }
    }
}