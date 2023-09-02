#ifndef __RELAY_MANAGER_H
#define __RELAY_MANAGER_H


#include <boost/asio/io_context.hpp>
#include "relay.hpp"
#include "relay_data.hpp"

class relay_manager
	: public std::enable_shared_from_this<relay_manager>
{
public:
	explicit relay_manager(asio::io_context &io, const relay_config &);
	~relay_manager();
	void add_request(const std::shared_ptr<relay_data>);
	void add_response(const std::shared_ptr<relay_data>);

	void manager_start();

	void add_local_raw_tcp(const std::shared_ptr<raw_tcp> relay);
	void add_remote_raw_tcp(const std::shared_ptr<raw_tcp> relay, uint32_t session, const std::string &host, const std::string &service);
private:
	struct manager_impl;
	std::unique_ptr<manager_impl> _impl;
};

#endif
