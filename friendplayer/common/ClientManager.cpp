#include "common/ClientManager.h"

ClientManager::client_id ClientManager::LookupClientIdByEndpoint(const ClientManager::asio_endpoint& endpoint) {
    std::lock_guard<std::mutex> lock(*mgr_m);
    auto ep_to_id_it = endpoint_to_id.find(endpoint);
    if (ep_to_id_it == endpoint_to_id.end()) {
        return CLIENT_ID_NOT_FOUND;
    }
    return ep_to_id_it->second;
}

ClientHandler* ClientManager::LookupClientHandlerById(ClientManager::client_id id) {
    std::lock_guard<std::mutex> lock(*mgr_m);
    auto id_to_handler_it = id_to_handler.find(id);
    if (id_to_handler_it == id_to_handler.end()) {
        return nullptr;
    }
    return &id_to_handler_it->second;
}

ClientHandler* ClientManager::LookupClientHandlerByEndpoint(const ClientManager::asio_endpoint& endpoint) {
    std::lock_guard<std::mutex> lock(*mgr_m);
    auto ep_to_id_it = endpoint_to_id.find(endpoint);
    if (ep_to_id_it == endpoint_to_id.end()) {
        return nullptr;
    }

    auto id_to_handler_it = id_to_handler.find(ep_to_id_it->second);
    if (id_to_handler_it == id_to_handler.end()) {
        return nullptr;
    }
    return &id_to_handler_it->second;
}

ClientHandler* ClientManager::CreateNewClient(const ClientManager::asio_endpoint& endpoint) {
    std::lock_guard<std::mutex> lock(*mgr_m);
    if (id_to_handler.size() > MAX_CLIENTS) {
        return nullptr;
    }
    auto new_entry = id_to_handler.emplace(next_client_id, ClientHandler(next_client_id, endpoint));
    endpoint_to_id.emplace(endpoint, next_client_id);
    next_client_id++;

    return &new_entry.first->second;
}

void ClientManager::DestroyClient(ClientManager::client_id id) {
    std::lock_guard<std::mutex> lock(*mgr_m);
    auto id_to_handler_it = id_to_handler.find(id);
    if (id_to_handler_it == id_to_handler.end()) {
        return;
    }
    auto ep_to_id_it = endpoint_to_id.find(id_to_handler_it->second.GetEndpoint());
    
    if (ep_to_id_it != endpoint_to_id.end()) {
        endpoint_to_id.erase(ep_to_id_it);
    }
    id_to_handler_it->second.KillAllThreads();
    id_to_handler.erase(id_to_handler_it);
}

void ClientManager::DestroyClient(const ClientManager::asio_endpoint& endpoint)  {
    std::lock_guard<std::mutex> lock(*mgr_m);
    auto ep_to_id_it = endpoint_to_id.find(endpoint);
    if (ep_to_id_it == endpoint_to_id.end()) {
        return;
    }
    auto id_to_handler_it = id_to_handler.find(ep_to_id_it->second);

    if (id_to_handler_it != id_to_handler.end()) {
        id_to_handler_it->second.KillAllThreads();
        id_to_handler.erase(id_to_handler_it);
    }
    endpoint_to_id.erase(ep_to_id_it);
}

ClientManager::~ClientManager()  {
    for (auto it = id_to_handler.begin(); it != id_to_handler.end(); it++) {
        it->second.KillAllThreads();
        id_to_handler.erase(it);
    }
}