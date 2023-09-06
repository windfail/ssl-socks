#include <queue>
#include <future>
#include <boost/format.hpp>
#include "base_relay.hpp"
#include "relay_manager.hpp"

struct base_relay::base_impl
{
    explicit base_impl():
	    _timeout(TIMEOUT)
    {}

    std::queue<std::shared_ptr<relay_data>> _bufs;
	int _timeout;
};

base_relay::base_relay(asio::io_context &io, const relay_config &conf, std::shared_ptr<relay_manager> mngr):
	// _strand(io.get_executor()),
	manager(mngr),
	config(conf),
	io(io),
	strand(io.get_executor()), state(RELAY_INIT),
    _impl(std::make_unique<base_impl>())
{
}
base_relay::~base_relay() = default;

void base_relay::send_data(const std::shared_ptr<relay_data> buf)
{
    auto self(shared_from_this());
    run_in_strand(strand, [this, self, buf](){
        if (state == RELAY_STOP) {
            auto msg =boost::format("send data on stopped");
            internal_log(msg.str());
            return;
        }
        _impl->_bufs.push(buf);
        reset_timeout();
    });
}
void base_relay::start_send()
{
    auto self(shared_from_this());
    asio::spawn(strand, [this, self](asio::yield_context yield){
        try {
	        state = RELAY_START;
	        asio::steady_timer timer(io);
	        while (true) {
		        while (!_impl->_bufs.empty()) {
			        auto buf = _impl->_bufs.front();
			        _impl->_bufs.pop();
			        // internal send shoud check len and throw error
			        internal_send_data(buf, yield);
		        }
		        // add timer
		        timer.expires_after(std::chrono::milliseconds(10));
		        boost::system::error_code err;
		        timer.async_wait(yield[err]);
		        if (err == asio::error::operation_aborted) {
			        // TBD timer stoped
			        internal_log(" send timer aborted");
			        return;
		        }
		        if (state == RELAY_STOP) {
			        internal_log(" send stopped: ");

			        return;
		        }
	        }
        } catch (boost::system::system_error& error) {
            internal_log("send data:", error);
            stop_relay();
        }
    });
}
void base_relay::internal_log(const std::string &desc, const boost::system::system_error &error)
{
    BOOST_LOG_TRIVIAL(error) <<"base_relay "<< desc<<error.what();
}

void base_relay::reset_timeout()
{
	_impl->_timeout = TIMEOUT;
}
int base_relay::timeout_down()
{
	if (_impl->_timeout == 0)
		return 0;
	if (--_impl->_timeout == 0) {
		stop_relay();
	}
	return _impl->_timeout;
}
// std::pair<std::string, std::string> base_relay::remote()
// {
//     return {_impl->_host, _impl->_service};
// }

// server_type base_relay::type()
// {
//     return _impl->_type;
// }
