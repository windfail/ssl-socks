#include <getopt.h>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <tuple>

#include <boost/asio.hpp>
#include <memory>
#include <vector>
#include <thread>
//#include "relay.hpp"
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

#include "relay_server.hpp"

namespace logging = boost::log;
namespace keywords = boost::log::keywords;

static void init_log(const std::string &log_file)
{

	logging::add_file_log(keywords::file_name = log_file,
			      keywords::target_file_name = log_file,
			      keywords::auto_flush = true,
			      keywords::format = "[%ThreadID%][%TimeStamp%]: %Message%"                                 /*< log record format >*/);

	logging::add_common_attributes();
	logging::core::get()->set_filter (
		logging::trivial::severity >= logging::trivial::info
		);
	BOOST_LOG_TRIVIAL(trace) << "A trace severity message";
	BOOST_LOG_TRIVIAL(debug) << "A debug severity message";
	BOOST_LOG_TRIVIAL(info) << "An informational severity message";
	BOOST_LOG_TRIVIAL(warning) << "A warning severity message";
	BOOST_LOG_TRIVIAL(error) << "An error severity message";
	BOOST_LOG_TRIVIAL(fatal) << "A fatal severity message";
}

int server_start(const relay_config &config)
{
	init_log(config.logfile);

    asio::io_context io;
	relay_server server(io, config);
	server.start_server();

	std::vector<std::thread> server_th;
	for (int i = 1; i < config.thread_num; i++) {
		server_th.emplace_back([&](){ server.start_relay();});
	}
	server.start_relay();

	return 0;

}

void str_strip(std::string & src, const std::string &ch)
{
	auto start = src.find_first_not_of(ch);
	auto end = src.find_last_not_of(ch);
	if (start == std::string::npos) {
		src = "";
		return;
	}
	src = src.substr(start, end-start+1);
	return;
}
std::pair<std::string, std::string> str_split(const std::string & src, const char ch)
{
	auto pos = src.find_first_of(ch);
	if (pos == std::string::npos) {
		return {src, ""	};
	}
	return {src.substr(0, pos), src.substr(pos+1)};

}

int main(int argc, char*argv[])
{
	relay_config config;


	std::string conf_file = "/etc/groxy_ssl/groxy_ssl.conf";

	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{0,         0,                 0,  0 }
		};

		int cmd = getopt_long(argc, argv, "c:",
				long_options, &option_index);
		if (cmd == -1)
			break;

		switch (cmd) {
		case 'c': {
			conf_file = optarg;
			break;
		}


		}

	}
	std::string conf;
	std::ifstream conf_in(conf_file);

	for (std::string line; std::getline(conf_in, line); ) {
		std::string key, value;
		std::tie(key, value) = str_split(line, '#');
		std::tie(key, value) = str_split(key, '=');
		str_strip(key, " \t");
		str_strip(value, " \t");
		if (key == "thread_num") {
			config.thread_num = stoi(value);
		} else if (key == "port") {
			config.local_port = stoi(value);
		} else if (key == "type") {
			config.type =
				value == "local" ? LOCAL_SERVER :
				value == "remote" ? REMOTE_SERVER:
				LOCAL_TRANSPARENT;
		} else if (key == "server") {
			config.remote_ip = value;
		} else if (key == "server_port") {
			config.remote_port = stoi(value);
		} else if (key == "cert") {
			config.cert = value;
		} else if (key == "key") {
			config.key = value;
		} else if (key == "gfwlist") {
			config.gfw_file = value;
		} else if (key == "log") {
			config.logfile = value;
		}
	}
	server_start(config);

	return 0;

}
