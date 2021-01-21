#pragma once

#include <Windows.h>
#include <ViGEm/Client.h>
#include <Xinput.h>
#include <iostream>
#include <protobuf/client_messages.pb.h>

#include <optional>

class InputStreamer {
    public:
        InputStreamer();
        ~InputStreamer();

        bool RegisterVirtualController();

        void RegisterPhysicalController(const DWORD user_index);

        //for now, passing params that i have types for
        bool UpdateVirtualController(const fp_network::ControllerFrame& input);

        std::optional<fp_network::ControllerFrame> CapturePhysicalController();

        bool is_virtual_controller_registered() { return virtual_controller_registered; }
        bool is_physical_controller_registered() { return physical_controller_registered; }

    private:
        bool virtual_controller_registered;
        bool physical_controller_registered;
        
        DWORD last_sequence_num;
        
        DWORD dw_user_index;
        
        PVIGEM_CLIENT client;
        PVIGEM_TARGET x360;

};

