#pragma once

#include <functional>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>

#include <google/protobuf/any.pb.h>

class AdminActor;
class BaseActor;

class ActorMap {
public:
    void FindActor(std::string_view actor_name, std::function<void(BaseActor*)>&& cb) const;

    void ForAllActors(std::function<void(BaseActor*)>&& cb);
    void AddAndStartActor(std::unique_ptr<BaseActor> new_actor);
    void AddActor(std::unique_ptr<BaseActor> new_actor);
    void SetAdminActor(std::shared_ptr<AdminActor> admin);
    std::unique_ptr<BaseActor> RemoveActor(std::string_view actor_name);
    bool IsEmpty();
    void StartAll();

private:
    std::map<std::string, std::unique_ptr<BaseActor>, std::less<>> actors;
    std::shared_ptr<BaseActor> admin_actor;
    mutable std::shared_mutex map_rw_m;
};