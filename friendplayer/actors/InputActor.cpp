#include "actors/InputActor.h"

#include "streamer/InputStreamer.h"
#include "common/Log.h"

void InputActor::OnInit(const std::optional<any_msg>& init_msg) {
    input_streamer = new InputStreamer();
}

void InputActor::OnMessage(const any_msg& msg) {
    if (msg.Is<fp_actor::InputData>()) {
        fp_actor::InputData input_msg;
        msg.UnpackTo(&input_msg);
        OnInputData(input_msg);
    }
}

void InputActor::OnFinish() {
    delete input_streamer;
}

void InputActor::OnInputData(fp_actor::InputData& frame) {
    std::string actor = frame.actor_name();
    switch (frame.DataFrame_case()) {
        case fp_network::ClientDataFrame::kKeyboard:
            OnKeyboardFrame(actor, frame.keyboard());
            break;
        case fp_network::ClientDataFrame::kMouseButton:
            OnMouseButtonFrame(actor, frame.mouse_button());
            break;
        case fp_network::ClientDataFrame::kMouseMotion:
            OnMouseMotionFrame(actor, frame.mouse_motion());
            break;
        case fp_network::ClientDataFrame::kController:
            OnControllerFrame(actor, frame.controller());
            break;
        default:
            LOG_WARNING("Invalid dataframe recvd in Input actor");
    }
}

void InputActor::OnKeyboardFrame(std::string& actor_name, const fp_network::KeyboardFrame& msg) {

}

void InputActor::OnMouseButtonFrame(std::string& actor_name, const fp_network::MouseButtonFrame& msg) {

}

void InputActor::OnMouseMotionFrame(std::string& actor_name, const fp_network::MouseMotionFrame& msg) {

}

void InputActor::OnControllerFrame(std::string& actor_name, const fp_network::ControllerFrame& msg) {
    if(!input_streamer->IsUserRegistered(actor_name)) {
        input_streamer->RegisterVirtualController(actor_name);
    }
    input_streamer->UpdateVirtualController(actor_name, msg);
}