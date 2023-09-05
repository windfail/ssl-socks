#include <boost/asio/spawn.hpp>
#include "relay_acceptor.hpp"
#include "relay_manager.hpp"
#include "raw_tcp.hpp"
#include "ssl_relay.hpp"

using namespace std;

struct relay_acceptor::acceptor_impl
{
	acceptor_impl(asio::io_context &io, const relay_config &config):
		_config(config), _io(io),
		_acceptor(io, tcp::v6()),
		_udp_acceptor(io, udp::v6()),
		_strand(io.get_executor())
	{
		try {
			_acceptor.set_option(tcp::acceptor::reuse_address(true));
			_acceptor.set_option(tcp::acceptor::keep_alive(true));

			_udp_acceptor.set_option(udp::socket::reuse_address(true));
			_udp_acceptor.set_option(udp::socket::keep_alive(true));

			if (config.type == LOCAL_TRANSPARENT) {
				_acceptor.set_option(_ip_transparent_t(true));

				_udp_acceptor.set_option(_ip_transparent_t(true));
				_udp_acceptor.set_option(asio::detail::socket_option::boolean<SOL_IPV6, IPV6_RECVORIGDSTADDR> (true));
				_udp_acceptor.set_option(asio::detail::socket_option::boolean<SOL_IP, IP_RECVORIGDSTADDR> (true));
			}
			_acceptor.bind(tcp::endpoint(tcp::v6(), config.local_port));
			_acceptor.listen();

			_udp_acceptor.bind(udp::endpoint(udp::v6(), config.local_port));
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "relay server init error: "<<error.what();
		}
	}
	~acceptor_impl() = default;

	const relay_config &_config;
	asio::io_context &_io;
	tcp::acceptor _acceptor;
	udp::socket _udp_acceptor;

	asio::strand<asio::io_context::executor_type> _strand;

	shared_ptr<relay_manager> _manager;
	void local_accept();
	void remote_accept();
	void local_udp_accept();
};

relay_acceptor::relay_acceptor(asio::io_context &io, const relay_config &config):
	_impl(std::make_unique<acceptor_impl>(io, config))
{
}
relay_acceptor::~relay_acceptor() = default;

void relay_acceptor::acceptor_impl::remote_accept()
{
	asio::spawn(_strand, [this](asio::yield_context yield) {
		while (true) {
			try {
				auto ssl_ptr = std::make_shared<ssl_relay> (_io, _config, _manager);
				_acceptor.async_accept(ssl_ptr->get_sock().lowest_layer(), yield);
				_manager->manager_start();
				ssl_ptr->start_relay();
				_manager = std::make_shared<relay_manager>(_io, _config);
			} catch (boost::system::system_error& error) {
				BOOST_LOG_TRIVIAL(error) << "remote accept error: "<<error.what();
				throw error;
			}
		}
	});


}
void relay_acceptor::acceptor_impl::local_accept()
{
	asio::spawn(_strand, [this](asio::yield_context yield) {
		while (true) {
			try {
				auto new_relay = make_shared<raw_tcp> (_io, _config, _manager);
				_acceptor.async_accept(new_relay->get_sock(), yield);
				if (_manager == nullptr) {
					// TBD throw error
				}
				BOOST_LOG_TRIVIAL(info) << "relay_server : add new tcp";
				_manager->add_local_raw_tcp(new_relay);
			} catch (boost::system::system_error& error) {
				BOOST_LOG_TRIVIAL(error) << "local accept error: "<<error.what();
				throw error;
			}
		}
	});
}


// local udp server
static std::pair<std::shared_ptr<relay_data>, udp::endpoint> transparent_udp_recv_on(udp::socket &u_sock)
{
	udp::endpoint src(udp::v6(), 0);
	auto buf = std::make_shared<relay_data>();

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
	auto len = recvmsg(u_sock.native_handle(), &msg, 0);
	buf->resize_udp(len);
	// struct cmsghdr *cmsg;

	for (auto cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if ((cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_RECVORIGDSTADDR)
			||(cmsg->cmsg_level == SOL_IPV6 && cmsg->cmsg_type == IPV6_RECVORIGDSTADDR)) {
			parse_addr(buf->data_buffer().data(), CMSG_DATA(cmsg));
			// auto [host, port] = parse_address(buf->data_buffer().data(), 19);
			// BOOST_LOG_TRIVIAL(info) << " udp recv dst "<<host<<port;
		//     memcpy(dstaddr, CMSG_DATA(cmsg), sizeof(struct sockaddr_in));
		//     dstaddr->ss_family = AF_INET;
		//     return 0;
		// } else if (cmsg->cmsg_level == SOL_IPV6 && cmsg->cmsg_type == IPV6_RECVORIGDSTADDR) {
		//     memcpy(dstaddr, CMSG_DATA(cmsg), sizeof(struct sockaddr_in6));
		//     dstaddr->ss_family = AF_INET6;
		//     return 0;
		}
	}
	return {buf, src};


}

void relay_acceptor::acceptor_impl::local_udp_accept()
{

	asio::spawn(_strand, [this](asio::yield_context yield) {
		while (true) {
			try {
				_udp_acceptor.async_wait(udp::socket::wait_read, yield);
				// BOOST_LOG_TRIVIAL(error) << "local start recv udp msg ";
				// recvmsg
				auto [buffer, src_addr] = transparent_udp_recv_on(_udp_acceptor);
				_manager->send_udp_data(src_addr, buffer);
			} catch (boost::system::system_error& error) {
				BOOST_LOG_TRIVIAL(error) << "local udp  error: "<<error.what();
				throw error;
			}
		}
	});
}
void relay_acceptor::start_accept()
{
	_impl->_manager = std::make_shared<relay_manager>(_impl->_io, _impl->_config);
	if (_impl->_config.type == REMOTE_SERVER) {
		_impl->remote_accept();
	} else {//if (_impl->_config.type == LOCAL_TRANSPARENT) {
		_impl->_manager->manager_start();
		_impl->local_udp_accept();
		_impl->local_accept();
	// } else if (_impl->_config.type == LOCAL_SERVER){
		// _impl->local_accept();
	}
}
