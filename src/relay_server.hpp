#ifndef _SSL_SOCKS_RELAY_SERVER_HPP
#define _SSL_SOCKS_RELAY_SERVER_HPP

#include <boost/asio/io_context.hpp>
#include "relay.hpp"

class relay_server
{
public:
	explicit relay_server(asio::io_context &io, const relay_config &config);
    ~relay_server();

    void local_udp_server_start();
	void local_tcp_server_start();
	void remote_server_start();

	void start_server();

    void server_run();

private:
    struct server_impl;
    std::unique_ptr<server_impl> _impl;

	// void start_timer();

};

#endif
