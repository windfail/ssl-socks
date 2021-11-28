#ifndef _SSL_SOCKS_RAW_TCP_HPP
#define _SSL_SOCKS_RAW_TCP_HPP
#include "relay.hpp"

// raw tcp , for client to local server and remote server to dest
class raw_tcp
	:public raw_relay
{
public:
	raw_tcp(asio::io_context *io, const std::shared_ptr<ssl_relay> &manager, uint32_t session = 0);
	~raw_tcp();
	void local_start();
	void transparent_start();
	tcp::socket & get_sock();
	void stop_this_relay();
    void start_raw_send(std::shared_ptr<relay_data> buf);
	void start_data_relay();

	void start_remote_connect(std::shared_ptr<relay_data> buf);

private:
    struct tcp_impl;
    std::unique_ptr<tcp_impl> _impl;

	void local_relay(bool dir);

};

#endif
