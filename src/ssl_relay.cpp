#include <vector>
#include <iostream>
#include <unordered_map>
#include <boost/asio/spawn.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/connect.hpp>
// #include <random>
//#include <boost/asio/yield.hpp>
#include <sstream>
#include <boost/format.hpp>
#include "raw_tcp.hpp"
#include "raw_udp.hpp"
#include "ssl_relay.hpp"
#include "gfwlist.hpp"

using boost::format;

ssl::context init_ssl(const relay_config &config);
// ok begin common ssl relay functions
std::string buf_to_string(void *buf, std::size_t size);

struct ssl_relay::ssl_impl
{
    ssl_impl(ssl_relay *owner, asio::io_context &io, const relay_config &config):
        _owner(owner),
		_io_context(io),
         _ctx(init_ssl(config)),
		_sock(io, _ctx),
        _host_resolver(io),
        _gfw(config.gfw_file),
        _timer(io)
    {}
    ~ssl_impl() = default;

    ssl_relay *_owner;
	asio::io_context &_io_context;
	ssl::context _ctx;
	ssl_socket _sock;
	tcp::resolver _host_resolver;
    // _relays
	std::unordered_map<uint32_t, std::shared_ptr<raw_relay>> _relays;
    std::map<udp::endpoint, uint32_t> _srcs;
	std::unordered_map<uint32_t, int> _timeout;
    std::shared_ptr<raw_udp> _udp_relay;

	gfw_list _gfw;
    uint32_t _session = 1;
    asio::steady_timer _timer;

    void impl_do_data(const std::shared_ptr<relay_data>& buf);
    void impl_start_read();

    uint32_t impl_add_raw_udp(uint32_t session, const udp::endpoint &src=udp::endpoint());
    void impl_start_timer();
};
void ssl_relay::ssl_impl::impl_start_timer()
{
    auto owner(_owner->shared_from_this());
    _owner->spawn_in_strand([this, owner](asio::yield_context yield) {
        while (true) {
            _timer.expires_after(std::chrono::seconds(RELAY_TICK));
            boost::system::error_code err;
            _timer.async_wait(yield[err]);
            if (err == asio::error::operation_aborted) {
                return;
            }
            std::vector<uint32_t> del;
            for (auto &[sess, timeout] : _timeout) {
                if (timeout--)
                    continue;
                //shutdown sess
                auto relay = _relays[sess];
                if ( relay )
                    relay->stop_raw_relay();
                del.push_back(sess);
            }
            for (auto sess:del)
                _owner->ssl_stop_raw_relay(sess);
        }
    });
}

uint32_t ssl_relay::ssl_impl::impl_add_raw_udp(uint32_t session, const udp::endpoint &src)
{
    if (session == 0) {
        session = _session++;
        _srcs[src] = session;
    }
    BOOST_LOG_TRIVIAL(info) << "ssl add raw udp session"<<session<<" from"<<src;
    if (_owner->type() == REMOTE_SERVER) {
        auto relay = std::make_shared<raw_udp>(_io_context, _owner->type(), src);
        relay->session(session);
        relay->manager(std::static_pointer_cast<ssl_relay> (_owner->shared_from_this()));
        relay->start_relay();
        _relays[session] = relay;
    } else {
        if (!_udp_relay) {
            _udp_relay = std::make_shared<raw_udp>(_io_context, _owner->type());
            _udp_relay->manager(std::static_pointer_cast<ssl_relay> (_owner->shared_from_this()));
            _udp_relay->start_relay();
        }
        _udp_relay->add_peer(session, src);
    }
    _timeout[session] = TIMEOUT_COUNT;
    return session;
}
void ssl_relay::ssl_impl::impl_start_read()
{
    auto owner(_owner->shared_from_this());
    _owner->spawn_in_strand([this, owner](asio::yield_context yield){
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
				impl_do_data(buf);
			}
		} catch (boost::system::system_error& error) {
            _owner->internal_log("read:", error);
            _owner->internal_stop_relay();
		}
    });
}
void ssl_relay::ssl_impl::impl_do_data(const std::shared_ptr<relay_data>& buf)
{
    auto session = buf->session();
    auto relay = _relays[session];
    if ( buf->cmd() == relay_data::DATA_RELAY) { // tcp data
        if (relay) {
            relay->send_data(buf);
        } else {
            // tell remote stop TBD
            _owner->ssl_stop_raw_relay(session);
        }
    } else if (buf->cmd() == relay_data::DATA_UDP) {
        if (relay == nullptr) {
            if (_owner->type() == REMOTE_SERVER) { // remote start new udp
                impl_add_raw_udp(session);
            } else { // local no session ,tell remote stop
                _timeout[session] = TIMEOUT_COUNT;
                _udp_relay->send_data(buf);
                return;
            }
        }
        _timeout[session] = TIMEOUT_COUNT;
        _relays[session]->send_data(buf);
    } else if (buf->cmd() == relay_data::START_TCP) { // remote get start connect
        if (relay) {
            relay->stop_raw_relay();
        }
        auto[host, port] = parse_address(buf->data_buffer().data(), buf->data_size());
        _owner->add_raw_tcp(nullptr, session, host, port);
    } else if (buf->cmd() == relay_data::STOP_RELAY) { // post stop to raw
        if (relay)
            relay->stop_raw_relay();
        _relays.erase(session);
        _timeout.erase(session);
    }
}

ssl_relay::~ssl_relay()
{
    BOOST_LOG_TRIVIAL(info) << "ssl relay destruct";
}
ssl_relay::ssl_relay(asio::io_context &io, const relay_config &config) :
    base_relay(io, config.type, config.remote_ip, config.remote_port), _impl(std::make_unique<ssl_impl>(this, io, config))
{
    BOOST_LOG_TRIVIAL(info) << "ssl relay construct";
}

std::size_t ssl_relay::internal_send_data(const std::shared_ptr<relay_data> &buf, asio::yield_context &yield)
{
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
	spawn_in_strand([this, self](asio::yield_context yield) {
		try {
			if (type() != REMOTE_SERVER) {
                auto[host, port] = remote();
				auto re_hosts = _impl->_host_resolver.async_resolve(host, port, yield);
                asio::async_connect(_impl->_sock.lowest_layer(), re_hosts, yield);
			}
			_impl->_sock.lowest_layer().set_option(tcp::no_delay(true));

			_impl->_sock.async_handshake(
				type() == REMOTE_SERVER ? ssl_socket::server : ssl_socket::client,
				yield);
            start_send();
			// start ssl read routine
            _impl->impl_start_read();
            _impl->impl_start_timer();
            BOOST_LOG_TRIVIAL(info) << "ssl relay started";
		} catch (boost::system::system_error& error) {
            internal_log("connect:", error);
            internal_stop_relay();
		}
	});
}

void ssl_relay::internal_stop_relay()
{
    auto self(shared_from_this());
	spawn_in_strand([this, self](asio::yield_context yield) {
        if (is_stop())
            return;
        is_stop(true);
        _impl->_timer.cancel();
        try{
            // stop all raw relays
            for (auto &[session, relay]:_impl->_relays) {
                if (relay)
                    relay->stop_raw_relay();
            }
            _impl->_relays.clear();
            // close sock
            _impl->_sock.async_shutdown(yield);
            _impl->_sock.lowest_layer().close();
		} catch (boost::system::system_error& error) {
        }
    });
}

// add raw_tcp:
// in local server: relay_server create and connect on raw_tcp, call with sess=0, ssl_relay create new session
// in remote server: ssl_relay get TCP_CONNECT cmd with session, create new raw_tcp with session
void ssl_relay::add_raw_tcp(const std::shared_ptr<raw_tcp> &tcp_relay, uint32_t sess, const std::string &host, const std::string &service)
{
    auto self(shared_from_this());
    run_in_strand([this, self, tcp_relay, sess, host, service]() {
        auto relay = tcp_relay;
        auto session = sess;
        if (session == 0) { // create session from relay_server
            session = _impl->_session++;
            if (_impl->_relays.count(session)) {
                BOOST_LOG_TRIVIAL(error) << "ssl relay session repeat: "<<session;
                internal_stop_relay();
                return;
            }
        } else { // create relay from ssl relay==nullptr
            relay = std::make_shared<raw_tcp> (_impl->_io_context, type(), host, service);
        }
        relay->session(session);
        relay->manager(std::static_pointer_cast<ssl_relay>(self));
        _impl->_relays[session] = relay;

        relay->start_relay();
    });
}
void ssl_relay::ssl_stop_raw_relay(uint32_t session)
{
    auto self(shared_from_this());
    run_in_strand([this, self, session]()
    {
        _impl->_relays.erase(session);
        _impl->_timeout.erase(session);
        // send to ssl
        // BOOST_LOG_TRIVIAL(info) << " send raw stop : "<<session;
        auto buffer = std::make_shared<relay_data>(session, relay_data::STOP_RELAY);
        send_data(buffer);
    });
}

ssl::context init_ssl(const relay_config &config)
{
	ssl::context ctx(config.type == REMOTE_SERVER ?
			 ssl::context::tlsv12_server :
			 ssl::context::tlsv12_client);
	BOOST_LOG_TRIVIAL(info) << "cert : " << config.cert << " key:"<<config.key;
	ctx.load_verify_file(config.cert);//"yily.crt");
	ctx.set_verify_mode(ssl::verify_peer|ssl::verify_fail_if_no_peer_cert);
	ctx.use_certificate_file(config.cert/*"yily.crt"*/, ssl::context::pem);
	ctx.use_rsa_private_key_file(config.key/*"key.pem"*/, ssl::context::pem);
	return ctx;
}

bool ssl_relay::check_host_gfw(const std::string &host)
{
    return _impl->_gfw.is_blocked(host);
}

ssl_socket & ssl_relay::get_sock()
{
    return _impl->_sock;
}
void ssl_relay::internal_log(const std::string &desc, const boost::system::system_error&error)
{
    BOOST_LOG_TRIVIAL(error) << "ssl_relay "<<desc<<error.what();
}

void ssl_relay::send_udp_data(const udp::endpoint &src, std::shared_ptr<relay_data> &buf)
{
    auto self(shared_from_this());
    run_in_strand([this, self, src, buf]() {
        auto sess = _impl->_srcs[src];
        if (sess == 0) {
            sess = _impl->impl_add_raw_udp(0, src);
        }
        buf->session(sess);
        _impl->_timeout[sess] = TIMEOUT_COUNT;
        send_data(buf);
    });
}
