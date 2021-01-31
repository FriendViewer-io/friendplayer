#pragma once

#include "actors/Actor.h"

#include <mutex>
#include <map>

#include "protobuf/actor_messages.pb.h"
#include "protobuf/network_messages.pb.h"

struct GLFWwindow;

class HostSettingsActor : public Actor {
public:
    HostSettingsActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name);

    virtual ~HostSettingsActor();

    void OnMessage(const any_msg& msg) override;
    void OnInit(const std::optional<any_msg>& init_msg) override;
    void OnFinish() override;

private:
    struct Client {
        std::string actor_name;
        uint64_t address;
        std::string ip;
        unsigned short port;
        bool is_mouse_enabled;
        bool is_keyboard_enabled;
        bool is_controller_enabled;
        bool is_controller_connected;
        uint32_t ping;
    };

    std::mutex clients_mutex;
    std::map<std::string, Client> connected_clients;

    void OnClientAdded(const fp_actor::AddClientSettings& msg);
    void OnClientRemoved(const fp_actor::RemoveClientSettings& msg);
    void OnClientUpdated(const fp_actor::UpdateClientSetting& msg);

    void UpdateClientActorState(const Client& client);

    static void WindowCloseProc(GLFWwindow* window);

    void Present();

    bool stop = false;
    std::unique_ptr<std::thread> present_thread;
};

DEFINE_ACTOR_GENERATOR(HostSettingsActor)