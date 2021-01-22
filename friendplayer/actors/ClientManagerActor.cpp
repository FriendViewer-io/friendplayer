#include "actors/ClientManagerActor.h"

#include "actors/CommonActorNames.h"
#include "protobuf/actor_messages.pb.h"

#include <fmt/format.h>

#include <asio/ip/udp.hpp>

void ClientManagerActor::OnInit(const std::optional<any_msg>& init_msg) {
    Actor::OnInit(init_msg);
    if (init_msg) {
        if (init_msg->Is<fp_actor::ClientManagerInit>()) {
            fp_actor::ClientManagerInit msg;
            init_msg->UnpackTo(&msg);
            is_host = msg.is_host();
            if (msg.has_host_ip() && msg.has_host_port()) {
                uint64_t addr = static_cast<uint64_t>(msg.host_port()) << 32;
                addr |= asio::ip::address::from_string(msg.host_ip()).to_v4().to_uint();
                
                fp_actor::CreateHostActor host_actor;
                host_actor.set_host_address(addr);
                any_msg any;
                any.PackFrom(host_actor);
                EnqueueMessage(std::move(any));
            }
        }
    }
}

void ClientManagerActor::OnMessage(const any_msg& msg) {
    if (msg.Is<fp_actor::NetworkRecv>()) {
        // Network message from socket
        fp_actor::NetworkRecv recv_msg;
        msg.UnpackTo(&recv_msg);

        // Check if this client exists
        if (address_to_client.find(recv_msg.address()) == address_to_client.end()) {
            // If not check if we need to request client creation
            if (saved_messages.find(recv_msg.address()) == saved_messages.end()) {
                // Ask admin to create a new client
                CreateClient(recv_msg.address());
            }
            // Save messages until client has been created by admin
            saved_messages[recv_msg.address()].emplace(std::move(recv_msg.msg()));
        } else {
            // Client exists, so send to them
            SendTo(address_to_client[recv_msg.address()], std::move(recv_msg.msg()));
        }
    } else if (msg.Is<fp_actor::CreateFinish>()) {
        // Admin has finished creating our client, pop all saved messages and send them
        // then add to our address -> client actor name map
        fp_actor::CreateFinish create_finish_msg;
        msg.UnpackTo(&create_finish_msg);
        uint64_t client_address = create_req_to_address[create_finish_msg.actor_name()];
        if (create_finish_msg.succeeded()) {
            while (!saved_messages[client_address].empty()) {
                auto& saved_message = saved_messages[client_address].front();
                SendTo(create_finish_msg.actor_name(), std::move(saved_message));
                saved_messages[client_address].pop();
            }
            address_to_client[client_address] = create_finish_msg.actor_name();
        }
        saved_messages.erase(client_address);
        create_req_to_address.erase(create_finish_msg.actor_name());

        fp_actor::ClientActorHeartbeatState heartbeat_state;
        heartbeat_state.set_client_actor_name(create_finish_msg.actor_name());
        heartbeat_state.set_disconnected(false);
        SendTo(HEARTBEAT_ACTOR_NAME, heartbeat_state);
    } else if (msg.Is<fp_actor::CreateHostActor>()) {
        fp_actor::CreateHostActor create_host_msg;
        msg.UnpackTo(&create_host_msg);
        CreateClient(create_host_msg.host_address());
    } else if (msg.Is<fp_actor::ClientTimeoutNotify>()) {
        fp_actor::ClientTimeoutNotify timeout_msg;
        msg.UnpackTo(&timeout_msg);
        if (is_host) {
            fp_actor::Kill kill_msg;
            SendTo(timeout_msg.client_actor_name(), kill_msg);
            for (auto it = address_to_client.begin(); it != address_to_client.end(); it++) {
                if (it->second == timeout_msg.client_actor_name()) {
                    saved_messages.erase(it->first);
                    address_to_client.erase(it);
                    break;
                }
            }
            create_req_to_address.erase(timeout_msg.client_actor_name());
        } else {
            // send to admin shutdown

        }
    } else if (msg.Is<fp_actor::VideoData>()) {
        fp_actor::VideoData video_data_msg;
        msg.UnpackTo(&video_data_msg);
        for (auto&& [address, client_name] : address_to_client) {
            buffer_map.Increment(video_data_msg.handle());
            SendTo(client_name, video_data_msg);
        }
        buffer_map.Decrement(video_data_msg.handle());
    } else {
        // pass it up to parent class
        Actor::OnMessage(msg);
    }
}

void ClientManagerActor::CreateClient(uint64_t address) {
    fp_actor::Create create_msg;
    std::string actor_name = is_host ? fmt::format(CLIENT_ACTOR_NAME_TEMPLATE, request_id_counter++) : HOST_ACTOR_NAME;
    create_msg.set_actor_type_name(is_host ? "ClientActor" : "HostActor");
    create_msg.set_response_actor(GetName());
    create_msg.set_actor_name(actor_name);
    fp_actor::ProtocolInit protocol_init_msg;
    protocol_init_msg.set_address(address);
    *create_msg.mutable_init_msg() = google::protobuf::Any();
    create_msg.mutable_init_msg()->PackFrom(protocol_init_msg);
    create_req_to_address[actor_name] = address;
    SendTo(ADMIN_ACTOR_NAME, create_msg);
}