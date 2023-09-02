#ifndef __RELAY_ACCEPTOR_HPP
#define __RELAY_ACCEPTOR_HPP

#include <boost/asio/io_context.hpp>
#include "relay.hpp"

class relay_acceptor
{
public:
	explicit relay_acceptor(asio::io_context &io, const relay_config &config);
	~relay_acceptor();
	void start_accept();

private:
	struct acceptor_impl;
	std::unique_ptr<acceptor_impl> _impl;
};


#endif
