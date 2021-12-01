#ifndef _GROXY_RELAY_SERVER_HPP
#define _GROXY_RELAY_SERVER_HPP
#include "relay.hpp"
#include "base_relay.hpp"

class relay_server
    :public base_relay
{
public:
	explicit relay_server(asio::io_context &io, const relay_config &config);
    ~relay_server();

    void local_udp_server_start();
	void local_tcp_server_start();
	void remote_server_start();

	void start_server();

    void start_relay();

private:
    struct server_impl;
    std::unique_ptr<server_impl> _impl;

	void start_timer();

    std::size_t internal_send_data(const std::shared_ptr<relay_data> &buf, asio::yield_context &yield) = 0;
    void internal_stop_relay()=0;
};

#endif
