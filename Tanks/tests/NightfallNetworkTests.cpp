#include "tankwars/Network.hpp"
#include "tankwars/Renderer.hpp"
#include "tankwars/Weapon.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace {
using namespace std::chrono_literals;
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void until(tw::net::Client& a,tw::net::Client& b,F done) {
    const auto deadline=std::chrono::steady_clock::now()+6s;
    while (std::chrono::steady_clock::now()<deadline) {
        a.poll(); b.poll(); require(a.connected() && b.connected(),"team clients remain connected");
        if (done()) return;
        a.sendInput({}); b.sendInput({}); std::this_thread::sleep_for(4ms);
    }
    throw std::runtime_error("Timed out waiting for team roster");
}
void render(tw::Renderer& r,sf::RenderTexture& image,const tw::Config& c,const tw::Snapshot& s) {
    image.clear(); r.resize(c); r.observe(s); r.scenery(c,2,false,&s); r.battle(c,s,tw::noPlayer,{},false); r.hud(c,s,0,true); image.display();
}
void run() {
    tw::Config c; c.loadoutSelection=true; c.titanMode=true;
    tw::net::Server server(c,0); tw::net::Client a,b;
    require(a.connect("127.0.0.1",server.port(),"HOST") && b.connect("127.0.0.1",server.port(),"GUEST"),"team clients connect");
    until(a,b,[&]{return a.welcomed() && b.welcomed() && a.latest().players.size()==2;});
    a.mode(4);
    until(a,b,[&]{return a.latest().players.size()==8 && b.latest().players.size()==8 && b.config().playerLimit==8;});
    require(b.config().teams && b.latest().modernRules,"room mode synchronizes to guest");
    b.seat(0,7); b.seat(b.id(),7);
    until(a,b,[&]{const auto& p=b.latest().players; return std::ranges::find(p,b.id(),&tw::Player::id)->seat==7;});
    require(std::ranges::find(b.latest().players,0,&tw::Player::id)->seat==0,"guest cannot drag host");
    a.seat(b.id(),2);
    until(a,b,[&]{return std::ranges::find(b.latest().players,b.id(),&tw::Player::id)->seat==2;});
    auto bot=std::ranges::find(a.latest().players,true,&tw::Player::bot)->id;
    a.botWeapon(bot,tw::WeaponKind::Flare);
    a.weapon(tw::WeaponKind::Mortar); b.weapon(tw::WeaponKind::Missile); a.ready(true); b.ready(true);
    until(a,b,[&]{return std::ranges::all_of(a.latest().players,[](const auto& p){return p.ready;});});
    require(std::ranges::find(a.latest().players,bot,&tw::Player::id)->weapon==tw::WeaponKind::Flare,"bot weapon change reaches host and guest");
    a.map(2);
    until(a,b,[&]{return a.latest().phase==tw::Phase::Playing && b.latest().phase==tw::Phase::Playing && a.latest().tick==b.latest().tick;});
    require(a.latest().tanks.size()==8 && b.latest().terrain==a.latest().terrain,"all eight tanks and terrain synchronize");
    sf::RenderTexture ia({1600,900}),ib({1600,900}); tw::Renderer ra(ia,TANKWARS_TEST_ASSETS),rb(ib,TANKWARS_TEST_ASSETS);
    ra.settings.shake=rb.settings.shake=false;
    const auto sa=a.latest(),sb=b.latest(); render(ra,ia,a.config(),sa); render(rb,ib,b.config(),sb);
    auto first=ia.getTexture().copyToImage(),second=ib.getTexture().copyToImage();
    require(std::equal(first.getPixelsPtr(),first.getPixelsPtr()+1600*900*4,second.getPixelsPtr()),"eight-player HUD and world match on both clients");
    for (std::uint8_t mapId=0;mapId<4;++mapId) {
        auto config=tw::MapFactory::preset(a.config(),mapId); tw::World world(config,123); world.reset(sa.players);
        auto state=world.snapshot(); state.players=sa.players; state.phase=tw::Phase::Playing; state.teamBattle=true;
        state.roomSize=8; state.elapsed=36;
        ra.resetEffects(); render(ra,ia,config,state);
        require(ia.getTexture().copyToImage().saveToFile(std::filesystem::temp_directory_path()/("tankwars-1.6-map-"+std::to_string(mapId)+".png")),"save uniform HUD on every map");
        if (mapId==3) {
            state.events={{9000,tw::EventKind::MortarImpact,{1600,world.map().height(1600)},0,110,1}};
            ra.observe(state);
            for (int frame=0;frame<3;++frame) {
                ra.update(frame==0 ? 0.22f : 0.55f); render(ra,ia,config,state);
                require(ia.getTexture().copyToImage().saveToFile(std::filesystem::temp_directory_path()/("tankwars-1.6-blast-"+std::to_string(frame)+".png")),"save dense blast phases");
            }
        }
    }
    std::cout<<"Nightfall TCP checks passed: roster changes, authorization, bot loadouts, 4v4 and visual parity\n";
}
}
int main() { try { run(); return 0; } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; } }
