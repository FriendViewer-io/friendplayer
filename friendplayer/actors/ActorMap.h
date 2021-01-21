#pragma once

#include <functional>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>

#include <google/protobuf/any.pb.h>

class BaseActor;

class ActorMap {
public:
    void FindActor(std::string_view actor_name, std::function<void(BaseActor*)>&& cb) const;

    void AddActor(std::unique_ptr<BaseActor> new_actor);
    std::unique_ptr<BaseActor> RemoveActor(std::string_view actor_name);

private:
    std::map<std::string, std::unique_ptr<BaseActor>, std::less<>> actors;
    mutable std::shared_mutex map_rw_m;
};