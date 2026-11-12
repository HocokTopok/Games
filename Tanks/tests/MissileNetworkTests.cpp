#include "tankwars/Network.hpp"
#include "tankwars/Renderer.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class Predicate> void until(tw::net::Client& a,tw::net::Client& b,Predicate done) {
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while (std::chrono::steady_clock::now()<deadline) {
        a.poll(); b.poll(); require(a.connected() && b.connected(),"clients stay connected");
        if (done()) return;
        a.sendInput({}); b.sendInput({}); std::this_thread::sleep_for(3ms);
    }
    throw std::runtime_error("Timed out waiting for missile state");
}
void render(tw::Renderer& renderer,sf::RenderTexture& image,const tw::Config& c,const tw::Snapshot& s) {
    image.clear(); renderer.resize(c); renderer.observe(s);
    renderer.scenery(c,2,false,&s); renderer.battle(c,s,tw::noPlayer,{},false); image.display();
}
void sameFrame(tw::Renderer& a,tw::Renderer& b,sf::RenderTexture& ia,sf::RenderTexture& ib,const tw::Config& c,const tw::Snapshot& sa,const tw::Snapshot& sb) {
    render(a,ia,c,sa); render(b,ib,c,sb);
    const auto first=ia.getTexture().copyToImage(), second=ib.getTexture().copyToImage();
    require(std::equal(first.getPixelsPtr(),first.getPixelsPtr()+first.getSize().x*first.getSize().y*4,second.getPixelsPtr()),"missile, designator and directed blast match on both clients");
}
void run() {
    tw::Config c; c.loadoutSelection=true; c.titanMode=true;
    c.weapon.missileReload=4.1f; c.weapon.missileSpeedScale=0.67f; c.weapon.missileTurnRate=1.25f;
    c.weapon.missileDamage=121; c.weapon.missileBlastRadius=53;
    tw::net::Server server(c,0); tw::net::Client a,b;
    require(a.connect("127.0.0.1",server.port(),"HOST") && b.connect("127.0.0.1",server.port(),"JOIN"),"clients connect");
    until(a,b,[&]{return a.welcomed() && b.welcomed();});
    const auto& w=b.config().weapon;
    require(w.missileReload==4.1f && w.missileSpeedScale==0.67f && w.missileTurnRate==1.25f && w.missileDamage==121 && w.missileBlastRadius==53,"all custom missile parameters synchronize");
    a.weapon(tw::WeaponKind::Missile); b.weapon(tw::WeaponKind::Mortar); a.ready(true); b.ready(true);
    until(a,b,[&]{return a.latest().players.size()==2 && a.latest().players[0].ready && a.latest().players[1].ready;});
    a.map(0); until(a,b,[&]{return a.latest().phase==tw::Phase::Playing && b.latest().phase==tw::Phase::Playing;});
    require(a.latest().tanks[0].weapon==tw::WeaponKind::Missile && b.latest().tanks[0].weapon==tw::WeaponKind::Missile,"missile selection reaches both clients");
    sf::RenderTexture ia({1600,900}), ib({1600,900});
    tw::Renderer ra(ia,TANKWARS_TEST_ASSETS), rb(ib,TANKWARS_TEST_ASSETS);
    ra.settings.shake=rb.settings.shake=false;
    bool flight=false,blast=false,flightParity=false,blastParity=false;
    const auto start=std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now()<start+5s) {
        const float t=std::chrono::duration<float>(std::chrono::steady_clock::now()-start).count();
        a.sendInput({0,t<0.6f ? -0.45f : 0.55f,0,t<0.15f}); b.sendInput({}); a.poll(); b.poll();
        require(a.connected() && b.connected(),"new weapon stream stays connected");
        const auto& sa=a.latest(); const auto& sb=b.latest();
        flight|=std::ranges::any_of(sb.projectiles,[](const auto& p){return p.weapon==tw::WeaponKind::Missile;});
        for (const auto& e : sb.events) if (e.kind==tw::EventKind::MissileImpact) {
            blast=true; require(e.radius==53 && std::abs(tw::length(e.normal)-1)<0.001f,"directed impact data reaches clients");
        }
        if (sa.tick==sb.tick && !sb.projectiles.empty() && t>0.2f && !flightParity) {
            sameFrame(ra,rb,ia,ib,a.config(),sa,sb); flightParity=true;
            require(ib.getTexture().copyToImage().saveToFile(std::filesystem::temp_directory_path()/"tankwars-missile-flight.png"),"save missile flight frame");
        }
        if (sa.tick==sb.tick && blast && !blastParity) {
            sameFrame(ra,rb,ia,ib,a.config(),sa,sb);
            ra.update(0.18f); rb.update(0.18f);
            sameFrame(ra,rb,ia,ib,a.config(),sa,sb); blastParity=true;
            require(ib.getTexture().copyToImage().saveToFile(std::filesystem::temp_directory_path()/"tankwars-missile-blast.png"),"save directed blast frame");
        }
        if (flight && blast && flightParity && blastParity) break;
        std::this_thread::sleep_for(4ms);
    }
    require(flight && blast && flightParity && blastParity,"missile flight, designator and directed explosion have network graphic parity");
    // Render every real terrain profile at its actual world dimensions for visual review.
    for (std::uint8_t id=0;id<4;++id) {
        const auto config=tw::MapFactory::preset(c,id);
        tw::World world(config); auto state=world.snapshot();
        state.phase=tw::Phase::Playing;
        ra.resetEffects(); render(ra,ia,config,state);
        require(ia.getTexture().copyToImage().saveToFile(std::filesystem::temp_directory_path()/("tankwars-map-"+std::to_string(id+1)+".png")),"save battlefield profile");
    }
    std::cout<<"Missile config, steering visuals and directed blast synchronized on two TCP clients\n";
}
}
int main() {
    try { run(); return 0; } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
