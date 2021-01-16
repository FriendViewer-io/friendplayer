#include <iostream>
#include <csignal>

#include "common/HeartbeatManager.h"
#include "common/ClientManager.h"
#include "common/ClientProtocolHandler.h"
#include "common/Config.h"
#include "common/Log.h"
#include "common/Timer.h"
#include "streamer/VideoStreamer.h"
#include "streamer/AudioStreamer.h"

std::shared_ptr<ClientSocket> client_socket;
std::shared_ptr<ClientProtocolHandler> protocol_handler;

std::shared_ptr<HostSocket> host_socket;
std::shared_ptr<ClientManager> client_mgr;
std::shared_ptr<HeartbeatManager> heartbeat_mgr;

void exit_handler(int signal) {
    LOG_INFO("Sending disconnect");
    client_socket->SendClientState(fp_proto::ClientState::DISCONNECTING);
    exit(1);
}


void audio_thread_host(std::shared_ptr<HostSocket> sock) {
    AudioStreamer audio_streamer;
    audio_streamer.InitEncoder(64000);
    while (true) {
        auto begin_tm = std::chrono::system_clock::now();
        std::vector<uint8_t> raw_frame_in, enc_frame;
        audio_streamer.CaptureAudio(raw_frame_in);
        auto capture_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - begin_tm);
        if (raw_frame_in.size() == 0) {
            LOG_TRACE("Skipped a whole 10ms, was there no audio???");
            continue;
        }
        begin_tm = std::chrono::system_clock::now();
        audio_streamer.EncodeAudio(raw_frame_in, enc_frame);
        auto encode_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - begin_tm);

        begin_tm = std::chrono::system_clock::now();
        int sz = enc_frame.size();
        sock->WriteAudioFrame(enc_frame);
        auto write_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - begin_tm);
        LOG_TRACE("Capture times={} {} {}", capture_elapsed.count(), encode_elapsed.count(), write_elapsed.count());
    }
}

void audio_thread_client(std::shared_ptr<ClientSocket> sock) {
    AudioStreamer audio_streamer;
    
    audio_streamer.InitDecoder();
    
    std::vector<uint8_t> enc_frame_out, raw_frame_out;
    enc_frame_out.resize(20 * 1024);

    while (true) {
        auto frame_start = std::chrono::system_clock::now();
        auto last_now = frame_start;
        RetrievedBuffer enc_frame_wrapper(enc_frame_out.data(), enc_frame_out.size());
        sock->GetAudioFrame(enc_frame_wrapper);
        auto get_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);
        last_now = std::chrono::system_clock::now();
        std::vector<uint8_t> enc_frame(enc_frame_wrapper.data_out.begin(), enc_frame_wrapper.data_out.end());

        if (enc_frame.empty()) { continue; }

        audio_streamer.DecodeAudio(enc_frame, raw_frame_out);
        auto decode_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);
        last_now = std::chrono::system_clock::now();
        if (raw_frame_out.size() > 0) {
            audio_streamer.PlayAudio(raw_frame_out);
        }
        auto play_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);
        LOG_TRACE("Elapsed times: {} {} {}", get_elapsed.count(), decode_elapsed.count(), play_elapsed.count());
    }
}

void heartbeat(std::shared_ptr<ClientProtocolHandler> protocol_handler) {
    while (true) {
        fp_proto::Message msg;
        msg.mutable_hb_msg()->set_is_response(false);
        protocol_handler->EnqueueSendMessage(std::move(msg));
        Sleep(10000);
    }
}


int main(int argc, char** argv) {
    using namespace std::chrono_literals;

    if (Config::LoadConfig(argc, argv) >= 0)
        return 1;

    Log::init_stdout_logging(LogOptions{Config::EnableTracing});

    Timer timer;
    VideoStreamer streamer;
    
    if (Config::IsHost) {
        client_mgr = std::make_shared<ClientManager>();
        heartbeat_mgr = std::make_shared<HeartbeatManager>(client_mgr);
        host_socket = std::make_shared<HostSocket>(Config::Port, client_mgr, heartbeat_mgr);

        streamer.SetSocket(host_socket);
        host_socket->StartSocket();
        heartbeat_mgr->StartHeartbeatManager();
        
        std::thread aud_th(audio_thread_host, host_socket);
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
            streamer.Encode(send_idr || host_socket->ShouldSendIDR());
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
        signal(SIGINT, exit_handler);
        signal(SIGTERM, exit_handler);
        
        protocol_handler = std::make_shared<ClientProtocolHandler>();
        client_socket = std::make_shared<ClientSocket>(Config::ServerIP, Config::Port, protocol_handler);
        
        streamer.SetSocket(client_socket);
        client_socket->StartSocket();
        protocol_handler->StartWorker();
        
        std::thread aud_th(audio_thread_client, client_socket);
        std::thread heartbeat_thread(heartbeat, protocol_handler);
        if (!streamer.InitDecode()) {
            LOG_CRITICAL("InitDecode failed");
            return 1;
        }
        while (true) {
            auto frame_start = std::chrono::system_clock::now();
            auto last_now = frame_start;
            streamer.Demux();
            auto demux_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);
            last_now = std::chrono::system_clock::now();
            streamer.Decode();
            auto decode_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);
            last_now = std::chrono::system_clock::now();
            streamer.PresentVideo();
            auto present_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - frame_start);
            LOG_TRACE("Elapsed times: {:>10}={:>8}+{:>8}+{:>8}", elapsed.count(), demux_elapsed.count(), decode_elapsed.count(), present_elapsed.count());
        }

    }
}