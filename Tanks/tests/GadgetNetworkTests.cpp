#include "../src/Network.cpp"
#include "tankwars/Renderer.hpp"
#include "tankwars/Weapon.hpp"
#include <filesystem>
#include <iostream>
#include <limits>

namespace {
using namespace tw;
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
template<class Predicate> void until(net::Client& a,net::Client& b,Predicate done,Input input={}) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (std::chrono::steady_clock::now()<deadline) {
        a.poll(); b.poll(); require(a.connected() && b.connected(),"gadget clients stay connected");
        if (done()) return;
        a.sendInput(input); b.sendInput({}); std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    throw std::runtime_error("Timed out waiting for gadget snapshot");
}
void live() {
    Config c; c.loadoutSelection=true; c.titanMode=true; c.asymmetricLoadouts=false;
    net::Server server(c,0); net::Client host,guest;
    require(host.connect("127.0.0.1",server.port(),"HOST") && guest.connect("127.0.0.1",server.port(),"GUEST"),"connect gadget peers");
    until(host,guest,[&]{return host.welcomed() && guest.welcomed() && host.latest().players.size()==2;});
    host.ready(true); guest.ready(true); host.gadget(GadgetKind::Armor);
    guest.map(3); host.map(1); until(host,guest,[&]{return guest.latest().mapSelected;});
    require(guest.latest().mapId==1 && guest.latest().phase==Phase::Lobby,"host alone selects map before weapons");
    host.weapon(WeaponKind::Napalm); guest.weapon(WeaponKind::Mortar); host.confirmWeapon(true); guest.confirmWeapon(true);
    host.gadget(GadgetKind::Jetpack); guest.gadget(GadgetKind::Armor); host.ready(true); guest.ready(true);
    until(host,guest,[&]{return guest.latest().phase==Phase::Playing;});
    require(guest.latest().gadgetData && findTank(guest.latest(),1)->armor==250,"armor and gadget stage reach guest");
    const auto initial=*findTank(guest.latest(),0);
    until(host,guest,[&]{return guest.latest().elapsed>0.7f;},{0,-0.7f,0,false,0,true});
    const auto flying=*findTank(guest.latest(),0);
    require(flying.jetActive && flying.fuel<0.7f && flying.position.y<initial.position.y-60,"E input drives authoritative flight visible to guest");
    until(host,guest,[&]{return guest.latest().elapsed>1.25f;});
    require(!findTank(guest.latest(),0)->jetActive && std::abs(findTank(guest.latest(),0)->fuel-flying.fuel)<0.04f,"release preserves airborne fuel on both peers");
}
void codecAndGraphics() {
    Config c=MapFactory::preset({},1); c.titanMode=true;
    std::vector<Player> p{{0,0,0,true,true,false,"HOST"},{1,1,1,true,true,false,"GUEST"}};
    p[0].weapon=WeaponKind::Napalm; p[1].weapon=WeaponKind::Ricochet;
    for (auto& v : p) { v.weaponChosen=v.weaponConfirmed=v.gadgetChosen=true; v.seat=v.id; }
    p[0].gadget=GadgetKind::Specialist; p[1].gadget=GadgetKind::Armor;
    World world(c,123); world.reset(p); auto source=world.snapshot(); source.players=p; source.phase=Phase::Playing; source.elapsed=6.06f;
    auto& first=source.tanks[0]; first.hp=210; first.position={800,650}; first.blueBurn=true; first.burnRemaining=2; first.gadgetRemaining=7; first.gadgetCooldown=12; first.velocity={72,0};
    source.tanks[1].position={1100,600}; source.tanks[1].armor=165;
    source.projectiles.push_back(Cannon::fire(first,c.weapon,90)); source.projectiles.back().blueFire=true; source.projectiles.back().damageScale=2.25f;
    source.projectiles.push_back({91,1,{1000,500},{1000,-30},0.2f}); source.projectiles.back().clusterPellet=true; source.projectiles.back().travel=250;
    source.projectiles.push_back({92,0,{1500,600},{200,-200},0.3f}); source.projectiles.back().weapon=WeaponKind::Flare; source.projectiles.back().empowered=true;
    source.fires.push_back({100,0,{780,world.map().height(780)-3},2,60,2.25f,true});
    source.skyStrikes.push_back({101,0,{1700,world.map().height(1700)},1.05f,230,1.5f,true});
    source.events.push_back({110,EventKind::ArmorBreak,{1100,600},1,30,1});
    source.events.push_back({111,EventKind::ClusterBurst,{1000,500},1,22,1});
    sf::Packet wire; net::writeState(wire,source,true); Snapshot decoded;
    require(net::readState(wire,decoded),"new gadget snapshot decodes");
    require(decoded.players[0].gadget==GadgetKind::Specialist && decoded.players[0].weaponConfirmed && decoded.players[0].gadgetChosen,"lobby stage fields roundtrip");
    require(decoded.tanks[1].armor==165 && decoded.tanks[0].blueBurn && decoded.tanks[0].gadgetCooldown==12,"armor and skill state roundtrip");
    require(decoded.projectiles[0].blueFire && decoded.projectiles[0].damageScale==2.25f && decoded.projectiles[1].clusterPellet && decoded.projectiles[1].travel==250 && decoded.projectiles[2].empowered,"all projectile modifiers roundtrip");
    require(decoded.fires[0].blueFire && decoded.skyStrikes[0].empowered && decoded.skyStrikes[0].radius==230,"fire priority and doubled beam survive codec");
    auto bad=source; bad.tanks[0].fuel=std::numeric_limits<float>::quiet_NaN(); sf::Packet invalid; net::writeState(invalid,bad,true); Snapshot rejected;
    require(!net::readState(invalid,rejected),"nonfinite gadget fuel is rejected");
    sf::Packet truncated; truncated.append(wire.getData(),wire.getDataSize()-1); require(!net::readState(truncated,rejected),"partial gadget state is rejected");
    sf::RenderTexture a({1600,900}),b({1600,900}); Renderer ra(a,TANKWARS_TEST_ASSETS),rb(b,TANKWARS_TEST_ASSETS);
    ra.settings.shake=rb.settings.shake=false;
    const auto render=[&](Renderer& r,sf::RenderTexture& target,const Snapshot& s) {
        target.clear(); r.resize(c); r.observe(s); r.update(0.4f); r.scenery(c,s.elapsed,false,&s); r.battle(c,s,noPlayer,{},false); r.hud(c,s,0); target.display();
    };
    render(ra,a,source); render(rb,b,decoded);
    const auto ia=a.getTexture().copyToImage(),ib=b.getTexture().copyToImage();
    require(std::equal(ia.getPixelsPtr(),ia.getPixelsPtr()+1600*900*4,ib.getPixelsPtr()),"new fire, armor debris, sparks, pellets and supernova render identically on peers");
    require(ia.saveToFile(std::filesystem::temp_directory_path()/"tankwars-1.8.1-gadgets.png"),"save new gadget effects preview");
    auto flash=source; flash.skyStrikes[0].age=c.weapon.skyWarmup*1.5f+0.001f;
    ra.resetEffects(); render(ra,a,flash);
    const auto white=a.getTexture().copyToImage();
    require(white.getPixel({100,100})==sf::Color::White && white.getPixel({700,200})==sf::Color::White,"initial supernova flash covers HUD and world");
    flash.skyStrikes[0].age+=1; render(ra,a,flash);
    require(a.getTexture().copyToImage().getPixel({100,100})!=sf::Color::White,"screen flash fades while beam remains active");
    // Separate close views make fine tank details and the fuel gauge reviewable.
    c=MapFactory::preset({},0); c.titanMode=true; World closeWorld(c,123); closeWorld.reset(p);
    auto close=closeWorld.snapshot(); close.players=p; close.phase=Phase::Playing; close.elapsed=6.06f;
    close.tanks[0].position={600,600}; close.tanks[0].hp=210; close.tanks[0].velocity={72,0}; close.tanks[0].gadget=GadgetKind::Jetpack; close.tanks[0].fuel=0.63f; close.tanks[0].jetActive=true;
    close.tanks[1].position={1000,610}; close.tanks[1].armor=250;
    ra.resetEffects(); render(ra,a,close); require(a.getTexture().copyToImage().saveToFile(std::filesystem::temp_directory_path()/"tankwars-1.8.1-tank-details.png"),"save fuel and armor preview");
    a.clear(); ra.resize(c); ra.scenery(c,2,true); ra.updateBanner(2); a.display();
    require(a.getTexture().copyToImage().saveToFile(std::filesystem::temp_directory_path()/"tankwars-1.8.1-banner.png"),"save update banner");
}
}
int main() { try { live(); codecAndGraphics(); std::cout<<"Gadget LAN, codec and pixel parity checks passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; } }
