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
        throw std::runtime_error("Timed out driving authoritative stomp");
    };
    require(ground.connect("127.0.0.1",server.port(),"GROUND"),"ground pilot connects");
    drive([&] { return ground.welcomed(); });
    require(flyer.connect("127.0.0.1",server.port(),"FLYER"),"flying pilot connects");
    drive([&] { return flyer.welcomed() && ground.latest().players.size() == 2; });
    ground.ready(true); flyer.ready(true);
    drive([&] { return ground.latest().phase == tw::Phase::Playing && flyer.latest().phase == tw::Phase::Playing; });
    drive([&] { return flyer.latest().tanks[1].position.x <= 500; },{-1,-tw::pi,0,false,0});
    drive([&] { return flyer.latest().tanks[1].position.y < 400; },{0,-tw::pi,0,false,1});
    drive([&] { return flyer.latest().tanks[1].position.x <= 305; },{-1,-tw::pi,0,false,1});
    require(flyer.latest().tanks[0].hp == config.titanHealth,"flight alone does not damage the target");
    drive([&] { return ground.latest().phase == tw::Phase::Victory && flyer.latest().phase == tw::Phase::Victory; },{0,-tw::pi,0,false,-1});
    for (const auto* state : {&ground.latest(),&flyer.latest()}) {
        require(state->winner == 1 && state->tanks[0].hp == 0 && state->tanks[1].hp == config.titanHealth,"both clients agree on lethal stomp and winner");
        require(state->tanks[0].maxHp == config.titanHealth && state->tanks[1].maxHp == config.titanHealth,"both clients receive the authoritative health capacity");
        require(std::ranges::any_of(state->events,[](const auto& e) { return e.kind == tw::EventKind::Destroyed && e.player == 0; }),"destruction event replicated to each pilot");
    }
}
}
int main() {
    try { run(); std::cout << "Authoritative stomp passed for both TCP clients\n"; return 0; }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
