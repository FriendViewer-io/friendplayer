#pragma once 

#include "actors/ActorMap.h"
#include "actors/BaseActor.h"

#include "actors/ActorType.h"


class AdminActor : public BaseActor {
public:
    AdminActor(ActorMap& actor_map, DataBufferMap& buffer_map);

    void OnInit(const std::optional<any_msg>&) override {}
    void OnMessage(const any_msg& msg) override;
    void StartActor() override;
    void OnFinish() override {}

private:
    ActorMap& writable_actor_map;
    bool shutting_down;
};

// DEFINE_ACTOR_GENERATOR(AdminActor)
// NO GENERATOR