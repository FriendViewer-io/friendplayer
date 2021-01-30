#include "actors/InputActor.h"

#include "streamer/InputStreamer.h"
#include "common/Log.h"

struct MonitorEnum {
    HMONITOR monitor_handle;
    int index;
};

BOOL CALLBACK MonitorEnumProc(HMONITOR Arg1, HDC Arg2, LPRECT Arg3, LPARAM Arg4) {
    MonitorEnum* mon = reinterpret_cast<MonitorEnum*>(Arg4);
    if (mon->index == 0) {
        mon->monitor_handle = Arg1;
        return FALSE;
    } else if (mon->index > 0) {
        mon->index--;
    }
    return TRUE;
}

HMONITOR GetMonitorByIndex(int index) {
    MonitorEnum monitor;
    monitor.index = index;
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, reinterpret_cast<LPARAM>(&monitor));
    return monitor.monitor_handle;
}

void GetOffsetForMonitor(HMONITOR monitor_handle, int& x, int& y) {
    MONITORINFO monitor_info = { 0 };
    monitor_info.cbSize = sizeof(monitor_info);
    if (GetMonitorInfo(monitor_handle, &monitor_info)) {
        x += monitor_info.rcMonitor.left;
        y += monitor_info.rcMonitor.top;
    }
}

InputActor::InputActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
    : Actor(actor_map, buffer_map, std::move(name)), input_streamer(nullptr) {}

InputActor::~InputActor() {
    
}

void InputActor::OnInit(const std::optional<any_msg>& init_msg) {
    Actor::OnInit(init_msg);
    if (init_msg) {
        if (init_msg->Is<fp_actor::InputInit>()) {
            fp_actor::InputInit msg;
            init_msg->UnpackTo(&msg);
            input_streamer = std::make_unique<InputStreamer>(msg.reuse_controllers());
        }
    }
}

void InputActor::OnMessage(const any_msg& msg) {
    if (msg.Is<fp_actor::InputData>()) {
        fp_actor::InputData input_msg;
        msg.UnpackTo(&input_msg);
        OnInputData(input_msg);
    } else if (msg.Is<fp_actor::UnregisterInputUser>()) {
        fp_actor::UnregisterInputUser unregister_msg;
        msg.UnpackTo(&unregister_msg);
        input_streamer->UnregisterVirtualController(unregister_msg.actor_name());
    } else if (msg.Is<fp_actor::MonitorEnumIndex>()) {
        fp_actor::MonitorEnumIndex enum_index_msg;
        msg.UnpackTo(&enum_index_msg);
        HMONITOR monitor_handle = GetMonitorByIndex(enum_index_msg.monitor_enum_index());
        if (monitor_handle) {
            stream_num_to_monitor[enum_index_msg.stream_num()] = monitor_handle;
        }
    } else {
        Actor::OnMessage(msg);
    }
}

void InputActor::OnInputData(fp_actor::InputData& frame) {
    std::string actor = frame.actor_name();
    switch (frame.DataFrame_case()) {
        case fp_actor::InputData::kKeyboard:
            OnKeyboardFrame(actor, frame.keyboard());
            break;
        case fp_actor::InputData::kMouse:
            OnMouseFrame(actor, frame.mouse());
            break;
        case fp_actor::InputData::kController:
            OnControllerFrame(actor, frame.controller());
            break;
        default:
            LOG_WARNING("Invalid dataframe recvd in Input actor");
            break;
    }
}

void InputActor::OnKeyboardFrame(std::string& actor_name, const fp_network::KeyboardFrame& msg) {
    INPUT keyboard_press = { 0 };
    keyboard_press.type = INPUT_KEYBOARD;
    keyboard_press.ki.wVk = msg.key();
    keyboard_press.ki.wScan = MapVirtualKey(msg.key(), MAPVK_VK_TO_VSC);
    if (!msg.pressed()) {
        keyboard_press.ki.dwFlags = KEYEVENTF_KEYUP;
    }
    switch (msg.key()) {
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_MENU:
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_INSERT:
        case VK_DELETE:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_NUMLOCK:
        case VK_UP:
        case VK_DOWN:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_SNAPSHOT:
            keyboard_press.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            break;
    }
    SendInput(1, &keyboard_press, sizeof(INPUT));
}

void InputActor::OnMouseFrame(std::string& actor_name, const fp_network::MouseFrame& msg) {
    int x_pos = msg.mouse_x();
    int y_pos = msg.mouse_y();
    if (stream_num_to_monitor.find(msg.stream_num()) == stream_num_to_monitor.end()) {
        return;
    }
    GetOffsetForMonitor(reinterpret_cast<HMONITOR>(stream_num_to_monitor[msg.stream_num()]), x_pos, y_pos);
    SetCursorPos(x_pos, y_pos);
    if (msg.has_button()) {
        INPUT mouse_press = { 0 };
        mouse_press.type = INPUT_MOUSE;
        if (msg.button() == fp_network::MouseFrame::MOUSE_L) {
            mouse_press.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        } else if (msg.button() == fp_network::MouseFrame::MOUSE_MIDDLE) {
            mouse_press.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
        } else if (msg.button() == fp_network::MouseFrame::MOUSE_R) {
            mouse_press.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
        } else if (msg.button() == fp_network::MouseFrame::MOUSE_X1) {
            mouse_press.mi.dwFlags = MOUSEEVENTF_XDOWN;
            mouse_press.mi.mouseData = XBUTTON1;
        } else if (msg.button() == fp_network::MouseFrame::MOUSE_X2) {
            mouse_press.mi.dwFlags = MOUSEEVENTF_XDOWN;
            mouse_press.mi.mouseData = XBUTTON2;
        }
        mouse_press.mi.dwFlags = mouse_press.mi.dwFlags << (msg.pressed() ? 0 : 1);
        SendInput(1, &mouse_press, sizeof(INPUT));
    }
    if (msg.has_mouse_wheel_y()) {
        INPUT mouse_scroll = { 0 };
        mouse_scroll.type = INPUT_MOUSE;
        mouse_scroll.mi.dwFlags = MOUSEEVENTF_WHEEL;
        mouse_scroll.mi.mouseData = WHEEL_DELTA * msg.mouse_wheel_y();
        SendInput(1, &mouse_scroll, sizeof(INPUT));
    }
    if (msg.has_mouse_wheel_x()) {
        INPUT mouse_scroll = { 0 };
        mouse_scroll.type = INPUT_MOUSE;
        mouse_scroll.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        mouse_scroll.mi.mouseData = WHEEL_DELTA * msg.mouse_wheel_x();
        SendInput(1, &mouse_scroll, sizeof(INPUT));
    }
}

void InputActor::OnControllerFrame(std::string& actor_name, const fp_network::ControllerFrame& msg) {
    if(!input_streamer->IsUserRegistered(actor_name)) {
        input_streamer->RegisterVirtualController(actor_name);
    }
    input_streamer->UpdateVirtualController(actor_name, msg);
}