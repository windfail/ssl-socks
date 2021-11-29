#ifndef _GROXY_RELAY_SERVER_HPP
#define _GROXY_RELAY_SERVER_HPP
#include "relay.hpp"
#include "raw_tcp.hpp"
#include "ssl_relay.hpp"

class relay_server
    :public base_relay
{
public:
	relay_server(asio::io_context *io, const relay_config &config);
    ~relay_server();

    void local_udp_server_start();
	void local_tcp_server_start();
	void remote_server_start();

	void start_server();

	// void run();// { _io_context.run(); }

private:
    struct server_impl;
    std::unique_ptr<server_impl> _impl;


//	void handle_timer(const boost::system::error_code& err);
	void start_timer();
};

#endif
