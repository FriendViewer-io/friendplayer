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

#include "protobuf/client_messages.pb.h"
#include "protobuf/common_messages.pb.h"
#include "protobuf/host_messages.pb.h"

class FrameRingBuffer;
class ProtocolManager;
class ClientProtocolHandler;
struct RetrievedBuffer;

constexpr size_t CLIENT_RECV_SIZE = 1024 * 256;
constexpr size_t HOST_SEND_SIZE = 1024 * 1024;

// 60 frame timeout for asking for IDR from host
// move this to host?
constexpr uint32_t IDR_SEND_TIMEOUT = 20;

class SocketBase {
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

    std::shared_ptr<ProtocolManager> protocol_mgr;

public:
    SocketBase(std::shared_ptr<ProtocolManager> protocol_mgr)
        : network_thread(nullptr),
          is_running(false),
          socket(io_service),
          protocol_mgr(std::move(protocol_mgr)) {}
    void StartSocket();
    void WaitForSocket();
    void Stop();
    void MessageSend(const fp_proto::Message& outgoing_msg,
            const asio_endpoint& target_endpoint);
};

class ClientSocket : public SocketBase {
public:
    ClientSocket(std::string_view ip, unsigned short port,
            std::shared_ptr<ProtocolManager> protocol_mgr);

    // blocks until available
    void GetVideoFrame(RetrievedBuffer& buf_in);
    // blocks until available
    void GetAudioFrame(RetrievedBuffer& buf_in);

    void SendStreamState(fp_proto::StreamState::State state);
    void SendRequestToHost(fp_proto::RequestToHost::RequestType request);

    void SendController(const fp_proto::ControllerFrame& frame);

    void MessageSend(const fp_proto::Message& outgoing_msg);

protected:
    virtual void NetworkThread();

private:
    asio_endpoint host_endpoint;

    ClientProtocolHandler* protocol_handler;

    std::atomic_uint32_t sent_frame_num;
    int32_t idr_send_timeout;
};

class HostSocket : public SocketBase {
public:
    HostSocket(unsigned short port, std::shared_ptr<ProtocolManager> protocol_mgr);

    void WriteVideoFrame(const std::vector<uint8_t>& data, bool is_idr_frame);
    void WriteAudioFrame(const std::vector<uint8_t>& data);

    void SetNeedIDR(bool need_idr);
    bool ShouldSendIDR();

    // Make this friended
    void WriteData(const asio::const_buffer& buffer, asio::ip::udp::endpoint endpoint);
    void SetPPSSPS(std::vector<uint8_t>&& pps_sps) {
        active_pps_sps = std::move(pps_sps);
        pps_sps_version++;
    }
    const std::vector<uint8_t>& GetPPSSPS() const { return active_pps_sps; }
    int GetPPSSPSVersion() const { return pps_sps_version; }

protected:
    virtual void NetworkThread();

private:
    asio_endpoint endpoint;

    std::atomic_bool should_send_idr;
    std::vector<uint8_t> active_pps_sps;
    int pps_sps_version;
};
