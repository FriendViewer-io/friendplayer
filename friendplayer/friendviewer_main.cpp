#include "common/Log.h"


int main(int argc, char** argv) {
    LogOptions opt = { true };
    Log::init_stdout_logging(opt);
    
    LOG_INFO("info log");
    LOG_WARNING("info log");
    LOG_ERROR("info log");
    LOG_CRITICAL("info log");
    LOG_TRACE("trace log");
}