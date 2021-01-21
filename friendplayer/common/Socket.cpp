// #include "common/Socket.h"

// #include "common/Log.h"
// #include "common/FrameRingBuffer.h"
// #include "common/ClientProtocolHandler.h"
// #include "common/HostProtocolHandler.h"
// #include "common/ProtocolHandler.h"
// #include "common/ProtocolManager.h"

// void SocketBase::StartSocket() {
//     is_running.store(true);
//     network_thread = std::make_unique<std::thread>(&SocketBase::NetworkThread, this);
// }

// void SocketBase::Stop() {
//     is_running.store(false);
//     socket.close();
//     network_thread->join();
// }

// void SocketBase::MessageSend(const fp_network::Network& outgoing_msg, const asio_endpoint& target_endpoint) {
//     socket.send_to(asio::buffer(outgoing_msg.SerializeAsString()), target_endpoint);
// }

// ClientSocket::ClientSocket(std::string_view ip, unsigned short port,
//         std::shared_ptr<ProtocolManager> protocol_mgr)
//     : SocketBase(std::move(protocol_mgr)),
//       sent_frame_num(0),
//       idr_send_timeout(-1) {
//     host_endpoint = asio_endpoint(asio_address::from_string(std::string(ip)), port);
//     socket.open(asio::ip::udp::v4());
//     socket.set_option(asio::socket_base::receive_buffer_size(CLIENT_RECV_SIZE));

//     ClientProtocolHandler* client = this->protocol_mgr->CreateNewClientProtocol(host_endpoint);
//     client->SetParentSocket(this);
//     client->StartWorker();

// }

// void ClientSocket::MessageSend(const fp_network::Network& outgoing_msg) {
//     SocketBase::MessageSend(outgoing_msg, host_endpoint);
// }

// bool ClientSocket::BlockForHandshake() {
//     bool handshaked_successfully = false;
//     protocol_mgr->foreach_client<ClientProtocolHandler>([this, &handshaked_successfully] (ClientProtocolHandler* handler) {
//         handshaked_successfully = handler->BlockForHandshake();
//     });
//     return handshaked_successfully;
// }

// void ClientSocket::GetVideoFrame(RetrievedBuffer& buf_in) {
//     protocol_mgr->foreach_client<ClientProtocolHandler>([this, &buf_in] (ClientProtocolHandler* handler) {
//         uint32_t size_to_decrypt = handler->GetVideoFrame(buf_in);
//         // Run decryption
//         if (size_to_decrypt != 0 || buf_in.bytes_received == 0 || (buf_in.bytes_received < buf_in.data_out.size())) {
//             idr_send_timeout = IDR_SEND_TIMEOUT;
//         } else if (idr_send_timeout >= 0) {
//             if (idr_send_timeout == 0) {
//                 SendRequestToHost(fp_network::RequestToHost::SEND_IDR);
//                 LOG_INFO("Requesting IDR from host");
//             }
//             idr_send_timeout--;
//         }
//     });
// }

// void ClientSocket::GetAudioFrame(RetrievedBuffer& buf_in) {
//     protocol_mgr->foreach_client<ClientProtocolHandler>([this, &buf_in] (ClientProtocolHandler* handler) {
//         uint32_t size_to_decrypt = handler->GetAudioFrame(buf_in);
//         // Run decryption
//     });
// }

// void ClientSocket::SendStreamState(fp_network::ClientState::State state) {
//     protocol_mgr->foreach_client<ClientProtocolHandler>([state] (ClientProtocolHandler* handler) {
//         fp_network::Network msg;
//         fp_network::StateMessage& state_msg = *msg.mutable_state_msg();
//         fp_network::ClientState& client_state = *state_msg.mutable_client_state();
//         client_state.set_state(state);
//         handler->EnqueueSendMessage(std::move(msg));
//     });
// }

// void ClientSocket::SendRequestToHost(fp_network::RequestToHost::RequestType request) {
//     protocol_mgr->foreach_client<ClientProtocolHandler>([this, request] (ClientProtocolHandler* handler) {
//         fp_network::Network msg;
//         fp_network::Data& data_msg = *msg.mutable_data_msg();
//         data_msg.set_needs_ack(true);
//         fp_network::ClientDataFrame& client_frame = *data_msg.mutable_client_frame();
//         client_frame.mutable_host_request()->set_type(request);
//         client_frame.set_frame_id(sent_frame_num.fetch_add(1));
//         handler->EnqueueSendMessage(std::move(msg));
//     });
// }

// void ClientSocket::SendController(const fp_network::ControllerFrame& frame) {
//     protocol_mgr->foreach_client<ClientProtocolHandler>([this, &frame] (ClientProtocolHandler* handler) {
//         fp_network::Network msg;
//         fp_network::Data& data_msg = *msg.mutable_data_msg();
//         data_msg.set_needs_ack(false);
//         fp_network::ClientDataFrame& client_frame = *data_msg.mutable_client_frame();
//         client_frame.mutable_controller()->CopyFrom(frame);
//         handler->EnqueueSendMessage(std::move(msg));
//     });
// }

// void ClientSocket::NetworkThread() {
//     std::string recv_buffer;
//     recv_buffer.resize(1500);

//     asio::error_code ec;

//     while (is_running.load()) {
//         size_t size = socket.receive_from(asio::buffer(recv_buffer), host_endpoint, 0, ec);
//         if (size == 0) {
//             continue;
//         }
//         LOG_INFO("Recieved {} bytes", size);
//         protocol_mgr->foreach_client<ClientProtocolHandler>([this, size, &recv_buffer] (ClientProtocolHandler* handler) {
//             fp_network::Network msg;
//             msg.ParseFromArray(recv_buffer.data(), size);
//             LOG_INFO("Client recieved message");
//             handler->EnqueueRecvMessage(std::move(msg));
//         });
//     }
// }

// HostSocket::HostSocket(unsigned short port, std::shared_ptr<ProtocolManager> protocol_mgr)
//   : SocketBase(std::move(protocol_mgr)) {
//     endpoint = asio_endpoint(asio::ip::udp::v4(), port);
//     socket = asio_socket(io_service, endpoint);
//     socket.set_option(asio::socket_base::send_buffer_size(HOST_SEND_SIZE));
//     pps_sps_version = -1;
// }

// void HostSocket::SendStreamState(fp_network::HostState::State state) {
//     protocol_mgr->foreach_client<HostProtocolHandler>([state] (HostProtocolHandler* handler) {
//         fp_network::Network msg;
//         fp_network::StateMessage& state_msg = *msg.mutable_state_msg();
//         fp_network::HostState& host_state = *state_msg.mutable_host_state();
//         host_state.set_state(state);
//         handler->EnqueueSendMessage(std::move(msg));
//     });
// }

// void HostSocket::SetNeedIDR(bool need_idr) {
//     should_send_idr.store(need_idr);
// }

// bool HostSocket::ShouldSendIDR() {
//     return should_send_idr.exchange(false);
// }

// void HostSocket::NetworkThread() {
//     std::string recv_buffer;
//     recv_buffer.resize(1500);

//     asio_endpoint client_endpoint;

//     while (is_running.load()) {
//         size_t size = socket.receive_from(asio::buffer(recv_buffer), client_endpoint);
//         if (size == 0) {
//             continue;
//         }
//         protocol_mgr->LookupProtocolHandlerByEndpoint(client_endpoint, [this, size, &recv_buffer, &client_endpoint] (ProtocolHandler* handler) {
//             if (handler == nullptr) {
//                 handler = protocol_mgr->CreateNewHostProtocol(client_endpoint);
//                 if (handler == nullptr) {
//                     LOG_INFO("Client tried to join but there were too many clients.");
//                     return;
//                 }
//                 handler->SetParentSocket(this);
//                 handler->StartWorker();
//             }
//             fp_network::Network msg;
//             msg.ParseFromArray(recv_buffer.data(), size);
//             handler->EnqueueRecvMessage(std::move(msg));
//         });
//     }
// }

// void HostSocket::WriteVideoFrame(const std::vector<uint8_t>& data, bool is_idr_frame) {
//     protocol_mgr->foreach_client<HostProtocolHandler>([is_idr_frame, &data] (HostProtocolHandler* client_handler) {
//         if (is_idr_frame) {
//             client_handler->SendVideoData(data, fp_network::VideoFrame::IDR);
//         } else {
//             client_handler->SendVideoData(data, fp_network::VideoFrame::NORMAL);
//         }
//     });
// }

// void HostSocket::WriteAudioFrame(const std::vector<uint8_t>& data) {
//     protocol_mgr->foreach_client<HostProtocolHandler>([&data] (HostProtocolHandler* client_handler) {
//         client_handler->SendAudioData(data);
//     });
// }