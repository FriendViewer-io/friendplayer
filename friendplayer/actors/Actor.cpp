#include "actors/Actor.h"

#include "actors/AdminActor.h"
#include "common/Log.h"
#include "protobuf/actor_messages.pb.h"

void Actor::OnMessage(const any_msg& msg) {
    if (msg.Is<fp_actor::Kill>()) {
        is_running = false;
    } else {
        LOG_WARNING("Actor {} received unhandled message type {}", GetName(), msg.type_url());
    }
}

void Actor::OnFinish() {
    fp_actor::Cleanup cleanup;
    cleanup.set_actor_name(GetName());
    SendTo(ADMIN_ACTOR_NAME, cleanup);

    LOG_INFO("Actor {} exiting", GetName());
}