#include "raw_udp.hpp"
#include "ssl_udp.hpp"
#include <sstream>
#include <iomanip>
#include <boost/format.hpp>
#include <boost/asio/spawn.hpp>


std::string buf_to_string(void *buf, std::size_t size)
{
	std::ostringstream out;
	out << std::setfill('0') << std::setw(2) << std::hex;

	for (std::size_t i =0; i< size; i++ ) {
		unsigned int a = ((uint8_t*)buf)[i];
		out << a << ' ';
		if ((i+1) % 32 == 0) out << '\n';
	}
	return out.str();
}
static std::pair<std::string, std::string> parse_address(uint8_t *data, std::size_t len)
{
	std::string host;
	std::string port_name;
	uint8_t * port;
	auto cmd = data[0];

	if (cmd == 1) {
		auto addr_4 = (asio::ip::address_v4::bytes_type *)&data[1];
		if (len < sizeof(*addr_4) + 3) {
			auto emsg = boost::format(" sock5 addr4 len error: %1%")%len;
			throw_err_msg(emsg.str());
		}
		host = asio::ip::make_address_v4(*addr_4).to_string();
		port = (uint8_t*)&addr_4[1];
	} else if (cmd == 4) {
		auto addr_6 = (asio::ip::address_v6::bytes_type *)&data[1];
		if (len < sizeof(*addr_6) + 3) {
			auto emsg = boost::format(" sock5 addr6 len error: %1%")%len;
			throw_err_msg(emsg.str());
		}
		host = asio::ip::make_address_v6(*addr_6).to_string();
		port = (uint8_t*)&addr_6[1];
	} else if (cmd == 3) {
		int host_len = data[1];
		if ( len < host_len +4) {
			auto emsg = boost::format(" sock5 host name len error: %1%, hostlen%2%")%len%host_len;
			throw_err_msg(emsg.str());
		}
		host.append((char*)&data[2], host_len);
		port = &data[host_len+2];
	} else {
		auto emsg = boost::format("sock5 cmd %1% not support")%cmd;
		throw(boost::system::system_error(
			      boost::system::error_code(),
			      emsg.str()));
	}
	port_name = boost::str(boost::format("%1%")%(port[0]<<8 | port[1]));
	return {host, port_name};

}
void raw_udp::remote_init(const udp::endpoint::protocol_type &type)
{
	boost::system::error_code ec;
	_sock.bind(udp::endpoint(type, 0), ec);
	if (ec) {
		stop_raw_udp(relay_data::from_raw);
	}
}
// common functions
void raw_udp::stop_raw_udp(const relay_data::stop_src src)
{
	if (_stopped) {
		//already stopped
		return;
	}
	_stopped = true;
//	BOOST_LOG_TRIVIAL(info) << " raw relay "<<_session <<" stopped: "<< "from "<< src<< _stopped;
	boost::system::error_code err;
	_sock.shutdown(udp::socket::shutdown_both, err);
	_sock.close(err);
	if (src == relay_data::from_raw) {
		auto task_ssl = std::bind(&ssl_udp::stop_ssl_udp, _manager, _session, src);
		_manager->get_strand().post(task_ssl, asio::get_associated_allocator(task_ssl));
	}
}

// send data on raw, udp transparent
// remote: send to remote dst
void raw_udp::send_data_on_raw(std::shared_ptr<relay_data> buf)
{
	// _bufs.push(buf);
	// if (_bufs.size() > 1) {
	//	return;
	// }
	auto self(shared_from_this());
	asio::spawn(_strand, [this, buf, self](asio::yield_context yield) {
		try {
//			while (!_bufs.empty()) {
			// if (head.type == ADDR_TYPE_V4) { // local: bind to ipv4, dst dport
			//	auto addr_4 = (asio::ip::address_v4::bytes_type *)&head.dst[0];
			//	udp::endpoint sender(asio::ip::make_address_v4(*addr_4), head.port);
			//	_sock.bind(sender);
			// } else if (head.type == ADDR_TYPE_V6) {
			//	auto addr_6 = (asio::ip::address_v6::bytes_type *)&head.dst[0];
			//	udp::endpoint sender(asio::ip::make_address_v6(*addr_6), head.port);
			//	_sock.bind(sender);
			// }
			auto head = buf->head();
			udp::endpoint dst;
			dst.port(head.port);
			if (head.type == ADDR_TYPE_V4) { // local: bind to ipv4, dst dport
				auto addr_4 = (asio::ip::address_v4::bytes_type *)&head.dst[0];
				dst.address(asio::ip::make_address_v4(*addr_4));
			} else if (head.type == ADDR_TYPE_V6) {
				auto addr_6 = (asio::ip::address_v6::bytes_type *)&head.dst[0];
				dst.address(asio::ip::make_address_v6(*addr_6));
			}
			auto len = _sock.async_send(buf->data_buffer(), yield);
			if (len != buf->data_size()) {
				auto emsg = boost::format(" udp send len%1%, buflen%2%")%len%buf->data_size();
				throw_err_msg(emsg.str());
			}
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "raw write error: "<<error.what();
			stop_raw_udp(relay_data::from_raw);
		}
	});
}

static int
get_dstaddr(struct msghdr *msg, struct sockaddr_storage *dstaddr)
{
	struct cmsghdr *cmsg;
	struct sockaddr_in;
	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
		if (cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_RECVORIGDSTADDR) {
			memcpy(dstaddr, CMSG_DATA(cmsg), sizeof(struct sockaddr_in));
			dstaddr->ss_family = AF_INET;

		} else if (cmsg->cmsg_level == SOL_IPV6 && cmsg->cmsg_type == IPV6_RECVORIGDSTADDR) {
			memcpy(dstaddr, CMSG_DATA(cmsg), sizeof(struct sockaddr_in6));
			dstaddr->ss_family = AF_INET6;

		}
	}

}
// only in local, start recv, udp transparent
void raw_udp::start_local_recv()
{
// use c to recev, get orig dst and src;
    struct sockaddr_storage src_addr;
    memset(&src_addr, 0, sizeof(struct sockaddr_storage));
    char control_buffer[64] = { 0 };
    struct msghdr msg;
    memset(&msg, 0, sizeof(struct msghdr));
    struct iovec iov[1];
    struct sockaddr_storage dst_addr;
    memset(&dst_addr, 0, sizeof(struct sockaddr_storage));

    msg.msg_name       = &src_addr;
    msg.msg_namelen    = src_addr_len;
    msg.msg_control    = control_buffer;
    msg.msg_controllen = sizeof(control_buffer);

    iov[0].iov_base = buf->data;
    iov[0].iov_len  = buf_size;
    msg.msg_iov     = iov;
    msg.msg_iovlen  = 1;

    buf->len = recvmsg(server_ctx->fd, &msg, 0);
    if (buf->len == -1) {
	ERROR("[udp] server_recvmsg");
	goto CLEAN_UP;
    } else if (buf->len > packet_size) {
	if (verbose) {
	    LOGI("[udp] UDP server_recv_recvmsg fragmentation, MTU at least be: " SSIZE_FMT,
		 buf->len + PACKET_HEADER_SIZE);
	}
    }

}
// only in remote, start recv, udp transparent
void raw_udp::start_data_relay()
{
	auto self(shared_from_this());
	asio::spawn(_strand, [this, self](asio::yield_context yield) {
		try {
			while (true) {
				auto buf = std::make_shared<relay_data>(_session);
				udp::endpoint sender;
				auto len = _sock.async_receive_from(buf->data_buffer(), sender, yield);
				//	BOOST_LOG_TRIVIAL(info) << " raw read len: "<< len;
				// post to manager
				buf->resize(len);
				auto head = buf->head();
				auto src = sender.address();
				if (src.is_v6()) {
					head.type = ADDR_TYPE_V6;
					auto bytes = src.to_v6().to_bytes();
					std::copy(bytes.begin(), bytes.end(), head.dst.begin());
				} else if (src.is_v4()) {
					head.type = ADDR_TYEP_V4;
					auto bytes = src.to_v4().to_bytes();
					std::copy(bytes.begin(), bytes.end(), head.dst.begin());
				}
				head.port = sender.port();

				auto send_on_ssl = std::bind(&ssl_udp::send_data_on_ssl, _manager, buf);
				_manager->get_strand().post(send_on_ssl, asio::get_associated_allocator(send_on_ssl));
			}
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "raw read error: "<<error.what();
			stop_raw_udp(relay_data::from_raw);
		}
	});
}
void raw_udp::dst_init(const udp::endpoint &dst)
{
	_sock.async_connect(dst);
}
extern "C" {
	int get_dst_addr(int sock, uint8_t *data);
}
void raw_udp::transparent_start()
{
	// send start cmd to ssl
	auto buffer = std::make_shared<relay_data>(_session, relay_data::START_CONNECT);
	auto data = (uint8_t*) buffer->data_buffer().data();
	// get origin dest
	int len = get_dst_addr(_sock.native_handle(), data);
	if (len < 0) {
		stop_raw_udp(relay_data::from_raw);
		return;
	}
	buffer->resize(len);

//	BOOST_LOG_TRIVIAL(info) << " send start remote data: \n" << buf_to_string(buffer->data_buffer().data(), buffer->data_buffer().size());
	auto send_on_ssl = std::bind(&ssl_udp::send_data_on_ssl, _manager, buffer);
	_manager->get_strand().post(send_on_ssl, asio::get_associated_allocator(send_on_ssl));
}

void raw_udp::start_remote_connect(std::shared_ptr<relay_data> buf)
{
	auto self(shared_from_this());
	asio::spawn(_strand, [this, self, buf](asio::yield_context yield) {
		try {
			auto data = (uint8_t*) buf->data_buffer().data();
			auto len = buf->data_size();
			auto[host, port] = parse_address(data, len);
			auto re_hosts = _host_resolve.async_resolve(host, port, yield);
			asio::async_connect(_sock, re_hosts, yield);

			// on remote connect
			auto buffer = std::make_shared<relay_data>(_session, relay_data::START_RELAY);
			auto start_task = std::bind(&ssl_udp::send_data_on_ssl, _manager, buffer);
			_manager->get_strand().post(start_task, asio::get_associated_allocator(start_task));

			// start raw data relay
			start_data_relay();

		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "start remote error: "<<error.what();
			stop_raw_udp(relay_data::from_raw);
		}
	});
}
