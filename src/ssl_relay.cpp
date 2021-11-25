
#include <iostream>
#include <boost/asio/spawn.hpp>
//#include <boost/asio/yield.hpp>
#include <sstream>
#include "relay.hpp"
#include <boost/format.hpp>

using boost::format;

// ok begin common ssl relay functions
std::string buf_to_string(void *buf, std::size_t size);

class ssl_relay::_relay_t {
public:
	std::shared_ptr<raw_relay> relay;
	int timeout {TIMEOUT};
	_relay_t() {
	};
	explicit _relay_t(const std::shared_ptr<raw_relay> &relay) :relay(relay) {};
	~_relay_t();
};

ssl_relay::_relay_t::~_relay_t()
{
	if (relay->session() == 0) {
		return;
	}
	auto stop_raw = std::bind(&raw_relay::stop_raw_relay, relay, relay_data::from_ssl);
	relay->get_strand().post(stop_raw, asio::get_associated_allocator(stop_raw));

}
void ssl_relay::on_ssl_shutdown(const boost::system::error_code& error)
{
	BOOST_LOG_TRIVIAL(error) << "ssl shutdown over" << error;
	boost::system::error_code err;
	_sock.lowest_layer().shutdown(tcp::socket::shutdown_both, err);
	_sock.lowest_layer().close(err);

	if (_config.type != REMOTE_SERVER) {
//		BOOST_LOG_TRIVIAL(info) << "local destroy ssl sock";
		// ssl_socket do not support move construct
		// so mannually destroy and use placment new to reconstruct
		_sock.~ssl_socket();
//		BOOST_LOG_TRIVIAL(info) << "new ssl ctx";
		auto _ctx = init_ssl(_config);
//		BOOST_LOG_TRIVIAL(info) << "new ssl sock";
		new(&_sock) ssl_socket (*_io_context, _ctx);
	}
	_ssl_status = NOT_START;
}
// if stop cmd is from raw relay, send stop cmd to ssl
void ssl_relay::stop_ssl_relay(uint32_t session, relay_data::stop_src src)
{
	if (src == relay_data::ssl_err) {
		// stop all raw_relay
//		BOOST_LOG_TRIVIAL(info) << "ssl stop all relay :"<< _relays.size()<<" _relays "<<_bufs.size() <<"bufs ";
		_relays.clear();
		_bufs = std::queue<std::shared_ptr<relay_data>> ();

		if (_ssl_status == SSL_START) {
			_sock.async_shutdown(
				asio::bind_executor(_strand,
						    std::bind(&ssl_relay::on_ssl_shutdown, shared_from_this(),
							      std::placeholders::_1)));
			_ssl_status = SSL_CLOSED;
		} else {
			boost::system::error_code err;
			_sock.lowest_layer().shutdown(tcp::socket::shutdown_both, err);
			_sock.lowest_layer().close(err);
			_ssl_status = NOT_START;
		}
//		BOOST_LOG_TRIVIAL(info) << "ssl stop over";

		return;
	}

	_relays.erase(session);
	if (src == relay_data::from_raw) {
		// send to ssl
		// BOOST_LOG_TRIVIAL(info) << " send raw stop : "<<session;
		auto buffer = std::make_shared<relay_data>(session, relay_data::STOP_RELAY);
		send_data_on_ssl(buffer);
	}
}


// send data on ssl sock
// buf is read from sock in raw_relay
void ssl_relay::send_data_on_ssl(std::shared_ptr<relay_data> buf)
{
//	BOOST_LOG_TRIVIAL(info) << "send sess"<<buf->session()<<", cmd"<<buf->cmd()<<",len"<<buf->data_size()<<"  on ssl ";
//	BOOST_LOG_TRIVIAL(info) << "head: "<< buf_to_string( buf->header_buffer().data(), buf->header_buffer().size());
//	BOOST_LOG_TRIVIAL(info) << " "<< buf_to_string( buf->data_buffer().data(), buf->data_buffer().size());
	if (_ssl_status == SSL_CLOSED) {
		return;
	}
	_timeout_wr = TIMEOUT;
	auto relay = _relays.find(buf->session());
	if (relay == _relays.end()) {
		return;
	}
	relay->second->timeout = TIMEOUT;

	_bufs.push(buf);
	BOOST_LOG_TRIVIAL(info) << "send sess"<<buf->session()<<"  on ssl," << _bufs.size()<<"bufs";
	if (_bufs.size() > 1) {
		return;
	}
	if (_ssl_status == NOT_START) {
		// start ssl connect
		_ssl_status = SSL_CONNECT;
		ssl_connect_start();
		return;
	}
// start ssl_write routine
	ssl_data_send();
}

// local ssl relay server functions
// call add_new_relay, vector access, must run in ssl_relay strand
void ssl_relay::local_handle_accept(std::shared_ptr<raw_relay> relay)
{
	if (_ssl_status == SSL_CLOSED ) {
		BOOST_LOG_TRIVIAL(error) <<" SSL closed  ";
		return ;
	}

	add_new_relay(relay);
	auto task =
		_config.type == LOCAL_SERVER ?
		std::bind(&raw_relay::local_start, relay) :
		std::bind(&raw_relay::transparent_start, relay);
	relay->get_strand().post(task, asio::get_associated_allocator(task));
}

uint32_t ssl_relay::add_new_relay(const std::shared_ptr<raw_relay> &relay)
{
	uint32_t session = 0;
	do {
		auto ran = _rand();
//		auto tmp = std::chrono::system_clock::now().time_since_epoch().count();
		auto tmp = time(nullptr);

		session = (ran & 0xffff0000) | (tmp & 0xffff);
//		BOOST_LOG_TRIVIAL(info) << " raw relay construct new session: "<<session;
	} while ( _relays.count(session) );

	relay->session(session);
	_relays.emplace(session, std::make_shared<_relay_t>(relay));
	return session;
}

void ssl_relay::ssl_data_send()
{
	auto self(shared_from_this());
	asio::spawn(_strand, [this, self](asio::yield_context yield) {
		try {
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
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "ssl write error: "<<error.what();
			stop_ssl_relay(0, relay_data::ssl_err);
		}
	});
}

void ssl_relay::do_ssl_data(std::shared_ptr<relay_data>& buf)
{
	auto session = buf->session();
	if (buf->cmd() == relay_data::KEEP_RELAY) {
		_timeout_kp = TIMEOUT;
	} else {
		_timeout_rd = TIMEOUT;
	}
	if ( buf->cmd() == relay_data::DATA_RELAY) {
		auto val = _relays.find(session);
		if (val == _relays.end() ) { // session stopped, tell remote to STOP
			stop_ssl_relay(session, relay_data::from_raw);
		} else {
			auto relay = val->second->relay;
			auto raw_data_send = std::bind(&raw_relay::send_data_on_raw, relay, buf);
			relay->get_strand().post(raw_data_send, asio::get_associated_allocator(raw_data_send));
		}
	} else if (buf->cmd() == relay_data::START_CONNECT) { // remote get start connect
		auto relay = std::make_shared<raw_relay> (_io_context, shared_from_this(), session);
		_relays[session] = std::make_shared<_relay_t>(relay);
		auto start_task = std::bind(&raw_relay::start_remote_connect, relay, buf);
		relay->get_strand().post(start_task, asio::get_associated_allocator(start_task));
	} else if (buf->cmd() == relay_data::START_RELAY) {
//		BOOST_LOG_TRIVIAL(info) << session <<" START RELAY: ";
		auto val = _relays.find(session);
		if (val == _relays.end() ) { // local stopped before remote connect, tell remote to stop
			stop_ssl_relay(session, relay_data::from_raw);
		} else {
			// local get start from remote, tell raw relay begin
			auto relay = val->second->relay;
			auto start_task = std::bind(&raw_relay::start_data_relay, relay);
			relay->get_strand().post(start_task, asio::get_associated_allocator(start_task));
		}
	} else if (buf->cmd() == relay_data::STOP_RELAY) { // post stop to raw
		_relays.erase(session);
	}
}

void ssl_relay::ssl_data_read()
{
	auto self(shared_from_this());
	asio::spawn(_strand, [this, self](boost::asio::yield_context yield) {
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
				do_ssl_data(buf);
			}
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "ssl read error: "<<error.what();
			stop_ssl_relay(0, relay_data::ssl_err);
		}
	});

}
void ssl_relay::ssl_connect_start()
{
	auto self(shared_from_this());

	auto ssl_start = [this, self](asio::yield_context yield) {
		try {
			if (_config.type != REMOTE_SERVER) {
				_sock.lowest_layer().async_connect(_remote, yield);
			}
			_sock.lowest_layer().set_option(tcp::no_delay(true));
			_sock.async_handshake(
				_config.type == REMOTE_SERVER ? ssl_socket::server : ssl_socket::client,
				yield);
			_ssl_status = SSL_START;
			// start buffer write routine
			ssl_data_send();
			// start ssl read routine
			ssl_data_read();

		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "ssl connect error: "<<error.what();
			stop_ssl_relay(0, relay_data::ssl_err);
		}
	};
	asio::spawn(_strand, ssl_start);
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
	if (_ssl_status == NOT_START)
		return;

	_timeout_rd--;
	_timeout_wr--;
//	_timeout_kp--;
	if (_timeout_rd < 0 && (_timeout_wr < 0 || _timeout_kp < 0)) {
		// shutdown
//		BOOST_LOG_TRIVIAL(info) << "ssl timeout: "<< _timeout_rd<< " "<<_timeout_wr<< " "<< _timeout_kp;
		stop_ssl_relay(0, relay_data::ssl_err);
	}
//	BOOST_LOG_TRIVIAL(info) << " ssl relay timer handle : "<< _timeout_rd<< " "<<_timeout_wr<< " "<< _timeout_kp;
}
