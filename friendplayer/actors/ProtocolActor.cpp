#include "actors/ProtocolActor.h"

#include "common/Log.h"

#include "actors/CommonActorNames.h"
#include "protobuf/network_messages.pb.h"
#include "protobuf/actor_messages.pb.h"

ProtocolActor::ProtocolActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name)
    : TimerActor(actor_map, buffer_map, std::move(name)),
      frame_window_start(0),
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

void ProtocolActor::OnTimerFire() {
    auto now = clock::now();
    std::optional<clock::time_point> earliest_ts;
    for (auto&& [seqnum, saved_msg] : unacked_messages) {
        if (saved_msg.NeedsSlowRetransmit(RTT_milliseconds)) {
            // if doing a slow RT without fast RT, just say we did a fast to reduce bandwidth
            saved_msg.did_fast_retransmit = true;
            saved_msg.last_send_ts = now;
            fp_network::Network retransmitted_msg;
            *retransmitted_msg.mutable_data_msg() = saved_msg.msg;
            TryIncrementHandle(retransmitted_msg.data_msg());
            SendToSocket(retransmitted_msg, true);
            auto blocking_acks_it = std::find(blocking_acks.begin(), blocking_acks.end(), seqnum);
            // ensure we don't have to re-add the blocking ack
            if (blocking_acks_it == blocking_acks.end()) {
                blocking_acks.emplace_back(seqnum);
            }
        }
        earliest_ts = (earliest_ts ? std::min(saved_msg.last_send_ts, *earliest_ts) : saved_msg.last_send_ts);
    }
    if (earliest_ts) {
        auto earliest_send_dt = std::chrono::duration_cast<std::chrono::milliseconds>(now - *earliest_ts);
        SetTimerInternal(RTT_milliseconds - static_cast<uint32_t>(earliest_send_dt.count()), false);
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
        if (msg.data_msg().needs_ack()) {
            // Saved acked messages which use shared buffers must addref
            TryIncrementHandle(msg.data_msg());
            unacked_messages.emplace(send_sequence_number, SavedDataMessage(msg.data_msg(), clock::now()));
            send_sequence_number++;
            fp_actor::StartTimer start_timer;
            start_timer.set_periodic(false);
            start_timer.set_period_ms(std::min(RTT_milliseconds * 2, static_cast<uint32_t>(300)));
            google::protobuf::Any any_msg;
            any_msg.PackFrom(start_timer);
            EnqueueMessage(std::move(any_msg));
        }
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
            OnDataMessage(msg.data_msg());
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
    // if acking a needs_ack packet, let time timer fire
    // have OnTimerFire determine what the timer needs to be rearmed to
    for (auto& unacked_msg : unacked_messages) {
        auto remove_seqnum_it = unacked_messages.find(msg.sequence_ack());
        auto remove_blocking_ack_it = std::find(blocking_acks.begin(), blocking_acks.end(), msg.sequence_ack());
        if (remove_seqnum_it != unacked_messages.end()) {
            TryDecrementHandle(remove_seqnum_it->second.msg);
            unacked_messages.erase(remove_seqnum_it);
        }
        if (remove_blocking_ack_it != blocking_acks.end()) {
            blocking_acks.erase(remove_blocking_ack_it);
        }
    }
    highest_acked_seqnum = std::max(highest_acked_seqnum, msg.sequence_ack());
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