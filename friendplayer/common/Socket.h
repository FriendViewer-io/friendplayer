#pragma once

#include <asio/io_service.hpp>
#include <asio/ip/udp.hpp>
#include <asio/buffer.hpp>
#include <atomic>
#include <condition_variable>
#include <map>
#include <shared_mutex>
#include <string_view>
#include <vector>

#include "common/FrameRingBuffer.h"
#include "common/Client.h"
#include "protobuf/client_messages.pb.h"
#include "protobuf/host_messages.pb.h"

constexpr unsigned short FP_UDP_PORT = 40040;
constexpr size_t VIDEO_FRAME_BUFFER = 5;
constexpr size_t AUDIO_FRAME_BUFFER = 5;
// Guess values, tune or scale these?
constexpr size_t VIDEO_FRAME_SIZE = 20000;
constexpr size_t AUDIO_FRAME_SIZE = 1795;

constexpr size_t CLIENT_RECV_SIZE = 1024 * 256;
constexpr size_t HOST_SEND_SIZE = 1024 * 256;

// 60 frame timeout for asking for IDR from host
constexpr uint32_t IDR_SEND_TIMEOUT = 60;

class SocketBase { 
public:
    SocketBase()
        : network_thread(nullptr),
          is_running(false),
          socket(io_service) {}
    void StartSocket();
    void WaitForSocket();
    void Stop();
    
protected:
    using asio_socket = asio::ip::udp::socket;
    using asio_endpoint = asio::ip::udp::endpoint;
    using asio_service = asio::io_service;
    using asio_address = asio::ip::address;
    virtual void NetworkThread() = 0;
    
    std::unique_ptr<std::thread> network_thread;
    std::atomic_bool is_running;

    asio_service io_service;
    asio_socket socket;
};

class ClientSocket : public SocketBase{
public:
    ClientSocket(std::string_view ip);

    // blocks until available
    void GetVideoFrame(std::basic_string_view<uint8_t>& frame_out);
    // blocks until available
    void GetAudioFrame(std::basic_string_view<uint8_t>& frame_out);

    void SendClientState(fp_proto::ClientState::State state);
    void SendRequestToHost(fp_proto::RequestToHost::RequestType request);

protected:
    virtual void NetworkThread();
private:
    asio_endpoint host_endpoint;

    FrameRingBuffer video_buffer;
    FrameRingBuffer audio_buffer;

    std::atomic_uint32_t sent_frame_num;
    int32_t idr_send_timeout;
};

class HostSocket : public SocketBase {
public:
    HostSocket();

    void WriteVideoFrame(const std::vector<uint8_t>& data, bool is_idr_frame);
    void WriteAudioFrame(const std::vector<uint8_t>& data);

    void SetNeedIDR(bool need_idr);
    bool ShouldSendIDR();

    // Make this friended
    void WriteData(const asio::const_buffer& buffer, asio::ip::udp::endpoint endpoint);
    void SetPPSSPS(std::vector<uint8_t>&& pps_sps) { active_pps_sps = std::move(pps_sps); }
    const std::vector<uint8_t> GetPPSSPS() const { return active_pps_sps; }

protected:
    virtual void NetworkThread();

private:
    
    asio_endpoint endpoint;

    std::map<asio_endpoint, Client> clients;

    std::shared_mutex m_client_wait;
    std::condition_variable_any cv_client_wait;
    
    std::atomic_bool should_send_idr;
    std::vector<uint8_t> active_pps_sps;
};
