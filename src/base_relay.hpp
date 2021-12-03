#ifndef _SSL_SOCKS_BASE_RELAY_HPP
#define _SSL_SOCKS_BASE_RELAY_HPP

#include <memory>
#include <boost/asio/spawn.hpp>
#include "relay_data.hpp"
#include "relay.hpp"

using boost::system::system_error;
using boost::system::error_code;

class base_relay
    : public std::enable_shared_from_this<base_relay>
{
public:
    base_relay(asio::io_context &io, server_type type, const std::string &host, const std::string &service);
    virtual ~base_relay();
    // use dispatch to run in own strand
    template<typename T> void run_in_strand(T &&func)
    {
        _strand.dispatch(func, asio::get_associated_allocator(func));
    }

    // spawn coroutin in own strand
    template<typename T> void spawn_in_strand(T &&func)
    {
        asio::spawn(_strand, func);
    }
    void send_data(const std::shared_ptr<relay_data> &buf);
    void start_send();

    virtual void start_relay() = 0;
protected:
    // void timeout_cancel();
    std::pair<std::string, std::string> remote();
    server_type type();
    void stop_relay();
    bool is_stop(bool=false);

private:
    struct base_impl;
    std::unique_ptr<base_impl> _impl;
    asio::strand<asio::io_context::executor_type> _strand;

    virtual std::size_t internal_send_data(const std::shared_ptr<relay_data> &buf, asio::yield_context &yield) = 0;
    virtual void internal_stop_relay() = 0;
    virtual void internal_log(const std::string &desc, const system_error&error=system_error(error_code()));
};

#endif
