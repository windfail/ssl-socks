#ifndef _SSL_SOCKS_SSL_TCP_HPP
#define _SSL_SOCKS_SSL_TCP_HPP

#include "relay.hpp"

// ssl relay , maintain tls between local server and remote server
class ssl_relay
	:public std::enable_shared_from_this<ssl_relay>
{
public:
	ssl_relay(asio::io_context *io, const relay_config &config) :
		_io_context(io), _strand(io->get_executor()), _ctx(init_ssl(config)),
		//_sock(std::make_unique<ssl_socket>(*io, _ctx)),
		_sock(*io, _ctx),
//		_acceptor(*io()),//, tcp::endpoint(tcp::v4(), config.local_port)),
		_remote(asio::ip::make_address(config.remote_ip), config.remote_port),
		_rand(std::random_device()()),
		_config(config), _gfw(config.gfw_file)
	{

		BOOST_LOG_TRIVIAL(info) << "ssl relay construct";
	}

	~ssl_relay()  {
		BOOST_LOG_TRIVIAL(info) << "ssl relay destruct";
	};

	auto & get_strand() {
		return _strand;
	}
	void stop_ssl_relay(uint32_t session, relay_data::stop_src src);
	void send_data_on_ssl(std::shared_ptr<relay_data> buf);

	ssl_socket & get_sock() {return _sock;}

	void ssl_connect_start();
	void local_handle_accept(std::shared_ptr<raw_relay> relay);

	void timer_handle();
	bool check_host_gfw(const std::string &host)
		{
			return _gfw.is_blocked(host);
		}

private:
	class _relay_t;
	asio::io_context *_io_context;
	enum {
		NOT_START,
		SSL_CONNECT,
		SSL_START,
		SSL_CLOSED
	} _ssl_status = NOT_START;
	relay_config _config;

	ssl::context _ctx;
	asio::strand<asio::io_context::executor_type> _strand;

	//std::shared_ptr<ssl_socket>  _sock;
	ssl_socket _sock;

	std::unordered_map<uint32_t, std::shared_ptr<_relay_t>> _relays;

	tcp::endpoint _remote;	// remote ssl relay ep
	std::queue<std::shared_ptr<relay_data>> _bufs; // buffers for write
	gfw_list _gfw;

	// random
	std::minstd_rand _rand;

	int _timeout_rd = TIMEOUT;
	int _timeout_wr = TIMEOUT;
	int _timeout_kp = TIMEOUT;

	void ssl_data_send();
	void ssl_data_read();

	void do_ssl_data(std::shared_ptr<relay_data>& buf);
	uint32_t add_new_relay(const std::shared_ptr<raw_relay> &relay);
	void on_ssl_shutdown(const boost::system::error_code& error);

};
#endif
