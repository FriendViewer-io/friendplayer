#pragma once

#include <atomic>
#include <memory>
#include <optional>

#include <google/protobuf/any.pb.h>

#include "actors/ActorMap.h"
#include "actors/AdminActor.h"
#include "actors/BaseActor.h"
#include "actors/DataBuffer.h"

class ActorEnvironment {
public:
    ActorEnvironment();

    void AddActor(std::string_view name, std::string_view inst_name, const std::optional<google::protobuf::Any>& = std::nullopt);
    void StartEnvironment();

private:
    std::shared_ptr<AdminActor> admin_actor;
    std::unique_ptr<ActorMap> actor_map;
    std::unique_ptr<DataBufferMap> buffer_map;

    enum class EnvState {
        INACTIVE,
        RUNNING,
        SHUTTING_DOWN,
    };

    std::unique_ptr<std::atomic<EnvState>> environment_state;
};