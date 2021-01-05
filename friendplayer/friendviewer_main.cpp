#include <winsock2.h>

#include "common/Log.h"
#include "common/Timer.h"
#include "Streamer.h"
#include "audio/AudioStreamer.h"

void audio_thread(bool is_sender, short port, char* ip) {
    WSAData wsa_data;
    WSAStartup(MAKEWORD(2,2), &wsa_data);

    AudioStreamer audio_streamer;

    // streamer.InitConnection(argv[2], atoi(argv[3]), is_sender);
    if (is_sender) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        struct sockaddr_in addr = { 0 };
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.S_un.S_addr = INADDR_ANY;
        bind(s, (struct sockaddr*)(&addr), sizeof(struct sockaddr_in));

        listen(s, SOMAXCONN);
        SOCKET natsock = accept(s, nullptr, nullptr);

        audio_streamer.InitEncoder(64000);

        while (true) {
            std::vector<uint8_t> raw_frame_in, enc_frame;
            audio_streamer.CaptureAudio(raw_frame_in);
            if (raw_frame_in.size() == 0) { continue; }
            audio_streamer.EncodeAudio(raw_frame_in, enc_frame);
            int sz = enc_frame.size();
            LOG_TRACE("Send size: {} bytes", sz);
            send(natsock, (char*)&sz, 4, 0);
            send(natsock, (char*)enc_frame.data(), enc_frame.size(), 0);
        }
    } else {
        audio_streamer.InitDecoder();
        
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        struct sockaddr_in addr = { 0 };
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.S_un.S_addr = inet_addr(ip);

        connect(s, (struct sockaddr*)&addr, sizeof(struct sockaddr_in));

        while (true) {
            int incoming_sz;
            recv(s, (char*)&incoming_sz, 4, 0);

            std::vector<uint8_t> raw_frame_out, enc_frame;
            enc_frame.resize(incoming_sz);
            recv(s, (char*)enc_frame.data(), incoming_sz, 0);

            audio_streamer.DecodeAudio(enc_frame, raw_frame_out);
            audio_streamer.PlayAudio(raw_frame_out);
        }
    }
}

int main(int argc, char** argv) {
    using namespace std::chrono_literals;

    Log::init_stdout_logging(LogOptions{false});

    if (argc != 4) {
        LOG_CRITICAL("Incorrect number of args - %s <streamer/client> ip <port>", argv[0]);
        return 1;
    }

    Timer timer;
    Streamer streamer;

    bool is_sender = strcmp(argv[1], "streamer") == 0;
    streamer.InitConnection(argv[2], atoi(argv[3]), is_sender);
    std::thread aud_th(audio_thread, is_sender, atoi(argv[3]), argv[2]);
    
    if (is_sender) {
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
            streamer.Encode(send_idr);
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
                LOG_INFO("Elapsed times: {} {} {}", capture_elapsed.count(), encode_elapsed.count(), elapsed.count());
            }
        }
    } else {
        if (!streamer.InitDecode(50)) {
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