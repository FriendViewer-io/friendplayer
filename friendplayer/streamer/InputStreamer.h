#pragma once

#include <Windows.h>
#include <ViGEm/Client.h>
#include <Xinput.h>
#include <iostream>
#include <map>
#include <string>
#include <protobuf/client_messages.pb.h>

#include <optional>

class InputStreamer {
public:
    InputStreamer();
    ~InputStreamer();

    bool RegisterVirtualController(std::string actor_name);

    void RegisterPhysicalController(const DWORD user_index);

    bool UpdateVirtualController(std::string actor_name, const fp_network::ControllerFrame& input);

    bool IsUserRegistered(std::string actor_name);

    std::optional<fp_network::ControllerFrame> CapturePhysicalController();

    bool is_physical_controller_registered() { return physical_controller_registered; }

private:
    struct ClientData {
        std::string actor_name;
        PVIGEM_TARGET controller;
        DWORD last_sequence_num;
    };

    bool physical_controller_registered;
    
    DWORD dw_user_index;
    
    PVIGEM_CLIENT vigem_client;
    std::map<std::string, ClientData> client_map;
    //PVIGEM_TARGET x360;

};

