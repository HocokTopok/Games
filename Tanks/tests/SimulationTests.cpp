#include "tankwars/GameSession.hpp"
#include "tankwars/Weapon.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool ok, const char* name) { ++checks; if (!ok) throw std::runtime_error(name); }
bool near(float a,float b,float epsilon = 0.05f) { return std::abs(a-b)<epsilon; }
void run() {
    tw::Config c; tw::NightfallBasin map(c);
    for (int x=0;x<=1600;x+=31) check(near(map.height(static_cast<float>(x)),map.height(1600.f-x)),"symmetric terrain");
    tw::Projectile p{1,0,{100,100},{900,-400},0};
    for (int i=0;i<120;++i) tw::Physics::integrate(p,c.fixedStep,c.gravity);
    check(near(p.position.x,1000) && near(p.position.y,-45),"analytic gravity / fixed step");
    tw::Tank t; t.position = {800,map.height(800)-12}; t.hp=100;
    const std::vector<tw::Tank> tanks{t};
    auto hit = tw::Physics::sweep({0,t.position.y},{1600,t.position.y},3,map,tanks);
    check(hit && hit->tank == 0,"swept projectile cannot tunnel through tank");
    auto ground = tw::Physics::sweep({400,100},{400,1000},3,map,{});
    check(ground && near(ground->position.y,map.height(400)-3),"first terrain collision");
    const float full = tw::Physics::splashDamage(t.position,t,c.weapon);
    const float half = tw::Physics::splashDamage(t.position+tw::Vec2{60,0},t,c.weapon);
    const float zero = tw::Physics::splashDamage(t.position+tw::Vec2{200,0},t,c.weapon);
    check(full>half && half>zero && zero==0,"smooth blast falloff");
    tw::GameSession session(c);
    auto a=session.addPlayer("ONE"), b=session.addPlayer("TWO");
    check(a==0 && b==1 && !session.addPlayer("THREE"),"two-player capacity");
    check(!session.setColor(1,0) && session.setColor(1,2),"unique colors");
    session.setReady(0,true); session.step(); check(session.snapshot().phase==tw::Phase::Lobby,"readiness gate");
    session.setReady(1,true); session.step(); check(session.snapshot().phase==tw::Phase::Playing,"start match");
    auto before=session.snapshot(); auto tank=before.tanks[0]; tank.aim=-0.55f;
    const auto predicted=tw::Physics::trajectory(tank,c,map,before.tanks);
    auto shot=tw::Cannon::fire(tank,c.weapon,1);
    std::optional<tw::Hit> actual;
    while (shot.age<c.weapon.projectileLifetime) {
        const auto from=shot.position; tw::Physics::integrate(shot,c.fixedStep,c.gravity);
        actual=tw::Physics::sweep(from,shot.position,c.weapon.projectileRadius,map,before.tanks,shot.age<0.12f ? tank.id : tw::noPlayer);
        if (actual || shot.position.x>c.worldWidth+100) break;
    }
    check(actual && tw::length(actual->position-predicted.back())<0.01f,"prediction and real shell agree");
    session.setInput(0,{1,-0.55f,1,true});
    for (int i=0;i<60;++i) session.step();
    auto after=session.snapshot();
    check(after.tanks[0].position.x>before.tanks[0].position.x,"continuous movement");
    check(after.tanks[0].power>before.tanks[0].power,"continuous power");
    check(after.tanks[0].reload>0 && after.projectiles.size()==1,"reload gates held fire");
    session.removePlayer(1);
    for (int i=0;i<1500 && session.snapshot().phase==tw::Phase::Playing;++i) session.step();
    check(session.snapshot().phase==tw::Phase::Victory && session.snapshot().winner==0,"disconnect and victory");
    session.rematch(); check(session.snapshot().phase==tw::Phase::Lobby && session.snapshot().players.size()==1,"rematch removes disconnected player");
    c.playerLimit=4; c.teams=true; tw::GameSession teams(c);
    for (unsigned i=0;i<4;++i) { teams.addPlayer("TEAM"); teams.setReady(static_cast<tw::PlayerId>(i),true); }
    teams.step(); check(teams.snapshot().tanks.size()==4,"four-player extension");
    auto invalid=c; invalid.weapon.reloadSeconds=0;
    bool rejected=false; try { invalid.validate(); } catch (...) { rejected=true; }
    check(rejected,"invalid config rejected");
    tw::World friendly(c);
    std::vector<tw::Player> players{{0,0,0,true,true,false,"A"},{1,1,0,true,true,false,"B"}};
    friendly.reset(players); std::array<tw::Input,tw::maxPlayers> inputs{};
    inputs[0]={1,0,0,false}; inputs[1]={-1,-tw::pi,0,false};
    for (int i=0;i<1100;++i) friendly.step(inputs);
    check(friendly.tanks()[1].position.x-friendly.tanks()[0].position.x>=c.tankHalfWidth*2,"tanks cannot overlap");
    inputs[0]={0,tw::pi/2,0,false}; inputs[1]={0,-tw::pi,0,false};
    for (int i=0;i<120;++i) friendly.step(inputs);
    inputs[0].fire=true; friendly.step(inputs);
    check(friendly.snapshot().tanks[0].hp<100,"self damage enabled");
    check(friendly.snapshot().tanks[1].hp<100,"friendly fire enabled");
    tw::GameSession duel(c); duel.addPlayer("HUMAN"); duel.addPlayer("BOT",true);
    duel.setReady(0,true); duel.setReady(1,true); duel.step();
    for (int i=0;i<120*50 && duel.snapshot().phase==tw::Phase::Playing;++i) {
        duel.setInput(0,{0,-0.5f,0,false}); duel.step();
    }
    check(duel.snapshot().phase==tw::Phase::Victory && duel.snapshot().winner==1,"bot completes real combat with damage and destruction");
}
}
int main() { try { run(); std::cout<<checks<<" simulation checks passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; } }
