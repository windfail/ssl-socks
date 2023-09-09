#include <getopt.h>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <tuple>
#include <string>
#include <boost/format.hpp>
#include <memory>
#include <vector>
#include <thread>
// #include <boost/json/src.hpp>
// using namespace boost::json;
//#include <nlohmann/json.hpp>
#include "json.hpp"
using json = nlohmann::json;

#include "relay.hpp"
// #include <boost/log/core.hpp>
// #include <boost/log/trivial.hpp>
// #include <boost/log/utility/setup/file.hpp>
// #include <boost/log/utility/setup/common_attributes.hpp>

#include "relay_acceptor.hpp"

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

    while (true){
        asio::io_context io;
        relay_acceptor server(io, config);
        std::vector<std::thread> server_th;
        try {
            server.start_accept();

            BOOST_LOG_TRIVIAL(info) << "main  start thread";
            for (int i = 0; i < config.thread_num; i++) {
                server_th.emplace_back([&](){ io.run();});
            }
            io.run();
        } catch (std::exception & e) {
            io.stop();
            for (auto && th: server_th) {
                BOOST_LOG_TRIVIAL(error) << "main :join thread ";
                th.join();
            }
            BOOST_LOG_TRIVIAL(error) << "main :server run error: "<<e.what();
	        sleep(1);
        } catch (...) {
            io.stop();
            BOOST_LOG_TRIVIAL(error) << "main ;server run error with unkown exception ";
	        sleep(1);
        }
    }

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
relay_config get_config(json &jconf)
{
    relay_config config;
    config.local_port = jconf["port"].get<int>();
    config.thread_num = jconf["thread_num"].get<int>();
    std::string type = jconf["type"].get<std::string>();
    if (type == "local") {
        config.type = LOCAL_SERVER;
    } else if (type == "tproxy") {
        config.type = LOCAL_TRANSPARENT;
    } else if (type == "remote") {
        config.type = REMOTE_SERVER;
    }
    if (config.type != REMOTE_SERVER) {
        int r_port = jconf["server_port"].get<int>();
        config.remote_port = (boost::format("%1%")%r_port).str();
        config.remote_ip = jconf["server"].get<std::string>();
        config.gfw_file = jconf.value("gfwlist", "/etc/ssl-socks/gfwlist");
        config.gfw.load_list(config.gfw_file);
    }
    config.cert = jconf["cert"].get<std::string>();
    config.key = jconf["key"].get<std::string>();
    config.logfile = jconf["log"].get<std::string>();

    return config;
}
// relay_config get_config(object &jconf)
// {
//     relay_config config;
//     config.local_port = jconf["port"].as_int64();
//     int r_port = jconf["server_port"].as_int64();
//     config.remote_port = (boost::format("%1%")%r_port).str();
//     config.remote_ip = jconf["server"].as_string().c_str();
//     config.thread_num = jconf["thread_num"].as_int64();
//     string type = jconf["type"].as_string();
//     if (type == "local") {
//         config.type = LOCAL_SERVER;
//     } else if (type == "tproxy") {
//         config.type = LOCAL_TRANSPARENT;
//     } else if (type == "remote") {
//         config.type = REMOTE_SERVER;
//     }
//     config.cert = jconf["cert"].as_string().c_str();
//     config.key = jconf["key"].as_string().c_str();
//     config.logfile = jconf["log"].as_string().c_str();
//     config.gfw_file = jconf["gfwlist"].as_string().c_str();

//     return config;
// }

int main(int argc, char*argv[])
{

	std::string conf_file = "/etc/ssl-socks/sock_ssl.conf";

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
	std::ifstream conf_in{conf_file};
    json conf = json::parse(conf_in, nullptr, true, true);
    // conf_in>>conf;
    relay_config config = get_config(conf);
    server_start(config);

	// if (std::ifstream conf_in{conf_file, std::ios::ate}){
    //     auto size = conf_in.tellg();
    //     std::string conf_str(size, 0);
    //     conf_in.seekg(0);
    //     conf_in.read(&conf_str[0], size);
    //     monotonic_resource mr;
    //     parse_options opt;
    //     opt.allow_comments = true;
    //     opt.allow_trailing_commas = true;
    //     auto conf=parse(conf_str, &mr, opt);
    //     relay_config config = get_config(conf.as_object());
    //     server_start(config);
    // }


	return 0;

}
