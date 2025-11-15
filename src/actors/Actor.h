#pragma once

#include "actors/BaseActor.h"
#include "actors/ActorType.h"

class Actor : public BaseActor {
public:
    Actor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
      : BaseActor(actor_map, buffer_map, std::move(name)) {}

    void OnInit(const std::optional<any_msg>&) override {}
    void OnMessage(const any_msg& msg) override;
    void OnFinish() override;

    virtual ~Actor() {}

private:
};

DEFINE_ACTOR_GENERATOR(Actor)