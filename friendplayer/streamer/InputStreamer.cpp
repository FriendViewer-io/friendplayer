#include "InputStreamer.h"

#include "common/Log.h"


InputStreamer::InputStreamer() {
    this->dw_user_index = 0;
    this->last_sequence_num = 0;
    this->virtual_controller_registered = false;
    this->physical_controller_registered = false;

    this->client = nullptr;
    this->x360 = nullptr;
}

InputStreamer::~InputStreamer(){
    if(virtual_controller_registered)
    {
        vigem_target_remove(this->client, this->x360);
        vigem_target_free(this->x360);
    }
}

//allocate a new controller, apperently multi-controller happens automatically without
//providing an index or whatever, so main thing is to keep track of this->x360 and map that 
//to users
bool InputStreamer::RegisterVirtualController()
{
    //upon registration of first controller, open a connection with the ViGEm bus.
    //not done in the constructor because there's no need to do this until a controller is actually connected

    //TODO:take another look at this when dealing with multiple users
    if(this->client == nullptr)
    {
        this->client = vigem_alloc();

        if (client == nullptr)
        {
            LOG_CRITICAL("vigem_alloc() not enough memory");
            return false;
        }

        const auto retval = vigem_connect(this->client);
        
        if (!VIGEM_SUCCESS(retval))
        {
            LOG_CRITICAL("ViGEm Bus connection failed with error code: {}", retval);
            return false;
        }

        this->virtual_controller_registered = true;
    }

    this->x360 = vigem_target_x360_alloc();
    
    const auto pir = vigem_target_add(client, x360);
    if(!VIGEM_SUCCESS(pir))
    {
        LOG_CRITICAL("Target plugin failed with error code: {}", pir);
        return false;
    }

    //TODO: Register notification handler for vibration, forward back with network  

    return true;
}

void InputStreamer::RegisterPhysicalController(const DWORD user_index)
{
    this->dw_user_index = user_index;
    this->physical_controller_registered = true;
}

//TODO:take a protobuf struct as input
bool InputStreamer::UpdateVirtualController(const fp_network::ControllerFrame& input)
{
    if(!this->virtual_controller_registered)
    {
        LOG_WARNING("Attemping to update unallocated virtual controller, seq_num: {}", input.sequence_num());
        return false;
    }
    if(input.sequence_num() > this->last_sequence_num)
    {
        this->last_sequence_num = input.sequence_num();

        XUSB_REPORT state;
        state.bLeftTrigger = static_cast<BYTE>(input.b_left_trigger());
        state.bRightTrigger = static_cast<BYTE>(input.b_right_trigger());
        state.sThumbLX = static_cast<SHORT>(input.s_thumb_lx());
        state.sThumbLY = static_cast<SHORT>(input.s_thumb_ly());
        state.sThumbRX = static_cast<SHORT>(input.s_thumb_rx());
        state.sThumbRY = static_cast<SHORT>(input.s_thumb_ry());
        state.wButtons = static_cast<USHORT>(input.w_buttons());
        vigem_target_x360_update(this->client, this->x360, state);
        return true;
    }
    else
        return false;  
}

//TODO: return into a protobuf struct
std::optional<fp_network::ControllerFrame> InputStreamer::CapturePhysicalController()
{

    if(!this->physical_controller_registered)
    {
        LOG_WARNING("Attemping to capture unallocated physical controller");
        return std::nullopt;
    }
    XINPUT_STATE state;
    XInputGetState(this->dw_user_index, &state);

    fp_network::ControllerFrame return_frame;

    return_frame.set_sequence_num(state.dwPacketNumber);
    return_frame.set_b_left_trigger(state.Gamepad.bLeftTrigger);
    return_frame.set_b_right_trigger(state.Gamepad.bRightTrigger);
    return_frame.set_s_thumb_lx(state.Gamepad.sThumbLX);
    return_frame.set_s_thumb_ly(state.Gamepad.sThumbLY);
    return_frame.set_s_thumb_rx(state.Gamepad.sThumbRX);
    return_frame.set_s_thumb_ry(state.Gamepad.sThumbRY);
    return_frame.set_w_buttons(state.Gamepad.wButtons);

    return return_frame;

}










