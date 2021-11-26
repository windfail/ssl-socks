#include <sstream>
#include <iomanip>
#include <boost/format.hpp>
#include <boost/asio/spawn.hpp>
#include "relay.hpp"
#include "ssl_relay.hpp"

struct raw_relay::raw_impl
{
    raw_impl(asio::io_context *io, const std::shared_ptr<ssl_relay> &manager, uint32_t session = 0) :
		_session (session), _strand(io->get_executor()), _manager(manager)
    {}
    ~raw_impl()=default;

	uint32_t _session;
	asio::strand<asio::io_context::executor_type> _strand;

	std::shared_ptr<ssl_relay> _manager;
	std::queue<std::shared_ptr<relay_data>> _bufs; // buffers for write
	bool _stopped = false;
};

raw_relay::raw_relay(asio::io_context *io, const std::shared_ptr<ssl_relay> &manager, uint32_t session) :
    _impl(std::make_unique<raw_relay::raw_impl> (io, manager, session))
{}

raw_relay::~raw_relay() = default;

uint32_t raw_relay::session()
{
    return _impl->_session;
}
void raw_relay::session(uint32_t id)
{
    _impl->_session = id;
}
asio::strand<asio::io_context::executor_type> & raw_relay::strand()
{
    return _impl->_strand;
}

std::shared_ptr<ssl_relay> & raw_relay::manager()
{
    return _impl->_manager;
}

// common functions
void raw_relay::stop_raw_relay(const relay_data::stop_src src)
{
	if (_impl->_stopped) {
		//already stopped
		return;
	}
	_impl->_stopped = true;
//	BOOST_LOG_TRIVIAL(info) << " raw relay "<<_session <<" stopped: "<< "from "<< src<< _stopped;
    stop_this_relay();
	if (src == relay_data::from_raw) {
		auto task_ssl = std::bind(&ssl_relay::stop_raw_relay, _impl->_manager, _impl->_session, src);
		_impl->_manager->strand().post(task_ssl, asio::get_associated_allocator(task_ssl));
	}
}

void raw_relay::send_data_on_raw(std::shared_ptr<relay_data> buf)
{
	_impl->_bufs.push(buf);
	if (_impl->_bufs.size() > 1) {
		return;
	}
    start_raw_send(_impl->_bufs.front());
}

void raw_relay::send_next_data()
{
    _impl->_bufs.pop();
    if (_impl->_bufs.empty()) return;
    start_raw_send(_impl->_bufs.front());
}
