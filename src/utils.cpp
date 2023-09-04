#include <sstream>
#include <iomanip>
#include <boost/format.hpp>
#include "relay.hpp"

std::string buf_to_string(void *buf, std::size_t size)
{
	std::ostringstream out;
	out << std::setfill('0') << std::setw(2) << std::hex;

	for (std::size_t i =0; i< size; i++ ) {
		unsigned int a = ((uint8_t*)buf)[i];
		out << a << ' ';
		if ((i+1) % 32 == 0) out << '\n';
	}
	return out.str();
}
std::size_t parse_addr(void *pdata, void*addr)
{
	auto dst_addr = (sockaddr_in*)addr;
	auto data=(uint8_t*)pdata;
	if (dst_addr->sin_family == AF_INET) {
		data[0] = 1;
		memcpy(&data[1], &dst_addr->sin_addr, 4);
		memcpy(&data[5], &dst_addr->sin_port, 2);
		return 7;
	} else {
		auto dst_addr6 = (struct sockaddr_in6*)addr;
		data[0] = 4;
		memcpy(&data[1], &dst_addr6->sin6_addr, 16);
		memcpy(&data[17], &dst_addr6->sin6_port, 2);
		return 19;
	}
}

std::pair<std::string, std::string> parse_address(void *buf, std::size_t len)
{
	std::string host;
	std::string port_name;
	uint8_t * port;
	auto data = (uint8_t*) buf;
	auto cmd = data[0];

	if (cmd == 1) {
		auto addr_4 = (asio::ip::address_v4::bytes_type *)&data[1];
		if (len < sizeof(*addr_4) + 3) {
			auto emsg = boost::format(" sock5 addr4 len error: %1%")%len;
			throw_err_msg(emsg.str());
		}
		host = asio::ip::make_address_v4(*addr_4).to_string();
		port = (uint8_t*)&addr_4[1];
	} else if (cmd == 4) {
		auto addr_6 = (asio::ip::address_v6::bytes_type *)&data[1];
		if (len < sizeof(*addr_6) + 3) {
			auto emsg = boost::format(" sock5 addr6 len error: %1%")%len;
			throw_err_msg(emsg.str());
		}
		host = asio::ip::make_address_v6(*addr_6).to_string();
		port = (uint8_t*)&addr_6[1];
	} else if (cmd == 3) {
		std::size_t host_len = data[1];
		if ( len < host_len +4) {
			auto emsg = boost::format(" sock5 host name len error: %1%, hostlen%2%")%len%host_len;
			throw_err_msg(emsg.str());
		}
		host.append((char*)&data[2], host_len);
		port = &data[host_len+2];
	} else {
		auto emsg = boost::format("sock5 cmd %1% not support")%cmd;
		throw(boost::system::system_error(
				  boost::system::error_code(),
				  emsg.str()));
	}
	port_name = boost::str(boost::format("%1%")%(port[0]<<8 | port[1]));
	return {host, port_name};

}
