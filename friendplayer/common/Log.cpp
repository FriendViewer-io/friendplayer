#include "common/Log.h"

#include <memory>

#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

namespace {

void common_init(const std::shared_ptr<spdlog::logger>& logger, const LogOptions& log_options) {
    const auto min_log_level =
        log_options.enable_trace_logging ? spdlog::level::trace : spdlog::level::info;
    logger->set_level(min_log_level);
    // logger->flush_on(min_log_level);
    spdlog::set_default_logger(logger);
}

}  // namespace

void Log::init_stdout_logging(const LogOptions& log_options) {
    std::shared_ptr<spdlog::logger> logger = spdlog::stdout_color_mt("log");
    // Example of pattern: [00:58:40.928] [log_example.cc:6] Trace msg
    // The difference between this pattern and the pattern used in Log::init_file_logging() is that
    // we don't include the level because level is represented using color.
    logger->set_pattern("%^[%H:%M:%S.%e] [%s:%#] %v%$");
    common_init(logger, log_options);
}

void Log::init_file_logging(const std::string& file_path, const LogOptions& log_options) {
    constexpr bool CLEAR_FILE_IF_NOT_EMPTY = true;
    std::shared_ptr<spdlog::logger> logger =
        spdlog::basic_logger_mt("log", file_path, CLEAR_FILE_IF_NOT_EMPTY);
    // Example of pattern: [00:58:40.928] [log_example.cc:6] [trace] Trace msg
    logger->set_pattern("%^[%H:%M:%S.%e] [%s:%#] [%l] %v%$");
    common_init(logger, log_options);
}
