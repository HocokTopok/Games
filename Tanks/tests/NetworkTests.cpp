#include "tankwars/Network.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
template<class Predicate> void until(tw::net::Client& a,tw::net::Client& b,Predicate done) {
    const auto deadline=std::chrono::steady_clock::now()+6s;
    while (std::chrono::steady_clock::now()<deadline) {
        a.poll(); b.poll();
        if (done()) return;
        a.sendInput({0,-0.6f,0,false}); b.sendInput({0,-2.5f,0,false});
        std::this_thread::sleep_for(5ms);
    }
    throw std::runtime_error("Timed out waiting for server state");
}
void run() {
    tw::Config config; config.asymmetricLoadouts=false; config.weapon.reloadSeconds=0.2f;
    tw::net::Server server(config,0);
    tw::net::Client a,b;
    require(a.connect("127.0.0.1",server.port(),"ALPHA"),"first client connects");
    until(a,b,[&]{return a.welcomed();});
    require(b.connect("127.0.0.1",server.port(),"BRAVO"),"second client connects");
    until(a,b,[&]{return b.welcomed() && a.latest().players.size()==2;});
    require(a.id()==0 && b.id()==1,"server assigns IDs");
    require(b.config().weapon.reloadSeconds==config.weapon.reloadSeconds,"host config synchronized");
    b.color(0);
    until(a,b,[&]{return b.latest().players.size()==2;});
    require(b.latest().players[1].color!=0,"duplicate colors rejected");
    a.ready(true); b.ready(true);
    until(a,b,[&]{return a.latest().phase==tw::Phase::Playing && b.latest().phase==tw::Phase::Playing;});
    require(a.latest().round==b.latest().round,"round synchronized");
    tw::net::Client late;
    require(late.connect("127.0.0.1",server.port(),"LATE"),"TCP late client connects");
    const auto lateDeadline=std::chrono::steady_clock::now()+2s;
    while (late.connected() && std::chrono::steady_clock::now()<lateDeadline) { late.poll(); a.poll(); b.poll(); a.sendInput({}); b.sendInput({}); std::this_thread::sleep_for(5ms); }
    require(!late.connected() && !late.error().empty(),"join during match rejected");
    const float x=a.latest().tanks[0].position.x;
    const auto deadline=std::chrono::steady_clock::now()+600ms;
    while (std::chrono::steady_clock::now()<deadline) {
        a.sendInput({1,-0.7f,0,true}); b.sendInput({0,-2.5f,0,false}); a.poll(); b.poll(); std::this_thread::sleep_for(5ms);
    }
    until(a,b,[&]{return a.latest().tanks[0].position.x>x+10 && !a.latest().events.empty() && !b.latest().events.empty();});
    require(a.latest().events.front().id==b.latest().events.front().id,"shared authoritative shot event");
    const auto timeout=std::chrono::steady_clock::now()+800ms;
    while (std::chrono::steady_clock::now()<timeout) { a.poll(); b.poll(); std::this_thread::sleep_for(5ms); }
    const float stopped=a.latest().tanks[0].position.x;
    const auto stable=std::chrono::steady_clock::now()+250ms;
    while (std::chrono::steady_clock::now()<stable) { a.poll(); b.poll(); std::this_thread::sleep_for(5ms); }
    require(std::abs(a.latest().tanks[0].position.x-stopped)<0.1f,"stale input stops movement");
    b.disconnect();
    until(a,b,[&]{return a.latest().phase==tw::Phase::Victory;});
    require(a.latest().winner==0,"disconnect awards survivor");
    a.rematch(); until(a,b,[&]{return a.latest().phase==tw::Phase::Lobby;});
    require(a.latest().players.size()==1,"rematch lobby cleaned");
}
void runLoadouts() {
    tw::Config config; config.titanMode=true;
    config.weapon.fireRadius=72; config.flightSpeed=375; config.weapon.napalmReload=0.08f;
    tw::net::Server server(config,0);
    tw::net::Client a,b;
    require(a.connect("127.0.0.1",server.port(),"HOST"),"napalm host connects");
    until(a,b,[&]{return a.welcomed();});
    require(b.connect("127.0.0.1",server.port(),"FLYER"),"flying guest connects");
    until(a,b,[&]{return b.welcomed() && a.latest().players.size()==2;});
    require(b.config().asymmetricLoadouts && b.config().titanMode,"loadout mode synchronized");
    require(b.config().gravity==config.gravity && b.config().weapon.maxPower==config.weapon.maxPower &&
            b.config().weapon.reloadSeconds==config.weapon.reloadSeconds,"real ballistics preserved for napalm in titan mode");
    require(b.config().weapon.fireRadius==72 && b.config().flightSpeed==375 && b.config().weapon.napalmReload==0.08f,"custom fire and flight balance synchronized");
    a.ready(true); b.ready(true);
    until(a,b,[&]{return a.latest().phase==tw::Phase::Playing && b.latest().phase==tw::Phase::Playing;});
    require(a.latest().tanks[0].weapon==tw::WeaponKind::Napalm && a.latest().tanks[1].weapon==tw::WeaponKind::Laser &&
            b.latest().tanks[1].canFly && !b.latest().tanks[0].canFly,"roles survive snapshot serialization");
    const float floor=b.latest().tanks[1].position.y;
    bool sawProjectile=false;
    const auto deadline=std::chrono::steady_clock::now()+1200ms;
    while (std::chrono::steady_clock::now()<deadline) {
        a.sendInput({0,tw::pi/2,0,true}); b.sendInput({0,-2.5f,0,false,1});
        a.poll(); b.poll();
        sawProjectile |= std::ranges::any_of(b.latest().projectiles,[](const auto& p){return p.napalm;});
        std::this_thread::sleep_for(5ms);
    }
    require(sawProjectile,"guest receives napalm projectiles");
    require(b.latest().tanks[1].position.y<floor-150 && a.latest().tanks[1].position.y<floor-150 &&
            a.latest().tanks[1].verticalVelocity<=0,"guest flight reaches authoritative snapshots");
    require(!a.latest().fires.empty() && !b.latest().fires.empty() && b.latest().tanks[0].burnRemaining>0,"ground fire and burning replicate to both clients");
    require(std::ranges::any_of(b.latest().events,[](const auto& e){return e.kind==tw::EventKind::NapalmImpact;}),"napalm impact kind synchronized");
    require(b.latest().tanks[0].hp==100,"existing titan armor preserved");
    const auto landing=std::chrono::steady_clock::now()+2500ms;
    while (std::chrono::steady_clock::now()<landing) {
        a.sendInput({0,-0.6f,0,false}); b.sendInput({0,-2.5f,0,false,-1});
        a.poll(); b.poll(); std::this_thread::sleep_for(5ms);
        if (std::abs(b.latest().tanks[1].position.y-floor)<0.1f && b.latest().tanks[1].verticalVelocity==0) break;
    }
    require(std::abs(b.latest().tanks[1].position.y-floor)<0.1f,"Down lands the guest through network input");
}
}
int main() { try { run(); runLoadouts(); std::cout<<"LAN integration passed: lobby, combat, timeout, rematch, napalm, fire, flight\n"; return 0; } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; } }
