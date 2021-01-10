#include "common/Client.h"

#include "common/Socket.h"
#include "common/Log.h"

#include <asio/buffer.hpp>


void Client::ClientMessageWorker() {
    const auto process_queue_cons = [this] {
        while (!task_queue.empty() && (task_queue.front().frame_id == frame_window_start)) {
            task_queue.front().deferred.get();
            frame_window_start = task_queue.front().frame_id + 1;
            task_queue.pop_front();
        }
    };

    while (IsConnectionValid()) {
        std::unique_lock<std::mutex> lock(*queue_m);
        queue_cv->wait(lock, [this] { return queue_ready->load(); });
        if (!IsConnectionValid()) {
            queue_ready->store(false);
            break;
        }
        if (task_queue.empty()) {
            queue_ready->store(false);
            continue;
        }

        process_queue_cons();
        if (!task_queue.empty()) {
            // window is a bit behind, dropped packets perhaps?
            if (task_queue.front().frame_id > frame_window_start + DROP_WINDOW) {
                frame_window_start = task_queue.front().frame_id;
                process_queue_cons();
            }
        }
        queue_ready->store(false);
    }
    std::unique_lock<std::mutex> lock(*queue_m);
    LOG_INFO("Message worker for client {} exiting, timeout={}", client_id, IsConnectionValid());
}

void Client::EnqueueMessage(fp_proto::ClientDataFrame&& message) {
    ClientTask new_task;
    uint32_t id = message.frame_id();

    std::lock_guard<std::mutex> lock(*queue_m);
    LOG_INFO("Got a message, id={}", id);
    if (id < frame_window_start) {
        LOG_INFO("Dropping out of order packet from client {}, id={}, window={}", client_id, id, frame_window_start);
        return;
    }
    new_task.frame_id = id;
    switch (message.DataFrame_case()) {
    case fp_proto::ClientDataFrame::kKeyboard:
        new_task.deferred = std::async(std::launch::deferred, &Client::OnKeyboardFrame, this, std::move(message));
        break;
    case fp_proto::ClientDataFrame::kMouse:
        new_task.deferred = std::async(std::launch::deferred, &Client::OnMouseFrame, this, std::move(message));
        break;
    case fp_proto::ClientDataFrame::kController:
        new_task.deferred = std::async(std::launch::deferred, &Client::OnControllerFrame, this, std::move(message));
        break;
    case fp_proto::ClientDataFrame::kHostRequest:
        new_task.deferred = std::async(std::launch::deferred, &Client::OnHostRequest, this, std::move(message));
        break;
    case fp_proto::ClientDataFrame::kClientState:
        new_task.deferred = std::async(std::launch::deferred, &Client::OnClientState, this, std::move(message));
        break;
    default:
        LOG_ERROR("Unknown message type from host: {}", static_cast<int>(message.DataFrame_case()));
        break;
    }
    
    auto it = task_queue.begin();
    for (; it != task_queue.end(); it++) {
        if (id < it->frame_id) {
            break;
        }
    }
    task_queue.emplace(it, std::move(new_task));
    queue_ready->store(true);
    queue_cv->notify_one();
}


void Client::OnClientState(const fp_proto::ClientDataFrame& msg) {
    const auto& cl_state = msg.client_state();
    LOG_INFO("Client state = {}", static_cast<int>(cl_state.state()));
    switch(cl_state.state()) {
        case fp_proto::ClientState::READY_FOR_PPS_SPS_IDR: {
            Transition(Client::ClientState::WAITING_FOR_VIDEO);
            parent_socket->SetNeedIDR(true);
        }
        break;
        case fp_proto::ClientState::READY_FOR_VIDEO: {
            Transition(Client::ClientState::READY);
        }
        break;
        case fp_proto::ClientState::HEARTBEAT: {
            Beat();
        }
        break;
        case fp_proto::ClientState::DISCONNECTING: {
            Transition(Client::ClientState::DISCONNECTED);
        }
        default: {
            LOG_ERROR("Client sent unknown state: {}", static_cast<int>(cl_state.state()));
        }
        break;
    }
}

void Client::OnHostRequest(const fp_proto::ClientDataFrame& msg) {
    const auto& request = msg.host_request();
    LOG_INFO("Client request to host = {}", static_cast<int>(request.type()));
    switch(request.type()) {
        case fp_proto::RequestToHost::SEND_IDR: {
            parent_socket->SetNeedIDR(true);
        }
        break;
        case fp_proto::RequestToHost::MUTE_AUDIO: {
            SetAudio(false);
        }
        break;
        case fp_proto::RequestToHost::PLAY_AUDIO: {
            SetAudio(true);
        }
    }
}

void Client::OnKeyboardFrame(const fp_proto::ClientDataFrame& msg) {

}

void Client::OnMouseFrame(const fp_proto::ClientDataFrame& msg) {

}

void Client::OnControllerFrame(const fp_proto::ClientDataFrame& msg) {

}

void Client::SendHostData(fp_proto::HostDataFrame& frame, const std::vector<uint8_t>& data) {
    
    switch (frame.DataFrame_case()) {
        case fp_proto::HostDataFrame::kAudio: {
            if (state != ClientState::READY) {
                return;
            }
        }
        break;
        case fp_proto::HostDataFrame::kVideo: {
            if (state != ClientState::READY
                && (state != ClientState::WAITING_FOR_VIDEO || !frame.video().is_idr_frame())) {
                return;
            }
        }
        break;
    }

    std::vector<uint8_t> buffered_data;
    if (video_frame_num == 0 && frame.DataFrame_case() == fp_proto::HostDataFrame::kVideo) {
        buffered_data = parent_socket->GetPPSSPS();
        buffered_data.insert(buffered_data.end(), data.begin(), data.end());
    } else {
        buffered_data = data;
    }
    
    frame.set_frame_size(buffered_data.size());

    for (size_t chunk_offset = 0; chunk_offset < buffered_data.size(); chunk_offset += MAX_DATA_CHUNK) {
        // if (fuckup_frame) {
        //     fuckup_frame = false;
        //     continue;
        // }
        
        const size_t chunk_end = std::min(chunk_offset + MAX_DATA_CHUNK, buffered_data.size());
        switch (frame.DataFrame_case()) {
            case fp_proto::HostDataFrame::kAudio: {
                frame.set_frame_num(audio_frame_num);
                frame.set_stream_point(audio_stream_point);
                frame.mutable_audio()->set_chunk_offset(static_cast<uint32_t>(chunk_offset));
                frame.mutable_audio()->set_data(buffered_data.data() + chunk_offset, 
                    chunk_end - chunk_offset);
            }
            break;
            case fp_proto::HostDataFrame::kVideo: {
                frame.set_frame_num(video_frame_num);
                frame.set_stream_point(video_stream_point);
                frame.mutable_video()->set_chunk_offset(static_cast<uint32_t>(chunk_offset));
                frame.mutable_video()->set_data(buffered_data.data() + chunk_offset, 
                    chunk_end - chunk_offset);
            }
            break;
        }
        parent_socket->WriteData(asio::buffer(frame.SerializeAsString()), client_endpoint);
    }
    switch (frame.DataFrame_case()) {
        case fp_proto::HostDataFrame::kAudio: {
            audio_stream_point += buffered_data.size();
            audio_frame_num++;
        }
        break;
        case fp_proto::HostDataFrame::kVideo: {
            video_stream_point += buffered_data.size();
            video_frame_num++;
        }
        break;
    }
}