#ifndef _SSL_SOCKS_RAW_TCP_HPP
#define _SSL_SOCKS_RAW_TCP_HPP
#include "gfwlist.hpp"
#include "relay.hpp"

// raw tcp , for client to local server and remote server to dest
class raw_tcp
	:public raw_relay, std::enable_shared_from_this<raw_tcp>
{
public:
	raw_tcp(asio::io_context *io, const std::shared_ptr<ssl_relay> &manager, uint32_t session = 0) :
		raw_relay(io, manager, session), _sock(*io), _host_resolve(*io), _sock_remote(*io)
		{
			BOOST_LOG_TRIVIAL(debug) << "raw tcp construct: ";
		}
	~raw_tcp() {
		BOOST_LOG_TRIVIAL(debug) << "raw tcp destruct: "<<_session;
	}
	void local_start();
	void transparent_start();
	tcp::socket & get_sock() {return _sock;}
	void stop_this_relay();
    void start_raw_send();
	void start_data_relay();

	void start_remote_connect(std::shared_ptr<relay_data> buf);

private:

	tcp::socket _sock;
	tcp::resolver _host_resolve;
	tcp::socket _sock_remote;

	void local_relay(bool dir);

};

#endif
