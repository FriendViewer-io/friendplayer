#include "Config.h"
#include <CLI/App.hpp>
#include <CLI/Formatter.hpp>
#include <CLI/Config.hpp>

namespace Config {
	bool IsHost;
	int AverageBitrate;
	std::string ServerIP;
	std::string HolepuncherIP;
	std::string Identifier;
	std::string HostIdentifier;
	unsigned short Port;
	std::vector<int> MonitorIndecies;
	bool EnableTracing;
	bool SaveControllers;

	int LoadConfig(int argc, char** argv) {
		Port = 40040;
		AverageBitrate = 2000000;
		EnableTracing = false;
		SaveControllers = false;
		HolepuncherIP = "198.199.81.165";
		
		CLI::App parser{ "FriendPlayer" };
		
		CLI::AsNumberWithUnit bitrate_validator(std::map<std::string, int>{{"b", 1}, {"kb", 1000}, {"mb", 1000 * 1000}});
		bitrate_validator.description("(b, kb, mb)");

		parser.add_flag("--trace,-T", EnableTracing, "Enable trace logging");

		CLI::App* host = parser.add_subcommand("host", "Host the FriendPlayer session using a holepunching server");
		CLI::Option* punch_opt = host->add_option("--ip,-i", HolepuncherIP, "IP to connect to for hole-punching")
			->default_str("198.199.81.165");
		CLI::Option* name_opt = host->add_option("--name,-n", Identifier, "Name to connect to the holepuncher under")
			->required(true);
		punch_opt->needs(name_opt);
		host->add_option("--port,-p", Port, "Port to connect to on the holepunching server")
			->default_str("40040");
		host->add_option("--bitrate,-b", AverageBitrate, "Average bitrate for stream")
			->default_str("2mb")
			->transform(bitrate_validator);
		host->add_option("--monitor,-m", MonitorIndecies, "Monitor index to stream")
			->default_str("0");
		host->add_flag("--save-controllers,-s", SaveControllers, "Reuse controllers after disconnections")
			->default_str("false");
		

		CLI::App* host_direct = parser.add_subcommand("dhost", "Host the FriendPlayer session in direct connection mode");
		host_direct->add_option("--port,-p", Port, "Port to listen on for incoming connections")
			->default_str("40040");
		host_direct->add_option("--bitrate,-b", AverageBitrate, "Average bitrate for stream")
			->default_str("2mb")
			->transform(bitrate_validator);
		host_direct->add_option("--monitor,-m", MonitorIndecies, "Monitor index to stream")
			->default_str("0");
		host_direct->add_flag("--save-controllers,-s", SaveControllers, "Reuse controllers after disconnections")
			->default_str("false");
		
		CLI::App* client = parser.add_subcommand("client", "Connect to a FriendPlayer session using server");
		punch_opt = client->add_option("--ip,-i", HolepuncherIP, "IP to connect to for hole-punching")
			->default_str("198.199.81.165");
		name_opt = client->add_option("--name,-n", Identifier, "Name to connect to the holepuncher under")
			->required(true);
		punch_opt->needs(name_opt);
		client->add_option("--port,-p", Port, "Port to connect to for holepunching")
			->default_str("40040");
		client->add_option("--target,-t", HostIdentifier, "Name of the host to connect to");

		CLI::App* client_direct = parser.add_subcommand("dclient", "Connect to a FriendPlayer session directly");
		client_direct->add_option("--port,-p", Port, "Port to directly connect to")
			->default_str("40040");
		client_direct->add_option("--name,-n", Identifier, "Name to connect under")
			->required(true);
		client_direct->add_option("--ip,-i", ServerIP, "IP to directly connect to")
			->excludes(punch_opt);

		parser.require_subcommand(1);

		CLI11_PARSE(parser, argc, argv);
		if (MonitorIndecies.size() == 0) {
			MonitorIndecies.push_back(0);
		}
		if (client_direct->parsed() || host_direct->parsed()) {
			HolepuncherIP = "";
		}
		IsHost = host->parsed() || host_direct->parsed();
		
		return -1;
	}
}