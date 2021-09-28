#include "LobbyServer.h"

#include <iostream>

LobbyServer::LobbyServer(boost::asio::io_context& io_context, 
    const tcp::endpoint& endpoint) 
  : _acceptor(io_context, endpoint)
{
    acceptConnections();
}

void LobbyServer::acceptConnections()
{
    _acceptor.async_accept(std::bind(&LobbyServer::newConnection, this, std::placeholders::_1, std::placeholders::_2));
}

void LobbyServer::newConnection(const boost::system::error_code& error,
    boost::asio::ip::tcp::socket peer)
{
    std::cout << "Connection Received!" << std::endl;

    std::cout << peer.local_endpoint() << std::endl;
    std::cout << peer.remote_endpoint() << std::endl;

    peer.send(boost::asio::buffer("Hi there!"));
    
    // Continue listening for connections
    acceptConnections();
}
