#include "relay_server.hpp"
#include <boost/asio/spawn.hpp>

struct relay_server::server_impl
{
    server_impl(asio::io_context *io, const relay_config &config):
        _config(config), _io(io),
		_acceptor(*io, tcp::v4()),
        _strand(*io.get_executor()),
		_remote(asio::ip::make_address(config.remote_ip), config.remote_port),_timer(*io) {

        _acceptor.set_option(tcp::acceptor::reuse_address(true));
        _acceptor.set_option(tcp::acceptor::keep_alive(true));
        _acceptor.set_option(_ip_transparent_t(true));
        _acceptor.bind(tcp::endpoint(tcp::v4(), config.local_port));
        _acceptor.listen();
	}
    ~server_impl() = default;

    void impl_add_new_tcp(const std::shared_ptr<raw_tcp> &new_tcp);
    
	relay_config _config;

	asio::io_context *_io;
	tcp::acceptor _acceptor;
	// asio::strand<asio::io_context::executor_type> _strand;
//	ssl::context _ctx;

    // for tcp listen and accept
	tcp::endpoint _remote;
	asio::steady_timer _timer;

	std::list<std::weak_ptr<ssl_relay>> _ssl_relays;
};

// add new tcp relay to ssl relay
// if no ssl relay, start new ssl connection
void relay_server::server_impl::impl_add_new_tcp(const std::shared_ptr<raw_tcp> &new_tcp)
{
    std::shared_ptr<ssl_relay> ssl_ptr = nullptr;
    if (!_ssl_relays.empty()) {
        ssl_ptr = _ssl_relays.front().lock();
    }
    if (ssl_ptr == nullptr) {
        ssl_ptr = std::make_shared<ssl_relay> (_io, _config);
        _ssl_relays.push_back(ssl_ptr);
        // init and connect to remote
        ssl_ptr->start_connect(_config->type);
    }
    new_tcp->manager(ssl_ptr);
    ssl_ptr->add_raw_tcp(new_tcp);
}
relay_server::relay_server(asio::io_context *io, const relay_config &config):
    base_relay(io), _impl(io, config)
{
}
// local udp server
void relay_server::local_udp_server_start()
{
    auto new_relay = std::make_shared<raw_udp> (_impl->_io, nullptr);
	// asio::spawn(_strand, [this](asio::yield_context yield) {
		auto ssl_ptr = std::make_shared<ssl_relay> (&_io_context, _config);
		_ssl_relays.emplace_back(ssl_ptr);
		// while (true) {
		// 	try {
		// 		// auto new_relay = std::make_shared<raw_tcp> (&_io_context, ssl_ptr);
		// 		// _acceptor.async_accept(new_relay->get_sock(), yield);
		// 		// auto task = std::bind(&ssl_relay::local_handle_accept, ssl_ptr, new_relay);
		// 		// ssl_ptr->strand().post(task, asio::get_associated_allocator(task));
		// 	} catch (boost::system::system_error& error) {
		// 		BOOST_LOG_TRIVIAL(error) << "local accept error: "<<error.what();
		// 	}
		// }
	// });

}

// local tcp server
void relay_server::local_tcp_server_start()
{
    spawn_in_strand([this](asio::yield_context yield) {
		// auto ssl_ptr = std::make_shared<ssl_relay> (_impl->_io, _impl->_config);
		// _ssl_relays.emplace_back(ssl_ptr);
		while (true) {
			try {
				auto new_relay = std::make_shared<raw_tcp> (_impl->_io, nullptr);
				_impl->_acceptor.async_accept(new_relay->get_sock(), yield);
                _impl->impl_add_new_tcp(new_relay);
				// auto task = std::bind(&ssl_relay::local_handle_accept, ssl_ptr, new_relay);
				// ssl_ptr->strand().post(task, asio::get_associated_allocator(task));
			} catch (boost::system::system_error& error) {
				BOOST_LOG_TRIVIAL(error) << "local accept error: "<<error.what();
			}
		}
	});

}

void relay_server::remote_server_start()
{
    spawn_in_strand( [this](asio::yield_context yield) {
		while (true) {
			try {
				auto ssl_ptr = std::make_shared<ssl_relay> (_impl->_io_context, _impl->_config);
				_impl->_acceptor.async_accept(ssl_ptr->get_sock().lowest_layer(), yield);
//						       std::bind(&relay_server::remote_handle_accept, this, ssl_ptr, std::placeholders::_1));
                ssl_ptr->start_connect(REMOTE_SERVER);
				// auto task = std::bind(&ssl_relay::ssl_connect_start, ssl_ptr);
				// ssl_ptr->strand().post(task, asio::get_associated_allocator(task));
				_ssl_relays.emplace_back(ssl_ptr);

			} catch (boost::system::system_error& error) {
				BOOST_LOG_TRIVIAL(error) << "remote accept error: "<<error.what();
			}
		}
	});

}
void relay_server::start_server()
{
	if (_config.type == REMOTE_SERVER) {
		remote_server_start();
	} else {
		local_tcp_server_start();
        local_udp_server_start();
	}
	start_timer();

}
// void relay_server::run()
// {
// 	try {
// 		_io_context.run();
// 	} catch (std::exception & e) {
// 		BOOST_LOG_TRIVIAL(error) << "server run error: "<<e.what();
// 	} catch (...) {
// 		BOOST_LOG_TRIVIAL(error) << "server run error with unkown exception ";
// 	}
// }

void relay_server::start_timer()
{
	asio::spawn(_strand, [this](asio::yield_context yield) {
		while (true) {
			_timer.expires_after(std::chrono::seconds(10));
			_timer.async_wait(yield);

			for (auto relay = _ssl_relays.begin(); relay != _ssl_relays.end();) {
				if (auto ssl_relay = relay->lock()) {
					ssl_relay->timer_handle();
					relay++;
				} else {
					BOOST_LOG_TRIVIAL(info) << " main timer erase ";
					relay = _ssl_relays.erase(relay);
				}
			}
		}
	});
}
