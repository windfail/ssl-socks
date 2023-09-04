#include <boost/format.hpp>
#include <boost/asio/spawn.hpp>
#include <unordered_map>
#include "raw_udp.hpp"
#include "ssl_relay.hpp"
#include "relay.hpp"
#include "relay_manager.hpp"

std::string buf_to_string(void *buf, std::size_t size);

struct raw_udp::udp_impl
{
    explicit udp_impl(raw_udp *owner, asio::io_context &io):
        _owner(owner),
        _sock(io, udp::v6())
        // _remote(dst)
        // , _host_resolver(io, udp::v6())
    {}
    ~udp_impl() =default;

    raw_udp *_owner;
    udp::socket _sock;
    // udp::resolver _host_resolver;
    udp::endpoint _remote;

    udp::endpoint _local;
    std::unordered_map<uint32_t, udp::endpoint> _peers;
    void impl_start_recv();
	udp::endpoint& get_send_data_addr(const std::shared_ptr<relay_data> buf);
};
void raw_udp::add_peer(uint32_t session, const udp::endpoint & peer)
{
    auto self(shared_from_this());
    run_in_strand(strand, [this, self, session, peer]() {
        _impl->_peers[session] = peer;
    });
}
void raw_udp::del_peer(uint32_t session)
{
    _impl->_peers.erase(session);
}
// remote raw_udp start recv
// local trasparent recv in acceptor
void raw_udp::udp_impl::impl_start_recv()
{
    auto owner(_owner->shared_from_this());
    asio::spawn(_owner->strand, [this, owner](asio::yield_context yield){
        try {
            while (true) {
                udp::endpoint peer;
                auto buf = std::make_shared<relay_data>();
                // BOOST_LOG_TRIVIAL(info) <<_owner->session()<< " raw udp recv at: "<< _sock.local_endpoint();
                auto len = _sock.async_receive_from(buf->udp_data_buffer(), peer, yield);
                parse_addr(buf->data_buffer().data(), peer.data());
                buf->session(_owner->session);
                // BOOST_LOG_TRIVIAL(info) << _owner->session()<<" raw udp read len: "<< len<<" from "<<peer<<"local"<<_sock.local_endpoint();
                // BOOST_LOG_TRIVIAL(info) << buf_to_string(buf->udp_data_buffer().data(), len);
                // post to manager
                buf->resize_udp(len);
                auto mngr = _owner->manager.lock();
                mngr->add_request(buf);
            }
        } catch (boost::system::system_error& error) {
            BOOST_LOG_TRIVIAL(error) << _owner->session<<" udp raw read error: "<<error.what();
            _owner->stop_relay();
        }
    });
}
// remote raw udp
raw_udp::raw_udp(asio::io_context &io, const relay_config& config):
    raw_relay(io, config), _impl(std::make_unique<udp_impl>(this, io))
{
    BOOST_LOG_TRIVIAL(info) << "raw udp construct ";
}
// raw_udp::raw_udp(asio::io_context &io, server_type type, const std::string &host, const std::string &service):
// 	raw_relay(io, config), _impl(std::make_unique<udp_impl>(this, io, host, service))
// {
//     BOOST_LOG_TRIVIAL(info) << "raw udp construct ";
// }

raw_udp::~raw_udp()
{
    BOOST_LOG_TRIVIAL(info) << "raw udp destruct: "<<session;
}

void raw_udp::stop_relay()
{
    auto self(shared_from_this());
    run_in_strand(strand, [this, self](){
	    if (config.type == LOCAL_TRANSPARENT) {
		    // do not stop, restart start_send
		    BOOST_LOG_TRIVIAL(info) << "restart tproxy udp send";
		    _impl->_local = udp::endpoint();
		    start_send();
		    return;
	    }
	    state = RELAY_STOP;
        // call close socket
        // BOOST_LOG_TRIVIAL(info) << "stop raw udp";
        boost::system::error_code err;
        _impl->_sock.shutdown(tcp::socket::shutdown_both, err);
        _impl->_sock.close(err);
    });
}
static udp::endpoint get_data_addr(const uint8_t *data)
{
	udp::endpoint daddr(udp::v6(), 0);
    if (data[0] == 1) {
        auto dst_addr = (struct sockaddr_in*)daddr.data();
        memcpy(&dst_addr->sin_addr, &data[1], 4);
        memcpy(&dst_addr->sin_port, &data[5], 2);
        dst_addr->sin_family = AF_INET;
    } else if (data[0] == 4) {
        auto dst_addr6 = (struct sockaddr_in6*)daddr.data();
        memcpy(&dst_addr6->sin6_addr, &data[1], 16);
        memcpy(&dst_addr6->sin6_port, &data[17], 2);
        dst_addr6->sin6_family = AF_INET6;
    }
    return daddr;
}

udp::endpoint& raw_udp::udp_impl::get_send_data_addr(const std::shared_ptr<relay_data> buf)
{
	// for local transparent
    uint8_t *data = (uint8_t*) buf->data_buffer().data();
	auto re_addr = get_data_addr(data);
	const auto &type = _owner->config.type;

	if (type == REMOTE_SERVER) {
	    _remote = get_data_addr(data);
        return _remote;
	}
    if (type == LOCAL_TRANSPARENT) {
	    // bind sock to re_addr
	    // BOOST_LOG_TRIVIAL(info) << session() <<"bind to udp "<< re_addr;
	    if (_local != re_addr) {
		    _local = re_addr;
		    _sock.close();
		    _sock.open(udp::v6());
		    _sock.set_option(udp::socket::reuse_address(true));
		    _sock.set_option(_ip_transparent_t(true));
		    _sock.bind(re_addr);
	    }
	    return _peers[buf->session()];
    }
    // TBD should not happen
    return _remote;
}
std::size_t raw_udp::internal_send_data(const std::shared_ptr<relay_data> buf, asio::yield_context &yield)
{
    // send to _remote
    // BOOST_LOG_TRIVIAL(info) << session()<<" udp send to "<< *dst<< "local"<<_impl->_sock.local_endpoint();
    // BOOST_LOG_TRIVIAL(info) << buf_to_string(buf->udp_data_buffer().data(), buf->udp_data_buffer().size());
    auto dst = _impl->get_send_data_addr(buf);
	auto len = _impl->_sock.async_send_to(buf->udp_data_buffer(), dst, yield);
    if (len != buf->udp_data_size()) {
        auto emsg = boost::format("udp relay len %1%, data size %2%")%len % buf->udp_data_size();
        throw_err_msg(emsg.str());
    }
    return len;
}

void raw_udp::start_relay()
{
    _impl->_sock.set_option(udp::socket::reuse_address(true));
    if (config.type == REMOTE_SERVER) {
        _impl->_sock.bind(udp::endpoint(udp::v6(), 0));
        _impl->impl_start_recv();
    }
    start_send();
}
void raw_udp::internal_log(const std::string &desc, const boost::system::system_error&error)
{
    BOOST_LOG_TRIVIAL(error) << "raw_udp "<<session << desc<<error.what();
}
