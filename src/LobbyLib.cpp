#include "LobbyLib/LobbyLib.h"

#include <list>
#include <deque>
#include <memory>
#include <numeric>
#include <unordered_set>
#include <unordered_map>

#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>

#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>

using std::size_t;
using std::string;
using std::unique_ptr;
using boost::asio::ip::tcp;
using boost::system::error_code;

namespace asio = boost::asio;
using namespace std::placeholders;

namespace LL
{

// Username constraints
const unsigned short MAX_USERNAME_LENGTH = 20;
const unsigned short MIN_USERNAME_CHAR = '!';
const unsigned short MAX_USERNAME_CHAR = '~';

// Number of digits in the lobby code
const int CODE_SIZE = 4;

// Size of the network message header
const unsigned short HEADER_SIZE = 2;

// Keep alive settings
const unsigned short INACTIVITY_TIMEOUT = 5000;
const unsigned short REPLY_TIMEOUT = 1000;
const unsigned short KEEP_ALIVE_TRIES = 3;

std::unique_ptr<boost::random::mt11213b> RANDOM_GENERATOR;

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

bool _hasEnoughContent(const SocketData& data, unsigned short required_size);
void _handleMessage(LobbyClient* client);
void _notifyInvalidUsername(Player* player, UsernameValidity validity);
void _handleMessage(Player* player);

UsernameValidity _isUsernameValid(const char* username, unsigned short length);
std::string _generateLobbyCode(int length = CODE_SIZE);

void _clientDisconnectedEvent(LobbyClient* client);
void _playerDisconnectedEvent(Player* player);

void _clientReadErrorEvent(LobbyClient* client, string message);
void _playerReadErrorEvent(Player* player, string message);

void _clientKeepAliveTimeout(LobbyClient* client, const error_code& error);
void _playerKeepAliveTimeout(Player* player, const error_code& error);

void _sendMessage(LobbyClient* client, const char* message, const size_t messageLength);
void _sendMessage(LobbyClient* client, const char** messageParts, const size_t* messageLengths, int numberOfParts);
void _sendMessage(Player* player, const char* message, const size_t messageLength);
void _sendMessage(Player* player, const char** messageParts, const size_t* messageLengths, int numberOfParts);
template <typename T>
void _sendMessage(T* obj, tcp::socket* socket, SocketData* data,
    std::function<void(T*, string)> errorEventHandler, const char** messageParts, const size_t* partLengths, int numberOfParts);
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
    std::list<string> lobbyPlayers;

    unsigned short keepAliveTimeout = INACTIVITY_TIMEOUT + REPLY_TIMEOUT * KEEP_ALIVE_TRIES;
    unique_ptr<asio::steady_timer> keepAliveTimer;
};

struct Lobby
{
    string code;
    std::list<Player*> players;
};
struct Player
{
    Lobby* lobby = nullptr;
    string username;
    LobbyServer* server;
    unique_ptr<tcp::socket> socket;
    SocketData socketData;

    int keepAliveTries = 0;
    bool hasSentFirstPing = false;
    unique_ptr<asio::steady_timer> keepAliveTimer;
};
struct LobbyServer 
{
    LobbyLibLoop* loop;
    unique_ptr<tcp::acceptor> acceptor;
    std::unordered_set<unique_ptr<Player>> players;
    std::unordered_map<string, unique_ptr<Lobby>> lobbies;
};

enum ClientMessages : unsigned char {
    PING = 0,
    JOINED_LOBBY = 1,
    LOBBY_UPDATED = 2,
    LOBBY_ERROR = 3,
    USERNAME_ERROR = 4,
};

enum PlayerMessages : unsigned char {
    PONG = 0,
    CREATE_LOBBY = 1,
    JOIN_EXISTING_LOBBY = 2
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

// Create a lobby client
LobbyClient* createLobbyClient(LobbyLibLoop* loop) 
{
    LobbyClient* client = new LobbyClient();
    client->loop = loop;
    return client;
}

// Delete a lobby client
void deleteClient(LobbyClient* client) 
{
    // To safely delete a client we must first disconnect it and then process the
    // remaining event handlers
    disconnectClient(client);
    processEvents(client->loop);
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
                                client->loop->io_context, asio::chrono::milliseconds(client->keepAliveTimeout));
                            client->keepAliveTimer->async_wait(std::bind(_clientKeepAliveTimeout, client, _1));

                            using sig = void(*)(LobbyClient*, bool, const error_code&, size_t);

                            // Start infinitely reading from the socket
                            asio::async_read(*client->socket, asio::buffer(client->socketData.header, HEADER_SIZE), 
                                std::bind(static_cast<sig>(_asyncReadHandler), client, true, _1, _2));
                        } else {
                            if (error != boost::asio::error::operation_aborted)
                                _connectErrorEvent(client, error2.message(), host, port);
                        }
                    }
                );
            } else {
                if (error != boost::asio::error::operation_aborted)
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

void disconnectClient(LobbyClient* client) 
{
    if (client->resolver)
        client->resolver->cancel();

    if (client->socket) {
        error_code error;
        client->socket->shutdown(tcp::socket::shutdown_both, error);
        client->socket->close(error);
    }
}


// Set username
UsernameValidity checkUsername(string username) 
{
    return _isUsernameValid(username.c_str(), username.length());
}


// Join/Create lobby
void joinExistingLobby(LobbyClient* client, string lobbyId, string username) 
{
    // Send the server a message asking to join an existing lobby and giving our username
    size_t messageLengths[] = {1, lobbyId.size() + 1, username.size() + 1};
    const char part1[] = {PlayerMessages::JOIN_EXISTING_LOBBY};
    const char* messageParts[] = {part1, lobbyId.c_str(), username.c_str()};
    _sendMessage(client, messageParts, messageLengths, 3);
}

void createNewLobby(LobbyClient* client, string username) 
{
    // Send the server a message asking to make a new lobby and giving our username
    size_t messageLengths[] = {1, username.length()};
    const char part1[] = {PlayerMessages::CREATE_LOBBY};
    const char* messageParts[] = {part1, username.c_str()};
    _sendMessage(client, messageParts, messageLengths, 2);
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
    // Clean up the acceptor
    stopListening(server);

    // Clean up the players
    for (const unique_ptr<Player> &player : server->players)
        disconnectPlayer(player.get());

    // Allow handlers to exit
    processEvents(server->loop);

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
        server->players.insert(std::move(player_ptr));

        // Start the keep alive timer for this socket and send the first ping
        player->keepAliveTimer = std::make_unique<asio::steady_timer>(
            server->loop->io_context, asio::chrono::milliseconds(0));
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

        // Restart so we can accept more connections
        server->acceptor->async_accept(std::bind(_asyncAcceptHandler, server, port, _1, _2));
    } else {
        if (error != asio::error::operation_aborted)
            _acceptErrorEvent(server, error.message(), port);
        
        // Delete the acceptor
        server->acceptor = nullptr;
    }
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

            // Start reading the header of the next message
            asio::async_read(*socket, asio::buffer(messageData->header, HEADER_SIZE), 
                std::bind(static_cast<sig>(_asyncReadHandler), obj, true, _1, _2));
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

bool _hasEnoughContent(const SocketData& data, unsigned short required_size)
{
    return data.content_size >= required_size;
}

void _handleMessage(LobbyClient* client) 
{
    SocketData& socketData = client->socketData;

    // The first byte of the message indicates what type of message it is
    uint8_t messageType = socketData.buffer[0];

    switch (messageType) {
        // Ping!
        case ClientMessages::PING: {
            // If the client timeout seconds are included in this ping, extract them
            if (_hasEnoughContent(socketData, 3)) {
                client->keepAliveTimeout = boost::endian::load_big_u16(
                    reinterpret_cast<unsigned char*>(socketData.buffer.get() + 1));
            }
            
            client->keepAliveTimer->cancel();
            // Send a "pong" !
            char message[] = {PlayerMessages::PONG};
            _sendMessage(client, message, 1);
            break;
        }
        // Joined lobby
        case ClientMessages::JOINED_LOBBY: {
            // Check if there is enough data for the lobby code and the last character in the content is a null terminator
            if (!_hasEnoughContent(socketData, 2) && socketData.buffer[socketData.content_size - 1] == '\0')
                return;

            // Extract the lobby code itself
            string code = string(socketData.buffer.get() + 1);

            // Get the list of players currently in the lobby
            std::list<string> playerList;

            // If there is at least one string in the username list
            if (_hasEnoughContent(socketData, 1 + code.size() + 2)) 
            {
                char* iterator = socketData.buffer.get() + 1 + code.size() + 1;
                while (iterator < socketData.buffer.get() + socketData.content_size) {
                    playerList.push_back(string(iterator));
                    iterator += playerList.back().size() + 1;
                }
            }

            // Create a lobby joined event
            unique_ptr<ClientJoinedLobbyEvent> event = std::make_unique<ClientJoinedLobbyEvent>();
            event->client = client;
            event->lobbyCode = code;
            event->lobbyPlayers = playerList;
            client->loop->events.push_back(std::move(event));

            // Move the list into the client
            client->lobbyPlayers = std::move(playerList);
            break;
        }
        // Lobby updated (a player joined/left)
        case ClientMessages::LOBBY_UPDATED: {
            if (!_hasEnoughContent(socketData, 2))
                return;

            // Get the update type
            LobbyUpdate updateType = static_cast<LobbyUpdate>(reinterpret_cast<unsigned char&>(socketData.buffer[1]));

            // A player has joined or left the lobby
            if (updateType == LobbyUpdate::PlayerJoined || updateType == LobbyUpdate::PlayerLeft) {
                if (!_hasEnoughContent(socketData, 3))
                    return;

                // Extract the player's username
                string username = string(socketData.buffer.get() + 2, socketData.buffer_size - 2);

                if (updateType == LobbyUpdate::PlayerJoined) {
                    // Add the player to this client's lobby player list
                    client->lobbyPlayers.push_back(username);
                } else {
                    // Remove the player from this client's lobby player list
                    auto iterator = std::find(client->lobbyPlayers.begin(), client->lobbyPlayers.end(), username);
                    if (iterator != client->lobbyPlayers.end())
                        client->lobbyPlayers.erase(iterator);
                }

                // Create the lobby update event
                unique_ptr<ClientLobbyUpdatedEvent> event = std::make_unique<ClientLobbyUpdatedEvent>();
                event->client = client;
                event->updateType = updateType;
                event->username = username;
                client->loop->events.push_back(std::move(event));
            }
            break;
        }
        // Lobby error
        case ClientMessages::LOBBY_ERROR: {
            if (!_hasEnoughContent(socketData, 2))
                return;

            // Extract the error code and error message and create an event
            unique_ptr<LobbyErrorEvent> event = std::make_unique<LobbyErrorEvent>();
            event->client = client;
            event->error = static_cast<LobbyErrorCode>(reinterpret_cast<unsigned char&>(socketData.buffer[1]));;
            event->error_message = string(socketData.buffer.get() + 2, socketData.content_size - 2);
            client->loop->events.push_back(std::move(event));
            break;
        }
        // Username error
        case ClientMessages::USERNAME_ERROR: {
            if (!_hasEnoughContent(socketData, 2))
                return;

            // Extract the error code and error message and create an event
            unique_ptr<UsernameErrorEvent> event = std::make_unique<UsernameErrorEvent>();
            event->client = client;
            event->validity = static_cast<UsernameValidity>(reinterpret_cast<unsigned char&>(socketData.buffer[1]));;
            event->error_message = string(socketData.buffer.get() + 2, socketData.content_size - 2);
            client->loop->events.push_back(std::move(event));
            break;
        }
    }
}

void _notifyInvalidUsername(Player* player, UsernameValidity validity)
{
    // Invalid username, notify the client
    string errorMessage;
    switch (validity) {
        case TooShort:
            errorMessage = "The given username is too short";
            break;
        case TooLong:
            errorMessage = "The given username is too long";
            break;
        case InvalidCharacters:
            errorMessage = "The given username contains invalid characters";
            break;
    }

    size_t messageLengths[] = {1, 1, errorMessage.length()};
    const char part1[] = {ClientMessages::USERNAME_ERROR};
    const char part2[] = {reinterpret_cast<char&>(validity)};
    const char* messageParts[] = {part1, part2, errorMessage.c_str()};
    _sendMessage(player, messageParts, messageLengths, 3);
}

void _handleMessage(Player* player)
{
    SocketData& socketData = player->socketData;

    // The first byte of the message indicates what type of message it is
    uint8_t messageType = socketData.buffer[0];

    switch (messageType) {
        // Pong!
        case PlayerMessages::PONG: {
            player->keepAliveTimer->cancel();
            break;
        }
        // Create new lobby
        case PlayerMessages::CREATE_LOBBY: {
            // Ignore this message if the player already has a lobby
            if (player->lobby != nullptr)
                return;

            // Check if the proposed username is valid
            char* username = socketData.buffer.get() + 1;
            unsigned short usernameLength = socketData.content_size - 1;
            UsernameValidity validity = _isUsernameValid(username, usernameLength);

            if (validity == UsernameValidity::Valid) {
                // Convert the username into a proper string
                player->username = string(username, usernameLength);

                // Find a lobby code that is not already in use
                string lobbyCode;
                do {
                    lobbyCode = _generateLobbyCode();
                } while (player->server->lobbies.find(lobbyCode) != player->server->lobbies.end());

                // Create a new lobby with the new code
                unique_ptr<Lobby> lobby = std::make_unique<Lobby>();
                lobby->code = lobbyCode;

                // Assign the player to the lobby and put the lobby in the server
                player->lobby = lobby.get();
                lobby->players.push_back(player);
                player->server->lobbies.insert(std::make_pair(lobbyCode, std::move(lobby)));

                // Create the new lobby event
                unique_ptr<LobbyCreatedEvent> event = std::make_unique<LobbyCreatedEvent>();
                event->lobby = lobby.get();
                event->code = lobbyCode;
                event->player = player;
                event->username = player->username;
                player->server->loop->events.push_back(std::move(event));

                // Let the client know that the lobby has been created
                size_t messageLengths[] = {1, lobbyCode.length() + 1};
                const char part1[] = {ClientMessages::JOINED_LOBBY};
                const char* messageParts[] = {part1, lobbyCode.c_str()};
                _sendMessage(player, messageParts, messageLengths, 2);
            } else {
                _notifyInvalidUsername(player, validity);
            }
            break;
        }
        // Join an existing lobby
        case JOIN_EXISTING_LOBBY: {
            // Make sure there is enough data for the lobby code and 
            // the last character in the content is a null terminator and
            // the player does not already have a lobby
            if (!_hasEnoughContent(socketData, 2) || 
                socketData.buffer[socketData.content_size - 1] != '\0' ||
                player->lobby != nullptr)
                return;

            // Get the code for the requested lobby
            string code = string(socketData.buffer.get() + 1);

            Lobby* lobby;
            LobbyErrorCode error;

            // Search for the lobby with the specified code
            auto iterator = player->server->lobbies.find(code);
            if (iterator != player->server->lobbies.end()) {
                lobby = iterator->second.get();
                error = LobbyErrorCode::NoError;
            } else {
                lobby = nullptr;
                error = LobbyErrorCode::DoesNotExist;
            }

            if (error == LobbyErrorCode::NoError) {
                // Check if the proposed username is valid
                string username = string(socketData.buffer.get() + 1 + code.size() + 1);
                UsernameValidity validity = _isUsernameValid(username.c_str(), username.size());

                if (validity == UsernameValidity::Valid) {
                    // Assign the player's username
                    player->username = username;

                    // Notify the client that they are now in the lobby
                    int numberOfPlayers = lobby->players.size();

                    size_t messageLengths[2 + numberOfPlayers] = {1, code.length() + 1};
                    const char part1[] = {ClientMessages::JOINED_LOBBY};
                    const char* messageParts[2 + numberOfPlayers] = {part1, code.c_str()};
                    
                    int i = 2;
                    for (Player* lobbyPlayer : lobby->players) {
                        messageLengths[i] = lobbyPlayer->username.size() + 1;
                        messageParts[i] = lobbyPlayer->username.c_str();
                        i++;
                    }

                    _sendMessage(player, messageParts, messageLengths, 2 + numberOfPlayers);

                    // Notify all other clients that there is a new lobby member
                    size_t messageLengths2[] = {1, 1, username.size()};
                    const char part1_2[] = {ClientMessages::LOBBY_UPDATED};
                    const char part2_2[] = {LobbyUpdate::PlayerJoined};
                    const char* messageParts2[] = {part1_2, part2_2, username.c_str()};
                    for (Player* lobbyPlayer : lobby->players)
                        _sendMessage(lobbyPlayer, messageParts, messageLengths, 3);
                    
                    // Place the player in the lobby
                    player->lobby = lobby;
                    lobby->players.push_back(player);

                    // Create a lobby update event
                    unique_ptr<PlayerLobbyUpdatedEvent> event = std::make_unique<PlayerLobbyUpdatedEvent>();
                    event->player = player;
                    event->updateType = LobbyUpdate::PlayerJoined;
                    event->username = player->username;
                    player->server->loop->events.push_back(std::move(event));
                } else {
                    _notifyInvalidUsername(player, validity);
                }
            } else {
                // Lobby error, notify the client
                string errorMessage;
                switch (error) {
                    case DoesNotExist:
                        errorMessage = "The specified lobby does not exist";
                        break;
                    case IsNotOpen:
                        errorMessage = "The specified lobby is not open";
                        break;
                    case IsFull:
                        errorMessage = "The specified lobby is already full";
                        break;
                }

                size_t messageLengths[] = {1, 1, errorMessage.length()};
                const char part1[] = {ClientMessages::LOBBY_ERROR};
                const char part2[] = {reinterpret_cast<char&>(error)};
                const char* messageParts[] = {part1, part2, errorMessage.c_str()};
                _sendMessage(player, messageParts, messageLengths, 3);
            }
            break;
        }
    }
}

// Check if a username is valid by checking it's length and the range of allowed characters
UsernameValidity _isUsernameValid(const char* username, unsigned short length) {
    if (length == 0)
        return TooShort;
    else if (length > MAX_USERNAME_LENGTH)
        return TooLong;

    for (int i = 0; i < length; i++) {
        char character = *(username + i);
        if (character < MIN_USERNAME_CHAR || character > MAX_USERNAME_CHAR)
            return InvalidCharacters;
    }

    return Valid;
}

std::string _generateLobbyCode(int length) 
{
    if (!RANDOM_GENERATOR)
        RANDOM_GENERATOR = std::make_unique<boost::random::mt11213b>(std::time(0));

    boost::random::uniform_int_distribution<> distribution(0, 35);

    char code[length];

    for (int i = 0; i < length; i++) {
        int value = distribution(*RANDOM_GENERATOR);
        code[i] = value + (value < 26 ? 'A' : '0' - 26);
    }

    return std::string(code, length);
}

void _clientDisconnectedEvent(LobbyClient* client) 
{
    // Cleanup operations
    disconnectClient(client);
    client->keepAliveTimer->cancel();

    unique_ptr<ClientDisconnectedEvent> event = std::make_unique<ClientDisconnectedEvent>();
    event->client = client;
    client->loop->events.push_back(std::move(event));
}

void _playerDisconnectedEvent(Player* player) 
{
    // Remove the player from their associated lobby, if necessary
    if (player->lobby != nullptr) {
        // Remove the player from the lobby
        auto iterator = std::find(player->lobby->players.begin(), player->lobby->players.end(), player);
        if (iterator != player->lobby->players.end())
            player->lobby->players.erase(iterator);

        if (player->lobby->players.empty()) {
            // Create a lobby destroyed event
            unique_ptr<LobbyDestroyedEvent> event = std::make_unique<LobbyDestroyedEvent>();
            event->lobby = player->lobby;
            event->code = player->lobby->code;
            event->player = player;
            event->username = player->username;

            // Delete the lobby if it is empty
            auto iterator2 = player->server->lobbies.find(player->lobby->code);
            if (iterator2 != player->server->lobbies.end())
                player->server->lobbies.erase(iterator2);

            player->server->loop->events.push_back(std::move(event));
        } else {
            // Notify other players that someone has left their lobby
            size_t messageLengths[] = {1, 1, player->username.size()};
            const char part1[] = {ClientMessages::LOBBY_UPDATED};
            const char part2[] = {LobbyUpdate::PlayerLeft};
            const char* messageParts[] = {part1, part2, player->username.c_str()};
            for (Player* lobbyPlayer : player->lobby->players)
                _sendMessage(lobbyPlayer, messageParts, messageLengths, 3);

            // Create a lobby updated event
            unique_ptr<PlayerLobbyUpdatedEvent> event = std::make_unique<PlayerLobbyUpdatedEvent>();
            event->player = player;
            event->updateType = LobbyUpdate::PlayerLeft;
            event->username = player->username;
            player->server->loop->events.push_back(std::move(event));
        }
    }

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
            client->keepAliveTimer->expires_after(asio::chrono::milliseconds(client->keepAliveTimeout));
            client->keepAliveTimer->async_wait(std::bind(_clientKeepAliveTimeout, client, _1));
        }
    } else {
        // The server has timed out, disconnect from it
        disconnectClient(client);
    }
}

void _playerKeepAliveTimeout(Player* player, const error_code& error) 
{
    if (error == asio::error::operation_aborted) {
        // Operation aborted means we received a "pong" before the timer expired
        if (player->socket->is_open()) {
            // Only reset the timer if the socket is still open, incase the operation was aborted
            // due to the sockect bring closed
            player->keepAliveTries = 0;
            player->keepAliveTimer->expires_after(asio::chrono::milliseconds(INACTIVITY_TIMEOUT));
            player->keepAliveTimer->async_wait(std::bind(_playerKeepAliveTimeout, player, _1));
        }
    } else {
        if (player->keepAliveTries >= KEEP_ALIVE_TRIES) {
            // The player has timed out, disconnect them
            disconnectPlayer(player);
        } else {
            // Send a "ping" and include the client timeout seconds if this is the first ping
            size_t messageLength = player->hasSentFirstPing ? 1 : 3;
            char message[messageLength];
            message[0] = ClientMessages::PING;

            if (!player->hasSentFirstPing) {
                boost::endian::store_big_u16(reinterpret_cast<unsigned char*>(message + 1), 
                    INACTIVITY_TIMEOUT + REPLY_TIMEOUT * KEEP_ALIVE_TRIES);

                player->hasSentFirstPing = true;
            }

            _sendMessage(player, message, messageLength);

            // Increment the counter for tries and start waiting for a pong
            player->keepAliveTries++;
            player->keepAliveTimer->expires_after(asio::chrono::milliseconds(REPLY_TIMEOUT));
            player->keepAliveTimer->async_wait(std::bind(_playerKeepAliveTimeout, player, _1));
        }
    }
}

void _sendMessage(LobbyClient* client, const char* message, const size_t messageLength)
{
    const char* messageParts[] = {message};
    size_t messageLengths[] = {messageLength};
    _sendMessage(client, messageParts, messageLengths, 1);
}

void _sendMessage(LobbyClient* client, const char** messageParts, const size_t* messageLengths, int numberOfParts)
{
    _sendMessage<LobbyClient>(client, client->socket.get(), &client->socketData, 
        _clientWriteErrorEvent, messageParts, messageLengths, numberOfParts);
}

void _sendMessage(Player* player, const char* message, const size_t messageLength)
{
    const char* messageParts[] = {message};
    size_t messageLengths[] = {messageLength};
    _sendMessage(player, messageParts, messageLengths, 1);
}

void _sendMessage(Player* player, const char** messageParts, const size_t* messageLengths, int numberOfParts)
{
    _sendMessage<Player>(player, player->socket.get(), &player->socketData, 
        _playerWriteErrorEvent, messageParts, messageLengths, numberOfParts);
}

template <typename T>
void _sendMessage(T* obj, tcp::socket* socket, SocketData* data,
    std::function<void(T*, string)> errorEventHandler, const char** messageParts, const size_t* partLengths, int numberOfParts)
{
    // The socket is not currently sending data if there are no buffers in the queue
    bool socketIsIdle = data->outgoingBuffers.empty();

    // Create a new buffer on the heap to store this message while it's being sent
    // Also we add 2 bytes since a message header will be added at the beginning
    size_t messageLength = std::accumulate(partLengths, partLengths + numberOfParts, 0);
    size_t finalMessageLength = 2 + messageLength;
    unique_ptr<char[]> heapBuffer = std::make_unique<char[]>(finalMessageLength);
    boost::endian::store_big_u16(reinterpret_cast<unsigned char*>(heapBuffer.get()), messageLength);

    // Copy all the parts of the message into the big buffer
    char* iterator = heapBuffer.get() + 2;
    for (int i = 0; i < numberOfParts; i++)
        iterator = std::copy(messageParts[i], messageParts[i] + partLengths[i], iterator);

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
        if (error != boost::asio::error::operation_aborted)
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
    // To safely delete a player we must first disconnect it and then process the
    // remaining event handlers
    disconnectPlayer(player);
    processEvents(player->server->loop);

    // Remove/delete the player from the server
    std::unordered_set<unique_ptr<Player>>::iterator iterator = 
        std::find_if(player->server->players.begin(), player->server->players.end(), 
            [=](const unique_ptr<Player>& ptr) {
                return ptr.get() == player;
        });

    player->server->players.erase(iterator);
}

}
