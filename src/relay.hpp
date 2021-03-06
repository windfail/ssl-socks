#ifndef _GROXY_RELAY_HPP
#define _GROXY_RELAY_HPP
#include <random>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <memory>
#include <unordered_map>
#include <queue>
#include "gfwlist.hpp"
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

namespace logging = boost::log;
namespace keywords = boost::log::keywords;

namespace asio = boost::asio;
//namespace ip = boost::asio::ip;
using boost::asio::ip::tcp;
using boost::asio::ip::udp;

namespace ssl = boost::asio::ssl;
typedef ssl::stream<tcp::socket> ssl_socket;

const int READ_BUFFER_SIZE = 4096;
const int TIMEOUT = 10;
const uint8_t ADDR_TYPE_V4 = 1;
const uint8_t ADDR_TYPE_V6 = 4;
const uint8_t ADDR_TYPE_HOST = 3;

#define CONF_PREFIX "/etc/ssl-socks/"

#define DEFAULT_CRT CONF_PREFIX "server.crt"
#define DEFAULT_KEY CONF_PREFIX "key.pem"
#define DEFAULT_GFW CONF_PREFIX "gfwlist"

enum server_type {
	LOCAL_SERVER,
	REMOTE_SERVER,
	LOCAL_TRANSPARENT
};

struct relay_config
{
	int local_port = 10230;
	int remote_port = 10230;
	std::string remote_ip = "";
	int thread_num = 1;
//	bool local = true;
	server_type type = LOCAL_SERVER;
	std::string cert = DEFAULT_CRT;
	std::string key =  DEFAULT_KEY;
	std::string logfile = "/dev/null";
	std::string gfw_file =  DEFAULT_GFW;

};

ssl::context init_ssl(const relay_config &config);
class ssl_relay;
class raw_relay;

class relay_data
{

public:
	enum command {
		STOP_RELAY,
		START_CONNECT,
		START_RELAY,
		DATA_RELAY,
		KEEP_RELAY,
	};
	enum stop_src {
		from_ssl,
		from_raw,
		ssl_err
	};

	struct _header_t {
		uint32_t _session;
		command _cmd;
		std::size_t _len;
		uint8_t type;
		uint8_t dst[16];
		unsigned short port;
		_header_t(uint32_t session, command cmd, std::size_t len) :_session(session), _cmd(cmd), _len(len) {
		}
	};
	struct _udp_header_t
	{
		uint32_t _session;
		command _cmd;
		uint8_t type;
		uint8_t dst[16];
		uint8_t dport[2];
		std::size_t _len;

	};

private:
	_header_t _header;
	uint8_t _data[READ_BUFFER_SIZE];

public:
	relay_data(uint32_t session) :_header(session, DATA_RELAY, READ_BUFFER_SIZE)
	{
	}

	relay_data(uint32_t session, command cmd) : _header(session, cmd, 0)
	{
	}

	_header_t & head()
	{
		return _header;
	}
	auto session()
	{
//		auto hd = (_header_t*)&_data[0];
		return _header._session;
	}
	auto cmd()
	{
//		auto hd = (_header_t*)&_data[0];
		return _header._cmd;
	}

	auto header_buffer() {
		return asio::buffer(&_header, sizeof(_header_t));
	}
	auto data_buffer() {
		//return asio::buffer(&_data[sizeof(_header_t)], _header._len);
		return asio::buffer(_data, _header._len);
	}
	auto buffers() {
		//return asio::buffer(&_header, sizeof(_header_t)+_header._len);
		return std::array<asio::mutable_buffer, 2> { header_buffer(), data_buffer() };
//asio::buffer(&_header, sizeof(_header_t)), asio::buffer(_data)} ;
	}
	void resize(std::size_t data_len) {
		_header._len = data_len;
//		_data.resize(data_len);
	}
	auto header_size() {
		return sizeof(_header_t);
	}
	auto data_size() {
		return _header._len;
	}
	auto size() {
		return _header._len + sizeof(_header_t);
	}
private:


};


inline void throw_err_msg(const std::string &msg)
{
	throw(boost::system::system_error(boost::system::error_code(), msg));
}

#endif
