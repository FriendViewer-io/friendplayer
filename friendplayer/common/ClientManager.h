#pragma once

#include <map>
#include <mutex>
#include <memory>

#include <asio/ip/udp.hpp>

#include "common/HostProtocolHandler.h"

constexpr size_t MAX_CLIENTS = 4;

class ClientManager {
public:
    using asio_endpoint = asio::ip::udp::endpoint;
    using client_id = int;

    static constexpr int CLIENT_ID_NOT_FOUND = -1;

    ClientManager() : next_client_id(0) {
        mgr_m = std::make_unique<std::mutex>();
    }

    client_id LookupClientIdByEndpoint(const asio_endpoint& endpoint);
    HostProtocolHandler* LookupHostProtocolHandlerById(client_id id);
    HostProtocolHandler* LookupHostProtocolHandlerByEndpoint(const asio_endpoint& endpoint);
    HostProtocolHandler* CreateNewClient(const asio_endpoint& endpoint);

    // Spin up another thread to ensure the client joins properly
    void DestroyClient(client_id id);
    void DestroyClient(const asio_endpoint& endpoint);

    template <typename _Fn>
    constexpr void foreach_client(_Fn&& client_cb) {
        std::lock_guard<std::mutex> lock(*mgr_m);
        for (auto&& [id, handler] : id_to_handler) {
            client_cb(handler);
        }
    }

    ~ClientManager();

private:
    std::unique_ptr<std::mutex> mgr_m;
    std::map<asio_endpoint, client_id> endpoint_to_id;
    std::map<client_id, HostProtocolHandler> id_to_handler;

    client_id next_client_id;
};