#include <queue>
#include <boost/asio/spawn.hpp>
#include "relay_manager.hpp"
#include "raw_tcp.hpp"
#include "raw_udp.hpp"
#include "ssl_relay.hpp"

struct udp_timeout
{
	uint32_t session;
	int timeout;
};

struct relay_manager::manager_impl
{
	manager_impl(relay_manager *owner, asio::io_context &io, const relay_config &config):
		_config(config),
		_owner(owner),
		_io(io),
		_strand(io.get_executor()),
		_state(RELAY_START)
	{

	}
	~manager_impl() = default;

	const relay_config & _config;
	relay_manager *_owner;
	asio::io_context &_io;
	asio::strand<asio::io_context::executor_type> _strand;

	std::shared_ptr<ssl_relay> _ssl;
	std::map<uint32_t, std::shared_ptr<raw_relay>> _relays;

	std::shared_ptr<raw_udp> _udp_send;
	std::map<udp::endpoint, udp_timeout> _srcs;
	std::map<uint32_t, udp::endpoint> _udp_srcs;

	relay_state_t _state;

	uint32_t _session = 1;
	void stop_manager();
	void analyze_res_data(const std::shared_ptr<relay_data>& buf);

	void local_transparent_send_udp(const std::shared_ptr<relay_data>& buf);
	void remote_server_send_udp(const std::shared_ptr<relay_data>& buf);
	void remote_server_start_tcp(const std::shared_ptr<relay_data>& buf);
	void add_remote_raw_tcp(uint32_t session, const std::string &host, const std::string &service);

	std::shared_ptr<raw_relay> add_remote_raw_udp(uint32_t session);//, const std::string &host, const std::string &service);
	// void add_local_raw_udp(const udp::endpoint&);

	void start_timer();
	void attach_ssl(const std::shared_ptr<ssl_relay> &ssl)
	{
		_ssl = ssl;
	}

};

relay_manager::relay_manager(asio::io_context &io, const relay_config &config):
	_impl(std::make_unique<manager_impl>(this, io, config))
{
}
relay_manager::~relay_manager() = default;

void relay_manager::add_request(const std::shared_ptr<relay_data> buf)
{
	auto self(shared_from_this());
	run_in_strand(_impl->_strand, [this, self, buf](){
		auto &ssl = _impl->_ssl;
		if (ssl == nullptr || ssl->state == RELAY_STOP) {
			if (_impl->_config.type == REMOTE_SERVER) {
				_impl->stop_manager();
				return;
			}
			// local servers, no valid ssl connection, start new
			ssl = std::make_shared<ssl_relay>(_impl->_io, _impl->_config, self);
			ssl->start_relay();
		}
		ssl->send_data(buf);
	});
}
void relay_manager::manager_impl::local_transparent_send_udp(const std::shared_ptr<relay_data>& buf)
{
	if (_udp_send->state == RELAY_STOP) {
		// TBD
		// some error occur, create new udp_send
		// _impl->_relays[0] = new_udp_send();
		BOOST_LOG_TRIVIAL(error) << " udp send is stopeed" ;
	}
	BOOST_LOG_TRIVIAL(info) << " add to udp send" ;
	_udp_send->send_data(buf);
}
void relay_manager::manager_impl::remote_server_send_udp(const std::shared_ptr<relay_data>& buf)
{
	auto session = buf->session();
	auto &relay = _relays[session];
	if (relay == nullptr || relay->state == RELAY_STOP) {
		relay = add_remote_raw_udp(session);
	}
	relay->send_data(buf);
	// BOOST_LOG_TRIVIAL(info) << "ssl send raw udp data" << session;
}
void relay_manager::manager_impl::remote_server_start_tcp(const std::shared_ptr<relay_data>& buf)
{
	auto session = buf->session();
	auto &relay = _relays[session];
	if (relay != nullptr) {
		// TBD repeat session
		relay->stop_relay();
	}
	auto[host, port] = parse_address(buf->data_buffer().data(), buf->data_size());
	add_remote_raw_tcp(session, host, port);
}
void relay_manager::add_response(const std::shared_ptr<relay_data> buf)
{
	auto self(shared_from_this());
	run_in_strand(_impl->_strand, [this, self, buf](){
		if ( buf->cmd() == relay_data::DATA_TCP) { // tcp data
			auto session = buf->session();
			auto tcp_session = _impl->_relays.find(session);
			if (tcp_session != _impl->_relays.end()) {
				auto& [ignored, relay] = *tcp_session;
				if (relay->state == RELAY_STOP) {
					BOOST_LOG_TRIVIAL(info) << "get tcp data on stoped session:" << session;
				} else {
					relay->send_data(buf);
				}
			} else {
				BOOST_LOG_TRIVIAL(info) << "get tcp data on unkown session:" << session;
			}
		} else if (buf->cmd() == relay_data::DATA_UDP) {
			BOOST_LOG_TRIVIAL(info) << "get udp data on  session:" << buf->session();
			if (_impl->_config.type == LOCAL_TRANSPARENT) {
				_impl->local_transparent_send_udp(buf);
				_impl->_srcs[_impl->_udp_srcs[buf->session()]].timeout = TIMEOUT;
			} else if (_impl->_config.type == REMOTE_SERVER) {
				_impl->remote_server_send_udp(buf);
			}
		} else if (buf->cmd() == relay_data::START_TCP) { // remote get start connect
			_impl->remote_server_start_tcp(buf);
		}
	});
}

void relay_manager::manager_impl::stop_manager()
{
	for (auto &[session, relay]:_relays) {
		if (relay)
			relay->stop_relay();
	}
	_relays.clear();
	_state = RELAY_STOP;
}


void relay_manager::manager_impl::start_timer()
{
	auto owner(_owner->shared_from_this());
	asio::spawn(_strand, [this, owner](asio::yield_context yield) {
		asio::steady_timer timer(_io);
		while (true) {
			if (_state == RELAY_STOP) return;
			timer.expires_after(std::chrono::seconds(RELAY_TICK));
			boost::system::error_code err;
			timer.async_wait(yield[err]);
			if (err == asio::error::operation_aborted) {
				return;
			}

			if (_ssl != nullptr
				&& _ssl->timeout_down() == 0) {
				if ( _config.type == REMOTE_SERVER) {
					stop_manager();
					return;
				} else {
					BOOST_LOG_TRIVIAL(info) << " udp clear ";
					_udp_srcs.clear();
					_srcs.clear();
					_udp_send->del_peer(0);
				}
			}

			for (auto iter = _srcs.begin(); iter != _srcs.end();) {
				auto &src = iter->second;
				if (--src.timeout <= 0) {
					BOOST_LOG_TRIVIAL(info) <<src.session<< " udp erase ";
					_udp_srcs.erase(src.session);
					_udp_send->del_peer(src.session);
					iter = _srcs.erase(iter);
				} else {
					++iter;
				}
			}
			for (auto iter = _relays.begin(); iter != _relays.end();) {
				auto &relay = iter->second;
				if (relay == nullptr
					|| relay->timeout_down() == 0) {
					iter = _relays.erase(iter);
				} else {
					++iter;
				}
			}
		}
	});
}

void relay_manager::manager_impl::add_remote_raw_tcp(uint32_t sess, const std::string &host, const std::string &service)
{

	auto relay = std::make_shared<raw_tcp> (_io, _config, _owner->shared_from_this(), host, service);
	relay->session = sess;
	_relays[sess] = relay;
	relay->start_relay();
}
// add raw_tcp:
// in local server: relay_server create and connect on raw_tcp, call with sess=0, ssl_relay create new session
// in remote server: ssl_relay get TCP_CONNECT cmd with session, create new raw_tcp with session
void relay_manager::add_local_raw_tcp(const std::shared_ptr<raw_tcp> &tcp_relay)
{
	auto self(shared_from_this());
	run_in_strand(_impl->_strand, [this, self, tcp_relay]() {
		auto relay = tcp_relay;
		auto session = _impl->_session++;
		if (_impl->_relays.count(session)) {
			BOOST_LOG_TRIVIAL(error) << "relay session repeat: "<<session;
			// TBD should not happen
			return;
		}
		relay->session = session;
		_impl->_relays[session] = relay;
		relay->start_relay();
	});
}

std::shared_ptr<raw_relay> relay_manager::manager_impl::add_remote_raw_udp(uint32_t session)
{
	auto mngr = _owner->shared_from_this();
	auto relay = std::make_shared<raw_udp>(_io, _config, mngr);
	relay->session = session;
	relay->start_relay();
	return relay;
}
void relay_manager::manager_start()
{
	_impl->start_timer();
	if (_impl->_config.type != REMOTE_SERVER) {
		// create local udp relay
		_impl->_udp_send = std::make_shared<raw_udp>(_impl->_io, _impl->_config, shared_from_this());
		_impl->_udp_send->start_send();
	}
}

void relay_manager::send_udp_data(const udp::endpoint src , const std::shared_ptr<relay_data> buf)
{
	auto self(shared_from_this());
	run_in_strand(_impl->_strand, [this, self, src, buf]{
		auto &sess = _impl->_srcs[src];
		if (sess.session == 0) {
			sess.session = ++_impl->_session;
			// auto udp_send = std::static_pointer_cast<raw_udp>( _impl->_relays[0]);
			_impl->_udp_send->add_peer(sess.session, src);
			_impl->_udp_srcs.insert({sess.session, src});
		}
		sess.timeout = TIMEOUT;
		buf->session(sess.session);
		add_request(buf);
		BOOST_LOG_TRIVIAL(info) << "send udp data:" << sess.session;
	});
}
void relay_manager::set_ssl(const std::shared_ptr<ssl_relay> &ssl)
{
	_impl->attach_ssl(ssl);
}
