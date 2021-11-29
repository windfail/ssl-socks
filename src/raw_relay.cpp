#include <sstream>
#include <iomanip>
#include <boost/format.hpp>
#include <boost/asio/spawn.hpp>
#include "raw_relay.hpp"
#include "ssl_relay.hpp"

struct raw_relay::raw_impl
{
    raw_impl(asio::io_context *io, const std::shared_ptr<ssl_relay> &manager, raw_relay *owner, uint32_t session = 0) :
		_session (session), _strand(io->get_executor()), _owner(owner->shared_from_this()), _manager(manager)
    {}
    ~raw_impl()=default;

    void impl_stop_relay(const relay_data::stop_src src);
    
	uint32_t _session;
	asio::strand<asio::io_context::executor_type> _strand;

    std::weak_ptr<raw_relay> _owner;
	std::shared_ptr<ssl_relay> _manager;
	std::queue<std::shared_ptr<relay_data>> _bufs; // buffers for write
	bool _stopped = false;
};

void raw_relay::raw_impl::impl_stop_relay(const relay_data::stop_src src)
{
    if (_stopped) {
        //already stopped
        return;
    }
    _stopped = true;
//	BOOST_LOG_TRIVIAL(info) << " raw relay "<<_session <<" stopped: "<< "from "<< src<< _stopped;
    // owner->stop_this_relay();
    if (src == relay_data::from_raw) {
        _manager->ssl_stop_raw_relay(_session, src);
    }
}

raw_relay::raw_relay(asio::io_context *io, const std::shared_ptr<ssl_relay> &manager, uint32_t session) :
    base_relay(io), _impl(std::make_unique<raw_relay::raw_impl> (io, manager, this, session))
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

std::shared_ptr<ssl_relay> & raw_relay::manager()
{
    return _impl->_manager;
}

// common functions
void raw_relay::stop_raw_relay(const relay_data::stop_src src)
{
    _impl->impl_stop_relay(src);
}

void raw_relay::send_data_on_raw(const std::shared_ptr<relay_data> &buf)
{
    auto self(shared_from_this());
    spawn_in_strand([this, self, buf](asio::yield_context yield){
		try {
            auto & bufs = _impl->_bufs;
            bufs.push(buf);
            if (bufs.size() > 1) {
                return;
            }
            while (!bufs.empty()) {
                auto buf = bufs.front();
                auto len = async_send_data(buf->data_buffer(), yield);
                if (len != buf->data_size()) {
                    auto emsg = boost::format(" wlen%1%, buflen%2%")%len%buf->data_size();
                    throw_err_msg(emsg.str());
                }
                bufs.pop();
            }
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "raw write error: "<<error.what();
			stop_raw_relay(relay_data::from_raw);
		}
    });
}

