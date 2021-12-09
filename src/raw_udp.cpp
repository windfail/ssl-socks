#include <boost/format.hpp>
#include <boost/asio/spawn.hpp>
#include "raw_udp.hpp"
#include "ssl_relay.hpp"
#include "relay.hpp"

std::string buf_to_string(void *buf, std::size_t size);

struct raw_udp::udp_impl
{
    explicit udp_impl(raw_udp *owner, asio::io_context &io, const udp::endpoint &src):
        _owner(owner),
        _sock(io, udp::v6()),
        _remote(src)
        // , _host_resolver(io, udp::v6())
    {}
    ~udp_impl() =default;

    raw_udp *_owner;
    udp::socket _sock;
    // udp::resolver _host_resolver;
    udp::endpoint _remote;

    void impl_start_recv();
};
// remote
void raw_udp::udp_impl::impl_start_recv()
{
    auto owner(_owner->shared_from_this());
    _owner->spawn_in_strand([this, owner](asio::yield_context yield){
        try {
            while (true) {
                udp::endpoint peer;
                auto buf = std::make_shared<relay_data>();
                BOOST_LOG_TRIVIAL(info) << " raw udp recv at: "<< _sock.local_endpoint();
                auto len = _sock.async_receive_from(buf->udp_data_buffer(), peer, yield);
                parse_addr(buf->data_buffer().data(), peer.data());
                buf->session(_owner->session());
                BOOST_LOG_TRIVIAL(info) << " raw udp read len: "<< len<<" from "<<peer<<"local"<<_sock.local_endpoint();
                BOOST_LOG_TRIVIAL(info) << buf_to_string(buf->udp_data_buffer().data(), len);
                // post to manager
                buf->resize_udp(len);
                auto mngr = _owner->manager();
                mngr->send_data(buf);
            }
        } catch (boost::system::system_error& error) {
            BOOST_LOG_TRIVIAL(error) << _owner->session()<<" raw read error: "<<error.what();
            _owner->internal_stop_relay();
        }
    });
}
// remote raw udp
raw_udp::raw_udp(asio::io_context &io, server_type type, const udp::endpoint &src, const std::string &host, const std::string &service):
    raw_relay(io, type, host, service), _impl(std::make_unique<udp_impl>(this, io, src))
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
    udp::endpoint re_addr(udp::v6(), 0);
    if (type() == LOCAL_TRANSPARENT) {
        get_data_addr(data, re_addr);
        // bind sock to re_addr

        BOOST_LOG_TRIVIAL(info) <<session()<< "bind to "<< re_addr;
        _impl->_sock.bind(re_addr);
    } else {
        get_data_addr(data, _impl->_remote);
    }

    // send to _remote
    BOOST_LOG_TRIVIAL(info) << session()<<"send to "<< _impl->_remote<< "local"<<_impl->_sock.local_endpoint();
    BOOST_LOG_TRIVIAL(info) << buf_to_string(buf->udp_data_buffer().data(), buf->udp_data_buffer().size());
    auto len = _impl->_sock.async_send_to(buf->udp_data_buffer(), _impl->_remote, yield);
    if (len != buf->udp_data_size()) {
        auto emsg = boost::format("udp relay len %1%, data size %2%")%len % buf->udp_data_size();
        throw_err_msg(emsg.str());
    }
    return len;
}

void raw_udp::start_relay()
{
    auto relay_type = type();
    _impl->_sock.set_option(udp::socket::reuse_address(true));
    if (relay_type == LOCAL_TRANSPARENT) {
        _impl->_sock.set_option(_ip_transparent_t(true));
    } else if (relay_type == REMOTE_SERVER) {
        _impl->_sock.bind(udp::endpoint(udp::v6(), 0));
        _impl->impl_start_recv();
    }
    start_send();
}
