#ifndef _SSL_SOCKS_RAW_TCP_HPP
#define _SSL_SOCKS_RAW_TCP_HPP

#include "relay.hpp"
class raw_relay
	:public std::enable_shared_from_this<raw_relay>
{
public:
	raw_relay(asio::io_context *io, const std::shared_ptr<ssl_relay> &manager, uint32_t session = 0) :
		_session (session), _strand(io->get_executor()), _sock(*io), _host_resolve(*io), _manager(manager), _sock_remote(*io)
		{
			BOOST_LOG_TRIVIAL(debug) << "raw relay construct: ";
		}
	~raw_relay() {
		BOOST_LOG_TRIVIAL(debug) << "raw relay destruct: "<<_session;
	}
	void local_start();
	void transparent_start();
	tcp::socket & get_sock() {return _sock;}
	auto session() {return _session;}
	void session(uint32_t id) { _session = id;}
	void stop_raw_relay(relay_data::stop_src);
	auto & get_strand() {
		return _strand;
	}
	void send_data_on_raw(std::shared_ptr<relay_data> buf);
	void start_data_relay();

	void start_remote_connect(std::shared_ptr<relay_data> buf);

private:
	asio::strand<asio::io_context::executor_type> _strand;
	uint32_t _session;

	tcp::socket _sock;
	tcp::resolver _host_resolve;
	std::shared_ptr<ssl_relay> _manager;
	std::queue<std::shared_ptr<relay_data>> _bufs; // buffers for write
	bool _stopped = false;
	tcp::socket _sock_remote;
	std::string local_buf;
	std::string remote_buf;

	void local_relay(bool dir);

};

#endif
