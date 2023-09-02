#include <vector>
#include <iostream>
#include <map>
#include <sstream>
#include <boost/asio/spawn.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/connect.hpp>
// #include <random>
//#include <boost/asio/yield.hpp>
#include <boost/format.hpp>
#include "raw_tcp.hpp"
#include "raw_udp.hpp"
#include "ssl_relay.hpp"
#include "relay_manager.hpp"

using boost::format;

static ssl::context init_ssl(const relay_config &config);
// ok begin common ssl relay functions

struct ssl_relay::ssl_impl
{
    ssl_impl(ssl_relay *owner, asio::io_context &io, const relay_config &config):
	    _owner(owner),
         _ctx(init_ssl(config)),
		_sock(io, _ctx)
    {}
    ~ssl_impl() = default;

    ssl_relay *_owner;
	ssl::context _ctx;
	ssl_socket _sock;
    // _relays

    std::map<udp::endpoint, uint32_t> _srcs;

    // udp relay timeout
    // std::shared_ptr<raw_udp> _local_udp;
    // raw_udp _local_udp;

    void impl_start_read();

    uint32_t impl_add_raw_udp(uint32_t session, const udp::endpoint &src=udp::endpoint());
    void impl_start_timer();
};

void ssl_relay::ssl_impl::impl_start_read()
{
    auto owner(_owner->shared_from_this());
    asio::spawn(_owner->strand, [this, owner](asio::yield_context yield){
        try{
			while (true) {
				auto buf = std::make_shared<relay_data>(0);
				auto len = _sock.async_read_some(buf->header_buffer(), yield);
				// BOOST_LOG_TRIVIAL(info) << "ssl read session "<<buf->session()<<" cmd"<<buf->cmd();
				if (len != buf->header_buffer().size()
				    || buf->head()._len > READ_BUFFER_SIZE) {
					auto emsg = format(
						" header len: %1%, expect %2%, hsize %3%, session %4%, cmd %5%, dlen %6%")
						%len %buf->header_size() % buf->head()._len %buf->session() %buf->cmd() %buf->data_size();
					throw_err_msg(emsg.str());
				}
				if (buf->data_size() != 0) { // read data
					len = asio::async_read(_sock, buf->data_buffer(), yield);
					if (len != buf->data_size()) {
						auto emsg = format(" read len: %1%, expect %2%") % len % buf->data_size();
                        throw_err_msg(emsg.str());
					}
				}
				owner->set_alive(true);
				auto mngr = owner->manager.lock();
				if (mngr == nullptr) {
					// TBD should not happen
				}
				mngr->add_response(buf);
			}
		} catch (boost::system::system_error& error) {
            _owner->internal_log("read:", error);
            _owner->stop_relay();
		}
    });
}

ssl_relay::~ssl_relay()
{
    BOOST_LOG_TRIVIAL(info) << "ssl relay destruct";
}
ssl_relay::ssl_relay(asio::io_context &io, const relay_config &config) :
    base_relay(io, config), _impl(std::make_unique<ssl_impl>(this, io, config))
{
    BOOST_LOG_TRIVIAL(info) << "ssl relay construct";
}

std::size_t ssl_relay::internal_send_data(const std::shared_ptr<relay_data> buf, asio::yield_context &yield)
{
	set_alive(true);
    auto len = async_write(_impl->_sock, buf->buffers(), yield);
    if (len != buf->size()) {
        auto emsg = format("ssl relay len %1%, data size %2%")%len % buf->size();
        throw_err_msg(emsg.str());
    }
    // BOOST_LOG_TRIVIAL(info) << "ssl send ok, "<<len;
    return len;
}

void ssl_relay::start_relay()
{
    auto self(shared_from_this());
    asio::spawn(strand, [this, self](asio::yield_context yield) {
		try {
			if (config.type != REMOTE_SERVER) {
				auto & host = config.remote_ip;
				auto & port = config.remote_port;
                auto msg = format("remote %1% port %2%")%host%port;
                internal_log(msg.str());
                tcp::resolver host_resolver(io);
				auto re_hosts = host_resolver.async_resolve(host, port, yield);
                asio::async_connect(_impl->_sock.lowest_layer(), re_hosts, yield);
			}
			_impl->_sock.lowest_layer().set_option(tcp::no_delay(true));

			_impl->_sock.async_handshake(
				config.type == REMOTE_SERVER ? ssl_socket::server : ssl_socket::client,
				yield);
			// TBD local transparent udp
            // if (type() == LOCAL_TRANSPARENT) {
            //     // _impl->_local_udp.manager(std::static_pointer_cast<ssl_relay> (self));
            //     // _impl->_local_udp.start_relay();
            //     _impl->_local_udp->manager(std::static_pointer_cast<ssl_relay> (self));
            //     _impl->_local_udp->start_relay();
            // }
            start_send();
            // start ssl read routine
            _impl->impl_start_read();
            BOOST_LOG_TRIVIAL(info) << "ssl relay started";
		} catch (boost::system::system_error& error) {
            auto emsg=format("ssl start_relay : ");
            internal_log(emsg.str(), error);
            stop_relay();
		}
	});
}

void ssl_relay::stop_relay()
{
    auto self(shared_from_this());
    asio::spawn(strand, [this, self](asio::yield_context yield) {
        // BOOST_LOG_TRIVIAL(info) << "ssl relay internal stop"<< self.use_count() << is_stop();
	    set_alive(false);
        try{
            // close sock
            BOOST_LOG_TRIVIAL(info) << "ssl relay shutdown"<< self.use_count();
            // boost::system::error_code ec;
            // _impl->_sock.async_shutdown(yield[ec]);
            _impl->_sock.async_shutdown(yield);
            BOOST_LOG_TRIVIAL(info) << "ssl relay shutdown ok"<< self.use_count();
            _impl->_sock.lowest_layer().close();
            BOOST_LOG_TRIVIAL(info) << "ssl relay internal stop over"<< self.use_count();
        } catch (boost::system::system_error& error) {
            _impl->_sock.lowest_layer().close();
            BOOST_LOG_TRIVIAL(info) << "ssl relay stop"<< self.use_count() <<" error:"<< error.what();
        }
    });
}

ssl::context init_ssl(const relay_config &config)
{
	ssl::context ctx(config.type == REMOTE_SERVER ?
			 ssl::context::tlsv12_server :
			 ssl::context::tlsv12_client);
	BOOST_LOG_TRIVIAL(info) << "cert : " << config.cert << " key:"<<config.key;
	ctx.load_verify_file("/etc/ssl-socks/ca.crt");//"yily.crt");
	ctx.set_verify_mode(ssl::verify_peer|ssl::verify_fail_if_no_peer_cert);
	ctx.use_certificate_file(config.cert/*"yily.crt"*/, ssl::context::pem);
	ctx.use_rsa_private_key_file(config.key/*"key.pem"*/, ssl::context::pem);
	return ctx;
}

ssl_socket & ssl_relay::get_sock()
{
    return _impl->_sock;
}
void ssl_relay::internal_log(const std::string &desc, const boost::system::system_error&error)
{
    BOOST_LOG_TRIVIAL(error) << "ssl_relay "<<desc<<error.what();
}

void ssl_relay::send_udp_data(const udp::endpoint &src, std::shared_ptr<relay_data> buf)
{
    auto self(shared_from_this());
    run_in_strand(strand, [this, self, src, buf]() {
        if (!alive()) {
            BOOST_LOG_TRIVIAL(error) << "ssl send udp on stop";
            return;
        }
        auto sess = _impl->_srcs[src];
        if (sess == 0) {
            sess = _impl->impl_add_raw_udp(0, src);
        }
        // BOOST_LOG_TRIVIAL(error) << "send udp data";
        buf->session(sess);
        send_data(buf);
    });
}

