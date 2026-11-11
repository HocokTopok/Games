#include "tankwars/GameSession.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void run() {
    tw::Tank ordinary;
    ordinary.hp = std::numeric_limits<float>::max();
    tw::Physics::damage(ordinary,0,true);
    require(ordinary.hp == 0,"laser is lethal regardless of finite HP");
    tw::Tank titan; titan.titan = true; titan.maxHp = titan.hp = 1000;
    tw::Physics::damage(titan,25);
    require(titan.hp == 975 && std::abs(titan.healthRatio()-0.975f) < 0.001f,"Titan has finite health and correctly scaled HP bar");
    tw::Physics::damage(titan,2000);
    require(titan.hp == 0,"ordinary damage can destroy a Titan and never makes HP negative");
    titan.hp = 1000;
    tw::Physics::crush(titan);
    require(!titan.alive(),"stomp remains lethal at full Titan health");

    tw::Config config;
    config.titanMode = true; config.asymmetricLoadouts = false; config.hillHeight = 0;
    tw::World world(config);
    world.reset({{0,0,0,true,true,false,"A"},{1,1,1,true,true,false,"B"}});
    for (const auto& tank : world.tanks()) require(tank.hp == 1000 && tank.maxHp == 1000,"both pilots spawn with 1000 HP");
    std::array<tw::Input,tw::maxPlayers> inputs{};
    inputs[0].aim = 0; inputs[1].aim = -tw::pi;
    for (int i = 0; i < 240; ++i) world.step(inputs);
    inputs[0].fire = true; world.step(inputs);
    const auto shot = world.snapshot();
    require(shot.tanks[0].titan && shot.tanks[1].titan,"both players receive Titan armor");
    require(shot.lasers.size() == 1 && shot.projectiles.empty(),"laser resolves in the firing tick");
    require(shot.tanks[0].hp == 1000 && shot.tanks[1].hp == 0,"laser remains lethal against a full-health Titan");
    require(shot.lasers[0].to.x < shot.tanks[1].position.x,"laser stops at the first tank");

    config.asymmetricLoadouts = true; config.worldWidth = 1000;
    tw::GameSession duel(config);
    duel.addPlayer("GROUND"); duel.addPlayer("FLYER");
    duel.setReady(0,true); duel.setReady(1,true); duel.step();
    require(duel.snapshot().tanks[1].canFly,"second pilot can jump and fly");
    for (int i = 0; i < 600; ++i) {
        duel.setInput(1,{-1,-tw::pi,0,false,0}); duel.step();
    }
    const float floor = duel.snapshot().tanks[1].position.y;
    for (int i = 0; i < 360; ++i) {
        duel.setInput(1,{-1,-tw::pi,0,false,1}); duel.step();
    }
    require(duel.snapshot().tanks[1].position.y < floor-100,"flyer rises above the target");
    require(duel.snapshot().tanks[0].hp == 1000,"passing above a tank is not a stomp");
    for (int i = 0; i < 1200 && duel.snapshot().phase == tw::Phase::Playing; ++i) {
        duel.setInput(1,{0,-tw::pi,0,false,0}); duel.step();
    }
    const auto landed = duel.snapshot();
    require(landed.phase == tw::Phase::Victory && landed.winner == 1,"landing on a Titan ends the match with a victory");
    require(landed.tanks[0].hp == 0 && landed.tanks[1].hp == 1000,"stomp destroys only the victim");
    require(std::ranges::any_of(landed.events,[](const auto& event) { return event.kind == tw::EventKind::Destroyed && event.player == 0; }),"stomp destruction is broadcast as a legacy-compatible event");
    config.titanHealth = std::numeric_limits<float>::infinity();
    bool rejected = false;
    try { config.validate(); } catch (...) { rejected = true; }
    require(rejected,"non-finite Titan health is rejected");
}
}
int main() {
    try { run(); std::cout << "Titan checks passed: armor, hitscan laser, flight, lethal stomp, victory\n"; return 0; }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
