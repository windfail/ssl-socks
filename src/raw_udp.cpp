#include <boost/format.hpp>
#include <boost/asio/spawn.hpp>
#include "raw_udp.hpp"
#include "ssl_relay.hpp"

struct raw_udp::udp_impl
{
    explicit udp_impl(asio::io_context *io):
        _sock(*io), _host_resolve(*io), _sock_tr(*io), _w_sock(&_sock), _r_sock(&_sock), _dst(&_remote)
    {}
    udp_impl(asio::io_context *io, const udp::endpoint &remote):
        _sock(*io), _host_resolve(*io), _sock_tr(*io), _remote(remote), _w_sock(&_sock), _r_sock(&_sock), _dst(&_remote)
    {}

    ~udp_impl() =default;

    udp::socket _sock;
    udp::resolver _host_resolve;
    udp::socket _sock_tr; // for transparent send
    udp::endpoint _remote;
    udp::socket *_w_sock;
    udp::socket *_r_sock;
    udp::endpoint *_dst;

    std::map<std::pair<udp::endpoint, udp::endpoint>, uint32_t> _addrs;
    std::unordered_map<uint32_t, std::pair<udp::endpoint, udp::endpoint>*> _sess_addrs;

};
// remote raw udp
raw_udp::raw_udp(asio::io_context *io, const std::shared_ptr<ssl_relay> &manager, uint32_t session, const udp::endpoint &remote) :
    raw_relay(io, manager, session), _impl(std::make_unique<udp_impl>(io, remote))
{
    BOOST_LOG_TRIVIAL(debug) << "raw udp construct in remote: " << session;
}
raw_udp::raw_udp(asio::io_context *io, const std::shared_ptr<ssl_relay> &manager) :
    raw_relay(io, manager), _impl(std::make_unique<udp_impl>(io))
{
    BOOST_LOG_TRIVIAL(debug) << "raw udp construct in local tproxy: ";
    _impl->_w_sock = &_impl->_sock_tr;
}

raw_udp::~raw_udp()
{
    BOOST_LOG_TRIVIAL(debug) << "raw udp destruct: "<<session();
}
void raw_udp::stop_this_relay()
{

}
void raw_udp::start_remote_relay()
{
    auto self(shared_from_this());
    asio::spawn(strand(), [this, self](asio::yield_context yield) {
        try {
            _impl->_sock.async_connect(_impl->_remote);

            if (session() == 0) {
                // for tproxy rebind to remote dst
                auto dst = _impl->_sess_addrs.find(buf->session());
                if (dst == _impl->_sess_addrs.end()) {
                    return ;
                }
                _impl->_dst = & dst->second->first;
                auto &src = dst->second->second;
            }
            auto len = _impl->_w_sock->async_send_to(buf->data_buffer(), *_impl->_dst, yield);
            if (len != buf->data_size()) {
                auto emsg = boost::format(" wlen%1%, buflen%2%")%len%buf->data_size();
                throw_err_msg(emsg.str());
            }
            send_next_data();
        } catch (boost::system::system_error& error) {
            BOOST_LOG_TRIVIAL(error) << "raw write error: "<<error.what();
            stop_raw_relay(relay_data::from_raw);
        }
    });

}
void raw_udp::start_raw_send(std::shared_ptr<relay_data> buf)
{
    auto self(shared_from_this());
    asio::spawn(strand(), [this, self, buf](asio::yield_context yield) {
        try {
            if (session() == 0) {
                // for tproxy rebind to remote dst
                auto dst = _impl->_sess_addrs.find(buf->session());
                if (dst == _impl->_sess_addrs.end()) {
                    return ;
                }
                _impl->_dst = & dst->second->first;
                auto &src = dst->second->second;
            }
            auto len = _impl->_w_sock->async_send_to(buf->data_buffer(), *_impl->_dst, yield);
            if (len != buf->data_size()) {
                auto emsg = boost::format(" wlen%1%, buflen%2%")%len%buf->data_size();
                throw_err_msg(emsg.str());
            }
            send_next_data();
        } catch (boost::system::system_error& error) {
            BOOST_LOG_TRIVIAL(error) << "raw write error: "<<error.what();
            stop_raw_relay(relay_data::from_raw);
        }
    });

}
