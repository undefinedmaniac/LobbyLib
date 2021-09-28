#pragma once

#include <boost/asio.hpp>

using boost::asio::ip::tcp;

class LobbyServer
{
public:
    
    LobbyServer(boost::asio::io_context& io_context, 
                const tcp::endpoint& endpoint);

private:
    void acceptConnections();
    void newConnection(const boost::system::error_code& error, 
                       boost::asio::ip::tcp::socket peer);

    tcp::acceptor _acceptor;
};