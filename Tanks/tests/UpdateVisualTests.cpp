#include "tankwars/Renderer.hpp"
#include "tankwars/Weapon.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace tw;
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
bool same(const sf::Image& a,const sf::Image& b) {
    return std::equal(a.getPixelsPtr(),a.getPixelsPtr()+1600*900*4,b.getPixelsPtr());
}
void run() {
    sf::RenderTexture a({1600,900}),b({1600,900});
    Renderer ra(a,TANKWARS_TEST_ASSETS),rb(b,TANKWARS_TEST_ASSETS);
    ra.settings.shake=rb.settings.shake=false;
    const auto output=std::filesystem::temp_directory_path()/"tankwars-1.8.1-previews";
    std::filesystem::create_directories(output);
    const auto render=[](Renderer& renderer,sf::RenderTexture& target,const Config& c,const Snapshot& s,float localTime) {
        target.clear(); renderer.resize(c); renderer.scenery(c,localTime,false,&s);
        renderer.battle(c,s,noPlayer,{},false); target.display();
        return target.getTexture().copyToImage();
    };
    for (std::uint8_t map=0;map<4;++map) {
        auto c=MapFactory::preset({},map); World world(c,123); world.reset({});
        auto state=world.snapshot(); state.elapsed=22;
        auto first=render(ra,a,c,state,2);
        auto second=render(rb,b,c,state,140);
        require(same(first,second),"atmosphere follows shared match time rather than client uptime");
        require(first.saveToFile(output/("map-"+std::to_string(map)+".png")),"save updated map preview");
        state.elapsed=29;
        require(!same(first,render(ra,a,c,state,2)),"fog, wind, vegetation or distant blasts animate with match time");
        if (map==3) for (float time : {17.2f,20.f,29.f,48.f}) {
            state.elapsed=time;
            require(render(ra,a,c,state,0).saveToFile(output/("stalingrad-"+std::to_string(static_cast<int>(time))+".png")),"save distant explosion phases");
        }
    }
    Config c; ra.resize(c); a.clear({12,22,35});
    for (unsigned i=0;i<3;++i) {
        const auto kind=static_cast<GadgetKind>(i);
        ra.gadgetPreview(kind,WeaponKind::Missile,{300+i*500.f,460},2);
        ra.text(gadgetName(kind,WeaponKind::Missile),{300+i*500.f,535},24,Renderer::white,true);
    }
    a.display(); require(a.getTexture().copyToImage().saveToFile(output/"gadget-selection.png"),"save dark armor and twin exhaust previews");
    Snapshot detail; detail.elapsed=22; detail.round=1;
    for (unsigned i=0;i<4;++i) {
        Projectile orb{100+i,0,{250+i*320.f,330},{650,-10},2.5f};
        orb.weapon=WeaponKind::Ricochet; orb.bounces=i; detail.projectiles.push_back(orb);
    }
    for (unsigned i=0;i<8;++i) {
        Projectile pellet{200+i,0,{700,480},direction((-13+i*3.6f)*pi/180)*2200,0};
        pellet.clusterPellet=true; detail.projectiles.push_back(pellet);
    }
    Tank jet; jet.position={350,590}; jet.hp=jet.maxHp=1000; jet.hasGadget=true; jet.jetActive=true;
    Tank armor=jet; armor.position={550,600}; armor.color=1; armor.jetActive=false; armor.gadget=GadgetKind::Armor; armor.armor=250;
    detail.tanks={jet,armor}; ra.resetEffects();
    for (int frame=0;frame<30;++frame) {
        for (auto& p : detail.projectiles) Physics::integrate(p,1.f/120,p.clusterPellet ? c.gravity : 0);
        ra.update(1.f/120); render(ra,a,c,detail,0);
    }
    require(a.getTexture().copyToImage().saveToFile(output/"weapon-and-tank-details.png"),"save ricochet trail stages, orange cluster tracers, armor and twin jets");
    std::cout<<"Updated atmosphere parity and animation checks passed; previews: "<<output<<'\n';
}
}
int main() { try { run(); return 0; } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; } }
