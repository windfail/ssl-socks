#ifndef _SSL_SOCKS_SSL_RELAY_HPP
#define _SSL_SOCKS_SSL_RELAY_HPP
#include <random>
#include <memory>
#include <unordered_map>
#include <queue>
#include "gfwlist.hpp"
#include "relay.hpp"

// ssl relay , maintain tls between local server and remote server
class ssl_relay
	:public base_relay
{
public:
	ssl_relay(asio::io_context *io, const relay_config &config);

    ~ssl_relay();

    ssl_socket & get_sock();
    void add_raw_tcp(const std::shared_ptr<raw_tcp> &relay);
	void ssl_stop_raw_relay(uint32_t session, relay_data::stop_src src);

	void send_data_on_ssl(const std::shared_ptr<relay_data> &buf);
	void ssl_connect_start();
	void local_handle_accept(const std::shared_ptr<raw_tcp> &relay);

	void timer_handle();
	bool check_host_gfw(const std::string &host);

private:
    struct ssl_impl;
    std::unique_ptr<ssl_impl> _impl;
    std::size_t internal_send_data(const std::shared_ptr<relay_data> &buf, asio::yield_context &yield);

};

#endif
