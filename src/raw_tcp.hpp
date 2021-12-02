#ifndef _SSL_SOCKS_RAW_TCP_HPP
#define _SSL_SOCKS_RAW_TCP_HPP
#include "raw_relay.hpp"
#include "relay.hpp"

// raw tcp , for client to local server and remote server to dest
class raw_tcp
	:public raw_relay
{
public:
	raw_tcp(asio::io_context &io, server_type type, const std::string &host="", const std::string &service="");
	~raw_tcp();
	// void local_start();
	// void transparent_start();
	tcp::socket & get_sock();
	void start_relay();

    void stop_raw_relay();
private:
    struct tcp_impl;
    std::unique_ptr<tcp_impl> _impl;
    std::size_t internal_send_data(const std::shared_ptr<relay_data> &buf, asio::yield_context &yield);
    void internal_stop_relay();

	void local_relay(bool dir);
    void internal_log(boost::system::system_error&error, const std::string &desc);

};

#endif
