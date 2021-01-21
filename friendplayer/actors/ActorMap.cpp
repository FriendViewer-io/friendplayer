#include "actors/ActorMap.h"

#include "actors/BaseActor.h"

void ActorMap::FindActor(std::string_view actor_name, std::function<void(BaseActor*)>&& cb) const {
    std::shared_lock<std::shared_mutex> r_lock(map_rw_m);
    auto actor_it = actors.find(actor_name);
    if (actor_it != actors.end()) {
        cb(actor_it->second.get());
    }
}

void ActorMap::AddActor(std::unique_ptr<BaseActor> new_actor) {
    std::unique_lock<std::shared_mutex> w_lock(map_rw_m);
    // TODO: something on repeat names
    actors[new_actor->GetName()] = std::move(new_actor);
    actors[new_actor->GetName()]->StartActor();
}

std::unique_ptr<BaseActor> ActorMap::RemoveActor(std::string_view name) {
    std::unique_lock<std::shared_mutex> w_lock(map_rw_m);
    auto actor_it = actors.find(name);
    if (actor_it == actors.end()) {
        return nullptr;
    }
    auto ret = std::move(actor_it->second);
    actors.erase(actor_it);
    return std::move(ret);
}