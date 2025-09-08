#include "tcpip.h"

Server::Server(boost::asio::io_context& io_context, short port) : mAcceptor(io_context, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)) {
    // now we call do_accept() where we wait for clients
    do_accept();
}

void Server::do_accept() {
    // this is an async accept which means the lambda function is 
    // executed, when a client connects
    mAcceptor.async_accept([this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
        if (!ec) {
            SDL_Log("creating session");
            std::make_shared<Session>(std::move(socket))->run();
        }
        else {
            SDL_Log("error: %s", ec.message().c_str());
        }
        do_accept();
    });
}

Session::Session(boost::asio::ip::tcp::socket socket) : mSocket(std::move(socket)) { }

void Session::run() {
    wait_for_request();
}

void Session::wait_for_request() {
    // since we capture `this` in the callback, we need to call shared_from_this()
    auto self(shared_from_this());
    // and now call the lambda once data arrives
    // we read a string until the null termination character
    boost::asio::async_read_until(mSocket, mBuffer, "\0",
        [this, self](boost::system::error_code ec, std::size_t /*length*/)
        {
            // if there was no error, everything went well and for this demo
            // we print the data to stdout and wait for the next request
            if (!ec) {
                std::string data{
                    std::istreambuf_iterator<char>(&mBuffer),
                    std::istreambuf_iterator<char>()
                };
                // we just print the data, you can here call other api's 
                // or whatever the server needs to do with the received data
                SDL_Log(data.c_str());
                wait_for_request();
            }
            else {
                SDL_Log("error: %s", ec.message().c_str());
            }
        });
}