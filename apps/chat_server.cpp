#include "LobbyLib/LobbyLib.h"

#include <memory>
#include <thread>
#include <chrono>
#include <iostream>
#include <algorithm>

#include <atomic>
#include <mutex>

std::atomic_bool server_on(true);

void safe_print(std::string str)
{
    static std::mutex cout_mutex;
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << str << std::endl;
}

void run_server()
{
    safe_print("Server starting!");

    LL::LoopPtr loop(LL::createLoop(), LL::deleteLoop);
    LL::ServerPtr server(LL::createLobbyServer(loop.get()), LL::deleteLobbyServer);

    LL::startListening(server.get(), "54621");

    while (server_on)
    {
        LL::processEvents(loop.get());
        while (LL::hasEvents(loop.get())) {
            LL::EventPtr event(LL::getNextEvent(loop.get()));

            
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    safe_print("Server stopping!");
}

int main()
{
    // Start the server in another thread
    std::thread server_thread(run_server);

    // Do other thread stuff
    std::string input;

    while (true) {
        std::getline(std::cin, input);
        std::transform(input.begin(), input.end(), input.begin(), [](char c){ return std::toupper(c); });

        if (input == "QUIT") {
            break;
        } else {
            safe_print("Unknown command! Type ? for help");
        }
    }

    // Stop the server and exit
    server_on = false;
    server_thread.join();

    return 0;
}