#include <queue>
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
    std::queue<std::shared_ptr<relay_data>> _bufs;
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
        _impl->_bufs.push(buf);
        BOOST_LOG_TRIVIAL(info) << "relay add buffer : " << _impl->_bufs.size();
        if (_impl->_bufs.size() == 1)
            _impl->_timer.expires_after(std::chrono::seconds(0));
    });
}
void base_relay::start_send()
{
    auto self(shared_from_this());
    spawn_in_strand([this, self](asio::yield_context yield){
        try {
            while (true) {
                boost::system::error_code ec;
                _impl->_timer.async_wait(yield[ec]);
                if (ec != asio::error::operation_aborted) {
                    BOOST_LOG_TRIVIAL(error) << "relay send timeout: ";
                    // internal_stop_relay();
                    // return;
                }
                while (!_impl->_bufs.empty()) {
                    auto buf = _impl->_bufs.front();
                    // internal send shoud check len and throw error
                    auto len = internal_send_data(buf, yield);
                    if (len != buf->size()) {
                        auto emsg = boost::format("send len %1%, data size %2%")%len % buf->size();
                        throw_err_msg(emsg.str());
                    }
                    _impl->_bufs.pop();
                }
                refresh_timer(TIMEOUT);
            }
        } catch (boost::system::system_error& error) {
            internal_log(error, "send data:");
            internal_stop_relay();
        }
    });
}
void base_relay::internal_log(boost::system::system_error&error, const std::string &desc)
{
    BOOST_LOG_TRIVIAL(error) <<"base_relay "<< desc<<error.what();
}
void base_relay::refresh_timer(int timeout)
{
    _impl->_timer.expires_after(std::chrono::seconds(timeout));
}
std::pair<std::string, std::string> base_relay::remote()
{
    return {_impl->_host, _impl->_service};
}

server_type base_relay::type()
{
    return _impl->_type;
}
