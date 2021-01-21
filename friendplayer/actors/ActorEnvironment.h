#pragma once

#include <atomic>
#include <memory>

class ActorMap;
class AdminActor;
class BaseActor;

class ActorEnvironment {
public:
    ActorEnvironment();

    void AddInitialActor(std::unique_ptr<BaseActor> actor);
    void StartEnvironment();
    void Shutdown();

private:
    std::unique_ptr<AdminActor> admin_actor;
    std::unique_ptr<ActorMap> actor_map;

    enum class EnvState {
        INACTIVE,
        RUNNING,
        SHUTTING_DOWN,
    };

    std::unique_ptr<std::atomic<EnvState>> environment_state;
};