#include <boost/format.hpp>
#include <boost/asio/spawn.hpp>
#include "raw_udp.hpp"
#include "ssl_relay.hpp"

struct raw_udp::udp_impl
{
    explicit udp_impl(asio::io_context &io):
        _sock(io), _host_resolver(io)
        // _sock_tr(io), _w_sock(&_sock), _r_sock(&_sock)
    {}

    ~udp_impl() =default;

    udp::socket _sock;
    udp::resolver _host_resolver;
    udp::endpoint _remote;

};
// remote raw udp
raw_udp::raw_udp(asio::io_context &io, server_type type, const std::string &host, const std::string &service):
    raw_relay(io, type, host, service), _impl(std::make_unique<udp_impl>(io))
{
    BOOST_LOG_TRIVIAL(debug) << "raw udp construct ";
}

raw_udp::~raw_udp()
{
    BOOST_LOG_TRIVIAL(debug) << "raw udp destruct: "<<session();
}

void raw_udp::stop_raw_relay()
{
    auto self(shared_from_this());
    run_in_strand([this, self](){
        // call close socket
        BOOST_LOG_TRIVIAL(info) << "stop raw udp";
        boost::system::error_code err;
        _impl->_sock.shutdown(tcp::socket::shutdown_both, err);
        _impl->_sock.close(err);
    });
}

void raw_udp::internal_stop_relay()
{
    BOOST_LOG_TRIVIAL(info) << "internal stop raw udp";
    stop_raw_relay();
    auto mngr = manager();
    auto buffer = std::make_shared<relay_data>(session(), relay_data::STOP_RELAY);
    mngr->send_data(buffer);
    mngr->ssl_stop_raw_relay(session());
}
static void get_data_addr(const uint8_t *data, udp::endpoint &daddr)
{
    if (data[0] == 1) {
        auto dst_addr = (struct sockaddr_in*)daddr.data();
        memcpy(&dst_addr->sin_addr, &data[1], 4);
        memcpy(&dst_addr->sin_port, &data[5], 2);
        dst_addr->sin_family = AF_INET;
    } else if (data[0] == 4) {
        auto dst_addr6 = (struct sockaddr_in6*)daddr.data();
        memcpy(&dst_addr6->sin6_addr, &data[1], 16);
        memcpy(&dst_addr6->sin6_port, &data[17], 2);
        dst_addr6->sin6_family = AF_INET6;
    }
}
std::size_t raw_udp::internal_send_data(const std::shared_ptr<relay_data> &buf, asio::yield_context &yield)
{
    uint8_t *data = (uint8_t*) buf->data_buffer().data();
    udp::endpoint re_addr;
    if (type() == LOCAL_TRANSPARENT) {
        get_data_addr(data, re_addr);
        // bind sock to re_addr
    } else {
        get_data_addr(data, _impl->_remote);
    }

    // send to _remote
    return async_write(_impl->_sock, buf->udp_data_buffer(), yield);
    // auto len = async_write(_impl->_sock, buf->data_buffer(), yield);
    // if (len != buf->size()) {
    //     auto emsg = boost::format("tcp relay len %1%, data size %2%")%len % buf->size();
    //     throw_err_msg(emsg.str());
    // }
    // BOOST_LOG_TRIVIAL(info) << "tcp send ok, "<<len;
    // return len;
}

void raw_udp::start_remote_relay()
{
    auto self(shared_from_this());
    asio::spawn(strand(), [this, self](asio::yield_context yield) {
        try {
            _impl->_sock.async_connect(_impl->_remote, yield);
            while (true) {
                auto buf = std::make_shared<relay_data>(session());
                auto len = _impl->_sock.async_receive(buf->data_buffer(), yield);
                buf->resize(len);
                auto send_on_ssl = std::bind(&ssl_relay::send_data_on_ssl, manager(), buf);
                manager()->strand().post(send_on_ssl, asio::get_associated_allocator(send_on_ssl));
            }
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
