#include <iostream>
#include <csignal>
#include <concurrentqueue/blockingconcurrentqueue.h>

#include "actors/ActorEnvironment.h"
#include "actors/CommonActorNames.h"

#include "common/Config.h"
#include "common/Log.h"

#include "protobuf/actor_messages.pb.h"

void exit_handler(int signal) {
    LOG_INFO("Sending disconnect");
    // if (client_socket) {
    //     client_socket->SendStreamState(fp_proto::ClientState::DISCONNECTING);
    // } else {
    //     host_socket->SendStreamState(fp_proto::HostState::DISCONNECTING);
    // }
    exit(1);
}


// void audio_thread_host(std::shared_ptr<HostSocket> sock) {
//     moodycamel::BlockingConcurrentQueue<int> test;
//     AudioStreamer audio_streamer;
//     audio_streamer.InitEncoder(64000);
//     while (true) {
//         auto begin_tm = std::chrono::system_clock::now();
//         std::vector<uint8_t> raw_frame_in, enc_frame;
//         audio_streamer.CaptureAudio(raw_frame_in);
//         auto capture_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - begin_tm);
//         if (raw_frame_in.size() == 0) {
//             LOG_TRACE("Skipped a whole 10ms, was there no audio???");
//             continue;
//         }
//         begin_tm = std::chrono::system_clock::now();
//         audio_streamer.EncodeAudio(raw_frame_in, enc_frame);
//         auto encode_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - begin_tm);
//
//         begin_tm = std::chrono::system_clock::now();
//         int sz = enc_frame.size();
//         sock->WriteAudioFrame(enc_frame);
//         auto write_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - begin_tm);
//         LOG_TRACE("Capture times={} {} {}", capture_elapsed.count(), encode_elapsed.count(), write_elapsed.count());
//     }
// }
//
// void audio_thread_client(std::shared_ptr<ClientSocket> sock) {
//     AudioStreamer audio_streamer;
//    
//     audio_streamer.InitDecoder();
//    
//     std::vector<uint8_t> enc_frame_out, raw_frame_out;
//     enc_frame_out.resize(20 * 1024);
//
//     while (protocol_mgr->HasClients()) {
//         auto frame_start = std::chrono::system_clock::now();
//         auto last_now = frame_start;
//         RetrievedBuffer enc_frame_wrapper(enc_frame_out.data(), enc_frame_out.size());
//         sock->GetAudioFrame(enc_frame_wrapper);
//         auto get_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);
//         last_now = std::chrono::system_clock::now();
//         std::vector<uint8_t> enc_frame(enc_frame_wrapper.data_out.begin(), enc_frame_wrapper.data_out.end());
//
//         if (enc_frame.empty()) { continue; }
//
//         audio_streamer.DecodeAudio(enc_frame, raw_frame_out);
//         auto decode_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);
//         last_now = std::chrono::system_clock::now();
//         if (raw_frame_out.size() > 0) {
//             audio_streamer.PlayAudio(raw_frame_out);
//         }
//         auto play_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);
//         LOG_TRACE("Elapsed times: {} {} {}", get_elapsed.count(), decode_elapsed.count(), play_elapsed.count());
//     }
// }

int main(int argc, char** argv) {
    using namespace std::chrono_literals;

    if (Config::LoadConfig(argc, argv) >= 0)
        return 1;

    Log::init_stdout_logging(LogOptions{Config::EnableTracing});

    ActorEnvironment env;
    google::protobuf::Any any_msg;

    fp_actor::SocketInit socket_init;
    socket_init.set_port(Config::Port);
    std::string socket_type;
    if (!Config::IsHost) {
        socket_init.set_ip(Config::ServerIP);
        socket_type = "ClientSocketActor";
    } else {
        socket_type = "HostSocketActor";
    }
    any_msg.PackFrom(socket_init);
    env.AddActor(socket_type, SOCKET_ACTOR_NAME, std::make_optional(std::move(any_msg)));

    if (!Config::IsHost) {
        fp_actor::ClientClientManagerInit client_mgr_init;
        client_mgr_init.set_host_ip(Config::ServerIP);
        client_mgr_init.set_host_port(Config::Port);
        any_msg.PackFrom(client_mgr_init);
    } else {
        fp_actor::HostClientManagerInit client_mgr_init;
        for (int monitor_index : Config::MonitorIndecies) {
            client_mgr_init.add_monitor_indices(monitor_index);
        }
        client_mgr_init.set_port(Config::Port);
        any_msg.PackFrom(client_mgr_init);
    }
    env.AddActor("ClientManagerActor", CLIENT_MANAGER_ACTOR_NAME, std::make_optional(std::move(any_msg)));

    fp_actor::HeartbeatActorInit heartbeat_init;
    heartbeat_init.set_timeout_ms(10000);
    heartbeat_init.set_heartbeat_send_ms(2000);
    any_msg.PackFrom(heartbeat_init);
    env.AddActor("HeartbeatActor", HEARTBEAT_ACTOR_NAME, std::make_optional(std::move(any_msg)));

    env.StartEnvironment();
}