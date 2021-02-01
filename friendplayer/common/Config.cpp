#include "Config.h"
#include <CLI/App.hpp>
#include <CLI/Formatter.hpp>
#include <CLI/Config.hpp>

namespace Config {
	bool IsHost;
	int AverageBitrate;
	std::string ServerIP;
	std::string HolepuncherIP;
	std::string HolepunchName;
	std::string HostName;
	unsigned short Port;
	std::vector<int> MonitorIndecies;
	bool EnableTracing;
	bool SaveControllers;

	int LoadConfig(int argc, char** argv) {
		Port = 40040;
		AverageBitrate = 2000000;
		EnableTracing = false;
		SaveControllers = false;
		//HolepuncherIP = "198.199.81.165";
		
		CLI::App parser{ "FriendPlayer" };
		
		parser.add_flag("--trace,-T", EnableTracing, "Enable trace logging");
		CLI::Option* punch_opt = parser.add_option("--puncher-ip,-p", HolepuncherIP, "IP to connect to for hole-punching")
			->default_str("198.199.81.165");
		CLI::Option* name_opt = parser.add_option("--name,-n", HolepunchName, "Name to connect to the holepuncher under");
		punch_opt->needs(name_opt);

		CLI::App* host = parser.add_subcommand("host", "Host the FriendPlayer session");
		host->add_option("--port,-P", Port, "Port to listen on, or the port to connect to for holepunching")
			->default_str("40040");
		host->add_option("--bitrate,-b", AverageBitrate, "Average bitrate for stream")
			->default_str("2000000");
		host->add_option("--monitor,-m", MonitorIndecies, "Monitor index to stream")
			->default_str("0");
		host->add_flag("--save-controllers,-s", SaveControllers, "Reuse controllers after disconnections")
			->default_str("false");
		
		CLI::App* client = parser.add_subcommand("client", "Connect to a FriendPlayer session");
		client->add_option("--port,-P", Port, "Port to listen on, or the port to connect to for holepunching")
			->default_str("40040");
		CLI::Option* ip_opt = client->add_option("--ip,-i", ServerIP, "IP to directly connect to")
			->excludes(punch_opt);
		client->add_option("--target,-t", HostName, "Name of the host to connect to")
			->excludes(ip_opt);

		CLI11_PARSE(parser, argc, argv);
		if (MonitorIndecies.size() == 0) {
			MonitorIndecies.push_back(0);
		}
		if (!ServerIP.empty()) {
			HolepuncherIP = "";
		}
		IsHost = host->parsed();
		return -1;
	}
}