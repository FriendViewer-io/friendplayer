#include "actors/AdminActor.h"

#include "actors/CommonActorNames.h"
#include "protobuf/actor_messages.pb.h"
#include "actors/ActorGenerator.h"

AdminActor::AdminActor(ActorMap& actor_map, DataBufferMap& buffer_map) 
  : BaseActor(actor_map, buffer_map, ADMIN_ACTOR_NAME),
    writable_actor_map(const_cast<ActorMap&>(actor_map)) {}

void AdminActor::OnMessage(const any_msg& msg) {
    if (msg.Is<fp_actor::Create>()) {
        fp_actor::Create create_message;
        msg.UnpackTo(&create_message);
        BaseActor* new_actor = generate_actor(create_message.actor_type_name(), writable_actor_map, buffer_map, create_message.actor_name());
        fp_actor::CreateFinish finish_msg;
        finish_msg.set_actor_name(create_message.actor_name());
        finish_msg.set_actor_type_name(create_message.actor_type_name());
        if (new_actor != nullptr) {
            finish_msg.set_succeeded(true);
            if (create_message.has_init_msg()) {
                new_actor->SetInitMessage(create_message.init_msg()); 
            }
            writable_actor_map.AddAndStartActor(std::unique_ptr<BaseActor>(new_actor));
        } else {
            finish_msg.set_succeeded(false);
        }
        SendTo(create_message.response_actor(), finish_msg);
    } else if (msg.Is<fp_actor::Cleanup>()) {
        fp_actor::Cleanup cleanup;
        msg.UnpackTo(&cleanup);
        writable_actor_map.RemoveActor(cleanup.actor_name());
    }
}

void AdminActor::StartActor() {
    is_running = true;
}