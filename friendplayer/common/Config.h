#pragma once

#include <string>
#include <vector>

namespace Config {
	extern int LoadConfig(int argc, char** argv);

	extern bool IsHost;
	extern int AverageBitrate;
	extern std::string ServerIP;
	extern unsigned short Port;
	extern std::vector<int> MonitorIndecies;
	extern bool EnableTracing;
	extern bool SaveControllers;
	
	extern std::string HolepuncherIP;
	extern std::string Identifier;
	extern std::string HostIdentifier;
}