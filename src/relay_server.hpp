#ifndef _GROXY_RELAY_SERVER_HPP
#define _GROXY_RELAY_SERVER_HPP
#include "relay.hpp"

class relay_server
{
public:
	relay_server(const relay_config &config):
		_io_context(),// _ctx(ssl::context::tlsv12_client),
		_config(config), _strand(_io_context.get_executor()),
		_acceptor(_io_context, tcp::endpoint(tcp::v4(), config.local_port)),
		_remote(asio::ip::make_address(config.remote_ip), config.remote_port),_timer(_io_context) {
	}

	void local_server_start();
	void remote_server_start();

	void start_server();

	void run();// { _io_context.run(); }

private:
	relay_config _config;

	asio::io_context _io_context;
	tcp::acceptor _acceptor;
	asio::strand<asio::io_context::executor_type> _strand;
//	ssl::context _ctx;
	tcp::endpoint _remote;
	asio::steady_timer _timer;

	std::list<std::weak_ptr<ssl_relay>> _ssl_relays;

//	void handle_timer(const boost::system::error_code& err);
	void start_timer();
};

#endif
