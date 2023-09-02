#include "relay_acceptor.hpp"
#include "relay_manager.hpp"
#include "raw_tcp.hpp"

using namespace std;

struct relay_acceptor::acceptor_impl
{
	acceptor_impl(asio::io_context &io, const relay_config &config):
		_config(config), _io(io),
		_acceptor(io, tcp::v6()),
		_strand(io.get_executor())
	{
		try {
			_acceptor.set_option(tcp::acceptor::reuse_address(true));
			_acceptor.set_option(tcp::acceptor::keep_alive(true));
			if (config.type == LOCAL_TRANSPARENT) {
				_acceptor.set_option(_ip_transparent_t(true));
			}
			_acceptor.bind(tcp::endpoint(tcp::v6(), config.local_port));
			_acceptor.listen();
		} catch (boost::system::system_error& error) {
			BOOST_LOG_TRIVIAL(error) << "relay server init error: "<<error.what();
		}
	}
	~acceptor_impl() = default;

	const relay_config &_config;
	asio::io_context &_io;
	tcp::acceptor _acceptor;
	asio::strand<asio::io_context::executor_type> _strand;

	shared_ptr<relay_manager> _manager;
	void local_accept();
	void remote_accept();
	void local_udp_accept();
};

relay_acceptor::relay_acceptor(asio::io_context &io, const relay_config &config):
	_impl(std::make_unique<acceptor_impl>(io, config))
{
}
relay_acceptor::~relay_acceptor() = default;


void relay_acceptor::acceptor_impl::local_accept()
{
	asio::spawn(_strand, [this](asio::yield_context yield) {
		while (true) {
			try {
				auto new_relay = make_shared<raw_tcp> (_io, _config);
				_acceptor.async_accept(new_relay->get_sock(), yield);
				if (_manager == nullptr) {
					// TBD throw error
				}
				BOOST_LOG_TRIVIAL(info) << "relay_server : add new tcp";
				_manager->add_raw_tcp(new_relay);
			} catch (boost::system::system_error& error) {
				BOOST_LOG_TRIVIAL(error) << "local accept error: "<<error.what();
				throw error;
			}
		}
	});
}
void relay_acceptor::acceptor_impl::local_udp_accept()
{

}
void relay_acceptor::start_accept()
{
	if (_impl->_config.type == REMOTE_SERVER) {
		_impl->remote_accept();
	} else if (_impl->_config.type == LOCAL_TRANSPARENT) {
		_impl->local_udp_accept();
		_impl->local_accept();
	} else if (_impl->_config.type == LOCAL_SERVER){
		_impl->local_accept();
	}
}
