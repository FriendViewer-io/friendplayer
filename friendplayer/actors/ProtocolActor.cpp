#include "actors/ProtocolActor.h"

#include "common/Log.h"

#include "actors/CommonActorNames.h"
#include "protobuf/network_messages.pb.h"
#include "protobuf/actor_messages.pb.h"

ProtocolActor::ProtocolActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
    : TimerActor(actor_map, buffer_map, std::move(name)),
      highest_acked_seqnum(0),
      send_sequence_number(0) {}

ProtocolActor::~ProtocolActor() {

}

void ProtocolActor::OnInit(const std::optional<any_msg>& init_msg) {
    TimerActor::OnInit(init_msg);
    if (init_msg) {
        if (init_msg->Is<fp_actor::ProtocolInit>()) {
            fp_actor::ProtocolInit msg;
            init_msg->UnpackTo(&msg);
            address = msg.address();
        }
    }
}

void ProtocolActor::OnMessage(const any_msg& msg) { 
    if (msg.Is<fp_actor::HeartbeatRequest>()) {
        fp_network::Network heartbeat_msg;
        heartbeat_msg.mutable_hb_msg()->set_is_response(false);
        heartbeat_msg.mutable_hb_msg()->set_timestamp(clock::now().time_since_epoch().count());
        SendToSocket(heartbeat_msg);
    } else if (msg.Is<fp_network::Network>()) {
        fp_network::Network net_msg;
        msg.UnpackTo(&net_msg);
        OnNetworkMessage(net_msg);
    } else if (msg.Is<fp_actor::NetworkSend>()) {
        fp_network::Network network_msg;
        msg.UnpackTo(&network_msg);
        SendToSocket(network_msg);
    } else {
        TimerActor::OnMessage(msg);
    }
}

void ProtocolActor::SendToSocket(fp_network::Network& msg, bool is_retransmit) {
    fp_actor::NetworkSend send_msg;
    send_msg.set_address(address);

    // Data messages can be acked so we must handle that here
    if (!is_retransmit && msg.Payload_case() == fp_network::Network::kDataMsg) {
        msg.mutable_data_msg()->set_sequence_number(send_sequence_number);
        // Saved acked messages which use shared buffers must addref
        TryIncrementHandle(msg.data_msg());
        unacked_messages.emplace_back(msg.data_msg());
        send_sequence_number++;
    }
    send_msg.mutable_msg()->CopyFrom(msg);
    SendTo(SOCKET_ACTOR_NAME, send_msg);
}

void ProtocolActor::OnNetworkMessage(const fp_network::Network& msg) {
    switch (msg.Payload_case()) {
    case fp_network::Network::kAckMsg: {
        OnAcknowledge(msg.ack_msg());
        break;
    }
    case fp_network::Network::kHbMsg: {
        if (msg.hb_msg().is_response()) {
            auto arrival_time = clock::now();
            RTT_milliseconds = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(arrival_time.time_since_epoch() - clock::duration(msg.hb_msg().timestamp())).count());
            fp_actor::ClientActorHeartbeatState heartbeat_state;
            heartbeat_state.set_client_actor_name(GetName());
            heartbeat_state.set_disconnected(false);
            SendTo(HEARTBEAT_ACTOR_NAME, heartbeat_state);
            LOG_INFO("Got heartbeat response from client {}, RTT={}", GetName(), RTT_milliseconds);
        } else {
            fp_network::Network send_heartbeat_msg;
            send_heartbeat_msg.CopyFrom(msg);
            send_heartbeat_msg.mutable_hb_msg()->set_is_response(true);
            SendToSocket(send_heartbeat_msg);
        }
        break;
    }
    case fp_network::Network::kHsMsg: {
        LOG_INFO("Received handshake message");
        if (!OnHandshakeMessage(msg.hs_msg())) {
            // If handshake fails kill this client
            fp_actor::ClientDisconnect dc_msg;
            dc_msg.set_client_name(GetName());
            SendTo(CLIENT_MANAGER_ACTOR_NAME, dc_msg);

            fp_actor::Kill kill_msg;
            EnqueueMessage(kill_msg);
        }
        break;
    }
    case fp_network::Network::kDataMsg: {
        if (protocol_state == HandshakeState::HS_READY) {
            fp_network::Network ack_msg;
            uint64_t msg_seqnum = msg.data_msg().sequence_number();
            ack_msg.mutable_ack_msg()->set_sequence_ack(msg_seqnum);
            SendToSocket(ack_msg);

            if (msg.data_msg().sequence_number() >= receive_window_start) {
                recv_window.push(msg.data_msg());
            }
            const auto process_queue = [this] () {
                while (!recv_window.empty() && recv_window.top().sequence_number() == receive_window_start) {
                    OnDataMessage(recv_window.top());
                    recv_window.pop();
                    receive_window_start++;
                }
            };
            process_queue();
            while (receive_window_start + RECEIVE_FFWD_WINDOW < msg_seqnum) {
                if (recv_window.empty()) {
                    receive_window_start = msg_seqnum - RECEIVE_FFWD_WINDOW;
                } else {
                    receive_window_start = std::min(msg_seqnum - RECEIVE_FFWD_WINDOW, recv_window.top().sequence_number());
                }
                process_queue();
            }
        }
        break;
    }
    case fp_network::Network::kStateMsg: {
        if (protocol_state == HandshakeState::HS_READY) {
            OnStateMessage(msg.state_msg());
        }
        break;
    }
    case fp_network::Network::kInfoMsg: {
        OnStreamInfoMessage(msg.info_msg());
        break;
    }
    }
}

void ProtocolActor::OnAcknowledge(const fp_network::Ack& msg) {
    // Erase the acked message
    const uint64_t acked_num = msg.sequence_ack();
    for (auto it = unacked_messages.begin(); it != unacked_messages.end(); it++) {
        if (it->sequence_number() == acked_num) {
            TryDecrementHandle(*it);
            unacked_messages.erase(it);
            break;
        }
    }

    highest_acked_seqnum = std::max(highest_acked_seqnum, acked_num);
    
    while (!unacked_messages.empty() &&
            unacked_messages.front().sequence_number() + FAST_RETRANSMIT_WINDOW < highest_acked_seqnum) {
        fp_network::Network net_msg;
        *net_msg.mutable_data_msg() = std::move(unacked_messages.front());
        unacked_messages.pop_front();
        LOG_INFO("Retransmitting sequence num {}", net_msg.data_msg().sequence_number());
        // socket decrements for us
        SendToSocket(net_msg, true);
    }
}

void ProtocolActor::TryIncrementHandle(const fp_network::Data& msg) {
    if (msg.Payload_case() == fp_network::Data::kHostFrame) {
        if (msg.host_frame().DataFrame_case() == fp_network::HostDataFrame::kVideo) {
            buffer_map.Increment(msg.host_frame().video().data_handle());
        } else if (msg.host_frame().DataFrame_case() == fp_network::HostDataFrame::kAudio) {
            buffer_map.Increment(msg.host_frame().audio().data_handle());
        }
    }
}

void ProtocolActor::TryDecrementHandle(const fp_network::Data& msg) {
    if (msg.Payload_case() == fp_network::Data::kHostFrame) {
        if (msg.host_frame().DataFrame_case() == fp_network::HostDataFrame::kVideo) {
            buffer_map.Decrement(msg.host_frame().video().data_handle());
        } else if (msg.host_frame().DataFrame_case() == fp_network::HostDataFrame::kAudio) {
            buffer_map.Decrement(msg.host_frame().audio().data_handle());
        }
    }
}