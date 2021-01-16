#pragma once

#include <memory>
#include <mutex>
#include <vector>
#include <map>

class ClientManager;

constexpr long long HEARTBEAT_TIMEOUT_MS = 5 * 1000;

constexpr int NUM_ATTEMPTS = 3;

constexpr long long UPDATE_TIME = HEARTBEAT_TIMEOUT_MS / NUM_ATTEMPTS;

class HeartbeatManager {
public:
    HeartbeatManager(std::shared_ptr<ClientManager> client_manager)
      : client_manager(std::move(client_manager)), heartbeat_thread(nullptr) {
        active_clients_m = std::make_unique<std::mutex>();
        active = std::make_unique<std::atomic_bool>(false);
    }

    void StartHeartbeatManager();
    void HeartbeatThread();
    void RegisterClient(int id);
    void UnregisterClient(int id);
    void UpdateClient(int id);
    void Stop() { active->store(false); }

    ~HeartbeatManager() { if (heartbeat_thread) { heartbeat_thread->join(); }}

private:
    std::unique_ptr<std::thread> heartbeat_thread;
    std::unique_ptr<std::mutex> active_clients_m;
    std::vector<int> active_clients;
    std::map<int, int> attempts_map;

    std::shared_ptr<ClientManager> client_manager;
    std::unique_ptr<std::atomic_bool> active;
};