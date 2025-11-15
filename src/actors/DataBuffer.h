#pragma once

#include <memory>
#include <shared_mutex>
#include <vector>

struct DataBuffer {
    DataBuffer(std::unique_ptr<std::string> data)
      : inner_data(std::move(data)), ref_count(std::make_unique<std::atomic_int>(1)) {}
    DataBuffer()
      : inner_data(nullptr), ref_count(std::make_unique<std::atomic_int>(0)) {}
    std::unique_ptr<std::string> inner_data;
    std::unique_ptr<std::atomic_int> ref_count;
};

class DataBufferMap {
public:
    // Returns false if failed to acquire
    bool Increment(uint64_t handle);
    void Decrement(uint64_t handle);

    // Should only be called when owning a handle!!
    std::string* GetBuffer(uint64_t handle);
    
    // Creates handle & initializes data
    uint64_t Create(void* data, size_t size);
    // Creates handle & wraps data with default deleter
    uint64_t Wrap(std::string* data);
    // Creates handle & wraps ptr with default deleter, transfer ownership!
    uint64_t Wrap(std::unique_ptr<std::string> data);

private:
    std::mutex buffer_list_m;
    std::vector<DataBuffer> buffers;
    std::vector<uint64_t> free_list;
};