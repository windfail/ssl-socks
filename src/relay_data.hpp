#ifndef _SSL_SOCKS_RELAY_DATA_HPP
#define _SSL_SOCKS_RELAY_DATA_HPP

#include <boost/asio/buffer.hpp>
namespace asio = boost::asio;

const int READ_BUFFER_SIZE = 4096;
class relay_data
{

public:
    enum command {
        STOP_TCP,
        START_TCP,
        DATA_UDP,
        DATA_TCP,
        STOP_UDP
    };
    struct _header_t {
        uint32_t _session;
        command _cmd;
        std::size_t _len;
        _header_t(uint32_t session, command cmd, std::size_t len) :_session(session), _cmd(cmd), _len(len) {
        }
    };
private:
    _header_t _header;
    uint8_t _data[READ_BUFFER_SIZE];

public:
    relay_data() :_header(0, DATA_UDP, READ_BUFFER_SIZE), _data{0}
    {}
    explicit relay_data(uint32_t session) :_header(session, DATA_TCP, READ_BUFFER_SIZE), _data{0} {
    }

    relay_data(uint32_t session, command cmd) : _header(session, cmd, 0), _data{0} {
    }

    _header_t & head() {
        return _header;
    }
    void session(uint32_t session)
    {
        _header._session = session;
    }
    auto session() {
//		auto hd = (_header_t*)&_data[0];
        return _header._session;
    }
    auto cmd() {
//		auto hd = (_header_t*)&_data[0];
        return _header._cmd;
    }

    auto header_buffer() {
        return asio::buffer(&_header, sizeof(_header_t));
    }
    auto data_buffer() {
        //return asio::buffer(&_data[sizeof(_header_t)], _header._len);
        return asio::buffer(_data, _header._len);
    }
    auto udp_data_buffer()
    {
        return asio::buffer(_data+19, _header._len-19);
    }
    auto buffers() {
        return std::array<asio::mutable_buffer, 2> { header_buffer(), data_buffer() };
    }

    void resize_udp(std::size_t data_len) {
        _header._len = data_len+19;
    }
    void resize(std::size_t data_len) {
        _header._len = data_len;
    }
    auto header_size() {
        return sizeof(_header_t);
    }
    auto data_size() {
        return _header._len;
    }
    auto udp_data_size()
    {
        return _header._len-19;
    }
    auto size() {
        return _header._len + sizeof(_header_t);
    }
private:
};

#endif
