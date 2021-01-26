#pragma once

#include "actors/Actor.h"

#include <asio/io_service.hpp>
#include <asio/ip/udp.hpp>

#include "protobuf/actor_messages.pb.h"

class SocketActor : public Actor {
private:
    static constexpr size_t BLOCK_SIZE = 16;
    // maximum chunk size over UDP accounding for proto overhead
    // and AES block encryption
    static constexpr size_t MAX_DATA_CHUNK = 476;
public:
    SocketActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
      : Actor(actor_map, buffer_map, std::move(name)),
        network_thread(nullptr),
        network_is_running(false),
        socket(io_service) {}

    void OnMessage(const any_msg& msg) override;
    void OnInit(const std::optional<any_msg>& init_msg) override;
    void OnFinish() override;

    void NetworkWorker();

protected:
    using asio_service = asio::io_service;
    using asio_socket = asio::ip::udp::socket;
    using asio_endpoint = asio::ip::udp::endpoint;
    using asio_address = asio::ip::address_v4;

    std::unique_ptr<std::thread> network_thread;
    bool network_is_running;

    asio_service io_service;
    asio_socket socket;
    asio_endpoint endpoint;
};

DEFINE_ACTOR_GENERATOR(SocketActor)

class HostSocketActor : public SocketActor {
private:
    static constexpr size_t CLIENT_SEND_SIZE = 1024 * 1024;

public:
    HostSocketActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
      : SocketActor(actor_map, buffer_map, std::move(name)) {}

    virtual ~HostSocketActor() { }

    void OnInit(const std::optional<any_msg>& init_msg) override {
        if (init_msg) {
            if (init_msg->Is<fp_actor::SocketInit>()) {
                fp_actor::SocketInit msg;
                init_msg->UnpackTo(&msg);

                endpoint = asio_endpoint(asio::ip::udp::v4(), msg.port());

                socket = asio_socket(io_service, endpoint);
                socket.set_option(asio::socket_base::send_buffer_size(CLIENT_SEND_SIZE));
            }
        }
        SocketActor::OnInit(init_msg);
    }

};

DEFINE_ACTOR_GENERATOR(HostSocketActor)

class ClientSocketActor : public SocketActor {
private:
    static constexpr size_t CLIENT_RECV_SIZE = 1024 * 1024;
    using asio_endpoint = asio::ip::udp::endpoint;

public:
    ClientSocketActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
      : SocketActor(actor_map, buffer_map, std::move(name)) {}

    virtual ~ClientSocketActor() { }

    void OnInit(const std::optional<any_msg>& init_msg) override {
        if (init_msg) {
            if (init_msg->Is<fp_actor::SocketInit>()) {
                fp_actor::SocketInit msg;
                init_msg->UnpackTo(&msg);

                endpoint = asio_endpoint(asio_address::from_string(msg.ip()), msg.port());
                socket.open(asio::ip::udp::v4());
                socket.set_option(asio::socket_base::receive_buffer_size(CLIENT_RECV_SIZE));
            }
        }
        SocketActor::OnInit(init_msg);
    }
};

DEFINE_ACTOR_GENERATOR(ClientSocketActor)