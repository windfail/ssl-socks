#ifndef _SSL_SOCKS_RELAY_DATA_HPP
#define _SSL_SOCKS_RELAY_DATA_HPP

#include <boost/asio/buffer.hpp>
namespace asio = boost::asio;

const int READ_BUFFER_SIZE = 4096;
class relay_data
{

public:
    enum command {
        STOP_RELAY,
        START_TCP,
        START_UDP,
        DATA_RELAY,
        KEEP_RELAY
    };
    enum stop_src {
        from_ssl,
        from_raw,
        ssl_err
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
    explicit relay_data(uint32_t session) :_header(session, DATA_RELAY, READ_BUFFER_SIZE), _data{0} {
    }

    relay_data(uint32_t session, command cmd) : _header(session, cmd, 0), _data{0} {

    }

    _header_t & head() {
        return _header;
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
    auto buffers() {
        //return asio::buffer(&_header, sizeof(_header_t)+_header._len);
        return std::array<asio::mutable_buffer, 2> { header_buffer(), data_buffer() };
//asio::buffer(&_header, sizeof(_header_t)), asio::buffer(_data)} ;
    }
    void resize(std::size_t data_len) {
        _header._len = data_len;
//		_data.resize(data_len);
    }
    auto header_size() {
        return sizeof(_header_t);
    }
    auto data_size() {
        return _header._len;
    }
    auto size() {
        return _header._len + sizeof(_header_t);
    }
private:
};

#endif
