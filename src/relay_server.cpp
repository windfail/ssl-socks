#include <boost/asio/spawn.hpp>
#include "ssl_relay.hpp"
#include "raw_tcp.hpp"
#include "raw_udp.hpp"
#include "relay_server.hpp"

struct relay_server::server_impl
{
    server_impl(asio::io_context &io, const relay_config &config):
        _config(config), _io(io),
        _acceptor(io, tcp::v6()),
        _u_sock(io, udp::v6()),
        _strand(io.get_executor())
    {
        try {
            _acceptor.set_option(tcp::acceptor::reuse_address(true));
            _acceptor.set_option(tcp::acceptor::keep_alive(true));
            _u_sock.set_option(udp::socket::reuse_address(true));
            _u_sock.set_option(udp::socket::keep_alive(true));
            if (config.type == LOCAL_TRANSPARENT) {
                _acceptor.set_option(_ip_transparent_t(true));
                _u_sock.set_option(_ip_transparent_t(true));
                _u_sock.set_option(asio::detail::socket_option::boolean<SOL_IPV6, IPV6_RECVORIGDSTADDR> (true));
                _u_sock.set_option(asio::detail::socket_option::boolean<SOL_IP, IP_RECVORIGDSTADDR> (true));
            }
            _acceptor.bind(tcp::endpoint(tcp::v6(), config.local_port));
            _u_sock.bind(udp::endpoint(udp::v6(), config.local_port));
            _acceptor.listen();
        } catch (boost::system::system_error& error) {
            BOOST_LOG_TRIVIAL(error) << "relay server init error: "<<error.what();
        }
	}
    ~server_impl() = default;

    void impl_add_new_tcp(const std::shared_ptr<raw_tcp> new_tcp);
    void impl_udp_recv(std::shared_ptr<relay_data> buf, udp::endpoint &src);

    relay_config _config;
    asio::io_context &_io;
    tcp::acceptor _acceptor;
    udp::socket  _u_sock;
    asio::strand<asio::io_context::executor_type> _strand;

    // std::weak_ptr<ssl_relay> _ssl_tcp;
    // std::weak_ptr<ssl_relay> _ssl_udp;
    std::weak_ptr<ssl_relay> _ssl;
};

relay_server::relay_server(asio::io_context &io, const relay_config &config):
    _impl(std::make_unique<server_impl>(io, config))
{
}
relay_server::~relay_server() = default;

void relay_server::local_udp_server_start()
{
}




std::size_t parse_addr(void *pdata, void*addr)
{
    auto dst_addr = (sockaddr_in*)addr;
    auto data=(uint8_t*)pdata;
    if (dst_addr->sin_family == AF_INET) {
        data[0] = 1;
        memcpy(&data[1], &dst_addr->sin_addr, 4);
        memcpy(&data[5], &dst_addr->sin_port, 2);
        return 7;
    } else {
        auto dst_addr6 = (struct sockaddr_in6*)addr;
        data[0] = 4;
        memcpy(&data[1], &dst_addr6->sin6_addr, 16);
        memcpy(&data[17], &dst_addr6->sin6_port, 2);
        return 19;
    }
}
