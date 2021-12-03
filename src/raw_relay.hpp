#ifndef _SSL_SOCKS_RAW_RELAY_HPP
#define _SSL_SOCKS_RAW_RELAY_HPP
#include "relay.hpp"
#include "base_relay.hpp"

// raw relay , base class for raw_tcp and raw_udp
class raw_relay
    :public base_relay
{
public:
    raw_relay(asio::io_context &io, server_type type, const std::string &host, const std::string &service);
    // raw_relay(asio::io_context &io, std::shared_ptr<ssl_relay> manager=nullptr, uint32_t session = 0);
    virtual ~raw_relay();

    uint32_t session();
    void session(uint32_t id);
    std::shared_ptr<ssl_relay> manager();
    void manager(const std::shared_ptr<ssl_relay> &ssl_ptr);

    // ssl relay call to stop raw relay
    virtual void stop_raw_relay() = 0;
private:
    struct raw_impl;
    std::unique_ptr<raw_impl> _impl;

};

#endif
