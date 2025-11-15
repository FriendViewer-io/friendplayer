#pragma once

#include "actors/TimerActor.h"

#include <map>

class HeartbeatActor : public TimerActor {
public:
    HeartbeatActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
        : TimerActor(actor_map, buffer_map, std::move(name)) {}

    void OnInit(const std::optional<any_msg>& init_msg) override;
    void OnMessage(const any_msg& msg) override;
    void OnTimerFire() override;

    virtual ~HeartbeatActor();

private:
    using clock = std::chrono::system_clock;
    std::map<std::string, clock::time_point, std::less<>> heartbeat_map;
    std::chrono::milliseconds timeout_ms;

    static constexpr std::chrono::milliseconds DEFAULT_TIMEOUT = std::chrono::milliseconds(10000);
    static constexpr std::chrono::milliseconds DEFAULT_SEND = std::chrono::milliseconds(2000);
};

DEFINE_ACTOR_GENERATOR(HeartbeatActor)