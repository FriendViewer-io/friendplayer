#include "common/HeartbeatManager.h"

#include "common/ClientManager.h"
#include "common/Log.h"

#include <chrono>

using namespace std::chrono_literals;

void HeartbeatManager::StartHeartbeatManager() {
    active->store(true);
    heartbeat_thread = std::make_unique<std::thread>(&HeartbeatManager::HeartbeatThread, this);
}

void HeartbeatManager::HeartbeatThread() {
    while (active->load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(UPDATE_TIME));
        std::lock_guard<std::mutex> lock(*active_clients_m);
        
        //destroy any clients which have waited at least NUM_ATTEMPTS*UPDATE_TIME, and add one to attempts
        for (auto it = attempts_map.begin(); it != attempts_map.end(); it++) {
            if (it->second >= NUM_ATTEMPTS) {
                LOG_INFO("Client id={} timed out", it->first);
                client_manager->DestroyClient(it->first);
                attempts_map.erase(it);
                active_clients.erase(std::find(active_clients.begin(), active_clients.end(), it->first));
            } else {
                attempts_map[it->first] = attempts_map[it->first] + 1;
            }
        }
        
        //send a heartbeat to all active clients
        for (int i : active_clients) {
            ClientHandler* this_client = client_manager->LookupClientHandlerById(i);

            fp_proto::Message send_msg;
            *send_msg.mutable_hb_msg() = fp_proto::HeartbeatMessage();
            send_msg.mutable_hb_msg()->set_is_response(false);
            this_client->EnqueueSendMessage(std::move(send_msg));
        }  
    }
}

void HeartbeatManager::RegisterClient(int id) {
    std::lock_guard<std::mutex> lock(*active_clients_m);
    
    active_clients.emplace_back(id);
    attempts_map[id] = 0;
}

void HeartbeatManager::UpdateClient(int id) {
    std::lock_guard<std::mutex> lock(*active_clients_m);
    
    auto active_client_it = std::find(active_clients.begin(), active_clients.end(), id);
    if (active_client_it != active_clients.end()) {
        attempts_map[id] = 0;
    } else {
        LOG_WARNING("Attempt to hb-update invalid client id={}", id);
    }
}