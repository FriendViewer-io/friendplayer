#include "actors/DataBuffer.h"

bool DataBufferMap::Increment(uint64_t handle) {
    std::lock_guard<std::mutex> lock(buffer_list_m);
    if (handle >= buffers.size()) {
        return false;
    }
    if (buffers[handle].ref_count->fetch_add(1) == 0) {
        buffers[handle].ref_count->fetch_sub(1);
        return false;
    }
    return true;
}

void DataBufferMap::Decrement(uint64_t handle) {
    std::lock_guard<std::mutex> lock(buffer_list_m);
    if (handle >= buffers.size()) {
        return;
    }
    // Either destroy or do nothing, but block increment from acquiring
    // if decrement occurs first
    // This (should) never be a problem that occurs, as simultaneous
    // increment and decrement when refcount --> 0 should be avoided by design
    int old_refcount = buffers[handle].ref_count->fetch_sub(1);
    if (old_refcount <= 1) {
        if (old_refcount == 1) {
            buffers[handle].inner_data.reset(nullptr);
            free_list.emplace_back(handle);
        } else {
            buffers[handle].ref_count->fetch_add(1);
        }
        return;
    }
}

std::string* DataBufferMap::GetBuffer(uint64_t handle) {
    std::lock_guard<std::mutex> lock(buffer_list_m);
    if (handle >= buffers.size()) {
        return nullptr;
    }

    return buffers[handle].inner_data.get();
}

uint64_t DataBufferMap::Create(void* data, size_t size) {
    std::unique_ptr<std::string> new_elem = std::make_unique<std::string>();
    new_elem->assign(static_cast<char*>(data), static_cast<char*>(data) + size);
    return Wrap(std::move(new_elem));
}

uint64_t DataBufferMap::Wrap(std::string* data) {
    std::unique_ptr<std::string> new_elem(data);
    return Wrap(std::move(new_elem));
}

uint64_t DataBufferMap::Wrap(std::unique_ptr<std::string> data) {
    uint64_t new_handle;
    std::lock_guard<std::mutex> lock(buffer_list_m);
    if (!free_list.empty()) {
        // Use existing slot
        new_handle = free_list.back();
        free_list.pop_back();
        buffers[new_handle].inner_data = std::move(data);
        buffers[new_handle].ref_count->store(1);
    } else {
        // Allocate a new slot
        new_handle = buffers.size();
        buffers.emplace_back(std::move(data));
    }
    return new_handle;
}