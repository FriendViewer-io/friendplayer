#pragma once

#include <stdint.h>
#include <string_view>
#include <vector>

#include "protobuf/host_messages.pb.h"

struct Frame {
    uint32_t num = 0;
    uint32_t size = 0;
    uint32_t current_read_size = 0;

    // Where this frame starts in the decryption stream
    uint32_t stream_point = 0;
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
    // Returns # of bytes needed to fix decryption
    uint32_t GetFront(std::string& buffer_out, bool& frame_was_corrupt);
    uint32_t GetFront(std::string& buffer_out) {
        bool unused;
        return GetFront(buffer_out, unused);
    }
    
private:
    std::vector<Frame> buffer;

    uint32_t frame_count;
    uint32_t frame_number;
    uint32_t stream_point;
    constexpr uint32_t frame_index() const { return frame_number % frame_count; }

    std::string buffer_name;

    uint32_t corrupt_frame_timeout;
};
