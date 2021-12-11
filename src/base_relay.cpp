#include <queue>
#include <future>
#include <boost/format.hpp>
#include "base_relay.hpp"

struct base_relay::base_impl
{
    explicit base_impl(asio::io_context &io, server_type type, const std::string &host, const std::string &service):
        _host(host), _service(service), _type(type),
        _timer(io, std::chrono::seconds(TIMEOUT))
    {}
    std::string _host;
    std::string _service;
    server_type _type;

    asio::steady_timer _timer;
    // std::promise<void> _send_st;

    std::queue<std::shared_ptr<relay_data>> _bufs;
    bool _is_stop = false;
};

base_relay::base_relay(asio::io_context &io, server_type type, const std::string &host, const std::string &service):
    _impl(std::make_unique<base_impl>(io, type, host, service)), _strand(io.get_executor())
{
}
base_relay::~base_relay() = default;

void base_relay::send_data(const std::shared_ptr<relay_data> &buf)
{
    auto self(shared_from_this());
    run_in_strand([this, self, buf](){
        if (_impl->_is_stop) {
            auto msg =boost::format("send data on stopped");
            internal_log(msg.str());
            return;
        }
        _impl->_bufs.push(buf);
        // auto msg =boost::format("add data buf, size %1%")%_impl->_bufs.size();
        // internal_log(msg.str());
        // if (_impl->_bufs.size() == 1){
            // _impl->_send_st.set_value();
        auto cnt = _impl->_timer.cancel();
            // auto msg =boost::format("send data cancel timer %1%")%cnt;
            // internal_log(msg.str());
        // }
    });
}
void base_relay::start_send()
{
    auto self(shared_from_this());
    spawn_in_strand([this, self](asio::yield_context yield){
        try {
            while (true) {
                while (!_impl->_bufs.empty()) {
                    auto buf = _impl->_bufs.front();
                    // internal send shoud check len and throw error
                    internal_send_data(buf, yield);
                    _impl->_bufs.pop();
                }
                if (_impl->_is_stop) {
                    // internal_log(" stop send");
                    return;
                }
                // _impl->_send_st = std::promise<void>();
                // auto send_ready = _impl->_send_st.get_future();
                // send_ready.wait();
                // internal_log("send complete, start wait: ");
                _impl->_timer.expires_after(std::chrono::seconds(TIMEOUT));
                boost::system::error_code err;
                _impl->_timer.async_wait(yield[err]);
            }
        } catch (boost::system::system_error& error) {
            internal_log("send data:", error);
            internal_stop_relay();
        }
    });
}
void base_relay::internal_log(const std::string &desc, const boost::system::system_error &error)
{
    BOOST_LOG_TRIVIAL(error) <<"base_relay "<< desc<<error.what();
}
// void base_relay::stop_relay()
// {
//     _impl->_is_stop = true;
// }

// param stop: true means set, false means get
// only set once
bool base_relay::is_stop(bool stop)
{
    if (stop) {
        // true means stop, cancel send wait
        // if (_impl->_bufs.empty()) {
            // _impl->_send_st.set_value();
        // }
        auto cnt = _impl->_timer.cancel();
        // auto msg =boost::format("cancel timer %1%")%cnt;
        // internal_log(msg.str());
        return _impl->_is_stop = true;
    }
    return _impl->_is_stop;
}
std::pair<std::string, std::string> base_relay::remote()
{
    return {_impl->_host, _impl->_service};
}

server_type base_relay::type()
{
    return _impl->_type;
}
