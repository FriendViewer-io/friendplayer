#pragma once

#include "actors/TimerActor.h"
#include "actors/DataBuffer.h"

#include "protobuf/network_messages.pb.h"

#include <chrono>

class ProtocolActor : public TimerActor {
public:
    static constexpr int FAST_RETRANSMIT_WINDOW = 3;

    ProtocolActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name);

    void OnInit(const std::optional<any_msg>& init_msg) override;
    void OnMessage(const any_msg& msg) override;
    void OnTimerFire() override;

protected:
    uint64_t address;

    void OnNetworkMessage(const fp_network::Network& msg);
    void OnAcknowledge(const fp_network::Ack& msg);
    void OnHeartbeat(const fp_network::Heartbeat& msg);

    virtual bool OnHandshakeMessage(const fp_network::Handshake& msg) = 0;
    virtual void OnDataMessage(const fp_network::Data& msg) = 0;
    virtual void OnStateMessage(const fp_network::State& msg) = 0;

    void SendToSocket(fp_network::Network& msg, bool is_retransmit = false);

    enum HandshakeState {
        HS_UNINITIALIZED, HS_WAITING_SHAKE_ACK, HS_READY, HS_FAILED
    };
    uint32_t RTT_milliseconds;
    uint32_t highest_acked_seqnum;

    HandshakeState protocol_state;

    struct SavedDataMessage {
        SavedDataMessage(const fp_network::Data& msg, clock::time_point send_ts)
            : msg(msg), last_send_ts(send_ts), did_fast_retransmit(false) { }

        fp_network::Data msg;
        clock::time_point last_send_ts;
        bool did_fast_retransmit;

        bool NeedsFastRetransmit(uint32_t current_ack_seqnum) {
            return msg.sequence_number() + FAST_RETRANSMIT_WINDOW < current_ack_seqnum &&
                   !did_fast_retransmit;
        }

        bool NeedsSlowRetransmit(uint32_t RTT_ms) {
            const uint32_t slow_retransmit_time = std::min(RTT_ms * 2, 500u);
            return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - last_send_ts).count() > slow_retransmit_time;
        }
    };

    uint32_t send_sequence_number;
    uint32_t frame_window_start;
    // Ack window for stream
    std::map<uint32_t, SavedDataMessage> unacked_messages;
    // Acks which are blocking further data sending (slow retransmission)
    std::vector<uint32_t> blocking_acks;
    uint32_t timer_seqnum;

    void TryIncrementHandle(const fp_network::Data& msg);
    void TryDecrementHandle(const fp_network::Data& msg);
};

DEFINE_ACTOR_GENERATOR(ProtocolActor)