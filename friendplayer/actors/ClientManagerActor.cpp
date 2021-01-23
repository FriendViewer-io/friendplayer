#include "actors/ClientManagerActor.h"

#include "actors/CommonActorNames.h"
#include "common/Log.h"
#include "protobuf/actor_messages.pb.h"

#include <asio/ip/udp.hpp>
#include <fmt/format.h>

void ClientManagerActor::OnInit(const std::optional<any_msg>& init_msg) {
    Actor::OnInit(init_msg);
    if (init_msg) {
        if (init_msg->Is<fp_actor::ClientClientManagerInit>()) {
            is_host = false;
            fp_actor::ClientClientManagerInit msg;
            init_msg->UnpackTo(&msg);
            uint64_t addr = static_cast<uint64_t>(msg.host_port()) << 32;
            addr |= asio::ip::address::from_string(msg.host_ip()).to_v4().to_uint();
            
            fp_actor::CreateHostActor host_actor;
            host_actor.set_host_address(addr);
            any_msg any;
            any.PackFrom(host_actor);
            EnqueueMessage(std::move(any));
        } else if (init_msg->Is<fp_actor::HostClientManagerInit>()) {
            is_host = true;
            fp_actor::HostClientManagerInit msg;
            init_msg->UnpackTo(&msg);
            stream_count = msg.monitor_indices_size();
            fp_actor::Create encoder_create_msg;
            encoder_create_msg.set_response_actor(GetName());
            encoder_create_msg.set_actor_type_name("VideoEncodeActor");
            fp_actor::VideoEncodeInit encode_init;
            for (int i = 0; i < stream_count; i++) {
                encoder_create_msg.set_actor_name(fmt::format(VIDEO_ENCODER_ACTOR_NAME_FORMAT, i));
                encode_init.set_monitor_idx(msg.monitor_indices(i));
                encode_init.set_stream_num(i);
                encoder_create_msg.mutable_init_msg()->PackFrom(encode_init);
                SendTo(ADMIN_ACTOR_NAME, encoder_create_msg);
            }
        } else {
            LOG_CRITICAL("ClientManagerActor initialized with unhandled init_msg of type {}!", init_msg->type_url());
        }
    } else {
        LOG_CRITICAL("ClientManagerActor initialized with no init_msg!");
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
        if (create_finish_msg.actor_type_name() == "VideoEncodeActor") {
            OnEncoderCreated(create_finish_msg.actor_name(), create_finish_msg.succeeded());
        } else if (create_finish_msg.actor_type_name() == "ClientActor"
            || create_finish_msg.actor_type_name() == "HostActor") {
            OnClientCreated(create_finish_msg.actor_name(), create_finish_msg.succeeded());
        }
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
    create_msg.set_response_actor(GetName());
    if (is_host) {
        create_msg.set_actor_type_name("ClientActor");
        create_msg.set_actor_name(fmt::format(CLIENT_ACTOR_NAME_TEMPLATE, request_id_counter++));
        fp_actor::ClientProtocolInit protocol_init_msg;
        protocol_init_msg.set_stream_count(stream_count);
        protocol_init_msg.mutable_base_init()->set_address(address);
        *create_msg.mutable_init_msg() = google::protobuf::Any();
        create_msg.mutable_init_msg()->PackFrom(protocol_init_msg);
    } else {
        create_msg.set_actor_type_name("HostActor");
        create_msg.set_actor_name(HOST_ACTOR_NAME);
        fp_actor::ProtocolInit protocol_init_msg;
        protocol_init_msg.set_address(address);
        *create_msg.mutable_init_msg() = google::protobuf::Any();
        create_msg.mutable_init_msg()->PackFrom(protocol_init_msg);
    }
    create_req_to_address[create_msg.actor_name()] = address;
    SendTo(ADMIN_ACTOR_NAME, create_msg);
}

void ClientManagerActor::OnEncoderCreated(const std::string& encoder_name, bool succeeded) {
    if (!succeeded) {
        LOG_WARNING("Failed to create video encoder {}!", encoder_name);
    }
}

void ClientManagerActor::OnClientCreated(const std::string& client_name, bool succeeded) {
    uint64_t client_address = create_req_to_address[client_name];
    if (succeeded) {
        while (!saved_messages[client_address].empty()) {
            auto& saved_message = saved_messages[client_address].front();
            SendTo(client_name, std::move(saved_message));
            saved_messages[client_address].pop();
        }
        address_to_client[client_address] = client_name;
    }
    saved_messages.erase(client_address);
    create_req_to_address.erase(client_name);

    fp_actor::ClientActorHeartbeatState heartbeat_state;
    heartbeat_state.set_client_actor_name(client_name);
    heartbeat_state.set_disconnected(false);
    SendTo(HEARTBEAT_ACTOR_NAME, heartbeat_state);
}