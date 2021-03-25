#ifndef _SSL_SOCKS_RAW_UDP_HPP
#define _SSL_SOCKS_RAW_UDP_HPP

#include "relay.hpp"
class raw_udp
	:public std::enable_shared_from_this<raw_udp>
{
public:
	raw_udp(asio::io_context *io, const std::shared_ptr<ssl_udp> &manager, const server_type &type, uint32_t session = 0 ) :
		_session (session), _strand(io->get_executor()), _sock(*io), _host_resolve(*io), _manager(manager), _type(type)
	{
		BOOST_LOG_TRIVIAL(debug) << "raw relay construct: ";
	}
	~raw_udp()
	{
		BOOST_LOG_TRIVIAL(debug) << "raw relay destruct: "<<_session;
	}
	void remote_init(const udp::endpoint &src);

	void local_start();
	void transparent_start();
	tcp::socket & get_sock() {return _sock;}
	auto session() {return _session;}
	void session(uint32_t id) { _session = id;}
	void stop_raw_udp(relay_data::stop_src);
	auto & get_strand() {
		return _strand;
	}
	void send_data_on_raw(std::shared_ptr<relay_data> buf);
	void start_data_relay();

	void start_remote_connect(std::shared_ptr<relay_data> buf);

private:
	asio::strand<asio::io_context::executor_type> _strand;
	uint32_t _session;
	server_type _type;
	udp::endpoint _dst;

	udp::socket _sock;
	udp::resolver _host_resolve;
	std::shared_ptr<ssl_udp> _manager;
	std::queue<std::shared_ptr<relay_data>> _bufs; // buffers for write
	bool _stopped = false;

	void local_relay(bool dir);

};

#endif
