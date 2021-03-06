#include "common/FrameRingBuffer.h"

#include "common/Log.h"

FrameRingBuffer::FrameRingBuffer(std::string name, size_t num_frames, size_t frame_capacity) 
        : buffer_name(name), frame_count(static_cast<uint32_t>(num_frames)), frame_number(0), corrupt_frame_timeout(-1) {
    buffer.resize(num_frames);
    for (int i = 0; i < num_frames; ++i) {
        buffer[i].data.resize(frame_capacity);
        buffer[i].num = i;
    }
    last_fps_check = std::chrono::system_clock::now();
}

bool FrameRingBuffer::AddFrameChunk(const fp_network::HostDataFrame& frame) {
    std::string data;
    uint32_t chunk_offset;

    if (frame.has_video()) {
        data = frame.video().data();
        chunk_offset = frame.video().chunk_offset();
    } else if (frame.has_audio()) {
        data = frame.audio().data();
        chunk_offset = frame.audio().chunk_offset();
    }

    // Invalid frame
    if (frame.frame_num() < frame_number) { 
        //LOG_WARNING("{}: Decoder got frame number behind {} < {}", buffer_name, frame.frame_num(), frame_number);
        return false;
    } else if (frame.frame_num() >= frame_number + frame_count) {
        // Frame is beyond current buffer (probably decoder isn't taking them out fast enough)
        for (uint32_t i = frame.frame_num(); i < frame.frame_num() + frame_count; ++i) {
            buffer[i % frame_count].num = i;
            buffer[i % frame_count].size = 0;
            buffer[i % frame_count].current_read_size = 0;
        }
        LOG_WARNING("{}: Decoder has dropped {} frames. Jumping from frame {} to {} ", buffer_name, frame_count, frame_number, frame.frame_num());
        frame_number = frame.frame_num();
    }

    Frame& buffer_frame = buffer[frame.frame_num() % frame_count];

    // Copy chunk data over to buffer
    if (buffer_frame.data.size() < frame.frame_size()) {
        buffer_frame.data.resize(frame.frame_size());
    }
    buffer_frame.size = frame.frame_size();
    buffer_frame.num = frame.frame_num();
    buffer_frame.current_read_size += static_cast<uint32_t>(data.size());
    std::copy(data.begin(), data.end(), buffer_frame.data.begin() + chunk_offset);

    return buffer[frame_index()].current_read_size == buffer[frame_index()].size && buffer[frame_index()].size > 0;
}

bool FrameRingBuffer::GetFront(std::string& buffer_out) {
    bool frame_was_corrupt = false;

    const auto& data = buffer[frame_index()].data;
    if (data.size() < buffer[frame_index()].size) {
        LOG_WARNING("Invalid frame size reported by FrameRingBuffer {}: {} < {}", buffer_name, data.size(), buffer[frame_index()].size);
        frame_was_corrupt = true;
    } else {
        buffer_out.resize(buffer[frame_index()].size);
        std::copy(data.begin(), data.begin() + buffer_out.size(), reinterpret_cast<uint8_t*>(buffer_out.data()));
        
        if (buffer[frame_index()].current_read_size != buffer[frame_index()].size
            || buffer[frame_index()].size == 0) {
            corrupt_frame_timeout = CORRUPT_FRAME_TIMEOUT;
        } else if (corrupt_frame_timeout >= 0) {
            if (corrupt_frame_timeout == 0) {
                frame_was_corrupt = true;
            }
            corrupt_frame_timeout--;
        }
    }

    buffer[frame_index()].num = frame_number + frame_count;
    buffer[frame_index()].size = 0;
    buffer[frame_index()].current_read_size = 0;
    frame_number++;

    return frame_was_corrupt;
}

double FrameRingBuffer::GetFPS() {
    auto now = std::chrono::system_clock::now();
    double fps = static_cast<double>(frame_number - last_frame_number) / std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fps_check).count();
    fps *= 1000;
    last_frame_number = frame_number;
    last_fps_check = now;
    return fps;
}