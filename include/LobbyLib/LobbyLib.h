#pragma once

#include <string>
#include <memory>
#include <list>

namespace LL
{

struct LobbyLibLoop;

struct LobbyClient;

struct LobbyServer;
struct Lobby;
struct Player;

enum LobbyUpdate : unsigned char {
    PlayerJoined = 0,
    PlayerLeft = 1
};

enum UsernameValidity : unsigned char {
    Valid = 0, 
    TooShort = 1, 
    TooLong = 2, 
    InvalidCharacters = 3
};

enum LobbyErrorCode : unsigned char {
    NoError = 0,
    DoesNotExist = 1,
    IsNotOpen = 2,
    IsFull = 3
};

enum EventType {
    Connected, ConnectError, 
    Accepted, AcceptError, 
    ClientDisconnected, PlayerDisconnected, 
    ClientReadError, PlayerReadError,
    ClientWriteError, PlayerWriteError,
    ClientJoinedLobby, ClientLobbyUpdated,
    LobbyCreated, LobbyDestroyed, 
    PlayerLobbyUpdated,
    LobbyError, UsernameError
};

struct Event {
    Event(EventType _type) : type(_type) {}
    EventType type;
};

struct ConnectedEvent : public Event {
    ConnectedEvent() : Event(Connected) {}
    LobbyClient* client;
    std::string host;
    std::string port;
};

struct ConnectErrorEvent : public Event {
    ConnectErrorEvent() : Event(ConnectError) {}
    LobbyClient* client;
    std::string error_message;
    std::string host;
    std::string port;
};

struct AcceptedEvent : public Event {
    AcceptedEvent() : Event(Accepted) {}
    Player* player;
    LobbyServer* server;
    std::string ip;
    std::string port;
};

struct AcceptErrorEvent : public Event {
    AcceptErrorEvent() : Event(AcceptError) {}
    LobbyServer* server;
    std::string error_message;
    std::string port;
};

struct ClientDisconnectedEvent : public Event {
    ClientDisconnectedEvent() : Event(ClientDisconnected) {}
    LobbyClient* client;
};

struct PlayerDisconnectedEvent : public Event {
    PlayerDisconnectedEvent() : Event(PlayerDisconnected) {}
    Player* player;
};

struct ClientReadErrorEvent : public Event {
    ClientReadErrorEvent() : Event(ClientReadError) {}
    LobbyClient* client;
    std::string error_message;
};

struct PlayerReadErrorEvent : public Event {
    PlayerReadErrorEvent() : Event(PlayerReadError) {}
    Player* player;
    std::string error_message;
};

struct ClientWriteErrorEvent : public Event {
    ClientWriteErrorEvent() : Event(ClientWriteError) {}
    LobbyClient* client;
    std::string error_message;
};

struct PlayerWriteErrorEvent : public Event {
    PlayerWriteErrorEvent() : Event(PlayerWriteError) {}
    Player* player;
    std::string error_message;
};

struct ClientJoinedLobbyEvent : public Event {
    ClientJoinedLobbyEvent() : Event(ClientJoinedLobby) {}
    LobbyClient* client;
    std::string lobbyCode;
    std::list<std::string> lobbyPlayers;
};

struct ClientLobbyUpdatedEvent : public Event {
    ClientLobbyUpdatedEvent() : Event(ClientLobbyUpdated) {}
    LobbyClient* client;
    LobbyUpdate updateType;
    std::string username;
};

struct LobbyCreatedEvent : public Event {
    LobbyCreatedEvent() : Event(LobbyCreated) {}
    Lobby* lobby;
    std::string code;

    Player* player;
    std::string username;
};

struct LobbyDestroyedEvent : public Event {
    LobbyDestroyedEvent() : Event(LobbyDestroyed) {}
    Lobby* lobby;
    std::string code;
    
    Player* player;
    std::string username;
};

struct PlayerLobbyUpdatedEvent : public Event {
    PlayerLobbyUpdatedEvent() : Event(PlayerLobbyUpdated) {}
    Player* player;
    LobbyUpdate updateType;
    std::string username;
};

struct LobbyErrorEvent : public Event {
    LobbyErrorEvent() : Event(LobbyError) {}
    LobbyClient* client;
    LobbyErrorCode error;
    std::string error_message;
};

struct UsernameErrorEvent : public Event {
    UsernameErrorEvent() : Event(UsernameError) {}
    LobbyClient* client;
    UsernameValidity validity;
    std::string error_message;
};

typedef std::unique_ptr<LobbyLibLoop, void(*)(LobbyLibLoop*)> LoopPtr;
typedef std::unique_ptr<LobbyServer, void(*)(LobbyServer*)> ServerPtr;
typedef std::unique_ptr<LobbyClient, void(*)(LobbyClient*)> ClientPtr;
typedef std::unique_ptr<Event> EventPtr;


/**
 * Create a new event loop
 * The loop must be deleted with deleteLoop() once it is done being used
 */
LobbyLibLoop* createLoop();

/**
 * Delete a loop and any unprocessed events
 */
void deleteLoop(LobbyLibLoop* loop);

/**
 * Process pending actions and generate events. This function must be called
 * periodically in order for network operations to function. Events should be
 * retrieved and responded to after calling this function.
 */
void processEvents(LobbyLibLoop* loop);

/**
 * Check if the loop contains any unprocessed events
 */
bool hasEvents(LobbyLibLoop* loop);

/**
 * Get the next unprocessed event from the event loop. The given Event* is a
 * heap allocated object and ownership is transferred to the caller of this
 * function. (Delete should be called on the Event* after it is no longer needed)
 */
Event* getNextEvent(LobbyLibLoop* loop);

// ------------ CLIENT INTERFACE ------------

/**
 * Create a new lobby client attached to the given loop.
 * The client must be deleted with deleteClient() once it is done being used
 */
LobbyClient* createLobbyClient(LobbyLibLoop* loop);

/**
 * Delete a lobby client. Warning!! Ensure that the client is not connected
 * before calling this function. Calling this function on an active client
 * will cause undefined behavior!
 */
void deleteClient(LobbyClient* client);

/**
 * Connect a client to the specified host with the given hostname and port number
 */
void connectToServer(LobbyClient* client, std::string host, std::string port);

/**
 * Disconnect from any active network communications
 */
void disconnectClient(LobbyClient* client);

// Check if a username meets the validity requirements
UsernameValidity checkUsername(std::string username);

// Join/Create lobby
void joinExistingLobby(LobbyClient* client, std::string lobbyId, std::string username);
void createNewLobby(LobbyClient* client, std::string username);

// Send messages
void sendChatMessage(LobbyClient* client, std::string message);
void sendGameMessage(LobbyClient* client, char bytes[], int numberOfBytes);

// ------------ SERVER INTERFACE ------------

// Create/Delete a lobby server
LobbyServer* createLobbyServer(LobbyLibLoop* loop);
void deleteLobbyServer(LobbyServer* server);

// Start/Stop listening
void startListening(LobbyServer* server, std::string port);
void stopListening(LobbyServer* server);

// Manage players
void disconnectPlayer(Player* player);
void deletePlayer(Player* player);

// Send messages
void sendGameMessage(Player* player, char bytes[], int numberOfBytes);
void broadcastGameMessage(Lobby* lobby, char bytes[], int numberOfBytes);

}
