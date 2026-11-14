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
template<class Predicate> void until(tw::net::Client& a,tw::net::Client& b,Predicate done) {
    const auto end=std::chrono::steady_clock::now()+4s;
    while (std::chrono::steady_clock::now()<end) {
        a.poll(); b.poll(); require(a.connected() && b.connected(),"both clients stay connected");
        if (done()) return;
        std::this_thread::sleep_for(4ms);
    }
    throw std::runtime_error("Timed out waiting for arsenal state");
}
void renderPair(tw::Renderer& a,tw::Renderer& b,sf::RenderTexture& ia,sf::RenderTexture& ib,const tw::Config& c,const tw::Snapshot& sa,const tw::Snapshot& sb) {
    a.resize(c); b.resize(c); a.observe(sa); b.observe(sb); a.update(1.f/60); b.update(1.f/60);
    ia.clear(); ib.clear(); a.scenery(c,3,false,&sa); b.scenery(c,3,false,&sb);
    a.battle(c,sa,tw::noPlayer,{},false); b.battle(c,sb,tw::noPlayer,{},false); ia.display(); ib.display();
    const auto x=ia.getTexture().copyToImage(), y=ib.getTexture().copyToImage();
    require(std::equal(x.getPixelsPtr(),x.getPixelsPtr()+x.getSize().x*x.getSize().y*4,y.getPixelsPtr()),"Host and Join render identical sky beams and warnings");
}
void run() {
    tw::Config c; c.loadoutSelection=true; c.titanMode=true;
    c.weapon.flareReload=9; c.weapon.flareSpeedScale=0.9f; c.weapon.flareGravityScale=0.55f;
    c.weapon.skyWarmup=0.45f; c.weapon.skyDuration=2.75f; c.weapon.skyRadius=92; c.weapon.skyDamagePerSecond=125;
    tw::net::Server server(c,0); tw::net::Client host,join;
    require(host.connect("127.0.0.1",server.port(),"HOST") && join.connect("127.0.0.1",server.port(),"JOIN"),"arsenal clients connect");
    until(host,join,[&]{return host.welcomed() && join.welcomed() && host.latest().players.size()==2;});
    const auto& w=join.config().weapon;
    require(w.flareReload==9 && w.flareSpeedScale==0.9f && w.flareGravityScale==0.55f && w.skyWarmup==0.45f && w.skyDuration==2.75f && w.skyRadius==92 && w.skyDamagePerSecond==125,"all custom flare and beam settings come from the host");
    host.map(0); until(host,join,[&]{return join.latest().mapSelected;});
    host.weapon(tw::WeaponKind::Flare); join.weapon(tw::WeaponKind::Ricochet); host.confirmWeapon(true); join.confirmWeapon(true); host.gadget(tw::GadgetKind::Jetpack); join.gadget(tw::GadgetKind::Jetpack); host.ready(true); join.ready(true);
    until(host,join,[&]{return host.latest().phase==tw::Phase::Playing && join.latest().phase==tw::Phase::Playing;});
    require(join.latest().tanks[0].weapon==tw::WeaponKind::Flare,"fifth loadout replicates as a flare launcher");
    sf::RenderTexture ia({1440,810}),ib({1440,810});
    tw::Renderer a(ia,TANKWARS_TEST_ASSETS),b(ib,TANKWARS_TEST_ASSETS); a.settings.shake=b.settings.shake=false;
    bool warning=false,beam=false,warningParity=false,beamParity=false,expired=false,projectile=false;
    const auto start=std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now()<start+6s) {
        const float time=std::chrono::duration<float>(std::chrono::steady_clock::now()-start).count();
        host.sendInput({0,tw::pi/2,0,time>0.75f && time<0.9f}); join.sendInput({}); host.poll(); join.poll();
        require(host.connected() && join.connected(),"flare packets and damage do not disconnect clients");
        const auto& x=host.latest(); const auto& y=join.latest();
        for (const auto& p : y.projectiles) projectile|=p.weapon==tw::WeaponKind::Flare;
        for (const auto& strike : y.skyStrikes) {
            warning|=strike.age<w.skyWarmup; beam|=strike.age>w.skyWarmup;
            require(strike.owner==0 && strike.radius==92 && strike.damageScale==1,"beam geometry and owner replicate");
            if (x.tick==y.tick && ((!warningParity && strike.age>0.1f && strike.age<w.skyWarmup) || (!beamParity && strike.age>w.skyWarmup+0.3f))) {
                renderPair(a,b,ia,ib,host.config(),x,y);
                if (strike.age<w.skyWarmup) warningParity=true; else beamParity=true;
            }
        }
        if (beam && x.skyStrikes.empty() && y.skyStrikes.empty()) { expired=true; break; }
        std::this_thread::sleep_for(4ms);
    }
    // A downward shot can enter terrain during its first tick, before a network snapshot.
    require((projectile || warning) && warning && beam && warningParity && beamParity && expired,"warning, beam, matching frames and expiration reach both clients");
    require(std::abs(join.latest().tanks[0].hp-(1000-125*2.75f))<0.2f,"both clients receive gradual damage with the configured total");
    const auto effects=join.consumeEffects();
    require(std::ranges::count_if(effects.events,[](const auto& e){return e.kind==tw::EventKind::SkyStrike;})==1,"beam ignition event is delivered exactly once");

    // A deterministic visual fixture shows the new beam, rotating spheres and varied blasts together.
    auto frame=join.latest(); frame.events.clear(); frame.projectiles.clear(); frame.skyStrikes={{900,0,{1090,630},w.skyWarmup+0.9f,92,1}};
    frame.events={{10001,tw::EventKind::MortarImpact,{510,590},0,78,1},{10002,tw::EventKind::MortarImpact,{740,575},0,78,1}};
    for (int i=0;i<3;++i) {
        tw::Projectile p{static_cast<std::uint32_t>(950+i),0,{280+i*140.f,370-i*30.f},{900,-100},0.6f+i*1.5f};
        p.weapon=tw::WeaponKind::Ricochet; p.bounces=i; frame.projectiles.push_back(p);
    }
    a.resetEffects(); a.observe(frame); a.update(0.35f); ia.clear(); a.scenery(host.config(),3,false,&frame); a.battle(host.config(),frame,tw::noPlayer,{},false); ia.display();
    const auto output=std::filesystem::temp_directory_path()/"tankwars-arsenal-fx.png";
    require(ia.getTexture().copyToImage().saveToFile(output),"new arsenal visual fixture saved");
    std::cout<<"Arsenal network passed: flare settings, persistent beam, gradual damage, pixel parity and expiration. Preview: "<<output<<'\n';
}
}
int main() { try { run(); return 0; } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; } }
