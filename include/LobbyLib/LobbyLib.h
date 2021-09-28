#pragma once

#include <string>
#include <memory>

namespace LL
{

struct LobbyLibLoop;

struct LobbyClient;

struct LobbyServer;
struct Lobby;
struct Player;

enum EventType {
    Connected, ConnectError, 
    Accepted, AcceptError, 
    ServerDisconnected, PlayerDisconnected, 
    ClientReadError, PlayerReadError,
    ClientWriteError, PlayerWriteError
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
    ClientDisconnectedEvent() : Event(ServerDisconnected) {}
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
 * The client must be deleted with deleteLobbyClient() once it is done being used
 */
LobbyClient* createLobbyClient(LobbyLibLoop* loop);

/**
 * Delete a lobby client and close any associated network connections
 */
void deleteLobbyClient(LobbyClient* client);

/**
 * Connect a client to the specified host with the given hostname and port number
 */
void connectToServer(LobbyClient* client, std::string host, std::string port);

/**
 * Disconnect from any active network communications
 */
void disconnectFromServer(LobbyClient* client);

// Set username
void setUsername(LobbyClient* client, std::string username);

// Join/Create lobby
void joinExistingLobby(LobbyClient* client, std::string lobbyId);
void createNewLobby(LobbyClient* client);

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
