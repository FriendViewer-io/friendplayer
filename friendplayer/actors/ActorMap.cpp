#include "actors/ActorMap.h"

#include "actors/BaseActor.h"
#include "actors/AdminActor.h"


void ActorMap::FindActor(std::string_view actor_name, std::function<void(BaseActor*)>&& cb) const {
    std::shared_lock<std::shared_mutex> r_lock(map_rw_m);
    auto actor_it = actors.find(actor_name);
    if (actor_it != actors.end()) {
        cb(actor_it->second.get());
    } else if (actor_name == admin_actor->GetName()) {
        cb(admin_actor.get());
    }
}

void ActorMap::SetAdminActor(std::shared_ptr<AdminActor> admin) {
    admin_actor = std::move(admin);
}

void ActorMap::AddAndStartActor(std::unique_ptr<BaseActor> new_actor) {
    std::unique_lock<std::shared_mutex> w_lock(map_rw_m);
    // TODO: something on repeat names
    auto tmp_ptr = new_actor.get();
    actors[tmp_ptr->GetName()] = std::move(new_actor);
    actors[tmp_ptr->GetName()]->StartActor();
}

void ActorMap::AddActor(std::unique_ptr<BaseActor> new_actor) {
    std::unique_lock<std::shared_mutex> w_lock(map_rw_m);
    // TODO: something on repeat names
    auto tmp_ptr = new_actor.get();
    actors[tmp_ptr->GetName()] = std::move(new_actor);
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

void ActorMap::StartAll() {
    for (auto&& [name, actor] : actors) {
        actor->StartActor();
    }
    admin_actor->StartActor();
}