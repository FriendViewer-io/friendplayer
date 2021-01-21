// #pragma once

// #include <chrono>
// #include <condition_variable>
// #include <list>
// #include <future>
// #include <mutex>
// #include <thread>
// #include <vector>

// #include <asio/ip/udp.hpp>

// #include "common/ProtocolHandler.h"

// #include "protobuf/common_messages.pb.h"
// #include "protobuf/client_messages.pb.h"
// #include "protobuf/host_messages.pb.h"
// #include "streamer/InputStreamer.h"

// class ProtocolManager;
// class HostSocket;

// // Client management from host perspective
// class HostProtocolHandler : public ProtocolHandler {
// public:
//     HostProtocolHandler(int id, asio_endpoint endpoint);

//     void SetAudio(bool enabled) { audio_enabled = enabled; }
//     void SetKeyboard(bool enabled) { keyboard_enabled = enabled; }
//     void SetMouse(bool enabled) { mouse_enabled = enabled; }
//     void SetController(bool enabled) { controller_enabled = enabled; }

//     virtual void SetParentSocket(SocketBase* parent) {
//         ProtocolHandler::SetParentSocket(parent);
//         host_socket = reinterpret_cast<HostSocket*>(parent_socket);
//     }
    

//     void SendAudioData(const std::vector<uint8_t>& data);
//     void SendVideoData(const std::vector<uint8_t>& data, fp_proto::VideoFrame_FrameType type);
    
//     virtual ~HostProtocolHandler() {}
    
// private:
//     bool audio_enabled;
//     bool keyboard_enabled;
//     bool mouse_enabled;
//     bool controller_enabled;

//     InputStreamer input_streamer;

//     HostSocket* host_socket;

//     uint32_t video_stream_point;
//     uint32_t audio_stream_point;
//     uint32_t audio_frame_num;
//     uint32_t video_frame_num;
//     int pps_sps_version;

//     // GUARDED BY send_message_queue_m
//     bool DoHandshake();
//     // GUARDED BY recv_message_queue_m
//     void OnDataFrame(const fp_network::Data& msg);

//     void OnStateMessage(const fp_proto::StateMessage& msg);
//     void OnClientState(const fp_proto::ClientState& cl_state);

//     void OnKeyboardFrame(const fp_network::ClientDataFrame& msg);
//     void OnMouseFrame(const fp_network::ClientDataFrame& msg);
//     void OnControllerFrame(const fp_network::ClientDataFrame& msg);
//     void OnHostRequest(const fp_network::ClientDataFrame& msg);
//     void OnStreamState(const fp_network::ClientDataFrame& msg);

// };