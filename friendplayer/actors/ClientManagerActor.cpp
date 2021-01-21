#include "actors/ClientManagerActor.h"

#include "actors/AdminActor.h"
#include "actors/ClientActor.h"
#include "protobuf/actor_messages.pb.h"

#include <fmt/format.h>


void ClientManagerActor::OnInit(const std::optional<any_msg>& init_msg) {
    Actor::OnInit(init_msg);
    if (init_msg) {
        if (init_msg->Is<fp_actor::ClientManagerInit>()) {
            fp_actor::ClientManagerInit msg;
            init_msg->UnpackTo(&msg);
            is_host = msg.is_host();
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
                fp_actor::Create create_msg;
                std::string actor_name = is_host ? fmt::format("Client{}", request_id_counter++) : "Host";
                create_msg.set_actor_type_name(is_host ? "ClientActor" : "HostActor");
                create_msg.set_response_actor(GetName());
                create_msg.set_actor_name(actor_name);
                fp_actor::ProtocolInit protocol_init_msg;
                protocol_init_msg.set_address(recv_msg.address());
                *create_msg.mutable_init_msg() = google::protobuf::Any();
                create_msg.mutable_init_msg()->PackFrom(protocol_init_msg);
                create_req_to_address[actor_name] = recv_msg.address();
                SendTo(ADMIN_ACTOR_NAME, create_msg);
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
    } else if (msg.Is<fp_actor::VideoData>()) {
        fp_actor::VideoData video_data_msg;
        msg.UnpackTo(&video_data_msg);
        for (auto&& [address, client_name] : address_to_client) {
            buffer_map.Increment(video_data_msg.handle());
            SendTo(client_name, msg);
        }
        buffer_map.Decrement(video_data_msg.handle());
    }
    Actor::OnMessage(msg);
}