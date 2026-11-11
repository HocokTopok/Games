#include "tankwars/GameSession.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void run() {
    tw::Config config;
    const std::vector<tw::Player> players{{0,0,0,true,true,false,"HOST"},{1,1,1,true,true,false,"GUEST"}};
    std::array<tw::Input,tw::maxPlayers> input{};
    tw::World world(config); world.reset(players);
    require(world.tanks()[0].weapon==tw::WeaponKind::Napalm && world.tanks()[1].canFly && !world.tanks()[0].canFly,"host and guest get distinct abilities");
    input[0]={0,-0.7f,0,true}; world.step(input);
    require(world.snapshot().projectiles.size()==3,"napalm starts with three pellets");
    const auto pellets=world.snapshot().projectiles;
    require(pellets[0].napalm && pellets[0].velocity.y<pellets[1].velocity.y && pellets[1].velocity.y<pellets[2].velocity.y,"napalm pellets have a spread");
    for (int i=0;i<60;++i) world.step(input);
    const auto stream=world.snapshot();
    const auto shots=std::ranges::count_if(stream.events,[](const auto& e){return e.kind==tw::EventKind::Shot;});
    require(shots>=5 && stream.projectiles.size()>=15,"held fire produces a machine gun stream");
    input[0].fire=false; const auto previousShots=shots;
    for (int i=0;i<20;++i) world.step(input);
    const auto stopped=world.snapshot();
    require(std::ranges::count_if(stopped.events,[](const auto& e){return e.kind==tw::EventKind::Shot;})==previousShots,"release stops the stream");

    world.reset(players); input={};
    input[0].aim=tw::pi/2;
    for (int i=0;i<120;++i) world.step(input);
    input[0].fire=true; world.step(input); input[0].fire=false;
    const auto impact=world.snapshot();
    require(!impact.fires.empty() && impact.tanks[0].burnRemaining>0,"napalm ignites terrain and nearby tanks");
    require(impact.tanks[0].hp<100 && impact.tanks[0].hp>85,"each napalm volley has low impact damage");
    const float hp=impact.tanks[0].hp;
    for (int i=0;i<120;++i) world.step(input);
    require(std::abs((hp-world.tanks()[0].hp)-config.weapon.fireDamagePerSecond)<0.1f,"overlapping napalm pools do not stack damage");
    input[0].move=1;
    for (int i=0;i<200;++i) world.step(input);
    input[0].move=0;
    require(world.tanks()[0].burnRemaining>0,"tank remains on fire after leaving the pool");
    const float escapedHp=world.tanks()[0].hp;
    for (int i=0;i<60;++i) world.step(input);
    require(world.tanks()[0].hp<escapedHp,"burning continues to deal damage after escape");
    for (int i=0;i<120*8;++i) world.step(input);
    require(world.snapshot().fires.empty() && world.tanks()[0].burnRemaining==0,"fire and burning eventually expire");
    const float cooledHp=world.tanks()[0].hp;
    for (int i=0;i<120;++i) world.step(input);
    require(world.tanks()[0].hp==cooledHp,"expired fire stops damaging tanks");

    world.reset(players); input={};
    const float floor=world.tanks()[1].position.y;
    input[0].lift=1; input[1].lift=1; world.step(input);
    require(world.tanks()[1].position.y<floor && world.tanks()[0].verticalVelocity==0,"only the guest jumps");
    input[1].lift=0;
    for (int i=0;i<240;++i) world.step(input);
    require(std::abs(world.tanks()[1].position.y-floor)<0.01f,"tapping Up jumps and lands under gravity");
    input[1].lift=1;
    for (int i=0;i<120;++i) world.step(input);
    require(world.tanks()[1].position.y<floor-250,"holding Up sustains flight");
    for (int i=0;i<120*4;++i) world.step(input);
    require(world.tanks()[1].position.y>=config.worldHeight*0.23f,"flight stays below the scoreboard");
    input[1].lift=-1;
    for (int i=0;i<120*3;++i) world.step(input);
    require(std::abs(world.tanks()[1].position.y-floor)<0.01f && world.tanks()[1].verticalVelocity==0,"Down descends and lands without sinking");
    input[1].move=-1; input[1].lift=1;
    for (int i=0;i<120*18;++i) world.step(input);
    require(world.tanks()[1].position.x<world.tanks()[0].position.x-60,"flying guest can pass over the host");
    world.reset(players);
    require(world.snapshot().fires.empty() && world.tanks()[1].verticalVelocity==0 && world.tanks()[0].burnRemaining==0,"new round resets fire and flight");
}
}
int main() {
    try { run(); std::cout<<"Napalm and flight mechanics passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
