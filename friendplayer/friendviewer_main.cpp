#include "common/Log.h"
#include "common/Timer.h"
#include "Streamer.h"


int main(int argc, char** argv) {
    using namespace std::chrono_literals;

    LogOptions opt = { false };
    Log::init_stdout_logging(opt);

    if (argc != 4) {
        LOG_CRITICAL("Incorrect number of args - %s <streamer/client> ip <port>", argv[0]);
        return 1;
    }

    Timer timer;
    Streamer streamer;

    bool is_sender = strcmp(argv[1], "streamer") == 0;
    streamer.InitConnection(argv[2], atoi(argv[3]), is_sender);
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

            LOG_INFO("Capturing at approx {} frames/sec", (static_cast<double>(framenum) / static_cast<double>(total_elapsed.count())) * 1000000.0);

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