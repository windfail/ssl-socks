#include <iostream>
#include <boost/asio/spawn.hpp>
//#include <boost/asio/yield.hpp>
#include <sstream>
#include "relay.hpp"
#include <boost/format.hpp>
#include "raw_tcp.hpp"
#include "ssl_relay.hpp"

using boost::format;

// ok begin common ssl relay functions
std::string buf_to_string(void *buf, std::size_t size);

struct ssl_relay::ssl_impl
{
    ssl_impl(asio::io_context &io, const relay_config &config):
        // , std::shared_ptr<ssl_relay> owner) :
		_io_context(io),
        // _strand(io->get_executor()),
         _ctx(init_ssl(config)),
		_sock(io, _ctx),
//		_acceptor(*io()),//, tcp::endpoint(tcp::v4(), config.local_port)),
		_remote(asio::ip::make_address(config.remote_ip), config.remote_port),
		_rand(std::random_device()()),
		// _config(config),
        _type(config.type),
        _gfw(config.gfw_file)
        // _owner(owner)
    {}
    ~ssl_impl() = default;

    class _relay_t {
    public:
        std::shared_ptr<raw_relay> relay;
        int timeout {TIMEOUT};
        _relay_t() {
        };
        explicit _relay_t(const std::shared_ptr<raw_relay> &relay) :relay(relay) {};
        ~_relay_t()=default;
    };
	asio::io_context &_io_context;
	enum {
		NOT_START,
		SSL_CONNECT,
		SSL_START,
		SSL_CLOSED
	} _ssl_status = NOT_START;

	// asio::strand<asio::io_context::executor_type> _strand;
	ssl::context _ctx;

	//std::shared_ptr<ssl_socket>  _sock;
	ssl_socket _sock;

    // _relays
	std::unordered_map<uint32_t, std::shared_ptr<_relay_t>> _relays;
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
};
std::size_t ssl_relay::internal_send_data(const std::shared_ptr<relay_data> &buf, asio::yield_context &yield)
{
    auto len = async_write(_impl->_sock, buf->buffers(), yield);
    if (len != buf->size()) {
        auto emsg = format("ssl relay len %1%, data size %2%")%len % buf->size();
        throw_err_msg(emsg.str());
        // throw(boost::system::system_error(boost::system::error_code(), emsg.str()));
    }
    BOOST_LOG_TRIVIAL(info) << "ssl send ok, "<<len;
    return len;
}

void ssl_relay::ssl_impl::impl_do_data(const std::shared_ptr<relay_data>& buf)
{
    auto session = buf->session();
    if (buf->cmd() == relay_data::KEEP_RELAY) {
        _timeout_kp = TIMEOUT;
    } else {
        _timeout_rd = TIMEOUT;
    }
    if ( buf->cmd() == relay_data::DATA_RELAY) {
        auto val = _relays.find(session);
        auto relay = default_relay;
        if (val != _relays.end() ) { // session stopped, tell remote to STOP
            relay = val->second->relay;
        }
        if (relay == nullptr) {
            // tell remote stop
            // impl_stop_raw_relay(session, relay_data::from_raw);
        } else {
            relay->send_data(buf);
        }
    } else if (buf->cmd() == relay_data::START_UDP) { // remote get start udp
    } else if (buf->cmd() == relay_data::START_TCP) { // remote get start connect
        auto relay = std::make_shared<raw_tcp> (_io_context, owner, session);
        _relays[session] = std::make_shared<ssl_impl::_relay_t>(relay);
        auto start_task = std::bind(&raw_tcp::start_remote_connect, relay, buf);
        relay->strand().post(start_task, asio::get_associated_allocator(start_task));
    } else if (buf->cmd() == relay_data::STOP_RELAY) { // post stop to raw
        _relays.erase(session);
    }

}

ssl_relay::~ssl_relay()
{
    BOOST_LOG_TRIVIAL(info) << "ssl relay destruct";
}

ssl_relay::ssl_relay(asio::io_context &io, const relay_config &config) :
    base_relay(io), _impl(io, std::make_unique<ssl_impl>(io, config))
{
    BOOST_LOG_TRIVIAL(info) << "ssl relay construct";
}

void ssl_relay::start_relay()
{
    auto self(shared_from_this());
	spawn_in_strand([this, self](asio::yield_context yield) {
		try {
			if (_impl->type != REMOTE_SERVER) {
				_impl->_sock.lowest_layer().async_connect(_impl->remote, yield);
			}
			_impl->_sock.lowest_layer().set_option(tcp::no_delay(true));
			_impl->_sock.async_handshake(
				type == REMOTE_SERVER ? ssl_socket::server : ssl_socket::client,
				yield);
			_impl->_ssl_status = ssl_impl::SSL_START;
            start_send();
			// start ssl read routine
			while (true) {
				auto buf = std::make_shared<relay_data>(0);
				auto len = _impl->_sock.async_read_some(buf->header_buffer(), yield);
				BOOST_LOG_TRIVIAL(info) << "ssl read session "<<buf->session()<<" cmd"<<buf->cmd();
				if (len != buf->header_buffer().size()
				    || buf->head()._len > READ_BUFFER_SIZE) {
					auto emsg = format(
						" header len: %1%, expect %2%, hsize %3%, session %4%, cmd %5%, dlen %6%")
						%len %buf->header_size() % buf->head()._len %buf->session() %buf->cmd() %buf->data_size();
					throw_err_msg(emsg.str());
				}
				if (buf->data_size() != 0) { // read data
					len = async_read(_impl->_sock, buf->data_buffer(), yield);
					if (len != buf->data_size()) {
						auto emsg = format(" read len: %1%, expect %2%") % len % buf->size();
                        throw_err_msg(emsg.str());
					}
				}
				impl_do_data(buf);
			}
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "ssl connect error: "<<error.what();
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
            for (auto [session, & relay]:_impl->_relays) {
                relay->relay->stop_raw_relay();
            }
            _impl->_relays.clear();
            // close sock
            _impl->_sock.async_shutdown(yield);
            _impl->_sock.lowest_layer().close();
		} catch (boost::system::system_error& error) {
        }
    });
}

void ssl_relay::add_raw_tcp(const std::shared_ptr<raw_tcp> &relay)
{
    auto self(shared_from_this());
    run_in_strand([this, self, relay] {
        uint32_t session = 0;
        do {
            auto ran = _impl->_rand();
//		auto tmp = std::chrono::system_clock::now().time_since_epoch().count();
            auto tmp = time(nullptr);
            session = (ran & 0xffff0000) | (tmp & 0xffff);
//		BOOST_LOG_TRIVIAL(info) << " raw relay construct new session: "<<session;
        } while ( _impl->_relays.count(session) );

        relay->session(session);
        _impl->_relays.emplace(session, std::make_shared<ssl_impl::_relay_t>(relay));
        relay->manager(self);

        if (_impl->_type == LOCAL_SERVER ){
            relay->local_start();
        } else {
            relay->transparent_start();
        }
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
	if (_impl->_ssl_status == ssl_impl::NOT_START)
		return;

	_impl->_timeout_rd--;
	_impl->_timeout_wr--;
//	_impl->_timeout_kp--;
	if (_impl->_timeout_rd < 0 && (_impl->_timeout_wr < 0 || _impl->_timeout_kp < 0)) {
		// shutdown
//		BOOST_LOG_TRIVIAL(info) << "ssl timeout: "<< _impl->_timeout_rd<< " "<<_impl->_timeout_wr<< " "<< _impl->_timeout_kp;
		_impl->impl_stop_ssl_relay();
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
