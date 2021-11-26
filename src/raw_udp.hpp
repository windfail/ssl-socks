#ifndef _SSL_SOCKS_RAW_UDP_HPP
#define _SSL_SOCKS_RAW_UDP_HPP
#include <map>
#include <unordered_map>
#include "relay.hpp"

// raw udp, for client to local server and remote server to dest
class raw_udp
    :public raw_relay, std::enable_shared_from_this<raw_udp>
{
public:
    raw_udp(asio::io_context *io, const std::shared_ptr<ssl_relay> &manager);
    raw_udp(asio::io_context *io, const std::shared_ptr<ssl_relay> &manager, uint32_t session, const udp::endpoint &remote);
    ~raw_udp();
    void stop_this_relay();
    void start_raw_send(std::shared_ptr<relay_data> buf);
    void start_remote_relay();
private:
    struct udp_impl;
    std::unique_ptr<udp_impl> _impl ;


};
#endif
