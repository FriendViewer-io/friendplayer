/**
 * @brief A log abstraction over spdlog for convenience. See the "TemplateExe" project for an
 *example of how to initialize logging.
 *
 * Advantages over std::cout:
 *	- Adds file name and line number to each log line
 *   - Rich formatting via the 'fmt' library, which is C++20 std::format.
 *   - Extremely fast
 *   - Multiple levels of severity for filtering between debug/info/warning/error messages
 *   - Colored console output
 *
 * Usage (initialize once at the beginning of main() then use the macros defined below):
 *
 *    int main(int argc, char* argv[]) {
 *   	Log::init_stdout_logging();
 *       ...
 *       LOG_INFO("Hello");
 *
 * OR
 *
 *    int main(int argc, char* argv[]) {
 *   	Log::init_file_logging("C:\\Memes\\TemplateExe.txt");
 *       ...
 *       LOG_INFO("Hello");
 *
 * Output:
 *    [19:03:07.367] [main.cpp:23] [info] Hello
 *
 */

#pragma once

// Comment the below line to disable trace level logging.
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include <string>

#include "spdlog/spdlog.h"

// Macros for logging.
#define LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define LOG_INFO(...) SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARNING(...) SPDLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)

struct LogOptions {
    // Enable logging for LOG_TRACE. If false, then LOG_TRACE will be a no-op. Beware that if you
    // log many things with LOG_TRACE, then enabling this option to true may cause a slight
    // performance hit. By default, this option is false.
    bool enable_trace_logging = false;
};

class Log {
 public:
    // Log to stdout (console). Supports colored log messages.
    static void init_stdout_logging(const LogOptions& log_options = {});

    // Log to the specified file path. If the file path does not exist, then the necessary
    // folders and file will be created for you.
    static void init_file_logging(const std::string& file_path, const LogOptions& log_options = {});
};
