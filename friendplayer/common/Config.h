#pragma once

#include <string>

namespace Config {
	extern int LoadConfig(int argc, char** argv);

	extern bool IsHost;
	extern int AverageBitrate;
	extern std::string ServerIP;
	extern unsigned short Port;
	extern int MonitorIndex;
	extern bool EnableTracing;
}