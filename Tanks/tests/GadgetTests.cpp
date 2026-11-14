#include "tankwars/GameSession.hpp"
#include "tankwars/Weapon.hpp"
#include "tankwars/Controls.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace tw;
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
bool near(float a,float b,float epsilon=0.03f) { return std::abs(a-b)<epsilon; }
Config config() { Config c; c.titanMode=true; c.asymmetricLoadouts=false; c.hillHeight=0; return c; }
std::vector<Player> players(WeaponKind weapon,GadgetKind gadget) {
    std::vector<Player> p{{0,0,0,true,true,false,"HOST"},{1,1,1,true,true,false,"GUEST"}};
    for (auto& pilot : p) { pilot.weapon=weapon; pilot.weaponChosen=true; pilot.seat=pilot.id; }
    p[0].gadget=gadget; p[0].gadgetChosen=true; return p;
}
Tank& tank(World& world,int id=0) { return const_cast<std::vector<Tank>&>(world.tanks()).at(id); }
void steps(World& world,int count,Input input={}) {
    std::array<Input,maxPlayers> inputs{}; inputs[0]=input;
    for (int i=0;i<count;++i) world.step(inputs);
}
void selection() {
    auto c=config(); c.loadoutSelection=true; GameSession game(c);
    game.addPlayer("HOST"); game.addPlayer("GUEST");
    require(!game.setWeapon(0,WeaponKind::Napalm) && !game.confirmWeapon(0,true) && !game.setGadget(0,GadgetKind::Armor),"map stage cannot be skipped");
    require(!game.setMap(1,1) && game.setMap(0,1),"host chooses the map first");
    for (int id=0;id<2;++id) {
        require(game.setWeapon(id,WeaponKind::Napalm),"weapon selectable after map");
        require(!game.setGadget(id,GadgetKind::Armor),"weapon must be confirmed before gadgets");
        require(game.confirmWeapon(id,true),"confirm weapon advances to gadgets");
        game.setReady(id,true); require(!game.snapshot().players[id].ready,"gadget choice required");
        require(game.setGadget(id,id ? GadgetKind::Jetpack : GadgetKind::Armor),"choose gadget");
        game.setReady(id,true);
    }
    require(!game.setGadget(0,static_cast<GadgetKind>(3)),"invalid gadget rejected");
    require(game.confirmWeapon(0,false),"return to weapon stage"); game.step();
    require(game.snapshot().phase==Phase::Lobby,"changing a confirmed loadout prevents deployment");
    game.confirmWeapon(0,true); game.setReady(0,true); game.step();
    require(game.snapshot().phase==Phase::Playing && game.snapshot().tanks[0].armor==250,"confirmed gadgets reach combat");
    require(!game.setGadget(0,GadgetKind::Specialist),"cannot change gadgets in combat");
    for (auto& t : const_cast<std::vector<Tank>&>(game.world().tanks())) t.hp=0;
    game.step(); game.rematch();
    require(!game.snapshot().mapSelected && !game.snapshot().players[0].weaponConfirmed,"rematch resets all stages");
    Controls controls;
    controls.handle(sf::Event::KeyPressed{.scancode=sf::Keyboard::Scancode::E});
    require(controls.sample(0).gadget,"E activates gadget");
    controls.handle(sf::Event::FocusLost{}); require(!controls.sample(0).gadget,"focus loss releases gadget");
}
void armor() {
    World world(config(),123); world.reset(players(WeaponKind::Mortar,GadgetKind::Armor));
    auto& t=tank(world); require(t.armor==250 && t.hp==1000,"armor adds 250 without changing health");
    t.shieldRemaining=1; Physics::damage(t,100); require(t.armor==250,"shield protects armor"); t.shieldRemaining=0;
    Physics::damage(t,200); require(t.armor==50 && t.hp==1000,"armor absorbs damage first");
    // World damage emits one detachment event when a real projectile strips the last plate.
    t.armor=1; t.position={500,400}; t.aim=0; tank(world,1).position={550,400};
    std::array<Input,maxPlayers> in{}; in[1]={0,pi,0,true}; tank(world,1).aim=pi; tank(world,1).weapon=WeaponKind::Cannon;
    world.step(in); for (int i=0;i<60 && t.armor>0;++i) world.step({});
    require(t.armor==0 && t.hp<1000,"overflow reaches health");
    require(std::ranges::count(world.snapshot().events,EventKind::ArmorBreak,&GameEvent::kind)==1,"armor detaches once");
    steps(world,10); require(std::ranges::count(world.snapshot().events,EventKind::ArmorBreak,&GameEvent::kind)==1,"broken armor does not repeat");
}
void jet() {
    for (std::uint8_t map=0;map<4;++map) {
        auto c=MapFactory::preset(config(),map); World world(c,123); world.reset(players(WeaponKind::Mortar,GadgetKind::Jetpack));
        const float ground=tank(world).position.y;
        steps(world,120,{0,-0.6f,0,false,0,true});
        require(near(tank(world).fuel,0.5f) && tank(world).verticalVelocity<0,"holding E accelerates upward and consumes fuel");
        const float rising=tank(world).verticalVelocity;
        steps(world,60); require(near(tank(world).fuel,0.6f) && tank(world).verticalVelocity>rising,"release applies gravity and restores fuel 2.5x slower");
        steps(world,300); require(near(tank(world).position.y,ground) && near(tank(world).fuel,1),"tank lands and refuels");
        float highest=ground;
        for (int i=0;i<240;++i) { steps(world,1,{0,-0.6f,0,false,0,true}); highest=std::min(highest,tank(world).position.y); }
        require(ground-highest>c.worldHeight*0.22f && ground-highest<c.worldHeight*0.37f,"full fuel reaches roughly middle-map altitude at every scale");
        steps(world,5,{0,-0.6f,0,false,0,true}); require(!tank(world).jetActive,"exhausted engine stops thrust");
        steps(world,60,{0,-0.6f,0,false,0,true}); require(!tank(world).jetActive,"empty engine does not pulse while held");
    }
}
void napalm() {
    auto c=config(); World blue(c,123),normal(c,123);
    blue.reset(players(WeaponKind::Napalm,GadgetKind::Specialist)); normal.reset(players(WeaponKind::Napalm,GadgetKind::Armor));
    for (auto* world : {&blue,&normal}) { tank(*world).position={600,400}; tank(*world).aim=0; tank(*world,1).position={680,400}; }
    steps(blue,1,{0,0,0,true,0,true}); steps(normal,1,{0,0,0,true});
    require(blue.snapshot().projectiles.front().blueFire && near(tank(blue).gadgetCooldown,15),"blue fire activates for ten seconds with fifteen-second cooldown");
    for (int i=0;i<18;++i) { steps(blue,1,{0,0}); steps(normal,1,{0,0}); }
    require(tank(blue,1).blueBurn && near(1000-tank(blue,1).hp,(1000-tank(normal,1).hp)*1.5f,0.03f),"direct hit and burning deal 1.5x damage");
    tank(blue).position={600,blue.map().height(600)-12}; tank(blue).aim=pi/2; tank(blue).reload=0;
    steps(blue,1,{0,pi/2,0,true});
    require(!blue.snapshot().fires.empty() && blue.snapshot().fires.front().blueFire,"blue pools preserve fire identity");
    tank(blue).gadgetRemaining=0; tank(blue).reload=0;
    steps(blue,1,{0,pi/2,0,true}); require(blue.snapshot().fires.front().blueFire,"ordinary fire cannot replace blue pools");
    steps(blue,1200); require(tank(blue).gadgetRemaining==0 && tank(blue).gadgetCooldown>0,"active effect ends independently of cooldown");
}
void mortar() {
    int triples=0;
    for (unsigned seed=1;seed<=250;++seed) {
        World world(config(),seed); world.reset(players(WeaponKind::Mortar,GadgetKind::Specialist));
        tank(world).position={800,300}; tank(world).aim=-pi/2;
        steps(world,1,{0,-pi/2,0,true}); const auto p=world.snapshot().projectiles;
        require(p.size()==1 || p.size()==3,"mortar produces one shell or a triple");
        if (p.size()==3) { ++triples; require(near(angleDelta(std::atan2(p[1].velocity.y,p[1].velocity.x),std::atan2(p[0].velocity.y,p[0].velocity.x)),-0.035f,0.001f) && near(angleDelta(std::atan2(p[1].velocity.y,p[1].velocity.x),std::atan2(p[2].velocity.y,p[2].velocity.x)),0.035f,0.001f),"bonus shells spread symmetrically"); }
    }
    require(triples>35 && triples<65,"seeded mortar trials approximate twenty percent");
}
void ricochet() {
    World world(config(),123); world.reset(players(WeaponKind::Ricochet,GadgetKind::Specialist));
    tank(world).position={800,300}; tank(world).aim=-pi/2;
    steps(world,1,{0,-pi/2,0,true,0,true});
    require(near(tank(world).reload,config().weapon.ricochetReload/2) && near(tank(world).gadgetRemaining,5),"overdrive doubles fire rate for five seconds");
    steps(world,601,{0,-pi/2,0,false,0,true});
    require(tank(world).gadgetRemaining==0 && tank(world).gadgetCooldown>14,"holding E cannot reactivate overdrive");
    tank(world).reload=0; steps(world,1,{0,-pi/2,0,true}); require(near(tank(world).reload,config().weapon.ricochetReload),"base fire rate returns");
    require(Cannon::ricochetDamage(0)==10 && Cannon::ricochetDamage(1)==25 && Cannon::ricochetDamage(2)==40 && Cannon::ricochetDamage(3)==55,"new ricochet balance");
}
void cluster() {
    World world(config(),123); world.reset(players(WeaponKind::Missile,GadgetKind::Specialist));
    tank(world).position={600,300}; tank(world).aim=0;
    steps(world,1,{0,0,0,true}); const auto missile=world.snapshot().projectiles.front();
    const auto terrain=world.snapshot().terrain;
    steps(world,1,{0,0,0,false,0,true}); auto shot=world.snapshot();
    require(shot.projectiles.size()==15 && std::ranges::all_of(shot.projectiles,[](const auto& p){return p.clusterPellet;}),"E splits missile into fifteen pellets");
    for (const auto& p : shot.projectiles) {
        require(near(length(p.velocity),length(missile.velocity)*2,0.1f),"pellets inherit twice missile speed");
        require(std::abs(angleDelta(std::atan2(missile.velocity.y,missile.velocity.x),std::atan2(p.velocity.y,p.velocity.x)))<=pi/12+0.001f,"pellets stay inside thirty-degree cone");
    }
    require(shot.terrain==terrain,"cluster burst does not destroy terrain");
    require(clusterDamage(0)==15 && clusterDamage(200)==15 && clusterDamage(300)==12.5f && clusterDamage(400)==10 && clusterDamage(600)==7.5f && clusterDamage(800)==5 && clusterDamage(2000)==5,"piecewise linear pellet falloff");
    auto& target=tank(world,1); target.position={850,300};
    steps(world,60,{0,0}); require(target.hp<1000 && world.snapshot().terrain==terrain,"pellets damage tanks without craters");
}
void solar() {
    auto c=config(); World world(c,123); world.reset(players(WeaponKind::Flare,GadgetKind::Specialist));
    tank(world).aim=pi/2;
    steps(world,1,{0,pi/2,0,false,0,true}); require(tank(world).specialistArmed,"supernova arms the next shot");
    steps(world,1,{0,pi/2,0,true}); auto s=world.snapshot();
    require(s.skyStrikes.size()==1 && s.skyStrikes[0].empowered && s.skyStrikes[0].radius==230,"supernova doubles radius");
    require(tank(world).specialistUsed && !tank(world).specialistArmed,"empowerment consumed by next shot");
    steps(world,90); require(std::ranges::none_of(world.snapshot().events,[](const auto& e){return e.kind==EventKind::SkyStrike;}),"supernova warning lasts 1.5x longer");
    steps(world,30); require(std::ranges::any_of(world.snapshot().events,[](const auto& e){return e.kind==EventKind::SkyStrike && e.intensity==1;}),"supernova starts with a flash event");
    tank(world).hp=1000; tank(world).shieldRemaining=10;
    steps(world,500); require(!world.snapshot().skyStrikes.empty(),"supernova lasts beyond normal beam duration");
    steps(world,230); require(world.snapshot().skyStrikes.empty(),"supernova ends after six seconds");
    steps(world,1,{0,pi/2,0,false,0,true}); require(!tank(world).specialistArmed,"supernova usable once per round");
}
}
int main() { try { selection(); armor(); jet(); napalm(); mortar(); ricochet(); cluster(); solar(); std::cout<<"Gadget simulation and selection checks passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; } }
