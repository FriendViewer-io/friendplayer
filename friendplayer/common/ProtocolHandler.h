#pragma once

#include <chrono>
#include <condition_variable>
#include <list>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include <asio/ip/udp.hpp>

#include "protobuf/common_messages.pb.h"
#include "protobuf/client_messages.pb.h"
#include "protobuf/host_messages.pb.h"

class SocketBase;

// Client management from host perspective
class ProtocolHandler {
protected:
    using clock = std::chrono::system_clock;
    static constexpr size_t BLOCK_SIZE = 16;
    // maximum chunk size over UDP accounding for proto overhead
    // and AES block encryption
    static constexpr size_t MAX_DATA_CHUNK = 476 - (476 % BLOCK_SIZE);

    // possibly implement ack and resend for important packets?
    // drop badly ordered packets
    static constexpr uint32_t RECV_DROP_WINDOW = 3;
    static constexpr uint32_t FAST_RETRANSMIT_WINDOW = 5;

public:
    using asio_endpoint = asio::ip::udp::endpoint;
    enum StreamState : uint32_t {
        UNINITIALIZED = 0,
        WAITING_FOR_VIDEO = 1,
        READY,
        DISCONNECTED,
    };

    ProtocolHandler(int client_id, const asio_endpoint& target_endpoint);

    virtual void SetParentSocket(SocketBase* parent) { parent_socket = parent; }
    int GetId() { return client_id; }
    const asio_endpoint& GetEndpoint() { return endpoint; }
    bool IsConnectionValid() { return state->load() != DISCONNECTED; }
    void Transition(StreamState new_state) { state->store(new_state); }
    StreamState GetState() { return state->load(); }

    int64_t GetLastHeartbeat() { return last_heartbeat.load(); }
    void UpdateHeartbeat() { last_heartbeat.store(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()); }

    bool BlockForHandshake();

    // Worker thread for this client (handles protocol)
    void RecvWorker();
    void SendWorker();
    void RetransmitWorker();
    void StartWorker() {
        async_recv_worker = std::make_unique<std::thread>(&ProtocolHandler::RecvWorker, this);
        async_send_worker = std::make_unique<std::thread>(&ProtocolHandler::SendWorker, this);
        async_retransmit_worker = std::make_unique<std::thread>(&ProtocolHandler::RetransmitWorker, this);
    }
    void KillAllThreads();
    // Threadsafe, pass message for ClientMessageWorker to do job
    void EnqueueRecvMessage(fp_proto::Message&& message);
    void EnqueueSendMessage(fp_proto::Message&& message);

    virtual ~ProtocolHandler() {}
    
protected:
    const int client_id;
    std::unique_ptr<std::atomic<StreamState>> state;

    SocketBase* parent_socket;
    asio_endpoint endpoint;

    std::atomic_int64_t last_heartbeat;
    
    // Send management
    std::unique_ptr<std::thread> async_send_worker;
    std::list<fp_proto::Message> send_message_queue;
    std::unique_ptr<std::mutex> send_message_queue_m;
    std::unique_ptr<std::condition_variable> send_message_queue_cv;
    uint32_t send_sequence_number;

    // Recv management
    std::unique_ptr<std::thread> async_recv_worker;
    std::list<fp_proto::Message> recv_message_queue;
    std::unique_ptr<std::mutex> recv_message_queue_m;
    std::unique_ptr<std::condition_variable> recv_message_queue_cv;

    // Shared send/recv
    struct SavedDataMessage {
        SavedDataMessage(const fp_proto::DataMessage& msg, clock::time_point send_ts)
            : msg(msg), last_send_ts(send_ts), did_fast_retransmit(false) {}

        fp_proto::DataMessage msg;
        clock::time_point last_send_ts;
        bool did_fast_retransmit;

        bool NeedsFastRetransmit(uint32_t current_ack_seqnum) {
            return msg.sequence_number() + FAST_RETRANSMIT_WINDOW < current_ack_seqnum &&
                   !did_fast_retransmit;
        }

        bool NeedsSlowRetransmit(uint32_t RTT_ms) {
            const uint32_t slow_retransmit_time = std::min(RTT_ms * 2, 300u);
            return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - last_send_ts).count() > slow_retransmit_time;
        }
    };
    std::unique_ptr<std::thread> async_retransmit_worker;
    std::unique_ptr<std::mutex> shared_data_m;
    std::unique_ptr<std::condition_variable> shared_data_cv;
    std::unique_ptr<std::atomic_bool> waiting_for_ack;
    uint32_t frame_window_start;
    // Ack window for stream
    std::map<uint32_t, SavedDataMessage> unacked_messages;
    // Acks which are blocking further data sending (slow retransmission)
    std::vector<uint32_t> blocking_acks;
    enum HandshakeState {
        HS_UNINITIALIZED, HS_WAITING_SHAKE_ACK, HS_READY, HS_FAILED
    };
    uint32_t RTT_milliseconds;
    uint32_t highest_acked_seqnum;

    std::unique_ptr<std::mutex> handshake_m;
    std::unique_ptr<std::condition_variable> handshake_signal;
    HandshakeState protocol_state;

    // add enc/dec here -- adam! :)

    // GUARDED BY send_message_queue_m
    void SendDataMessage_internal(fp_proto::Message& msg);
    void DoSlowRetransmission();

    void OnAcknowledge(const fp_proto::AckMessage& msg);

protected:
    // GUARDED BY send_message_queue_m
    virtual bool DoHandshake() = 0;
    // GUARDED BY recv_message_queue_m
    virtual void OnDataFrame(const fp_proto::DataMessage& msg) = 0;
    // GUARDED BY recV_message_queue_m
    virtual void OnStateMessage(const fp_proto::StateMessage& msg) = 0;
};