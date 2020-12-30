#pragma once

#include <array>
#include <list>
#include <condition_variable>
#include <stdint.h>
#include <mutex>
#include <thread>
#include <vector>

static constexpr size_t chunk_size = 1500;

struct FrameInfo {
    uint32_t frame_num = -1;
    uint32_t frame_size = -1;
    uint32_t current_read_size = 0;
};

struct H264Frame {
    FrameInfo frame_info;
    std::array<char, 1024 * 1024> frame_data;
};

class UDPSocket {
public:
    virtual void start_backend() = 0;
    virtual void sync() = 0;
    virtual void init_connection(const char* ip, short port) = 0;

protected:
    std::thread* worker_thread;
    std::mutex mutex;
    std::condition_variable cond;
    unsigned long long socket_;
    struct sockaddr_in* target_addr;
    int sockaddr_len;
};

class UDPSocketSender : public UDPSocket {
public:
    void start_backend();
    void init_connection(const char* ip, short port);
    void sync();
    
    void send_frames();
    void enqueue_send(std::vector<uint8_t>&& outgoing);
    
private:
    std::list<std::vector<uint8_t>> out_buf;
};

class UDPSocketReceiver : public UDPSocket {
public:
    UDPSocketReceiver() {
        for (int i = 0; i < frame_buffer.size(); ++i) {
            frame_buffer[i].frame_info.frame_num = i;
        }
    }

    void start_backend();
    void sync();
    void init_connection(const char* ip, short port);

// wait until timeout or receives a full frame
    bool get_frame(uint8_t* out_buf, uint32_t* buf_size, uint32_t* current_frame_num, uint32_t timeout_ms);
// worker thread
    void read_frames();


private:
    std::array<H264Frame, 5> frame_buffer;
    uint32_t current_frame_index;

    char read_buffer[chunk_size + 8];
};