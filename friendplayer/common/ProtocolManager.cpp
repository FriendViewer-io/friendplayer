#include "common/ProtocolManager.h"

#include "common/Log.h"
#include "common/HostProtocolHandler.h"
#include "common/ClientProtocolHandler.h"
#include "common/ProtocolHandler.h"

#include <chrono>

using namespace std::chrono_literals;

ProtocolManager::client_id ProtocolManager::LookupClientIdByEndpoint(const ProtocolManager::asio_endpoint& endpoint) {
    std::lock_guard<std::mutex> lock(*mgr_m);
    auto ep_to_id_it = endpoint_to_id.find(endpoint);
    if (ep_to_id_it == endpoint_to_id.end()) {
        return CLIENT_ID_NOT_FOUND;
    }
    return ep_to_id_it->second;
}

ProtocolHandler* ProtocolManager::LookupProtocolHandlerById(ProtocolManager::client_id id) {
    std::lock_guard<std::mutex> lock(*mgr_m);
    auto id_to_handler_it = id_to_handler.find(id);
    if (id_to_handler_it == id_to_handler.end()) {
        return nullptr;
    }
    return id_to_handler_it->second;
}

ProtocolHandler* ProtocolManager::LookupProtocolHandlerByEndpoint(const ProtocolManager::asio_endpoint& endpoint) {
    std::lock_guard<std::mutex> lock(*mgr_m);
    auto ep_to_id_it = endpoint_to_id.find(endpoint);
    if (ep_to_id_it == endpoint_to_id.end()) {
        return nullptr;
    }

    auto id_to_handler_it = id_to_handler.find(ep_to_id_it->second);
    if (id_to_handler_it == id_to_handler.end()) {
        return nullptr;
    }
    return id_to_handler_it->second;
}

HostProtocolHandler* ProtocolManager::CreateNewHostProtocol(const ProtocolManager::asio_endpoint& endpoint) {
    std::lock_guard<std::mutex> lock(*mgr_m);
    if (id_to_handler.size() > MAX_CLIENTS) {
        return nullptr;
    }
    auto new_handler = new HostProtocolHandler(next_client_id, endpoint);
    id_to_handler.emplace(next_client_id, new_handler);
    endpoint_to_id.emplace(endpoint, next_client_id);
    next_client_id++;
    
    return new_handler;
}

ClientProtocolHandler* ProtocolManager::CreateNewClientProtocol(const ProtocolManager::asio_endpoint& endpoint) {
    std::lock_guard<std::mutex> lock(*mgr_m);
    if (id_to_handler.size() > MAX_CLIENTS) {
        return nullptr;
    }
    auto new_handler = new ClientProtocolHandler(endpoint);
    id_to_handler.emplace(-1, new_handler);
    endpoint_to_id.emplace(endpoint, next_client_id);

    return new_handler;
}

void ProtocolManager::DestroyClient(ProtocolManager::client_id id) {
    std::lock_guard<std::mutex> lock(*mgr_m);
    auto id_to_handler_it = id_to_handler.find(id);
    if (id_to_handler_it == id_to_handler.end()) {
        return;
    }
    auto ep_to_id_it = endpoint_to_id.find(id_to_handler_it->second->GetEndpoint());
    
    if (ep_to_id_it != endpoint_to_id.end()) {
        endpoint_to_id.erase(ep_to_id_it);
    }
    id_to_handler_it->second->KillAllThreads();
    delete (id_to_handler_it->second);
    id_to_handler.erase(id_to_handler_it);
}

void ProtocolManager::DestroyClient(const ProtocolManager::asio_endpoint& endpoint)  {
    std::lock_guard<std::mutex> lock(*mgr_m);
    auto ep_to_id_it = endpoint_to_id.find(endpoint);
    if (ep_to_id_it == endpoint_to_id.end()) {
        return;
    }
    auto id_to_handler_it = id_to_handler.find(ep_to_id_it->second);

    if (id_to_handler_it != id_to_handler.end()) {
        id_to_handler_it->second->KillAllThreads();
        delete (id_to_handler_it->second);
        id_to_handler.erase(id_to_handler_it);
    }
    endpoint_to_id.erase(ep_to_id_it);
}

void ProtocolManager::ClientStateThread() {
    const int64_t timeout_time = std::chrono::milliseconds(HEARTBEAT_TIMEOUT_MS).count();
    while (listener_active->load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(UPDATE_TIME));
        const int64_t cur_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        for (auto it = id_to_handler.begin(); it != id_to_handler.end(); it++) {
            if (it->second->GetLastHeartbeat() + timeout_time < cur_time) {
                LOG_INFO("Client {} timed out", it->second->GetId());
                it->second->Transition(ProtocolHandler::StreamState::DISCONNECTED);
            }
            if (it->second->GetState() == ProtocolHandler::StreamState::DISCONNECTED) {
                DestroyClient(it->first);
            } else {
                fp_proto::Message send_msg;
                *send_msg.mutable_hb_msg() = fp_proto::HeartbeatMessage();
                send_msg.mutable_hb_msg()->set_is_response(false);
                it->second->EnqueueSendMessage(std::move(send_msg));
            }
        }
    }
}

ProtocolManager::~ProtocolManager() {
    listener_active->store(false);
    client_state_listener->join();

    for (auto it = id_to_handler.begin(); it != id_to_handler.end(); it++) {
        it->second->KillAllThreads();
        delete (it->second);
        id_to_handler.erase(it);
    }
}