#include "relay.hpp"

struct base_relay::base_impl
{
    explicit base_impl(asio::io_context &io):
        _strand(io.get_executor()), _send_timer(io, std::chrono::seconds(TIMEOUT))
    {}
    ~base_impl() = default;
    asio::strand<asio::io_context::executor_type> _strand;
    asio::steady_timer _send_timer;
    std::queue<std::shared_ptr<relay_data>> _bufs;
};
base_relay::base_relay(asio::io_context &io):
    _impl(std::make_unique<base_impl>(io))
{
}
base_relay::~base_relay() = default;

template<typename T> void base_relay::run_in_strand(T &&func)
{
    _impl->_strand.dispatch(func, asio::get_associated_allocator(func));
}

// spawn coroutin in own strand
template<typename T> void base_relay::spawn_in_strand(T &&func)
{
    asio::spawn(_impl->_strand, func);
}
void base_relay::send_data(const std::shared_ptr<relay_data> &buf)
{
    auto self(shared_from_this());
    run_in_strand([this, self, buf](){
        _impl->_bufs.push(buf);
        if (_impl->_bufs.size() == 1)
            _impl->_send_timer.expires_after(std::chrono::seconds(0));
    });
}
void base_relay::start_send()
{
    auto self(shared_from_this());
    spawn_in_strand([this, self](asio::yield_context yield){
        try {
            while (true) {
                boost::system::error_code ec;
                _impl->_send_timer.async_wait(yield[ec]);
                if (ec != asio::error::operation_aborted)
                    internal_stop_relay();
                do {
                    auto buf = _impl->_bufs.front();
                    // internal send shoud check len and throw error
                    internal_send_data(buf, yield);
                    _impl->_bufs.pop();
                } while (!_impl->_bufs.empty());
            }
        } catch (boost::system::system_error& error) {
            BOOST_LOG_TRIVIAL(error) << " relay error: "<<error.what();
            internal_stop_relay();
        }
    });
}
