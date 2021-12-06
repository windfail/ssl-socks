#ifndef _SSL_SOCKS_RELAY_HPP
#define _SSL_SOCKS_RELAY_HPP
// #define BOOST_ASIO_ENABLE_HANDLER_TRACKING
#include <cstdint>
#include <boost/asio/detail/socket_option.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

namespace asio = boost::asio;

using boost::asio::ip::tcp;
using boost::asio::ip::udp;

namespace logging = boost::log;
namespace keywords = boost::log::keywords;

typedef boost::asio::detail::socket_option::boolean<SOL_IPV6, IPV6_TRANSPARENT> _ip_transparent_t;


const int RELAY_TICK = 10;
const int TIMEOUT_COUNT = 12;
const int TIMEOUT = RELAY_TICK*TIMEOUT_COUNT;

class base_relay;
class raw_relay;
class raw_tcp;
class raw_udp;

class ssl_relay;

enum server_type {
	LOCAL_SERVER,
	REMOTE_SERVER,
	LOCAL_TRANSPARENT
};

struct relay_config
{
	int local_port = 10230;
    std::string remote_port = "10230";
	std::string remote_ip = "";
	int thread_num = 1;
	server_type type = LOCAL_SERVER;
	std::string cert = "/etc/groxy_ssl/groxy_ssl.crt";
	std::string key = "/etc/groxy_ssl/groxy_ssl.pem";
	std::string logfile = "/dev/null";
	std::string gfw_file = "/etc/groxy_ssl/gfwlist";

};

inline void throw_err_msg(const std::string &msg)
{
	throw(boost::system::system_error(boost::system::error_code(), msg));
}

std::pair<std::string, std::string> parse_address(uint8_t *data, std::size_t len);
template<typename T>
std::size_t parse_endpoint(uint8_t *data, const asio::ip::basic_endpoint<T> &dst)
// std::size_t parse_endpoin(uint8_t *data, const asio::ip::basic_endpoint &dst)
{
        if (dst.address().is_v4()) {
            auto dst_addr = (struct sockaddr_in*)dst.data();
            data[0] = 1;
            memcpy(&data[1], &dst_addr->sin_addr, 4);
            memcpy(&data[5], &dst_addr->sin_port, 2);
            return 7;
        } else {
            auto dst_addr6 = (struct sockaddr_in6*)dst.data();
            data[0] = 4;
            memcpy(&data[1], &dst_addr6->sin6_addr, 16);
            memcpy(&data[17], &dst_addr6->sin6_port, 2);
            return 19;
        }
}

#endif
