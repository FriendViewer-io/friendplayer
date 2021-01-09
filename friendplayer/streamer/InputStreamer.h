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

        bool RegisterPhysicalController(const DWORD user_index);

        //for now, passing params that i have types for
        bool UpdateVirtualController(const fp_proto::ControllerFrame& input);

        std::optional<fp_proto::ControllerFrame> CapturePhysicalController();

    private:
        bool virtual_controller_registered;
        bool physical_controller_registered;
        
        DWORD last_sequence_num;
        
        DWORD dw_user_index;
        
        PVIGEM_CLIENT client;
        PVIGEM_TARGET x360;

};

