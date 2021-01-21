#include "actors/BaseActor.h"

#include <string_view>

class ActorMap;
class DataBufferMap;

BaseActor* generate_actor(std::string_view type_name, const ActorMap& actor_map, DataBufferMap& buffer_map, std::string_view instance_name);