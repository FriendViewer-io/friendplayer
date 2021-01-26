#include "actors/SocketActor.h"

#include "actors/CommonActorNames.h"
#include "protobuf/actor_messages.pb.h"
#include "common/Log.h"

void SocketActor::OnInit(const std::optional<any_msg>& init_msg) {
    Actor::OnInit(init_msg);
    network_is_running = true;
    network_thread = std::make_unique<std::thread>(&SocketActor::NetworkWorker, this);
}

void SocketActor::OnMessage(const any_msg& msg) {
    if (msg.Is<fp_actor::NetworkSend>()) {
        fp_actor::NetworkSend send_msg;
        msg.UnpackTo(&send_msg);

        fp_network::Network& network_msg = *send_msg.mutable_msg();
        asio_endpoint send_endpoint(asio_address(send_msg.address() & 0xFFFFFFFF), (send_msg.address() >> 32) & 0xFFFF);

        // Fill in buffer with actual data if necessary
        if (network_msg.Payload_case() == fp_network::Network::kDataMsg
            && network_msg.data_msg().Payload_case() == fp_network::Data::kHostFrame) {
            auto& host_frame = *network_msg.mutable_data_msg()->mutable_host_frame();
            uint64_t handle = 0;
            uint64_t seq_num = network_msg.data_msg().sequence_number();
            if (host_frame.has_video()) {
                handle = host_frame.video().data_handle();
                host_frame.mutable_video()->clear_DataBacking();
                std::string* buf = buffer_map.GetBuffer(handle);
                for (size_t chunk_offset = 0; chunk_offset < buf->size(); chunk_offset += MAX_DATA_CHUNK) {
                    const size_t chunk_end = std::min(chunk_offset + MAX_DATA_CHUNK, buf->size());
                    network_msg.mutable_data_msg()->set_sequence_number(seq_num);
                    seq_num++;
                    host_frame.mutable_video()->set_chunk_offset(static_cast<uint32_t>(chunk_offset));
                    host_frame.mutable_video()->set_data(buf->data() + chunk_offset, chunk_end - chunk_offset);
                    socket.send_to(asio::buffer(network_msg.SerializeAsString()), send_endpoint);
                }
            } else if (host_frame.has_audio()) {
                handle = host_frame.audio().data_handle();
                host_frame.mutable_audio()->clear_DataBacking();
                std::string* buf = buffer_map.GetBuffer(handle);
                for (size_t chunk_offset = 0; chunk_offset < buf->size(); chunk_offset += MAX_DATA_CHUNK) {
                    const size_t chunk_end = std::min(chunk_offset + MAX_DATA_CHUNK, buf->size());
                    network_msg.mutable_data_msg()->set_sequence_number(seq_num);
                    seq_num++;
                    host_frame.mutable_audio()->set_chunk_offset(static_cast<uint32_t>(chunk_offset));
                    host_frame.mutable_audio()->set_data(buf->data() + chunk_offset, chunk_end - chunk_offset);
                    socket.send_to(asio::buffer(network_msg.SerializeAsString()), send_endpoint);
                }
            }
            buffer_map.Decrement(handle);
        } else {
            socket.send_to(asio::buffer(network_msg.SerializeAsString()), send_endpoint);
        }
    } else {
        Actor::OnMessage(msg);
    }
}

void SocketActor::OnFinish() {
    network_is_running = false;
    socket.close();
    network_thread->join();
    Actor::OnFinish();
}

void SocketActor::NetworkWorker() {
    std::string recv_buffer;
    recv_buffer.resize(1500);

    while (network_is_running) {
        asio::error_code ec;
        size_t recv_size = socket.receive_from(asio::buffer(recv_buffer), endpoint, 0, ec);

        if (recv_size == 0 || ec.value() != 0) {
            continue;
        }
        fp_actor::NetworkRecv msg;
        uint64_t address = endpoint.address().to_v4().to_uint();
        address |= static_cast<uint64_t>(endpoint.port()) << 32;
        msg.set_address(address);

        fp_network::Network recv_msg;
        recv_msg.ParseFromArray(recv_buffer.data(), recv_size);
        // Put data message in buffer
        if (recv_msg.Payload_case() == fp_network::Network::kDataMsg
            && recv_msg.data_msg().Payload_case() == fp_network::Data::kHostFrame) {
            auto& host_frame = *recv_msg.mutable_data_msg()->mutable_host_frame();
            if (host_frame.has_video()) {
                std::string* data = host_frame.mutable_video()->release_data();
                host_frame.mutable_video()->clear_DataBacking();
                host_frame.mutable_video()->set_data_handle(buffer_map.Wrap(data));
            } else if (host_frame.has_audio()) {
                std::string* data = host_frame.mutable_audio()->release_data();
                host_frame.mutable_audio()->clear_DataBacking();
                host_frame.mutable_audio()->set_data_handle(buffer_map.Wrap(data));
            }
        }
        *msg.mutable_msg() = recv_msg;
        SendTo(CLIENT_MANAGER_ACTOR_NAME, msg);
    }
}
