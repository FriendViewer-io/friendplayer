#pragma once

#include <google/protobuf/any.pb.h>

#include "actors/Actor.h"
#include "actors/ActorMap.h"
#include "actors/BaseActor.h"
#include "protobuf/actor_messages.pb.h"

class TimerActor : public Actor {
public:
    using clock = std::chrono::system_clock;

    TimerActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
      : Actor(actor_map, buffer_map, std::move(name)), timer_handle(0) {
        fire_in_queue = std::make_unique<std::atomic_bool>(false);
    }

    void OnInit(const std::optional<any_msg>&) override {}
    void OnMessage(const any_msg& msg) override;
    virtual void OnTimerFire() = 0;

    bool IsTimerActive() { return timer_handle != 0; }

    virtual ~TimerActor();

protected:
    void SetTimerInternal(uint32_t period_ms, bool periodic);

private:
    friend class WinMMTimer;
    void StopTimer();
    void SendTimerFire(bool disarm);

    unsigned int timer_handle;
    std::unique_ptr<std::atomic_bool> fire_in_queue;
    bool is_periodic;
};

DEFINE_ACTOR_GENERATOR(TimerActor)