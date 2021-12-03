#include <boost/asio/spawn.hpp>
#include "ssl_relay.hpp"
#include "raw_tcp.hpp"
#include "raw_udp.hpp"
#include "relay_server.hpp"

struct relay_server::server_impl
{
    server_impl(asio::io_context &io, const relay_config &config):
        _config(config), _io(io),
		_acceptor(io, tcp::v4()),
        _strand(io.get_executor())
    {
        try {
            _acceptor.set_option(tcp::acceptor::reuse_address(true));
            _acceptor.set_option(tcp::acceptor::keep_alive(true));
            if (config.type == LOCAL_TRANSPARENT)
                _acceptor.set_option(_ip_transparent_t(true));
            _acceptor.bind(tcp::endpoint(tcp::v4(), config.local_port));
            _acceptor.listen();
        } catch (boost::system::system_error& error) {
            BOOST_LOG_TRIVIAL(error) << "relay server init error: "<<error.what();
        }
	}
    ~server_impl() = default;

    void impl_add_new_tcp(const std::shared_ptr<raw_tcp> &new_tcp);

	relay_config _config;
	asio::io_context &_io;
	tcp::acceptor _acceptor;
	asio::strand<asio::io_context::executor_type> _strand;

	std::weak_ptr<ssl_relay> _ssl_tcp;
	std::weak_ptr<ssl_relay> _ssl_udp;
};

// add new tcp relay to ssl relay
// if no ssl relay, start new ssl connection
void relay_server::server_impl::impl_add_new_tcp(const std::shared_ptr<raw_tcp> &new_tcp)
{
    auto ssl_ptr = _ssl_tcp.lock();
    if (ssl_ptr == nullptr) {
        ssl_ptr = std::make_shared<ssl_relay> (_io, _config);
        // _ssl_relays.push_back(ssl_ptr);
        // init and connect to remote
        ssl_ptr->start_relay();
    }
    ssl_ptr->add_raw_tcp(new_tcp);
}
relay_server::relay_server(asio::io_context &io, const relay_config &config):
    _impl(std::make_unique<server_impl>(io, config))
{
}
relay_server::~relay_server() = default;

// local udp server
void relay_server::local_udp_server_start()
{
    // auto new_relay = std::make_shared<raw_udp> (_impl->_io, nullptr);
	// asio::spawn(_strand, [this](asio::yield_context yield) {
		// auto ssl_ptr = std::make_shared<ssl_relay> (&_io_context, _config);
		// _ssl_udp_relays.emplace_back(ssl_ptr);
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
    asio::spawn(_impl->_strand, [this](asio::yield_context yield) {
		while (true) {
			try {
				auto new_relay = std::make_shared<raw_tcp> (_impl->_io, _impl->_config.type);
				_impl->_acceptor.async_accept(new_relay->get_sock(), yield);
                _impl->impl_add_new_tcp(new_relay);
			} catch (boost::system::system_error& error) {
				BOOST_LOG_TRIVIAL(error) << "local accept error: "<<error.what();
			}
		}
	});

}

void relay_server::remote_server_start()
{
    asio::spawn(_impl->_strand, [this](asio::yield_context yield) {
		while (true) {
			try {
				auto ssl_ptr = std::make_shared<ssl_relay> (_impl->_io, _impl->_config);
				_impl->_acceptor.async_accept(ssl_ptr->get_sock().lowest_layer(), yield);
                ssl_ptr->start_relay();
				// _impl->_ssl_relays.emplace_back(ssl_ptr);
			} catch (boost::system::system_error& error) {
				BOOST_LOG_TRIVIAL(error) << "remote accept error: "<<error.what();
			}
		}
	});

}
void relay_server::start_server()
{
	if (_impl->_config.type == REMOTE_SERVER) {
		remote_server_start();
	} else {
		local_tcp_server_start();
        // local_udp_server_start();
	}
}
void relay_server::server_run()
{
	try {
		_impl->_io.run();
	} catch (std::exception & e) {
		BOOST_LOG_TRIVIAL(error) << "server run error: "<<e.what();
	} catch (...) {
		BOOST_LOG_TRIVIAL(error) << "server run error with unkown exception ";
	}
}

