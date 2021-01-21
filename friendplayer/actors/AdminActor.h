#pragma once 

#include "actors/ActorMap.h"
#include "actors/BaseActor.h"

#include "actors/ActorType.h"

constexpr const char ADMIN_ACTOR_NAME[] = "admin";

class AdminActor : public BaseActor {
public:
    AdminActor(ActorMap& actor_map, DataBufferMap& buffer_map);

    void OnInit(const std::optional<any_msg>&) override {}
    void OnMessage(const any_msg& msg) override;
    void StartActor() override;

private:
    ActorMap& writable_actor_map;
};

// DEFINE_ACTOR_GENERATOR(AdminActor)
// NO GENERATOR