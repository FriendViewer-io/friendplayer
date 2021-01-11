#include "Socket.h"

#include "common/Log.h"


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

ClientSocket::ClientSocket(std::string_view ip)
    : video_buffer("video buffer", VIDEO_FRAME_BUFFER, VIDEO_FRAME_SIZE),
      audio_buffer("audio buffer", AUDIO_FRAME_BUFFER, AUDIO_FRAME_SIZE),
      sent_frame_num(0),
      idr_send_timeout(-1) {
    host_endpoint = asio_endpoint(asio_address::from_string(std::string(ip)), FP_UDP_PORT);
    socket.open(asio::ip::udp::v4());
    socket.bind(asio_endpoint(asio::ip::udp::v4(), FP_UDP_PORT));
    socket.set_option(asio::socket_base::receive_buffer_size(CLIENT_RECV_SIZE));
}

void ClientSocket::GetVideoFrame(RetrievedBuffer& buf_in) {
    uint32_t size_to_decrypt = video_buffer.GetFront(buf_in);
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
    uint32_t size_to_decrypt = audio_buffer.GetFront(buf_in);
    // Run decryption
}

void ClientSocket::SendClientState(fp_proto::ClientState::State state) {
    fp_proto::ClientState state_msg;
    state_msg.set_state(state);
    fp_proto::ClientDataFrame frame_msg;
    frame_msg.set_frame_id(sent_frame_num.fetch_add(1));
    *frame_msg.mutable_client_state() = state_msg;
    socket.send_to(asio::buffer(frame_msg.SerializeAsString()), host_endpoint);
}

void ClientSocket::SendRequestToHost(fp_proto::RequestToHost::RequestType request) {
    fp_proto::RequestToHost request_msg;
    request_msg.set_type(request);
    fp_proto::ClientDataFrame frame_msg;
    frame_msg.set_frame_id(sent_frame_num.fetch_add(1));
    *frame_msg.mutable_host_request() = request_msg;
    socket.send_to(asio::buffer(frame_msg.SerializeAsString()), host_endpoint);
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
        
        fp_proto::HostDataFrame frame;
        frame.ParseFromArray(recv_buffer.data(), size);
        
        switch (frame.DataFrame_case()) {
            case fp_proto::HostDataFrame::kAudio: {
                audio_buffer.AddFrameChunk(frame);
            }
            break;
            case fp_proto::HostDataFrame::kVideo: {
                video_buffer.AddFrameChunk(frame);
            }
            break;
            default: {
                LOG_ERROR("Unknown message type from host: {}", static_cast<int>(frame.DataFrame_case()));
            }
            break;
        }
    }
}

HostSocket::HostSocket() {
    endpoint = asio_endpoint(asio::ip::udp::v4(), FP_UDP_PORT);
    socket = asio_socket(io_service, endpoint);
    socket.set_option(asio::socket_base::send_buffer_size(HOST_SEND_SIZE));
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
        fp_proto::ClientDataFrame frame;
        frame.ParseFromArray(recv_buffer.data(), size);
        {
            std::lock_guard<std::shared_mutex> l(m_client_wait);
            auto client_it = clients.find(client_endpoint);
            if (client_it == clients.end()) {
                LOG_INFO("New client added");
                clients.emplace(client_endpoint, Client(this, clients.size(), client_endpoint));
                if (clients.size() == 1) {
                    cv_client_wait.notify_all();
                }
                client_it = clients.find(client_endpoint);
                client_it->second.StartWorker();
            }
            client_it->second.EnqueueMessage(std::move(frame));
        }
        
    }

}

void HostSocket::WriteVideoFrame(const std::vector<uint8_t>& data, bool is_idr_frame) {
    fp_proto::HostDataFrame frame;
    *frame.mutable_video() = fp_proto::VideoFrame();
    frame.mutable_video()->set_is_idr_frame(is_idr_frame);
    
    static bool end_pressed = false;
    
    bool fuckup_frame = false, end_state = GetAsyncKeyState(VK_HOME);
    if (end_state && !end_pressed) {
        fuckup_frame = true;
    }
    end_pressed = end_state;
    
    std::shared_lock<std::shared_mutex> l(m_client_wait);
    cv_client_wait.wait(l, [this] () -> bool {
        return clients.size() > 0;
    });

    for (auto& client_pair : clients) {
        client_pair.second.SendHostData(frame, data);
    }
}

void HostSocket::WriteAudioFrame(const std::vector<uint8_t>& data) {
    fp_proto::HostDataFrame frame;
    *frame.mutable_audio() = fp_proto::AudioFrame();
    
    std::shared_lock<std::shared_mutex> l(m_client_wait);
    cv_client_wait.wait(l, [this] () -> bool {
        return clients.size() > 0;
    });

    for (auto& client_pair : clients) {
        client_pair.second.SendHostData(frame, data);
    }
}