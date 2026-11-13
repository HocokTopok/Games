#include "tankwars/Network.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void run() {
    tw::Config config;
    config.titanMode = true; config.asymmetricLoadouts = true;
    config.worldWidth = 1000; config.spawnInset = 300; config.tankSpeed = 300; config.hillHeight = 0;
    tw::net::Server server(config,0);
    tw::net::Client ground,flyer;
    const auto drive = [&](auto predicate,tw::Input input = {}) {
        const auto deadline = std::chrono::steady_clock::now()+5s;
        while (std::chrono::steady_clock::now() < deadline) {
            ground.sendInput({0,0,0,false}); flyer.sendInput(input);
            ground.poll(); flyer.poll();
            if (predicate()) return;
            std::this_thread::sleep_for(5ms);
        }
        throw std::runtime_error("Timed out driving authoritative contact");
    };
    require(ground.connect("127.0.0.1",server.port(),"GROUND"),"ground pilot connects");
    drive([&] { return ground.welcomed(); });
    require(flyer.connect("127.0.0.1",server.port(),"FLYER"),"flying pilot connects");
    drive([&] { return flyer.welcomed() && ground.latest().players.size() == 2; });
    ground.ready(true); flyer.ready(true);
    drive([&] { return ground.latest().phase == tw::Phase::Playing && flyer.latest().phase == tw::Phase::Playing; });
    drive([&] { return flyer.latest().tanks[1].position.x <= 210; },{-1,-tw::pi,0,false,0});
    require(flyer.latest().tanks[0].hp==config.titanHealth && flyer.latest().tanks[1].hp==config.titanHealth,"ground tanks pass through each other without damage");
    drive([&] { return flyer.latest().tanks[1].position.y < 400; },{0,-tw::pi,0,false,1});
    drive([&] { return flyer.latest().tanks[1].position.x >= 300; },{1,-tw::pi,0,false,1});
    const float floor=config.groundHeight-config.tankHalfHeight;
    drive([&] { return ground.latest().tanks[1].position.y>=floor-0.01f && flyer.latest().tanks[1].position.y>=floor-0.01f; },{0,-tw::pi,0,false,-1});
    for (const auto* state : {&ground.latest(),&flyer.latest()}) {
        require(state->phase==tw::Phase::Playing && state->winner==tw::noPlayer,"contact does not end the match");
        require(state->tanks[0].hp==config.titanHealth && state->tanks[1].hp==config.titanHealth,"both clients agree on harmless landing");
        require(state->tanks[0].maxHp==config.titanHealth && state->tanks[1].maxHp==config.titanHealth,"both clients receive authoritative health capacity");
        require(state->events.empty(),"no contact impact or destruction is replicated");
        require(std::abs(state->tanks[0].position.x-state->tanks[1].position.x)<config.tankHalfWidth,"tanks can occupy the same ground position");
    }
}
}
int main() {
    try { run(); std::cout << "Authoritative pass-through and harmless landing passed for both TCP clients\n"; return 0; }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
