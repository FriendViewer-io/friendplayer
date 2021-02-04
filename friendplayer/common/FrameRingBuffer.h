#pragma once

#include <chrono>
#include <stdint.h>
#include <string_view>
#include <vector>

#include "protobuf/host_messages.pb.h"

struct Frame {
    uint32_t num = 0;
    uint32_t size = 0;
    uint32_t current_read_size = 0;
    std::vector<uint8_t> data;
};

struct RetrievedBuffer {
    RetrievedBuffer(uint8_t* buffer_in, int buffer_size) 
        : data_out(buffer_in, buffer_size), bytes_received(0) {}

    std::basic_string_view<uint8_t> data_out;
    uint32_t bytes_received;
};

class FrameRingBuffer {
private:
    static constexpr uint32_t CORRUPT_FRAME_TIMEOUT = 20;

public:
    FrameRingBuffer(std::string name, size_t num_frames, size_t frame_capacity);

    bool AddFrameChunk(const fp_network::HostDataFrame& frame);
    bool GetFront(std::string& buffer_out);
    double GetFPS();
    
private:
    std::vector<Frame> buffer;

    uint32_t frame_count;
    uint32_t frame_number;
    constexpr uint32_t frame_index() const { return frame_number % frame_count; }

    std::chrono::time_point<std::chrono::system_clock> last_fps_check;
    uint32_t last_frame_number;

    std::string buffer_name;

    uint32_t corrupt_frame_timeout;
};
