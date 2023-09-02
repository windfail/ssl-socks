#ifndef _SSL_SOCKS_BASE_RELAY_HPP
#define _SSL_SOCKS_BASE_RELAY_HPP

#include <memory>
#include "relay_data.hpp"
#include "relay.hpp"

// using namespace boost::system;

class base_relay
	: public std::enable_shared_from_this<base_relay>
{
public:
	base_relay(asio::io_context &io, const relay_config &config);
	virtual ~base_relay();
	void send_data(const std::shared_ptr<relay_data> buf);
	void start_send();

	virtual void start_relay() = 0;
	virtual void stop_relay() = 0;
	relay_state_t get_state();

	void set_alive(bool);
	bool alive();
	void timeout_down();

	std::weak_ptr<relay_manager> manager;
	const relay_config &config;
	asio::io_context &io;
	asio::strand<asio::io_context::executor_type> strand;
protected:
	std::pair<std::string, std::string> remote();
	server_type type();

private:
	struct base_impl;
	std::unique_ptr<base_impl> _impl;

	// internal_send_data
	// actually send data on diferrent socket
	virtual std::size_t internal_send_data(const std::shared_ptr<relay_data> buf, asio::yield_context &yield) = 0;

	virtual void internal_log(const std::string &desc, const boost::system::system_error&error=boost::system::system_error(boost::system::error_code()));
};

#endif
