#pragma once

#include "actors/TimerActor.h"
#include "actors/DataBuffer.h"

#include "protobuf/network_messages.pb.h"

#include <chrono>
#include <list>
#include <queue>
#include <vector>

class ProtocolActor : public TimerActor {
public:
    static constexpr int FAST_RETRANSMIT_WINDOW = 4;
    static constexpr int RECEIVE_FFWD_WINDOW = 80;

    ProtocolActor(const ActorMap& actor_map, DataBufferMap& buffer_map, std::string&& name);

    virtual ~ProtocolActor();

    void OnInit(const std::optional<any_msg>& init_msg) override;
    void OnMessage(const any_msg& msg) override;
    void OnTimerFire() override {}

protected:
    uint64_t address;

    void OnNetworkMessage(const fp_network::Network& msg);
    void OnAcknowledge(const fp_network::Ack& msg);

    virtual bool OnHandshakeMessage(const fp_network::Handshake& msg) = 0;
    virtual void OnDataMessage(const fp_network::Data& msg) = 0;
    virtual void OnStateMessage(const fp_network::State& msg) = 0;
    virtual void OnStreamInfoMessage(const fp_network::StreamInfo& msg) { }

    void SendToSocket(fp_network::Network& msg, bool is_retransmit = false);

    enum HandshakeState {
        HS_UNINITIALIZED, HS_WAITING_SHAKE_ACK, HS_READY, HS_FAILED
    };
    uint32_t RTT_milliseconds;
    uint64_t highest_acked_seqnum;

    HandshakeState protocol_state;

    uint64_t send_sequence_number;
    // Ack window for stream
    std::list<fp_network::Data> unacked_messages;
    uint64_t receive_window_start;

    struct SeqnumLess {
        bool operator()(const fp_network::Data& lhs, const fp_network::Data& rhs) const {
            return lhs.sequence_number() > rhs.sequence_number();
        }
    };
    // Recv window for stream
    std::priority_queue<fp_network::Data, std::vector<fp_network::Data>, SeqnumLess> recv_window;

    void TryIncrementHandle(const fp_network::Data& msg);
    void TryDecrementHandle(const fp_network::Data& msg);
};

DEFINE_ACTOR_GENERATOR(ProtocolActor)