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
    ssl_impl(asio::io_context *io, const relay_config &config):
        // , std::shared_ptr<ssl_relay> owner) :
		_io_context(io),
        // _strand(io->get_executor()),
         _ctx(init_ssl(config)),
		_sock(*io, _ctx),
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
        ~_relay_t(){
            relay->stop_raw_relay(relay_data::from_ssl);
        };
    };
	asio::io_context *_io_context;
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
	std::queue<std::shared_ptr<relay_data>> _bufs; // buffers for write
    bool send_start=false;

	// random
	std::minstd_rand _rand;
	// relay_config _config;
    server_type _type;
	gfw_list _gfw;

	int _timeout_rd = TIMEOUT;
	int _timeout_wr = TIMEOUT;
	int _timeout_kp = TIMEOUT;

    void impl_buffer_add(const std::shared_ptr<relay_data> &buf);
	void impl_stop_raw_relay(uint32_t session, relay_data::stop_src src);
    void impl_stop_ssl_relay();

    void impl_handle_accept(const std::shared_ptr<raw_tcp> &relay);

    void impl_data_send();
	void impl_data_read();
};
void ssl_relay::ssl_impl::impl_data_send()
{
    auto owner(_owner.lock());
	asio::spawn(_strand, [this, owner](asio::yield_context yield) {
		try {
            send_start = true;
			while (!_bufs.empty()) {
				auto buf = _bufs.front();
//	BOOST_LOG_TRIVIAL(info) << "end of send sess"<<buf->session()<<", cmd"<<buf->cmd()<<",len"<<buf->head()._len<<"  on ssl  ";
				auto len = async_write(_sock, buf->buffers(), yield);
				// check len
				if (len != buf->size()) {
					auto emsg = format(" len %1%, data size %2%")%len % buf->size();
					throw(boost::system::system_error(boost::system::error_code(), emsg.str()));
				}
				_bufs.pop();
				BOOST_LOG_TRIVIAL(info) << "ssl send ok, "<<_bufs.size()<<" _bufs lef";
			}
            send_start = false;
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "ssl write error: "<<error.what();
			impl_stop_ssl_relay();
		}
	});
}
void ssl_relay::ssl_impl::impl_buffer_add(const std::shared_ptr<relay_data> &buf)
{
    auto owner(_owner.lock());
    auto send_on_ssl = [this, owner, buf]()
    {
        _timeout_wr = TIMEOUT;
        auto relay = _relays.find(buf->session());
        if (relay == _relays.end()) {
            return;
        }
        relay->second->timeout = TIMEOUT;

        _bufs.push(buf);
        BOOST_LOG_TRIVIAL(info) << "send sess"<<buf->session()<<"  on ssl," << _bufs.size()<<"bufs";
        if (send_start || _ssl_status != SSL_START) return;
// start ssl_write routine
        impl_data_send();
    };
    run_in_strand(send_on_ssl);
}

void ssl_relay::ssl_impl::impl_data_read()
{
	auto owner(_owner.lock());
    auto do_data = [this, owner](std::shared_ptr<relay_data>& buf)
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
                relay = val->second->relay.lock();
            }
            if (relay == nullptr) {
                // tell remote stop
                impl_stop_raw_relay(session, relay_data::from_raw);
            } else {
                auto raw_data_send = std::bind(&raw_relay::send_data_on_raw, relay, buf);
                relay->strand().post(raw_data_send, asio::get_associated_allocator(raw_data_send));
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
    };
	asio::spawn(_strand, [this, owner, do_data](boost::asio::yield_context yield) {
		try {
			while (true) {
				auto buf = std::make_shared<relay_data>(0);
				auto len = _sock.async_read_some(buf->header_buffer(), yield);
				BOOST_LOG_TRIVIAL(info) << "ssl read session "<<buf->session()<<" cmd"<<buf->cmd();
				if (len != buf->header_buffer().size()
				    || buf->head()._len > READ_BUFFER_SIZE) {
					auto emsg = format(
						" header len: %1%, expect %2%, hsize %3%, session %4%, cmd %5%, dlen %6%")
						%len %buf->header_size() % buf->head()._len %buf->session() %buf->cmd() %buf->data_size();
					throw(boost::system::system_error(boost::system::error_code(), emsg.str()));
				}
				if (buf->data_size() != 0) { // read data
					len = async_read(_sock, buf->data_buffer(), yield);
					if (len != buf->data_size()) {
						auto emsg = format(" read len: %1%, expect %2%") % len % buf->size();
						throw(boost::system::system_error(boost::system::error_code(), emsg.str()));
					}
				}
				do_data(buf);
			}
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "ssl read error: "<<error.what();
			impl_stop_ssl_relay();
		}
	});

}

// stop raw relay on ssl_relay, erease from _relays, tell other
void ssl_relay::ssl_impl::impl_stop_raw_relay(uint32_t session, relay_data::stop_src src)
{
    auto owner(_owner.lock());
    run_in_strand([this, owner, session, src]()
    {
        _relays.erase(session);
        // if not from other, need to send, TBD need add from_timeout?
        if (src == relay_data::from_raw) {
            // send to ssl
            // BOOST_LOG_TRIVIAL(info) << " send raw stop : "<<session;
            auto buffer = std::make_shared<relay_data>(session, relay_data::STOP_RELAY);
            impl_buffer_add(buffer);
            // send_data_on_ssl(buffer);
        }
    });
}
ssl_relay::ssl_relay(asio::io_context *io, const relay_config &config) :
    base_relay(io), _impl(io, std::make_unique<ssl_impl>(io, config))
{
    BOOST_LOG_TRIVIAL(info) << "ssl relay construct";
    auto type = config.type;
    auto self(shared_from_this());

	spawn_in_strand([this, self, type](asio::yield_context yield) {
		try {
			if (type != REMOTE_SERVER) {
				_impl->_sock.lowest_layer().async_connect(_impl->remote, yield);
			}
			_impl->_sock.lowest_layer().set_option(tcp::no_delay(true));
			_impl->_sock.async_handshake(
				type == REMOTE_SERVER ? ssl_socket::server : ssl_socket::client,
				yield);
			_impl->_ssl_status = ssl_impl::SSL_START;
			//  buffer write routine start when buf add
			// impl_data_send();
			// start ssl read routine
			impl_data_read();
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "ssl connect error: "<<error.what();
			// impl_stop_ssl_relay();
		}
	});
}
ssl_relay::~ssl_relay()
{
    BOOST_LOG_TRIVIAL(info) << "ssl relay destruct";
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

// send data on ssl sock
// buf is read from sock in raw_relay
void ssl_relay::send_data_on_ssl(const std::shared_ptr<relay_data> &buf)
{
//	BOOST_LOG_TRIVIAL(info) << "send sess"<<buf->session()<<", cmd"<<buf->cmd()<<",len"<<buf->data_size()<<"  on ssl ";
//	BOOST_LOG_TRIVIAL(info) << "head: "<< buf_to_string( buf->header_buffer().data(), buf->header_buffer().size());
//	BOOST_LOG_TRIVIAL(info) << " "<< buf_to_string( buf->data_buffer().data(), buf->data_buffer().size());
    _impl->impl_buffer_add(buf);
}


void ssl_relay::ssl_stop_raw_relay(uint32_t session, relay_data::stop_src src)
{
    auto self(shared_from_this());
    run_in_strand([this, self, session, src]()
    {
        _impl->_relays.erase(session);
        // if not from other, need to send, TBD need add from_timeout?
        if (src == relay_data::from_raw) {
            // send to ssl
            // BOOST_LOG_TRIVIAL(info) << " send raw stop : "<<session;
            auto buffer = std::make_shared<relay_data>(session, relay_data::STOP_RELAY);
            impl_buffer_add(buffer);
            // send_data_on_ssl(buffer);
        }
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
