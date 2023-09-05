#include <sys/types.h>
#include <sys/socket.h>
#include <sstream>
#include <iomanip>
#include <boost/format.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/write.hpp>
#include "raw_tcp.hpp"
#include "relay_manager.hpp"

std::pair<std::string, std::string> parse_address(void *buf, std::size_t len);

struct raw_tcp::tcp_impl
{
    explicit tcp_impl(raw_tcp *owner, asio::io_context &io) :
        _owner(owner), _sock(io), _sock_remote(io)
    {}
	explicit tcp_impl(raw_tcp *owner, asio::io_context &io, const std::string &host, const std::string &service) :
        _owner(owner), _sock(io), _sock_remote(io),
        _dst_host(host), _dst_service(service)
    {}

    raw_tcp *_owner;

	// main tcp sock
	// for local server, read/write from local client
	// for remote server, read/write with real remote dst
	tcp::socket _sock;

	// sock remote for local server only, used for non block requests, raw tcp direct connect to real remote dst, not through ssl
	tcp::socket _sock_remote;

	std::string local_buf;
	std::string remote_buf;

	// remote real dst/service
	std::string _dst_host;
	std::string _dst_service;

    void impl_start_read();
    void impl_start_local();
    void impl_start_remote();
    void impl_start_transparent();
    void impl_local_relay(bool dir);

};
void raw_tcp::tcp_impl::impl_start_read()
{
	auto owner(_owner->shared_from_this());

	asio::spawn(_owner->strand, [this, owner](asio::yield_context yield) {
		try {
			while (true) {
				auto buf = std::make_shared<relay_data>(_owner->session);
				auto len = _sock.async_read_some(buf->data_buffer(), yield);
				//	BOOST_LOG_TRIVIAL(info) << " raw read len: "<< len;
				// post to manager
				buf->resize(len);
				auto mngr = _owner->manager.lock();
				if (mngr == nullptr) {
					// TBD should not happen
				}
                mngr->add_request(buf);
                _owner->reset_timeout();
			}
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << _owner->session<<" tcp raw read error: "<<error.what();
            _owner->stop_relay();
		}
	});
}

raw_tcp::raw_tcp(asio::io_context &io, const relay_config&config, std::shared_ptr<relay_manager> mngr) :
	raw_relay(io, config, mngr),_impl(std::make_unique<tcp_impl> (this, io))
{
    BOOST_LOG_TRIVIAL(info) << "raw tcp construct: ";
}
raw_tcp::raw_tcp(asio::io_context &io, const relay_config&config, std::shared_ptr<relay_manager> mngr, const std::string &host, const std::string &service) :
	raw_relay(io, config, mngr), _impl(std::make_unique<tcp_impl> (this, io, host, service))
{
    BOOST_LOG_TRIVIAL(info) << "raw tcp construct: ";
}
raw_tcp::~raw_tcp()
{
    BOOST_LOG_TRIVIAL(info) << "raw tcp destruct: "<<session;
}

tcp::socket& raw_tcp::get_sock()
{
	return _impl->_sock;
}
void raw_tcp::stop_relay()
{
    auto self(shared_from_this());
    run_in_strand(strand, [this, self](){
        // call close socket
        // BOOST_LOG_TRIVIAL(info) << "raw_tcp: stop raw tcp"<< session();
	    state = RELAY_STOP;
        boost::system::error_code err;
        _impl->_sock.shutdown(tcp::socket::shutdown_both, err);
        _impl->_sock.close(err);
    });
}

std::size_t raw_tcp::internal_send_data(const std::shared_ptr<relay_data> buf, asio::yield_context &yield)
{
    // return async_write(_impl->_sock, buf->data_buffer(), yield);
	auto len = asio::async_write(_impl->_sock, buf->data_buffer(), yield);
    // BOOST_LOG_TRIVIAL(info) << session() <<" tcp send from "<< _impl->_sock.local_endpoint()<<"to "<<_impl->_sock.remote_endpoint()<<"len "<<len;
    if (len != buf->data_size()) {
        auto emsg = boost::format("tcp %d relay len %1%, data size %2%")%session%len % buf->size();
        throw_err_msg(emsg.str());
    }
    return len;
}


// local server functions
// for non-blocked hosts in socks5 mode, directly relay
// dir: true - read from remote / send to local client
//      false - read from local client / send to remote
void raw_tcp::tcp_impl::impl_local_relay(bool dir)
{
	auto owner(_owner->shared_from_this());
	asio::spawn(_owner->strand, [this, owner, dir](asio::yield_context yield) {
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
            _owner->stop_relay();
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
        // BOOST_LOG_TRIVIAL(info) << _owner->session() <<" tcp start "<< _sock.local_endpoint()<<"from "<<_sock.remote_endpoint();
        auto buffer = std::make_shared<relay_data>(_owner->session, relay_data::START_TCP);
        auto dst = _sock.local_endpoint();
        buffer->resize(parse_addr(buffer->data_buffer().data(), dst.data()));
//	BOOST_LOG_TRIVIAL(info) << " send start remote data: \n" << buf_to_string(buffer->data_buffer().data(), buffer->data_buffer().size());
        auto mngr = _owner-> manager.lock();
        if (mngr == nullptr) {
	        //TBD should not happen!!
        }
        mngr->add_request(buffer);
        impl_start_read();
        _owner->start_send();
    } catch (boost::system::system_error& error) {
        _owner->internal_log("tproxy start:", error);
        _owner->stop_relay();
    }
}

// local socks5 proxy start
void raw_tcp::tcp_impl::impl_start_local()
{
	auto owner(_owner->shared_from_this());
	asio::spawn(_owner->strand, [this, owner](asio::yield_context yield) {
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
			// BOOST_LOG_TRIVIAL(info) << "resolve host " <<host <<" port "<<port ;
			auto mngr = _owner->manager.lock();
			bool block = _owner->config.gfw.is_blocked(host);

			if (block) {
                BOOST_LOG_TRIVIAL(info) << "blocked, use ssl";
				// send start cmd to ssl
				auto buffer = std::make_shared<relay_data>(_owner->session, relay_data::START_TCP);
				std::copy_n(data, len -3, (uint8_t*)buffer->data_buffer().data());
//	BOOST_LOG_TRIVIAL(info) << " send start remote data: \n" << buf_to_string(buffer->data_buffer().data(), buffer->data_buffer().size());
				buffer->resize(len -3);
                mngr->add_request(buffer);
                impl_start_read();
			} else {
                BOOST_LOG_TRIVIAL(info) << "not blocked, use local";
                tcp::resolver _host_resolver(_owner->io);
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
            _owner->stop_relay();
		}
	});
}

// start remote raw_tcp, connect to real dst
void raw_tcp::tcp_impl::impl_start_remote()
{
	auto owner(_owner->shared_from_this());
	asio::spawn(_owner->strand, [this, owner](asio::yield_context yield) {
		try {
			tcp::resolver _host_resolver(_owner->io);
			auto re_hosts = _host_resolver.async_resolve(_dst_host, _dst_service, yield);
			asio::async_connect(_sock, re_hosts, yield);
            // BOOST_LOG_TRIVIAL(info) << "raw_tcp connect to "<<host<<port;

            _owner->start_send();
			// start raw data relay
			impl_start_read();

		} catch (boost::system::system_error& error) {
            _owner->internal_log("start remote :", error);
            _owner->stop_relay();
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
    run_relay[config.type]();
}
void raw_tcp::internal_log(const std::string &desc, const boost::system::system_error&error)
{
    BOOST_LOG_TRIVIAL(error) << "raw_tcp "<<session << desc<<error.what();
}

