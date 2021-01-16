#include "common/Socket.h"

#include "common/Log.h"
#include "common/HeartbeatManager.h"


void SocketBase::StartSocket() {
    is_running.store(true);
    network_thread = std::make_unique<std::thread>(&SocketBase::NetworkThread, this);
}

void SocketBase::WaitForSocket() {
    network_thread->join();
}

void SocketBase::Stop() {
    is_running.store(false);
    socket.close();
}

void SocketBase::MessageSend(const fp_proto::Message& outgoing_msg, const asio_endpoint& target_endpoint) {
    socket.send_to(asio::buffer(outgoing_msg.SerializeAsString()), target_endpoint);
}

ClientSocket::ClientSocket(std::string_view ip, unsigned short port, std::shared_ptr<ClientProtocolHandler> protocol_handler)
    : protocol_handler(std::move(protocol_handler)),
      sent_frame_num(0),
      idr_send_timeout(-1) {
    host_endpoint = asio_endpoint(asio_address::from_string(std::string(ip)), port);
    this->protocol_handler->SetParentSocket(this);
    socket.open(asio::ip::udp::v4());
    socket.set_option(asio::socket_base::receive_buffer_size(CLIENT_RECV_SIZE));
}

void ClientSocket::MessageSend(const fp_proto::Message& outgoing_msg) {
    SocketBase::MessageSend(outgoing_msg, host_endpoint);
}

void ClientSocket::GetVideoFrame(RetrievedBuffer& buf_in) {
    uint32_t size_to_decrypt = protocol_handler->GetVideoFrame(buf_in);
    // Run decryption
    if (size_to_decrypt != 0 || buf_in.bytes_received == 0 || (buf_in.bytes_received < buf_in.data_out.size())) {
        idr_send_timeout = IDR_SEND_TIMEOUT;
    } else if (idr_send_timeout >= 0) {
        if (idr_send_timeout == 0) {
            SendRequestToHost(fp_proto::RequestToHost::SEND_IDR);
            LOG_INFO("Requesting IDR from host");
        }
        idr_send_timeout--;
    }
}

void ClientSocket::GetAudioFrame(RetrievedBuffer& buf_in) {
    uint32_t size_to_decrypt = protocol_handler->GetAudioFrame(buf_in);
    // Run decryption
}

void ClientSocket::SendClientState(fp_proto::ClientState::State state) {
    fp_proto::Message msg;
    fp_proto::DataMessage& data_msg = *msg.mutable_data_msg();
    data_msg.set_needs_ack(true);
    fp_proto::ClientDataFrame& client_frame = *data_msg.mutable_client_frame();
    client_frame.mutable_client_state()->set_state(state);
    client_frame.set_frame_id(sent_frame_num.fetch_add(1));
    protocol_handler->EnqueueSendMessage(std::move(msg));
}

void ClientSocket::SendRequestToHost(fp_proto::RequestToHost::RequestType request) {
    fp_proto::Message msg;
    fp_proto::DataMessage& data_msg = *msg.mutable_data_msg();
    data_msg.set_needs_ack(true);
    fp_proto::ClientDataFrame& client_frame = *data_msg.mutable_client_frame();
    client_frame.mutable_host_request()->set_type(request);
    client_frame.set_frame_id(sent_frame_num.fetch_add(1));
    protocol_handler->EnqueueSendMessage(std::move(msg));
}

void ClientSocket::NetworkThread() {
    std::string recv_buffer;
    recv_buffer.resize(1500);

    asio::error_code ec;

    while (is_running.load()) {
        size_t size = socket.receive_from(asio::buffer(recv_buffer), host_endpoint, 0, ec);
        if (size == 0) {
            continue;
        }
        
        fp_proto::Message msg;
        msg.ParseFromArray(recv_buffer.data(), size);
        protocol_handler->EnqueueRecvMessage(std::move(msg));
    }
}

HostSocket::HostSocket(unsigned short port, std::shared_ptr<ClientManager> client_mgr,
                       std::shared_ptr<HeartbeatManager> heartbeat_mgr)
  : client_mgr(std::move(client_mgr)), heartbeat_mgr(std::move(heartbeat_mgr)) {
    endpoint = asio_endpoint(asio::ip::udp::v4(), port);
    socket = asio_socket(io_service, endpoint);
    socket.set_option(asio::socket_base::send_buffer_size(HOST_SEND_SIZE));
    pps_sps_version = -1;
}

void HostSocket::WriteData(const asio::const_buffer& buffer, asio::ip::udp::endpoint endpoint) {
    socket.send_to(buffer, endpoint);
}

void HostSocket::SetNeedIDR(bool need_idr) {
    should_send_idr.store(need_idr);
}

bool HostSocket::ShouldSendIDR() {
    return should_send_idr.exchange(false);
}

void HostSocket::NetworkThread() {
    std::string recv_buffer;
    recv_buffer.resize(1500);

    asio_endpoint client_endpoint;

    while (is_running.load()) {
        size_t size = socket.receive_from(asio::buffer(recv_buffer), client_endpoint);
        if (size == 0) {
            continue;
        }
        ClientHandler* handler = client_mgr->LookupClientHandlerByEndpoint(client_endpoint);
        if (handler == nullptr) {
            handler = client_mgr->CreateNewClient(client_endpoint);
            if (handler == nullptr) {
                LOG_INFO("Client tried to join but there were too many clients.");
                continue;
            }
            handler->SetParentSocket(this);
            handler->SetClientManager(client_mgr);
            handler->SetHeartbeatManager(heartbeat_mgr);
            heartbeat_mgr->RegisterClient(handler->GetId());
            handler->StartWorker();
        }
        fp_proto::Message msg;
        msg.ParseFromArray(recv_buffer.data(), size);
        handler->EnqueueRecvMessage(std::move(msg));
    }
}

void HostSocket::WriteVideoFrame(const std::vector<uint8_t>& data, bool is_idr_frame) {
    client_mgr->foreach_client([is_idr_frame, &data] (ClientHandler& client_handler) {
        if (is_idr_frame) {
            client_handler.SendVideoData(data, fp_proto::VideoFrame::IDR);
        } else {
            client_handler.SendVideoData(data, fp_proto::VideoFrame::NORMAL);
        }
    });
}

void HostSocket::WriteAudioFrame(const std::vector<uint8_t>& data) {
    client_mgr->foreach_client([&data] (ClientHandler& client_handler) {
        client_handler.SendAudioData(data);
    });
}