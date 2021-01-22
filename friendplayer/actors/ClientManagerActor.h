#pragma once

#include "actors/Actor.h"

#include <map>
#include <queue>
#include <set>

#include "protobuf/network_messages.pb.h"

class ClientManagerActor : public Actor {
public:
    ClientManagerActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
        : Actor(actor_map, buffer_map, std::move(name)), request_id_counter(0) { }

    void OnMessage(const any_msg& msg) override;
    void OnInit(const std::optional<any_msg>& init_msg) override;

private:

    void CreateClient(uint64_t address);

    std::map<uint64_t, std::string> address_to_client;
    std::map<uint64_t, std::queue<fp_network::Network>> saved_messages;
    std::map<std::string, uint64_t> create_req_to_address;

    uint32_t request_id_counter;
    bool is_host;
};

DEFINE_ACTOR_GENERATOR(ClientManagerActor)