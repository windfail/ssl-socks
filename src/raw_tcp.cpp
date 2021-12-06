#include <sys/types.h>
#include <sys/socket.h>
#include <sstream>
#include <iomanip>
#include <boost/format.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/connect.hpp>
#include "raw_tcp.hpp"
#include "ssl_relay.hpp"
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
std::pair<std::string, std::string> parse_address(uint8_t *data, std::size_t len)
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

struct raw_tcp::tcp_impl
{
    explicit tcp_impl(raw_tcp *owner, asio::io_context &io) :
        _owner(owner), _sock(io),
        // _type(type),
        _host_resolver(io), _sock_remote(io)
    {}

    raw_tcp *_owner;
	tcp::socket _sock;
    // server_type _type;
	tcp::resolver _host_resolver;
	tcp::socket _sock_remote;
	std::string local_buf;
	std::string remote_buf;

    void impl_start_read();
    void impl_start_local();
    void impl_start_remote();
    void impl_start_transparent();
    void impl_local_relay(bool dir);
};
void raw_tcp::tcp_impl::impl_start_read()
{
	auto owner(_owner->shared_from_this());

    _owner->spawn_in_strand([this, owner](asio::yield_context yield) {
		try {
			while (true) {
				auto buf = std::make_shared<relay_data>(_owner->session());
				auto len = _sock.async_read_some(buf->data_buffer(), yield);
				//	BOOST_LOG_TRIVIAL(info) << " raw read len: "<< len;
				// post to manager
				buf->resize(len);
                auto mngr = _owner->manager();
                mngr->send_data(buf);
			}
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << _owner->session()<<" raw read error: "<<error.what();
            _owner->internal_stop_relay();
		}
	});
}

raw_tcp::raw_tcp(asio::io_context &io, server_type type, const std::string &host, const std::string &service) :
    raw_relay(io, type, host, service), _impl(std::make_unique<tcp_impl> (this, io))
{
    BOOST_LOG_TRIVIAL(info) << "raw tcp construct: ";
}
raw_tcp::~raw_tcp()
{
    BOOST_LOG_TRIVIAL(info) << "raw tcp destruct: "<<session();
}
tcp::socket & raw_tcp::get_sock()
{
    return _impl->_sock;
}

void raw_tcp::stop_raw_relay()
{
    auto self(shared_from_this());
    run_in_strand([this, self](){
        if (is_stop())
            return;
        is_stop(true);
        // call close socket
        // BOOST_LOG_TRIVIAL(info) << "raw_tcp: stop raw tcp"<< session();
        boost::system::error_code err;
        _impl->_sock.shutdown(tcp::socket::shutdown_both, err);
        _impl->_sock.close(err);
    });
}

void raw_tcp::internal_stop_relay()
{
    if (is_stop())
        return;
	// BOOST_LOG_TRIVIAL(info) << "internal stop raw tcp"<<session();
    stop_raw_relay();
    auto mngr = manager();
    auto buffer = std::make_shared<relay_data>(session(), relay_data::STOP_RELAY);
    mngr->send_data(buffer);
    mngr->ssl_stop_raw_relay(session());
}

std::size_t raw_tcp::internal_send_data(const std::shared_ptr<relay_data> &buf, asio::yield_context &yield)
{
    // return async_write(_impl->_sock, buf->data_buffer(), yield);
    auto len = async_write(_impl->_sock, buf->data_buffer(), yield);
    if (len != buf->data_size()) {
        auto emsg = boost::format("tcp %d relay len %1%, data size %2%")%session()%len % buf->size();
        throw_err_msg(emsg.str());
    }
    // BOOST_LOG_TRIVIAL(info) << "tcp send ok, "<<len;
    return len;
}


// local server functions
// for non-blocked hosts in socks5 mode, directly relay
// dir: true - read from remote / send to local client
//      false - read from local client / send to remote
void raw_tcp::tcp_impl::impl_local_relay(bool dir)
{
	auto owner(_owner->shared_from_this());
    _owner->spawn_in_strand([this, owner, dir](asio::yield_context yield) {
		try {
			auto sock_r = &_sock;
			auto sock_w = &_sock_remote;
			auto buf = &local_buf;
			if (dir) {
				sock_r = &_sock_remote;
				sock_w = &_sock;
				buf = &remote_buf;
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
            _owner->internal_log("local relay:", error);
            _owner->internal_stop_relay();
		}
	});
}

extern "C"
{
    extern int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
    int get_dst_addr(int sock, struct sockaddr_storage *ss, socklen_t *len);
}

// local transparent proxy start
void raw_tcp::tcp_impl::impl_start_transparent()
{
    try {
	// send start cmd to ssl
        auto buffer = std::make_shared<relay_data>(_owner->session(), relay_data::START_TCP);
        auto data = (uint8_t*) buffer->data_buffer().data();
        // struct sockaddr_storage ss;
        // socklen_t len = sizeof(ss);
        // get origin dest
        //  proxy for dnat
        // int ret = get_dst_addr(_sock.native_handle(), ss, len);
        // proxy for tproxy
        auto dst = _sock.local_endpoint();

        buffer->resize(parse_endpoint(data, dst));
        // BOOST_LOG_TRIVIAL(info) << "tproxy start tcp "<<dst  ;
        // int ret = getsockname(_sock.native_handle(), (struct sockaddr*)&ss, &len);
        // if (ret < 0) {
		// stop_raw_relay(relay_data::from_raw);
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
        auto mngr = _owner-> manager();
        mngr->send_data(buffer);
        impl_start_read();
        _owner->start_send();
    } catch (boost::system::system_error& error) {
        _owner->internal_log("tproxy start:", error);
        _owner->internal_stop_relay();
    }
}

// local socks5 proxy start
void raw_tcp::tcp_impl::impl_start_local()
{
	auto owner(_owner->shared_from_this());
    _owner->spawn_in_strand([this, owner](asio::yield_context yield) {
		try {
			std::vector<uint8_t> buf(512,0);
			auto len = _sock.async_receive(asio::buffer(buf), yield);
			// BOOST_LOG_TRIVIAL(info) << "local start rec len " << len <<" data " << buf[0] << buf[1] <<buf[2] ;
			buf[0] = 5, buf[1] = 0;
			len = async_write(_sock, asio::buffer(buf, 2), yield);

			if (len != 2) {
				auto emsg = boost::format("write 0x5 0x0, len %1%")%len;
				throw_err_msg(emsg.str());
			}
			// BOOST_LOG_TRIVIAL(info) << "local start writeback "  ;
// get sock5 connect cmd
			len = _sock.async_read_some(asio::buffer(buf), yield);
			// BOOST_LOG_TRIVIAL(info) << "local read cmd "  ;
			if (len < 6 || buf[1] != 1 ) {
				auto emsg = boost::format("addr get len %1%, cmd %2%")%len %buf[1];
				throw_err_msg(emsg.str());
			}
			auto data = (uint8_t*) & buf[3];
			auto [host, port] = parse_address(data, len-3);
			BOOST_LOG_TRIVIAL(info) << "resolve host " <<host <<" port "<<port ;
            auto mngr = _owner->manager();
			bool block = mngr->check_host_gfw(host);

			if (block) {
                BOOST_LOG_TRIVIAL(info) << "blocked, use ssl";
				// send start cmd to ssl
				auto buffer = std::make_shared<relay_data>(_owner->session(), relay_data::START_TCP);
				std::copy_n(data, len -3, (uint8_t*)buffer->data_buffer().data());
//	BOOST_LOG_TRIVIAL(info) << " send start remote data: \n" << buf_to_string(buffer->data_buffer().data(), buffer->data_buffer().size());
				buffer->resize(len -3);
                mngr->send_data(buffer);
                impl_start_read();
			} else {
                BOOST_LOG_TRIVIAL(info) << "not blocked, use local";
				auto re_hosts = _host_resolver.async_resolve(host, port, yield);
				asio::async_connect(_sock_remote, re_hosts, yield);
				impl_local_relay(true);
				impl_local_relay(false);
			}
            // start_send for local relay too, use timer to stop when timeout
            _owner->start_send();
			// send sock5 ok back
			//BOOST_LOG_TRIVIAL(info) << " send sock5 ok back : ";
			buf =  {5, 0, 0, 1, 0, 0, 0, 0, 0, 0};
			async_write(_sock, asio::buffer(buf), yield);

		} catch (boost::system::system_error& error) {
            _owner->internal_log("local start:", error);
            _owner->internal_stop_relay();
		}
	});
}

void raw_tcp::tcp_impl::impl_start_remote()
{
	auto owner(_owner->shared_from_this());
    _owner->spawn_in_strand([this, owner](asio::yield_context yield) {
		try {
			// auto data = (uint8_t*) buf->data_buffer().data();
			// auto len = buf->data_size();
			auto[host, port] = _owner->remote();
			auto re_hosts = _host_resolver.async_resolve(host, port, yield);
			asio::async_connect(_sock, re_hosts, yield);
            // BOOST_LOG_TRIVIAL(info) << "raw_tcp connect to "<<host<<port;

            _owner->start_send();
			// start raw data relay
			impl_start_read();

		} catch (boost::system::system_error& error) {
            _owner->internal_log("start remote :", error);
            _owner->internal_stop_relay();
		}
	});
}
void raw_tcp::start_relay()
{
    std::function<void()> run_relay[] = {
        [this]() {_impl->impl_start_local();},
        [this]() {_impl->impl_start_remote();},
        [this]() {_impl->impl_start_transparent();},
    };
    // run_relay[_impl->_type]();
    run_relay[type()]();
}
void raw_tcp::internal_log(const std::string &desc, const boost::system::system_error&error)
{
    BOOST_LOG_TRIVIAL(error) << "raw_tcp "<<session() << desc<<error.what();
}
