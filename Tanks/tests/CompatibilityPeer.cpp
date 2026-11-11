#include "tankwars/Network.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc,char** argv) {
    using namespace std::chrono_literals;
    try {
        if (argc < 3) throw std::runtime_error("Usage: compatibility_peer server|client PORT [CONFIG]");
        const auto port = static_cast<unsigned short>(std::stoul(argv[2]));
        if (std::string(argv[1]) == "server") {
            auto config = argc > 3 ? tw::Config::load(argv[3]) : tw::Config{};
            tw::net::Server server(config,port);
            std::cout << "READY " << server.port() << std::endl;
            std::this_thread::sleep_for(8s);
            return 0;
        }
        tw::net::Client client;
        if (!client.connect("127.0.0.1",port,"COMPAT")) throw std::runtime_error(client.error());
        const auto deadline = std::chrono::steady_clock::now()+6s;
        auto playedSince = deadline;
        bool ready = false, playing = false, shot = false;
        float firstX = 0, latestX = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            client.poll();
            if (!client.connected()) throw std::runtime_error(client.error());
            if (client.welcomed() && !ready) {
                client.ready(true); ready = true;
                std::cout << "WELCOME " << static_cast<unsigned>(client.id()) << std::endl;
            }
            const auto& state = client.latest();
            if (state.phase == tw::Phase::Playing) {
                const auto* tank = tw::findTank(state,client.id());
                if (tank) {
#ifdef TANKWARS_NATIVE_HEALTH
                    if (tank->hp < 0 || tank->hp > tank->maxHp) throw std::runtime_error("Invalid native health capacity");
                    if (client.config().titanMode && tank->maxHp != client.config().titanHealth) throw std::runtime_error("Titan health not synchronized");
#else
                    if (tank->hp < 0 || tank->hp > 100) throw std::runtime_error("Legacy health was not normalized to 0-100");
#endif
                    if (!playing) { firstX = tank->position.x; playing = true; playedSince = std::chrono::steady_clock::now(); }
                    latestX = tank->position.x;
                }
            }
            for (const auto& event : state.events) if (event.kind == tw::EventKind::Shot) shot = true;
            const bool left = client.id() == 0;
            client.sendInput({left ? 1.f : -1.f,left ? -0.6f : -2.5f,0,true});
            if (playing && std::chrono::steady_clock::now()-playedSince > 1200ms) {
                if (std::abs(latestX-firstX) < 10 || !shot) throw std::runtime_error("Missing synchronized movement or firing");
                std::cout << "COMPAT OK " << static_cast<unsigned>(client.id()) << std::endl;
                return 0;
            }
            std::this_thread::sleep_for(5ms);
        }
        throw std::runtime_error("Compatibility match did not start");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
