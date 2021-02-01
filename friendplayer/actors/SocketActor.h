#pragma once

#include "actors/Actor.h"

#include <asio/io_service.hpp>
#include <asio/ip/udp.hpp>
#include <puncher_messages.pb.h>

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

    virtual ~SocketActor() {}

    void OnMessage(const any_msg& msg) override;
    void OnInit(const std::optional<any_msg>& init_msg) override;
    void OnFinish() override;

    void NetworkWorker();
    virtual void OnPuncherMessage(const fp_puncher::ServerMessage& msg) = 0;

protected:
    using asio_service = asio::io_service;
    using asio_socket = asio::ip::udp::socket;
    using asio_endpoint = asio::ip::udp::endpoint;
    using asio_address = asio::ip::address_v4;

    std::unique_ptr<std::thread> network_thread;
    bool network_is_running;

    asio_service io_service;
    asio_socket socket;

    bool use_holepunching;
    asio_endpoint holepunch_endpoint;
    std::string holepunch_identity;
    std::string session_token;
};

DEFINE_ACTOR_GENERATOR(SocketActor)

class HostSocketActor : public SocketActor {
private:
    static constexpr size_t CLIENT_SEND_SIZE = 1024 * 1024;

public:
    HostSocketActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
      : SocketActor(actor_map, buffer_map, std::move(name)) {}

    virtual ~HostSocketActor() { }

    void OnInit(const std::optional<any_msg>& init_msg) override;
    void OnPuncherMessage(const fp_puncher::ServerMessage& msg) override;
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

    void OnInit(const std::optional<any_msg>& init_msg) override;
    void OnPuncherMessage(const fp_puncher::ServerMessage& msg) override;
};

DEFINE_ACTOR_GENERATOR(ClientSocketActor)