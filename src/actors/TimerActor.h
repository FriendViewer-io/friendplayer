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
      : Actor(actor_map, buffer_map, std::move(name)), timer_handle(0), ignore_before(0), capture(nullptr) {}

    void OnInit(const std::optional<any_msg>&) override {}
    void OnMessage(const any_msg& msg) override;
    virtual void OnTimerFire() = 0;

    bool IsTimerActive() { return timer_handle != 0; }

    virtual ~TimerActor();

protected:
    void SetTimerInternal(uint32_t period_ms, bool periodic);
    void StopTimer();
    void IgnoreBeforeNow();

private:
    friend class WinMMTimer;
    void SendTimerFire(bool disarm, uint64_t timer_timestamp);

    struct TimerCapture {
        TimerActor* this_actor;
        uint64_t timestamp;
        bool is_periodic;
    };

    unsigned int timer_handle;
    bool is_periodic;
    uint64_t ignore_before;
    TimerCapture* capture;
};

DEFINE_ACTOR_GENERATOR(TimerActor)