#pragma once

#include <chrono>
#include <condition_variable>
#include <list>
#include <future>
#include <mutex>
#include <thread>

#include <asio/ip/udp.hpp>

#include "protobuf/client_messages.pb.h"
#include "protobuf/host_messages.pb.h"

class HostSocket;

constexpr size_t BLOCK_SIZE = 16;
// maximum chunk size over UDP accounding for proto overhead
// and AES block encryption
constexpr size_t MAX_DATA_CHUNK = 476 - (476 % BLOCK_SIZE);

constexpr long long HEARTBEAT_TIMEOUT_MS = 30 * 1000;
// possibly implement ack and resend for important packets?
// drop badly ordered packets
constexpr int DROP_WINDOW = 3;

// Client management from host perspective
class Client {
private:
    using clock = std::chrono::system_clock;
    
public:
    enum ClientState : uint32_t {
        UNINITIALIZED = 0,
        WAITING_FOR_VIDEO = 1,
        READY,
        DISCONNECTED,
    };

    Client(HostSocket* host_socket, int id, asio::ip::udp::endpoint endpoint)
        : client_id(id),
          audio_enabled(true),
          keyboard_enabled(false),
          mouse_enabled(false),
          controller_enabled(true),
          state(UNINITIALIZED),
          video_stream_point(0),
          audio_stream_point(0),
          audio_frame_num(0),
          video_frame_num(0),
          heartbeat_m(nullptr),
          queue_m(nullptr),
          async_worker(nullptr),
          queue_cv(nullptr),
          queue_ready(nullptr),
          last_heartbeat(clock::now()),
          parent_socket(host_socket),
          client_endpoint(endpoint),
          frame_window_start(0) {
            heartbeat_m = std::make_unique<std::mutex>();
            queue_m = std::make_unique<std::mutex>();
            queue_cv = std::make_unique<std::condition_variable>();
            queue_ready = std::make_unique<std::atomic_bool>(false);
        }

    void SetAudio(bool enabled) { audio_enabled = enabled; }
    void SetKeyboard(bool enabled) { keyboard_enabled = enabled; }
    void SetMouse(bool enabled) { mouse_enabled = enabled; }
    void SetController(bool enabled) { controller_enabled = enabled; }
    bool IsConnectionValid() {
        std::lock_guard<std::mutex> lock(*heartbeat_m);
        return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - last_heartbeat).count() < HEARTBEAT_TIMEOUT_MS;
    }
    void Beat() { 
        std::lock_guard<std::mutex> lock(*heartbeat_m);
        last_heartbeat = clock::now(); 
    }
    void Transition(ClientState new_state) { state = new_state; }
    ClientState GetState() { return state; }
    
    void AddAudioStreamPoint(uint32_t val) { audio_stream_point += val; }
    uint32_t GetAudioStreamPoint() { return audio_stream_point; }

    void AddVideoStreamPoint(uint32_t val) { video_stream_point += val; }
    uint32_t GetVideoStreamPoint() { return video_stream_point; }

    void ClientMessageWorker();
    void StartWorker() {
        async_worker = std::make_unique<std::thread>(&Client::ClientMessageWorker, this);
    }
    void EnqueueMessage(fp_proto::ClientDataFrame&& message);

    void SendHostData(fp_proto::HostDataFrame& frame, const std::vector<uint8_t>& data);
    
private:
    const int client_id;

    bool audio_enabled;
    bool keyboard_enabled;
    bool mouse_enabled;
    bool controller_enabled;
    ClientState state;

    HostSocket* parent_socket;
    asio::ip::udp::endpoint client_endpoint;

    uint32_t video_stream_point;
    uint32_t audio_stream_point;
    uint32_t audio_frame_num;
    uint32_t video_frame_num;

    std::unique_ptr<std::mutex> heartbeat_m;
    clock::time_point last_heartbeat;

    struct ClientTask {
        uint32_t frame_id;
        std::future<void> deferred;
    };
    std::unique_ptr<std::thread> async_worker;
    std::list<ClientTask> task_queue;
    std::unique_ptr<std::mutex> queue_m;
    std::unique_ptr<std::condition_variable> queue_cv;
    std::unique_ptr<std::atomic_bool> queue_ready;
    uint32_t frame_window_start;


    // add enc/dec here -- adam! :)

    void OnKeyboardFrame(const fp_proto::ClientDataFrame& msg);
    void OnMouseFrame(const fp_proto::ClientDataFrame& msg);
    void OnControllerFrame(const fp_proto::ClientDataFrame& msg);
    void OnHostRequest(const fp_proto::ClientDataFrame& msg);
    void OnClientState(const fp_proto::ClientDataFrame& msg);
};