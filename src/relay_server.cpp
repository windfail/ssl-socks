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
            }
            _acceptor.bind(tcp::endpoint(tcp::v6(), config.local_port));
            _u_sock.bind(udp::endpoint(udp::v6(), config.local_port));
            _acceptor.listen();
        } catch (boost::system::system_error& error) {
            BOOST_LOG_TRIVIAL(error) << "relay server init error: "<<error.what();
        }
	}
    ~server_impl() = default;

    void impl_add_new_tcp(const std::shared_ptr<raw_tcp> &new_tcp);
    void impl_udp_recv(std::shared_ptr<relay_data> &buf, udp::endpoint &src);

    relay_config _config;
    asio::io_context &_io;
    tcp::acceptor _acceptor;
    udp::socket  _u_sock;
    asio::strand<asio::io_context::executor_type> _strand;

    std::weak_ptr<ssl_relay> _ssl_tcp;
    std::weak_ptr<ssl_relay> _ssl_udp;
};

// add new tcp relay to ssl relay
// if no ssl relay, start new ssl connection
void relay_server::server_impl::impl_add_new_tcp(const std::shared_ptr<raw_tcp> &new_tcp)
{
    auto ssl_ptr = _ssl_tcp.lock();
    if (ssl_ptr == nullptr) {
        ssl_ptr = std::make_shared<ssl_relay> (_io, _config);
        _ssl_tcp = ssl_ptr;
        // init and connect to remote
        // BOOST_LOG_TRIVIAL(info) << "relay_server :ssl start";
        ssl_ptr->start_relay();
    }
    ssl_ptr->add_raw_tcp(new_tcp);
}
relay_server::relay_server(asio::io_context &io, const relay_config &config):
    _impl(std::make_unique<server_impl>(io, config))
{
}
relay_server::~relay_server() = default;

// local udp server
void relay_server::server_impl::impl_udp_recv(std::shared_ptr<relay_data> &buf, udp::endpoint &src)
{
    uint8_t ctrl_msg[128];
    // struct sockaddr_storage src;
    struct msghdr msg;
    msg.msg_name = src.data();
    msg.msg_namelen = src.size(),
    msg.msg_control = ctrl_msg;
    msg.msg_controllen = sizeof(ctrl_msg);
    struct iovec iobuf = {
        buf->udp_data_buffer().data(), buf->udp_data_size()
    };
    msg.msg_iov = &iobuf;
    msg.msg_iovlen = 1;
    recvmsg(_u_sock.native_handle(), &msg, 0);
    // struct cmsghdr *cmsg;

    for (auto cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        if ((cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_RECVORIGDSTADDR)
            ||(cmsg->cmsg_level == SOL_IPV6 && cmsg->cmsg_type == IPV6_RECVORIGDSTADDR)) {
            parse_addr(buf->data_buffer().data(), CMSG_DATA(cmsg));
        //     memcpy(dstaddr, CMSG_DATA(cmsg), sizeof(struct sockaddr_in));
        //     dstaddr->ss_family = AF_INET;
        //     return 0;
        // } else if (cmsg->cmsg_level == SOL_IPV6 && cmsg->cmsg_type == IPV6_RECVORIGDSTADDR) {
        //     memcpy(dstaddr, CMSG_DATA(cmsg), sizeof(struct sockaddr_in6));
        //     dstaddr->ss_family = AF_INET6;
        //     return 0;
        }
    }


}
void relay_server::local_udp_server_start()
{
    // auto new_relay = std::make_shared<raw_udp> (_impl->_io, nullptr);
	asio::spawn(_impl->_strand, [this](asio::yield_context yield) {
		// auto ssl_ptr = std::make_shared<ssl_relay> (&_io_context, _config);
		// _ssl_udp_relays.emplace_back(ssl_ptr);
		while (true) {
			try {
                _impl->_u_sock.async_wait(udp::socket::wait_read, yield);
                auto buffer = std::make_shared<relay_data>();
                udp::endpoint src_addr;
                // recvmsg
                _impl->impl_udp_recv(buffer, src_addr);
                auto ssl_ptr = _impl->_ssl_udp.lock();
                if (ssl_ptr == nullptr) {
                    ssl_ptr = std::make_shared<ssl_relay> (_impl->_io, _impl->_config);
                    _impl->_ssl_udp = ssl_ptr;
                    ssl_ptr->start_relay();
                }
                // ssl_ptr->relay_udp(buffer);
			} catch (boost::system::system_error& error) {
				BOOST_LOG_TRIVIAL(error) << "local accept error: "<<error.what();
			}
		}
	});

}

// local tcp server
void relay_server::local_tcp_server_start()
{
    asio::spawn(_impl->_strand, [this](asio::yield_context yield) {
		while (true) {
			try {
				auto new_relay = std::make_shared<raw_tcp> (_impl->_io, _impl->_config.type);
				_impl->_acceptor.async_accept(new_relay->get_sock(), yield);
                _impl->impl_add_new_tcp(new_relay);
			} catch (boost::system::system_error& error) {
				BOOST_LOG_TRIVIAL(error) << "local accept error: "<<error.what();
			}
		}
	});

}

void relay_server::remote_server_start()
{
    asio::spawn(_impl->_strand, [this](asio::yield_context yield) {
		while (true) {
			try {
				auto ssl_ptr = std::make_shared<ssl_relay> (_impl->_io, _impl->_config);
				_impl->_acceptor.async_accept(ssl_ptr->get_sock().lowest_layer(), yield);
                ssl_ptr->start_relay();
			} catch (boost::system::system_error& error) {
				BOOST_LOG_TRIVIAL(error) << "remote accept error: "<<error.what();
			}
		}
	});

}
void relay_server::start_server()
{
	if (_impl->_config.type == REMOTE_SERVER) {
		remote_server_start();
	} else {
		local_tcp_server_start();
        // local_udp_server_start();
	}
}
void relay_server::server_run()
{
	try {
		_impl->_io.run();
	} catch (std::exception & e) {
		BOOST_LOG_TRIVIAL(error) << "server run error: "<<e.what();
	} catch (...) {
		BOOST_LOG_TRIVIAL(error) << "server run error with unkown exception ";
	}
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
