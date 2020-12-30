#include "common/Log.h"
#include "Streamer.h"

int main(int argc, char** argv) {
    using namespace std::chrono_literals;

    LogOptions opt = { false };
    Log::init_stdout_logging(opt);

    if (argc != 4) {
        LOG_CRITICAL("Incorrect number of args - %s <streamer/client> ip <port>", argv[0]);
        return 1;
    }

    Streamer streamer;


    bool is_sender = strcmp(argv[1], "streamer") == 0;
    streamer.InitConnection(argv[2], atoi(argv[3]), is_sender);
    if (is_sender) {
        if (!streamer.InitEncode()) {
            LOG_CRITICAL("InitEncode failed");
            return 1;
        }

        while (true) {
            auto frame_start = std::chrono::system_clock::now();
            auto last_now = frame_start;
            streamer.CaptureFrame(8);
            auto capture_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);
            last_now = std::chrono::system_clock::now();
            streamer.Encode(false);
            auto encode_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - last_now);
            std::this_thread::sleep_until(frame_start + 16666us);
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - frame_start);
            LOG_TRACE("Elapsed times: {} {} {}", capture_elapsed.count(), encode_elapsed.count(), elapsed.count());
        }
    } else {
        if (!streamer.InitDecode(50)) {
            LOG_CRITICAL("InitDecode failed");
            return 1;
        }
        while (true) {
            streamer.Demux();
            streamer.Decode();
            streamer.PresentVideo();
        }

    }



    
    LOG_INFO("info log");
    LOG_WARNING("info log");
    LOG_ERROR("info log");
    LOG_CRITICAL("info log");
    LOG_TRACE("trace log");
}