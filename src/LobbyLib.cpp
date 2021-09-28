#include "LobbyLib/LobbyLib.h"

#include <set>
#include <deque>
#include <memory>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>

using std::size_t;
using std::string;
using std::unique_ptr;
using boost::asio::ip::tcp;
using boost::system::error_code;

namespace asio = boost::asio;
using namespace std::placeholders;

namespace LL
{

const short HEADER_SIZE = 2;
const short INACTIVITY_TIMEOUT = 1;
const short REPLY_TIMEOUT = 1;
const short KEEP_ALIVE_TRIES = 3;

struct SocketData;

void _connectErrorEvent(LobbyClient* client, string message, string host, string port);

void _asyncAcceptHandler(LobbyServer* server, string port, const error_code& error, tcp::socket peer);
void _acceptErrorEvent(LobbyServer* server, string message, string port);

void _asyncReadHandler(LobbyClient* client, bool readHeader, const error_code& error, size_t bytes_transferred);
void _asyncReadHandler(Player* player, bool readHeader, const error_code& error, size_t bytes_transferred);
template <typename T>
void _asyncReadHandler(T* obj, tcp::socket* socket, SocketData* messageData, 
    std::function<void(T*)> disconnectHandler, std::function<void(T*, string)> readErrorHandler,
    bool readHeader, const error_code& error);

void _handleMessage(LobbyClient* client);
void _handleMessage(Player* player);

void _clientDisconnectedEvent(LobbyClient* client);
void _playerDisconnectedEvent(Player* player);

void _clientReadErrorEvent(LobbyClient* client, string message);
void _playerReadErrorEvent(Player* player, string message);

void _clientKeepAliveTimeout(LobbyClient* client, const error_code& error);
void _playerKeepAliveTimeout(Player* player, const error_code& error);

void _sendMessage(LobbyClient* client, char message[], size_t messageLength);
void _sendMessage(Player* player, char message[], size_t messageLength);
template <typename T>
void _sendMessage(T* obj, tcp::socket* socket, SocketData* data, 
    std::function<void(T*, string)> errorEventHandler, char message[], size_t messageLength);
template <typename T>
void _asyncWriteHandler(T* obj, tcp::socket* socket, SocketData* data, 
    std::function<void(T*, string)> errorEventHandler, const error_code& error, size_t bytes_transferred);

void _clientWriteErrorEvent(LobbyClient* client, string message);
void _playerWriteErrorEvent(Player* player, string message);

struct LobbyLibLoop 
{
    asio::io_context io_context;
    std::deque<unique_ptr<Event>> events;
};

struct SocketData 
{
    unsigned char header[HEADER_SIZE];
    uint16_t content_size;
    uint16_t buffer_size;
    unique_ptr<char[]> buffer;
    std::deque<std::tuple<unique_ptr<char[]>, size_t>> outgoingBuffers;
};

struct LobbyClient 
{
    LobbyLibLoop* loop;
    unique_ptr<tcp::resolver> resolver;
    unique_ptr<tcp::socket> socket;
    SocketData socketData;

    short keepAliveTimeout = INACTIVITY_TIMEOUT + REPLY_TIMEOUT*KEEP_ALIVE_TRIES;
    unique_ptr<asio::steady_timer> keepAliveTimer;
};

struct LobbyServer 
{
    LobbyLibLoop* loop;
    unique_ptr<tcp::acceptor> acceptor;
    std::set<unique_ptr<Player>> players;
};
struct Lobby 
{

};
struct Player 
{
    LobbyServer* server;
    unique_ptr<tcp::socket> socket;
    SocketData socketData;

    int keepAliveTries = 0;
    unique_ptr<asio::steady_timer> keepAliveTimer;
};

LobbyLibLoop* createLoop() 
{
    return new LobbyLibLoop();
}

void deleteLoop(LobbyLibLoop* loop) 
{
    delete loop;
}

void processEvents(LobbyLibLoop* loop) 
{
    loop->io_context.restart();
    loop->io_context.poll();
}

bool hasEvents(LobbyLibLoop* loop) 
{
    return !loop->events.empty();
}

Event* getNextEvent(LobbyLibLoop* loop) 
{
    Event* event = loop->events.front().release();
    loop->events.pop_front();
    return event;
}

// Create/Delete a lobby client
LobbyClient* createLobbyClient(LobbyLibLoop* loop) 
{
    LobbyClient* client = new LobbyClient();
    client->loop = loop;
    return client;
}

void deleteLobbyClient(LobbyClient* client) 
{
    delete client;
}


// Network Connect/Disconnect operations
void connectToServer(LobbyClient* client, string host, string port) 
{
    client->resolver = std::make_unique<tcp::resolver>(client->loop->io_context);
    client->resolver->async_resolve(host, port, 
        [=](const error_code& error, tcp::resolver::results_type endpoints) {
            client->resolver = nullptr;
            if (!error) {
                client->socket = std::make_unique<tcp::socket>(client->loop->io_context);
                asio::async_connect(*client->socket, endpoints, 
                    [=](const error_code& error2, const tcp::endpoint& endpoint) {
                        if (!error2) {
                            unique_ptr<ConnectedEvent> event = std::make_unique<ConnectedEvent>();
                            event->client = client;
                            event->host = host;
                            event->port = port;
                            client->loop->events.push_back(std::move(event));

                            // Start the inactivity timer for this socket
                            client->keepAliveTimer = std::make_unique<asio::steady_timer>(
                                client->loop->io_context, asio::chrono::seconds(client->keepAliveTimeout));
                            client->keepAliveTimer->async_wait(std::bind(_clientKeepAliveTimeout, client, _1));

                            using sig = void(*)(LobbyClient*, bool, const error_code&, size_t);

                            // Start infinitely reading from the socket
                            asio::async_read(*client->socket, asio::buffer(client->socketData.header, HEADER_SIZE), 
                                std::bind(static_cast<sig>(_asyncReadHandler), client, true, _1, _2));
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

void _connectErrorEvent(LobbyClient* client, string message, string host, string port) 
{
    unique_ptr<ConnectErrorEvent> event = std::make_unique<ConnectErrorEvent>();
    event->client = client;
    event->error_message = message;
    event->host = host;
    event->port = port;
    client->loop->events.push_back(std::move(event));
}

void disconnectFromServer(LobbyClient* client) 
{
    if (client->socket) {
        error_code error;
        client->socket->shutdown(tcp::socket::shutdown_both, error);
        client->socket->close(error);
    }
}


// Set username
void setUsername(LobbyClient* client, string username) 
{

}


// Join/Create lobby
void joinExistingLobby(LobbyClient* client, string lobbyId) 
{

}

void createNewLobby(LobbyClient* client) 
{

}


// Send messages
void sendChatMessage(LobbyClient* client, string message) 
{

}

void sendGameMessage(LobbyClient* client, char bytes[], int numberOfBytes) 
{

}

// Create/Delete a lobby server
LobbyServer* createLobbyServer(LobbyLibLoop* loop) 
{
    LobbyServer* server = new LobbyServer();
    server->loop = loop;
    return server;
}

void deleteLobbyServer(LobbyServer* server) 
{
    delete server;
}


// Start/Stop listening
void startListening(LobbyServer* server, string port) 
{
    int port_number;
    try {
        port_number = std::stoi(port);
    } catch (...) {
        _acceptErrorEvent(server, "Invalid port", port);
        return;
    }

    stopListening(server);

    server->acceptor = std::make_unique<tcp::acceptor>(server->loop->io_context, tcp::endpoint(tcp::v6(), port_number));
    server->acceptor->async_accept(std::bind(_asyncAcceptHandler, server, port, _1, _2));
}

void _asyncAcceptHandler(LobbyServer* server, string port, const error_code& error, tcp::socket peer)
{
    if (!error) {
        // Create a new socket on the heap
        unique_ptr<tcp::socket> socket_ptr = std::make_unique<tcp::socket>(std::move(peer));
        tcp::socket* socket = socket_ptr.get();

        // Create a new player object
        unique_ptr<Player> player_ptr = std::make_unique<Player>();
        Player* player = player_ptr.get();
        player->server = server;
        player->socket = std::move(socket_ptr);
        player->keepAliveTimer = std::make_unique<asio::steady_timer>(
            server->loop->io_context, asio::chrono::seconds(INACTIVITY_TIMEOUT));
        server->players.insert(std::move(player_ptr));

        // Start the inactivity timer for this socket
        player->keepAliveTimer->async_wait(std::bind(_playerKeepAliveTimeout, player, _1));

        // Create the AcceptedEvent for this newly accepted connection
        unique_ptr<AcceptedEvent> event = std::make_unique<AcceptedEvent>();
        event->player = player;
        event->server = server;
        event->ip = socket->remote_endpoint().address().to_string();
        event->port = std::to_string(socket->remote_endpoint().port());
        server->loop->events.push_back(std::move(event));

        using sig = void(*)(Player*, bool, const error_code&, size_t);

        // Start infinitely reading from the socket
        asio::async_read(*socket, asio::buffer(player->socketData.header, HEADER_SIZE), 
            std::bind(static_cast<sig>(_asyncReadHandler), player, true, _1, _2));
    } else {
        if (error == asio::error::operation_aborted) {
            server->acceptor = nullptr;
            return;
        }

        _acceptErrorEvent(server, error.message(), port);
    }

    server->acceptor->async_accept(std::bind(_asyncAcceptHandler, server, port, _1, _2));
}

void _acceptErrorEvent(LobbyServer* server, string message, string port) 
{
    unique_ptr<AcceptErrorEvent> event = std::make_unique<AcceptErrorEvent>();
    event->server = server;
    event->error_message = message;
    event->port = port;
    server->loop->events.push_back(std::move(event));
}

void _asyncReadHandler(LobbyClient* client, bool readHeader, const error_code& error, size_t bytes_transferred) 
{
    _asyncReadHandler<LobbyClient>(client, client->socket.get(), &client->socketData, 
        _clientDisconnectedEvent, _clientReadErrorEvent, readHeader, error);
}

void _asyncReadHandler(Player* player, bool readHeader, const error_code& error, size_t bytes_transferred) 
{
    _asyncReadHandler<Player>(player, player->socket.get(), &player->socketData, 
        _playerDisconnectedEvent, _playerReadErrorEvent, readHeader, error);
}

template <typename T>
void _asyncReadHandler(T* obj, tcp::socket* socket, SocketData* messageData, std::function<void(T*)> disconnectHandler, 
    std::function<void(T*, string)> readErrorHandler, bool readHeader, const error_code& error) 
{
    using sig = void(*)(T*, bool, const error_code&, size_t);
    if (!error) {
        if (readHeader) {
            // Get the length of the message content as a 2 byte unsigned short
            messageData->content_size = boost::endian::load_big_u16(messageData->header);

            // Create/Resize the content buffer to store the next message
            if (!messageData->buffer || messageData->buffer_size < messageData->content_size) {
                messageData->buffer_size = messageData->content_size;
                messageData->buffer = std::make_unique<char[]>(messageData->content_size);
            }

            // Go read the actual message content now!
            asio::async_read(*socket, asio::buffer(messageData->buffer.get(), messageData->content_size), 
                std::bind(static_cast<sig>(_asyncReadHandler), obj, false, _1, _2));
        } else {
            // Process the message
            _handleMessage(obj);

            // Start reading the head of the next message
            asio::async_read(*socket, asio::buffer(messageData->header, HEADER_SIZE), 
                std::bind(static_cast<sig>(_asyncReadHandler), obj, false, _1, _2));
        }
    } else {
        // Deal with EOF or other read errors
        if ((error == asio::error::eof) || 
            (error == asio::error::connection_reset) ||
            (error == asio::error::operation_aborted)) {
            disconnectHandler(obj);
        } else {
            readErrorHandler(obj, error.message());
        }
    }
}

void _handleMessage(LobbyClient* client) 
{
    // The first byte of the message indicates what type of message it is
    uint8_t messageType = client->socketData.buffer[0];

    switch (messageType) {
        // Ping!
        case 0: {
            std::cout << "Ping!" << std::endl;
            client->keepAliveTimer->cancel();
            // Send a "pong" !
            char message[] = {0};
            _sendMessage(client, message, 1);
            break;
        }
    }
}

void _handleMessage(Player* player) 
{
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

void _clientDisconnectedEvent(LobbyClient* client) 
{
    // Cleanup operations
    disconnectFromServer(client);
    client->keepAliveTimer->cancel();

    unique_ptr<ClientDisconnectedEvent> event = std::make_unique<ClientDisconnectedEvent>();
    event->client = client;
    client->loop->events.push_back(std::move(event));
}

void _playerDisconnectedEvent(Player* player) 
{
    // Cleanup operations
    disconnectPlayer(player);
    player->keepAliveTimer->cancel();
    
    unique_ptr<PlayerDisconnectedEvent> event = std::make_unique<PlayerDisconnectedEvent>();
    event->player = player;
    player->server->loop->events.push_back(std::move(event));
}

void _clientReadErrorEvent(LobbyClient* client, string message) 
{
    unique_ptr<ClientReadErrorEvent> event = std::make_unique<ClientReadErrorEvent>();
    event->client = client;
    event->error_message = message;
    client->loop->events.push_back(std::move(event));
}

void _playerReadErrorEvent(Player* player, string message) 
{
    unique_ptr<PlayerReadErrorEvent> event = std::make_unique<PlayerReadErrorEvent>();
    event->player = player;
    event->error_message = message;
    player->server->loop->events.push_back(std::move(event));
}

void _clientKeepAliveTimeout(LobbyClient* client, const error_code& error) {
    if (error == asio::error::operation_aborted) {
        if (client->socket->is_open()) {
            // Operation aborted means we received a "ping" before the timer expired
            client->keepAliveTimer->expires_after(asio::chrono::seconds(client->keepAliveTimeout));
            client->keepAliveTimer->async_wait(std::bind(_clientKeepAliveTimeout, client, _1));
        }
    } else {
        // The server has timed out, disconnect from it
        std::cout << "Client timeout" << std::endl;
        disconnectFromServer(client);
    }
}

void _playerKeepAliveTimeout(Player* player, const error_code& error) 
{
    if (error == asio::error::operation_aborted) {
        if (player->socket->is_open()) {
            // Operation aborted means we received a "pong" before the timer expired
            player->keepAliveTries = 0;
            player->keepAliveTimer->expires_after(asio::chrono::seconds(INACTIVITY_TIMEOUT));
            player->keepAliveTimer->async_wait(std::bind(_playerKeepAliveTimeout, player, _1));
        }
    } else {
        if (player->keepAliveTries < KEEP_ALIVE_TRIES) {
            player->keepAliveTries++;
            // Send a "ping" 
            char message[] = {0};
            _sendMessage(player, message, 1);

            // Start waiting for a pong
            player->keepAliveTimer->expires_after(asio::chrono::seconds(REPLY_TIMEOUT));
            player->keepAliveTimer->async_wait(std::bind(_playerKeepAliveTimeout, player, _1));
        } else {
            // The player has timed out, disconnect them
            std::cout << "Player timeout" << std::endl;
            disconnectPlayer(player);
        }
    }
}

void _sendMessage(LobbyClient* client, char message[], size_t messageLength)
{
    _sendMessage<LobbyClient>(client, client->socket.get(), &client->socketData, 
        _clientWriteErrorEvent, message, messageLength);
}

void _sendMessage(Player* player, char message[], size_t messageLength)
{
    _sendMessage<Player>(player, player->socket.get(), &player->socketData, 
        _playerWriteErrorEvent, message, messageLength);
}

template <typename T>
void _sendMessage(T* obj, tcp::socket* socket, SocketData* data, 
    std::function<void(T*, string)> errorEventHandler, char message[], size_t messageLength)
{
    // The socket is not currently sending data if there are no buffers in the queue
    bool socketIsIdle = data->outgoingBuffers.empty();

    // Create a new buffer on the heap to store this message while it's being sent
    // Also we add 2 bytes since a message header will be added at the beginning
    size_t finalMessageLength = messageLength + 2;
    unique_ptr<char[]> heapBuffer = std::make_unique<char[]>(finalMessageLength);
    boost::endian::store_big_u16(reinterpret_cast<unsigned char*>(heapBuffer.get()), messageLength);
    std::copy(message, message + messageLength, heapBuffer.get() + 2);

    // Create a boost wrapper for the buffer
    asio::mutable_buffer buffer = asio::buffer(heapBuffer.get(), finalMessageLength);

    // Add the new buffer to the back of the queue
    data->outgoingBuffers.push_back(std::make_tuple(std::move(heapBuffer), finalMessageLength));

    // Start the sending operation if the socket is idle
    if (socketIsIdle)
        asio::async_write(*socket, buffer, 
            std::bind(_asyncWriteHandler<T>, obj, socket, data, errorEventHandler, _1, _2));
}

template <typename T>
void _asyncWriteHandler(T* obj, tcp::socket* socket, SocketData* data, 
    std::function<void(T*, string)> errorEventHandler, const error_code& error, size_t bytes_transferred)
{
    if (!error) {
        // Yank out the front element, since we just finished sending it
        data->outgoingBuffers.pop_front();

        // If there are more buffers to send, start sending them
        if (!data->outgoingBuffers.empty()) {
            asio::mutable_buffer buffer = asio::buffer(
                std::get<0>(data->outgoingBuffers.front()).get(), 
                std::get<1>(data->outgoingBuffers.front()));

            asio::async_write(*socket, buffer, 
                std::bind(_asyncWriteHandler<T>, obj, socket, data, errorEventHandler, _1, _2));
        }
    } else {
        errorEventHandler(obj, error.message());
    }
}

void _clientWriteErrorEvent(LobbyClient* client, string message) 
{
    unique_ptr<ClientWriteErrorEvent> event = std::make_unique<ClientWriteErrorEvent>();
    event->client = client;
    event->error_message = message;
    client->loop->events.push_back(std::move(event));
}

void _playerWriteErrorEvent(Player* player, string message) 
{
    unique_ptr<PlayerWriteErrorEvent> event = std::make_unique<PlayerWriteErrorEvent>();
    event->player = player;
    event->error_message = message;
    player->server->loop->events.push_back(std::move(event));
}

void stopListening(LobbyServer* server) 
{
    if (server->acceptor) {
        error_code error;
        server->acceptor->cancel(error);
        server->acceptor->close(error);
        server->acceptor = nullptr;
    }
}

// Manage players
void disconnectPlayer(Player* player) 
{
    error_code error;
    player->socket->shutdown(tcp::socket::shutdown_both, error);
    player->socket->close(error);
}

void deletePlayer(Player* player) 
{
    std::set<unique_ptr<Player>>::iterator iterator = 
        std::find_if(player->server->players.begin(), player->server->players.end(), 
            [=](const unique_ptr<Player>& ptr) {
                return ptr.get() == player;
        });

    player->server->players.erase(iterator);
}

}
