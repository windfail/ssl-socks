#ifndef _SSL_SOCKS_RAW_RELAY_HPP
#define _SSL_SOCKS_RAW_RELAY_HPP
#include "relay.hpp"

// raw relay , base class for raw_tcp and raw_udp
class raw_relay
    :protected base_relay, public std::enable_shared_from_this<raw_relay>
{
public:
    raw_relay(asio::io_context *io, const std::shared_ptr<ssl_relay> &manager, uint32_t session = 0);
    virtual ~raw_relay();

    uint32_t session();
    void session(uint32_t id);
    // asio::strand<asio::io_context::executor_type> & strand();
    std::shared_ptr<ssl_relay> & manager();

    void stop_raw_relay(const relay_data::stop_src);
    void send_data_on_raw(const std::shared_ptr<relay_data> &buf);

protected:
    typedef std::queue<std::shared_ptr<relay_data>> data_t;

    virtual void start_raw_send(data_t &bufs) = 0;
    virtual void stop_this_relay(const relay_data::stop_src) = 0;
private:
    struct raw_impl;
    std::unique_ptr<raw_impl> _impl;
    template<typename HANDL_T>
    virtual void async_send_data(const std::shared_ptr<relay_data> &buf, HANDL_T &&handle)=0;

};

#endif
