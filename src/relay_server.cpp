#include "relay_server.hpp"
#include <boost/asio/spawn.hpp>

// local tcp server
void relay_server::local_tcp_server_start()
{
	asio::spawn(_strand, [this](asio::yield_context yield) {
		auto ssl_ptr = std::make_shared<ssl_relay> (&_io_context, _config);
		_ssl_relays.emplace_back(ssl_ptr);
		while (true) {
			try {
				auto new_relay = std::make_shared<raw_tcp> (&_io_context, ssl_ptr);
				_acceptor.async_accept(new_relay->get_sock(), yield);
				auto task = std::bind(&ssl_relay::local_handle_accept, ssl_ptr, new_relay);
				ssl_ptr->get_strand().post(task, asio::get_associated_allocator(task));
			} catch (boost::system::system_error& error) {
				BOOST_LOG_TRIVIAL(error) << "local accept error: "<<error.what();
			}
		}
	});

}

void relay_server::remote_server_start()
{
	asio::spawn(_strand, [this](asio::yield_context yield) {
		while (true) {
			try {
				auto ssl_ptr = std::make_shared<ssl_relay> (&_io_context, _config);
				_acceptor.async_accept(ssl_ptr->get_sock().lowest_layer(), yield);
//						       std::bind(&relay_server::remote_handle_accept, this, ssl_ptr, std::placeholders::_1));
				auto task = std::bind(&ssl_relay::ssl_connect_start, ssl_ptr);
				ssl_ptr->get_strand().post(task, asio::get_associated_allocator(task));
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
	}
	start_timer();

}
void relay_server::run()
{
	try {
		_io_context.run();
	} catch (std::exception & e) {
		BOOST_LOG_TRIVIAL(error) << "server run error: "<<e.what();
	} catch (...) {
		BOOST_LOG_TRIVIAL(error) << "server run error with unkown exception ";
	}
}

void relay_server::start_timer()
{
	asio::spawn(_strand, [this](asio::yield_context yield) {
		while (true) {
			_timer.expires_after(std::chrono::seconds(10));
			_timer.async_wait(yield);

			for (auto relay = _ssl_relays.begin(); relay != _ssl_relays.end();) {
				if (auto ssl_relay = relay->lock()) {
					auto ssl_timer = std::bind(&ssl_relay::timer_handle, ssl_relay);
					ssl_relay->get_strand().post(ssl_timer, asio::get_associated_allocator(ssl_timer));
					relay++;
				} else {
					BOOST_LOG_TRIVIAL(info) << " main timer erase ";
					relay = _ssl_relays.erase(relay);
				}
			}
		}
	});
}
