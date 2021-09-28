#include "LobbyLib/LobbyLib.h"

#include <set>
#include <deque>
#include <memory>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>

using boost::asio::ip::tcp;

namespace LL
{

const short HEADER_SIZE = 2;
const short INACTIVITY_TIMEOUT = 1;
const short REPLY_TIMEOUT = 1;

struct SocketData;

void _connectErrorEvent(LobbyClient* client, std::string message, std::string host, std::string port);

void _asyncAcceptHandler(LobbyServer* server, std::string port, const boost::system::error_code& error, tcp::socket peer);
void _acceptErrorEvent(LobbyServer* server, std::string message, std::string port);

template <typename T>
void _asyncReadHandler(T* obj, tcp::socket& socket, SocketData& messageData, 
    std::function<void(T*)> disconnectHandler, std::function<void(T*, std::string)> readErrorHandler,
    bool readHeader, const boost::system::error_code& error);

void _handleMessage(LobbyClient* client);
void _handleMessage(Player* player);

void _clientDisconnectedEvent(LobbyClient* client);
void _playerDisconnectedEvent(Player* player);

void _clientReadErrorEvent(LobbyClient* client, std::string message);
void _playerReadErrorEvent(Player* player, std::string message);

void _playerKeepAliveTimeout(Player* player, const boost::system::error_code& error);
void _sendPingAndResetTimeout(Player* player);

template <typename T>
void _sendMessage(T* obj, tcp::socket* socket, SocketData* data, std::function<void(T*, std::string)> errorEventHandler, 
    char message[], std::size_t messageLength);
template <typename T>
void _asyncWriteHandler(T* obj, tcp::socket* socket, SocketData* data, std::function<void(T*, std::string)> errorEventHandler, 
    const boost::system::error_code& error, std::size_t bytes_transferred);

void _clientWriteErrorEvent(LobbyClient* client, std::string message);
void _playerWriteErrorEvent(Player* player, std::string message);

enum KeepAliveState {
    InactivityTimeout, PingAttempt1, PingAttempt2, PingAttempt3
};

struct LobbyLibLoop {
    boost::asio::io_context io_context;
    std::deque<std::unique_ptr<Event>> events;
};

struct SocketData {
    unsigned char header[HEADER_SIZE];
    uint16_t content_size;
    uint16_t buffer_size;
    std::unique_ptr<char[]> buffer;
    std::deque<std::tuple<std::unique_ptr<char[]>, std::size_t>> outgoingBuffers;
};

struct LobbyClient {
    LobbyLibLoop* loop;
    std::unique_ptr<tcp::resolver> resolver;
    std::unique_ptr<tcp::socket> socket;
    SocketData socketData;
};

struct LobbyServer {
    LobbyLibLoop* loop;
    std::unique_ptr<tcp::acceptor> acceptor;
    std::set<std::unique_ptr<Player>> players;
};
struct Lobby {

};
struct Player {
    LobbyServer* server;
    std::unique_ptr<tcp::socket> socket;
    SocketData socketData;

    KeepAliveState keepAliveState = InactivityTimeout;
    std::unique_ptr<boost::asio::steady_timer> keepAliveTimer;
};

LobbyLibLoop* createLoop() {
    return new LobbyLibLoop();
}

void deleteLoop(LobbyLibLoop* loop) {
    delete loop;
}

void processEvents(LobbyLibLoop* loop) {
    loop->io_context.restart();
    loop->io_context.poll();
}

bool hasEvents(LobbyLibLoop* loop) {
    return !loop->events.empty();
}

Event* getNextEvent(LobbyLibLoop* loop) {
    Event* event = loop->events.front().release();
    loop->events.pop_front();
    return event;
}

// Create/Delete a lobby client
LobbyClient* createLobbyClient(LobbyLibLoop* loop) {
    LobbyClient* client = new LobbyClient();
    client->loop = loop;
    return client;
}

void deleteLobbyClient(LobbyClient* client) {
    delete client;
}


// Network Connect/Disconnect operations
void connectToServer(LobbyClient* client, std::string host, std::string port) {
    client->resolver = std::make_unique<tcp::resolver>(client->loop->io_context);
    client->resolver->async_resolve(host, port, 
        [=](const boost::system::error_code& error, tcp::resolver::results_type endpoints) {
            client->resolver = nullptr;
            if (!error) {
                client->socket = std::make_unique<tcp::socket>(client->loop->io_context);
                boost::asio::async_connect(*client->socket, endpoints, 
                    [=](const boost::system::error_code& error2, const tcp::endpoint& endpoint) {
                        if (!error2) {
                            std::unique_ptr<ConnectedEvent> event = std::make_unique<ConnectedEvent>();
                            event->client = client;
                            event->host = host;
                            event->port = port;
                            client->loop->events.push_back(std::move(event));

                            // Start infinitely reading from the socket
                            boost::asio::async_read(*client->socket, boost::asio::buffer(client->socketData.header, HEADER_SIZE), 
                                [=](const boost::system::error_code& error, std::size_t bytes_transferred) {
                                _asyncReadHandler<LobbyClient>(client, *client->socket, client->socketData,
                                    _clientDisconnectedEvent, _clientReadErrorEvent, true, error);
                            });
                        } else {
                            _connectErrorEvent(client, error2.message(), host, port);
                        }
                    }
                );
            } else {
                _connectErrorEvent(client, error.message(), host, port);
            }
        }
    );
}

void _connectErrorEvent(LobbyClient* client, std::string message, std::string host, std::string port) {
    std::unique_ptr<ConnectErrorEvent> event = std::make_unique<ConnectErrorEvent>();
    event->client = client;
    event->error_message = message;
    event->host = host;
    event->port = port;
    client->loop->events.push_back(std::move(event));
}

void disconnectFromServer(LobbyClient* client) {
    if (client->socket) {
        boost::system::error_code code;
        client->socket->close(code);
    }
}


// Set username
void setUsername(LobbyClient* client, std::string username) {

}


// Join/Create lobby
void joinExistingLobby(LobbyClient* client, std::string lobbyId) {

}

void createNewLobby(LobbyClient* client) {

}


// Send messages
void sendChatMessage(LobbyClient* client, std::string message) {

}

void sendGameMessage(LobbyClient* client, char bytes[], int numberOfBytes) {

}

// Create/Delete a lobby server
LobbyServer* createLobbyServer(LobbyLibLoop* loop) {
    LobbyServer* server = new LobbyServer();
    server->loop = loop;
    return server;
}

void deleteLobbyServer(LobbyServer* server) {
    delete server;
}


// Start/Stop listening
void startListening(LobbyServer* server, std::string port) {
    int port_number;
    try {
        port_number = std::stoi(port);
    } catch (...) {
        _acceptErrorEvent(server, "Invalid port", port);
        return;
    }

    stopListening(server);

    server->acceptor = std::make_unique<tcp::acceptor>(server->loop->io_context, tcp::endpoint(tcp::v6(), port_number));
    server->acceptor->async_accept(std::bind(_asyncAcceptHandler, server, port, std::placeholders::_1, std::placeholders::_2));
}

void _asyncAcceptHandler(LobbyServer* server, std::string port, const boost::system::error_code& error, tcp::socket peer)
{
    if (!error) {
        // Create a new socket on the heap
        std::unique_ptr<tcp::socket> socket_ptr = std::make_unique<tcp::socket>(std::move(peer));
        tcp::socket* socket = socket_ptr.get();

        // Create a new player object
        std::unique_ptr<Player> player_ptr = std::make_unique<Player>();
        Player* player = player_ptr.get();
        player->server = server;
        player->socket = std::move(socket_ptr);
        player->keepAliveTimer = std::make_unique<boost::asio::steady_timer>(
            server->loop->io_context, boost::asio::chrono::seconds(INACTIVITY_TIMEOUT));
        server->players.insert(std::move(player_ptr));

        // Start the inactivity timer for this socket
        player->keepAliveTimer->async_wait(std::bind(_playerKeepAliveTimeout, player, std::placeholders::_1));

        // Create the AcceptedEvent for this newly accepted connection
        std::unique_ptr<AcceptedEvent> event = std::make_unique<AcceptedEvent>();
        event->player = player;
        event->server = server;
        event->ip = socket->remote_endpoint().address().to_string();
        event->port = std::to_string(socket->remote_endpoint().port());
        server->loop->events.push_back(std::move(event));

        // Start infinitely reading from the socket
        boost::asio::async_read(*socket, boost::asio::buffer(player->socketData.header, HEADER_SIZE), 
            [=](const boost::system::error_code& error, std::size_t bytes_transferred) {
            _asyncReadHandler<Player>(player, *socket, player->socketData,
                _playerDisconnectedEvent, _playerReadErrorEvent, true, error);
        });
    } else {
        if (error == boost::asio::error::operation_aborted) {
            server->acceptor = nullptr;
            return;
        }

        _acceptErrorEvent(server, error.message(), port);
    }

    server->acceptor->async_accept(std::bind(_asyncAcceptHandler, server, port, std::placeholders::_1, std::placeholders::_2));
}

void _acceptErrorEvent(LobbyServer* server, std::string message, std::string port) {
    std::unique_ptr<AcceptErrorEvent> event = std::make_unique<AcceptErrorEvent>();
    event->server = server;
    event->error_message = message;
    event->port = port;
    server->loop->events.push_back(std::move(event));
}

template <typename T>
void _asyncReadHandler(T* obj, tcp::socket& socket, SocketData& messageData, 
    std::function<void(T*)> disconnectHandler, std::function<void(T*, std::string)> readErrorHandler,
    bool readHeader, const boost::system::error_code& error) {
    if (!error) {
        // Create the handler for the next read
        std::function<void(const boost::system::error_code&, std::size_t)> asyncReadHandler = 
        [=, &socket, &messageData](const boost::system::error_code& error, std::size_t bytes_transferred) {
            _asyncReadHandler<T>(obj, socket, messageData, disconnectHandler, readErrorHandler, !readHeader, error);
        };

        if (readHeader) {
            // Get the length of the message content as a 2 byte unsigned short
            messageData.content_size = boost::endian::load_big_u16(messageData.header);

            // Create/Resize the content buffer to store the next message
            if (!messageData.buffer || messageData.buffer_size < messageData.content_size) {
                messageData.buffer_size = messageData.content_size;
                messageData.buffer = std::make_unique<char[]>(messageData.content_size);
            }

            // Go read the actual message content now!
            boost::asio::async_read(socket, boost::asio::buffer(messageData.buffer.get(), messageData.content_size), asyncReadHandler);
        } else {
            // Process the message
            _handleMessage(obj);

            // Start reading the head of the next message
            boost::asio::async_read(socket, boost::asio::buffer(messageData.header, HEADER_SIZE), asyncReadHandler);
        }
    } else {
        // Deal with EOF or other read errors
        if (error == boost::asio::error::eof || error == boost::asio::error::operation_aborted)
            disconnectHandler(obj);
        else
            readErrorHandler(obj, error.message());
    }
}

void _handleMessage(LobbyClient* client) {
    // The first byte of the message indicates what type of message it is
    uint8_t messageType = client->socketData.buffer[0];

    switch (messageType) {
        // Ping!
        case 0: {
            std::cout << "Ping!" << std::endl;
            // Send a "pong" !
            char message[] = {0};
            //_sendMessage<LobbyClient>(client, client->socket.get(), &client->socketData, _clientWriteErrorEvent, message, 1);
            break;
        }
    }
}

void _handleMessage(Player* player) {
    // The first byte of the message indicates what type of message it is
    uint8_t messageType = player->socketData.buffer[0];

    switch (messageType) {
        // Pong!
        case 0: {
            std::cout << "Pong!" << std::endl;
            player->keepAliveTimer->cancel();
            break;
        }
    }
}

void _clientDisconnectedEvent(LobbyClient* client) {
    std::unique_ptr<ClientDisconnectedEvent> event = std::make_unique<ClientDisconnectedEvent>();
    event->client = client;
    client->loop->events.push_back(std::move(event));
}

void _playerDisconnectedEvent(Player* player) {
    std::unique_ptr<PlayerDisconnectedEvent> event = std::make_unique<PlayerDisconnectedEvent>();
    event->player = player;
    player->server->loop->events.push_back(std::move(event));
}

void _clientReadErrorEvent(LobbyClient* client, std::string message) {
    std::unique_ptr<ClientReadErrorEvent> event = std::make_unique<ClientReadErrorEvent>();
    event->client = client;
    event->error_message = message;
    client->loop->events.push_back(std::move(event));
}

void _playerReadErrorEvent(Player* player, std::string message) {
    std::unique_ptr<PlayerReadErrorEvent> event = std::make_unique<PlayerReadErrorEvent>();
    event->player = player;
    event->error_message = message;
    player->server->loop->events.push_back(std::move(event));
}

void _playerKeepAliveTimeout(Player* player, const boost::system::error_code& error) {
    if (error == boost::asio::error::operation_aborted) {
        // Operation aborted means we received a "pong" before the timer expired
        player->keepAliveState = KeepAliveState::InactivityTimeout;
        player->keepAliveTimer->expires_after(boost::asio::chrono::seconds(INACTIVITY_TIMEOUT));
        player->keepAliveTimer->async_wait(std::bind(_playerKeepAliveTimeout, player, std::placeholders::_1));
    } else {
        switch (player->keepAliveState) {
            case InactivityTimeout: {
                player->keepAliveState = KeepAliveState::PingAttempt1;
                _sendPingAndResetTimeout(player);
                break;
            }
            case PingAttempt1: {
                player->keepAliveState = KeepAliveState::PingAttempt2;
                _sendPingAndResetTimeout(player);
                break;
            }
            case PingAttempt2: {
                player->keepAliveState = KeepAliveState::PingAttempt3;
                _sendPingAndResetTimeout(player);
                break;
            }
            case PingAttempt3: {
                // Disconnect the player
                disconnectPlayer(player);
                break;
            }
        }
    }
}

void _sendPingAndResetTimeout(Player* player) {
    // Send a "ping" 
    char message[] = {0};
    _sendMessage<Player>(player, player->socket.get(), &player->socketData, _playerWriteErrorEvent, message, 1);

    player->keepAliveTimer->expires_after(boost::asio::chrono::seconds(REPLY_TIMEOUT));
    player->keepAliveTimer->async_wait(std::bind(_playerKeepAliveTimeout, player, std::placeholders::_1));
}

template <typename T>
void _sendMessage(T* obj, tcp::socket* socket, SocketData* data, std::function<void(T*, std::string)> errorEventHandler, 
    char message[], std::size_t messageLength) {
    // The socket is not currently sending data if there are no buffers in the queue
    bool socketIsIdle = data->outgoingBuffers.empty();

    // Create a new buffer on the heap to store this message while it's being sent
    // Also we add 2 bytes since a message header will be added at the beginning
    std::size_t finalMessageLength = messageLength + 2;
    std::unique_ptr<char[]> heapBuffer = std::make_unique<char[]>(finalMessageLength);
    boost::endian::store_big_u16(reinterpret_cast<unsigned char*>(heapBuffer.get()), messageLength);
    std::copy(message, message + messageLength, heapBuffer.get() + 2);

    // Create a boost wrapper for the buffer
    boost::asio::mutable_buffer buffer = boost::asio::buffer(heapBuffer.get(), finalMessageLength);

    // Add the new buffer to the back of the queue
    data->outgoingBuffers.push_back(std::make_tuple(std::move(heapBuffer), finalMessageLength));

    // Start the sending operation if the socket is idle
    if (socketIsIdle)
        boost::asio::async_write(*socket, buffer, 
            std::bind(_asyncWriteHandler<T>, obj, socket, data, errorEventHandler, std::placeholders::_1, std::placeholders::_2));
}

template <typename T>
void _asyncWriteHandler(T* obj, tcp::socket* socket, SocketData* data, std::function<void(T*, std::string)> errorEventHandler, 
    const boost::system::error_code& error, std::size_t bytes_transferred) {
    if (!error) {
        // Yank out the front element, since we just finished sending it
        data->outgoingBuffers.pop_front();

        // If there are more buffers to send, start sending them
        if (!data->outgoingBuffers.empty()) {
            boost::asio::mutable_buffer buffer = boost::asio::buffer(
                std::get<0>(data->outgoingBuffers.front()).get(), 
                std::get<1>(data->outgoingBuffers.front()));

            boost::asio::async_write(*socket, buffer, 
                std::bind(_asyncWriteHandler<T>, obj, socket, data, errorEventHandler, std::placeholders::_1, std::placeholders::_2));
        }
    } else {
        errorEventHandler(obj, error.message());
    }
}

void _clientWriteErrorEvent(LobbyClient* client, std::string message) {
    std::unique_ptr<ClientWriteErrorEvent> event = std::make_unique<ClientWriteErrorEvent>();
    event->client = client;
    event->error_message = message;
    client->loop->events.push_back(std::move(event));
}

void _playerWriteErrorEvent(Player* player, std::string message) {
    std::unique_ptr<PlayerWriteErrorEvent> event = std::make_unique<PlayerWriteErrorEvent>();
    event->player = player;
    event->error_message = message;
    player->server->loop->events.push_back(std::move(event));
}

void stopListening(LobbyServer* server) {
    if (server->acceptor) {
        boost::system::error_code error;
        server->acceptor->cancel(error);
        server->acceptor->close(error);
        server->acceptor = nullptr;
    }
}

// Manage players
void disconnectPlayer(Player* player) {
    boost::system::error_code error;
    player->socket->close(error);
}

void deletePlayer(Player* player) {
    std::set<std::unique_ptr<Player>>::iterator iterator = 
        std::find_if(player->server->players.begin(), player->server->players.end(), 
            [=](const std::unique_ptr<Player>& ptr) {
                return ptr.get() == player;
        });

    player->server->players.erase(iterator);
}

}
