#ifndef _SSL_SOCKS_SSL_RELAY_HPP
#define _SSL_SOCKS_SSL_RELAY_HPP
#include <memory>
#include <boost/asio/ssl.hpp>
#include "base_relay.hpp"
#include "relay.hpp"

namespace ssl = boost::asio::ssl;
typedef ssl::stream<tcp::socket> ssl_socket;
// ssl relay , maintain tls between local server and remote server
class ssl_relay
	:public base_relay
{
public:
	ssl_relay(asio::io_context &io, const relay_config &config);

    ~ssl_relay();

    ssl_socket & get_sock();
    void add_raw_tcp(const std::shared_ptr<raw_tcp> &relay, uint32_t session = 0, const std::string &host = "", const std::string &service="");

    // raw relay call ssl to stop raw, send to peer too
	void ssl_stop_tcp_relay(uint32_t session);
	void ssl_stop_udp_relay(uint32_t session);
    void start_relay();

	bool check_host_gfw(const std::string &host);
    void send_udp_data(const udp::endpoint &src, std::shared_ptr<relay_data> &buf);

private:
    struct ssl_impl;
    std::unique_ptr<ssl_impl> _impl;
    std::size_t internal_send_data(const std::shared_ptr<relay_data> &buf, asio::yield_context &yield);
    void internal_stop_relay();
    void internal_log(const std::string &desc, const boost::system::system_error&error=system_error(error_code()));

};

#endif
