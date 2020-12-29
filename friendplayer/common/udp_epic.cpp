#include "udp_epic.h"

#include <iostream>
#include <WinSock2.h>

template <size_t sz>
struct FrameChunk {
    uint32_t frame_num;
    uint32_t chunk_num;
    std::array<char, sz> chunk_data;
};

void UDPSocketSender::init_connection(const char* ip, short port) {
    WSAData wsaData;
    WSAStartup(MAKEWORD(2,2), &wsaData);

    socket_ = socket(AF_INET, SOCK_DGRAM , IPPROTO_UDP);

    target_addr = (struct sockaddr_in*)malloc(sizeof(struct sockaddr_in));
    memset(target_addr, 0, sizeof(struct sockaddr_in));
    sockaddr_len = sizeof(struct sockaddr_in);

    struct sockaddr_in me_addr;
    memset(&me_addr, 0, sizeof(struct sockaddr_in));
    me_addr.sin_family = AF_INET;
	me_addr.sin_addr.s_addr = INADDR_ANY;
	me_addr.sin_port = htons(port);
    
    bind(socket_, (struct sockaddr*)&me_addr, sizeof(struct sockaddr_in));
    
    int recv_buffer = 256 * 1024;
    setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, (char*) &recv_buffer, sizeof(int));
}

void UDPSocketSender::sync() {
    char dumb[4];
    int ret=recvfrom(socket_, dumb, 4, 0, (struct sockaddr *)target_addr, &sockaddr_len);
}

void UDPSocketSender::start_backend() {
    worker_thread = new std::thread(&UDPSocketSender::send_frames, this);
}

void UDPSocketSender::enqueue_send(std::vector<uint8_t>&& outgoing) {
    {
        std::lock_guard<std::mutex> lck(mutex);
        out_buf.emplace_back(std::move(outgoing));
    }
    cond.notify_one();
}

// send literally everything for a frame
void UDPSocketSender::send_frames() {
    uint32_t current_frame_number = 0;
    FrameChunk<chunk_size> chunk;

    while (true) {
        std::unique_lock<std::mutex> lck(mutex);
        if (out_buf.size() == 0) {
            cond.wait(lck, [this] () -> bool { return out_buf.size() > 0; });
        }
        while (out_buf.size() > 0) {
            std::vector<uint8_t> send_buf;
            out_buf.front().swap(send_buf);
            out_buf.pop_front();

            FrameInfo info = { current_frame_number, send_buf.size() };
            sendto(socket_, (char*)&info, 8, 0,
                (struct sockaddr*)target_addr, sizeof(struct sockaddr_in));


            chunk.frame_num = current_frame_number;
            for (size_t i = 0; (i * chunk_size) < send_buf.size(); i++) {
                chunk.chunk_num = i;
                size_t chunk_end = (i + 1) * chunk_size;
                size_t chunk_begin = i * chunk_size;
                size_t send_size = (chunk_end > send_buf.size()) ?
                                        (send_buf.size() - chunk_begin) :
                                        chunk_size;
                memcpy(chunk.chunk_data.data(), send_buf.data() + chunk_begin, send_size);
                sendto(socket_, (char*)&chunk, send_size + 8, 0, 
                    (struct sockaddr*)target_addr, sizeof(struct sockaddr_in));
                
            }
            current_frame_number++;
        }
    }
}

void UDPSocketReceiver::start_backend() {
    worker_thread = new std::thread(&UDPSocketReceiver::read_frames, this);
}

// WSAStartup, create socket, set target conn,
void UDPSocketReceiver::init_connection(const char* ip, short port) {
    WSAData wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
    
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    
    target_addr = (struct sockaddr_in*)malloc(sizeof(struct sockaddr_in));
    sockaddr_len = sizeof(struct sockaddr_in);

    memset(target_addr, 0, sizeof(struct sockaddr_in));
    target_addr->sin_addr.S_un.S_addr = inet_addr(ip);
    target_addr->sin_family = AF_INET;
    target_addr->sin_port = htons(port);

    int send_buffer = 256 * 1024;    // 64 KB
    int send_buffer_sizeof = sizeof(int);
    setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, (char*)&send_buffer, send_buffer_sizeof);

    current_frame_index = 0;
}


void UDPSocketReceiver::sync() {
    char dumb[4];
    sendto(socket_, dumb, 4, 0, (struct sockaddr *) target_addr, sockaddr_len);
}

uint32_t UDPSocketReceiver::get_frame(uint8_t* out_buf, uint32_t timeout_ms) {
    LARGE_INTEGER t_start, t_end, freq;
    QueryPerformanceFrequency(&freq);

    uint32_t final_read_size;
    QueryPerformanceCounter(&t_start);
    std::unique_lock<std::mutex> lock(mutex);
    //std::cout << "Calling get_frame" << std::endl;
    if (frame_buffer[current_frame_index].frame_info.frame_num == 0)
        timeout_ms = 500;
    bool is_frame_complete = frame_buffer[current_frame_index].frame_info.current_read_size == frame_buffer[current_frame_index].frame_info.frame_size || cond.wait_for(lock,
        std::chrono::milliseconds(timeout_ms),
     [this] () -> bool {
        return frame_buffer[current_frame_index].frame_info.current_read_size == frame_buffer[current_frame_index].frame_info.frame_size;
    });
    QueryPerformanceCounter(&t_end);
    long long micros_elapsed = (1000000 * (t_end.QuadPart - t_start.QuadPart)) / freq.QuadPart;
    //std::cout << "Size = " << frame_buffer[current_frame_index].frame_info.current_read_size << "Lock and CV time = " << micros_elapsed << " us" << std::endl;

    if (is_frame_complete) {
        //std::cout << "Received frame #" << frame_buffer[current_frame_index].frame_info.frame_num << " size = " << frame_buffer[current_frame_index].frame_info.current_read_size << std::endl;
        memcpy(out_buf, frame_buffer[current_frame_index].frame_data.data(), frame_buffer[current_frame_index].frame_info.frame_size);
        final_read_size = frame_buffer[current_frame_index].frame_info.frame_size;
    } else {
        // memcpy size here is wrong, it should be max chunk offset for size instead
        memcpy(out_buf, frame_buffer[current_frame_index].frame_data.data(), frame_buffer[current_frame_index].frame_info.current_read_size);
        final_read_size = frame_buffer[current_frame_index].frame_info.current_read_size;
        //std::cout << "garbage! " << frame_buffer[current_frame_index].frame_info.current_read_size << std::endl;
    }

    frame_buffer[current_frame_index].frame_info.frame_num += frame_buffer.size();
    frame_buffer[current_frame_index].frame_info.frame_size = -1;
    frame_buffer[current_frame_index].frame_info.current_read_size = 0;
    
    current_frame_index++;
    current_frame_index %= frame_buffer.size();

    return final_read_size;
}

// wait until timeout or receives a full frame
void UDPSocketReceiver::read_frames() {

    LARGE_INTEGER t_start[5], t_end[5], freq;
    QueryPerformanceFrequency(&freq);

    for (int i = 0; i < 5; ++i) {
        t_start[i].QuadPart = 0;
        t_end[i].QuadPart = 0;
    }
    uint32_t recv_len;
    uint32_t current_frame_num;
    while (true) {
        recv_len = recvfrom(socket_, read_buffer, sizeof(read_buffer), 0, (struct sockaddr *) target_addr, &sockaddr_len);
        std::lock_guard<std::mutex> lock(mutex);
        current_frame_num = frame_buffer[current_frame_index].frame_info.frame_num;
        uint32_t frame_num = ((uint32_t*) read_buffer)[0];
        std::cout << "Received " << recv_len << " bytes frame number " << frame_num << std::endl;
        if (current_frame_num > frame_num) {
            continue;
        } else if (frame_num >= current_frame_num + frame_buffer.size()) {
            for (int i = 0; i < frame_buffer.size(); ++i) {
                frame_buffer[i].frame_info.frame_num = frame_num + i;
                frame_buffer[i].frame_info.frame_size = -1;
                frame_buffer[i].frame_info.current_read_size = 0;
            }
            current_frame_index = 0;
            std::cout << "Decoder lagging behind? Jumping from " << current_frame_num << " to " << frame_num << std::endl;
            current_frame_num = frame_num;
        }
        uint32_t frame_index = (current_frame_index + (frame_num - current_frame_num)) % frame_buffer.size();
        if (t_start[frame_index].QuadPart == 0) {
            QueryPerformanceCounter(&t_start[frame_index]);
        }
        // Frame begin
        if (recv_len == 8) {
            uint32_t size = ((uint32_t*) read_buffer)[1];
            //std::cout << "Total frame size = " << size << std::endl;
            // Make sure frame is within frame buffer
            frame_buffer[frame_index].frame_info.frame_size = size;
        } else { // Frame chunk
            uint32_t chunk_num = ((uint32_t*) read_buffer)[1];
            // Make sure frame is within frame buffer
            recv_len -= 8;
            memcpy(frame_buffer[frame_index].frame_data.data() + (chunk_num * chunk_size), read_buffer + 8, recv_len);
            frame_buffer[frame_index].frame_info.current_read_size += recv_len;
            //std::cout << "Chunk num = " << chunk_num << " total size = " << frame_buffer[frame_index].frame_info.current_read_size << std::endl;
        }
        if (frame_buffer[frame_index].frame_info.current_read_size == frame_buffer[frame_index].frame_info.frame_size) {
            QueryPerformanceCounter(&t_end[frame_index]);
            long long micros_elapsed = (1000000 * (t_end[frame_index].QuadPart - t_start[frame_index].QuadPart)) / freq.QuadPart;
            std::cout << "Finished frame in " << micros_elapsed << " size of " << frame_buffer[frame_index].frame_info.current_read_size << std::endl;
            t_start[frame_index].QuadPart = 0;
        }
        // If the current frame is complete notify the receiver
        if (current_frame_num == frame_num && frame_buffer[current_frame_index].frame_info.current_read_size == frame_buffer[current_frame_index].frame_info.frame_size) {
            //std::cout << "Packet time = " << micros_elapsed << " us" << std::endl;
            //std::cout << "Average bitrate = " << (total_recv / micros_elapsed) << std::endl;
            //std::cout << "Done with frame " << current_frame_num << std::endl;
            cond.notify_one();
        }
    }
    
}