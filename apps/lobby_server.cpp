#include "LobbyLib/LobbyLib.h"

#include <memory>
#include <thread>
#include <chrono>
#include <vector>
#include <iostream>

int main()
{
    LL::LoopPtr loop(LL::createLoop(), LL::deleteLoop);
    LL::ServerPtr server(LL::createLobbyServer(loop.get()), LL::deleteLobbyServer);
    LL::ClientPtr client(LL::createLobbyClient(loop.get()), LL::deleteClient);

    LL::startListening(server.get(), "54621");
    LL::connectToServer(client.get(), "127.0.0.1", "54621");

    for (int i = 0; i < 300; i++)
    {
        if (i % 10 == 0) {
            std::cout << i << std::endl;
        }

        if (i > 150) {
            server.reset();
        }

        LL::processEvents(loop.get());
        while (LL::hasEvents(loop.get())) {
            LL::EventPtr event(LL::getNextEvent(loop.get()));

            if (event->type == LL::EventType::Connected) {
                LL::ConnectedEvent* connectedEvent = static_cast<LL::ConnectedEvent*>(event.get());
                std::cout << "Connected!" << std::endl;
                std::cout << "Host: " << connectedEvent->host << "  Port: " << connectedEvent->port << std::endl;
            } else if (event->type == LL::EventType::ConnectError) {
                LL::ConnectErrorEvent* errorEvent = static_cast<LL::ConnectErrorEvent*>(event.get());
                std::cout << "Error! " << errorEvent->error_message << std::endl;
            } else if (event->type == LL::EventType::Accepted) {
                LL::AcceptedEvent* acceptedEvent = static_cast<LL::AcceptedEvent*>(event.get());
                std::cout << "Connection Accepted!" << std::endl;
                std::cout << "IP: " << acceptedEvent->ip << "   Port: " << acceptedEvent->port << std::endl;
            } else if (event->type == LL::EventType::PlayerDisconnected) {
                std::cout << "Player disconnected!" << std::endl;
            } else if (event->type == LL::EventType::ClientDisconnected) {
                std::cout << "Client disconnected!" << std::endl;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}