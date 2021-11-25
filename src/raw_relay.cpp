#include <sstream>
#include <iomanip>
#include <boost/format.hpp>
#include <boost/asio/spawn.hpp>
#include "relay.hpp"
#include "ssl_relay.hpp"

// common functions
void raw_relay::stop_raw_relay(const relay_data::stop_src src)
{
	if (_stopped) {
		//already stopped
		return;
	}
	_stopped = true;
//	BOOST_LOG_TRIVIAL(info) << " raw relay "<<_session <<" stopped: "<< "from "<< src<< _stopped;
    stop_this_relay();
	if (src == relay_data::from_raw) {
		auto task_ssl = std::bind(&ssl_relay::stop_ssl_relay, _manager, _session, src);
		_manager->get_strand().post(task_ssl, asio::get_associated_allocator(task_ssl));
	}
}

void raw_relay::send_data_on_raw(std::shared_ptr<relay_data> buf)
{
	_bufs.push(buf);
	if (_bufs.size() > 1) {
		return;
	}
    start_raw_send();
}

