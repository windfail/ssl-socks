
#include <iostream>
#include <unordered_map>
#include <boost/asio/spawn.hpp>
#include <boost/asio/read.hpp>
#include <random>
//#include <boost/asio/yield.hpp>
#include <sstream>
#include <boost/format.hpp>
#include "raw_tcp.hpp"
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
		_remote(asio::ip::make_address(config.remote_ip), config.remote_port),
		_rand(std::random_device()()),
        _type(config.type),
        _gfw(config.gfw_file)
    {}
    ~ssl_impl() = default;

    ssl_relay *_owner;
	asio::io_context &_io_context;

	ssl::context _ctx;
	ssl_socket _sock;

    // _relays
	std::unordered_map<uint32_t, std::shared_ptr<raw_relay>> _relays;
    // default relay used for local udp relay
    std::shared_ptr<raw_relay> default_relay;

	tcp::endpoint _remote;	// remote ssl relay ep
	// random
	std::minstd_rand _rand;
	// relay_config _config;
    server_type _type;
	gfw_list _gfw;

	int _timeout_rd = TIMEOUT;
	int _timeout_wr = TIMEOUT;
	int _timeout_kp = TIMEOUT;

    void impl_do_data(const std::shared_ptr<relay_data>& buf);
    void impl_start_read();
};

void ssl_relay::ssl_impl::impl_start_read()
{
    auto owner(_owner->shared_from_this());
    _owner->spawn_in_strand([this, owner](asio::yield_context yield){
        try{
			while (true) {
				auto buf = std::make_shared<relay_data>(0);
				auto len = _sock.async_read_some(buf->header_buffer(), yield);
				BOOST_LOG_TRIVIAL(info) << "ssl read session "<<buf->session()<<" cmd"<<buf->cmd();
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
						auto emsg = format(" read len: %1%, expect %2%") % len % buf->size();
                        throw_err_msg(emsg.str());
					}
				}
                _owner->refresh_timer(TIMEOUT);
				impl_do_data(buf);
			}
		} catch (boost::system::system_error& error) {
            _owner->internal_log(error, "read:");
            _owner->internal_stop_relay();
		}
    });
}
void ssl_relay::ssl_impl::impl_do_data(const std::shared_ptr<relay_data>& buf)
{
    auto session = buf->session();
    if (buf->cmd() == relay_data::KEEP_RELAY) {
        _timeout_kp = TIMEOUT;
        return;
    } else {
        _timeout_rd = TIMEOUT;
    }
    auto val = _relays.find(session);
    std::shared_ptr<raw_relay> relay =
        val == _relays.end() ? nullptr :
        val->second;
    if ( buf->cmd() == relay_data::DATA_RELAY) {
        if (relay == nullptr) {
            // tell remote stop
            // impl_stop_raw_relay(session, relay_data::from_raw);
        } else {
            relay->send_data(buf);
        }
    } else if (buf->cmd() == relay_data::START_UDP) { // remote get start udp
    } else if (buf->cmd() == relay_data::START_TCP) { // remote get start connect
        if (relay != nullptr) {
            relay->stop_raw_relay();
        }
        auto data = (uint8_t*) buf->data_buffer().data();
        auto len = buf->data_size();
        auto[host, port] = parse_address(data, len);
        _owner->add_raw_tcp(nullptr, session, host, port);
    } else if (buf->cmd() == relay_data::STOP_RELAY) { // post stop to raw
        if (relay != nullptr)
            relay->stop_raw_relay();
        _relays.erase(session);
    }
}

ssl_relay::~ssl_relay()
{
    BOOST_LOG_TRIVIAL(info) << "ssl relay destruct";
}

ssl_relay::ssl_relay(asio::io_context &io, const relay_config &config) :
    base_relay(io), _impl(std::make_unique<ssl_impl>(this, io, config))
{
    BOOST_LOG_TRIVIAL(info) << "ssl relay construct";
}
std::size_t ssl_relay::internal_send_data(const std::shared_ptr<relay_data> &buf, asio::yield_context &yield)
{
    return async_write(_impl->_sock, buf->buffers(), yield);
    // if (len != buf->size()) {
    //     auto emsg = format("ssl relay len %1%, data size %2%")%len % buf->size();
    //     throw_err_msg(emsg.str());
    // }
    // BOOST_LOG_TRIVIAL(info) << "ssl send ok, "<<len;
    // return len;
}

void ssl_relay::start_relay()
{
    auto self(shared_from_this());
	spawn_in_strand([this, self](asio::yield_context yield) {
		try {
			if (_impl->_type != REMOTE_SERVER) {
				_impl->_sock.lowest_layer().async_connect(_impl->_remote, yield);
			}
			_impl->_sock.lowest_layer().set_option(tcp::no_delay(true));
			_impl->_sock.async_handshake(
				_impl->_type == REMOTE_SERVER ? ssl_socket::server : ssl_socket::client,
				yield);
            start_send();
			// start ssl read routine
            _impl->impl_start_read();
		} catch (boost::system::system_error& error) {
            internal_log(error, "connect:");
            internal_stop_relay();
		}
	});
}

void ssl_relay::internal_stop_relay()
{
    auto self(shared_from_this());
	spawn_in_strand([this, self](asio::yield_context yield) {
        try{
            // stop all raw relays
            for (auto && [session, relay]:_impl->_relays) {
                relay->stop_raw_relay();
            }
            _impl->_relays.clear();
            // close sock
            _impl->_sock.async_shutdown(yield);
            _impl->_sock.lowest_layer()
                .close();
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
            do {
                auto ran = _impl->_rand();
                auto tmp = time(nullptr);
                session = (ran & 0xffff0000) | (tmp & 0xffff);
//		BOOST_LOG_TRIVIAL(info) << " raw relay construct new session: "<<session;
            } while ( _impl->_relays.count(session) );
        } else { // create relay from ssl relay==nullptr
            relay = std::make_shared<raw_tcp> (_impl->_io_context, _impl->_type, host, service);
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

void ssl_relay::timer_handle()
{

	_impl->_timeout_rd--;
	_impl->_timeout_wr--;
//	_impl->_timeout_kp--;
	if (_impl->_timeout_rd < 0 && (_impl->_timeout_wr < 0 || _impl->_timeout_kp < 0)) {
		// shutdown
//		BOOST_LOG_TRIVIAL(info) << "ssl timeout: "<< _impl->_timeout_rd<< " "<<_impl->_timeout_wr<< " "<< _impl->_timeout_kp;
        internal_stop_relay();
	}
//	BOOST_LOG_TRIVIAL(info) << " ssl relay timer handle : "<< _impl->_timeout_rd<< " "<<_impl->_timeout_wr<< " "<< _impl->_timeout_kp;
}


bool ssl_relay::check_host_gfw(const std::string &host)
{
    return _impl->_gfw.is_blocked(host);
}

ssl_socket & ssl_relay::get_sock()
{
    return _impl->_sock;
}
void ssl_relay::internal_log(boost::system::system_error&error, const std::string &desc)
{
    BOOST_LOG_TRIVIAL(error) << "ssl_relay "<<desc<<error.what();
}
