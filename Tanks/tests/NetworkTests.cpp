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
    const auto endpoint=tw::net::parseEndpoint(" \t127.0.0.1 : 53001 \r\n",53000);
    require(endpoint && endpoint->address=="127.0.0.1" && endpoint->port==53001,"pasted endpoints trim surrounding whitespace");
    for (const auto* invalid : {"", "127.0.0.1:", "127.0.0.1:0", "127.0.0.1:65536", "127.0.0.1:-1", "127.0.0.1:12x", "127.0.0.1:53:54", "127 .0.0.1"})
        require(!tw::net::parseEndpoint(invalid,53000),"malformed addresses and ports are rejected");
    tw::Config config; config.asymmetricLoadouts=false; config.weapon.reloadSeconds=0.2f;
    tw::net::Server server(config,0);
    tw::net::Client a,b;
    require(!a.connect("127.0.0.1:bad",server.port(),"ALPHA"),"invalid first attempt can be retried");
    require(a.connect(" 127.0.0.1 : "+std::to_string(server.port())+" ",53000,"ALPHA"),"first client connects with pasted endpoint and custom port");
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
    require(b.connect(" 127.0.0.1 ",server.port(),"BRAVO"),"disconnected client can reconnect to the same host");
    until(a,b,[&]{return b.welcomed() && a.latest().players.size()==2 && b.latest().players.size()==2;});
    require(b.id()==1 && b.error().empty(),"reconnection restores the vacant seat and clears previous errors");
}
void runLoadouts() {
    // Leave room for the faster mortar shell to land inside the fixture.
    tw::Config config; config.titanMode=true; config.titanHealth=1250; config.hillHeight=0; config.worldWidth=2200;
    config.weapon.mortarBlastRadius=72; config.flightSpeed=375; config.weapon.orbGrowthSeconds=4;
    tw::net::Server server(config,0);
    tw::net::Client a,b;
    require(a.connect("127.0.0.1",server.port(),"HOST"),"orb host connects");
    until(a,b,[&]{return a.welcomed();});
    require(b.connect("127.0.0.1",server.port(),"FLYER"),"mortar guest connects");
    until(a,b,[&]{return b.welcomed() && a.latest().players.size()==2;});
    require(b.config().asymmetricLoadouts && b.config().titanMode && b.config().titanHealth==1250,"loadout mode and health synchronized");
    require(b.config().gravity==config.gravity && b.config().weapon.maxPower==config.weapon.maxPower && b.config().weapon.reloadSeconds==config.weapon.reloadSeconds,"real ballistics synchronized");
    require(b.config().weapon.mortarBlastRadius==72 && b.config().flightSpeed==375 && b.config().weapon.orbGrowthSeconds==4,"custom mortar and orb parameters synchronized");
    a.ready(true); b.ready(true);
    until(a,b,[&]{return a.latest().phase==tw::Phase::Playing && b.latest().phase==tw::Phase::Playing;});
    require(a.latest().detailedWeapons && b.latest().detailedWeapons && a.interpolated().detailedWeapons && b.interpolated().detailedWeapons,"both clients preserve mandatory projectile graphics data through interpolation");
    require(a.latest().tanks[0].weapon==tw::WeaponKind::GrowingOrb && b.latest().tanks[1].weapon==tw::WeaponKind::Mortar && b.latest().tanks[1].canFly,"new weapon roles survive serialization");
    const float floor=b.latest().tanks[1].position.y;
    bool sawOrb=false,sawMortar=false,sawBounce=false;
    const auto observe=[&] {
        for (const auto& p : b.latest().projectiles) {
            const auto visual=b.interpolated();
            const auto found=std::ranges::find(visual.projectiles,p.id,&tw::Projectile::id);
            require(found!=visual.projectiles.end() && found->weapon==p.weapon && found->bounces==p.bounces,"interpolation keeps weapon graphics and ricochet state");
            sawOrb |= p.weapon==tw::WeaponKind::GrowingOrb;
            sawMortar |= p.weapon==tw::WeaponKind::Mortar;
            sawBounce |= p.bounces>0;
        }
    };
    const auto start=std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now()<start+1200ms) {
        a.sendInput({0,-0.12f,0,true}); b.sendInput({0,-tw::pi+0.65f,0,std::chrono::steady_clock::now()<start+500ms,1});
        a.poll(); b.poll(); observe(); std::this_thread::sleep_for(5ms);
    }
    require(sawOrb && sawMortar,"both new projectile types replicate");
    require(a.latest().tanks[1].position.y<floor-150 && b.latest().tanks[1].position.y<floor-150,"mortar tank still flies through authoritative input");
    const auto landing=std::chrono::steady_clock::now()+2500ms;
    while (std::chrono::steady_clock::now()<landing) {
        a.sendInput({0,-0.6f,0,false}); b.sendInput({0,-2.5f,0,false,-1});
        a.poll(); b.poll(); observe(); std::this_thread::sleep_for(5ms);
        if (std::abs(b.latest().tanks[1].position.y-floor)<0.1f && b.latest().tanks[1].verticalVelocity==0) break;
    }
    require(std::abs(b.latest().tanks[1].position.y-floor)<0.1f,"Down lands the mortar tank");
    until(a,b,[&] { observe(); return std::ranges::any_of(b.latest().events,[](const auto& e){return e.kind==tw::EventKind::MortarImpact;}); });
    require(sawBounce,"orb ricochet count replicates to the guest");
    const auto blast=std::ranges::find_if(b.latest().events,[](const auto& e){return e.kind==tw::EventKind::MortarImpact;});
    require(blast!=b.latest().events.end() && blast->radius==72 && blast->intensity==1,"mortar explosion animation receives its authoritative radius and intensity");
    require(a.latest().tanks[0].maxHp==1250 && b.latest().tanks[0].maxHp==1250,"finite health still replicates with the new weapon extension");
}

}
int main() { try { run(); runLoadouts(); std::cout<<"LAN integration passed: lobby, combat, timeout, rematch, orb, mortar, ricochets, flight\n"; return 0; } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; } }
