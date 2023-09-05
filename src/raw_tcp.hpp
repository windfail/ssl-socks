#ifndef _SSL_SOCKS_RAW_TCP_HPP
#define _SSL_SOCKS_RAW_TCP_HPP
#include "raw_relay.hpp"
#include "relay.hpp"

// raw tcp , for client to local server and remote server to dest
class raw_tcp
	:public raw_relay
{
public:
	//
	raw_tcp(asio::io_context &io, const relay_config&config, std::shared_ptr<relay_manager> mngr);
	raw_tcp(asio::io_context &io, const relay_config&config, std::shared_ptr<relay_manager> mngr, const std::string &host, const std::string &service);

	~raw_tcp();

	tcp::socket & get_sock();
	void start_relay();
	void stop_relay();
private:
	struct tcp_impl;
	std::unique_ptr<tcp_impl> _impl;

	std::size_t internal_send_data(const std::shared_ptr<relay_data> buf, asio::yield_context &yield);
	void internal_log(const std::string &desc, const boost::system::system_error &error=boost::system::system_error(boost::system::error_code()));

};

#endif
