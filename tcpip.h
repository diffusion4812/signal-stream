#pragma once

#include <boost/asio.hpp>
#include <SDL3/SDL_log.h>
#include <memory>

class Server {
public:
    Server(boost::asio::io_context& io_context, short port);
private:
    void do_accept();
    boost::asio::ip::tcp::acceptor mAcceptor;
};

class Session : public std::enable_shared_from_this<Session>
{
public:
    Session(boost::asio::ip::tcp::socket socket);
    void run();
private:
    void wait_for_request();
    boost::asio::ip::tcp::socket mSocket;
    boost::asio::streambuf mBuffer;
};