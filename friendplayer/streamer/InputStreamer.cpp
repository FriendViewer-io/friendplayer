#include "InputStreamer.h"

#include "common/Log.h"


InputStreamer::InputStreamer()
    : dw_user_index(0),
    physical_controller_registered(false),
    vigem_client(nullptr) { }

InputStreamer::~InputStreamer(){
    for (auto&& [actor_name, client] : client_map) {
        vigem_target_remove(vigem_client, client.controller);
        vigem_target_free(client.controller);
    }
    if (vigem_client != nullptr) {
        vigem_disconnect(vigem_client);
        vigem_free(vigem_client);
    }
}

bool InputStreamer::IsUserRegistered(std::string actor_name) {
    return client_map.find(actor_name) != client_map.end();
}

//allocate a new controller, apperently multi-controller happens automatically without
//providing an index or whatever, so main thing is to keep track of x360 and map that 
//to users
bool InputStreamer::RegisterVirtualController(std::string actor_name)
{
    //upon registration of first controller, open a connection with the ViGEm bus.
    //not done in the constructor because there's no need to do this until a controller is actually connected

    //TODO:take another look at this when dealing with multiple users
    if(vigem_client == nullptr)
    {
        vigem_client = vigem_alloc();

        if (vigem_client == nullptr)
        {
            LOG_CRITICAL("vigem_alloc() not enough memory");
            return false;
        }

        const auto retval = vigem_connect(vigem_client);
        
        if (!VIGEM_SUCCESS(retval))
        {
            LOG_CRITICAL("ViGEm Bus connection failed with error code: {}", retval);
            return false;
        }
    }
    
    ClientData new_client = {actor_name, vigem_target_x360_alloc(), 0};

    const auto err = vigem_target_add(vigem_client, client_map[actor_name].controller);
    if(!VIGEM_SUCCESS(err)) {
        vigem_target_free(new_client.controller);
        LOG_CRITICAL("Target plugin failed with error code: {}, actor name: {}", err, actor_name);
        return false;
    }

    client_map[actor_name] = new_client;
    //TODO: Register notification handler for vibration, forward back with network  

    return true;
}

void InputStreamer::RegisterPhysicalController(const DWORD user_index)
{
    dw_user_index = user_index;
    physical_controller_registered = true;
}

//TODO:take a protobuf struct as input
bool InputStreamer::UpdateVirtualController(std::string actor_name, const fp_network::ControllerFrame& input) {
    
    auto it = client_map.find(actor_name);
    if(it == client_map.end())
    {
        LOG_WARNING("Attemping to update unallocated virtual controller, seq_num: {}, actor name: {}", input.sequence_num(), actor_name);
        return false;
    }

    ClientData& this_client = client_map[actor_name];

    if(input.sequence_num() > this_client.last_sequence_num)
    {
        this_client.last_sequence_num = input.sequence_num();

        XUSB_REPORT state;
        state.bLeftTrigger = static_cast<BYTE>(input.b_left_trigger());
        state.bRightTrigger = static_cast<BYTE>(input.b_right_trigger());
        state.sThumbLX = static_cast<SHORT>(input.s_thumb_lx());
        state.sThumbLY = static_cast<SHORT>(input.s_thumb_ly());
        state.sThumbRX = static_cast<SHORT>(input.s_thumb_rx());
        state.sThumbRY = static_cast<SHORT>(input.s_thumb_ry());
        state.wButtons = static_cast<USHORT>(input.w_buttons());
        vigem_target_x360_update(vigem_client, this_client.controller, state);
        return true;
    }
    else
        return false;  
}

//TODO: return into a protobuf struct
std::optional<fp_network::ControllerFrame> InputStreamer::CapturePhysicalController() {

    if(!physical_controller_registered)
    {
        LOG_WARNING("Attemping to capture unallocated physical controller");
        return std::nullopt;
    }
    XINPUT_STATE state;
    XInputGetState(dw_user_index, &state);

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










