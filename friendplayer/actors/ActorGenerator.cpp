#include "actors/ActorGenerator.h"

#include <tuple>
#include <string_view>

template <typename _Fn, typename Acc, typename... T, std::size_t... Indices>
constexpr static auto fold_tuple_impl(_Fn&& f, const std::tuple<T...>& tuple, Acc init, std::index_sequence<Indices...>)
    -> Acc {
    return ((init = f(std::get<Indices>(tuple), init)), ...);
}

template <typename _Fn, typename Acc, typename... T>
constexpr static auto fold_tuple(_Fn&& f, const std::tuple<T...>& tuple, Acc init) {
    return fold_tuple_impl(std::forward<_Fn>(f), tuple, init, std::make_index_sequence<sizeof...(T)>());
}

template <std::size_t... Indices>
BaseActor* generate_dynamic(std::string_view name, const ActorMap& actor_map, DataBufferMap& buffer_map, std::string_view instance_name, std::index_sequence<Indices...>) {
    auto tup = std::tuple(
        actorgen::generate<Indices>(name, actor_map, buffer_map, instance_name)...);
    return fold_tuple([] (BaseActor* cur, BaseActor* acc) {
        if (cur == nullptr) {
            return acc;
        } else {
            return cur;
        } 
    }, tup, static_cast<BaseActor*>(nullptr));
}

BaseActor* generate_actor(std::string_view name, const ActorMap& actor_map, DataBufferMap& buffer_map, std::string_view instance_name) {
    return generate_dynamic(name, actor_map, buffer_map,
        instance_name, std::make_index_sequence<GET_UID_COUNT()>());
}