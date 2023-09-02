#include <queue>
#include <boost/asio/spawn.hpp>
#include "relay_manager.hpp"
#include "raw_tcp.hpp"
#include "raw_udp.hpp"
#include "ssl_relay.hpp"

static std::shared_ptr<ssl_relay> start_new_ssl_relay(asio::io_context &io, const relay_config &config)
{
	// TBD
	auto relay = std::make_shared<ssl_relay>(io, config);

	return relay;
}

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
	std::map<udp::endpoint, uint32_t> _srcs;

	relay_state_t _state;

	uint32_t _session = 1;
	// void req_dispatch();
	// void res_dispatch();
	void stop_manager();
	void analyze_res_data(const std::shared_ptr<relay_data>& buf);

	void local_transparent_send_udp(const std::shared_ptr<relay_data>& buf);
	void remote_server_send_udp(const std::shared_ptr<relay_data>& buf);
	void remote_server_start_tcp(const std::shared_ptr<relay_data>& buf);

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
		if (ssl == nullptr || ssl->get_state() == RELAY_STOP) {
			if (_impl->_config.type == REMOTE_SERVER) {
				_impl->stop_manager();
				return;
			}
			// local servers, no valid ssl connection, start new
			ssl = start_new_ssl_relay(_impl->_io, _impl->_config);
		}
		ssl->send_data(buf);
	});
}
void relay_manager::manager_impl::local_transparent_send_udp(const std::shared_ptr<relay_data>& buf)
{
	// TBD
// send udp
	auto udp_send = _relays[1];
	if (!udp_send->alive()) {
		// TBD
		// some error occur, create new udp_send
		// _impl->_relays[1] = new_udp_send();
	}
	udp_send->send_data(buf);
}
void relay_manager::manager_impl::remote_server_send_udp(const std::shared_ptr<relay_data>& buf)
{
	auto session = buf->session();
	auto &relay = _relays[session];
	if (relay == nullptr || !relay->alive()) {
		// TBD add new raw_udp and send_data
		// relay = impl_add_raw_udp(session);
	}
	relay->send_data(buf);
	// BOOST_LOG_TRIVIAL(info) << "ssl send raw udp data" << session;
}
void relay_manager::manager_impl::remote_server_start_tcp(const std::shared_ptr<relay_data>& buf)
{
	// TBD
	auto session = buf->session();
	auto &relay = _relays[session];
	if (relay != nullptr) {
		relay->stop_relay();
	}
	auto[host, port] = parse_address(buf->data_buffer().data(), buf->data_size());
	// TBD add new raw tcp
	// _owner->add_raw_tcp(nullptr, session, host, port);
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
				relay->send_data(buf);
			}
		} else if (buf->cmd() == relay_data::DATA_UDP) {
			if (_impl->_config.type == LOCAL_TRANSPARENT) {
				_impl->local_transparent_send_udp(buf);
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


// void relay_manager::manager_start()
// {
//	if (_impl->_config.type == LOCAL_TRANSPARENT || _impl->_config.type == LOCAL_SERVER) {
//		local_accept();
//		// TBD
//		// local_udp_receive();
//		// TBD
//		// local_udp_send();
//	} else if (_impl->_config.type == REMOTE_SERVER) { //TBD remote

//	}
//	_impl->req_dispatch();
//	_impl->res_dispatch();

//	// TBD
//	// start timer check
// }

// // add raw_tcp:
// // in local server: relay_server create and connect on raw_tcp, call with sess=0, ssl_relay create new session
// // in remote server: ssl_relay get TCP_CONNECT cmd with session, create new raw_tcp with session
// void relay_manager::add_raw_tcp(const std::shared_ptr<raw_tcp> tcp_relay, uint32_t sess, const std::string &host, const std::string &service)
// {
//	auto self(shared_from_this());
//	run_in_strand(_impl->_strand, [this, self, tcp_relay, sess, host, service]() {
//		auto relay = tcp_relay;
//		auto session = sess;
//		if (session == 0) { // create session from relay_server
//			session = _impl->_session++;
//			if (_impl->_relays.count(session)) {
//				BOOST_LOG_TRIVIAL(error) << "relay session repeat: "<<session;
//				// TBD error
//				// internal_stop_relay();
//				return;
//			}
//		} else { // remote create relay from ssl relay==nullptr
//			relay = std::make_shared<raw_tcp> (_impl->_io_context, type(), host, service);
//		}
//		relay->session(session);
//		relay->manager(std::static_pointer_cast<ssl_relay>(self));
//		_impl->_tcp_relays[session] = relay;

//		relay->start_relay();
//	});
// }

// uint32_t ssl_relay::ssl_impl::impl_add_raw_udp(uint32_t session, const udp::endpoint &src)
// {
//     if (session == 0) {
//         session = _session++;
//         _srcs[src] = session;
//     }
//     // BOOST_LOG_TRIVIAL(info) << "ssl add raw udp session"<<session<<" from"<<src;
//     if (_owner->type() == REMOTE_SERVER) {
//         auto relay = std::make_shared<raw_udp>(_io_context, _owner->type(), src);
//         relay->session(session);
//         relay->manager(std::static_pointer_cast<ssl_relay> (_owner->shared_from_this()));
//         relay->start_relay();
//         _udp_relays[session] = relay;
//     } else {
//         // _local_udp.add_peer(session, src);
//         _local_udp->add_peer(session, src);
//     }
//     _timeout[session] = TIMEOUT_COUNT;
//     return session;
// }
