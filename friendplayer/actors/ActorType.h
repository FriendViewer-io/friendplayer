#pragma once

// From https://stackoverflow.com/questions/6166337/does-c-support-compile-time-counters
#define COUNTER_READ_CRUMB(TAG, RANK, ACC) counter_crumb(TAG(), constant_index<RANK>(), constant_index<ACC>())
#define COUNTER_READ(TAG) COUNTER_READ_CRUMB(TAG, 1, \
                          COUNTER_READ_CRUMB(TAG, 2, \
                          COUNTER_READ_CRUMB(TAG, 4, \
                          COUNTER_READ_CRUMB(TAG, 8, \
                          COUNTER_READ_CRUMB(TAG, 16, \
                          COUNTER_READ_CRUMB(TAG, 32, \
                          COUNTER_READ_CRUMB(TAG, 64, \
                          COUNTER_READ_CRUMB(TAG, 128, 0))))))))

#define COUNTER_INC(TAG) \
    constant_index<COUNTER_READ(TAG) + 1> \
    constexpr counter_crumb(TAG, constant_index<(COUNTER_READ(TAG) + 1) & ~COUNTER_READ(TAG)>, \
                  				 constant_index<(COUNTER_READ(TAG) + 1) & COUNTER_READ(TAG)>) { return { }; }
     
#include <utility>
#include <string_view>

template <std::size_t N>
struct constant_index : std::integral_constant<std::size_t, N> { };
     
namespace actorgen {
template <typename id, std::size_t rank, std::size_t acc>
constexpr constant_index<acc> counter_crumb(id, constant_index<rank>, constant_index<acc>) { return { }; }
     
struct actor_type_counter {};
}

// Generate a densely packed UID for an actor, as well as a generator based on provided name
// IMPORTANT! UIDs should only be used to iterate, and NOT to actually identify an actor!
// Including this header on separate TUs will cause conflict with these TYPEUIDs
#define DEFINE_ACTOR_GENERATOR(actor_name) \
    namespace actorgen { \
        COUNTER_INC(actor_type_counter); \
        static constexpr int actor_name##_TYPEUID = COUNTER_READ(actor_type_counter) - 1; \
        template<std::size_t type_uid, std::enable_if_t<(type_uid == actor_name##_TYPEUID) && (!std::is_abstract_v<actor_name>), bool> = true> \
        actor_name* generate(std::string_view val, const ActorMap& actor_map, DataBufferMap& data_map, std::string_view name) { \
            if (val == #actor_name) { \
                return new actor_name(actor_map, data_map, name); \
            } \
            return nullptr; \
        } \
        template<std::size_t type_uid, std::enable_if_t<(type_uid == actor_name##_TYPEUID) && (std::is_abstract_v<actor_name>), bool> = true> \
        actor_name* generate(std::string_view val, const ActorMap& actor_map, DataBufferMap& data_map, std::string_view name) { \
            return nullptr; \
        } \
    }

// Get highest generated number from integer generator
#define GET_UID_COUNT() COUNTER_READ(actorgen::actor_type_counter)