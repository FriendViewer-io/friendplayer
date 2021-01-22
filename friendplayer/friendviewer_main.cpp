#include <iostream>
#include <csignal>
#include <concurrentqueue/blockingconcurrentqueue.h>

#include "actors/ActorEnvironment.h"
#include "actors/CommonActorNames.h"

#include "common/ProtocolManager.h"
#include "common/Config.h"
#include "common/Log.h"
#include "common/Timer.h"
#include "common/FrameRingBuffer.h"
#include "streamer/VideoStreamer.h"
#include "streamer/AudioStreamer.h"
#include "streamer/InputStreamer.h"

#include "protobuf/actor_messages.pb.h"

// std::shared_ptr<ProtocolManager> protocol_mgr;
// std::shared_ptr<ClientSocket> client_socket;
// std::shared_ptr<HostSocket> host_socket;

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

    fp_actor::ClientManagerInit client_mgr_init;
    client_mgr_init.set_is_host(Config::IsHost);
    if (!Config::IsHost) {
        client_mgr_init.set_host_ip(Config::ServerIP);
        client_mgr_init.set_host_port(Config::Port);
    }
    any_msg.PackFrom(client_mgr_init);
    env.AddActor("ClientManagerActor", CLIENT_MANAGER_ACTOR_NAME, std::make_optional(std::move(any_msg)));

    fp_actor::HeartbeatActorInit heartbeat_init;
    heartbeat_init.set_timeout_ms(10000);
    heartbeat_init.set_heartbeat_send_ms(2000);
    any_msg.PackFrom(heartbeat_init);
    env.AddActor("HeartbeatActor", HEARTBEAT_ACTOR_NAME, std::make_optional(std::move(any_msg)));

    if (Config::IsHost) {
        env.AddActor("VideoEncodeActor", VIDEO_ENCODER_ACTOR_NAME);
    } else {
        env.AddActor("VideoDecodeActor", VIDEO_DECODER_ACTOR_NAME);
    }

    env.StartEnvironment();
}

int main2(int argc, char** argv) {
    using namespace std::chrono_literals;

    if (Config::LoadConfig(argc, argv) >= 0)
        return 1;

    Log::init_stdout_logging(LogOptions{Config::EnableTracing});

    Timer timer;
    VideoStreamer streamer;
    InputStreamer i_streamer;
    
    //protocol_mgr = std::make_shared<ProtocolManager>();

    signal(SIGINT, exit_handler);
    signal(SIGTERM, exit_handler);    

    if (Config::IsHost) {
        //host_socket = std::make_shared<HostSocket>(Config::Port, protocol_mgr);

        //streamer.SetSocket(host_socket);
        //host_socket->StartSocket();
        
        //std::thread aud_th(audio_thread_host, host_socket);
        if (!streamer.InitEncode()) {
            LOG_CRITICAL("InitEncode failed");
            return 1;
        }

        int framenum = 0;
        timer.Start(16666);
        bool end_pressed = false;
        auto capture_start = std::chrono::system_clock::now();

        while (true) {
            auto frame_start = std::chrono::system_clock::now();
            auto last_now = frame_start;
            auto capture_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);
            last_now = std::chrono::system_clock::now();

            bool send_idr = false, end_state = GetAsyncKeyState(VK_END);
            if (end_state && !end_pressed) {
                send_idr = true;
            }
            end_pressed = end_state;
            //streamer.Encode(send_idr || host_socket->ShouldSendIDR());
            auto encode_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);

            if (!timer.Synchronize()) {
                LOG_WARNING("Timer failed to wait for event");
            }
            if (framenum == 0) {
                timer.ResetCadence();
            }

            framenum++;

            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - frame_start);
            auto total_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - capture_start);

            LOG_TRACE("Capturing at approx {} frames/sec", (static_cast<double>(framenum) / static_cast<double>(total_elapsed.count())) * 1000000.0);

            if (elapsed.count() > 20000) {
                //LOG_WARNING("Elapsed times: {} {} {}", capture_elapsed.count(), encode_elapsed.count(), elapsed.count());
            } else {
                LOG_TRACE("Elapsed times: {} {} {}", capture_elapsed.count(), encode_elapsed.count(), elapsed.count());
            }
        }
    } else {
        //client_socket = std::make_shared<ClientSocket>(Config::ServerIP, Config::Port, protocol_mgr);
        
        //client_socket->StartSocket();

        // if (!client_socket->BlockForHandshake()) {
        //     client_socket->Stop();
        //     LOG_ERROR("Handshake to server failed.");
        //     return 1;
        // }
        
        //std::thread aud_th(audio_thread_client, client_socket);
        if (!streamer.InitDecode()) {
            LOG_CRITICAL("InitDecode failed");
            return 1;
        }

        //TODO: instead of passing 0, add desired controller index to config/args.
        i_streamer.RegisterPhysicalController(0);
        while (true) {
            auto frame_start = std::chrono::system_clock::now();
            auto last_now = frame_start;
            //streamer.Demux();
            auto demux_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);
            last_now = std::chrono::system_clock::now();
            //streamer.Decode();
            auto decode_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);
            last_now = std::chrono::system_clock::now();
            //streamer.PresentVideo();
            auto present_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - frame_start);
            LOG_TRACE("Elapsed times: {:>10}={:>8}+{:>8}+{:>8}", elapsed.count(), demux_elapsed.count(), decode_elapsed.count(), present_elapsed.count());

            const auto controller_frame = i_streamer.CapturePhysicalController();
            if(controller_frame.has_value())
            {
                //client_socket->SendController(std::move(controller_frame.value()));
            }
        }
        //client_socket->Stop();
    }
}