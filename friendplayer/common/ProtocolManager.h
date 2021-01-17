#pragma once

#include <map>
#include <mutex>
#include <memory>
#include <thread>
#include <type_traits>

#include <asio/ip/udp.hpp>

class ClientProtocolHandler;
class HostProtocolHandler;
class ProtocolHandler;

constexpr long long HEARTBEAT_TIMEOUT_MS = 5 * 1000;

constexpr int NUM_ATTEMPTS = 3;

constexpr long long UPDATE_TIME = HEARTBEAT_TIMEOUT_MS / NUM_ATTEMPTS;

constexpr size_t MAX_CLIENTS = 4;

class ProtocolManager {
public:
    using asio_endpoint = asio::ip::udp::endpoint;
    using client_id = int;

    static constexpr int CLIENT_ID_NOT_FOUND = -1;

    ProtocolManager() : next_client_id(0) {
        mgr_m = std::make_unique<std::mutex>();
        listener_active = std::make_unique<std::atomic_bool>(true);
        client_state_listener = std::make_unique<std::thread>(&ProtocolManager::ClientStateThread, this);
    }

    // GUARDED BY mgr_m
    template <typename _Fn>
    void LookupProtocolHandlerByEndpoint(const asio_endpoint& endpoint, _Fn&& cb) {
        std::lock_guard<std::mutex> lock(*mgr_m);
        auto ep_to_id_it = endpoint_to_id.find(endpoint);
        if (ep_to_id_it == endpoint_to_id.end()) {
            cb(nullptr);
            return;
        }

        auto id_to_handler_it = id_to_handler.find(ep_to_id_it->second);
        if (id_to_handler_it == id_to_handler.end()) {
            cb(nullptr);
            return;
        }
        cb(id_to_handler_it->second);
    }
    
    // WARNING: DOES NOT LOCK MUTEX! Only call from a threadsafe location :)
    HostProtocolHandler* CreateNewHostProtocol(const asio_endpoint& endpoint);
    ClientProtocolHandler* CreateNewClientProtocol(const asio_endpoint& endpoint);

    bool HasClients() { return !id_to_handler.empty(); }



    // Handles natural disconnects and heartbeats
    void ClientStateThread();

    template <typename Handler, typename _Fn>
    constexpr void foreach_client(_Fn&& client_cb) {
        static_assert(std::is_base_of_v<ProtocolHandler, Handler>, "Handler must be base class of ProtocolHandler!");
        std::lock_guard<std::mutex> lock(*mgr_m);
        for (auto&& [id, handler] : id_to_handler) {
            client_cb(static_cast<Handler*>(handler));
        }
    }

    ~ProtocolManager();

private:
    void DestroyClient(client_id id);
    // void DestroyClient(const asio_endpoint& endpoint);

    std::unique_ptr<std::mutex> mgr_m;
    std::map<asio_endpoint, client_id> endpoint_to_id;
    std::map<client_id, ProtocolHandler*> id_to_handler;

    std::unique_ptr<std::thread> client_state_listener;
    std::unique_ptr<std::atomic_bool> listener_active;

    client_id next_client_id;
};