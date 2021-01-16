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

class ClientManager;
class HostSocket;
class HeartbeatManager;


// Client management from host perspective
class ClientHandler {
private:
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
    enum ClientState : uint32_t {
        UNINITIALIZED = 0,
        WAITING_FOR_VIDEO = 1,
        READY,
        DISCONNECTED,
    };

    ClientHandler(int id, asio_endpoint endpoint);

    void SetParentSocket(HostSocket* parent) { parent_socket = parent; }
    void SetClientManager(std::shared_ptr<ClientManager> mgr) { client_manager = std::move(mgr); }
    void SetHeartbeatManager(std::shared_ptr<HeartbeatManager> mgr) { heartbeat_manager = std::move(mgr); }
    int GetId() { return client_id; }
    const asio_endpoint& GetEndpoint() { return client_endpoint; }
    void SetAudio(bool enabled) { audio_enabled = enabled; }
    void SetKeyboard(bool enabled) { keyboard_enabled = enabled; }
    void SetMouse(bool enabled) { mouse_enabled = enabled; }
    void SetController(bool enabled) { controller_enabled = enabled; }
    bool IsConnectionValid() { return state->load() != DISCONNECTED; }
    void Transition(ClientState new_state) { state->store(new_state); }
    ClientState GetState() { return state->load(); }
    
    void AddAudioStreamPoint(uint32_t val) { audio_stream_point += val; }
    uint32_t GetAudioStreamPoint() { return audio_stream_point; }

    void AddVideoStreamPoint(uint32_t val) { video_stream_point += val; }
    uint32_t GetVideoStreamPoint() { return video_stream_point; }

    // Worker thread for this client (handles protocol)
    void ClientRecvWorker();
    void ClientSendWorker();
    void ClientRetransmitWorker();
    void StartWorker() {
        async_recv_worker = std::make_unique<std::thread>(&ClientHandler::ClientRecvWorker, this);
        async_send_worker = std::make_unique<std::thread>(&ClientHandler::ClientSendWorker, this);
        async_retransmit_worker = std::make_unique<std::thread>(&ClientHandler::ClientRetransmitWorker, this);
    }
    void KillAllThreads() {
        state->store(DISCONNECTED);
        send_message_queue_cv->notify_one();
        recv_message_queue_cv->notify_one();
        shared_data_cv->notify_one();

        async_send_worker->join();
        async_recv_worker->join();
        async_retransmit_worker->join();
    }
    // Threadsafe, pass message for ClientMessageWorker to do job
    void EnqueueRecvMessage(fp_proto::Message&& message);
    void EnqueueSendMessage(fp_proto::Message&& message);

    void SendAudioData(const std::vector<uint8_t>& data);
    void SendVideoData(const std::vector<uint8_t>& data, fp_proto::VideoFrame_FrameType type);
    
private:
    const int client_id;

    bool audio_enabled;
    bool keyboard_enabled;
    bool mouse_enabled;
    bool controller_enabled;
    std::unique_ptr<std::atomic<ClientState>> state;

    HostSocket* parent_socket;
    std::shared_ptr<ClientManager> client_manager;
    std::shared_ptr<HeartbeatManager> heartbeat_manager;

    asio_endpoint client_endpoint;

    uint32_t video_stream_point;
    uint32_t audio_stream_point;
    uint32_t audio_frame_num;
    uint32_t video_frame_num;
    int pps_sps_version;

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
        HS_UNINITIALIZED, HS_WAITING_SHAKE_ACK, HS_READY
    };
    HandshakeState protocol_state;
    uint32_t RTT_milliseconds;
    uint32_t highest_acked_seqnum;

    // add enc/dec here -- adam! :)

    // GUARDED BY send_message_queue_m
    void SendDataMessage_internal(fp_proto::Message& msg);
    void DoSlowRetransmission();

    bool DoHandshake();

    void OnAcknowledge(const fp_proto::AckMessage& msg);
    // GUARDED BY recv_message_queue_m
    void OnDataFrame(const fp_proto::DataMessage& msg);

    void OnKeyboardFrame(const fp_proto::ClientDataFrame& msg);
    void OnMouseFrame(const fp_proto::ClientDataFrame& msg);
    void OnControllerFrame(const fp_proto::ClientDataFrame& msg);
    void OnHostRequest(const fp_proto::ClientDataFrame& msg);
    void OnClientState(const fp_proto::ClientDataFrame& msg);
};