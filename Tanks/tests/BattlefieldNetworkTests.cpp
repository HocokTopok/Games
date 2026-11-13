#include "tankwars/Network.hpp"
#include "tankwars/Renderer.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
void require(bool ok,const char* text) { if (!ok) throw std::runtime_error(text); }
template<class Predicate> void until(tw::net::Client& a,tw::net::Client& b,Predicate done,float seconds=6) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::duration<float>(seconds);
    while (std::chrono::steady_clock::now()<deadline) {
        a.poll(); b.poll(); require(a.connected() && b.connected(),"clients stay connected");
        if (done()) return;
        a.sendInput({}); b.sendInput({}); std::this_thread::sleep_for(3ms);
    }
    throw std::runtime_error("Timed out waiting for battlefield state");
}
void sameFrame(tw::Renderer& host,tw::Renderer& guest,sf::RenderTexture& a,sf::RenderTexture& b,
               const tw::Config& config,const tw::Snapshot& sa,const tw::Snapshot& sb) {
    a.clear(); b.clear(); host.resize(config); guest.resize(config);
    host.observe(sa); guest.observe(sb);
    host.scenery(config,4,false,&sa); guest.scenery(config,4,false,&sb);
    host.battle(config,sa,tw::noPlayer,{},false); guest.battle(config,sb,tw::noPlayer,{},false);
    a.display(); b.display();
    const auto ia=a.getTexture().copyToImage(), ib=b.getTexture().copyToImage();
    const std::size_t size=ia.getSize().x*ia.getSize().y*4;
    require(std::equal(ia.getPixelsPtr(),ia.getPixelsPtr()+size,ib.getPixelsPtr()),"Host and Join render identical effects pixel for pixel");
}
void run() {
    tw::Config config; config.loadoutSelection=true; config.titanMode=true; config.titanHealth=100000;
    config.weapon.ricochetReload=0.38f; config.weapon.ricochetSpeedScale=1.5f;
    tw::net::Server server(config,0); tw::net::Client host,join;
    require(host.connect("127.0.0.1",server.port(),"HOST"),"host connects");
    require(join.connect("127.0.0.1",server.port(),"JOIN"),"guest connects");
    until(host,join,[&]{return host.welcomed() && join.welcomed() && host.latest().players.size()==2;});
    require(join.config().loadoutSelection && join.config().weapon.ricochetReload==0.38f && join.config().weapon.ricochetSpeedScale==1.5f,"selection and custom gun settings replicate");
    host.ready(true); join.ready(true);
    until(host,join,[&]{return host.latest().tick==join.latest().tick;});
    require(host.latest().phase==tw::Phase::Lobby && !host.latest().players[0].ready,"server enforces weapon selection");
    host.weapon(tw::WeaponKind::Napalm); join.weapon(tw::WeaponKind::Mortar);
    host.ready(true); join.ready(true);
    until(host,join,[&]{return host.latest().players[0].ready && host.latest().players[1].ready;});
    require(host.latest().phase==tw::Phase::Lobby,"both players confirm before map selection");
    join.map(1); std::this_thread::sleep_for(40ms); host.poll(); join.poll();
    require(host.latest().phase==tw::Phase::Lobby,"guest cannot choose the host's map");
    host.map(3);
    until(host,join,[&]{return host.latest().phase==tw::Phase::Playing && join.latest().phase==tw::Phase::Playing;});
    require(host.config().worldWidth==3200 && join.config().worldWidth==3200 && join.latest().mapId==3,"map and camera dimensions synchronize");
    require(join.latest().tanks[0].weapon==tw::WeaponKind::Napalm && host.latest().tanks[1].weapon==tw::WeaponKind::Mortar,"chosen weapons replicate to both players");
    const auto original=host.latest().terrain;
    sf::RenderTexture hostImage({1280,720}), joinImage({1280,720});
    tw::Renderer hostRenderer(hostImage,TANKWARS_TEST_ASSETS), joinRenderer(joinImage,TANKWARS_TEST_ASSETS);
    hostRenderer.settings.shake=joinRenderer.settings.shake=false;
    bool flames=false,mortar=false,craters=false,parity=false,blastParity=false,plane=false,falling=false,landed=false;
    const auto start=std::chrono::steady_clock::now();
    const auto output=std::filesystem::temp_directory_path()/"tankwars-join-fx.png";
    while (std::chrono::steady_clock::now()<start+46s) {
        const float elapsed=std::chrono::duration<float>(std::chrono::steady_clock::now()-start).count();
        host.sendInput({0,tw::pi/2,0,elapsed<2}); join.sendInput({0,-tw::pi+0.85f,0,elapsed<2});
        host.poll(); join.poll(); require(host.connected() && join.connected(),"new FX stream remains connected");
        const auto& a=host.latest(); const auto& b=join.latest();
        flames|=!b.fires.empty();
        mortar|=std::ranges::any_of(b.events,[](const auto& e){return e.kind==tw::EventKind::MortarImpact && e.radius==110;});
        craters|=b.terrain!=original;
        plane|=b.plane.active;
        for (const auto& d : b.supplies) {
            require(d.position.x>=800 && d.position.x<=2400,"network drop stays in middle half");
            falling|=!d.landed; landed|=d.landed;
        }
        if (a.tick==b.tick && flames && !parity && !b.fires.empty()) {
            require(a.terrain==b.terrain,"both clients share identical crater geometry");
            sameFrame(hostRenderer,joinRenderer,hostImage,joinImage,host.config(),a,b);
            require(joinImage.getTexture().copyToImage().saveToFile(output),"guest screenshot saved"); parity=true;
        }
        if (a.tick==b.tick && mortar && !blastParity) {
            sameFrame(hostRenderer,joinRenderer,hostImage,joinImage,host.config(),a,b); blastParity=true;
        }
        if (elapsed>8 && flames && mortar && craters && parity && blastParity) {
            // Verify events survived multiple poll calls before the rendered frame consumed them.
            const auto effects=join.consumeEffects();
            require(std::ranges::any_of(effects.events,[](const auto& e){return e.kind==tw::EventKind::NapalmImpact;}) &&
                    std::ranges::any_of(effects.events,[](const auto& e){return e.kind==tw::EventKind::MortarImpact;}),"buffered frames preserve fire and explosion events");
            require(join.consumeEffects().events.empty(),"effect events consumed once");
            break;
        }
        std::this_thread::sleep_for(4ms);
    }
    require(flames && mortar && craters && parity && blastParity,"guest receives napalm, mortar blasts and craters with matching graphics");
    // Continue the live match through the first complete supply cycle.
    bool planeParity=false,crateParity=false;
    until(host,join,[&] {
        const auto& a=host.latest(); const auto& b=join.latest(); plane|=b.plane.active;
        for (const auto& d : b.supplies) { falling|=!d.landed; landed|=d.landed; }
        if (a.tick==b.tick && b.plane.active) require(a.plane.position.x==b.plane.position.x && a.plane.targetX==b.plane.targetX,"plane is authoritative");
        if (a.tick==b.tick && b.plane.active && b.plane.released && !planeParity) {
            sameFrame(hostRenderer,joinRenderer,hostImage,joinImage,host.config(),a,b); planeParity=true;
        }
        if (a.tick==b.tick && !b.supplies.empty() && b.supplies.back().landed && b.supplies.back().age>1 && !crateParity) {
            sameFrame(hostRenderer,joinRenderer,hostImage,joinImage,host.config(),a,b); crateParity=true;
            const auto supplyImage=std::filesystem::temp_directory_path()/"tankwars-supply-fx.png";
            require(joinImage.getTexture().copyToImage().saveToFile(supplyImage),"opened supply screenshot saved");
        }
        return plane && falling && landed && planeParity && crateParity;
    },42);
    require(plane && falling && landed,"guest sees plane, parachute and opened supply crate");
    const auto& d=join.latest().supplies.back();
    require(d.position.x>=800 && d.position.x<=2400,"authoritative supply drop range");
    std::cout<<"Host/Join pixel parity passed; live napalm, mortar, craters and supply cycle synchronized. Screenshot: "<<output<<'\n';
}
void ricochetAndMissile() {
    tw::Config c; c.loadoutSelection=true; c.titanMode=true;
    tw::net::Server server(c,0); tw::net::Client a,b;
    require(a.connect("127.0.0.1",server.port(),"HOST") && b.connect("127.0.0.1",server.port(),"JOIN"),"second pair connects");
    until(a,b,[&]{return a.welcomed() && b.welcomed();});
    a.weapon(tw::WeaponKind::Ricochet); b.weapon(tw::WeaponKind::Missile); a.ready(true); b.ready(true);
    until(a,b,[&]{return a.latest().players.size()==2 && a.latest().players[0].ready && a.latest().players[1].ready;});
    a.map(0); until(a,b,[&]{return a.latest().phase==tw::Phase::Playing && b.latest().phase==tw::Phase::Playing;});
    sf::RenderTexture hostImage({1280,720}), joinImage({1280,720});
    tw::Renderer hostRenderer(hostImage,TANKWARS_TEST_ASSETS), joinRenderer(joinImage,TANKWARS_TEST_ASSETS);
    hostRenderer.settings.shake=joinRenderer.settings.shake=false;
    bool projectile=false,beam=false,bounce=false,parity=false; const auto start=std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now()<start+4s) {
        a.sendInput({0,-tw::pi+0.12f,0,true}); b.sendInput({0,-2.8f,0,true}); a.poll(); b.poll();
        require(a.connected() && b.connected(),"ricochet and missile clients connected");
        for (const auto& p : b.latest().projectiles) if (p.weapon==tw::WeaponKind::Ricochet) { projectile=true; bounce|=p.bounces>0; }
        beam|=std::ranges::any_of(b.latest().projectiles,[](const auto& p){return p.weapon==tw::WeaponKind::Missile;});
        if (!parity && bounce && beam && a.latest().tick==b.latest().tick) {
            sameFrame(hostRenderer,joinRenderer,hostImage,joinImage,a.config(),a.latest(),b.latest()); parity=true;
        }
        std::this_thread::sleep_for(4ms);
    }
    require(projectile && beam && bounce && parity,"new ricochet projectile, wall bounce and guided missile reach both clients");
}
}
int main() {
    try { run(); ricochetAndMissile(); return 0; }
    catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
