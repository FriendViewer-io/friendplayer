#include "actors/ActorEnvironment.h"

#include "actors/ActorGenerator.h"
#include "actors/AdminActor.h"
#include "actors/BaseActor.h"
#include "actors/CommonActorNames.h"
#include "actors/DataBuffer.h"
#include "common/Log.h"

ActorEnvironment::ActorEnvironment() {
    actor_map = std::make_unique<ActorMap>();
    buffer_map = std::make_unique<DataBufferMap>();
    environment_state = std::make_unique<std::atomic<EnvState>>(EnvState::INACTIVE);
    admin_actor = std::make_shared<AdminActor>(*actor_map, *buffer_map);
    actor_map->SetAdminActor(admin_actor);
}

void ActorEnvironment::AddActor(std::string_view actor, std::string_view inst_name, const std::optional<google::protobuf::Any>& init_msg) {
    if (environment_state->load() != EnvState::INACTIVE) {
        LOG_WARNING("Tried to add an actor while active");
        return;
    }
    auto new_actor = generate_actor(actor, *actor_map, *buffer_map, inst_name);
    if (init_msg) {
        new_actor->SetInitMessage(*init_msg);
    }
    actor_map->AddActor(std::unique_ptr<BaseActor>(new_actor));
}

void ActorEnvironment::StartEnvironment() {
    actor_map->StartAll();
    admin_actor->MessageLoop();
}
