#pragma once

#include <atomic>
#include <stdint.h>
#include <mutex>
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

class FrameRingBuffer {
public:
    FrameRingBuffer(std::string name, size_t num_frames, size_t frame_capacity);

    void AddFrameChunk(const fp_proto::HostDataFrame& frame);
    // Returns missed bytes needed to decrypt
    uint32_t GetFront(std::basic_string_view<uint8_t>& data_out);
    
private:
    std::vector<Frame> buffer;

    uint32_t frame_count;
    uint32_t frame_number;
    uint32_t stream_point;
    constexpr uint32_t frame_index() const { return frame_number % frame_count; }
    
    std::mutex buffer_m;
    std::condition_variable frame_ready_cv;
    std::atomic_bool frame_ready;

    std::string buffer_name;
};
