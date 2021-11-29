#include <sys/types.h>
#include <sys/socket.h>
#include <sstream>
#include <iomanip>
#include <boost/format.hpp>
#include <boost/asio/spawn.hpp>
#include "raw_tcp.hpp"
#include "ssl_relay.hpp"
struct raw_tcp::tcp_impl
{
    explicit tcp_impl(asio::io_context *io):
        _sock(*io), _host_resolve(*io), _sock_remote(*io)
    {}
    ~tcp_impl() = default;

	tcp::socket _sock;
	tcp::resolver _host_resolve;
	tcp::socket _sock_remote;
	std::string local_buf;
	std::string remote_buf;
};

raw_tcp::raw_tcp(asio::io_context *io, const std::shared_ptr<ssl_relay> &manager, uint32_t session) :
    raw_relay(io, manager, session), _impl(std::make_unique<tcp_impl> (io))
{
    BOOST_LOG_TRIVIAL(debug) << "raw tcp construct: ";
}
raw_tcp::~raw_tcp()
{
    BOOST_LOG_TRIVIAL(debug) << "raw tcp destruct: "<<session();
}
tcp::socket & raw_tcp::get_sock()
{
    return _impl->_sock;
}
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
        std::size_t host_len = data[1];
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

void raw_tcp::stop_this_relay(const relay_data::stop_src src)
{
//	BOOST_LOG_TRIVIAL(info) << " raw relay "<<session() <<" stopped: "<< "from "<< src<< _stopped;
    stop_raw_relay(src);
	boost::system::error_code err;
	_impl->_sock.shutdown(tcp::socket::shutdown_both, err);
	_impl->_sock.close(err);
}

void raw_tcp::start_data_relay()
{
	auto self(shared_from_this());
	asio::spawn(strand(), [this, self](asio::yield_context yield) {
		try {
			while (true) {
				auto buf = std::make_shared<relay_data>(session());
				auto len = _impl->_sock.async_read_some(buf->data_buffer(), yield);
				//	BOOST_LOG_TRIVIAL(info) << " raw read len: "<< len;
				// post to manager
				buf->resize(len);
                manager()->send_data_on_ssl(buf);
				// auto send_on_ssl = std::bind(&ssl_relay::send_data_on_ssl, manager(), buf);
				// manager()->strand().post(send_on_ssl, asio::get_associated_allocator(send_on_ssl));
			}
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "raw read error: "<<error.what();
			stop_raw_relay(relay_data::from_raw);
		}
	});
}

// local server functions
// for non-blocked hosts in socks5 mode, directly relay
// dir: true - read from remote / send to local client
//      false - read from local client / send to remote
void raw_tcp::local_relay(bool dir)
{
	auto self(shared_from_this());
	asio::spawn(strand(), [this, self, dir](asio::yield_context yield) {
		try {
			auto sock_r = &_impl->_sock;
			auto sock_w = &_impl->_sock_remote;
			auto buf = &_impl->local_buf;
			if (dir) {
				sock_r = &_impl->_sock_remote;
				sock_w = &_impl->_sock;
				buf = &_impl->remote_buf;
			}
			while (true) {
				buf->resize(READ_BUFFER_SIZE);
				auto len = sock_r->async_read_some(asio::buffer(*buf), yield);
				buf->resize(len);
				len = async_write(*sock_w, asio::buffer(*buf), yield);
				if (len != buf->size()) {
					auto emsg = boost::format(" wlen%1%, buflen%2%")%len%buf->size();
					throw_err_msg(emsg.str());
				}
			}
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "local relay error: "<<error.what();
			stop_raw_relay(relay_data::from_raw);
		}
	});
}

extern "C"
{
    extern int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
    int get_dst_addr(int sock, struct sockaddr_storage *ss, socklen_t *len);
}
// local transparent proxy start
void raw_tcp::transparent_start()
{
    try {
	// send start cmd to ssl
        auto buffer = std::make_shared<relay_data>(session(), relay_data::START_TCP);
	auto data = (uint8_t*) buffer->data_buffer().data();
    // struct sockaddr_storage ss;
    // socklen_t len = sizeof(ss);
	// get origin dest
    //  proxy for dnat
	// int ret = get_dst_addr(_sock.native_handle(), ss, len);
    // proxy for tproxy
    auto dst = _impl->_sock.local_endpoint();
    if (dst.address().is_v4()) {
        auto dst_addr = (struct sockaddr_in*)dst.data();
        data[0] = 1;
        memcpy(&data[1], &dst_addr->sin_addr, 4);
		memcpy(&data[5], &dst_addr->sin_port, 2);
        buffer->resize(7);
    } else {
        auto dst_addr6 = (struct sockaddr_in6*)dst.data();
		data[0] = 4;
		memcpy(&data[1], &dst_addr6->sin6_addr, 16);
		memcpy(&data[17], &dst_addr6->sin6_port, 2);
        buffer->resize(19);
    }
    // int ret = getsockname(_sock.native_handle(), (struct sockaddr*)&ss, &len);
    // if (ret < 0) {
	// 	stop_raw_relay(relay_data::from_raw);
	// 	return;
	// }
    // if (ss.ss_family == AF_INET) {
    //     auto dst_addr = (struct sockaddr_in*)&ss;
    //     data[0] = 1;
    //     memcpy(&data[1], &dst_addr->sin_addr, 4);
	// 	memcpy(&data[5], &dst_addr->sin_port, 2);
    //     buffer->resize(7);
    // } else if (ss.ss_family == AF_INET6) {
    //     auto dst_addr6 = (struct sockaddr_in6*)&ss;
	// 	data[0] = 4;
	// 	memcpy(&data[1], &dst_addr6->sin6_addr, 16);
	// 	memcpy(&data[17], &dst_addr6->sin6_port, 2);
    //     buffer->resize(19);
    // }

//	BOOST_LOG_TRIVIAL(info) << " send start remote data: \n" << buf_to_string(buffer->data_buffer().data(), buffer->data_buffer().size());
    manager()->send_data_on_ssl(buffer);
	// auto send_on_ssl = std::bind(&ssl_relay::send_data_on_ssl, manager(), buffer);
	// manager()->strand().post(send_on_ssl, asio::get_associated_allocator(send_on_ssl));
    start_data_relay();
    } catch (boost::system::system_error& error) {
        BOOST_LOG_TRIVIAL(error) << "local start error: "<<error.what();
        stop_raw_relay(relay_data::from_raw);
    }
}

// local socks5 proxy start
void raw_tcp::local_start()
{
	auto self(shared_from_this());
	asio::spawn(strand(), [this, self](asio::yield_context yield) {
		try {
			std::vector<uint8_t> buf(512,0);
			auto len = _impl->_sock.async_receive(asio::buffer(buf), yield);
			BOOST_LOG_TRIVIAL(info) << "local start rec len " << len <<" data " << buf[0] << buf[1] <<buf[2] ;
			buf[0] = 5, buf[1] = 0;
			len = async_write(_impl->_sock, asio::buffer(buf, 2), yield);

			if (len != 2) {
				auto emsg = boost::format("write 0x5 0x0, len %1%")%len;
				throw_err_msg(emsg.str());
			}
			BOOST_LOG_TRIVIAL(info) << "local start writeback "  ;
// get sock5 connect cmd
			len = _impl->_sock.async_read_some(asio::buffer(buf), yield);
			BOOST_LOG_TRIVIAL(info) << "local read cmd "  ;
			if (len < 6 || buf[1] != 1 ) {
				auto emsg = boost::format("addr get len %1%, cmd %2%")%len %buf[1];
				throw_err_msg(emsg.str());
			}
			auto data = (uint8_t*) & buf[3];
			auto [host, port] = parse_address(data, len-3);
			BOOST_LOG_TRIVIAL(info) << "resolve host " <<host <<" port "<<port ;
			bool block = manager()->check_host_gfw(host);

			if (block) {
				// send start cmd to ssl
				auto buffer = std::make_shared<relay_data>(session(), relay_data::START_TCP);
				std::copy_n(data, len -3, (uint8_t*)buffer->data_buffer().data());
//	BOOST_LOG_TRIVIAL(info) << " send start remote data: \n" << buf_to_string(buffer->data_buffer().data(), buffer->data_buffer().size());
				buffer->resize(len -3);
                manager()->send_data_on_ssl(buffer);
				// auto send_on_ssl = std::bind(&ssl_relay::send_data_on_ssl, manager(), buffer);
				// manager()->strand().post(send_on_ssl, asio::get_associated_allocator(send_on_ssl));
			} else {
				auto re_hosts = _impl->_host_resolve.async_resolve(host, port, yield);
				asio::async_connect(_impl->_sock_remote, re_hosts, yield);
				local_relay(true);
				local_relay(false);
			}
			// send sock5 ok back
			//BOOST_LOG_TRIVIAL(info) << " send sock5 ok back : ";
			buf =  {5, 0, 0, 1, 0, 0, 0, 0, 0, 0};
			async_write(_impl->_sock, asio::buffer(buf), yield);

            start_data_relay();
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "local start error: "<<error.what();
			stop_raw_relay(relay_data::from_raw);
		}
	});
}

void raw_tcp::start_remote_connect(std::shared_ptr<relay_data> buf)
{
	auto self(shared_from_this());
	asio::spawn(strand(), [this, self, buf](asio::yield_context yield) {
		try {
			auto data = (uint8_t*) buf->data_buffer().data();
			auto len = buf->data_size();
			auto[host, port] = parse_address(data, len);
			auto re_hosts = _impl->_host_resolve.async_resolve(host, port, yield);
			asio::async_connect(_impl->_sock, re_hosts, yield);

			// on remote connect
			// auto buffer = std::make_shared<relay_data>(session(), relay_data::START_RELAY);
			// auto start_task = std::bind(&ssl_relay::send_data_on_ssl, manager(), buffer);
			// manager()->strand().post(start_task, asio::get_associated_allocator(start_task));

			// start raw data relay
			start_data_relay();

		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "start remote error: "<<error.what();
			stop_raw_relay(relay_data::from_raw);
		}
	});
}
void raw_tcp::send_data_on_raw(const std::shared_ptr<relay_data> &buf)
{
    auto bufs = buffers();
    bufs.push_back(buf);
    
}
void raw_tcp::start_raw_send(data_t &bufs)
{
	auto self(shared_from_this());
	asio::spawn(strand(), [this, self, bufs](asio::yield_context yield) {
		try {
            while (!bufs.empty()) {
                auto buf = bufs.front();
                auto len = async_write(_impl->_sock, buf->data_buffer(), yield);
                if (len != buf->data_size()) {
                    auto emsg = boost::format(" wlen%1%, buflen%2%")%len%buf->data_size();
                    throw_err_msg(emsg.str());
                }
                bufs.pop();
            }
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "raw write error: "<<error.what();
			stop_raw_relay(relay_data::from_raw);
		}
	});
}
