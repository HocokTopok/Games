#include "tankwars/GameSession.hpp"
#include "tankwars/Weapon.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void run() {
    tw::Config config;
    const std::vector<tw::Player> players{{0,0,0,true,true,false,"HOST"},{1,1,1,true,true,false,"GUEST"}};
    std::array<tw::Input,tw::maxPlayers> input{};
    tw::World world(config); world.reset(players);
    require(world.tanks()[0].weapon==tw::WeaponKind::GrowingOrb && world.tanks()[1].weapon==tw::WeaponKind::Mortar && world.tanks()[1].canFly && !world.tanks()[0].canFly,"host gets an orb and guest gets a flying mortar");
    require(tw::Cannon::orbDamage(0,config.weapon)==1 && std::abs(tw::Cannon::orbDamage(2.5f,config.weapon)-25.75f)<0.001f && tw::Cannon::orbDamage(5,config.weapon)==100 && tw::Cannon::orbDamage(30,config.weapon)==100,"orb damage grows quadratically from 1 to 100 and is capped");
    tw::Projectile orb{1,0,{1500,200},{1200,0},0}; orb.weapon=tw::WeaponKind::GrowingOrb;
    const float initialRadius=tw::Cannon::radius(orb,config.weapon);
    auto result=tw::Physics::advance(orb,0.2f,config,world.map(),{});
    require(!result.hit && result.ricochetCount==1 && orb.bounces==1 && std::abs(tw::length(orb.velocity)-600)<0.01f,"first wall bounce halves velocity");
    require(tw::Cannon::radius(orb,config.weapon)>initialRadius && orb.age>0.19f,"orb continues growing through a bounce");
    orb.position={tw::Cannon::radius(orb,config.weapon)+1,200}; orb.velocity={-600,0};
    result=tw::Physics::advance(orb,0.1f,config,world.map(),{});
    require(!result.hit && orb.bounces==2 && std::abs(tw::length(orb.velocity)-300)<0.01f,"second wall bounce halves velocity again");
    orb.position={1590,200}; orb.velocity={300,0};
    result=tw::Physics::advance(orb,0.2f,config,world.map(),{});
    require(result.hit && orb.bounces==2 && result.ricochetCount==0,"third wall contact detonates instead of bouncing");
    orb={1,0,{1580,20},{100,-100},0}; orb.weapon=tw::WeaponKind::GrowingOrb;
    result=tw::Physics::advance(orb,0.2f,config,world.map(),{});
    require(!result.hit && orb.bounces==1 && orb.velocity.x<0 && orb.velocity.y>0 && std::abs(tw::length(orb.velocity)-std::sqrt(5000.f))<0.01f,"corner contact reflects both axes and halves speed once");
    tw::Tank predictor=world.tanks()[0]; predictor.position={800,300}; predictor.aim=0;
    const auto predicted=tw::Physics::trajectory(predictor,config,world.map(),{});
    auto actual=tw::Cannon::fire(predictor,config.weapon,9);
    do { result=tw::Physics::advance(actual,config.fixedStep,config,world.map(),{}); } while (!result.hit && !result.expired);
    require(actual.bounces==2 && tw::length(predicted.back()-actual.position)<0.01f,"orb aiming guide includes both real ricochets");
    tw::Tank target; target.position={1500,200}; target.id=1;
    orb={1,0,{1400,200},{2000,0},1}; orb.weapon=tw::WeaponKind::GrowingOrb;
    result=tw::Physics::advance(orb,0.2f,config,world.map(),std::span<const tw::Tank>(&target,1));
    require(result.hit && result.hit->tank==1 && orb.bounces==0,"orb hits a tank before a later wall without tunnelling");
    world.reset(players); input={}; input[0].fire=true; world.step(input);
    require(world.snapshot().projectiles.size()==1 && world.snapshot().projectiles[0].weapon==tw::WeaponKind::GrowingOrb,"host fires one growing orb");
    input[0].fire=false;
    for (int i=0;i<30;++i) world.step(input);
    require(std::ranges::count_if(world.snapshot().events,[](const auto& e){return e.kind==tw::EventKind::Shot;})==1,"release stops firing");
    config.hillHeight=0; tw::World mortarWorld(config); mortarWorld.reset(players); input={};
    input[1]={0,-tw::pi+0.65f,0,true}; mortarWorld.step(input); input[1].fire=false;
    auto shot=mortarWorld.snapshot().projectiles.at(0);
    require(shot.weapon==tw::WeaponKind::Mortar && mortarWorld.snapshot().lasers.empty(),"guest launches a mortar shell instead of a laser");
    require(config.weapon.mortarReload==1.6f && config.weapon.laserReload==2.4f && config.weapon.mortarBlastRadius>config.tankHalfWidth*2,"mortar unchanged, railgun fires half as often, and mortar radius exceeds tank length");
    const auto mortarGuide=tw::Physics::trajectory(mortarWorld.tanks()[1],config,mortarWorld.map(),mortarWorld.tanks());
    do { result=tw::Physics::advance(shot,config.fixedStep,config,mortarWorld.map(),mortarWorld.tanks()); } while (!result.hit && !result.expired);
    require(result.hit && shot.age>2 && tw::length(mortarGuide.back()-shot.position)<4,"slow mortar follows its displayed high arc for several seconds");
    for (int i=0;i<120*7;++i) mortarWorld.step(input);
    const auto impact=mortarWorld.snapshot();
    require(std::ranges::any_of(impact.events,[](const auto& e){return e.kind==tw::EventKind::MortarImpact && e.radius==68;}),"mortar impact carries its actual blast radius for animation");
    require(impact.tanks[0].hp<100 && impact.fires.empty(),"mortar blast damages a nearby tank without leaving napalm");
    target.position={config.weapon.mortarBlastRadius+config.tankHalfWidth+1,0};
    require(tw::Physics::areaDamage({0,0},target,config.weapon.mortarBlastRadius,config.weapon.mortarDamage,config)==0,"mortar cannot damage beyond its blast radius");

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
    try { run(); std::cout<<"Orb, mortar and flight mechanics passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
