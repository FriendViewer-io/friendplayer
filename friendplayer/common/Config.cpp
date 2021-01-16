#include "Config.h"
#include <CLI/App.hpp>
#include <CLI/Formatter.hpp>
#include <CLI/Config.hpp>
#include "common/Log.h"

namespace Config {
	bool IsHost;
	int AverageBitrate;
	std::string ServerIP;
	unsigned short Port;
	int MonitorIndex;
	bool EnableTracing;

	int LoadConfig(int argc, char** argv) {
		Port = 40040;
		AverageBitrate = 2000000;
		MonitorIndex = 1;
		EnableTracing = false;
		
		CLI::App parser{ "FriendPlayer" };
		
		parser.add_flag("--trace,-t", EnableTracing, "Enable trace logging");

		CLI::App* host = parser.add_subcommand("host", "Host the FriendPlayer session");
		host->add_option("--port,-p", Port, "Port to listen on")
			->default_str("40040");
		host->add_option("--bitrate,-b", AverageBitrate, "Average bitrate for stream")
			->default_str("2000000");
		host->add_option("--monitor,-m", MonitorIndex, "Monitor index to stream")
			->default_str("1");
		CLI::App* client = parser.add_subcommand("client", "Connect to a FriendPlayer session");
		client->add_option("--port,-p", Port, "Port to connect to")
			->default_str("40040");
		client->add_option("--ip,-i", ServerIP, "IP to connect to")
			->required(true);

		CLI11_PARSE(parser, argc, argv);
		IsHost = host->parsed();
		return -1;
	}
}