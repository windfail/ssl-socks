#ifndef _SSL_SOCKS_RAW_UDP_HPP
#define _SSL_SOCKS_RAW_UDP_HPP
#include <memory>
#include "relay.hpp"
#include "raw_relay.hpp"

// raw udp, for client to local server and remote server to dest
class raw_udp
	:public raw_relay
{
public:
	raw_udp(asio::io_context &io, server_type type, const udp::endpoint &src=udp::endpoint(udp::v6(), 0), const std::string &host="", const std::string &service="");
	~raw_udp();
	void start_relay();

	void stop_raw_relay();
	void add_peer(uint32_t session, const udp::endpoint & peer);
	void del_peer(uint32_t session);
private:
	struct udp_impl;
	std::unique_ptr<udp_impl> _impl ;

	std::size_t internal_send_data(const std::shared_ptr<relay_data> buf, asio::yield_context &yield);
	void internal_stop_relay();
	// void local_relay(bool dir);
	void internal_log(const std::string &desc, const system_error&error=system_error(error_code()));
};
#endif
