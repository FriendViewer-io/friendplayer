// #include "common/ClientProtocolHandler.h"

// #include "common/Log.h"

// using namespace std::chrono_literals;

// ClientProtocolHandler::ClientProtocolHandler(asio_endpoint endpoint)
//     : ProtocolHandler(-1, endpoint),
//       video_buffer("VideoBuffer", VIDEO_FRAME_BUFFER, VIDEO_FRAME_SIZE),
//       audio_buffer("AudioBuffer", AUDIO_FRAME_BUFFER, AUDIO_FRAME_SIZE) {}

// bool ClientProtocolHandler::DoHandshake() {
//     bool got_msg;

//     fp_proto::Message handshake_request;
//     *handshake_request.mutable_hs_msg() = fp_proto::HandshakeMessage();
//     handshake_request.mutable_hs_msg()->set_magic(0x46524E44504C5952ull);
    
//     protocol_state = HandshakeState::HS_WAITING_SHAKE_ACK;
//     LOG_INFO("Sending first handshake");
//     EnqueueSendMessage(std::move(handshake_request));

//     std::unique_lock<std::mutex> lock(*recv_message_queue_m);
//     got_msg = recv_message_queue_cv->wait_for(lock, 5s, [this] { return !recv_message_queue.empty(); });
//     if (!got_msg) {
//         return false;
//     }

//     LOG_INFO("Got handshake back");
//     fp_proto::Message incoming_msg = std::move(recv_message_queue.front());
//     recv_message_queue.pop_front();
//     if (incoming_msg.Payload_case() != fp_proto::Message::kHsMsg) {
//         return false;
//     }
//     if (incoming_msg.hs_msg().magic() != 0x46524E44504C5953ull) {
//         return false;
//     }

//     *handshake_request.mutable_hs_msg() = fp_proto::HandshakeMessage();
//     handshake_request.mutable_hs_msg()->set_magic(0x46524E44504C5954ull);
    
//     protocol_state = HandshakeState::HS_READY;
//     EnqueueSendMessage(std::move(handshake_request));
//     LOG_INFO("Done shaking");
//     return true;
// }

// void ClientProtocolHandler::OnDataFrame(const fp_network::Data& msg) {
//     switch(msg.Payload_case()){
//         case fp_network::Data::kClientFrame: {
//             LOG_WARNING("Client frame recievied from host side...???");
//         }
//         break;
//         case fp_network::Data::kHostFrame: {
//             const fp_proto::HostDataFrame& c_msg = msg.host_frame();
//             switch(c_msg.DataFrame_case()) {
//                 case fp_proto::HostDataFrame::kAudio:
//                     OnAudioFrame(c_msg);
//                     break;
//                 case fp_proto::HostDataFrame::kVideo:
//                     OnVideoFrame(c_msg);
//                     break;
//             }
//         }
//         break;
//     }
// }

// void ClientProtocolHandler::OnStateMessage(const fp_proto::StateMessage& msg) {
//     switch (msg.State_case()) {
//     case fp_proto::StateMessage::kClientState:
//         LOG_WARNING("Received ClientState message from host, ignoring");
//         break;
//     case fp_proto::StateMessage::kHostState:
//         OnHostState(msg.host_state());
//         break;
//     }
// }

// void ClientProtocolHandler::OnVideoFrame(const fp_proto::HostDataFrame& msg) {
//     video_buffer.AddFrameChunk(msg);
// }

// void ClientProtocolHandler::OnAudioFrame(const fp_proto::HostDataFrame& msg) {
//     audio_buffer.AddFrameChunk(msg);
// }

// void ClientProtocolHandler::OnHostState(const fp_proto::HostState& msg) {
//     LOG_INFO("Received HostState={}", static_cast<int>(msg.state()));
//     switch(msg.state()) {
//     case fp_proto::HostState::DISCONNECTING:
//         Transition(StreamState::DISCONNECTED);
//         break;
//     default:
//         LOG_INFO("Unknown HostState {}!", static_cast<int>(msg.state()));
//         break;
//     }
// }

// uint32_t ClientProtocolHandler::GetVideoFrame(RetrievedBuffer& buf_in) {
//     return video_buffer.GetFront(buf_in);
// }

// uint32_t ClientProtocolHandler::GetAudioFrame(RetrievedBuffer& buf_in) {
//     return audio_buffer.GetFront(buf_in);
//     // Run decryption
// }