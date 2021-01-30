#include "Config.h"
#include <CLI/App.hpp>
#include <CLI/Formatter.hpp>
#include <CLI/Config.hpp>

namespace Config {
	bool IsHost;
	int AverageBitrate;
	std::string ServerIP;
	unsigned short Port;
	std::vector<int> MonitorIndecies;
	bool EnableTracing;
	bool SaveControllers;

	int LoadConfig(int argc, char** argv) {
		Port = 40040;
		AverageBitrate = 2000000;
		EnableTracing = false;
		SaveControllers = false;
		
		CLI::App parser{ "FriendPlayer" };
		
		parser.add_flag("--trace,-t", EnableTracing, "Enable trace logging");

		CLI::App* host = parser.add_subcommand("host", "Host the FriendPlayer session");
		host->add_option("--port,-p", Port, "Port to listen on")
			->default_str("40040");
		host->add_option("--bitrate,-b", AverageBitrate, "Average bitrate for stream")
			->default_str("2000000");
		host->add_option("--monitor,-m", MonitorIndecies, "Monitor index to stream")
			->default_str("1");
		host->add_flag("--save-controllers,-s", SaveControllers, "Reuse controllers after disconnections")
			->default_str("false");
		
		CLI::App* client = parser.add_subcommand("client", "Connect to a FriendPlayer session");
		client->add_option("--port,-p", Port, "Port to connect to")
			->default_str("40040");
		client->add_option("--ip,-i", ServerIP, "IP to connect to")
			->required(true);

		CLI11_PARSE(parser, argc, argv);
		if (MonitorIndecies.size() == 0) {
			MonitorIndecies.push_back(0);
		}
		IsHost = host->parsed();
		return -1;
	}
}