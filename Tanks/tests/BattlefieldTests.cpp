#include "tankwars/GameSession.hpp"
#include "tankwars/Weapon.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* what) { if (!ok) throw std::runtime_error(what); }
bool near(float a,float b,float tolerance=0.06f) { return std::abs(a-b)<tolerance; }
std::vector<tw::Player> players(tw::WeaponKind a,tw::WeaponKind b) {
    std::vector<tw::Player> p{{0,0,0,true,true,false,"HOST"},{1,1,1,true,true,false,"JOIN"}};
    p[0].weapon=a; p[1].weapon=b; p[0].weaponChosen=p[1].weaponChosen=true; return p;
}
void selection() {
    tw::Config c; c.loadoutSelection=true; tw::GameSession game(c);
    game.addPlayer("HOST"); game.addPlayer("JOIN");
    game.setReady(0,true); game.setReady(1,true); game.step();
    require(!game.snapshot().players[0].ready && game.snapshot().phase==tw::Phase::Lobby,"must select a weapon before confirmation");
    require(!game.setMap(0,0),"map cannot be chosen before weapons");
    require(!game.setWeapon(0,tw::WeaponKind::Cannon) && !game.setWeapon(0,tw::WeaponKind::GrowingOrb),"cannon and duplicate orb cannot be selected");
    for (auto weapon : tw::selectableWeapons) require(game.setWeapon(0,weapon),"all five weapons available");
    game.setWeapon(0,tw::WeaponKind::Napalm); game.setWeapon(1,tw::WeaponKind::Ricochet);
    game.setReady(0,true); game.setReady(1,true); game.step();
    require(game.snapshot().phase==tw::Phase::Lobby,"confirmations wait for host map");
    require(!game.setMap(1,2) && !game.setMap(0,4),"only host selects valid maps");
    require(game.setMap(0,3),"host selects battlefield"); game.step();
    const auto state=game.snapshot();
    require(state.phase==tw::Phase::Playing && state.mapId==3 && state.mapSelected,"chosen map starts game");
    require(state.tanks[0].weapon==tw::WeaponKind::Napalm && state.tanks[1].weapon==tw::WeaponKind::Ricochet,"loadouts are independent of player role");
    require(game.config().worldWidth==3200 && game.config().worldHeight==1800,"largest map scales camera");
    require(!game.setWeapon(0,tw::WeaponKind::Laser) && !game.setMap(0,1),"loadouts and map locked during battle");
}
void maps() {
    float previousWidth=0;
    for (std::uint8_t id=0;id<4;++id) {
        auto c=tw::MapFactory::preset({},id); c.validate(); auto map=tw::MapFactory::create(c);
        require(c.worldWidth>previousWidth,"each battlefield is larger"); previousWidth=c.worldWidth;
        for (float x=0;x<=c.worldWidth;x+=7) require(map->height(x)<=map->bedrock(x),"bedrock stays below surface");
        const float x=c.worldWidth*0.5f, before=map->height(x);
        map->crater({x,before},52,14);
        require(near(map->height(x),before+14),"mortar makes a shallow crater");
        require(std::abs(map->height(x-53)-(tw::MapFactory::create(c)->height(x-53)))<0.1f,"smooth crater meets untouched terrain");
        for (int n=0;n<60;++n) map->crater({x,map->height(x)},52,14);
        require(near(map->height(x),map->bedrock(x)),"repeated impacts reach unbreakable layer");
        for (float xx=1;xx<c.worldWidth;xx+=2) {
            require(map->height(xx)<=map->bedrock(xx)+0.01f,"erosion stays above bedrock");
            const auto original=tw::MapFactory::create(c);
            const float natural=std::abs(original->height(xx+2)-original->height(xx));
            require(std::abs(map->height(xx+2)-map->height(xx))<=natural+2.01f,"crater rims preserve existing mountain steepness");
        }
        const auto copy=tw::MapFactory::create(c,map->samples());
        for (float xx=0;xx<c.worldWidth;xx+=13) require(near(map->height(xx),copy->height(xx),0.001f),"snapshot reconstructs exact surface");
        if (id==2) {
            auto original=tw::MapFactory::create(c);
            for (float xx=0;xx<=c.worldWidth;xx+=4) {
                require(original->height(xx)/c.worldHeight*900<750,"ravine floor remains above the bottom panel with clearance");
                require(original->bedrock(xx)/c.worldHeight*900<753,"even fully eroded ravine stays above the panel");
            }
            require(original->height(0)<original->height(x)-700 && original->height(c.worldWidth)<original->height(x)-700,"high edges flank the deep ravine");
            bool mud=false,firm=false;
            for (float xx=c.worldWidth*0.3f;xx<c.worldWidth*0.7f;xx+=10) { mud|=original->movementScale(xx)<1; firm|=original->movementScale(xx)==1; }
            require(mud && firm && original->movementScale(100)==1,"marsh contains mud and firm hummocks");
        }
    }
}
void mudMovement() {
    auto c=tw::MapFactory::preset({},2); c.spawnInset=c.worldWidth*0.31f;
    tw::World world(c); world.reset(players(tw::WeaponKind::Napalm,tw::WeaponKind::Mortar));
    const float x=world.tanks()[0].position.x;
    std::array<tw::Input,tw::maxPlayers> input{}; input[0].move=1; world.step(input);
    require(world.tanks()[0].inMud && near(world.tanks()[0].position.x-x,c.tankSpeed*c.fixedStep*0.48f,0.001f),"mud slows actual tank movement");
}
void weapons() {
    tw::Config c; c.hillHeight=0; c.titanMode=true;
    tw::World world(c); auto p=players(tw::WeaponKind::Ricochet,tw::WeaponKind::Mortar); world.reset(p);
    tw::Projectile shot{1,0,{1400,200},{1200,-100},0}; shot.weapon=tw::WeaponKind::Ricochet;
    const float radius=tw::Cannon::radius(shot,c.weapon);
    auto result=tw::Physics::advance(shot,0.08f,c,world.map(),{});
    require(!result.hit && shot.velocity.y>-100 && shot.position.y<200,"ricochet has gravity and a parabolic arc");
    shot.position={1590,200}; shot.velocity={1200,0};
    result=tw::Physics::advance(shot,0.02f,c,world.map(),{});
    require(!result.hit && shot.bounces==1 && shot.velocity.x<0 && std::abs(shot.velocity.x+600)<1,"first wall bounce halves speed");
    require(tw::Cannon::radius(shot,c.weapon)>radius && tw::Cannon::ricochetDamage(shot.bounces)==40,"first bounce strengthens damage and effect");
    shot.position={8,200}; shot.velocity={-600,0};
    result=tw::Physics::advance(shot,0.02f,c,world.map(),{});
    require(!result.hit && shot.bounces==2 && near(shot.velocity.x,300) && tw::Cannon::ricochetDamage(2)==50,"second bounce gives 50 damage and halves speed");
    shot.position={1592,200}; shot.velocity={300,0};
    result=tw::Physics::advance(shot,0.03f,c,world.map(),{});
    require(!result.hit && shot.bounces==3 && tw::Cannon::ricochetDamage(3)==60,"third wall contact gives the last bounce and 60 damage");
    shot.position={tw::Cannon::radius(shot,c.weapon)+0.1f,200}; shot.velocity={-150,0};
    result=tw::Physics::advance(shot,0.03f,c,world.map(),{});
    require(result.hit && shot.bounces==3,"fourth wall contact detonates");
    shot={1,0,{6.5f,200},{-245,0},0}; shot.weapon=tw::WeaponKind::Ricochet; shot.bounces=1;
    result=tw::Physics::advance(shot,0.01f,c,world.map(),{});
    require(!result.hit && shot.bounces==2,"slow second ricochet survives its increased radius");
    result=tw::Physics::advance(shot,c.fixedStep,c,world.map(),{});
    require(!result.hit && shot.bounces==2 && shot.velocity.x>0,"moving away from wall does not count another collision");
    auto tank=world.tanks()[0]; tank.aim=-0.4f;
    const auto guide=tw::Physics::trajectory(tank,c,world.map(),{}); auto actual=tw::Cannon::fire(tank,c.weapon,99);
    do { result=tw::Physics::advance(actual,c.fixedStep,c,world.map(),{}); } while (!result.hit && !result.expired);
    require(tw::length(guide.back()-actual.position)<0.01f,"ricochet guide matches actual ballistic flight");
    require(c.weapon.laserReload==2.4f && c.weapon.laserHitDamage(1000)==80,"railgun has double reload interval and damage");
    for (auto kind : tw::selectableWeapons) {
        world.reset(players(kind,tw::WeaponKind::Mortar));
        const auto surface=world.snapshot().terrain; std::array<tw::Input,tw::maxPlayers> input{};
        const float aim=kind==tw::WeaponKind::Mortar ? -1.1f : tw::pi/2;
        input[0].aim=aim;
        for (int i=0;i<120;++i) world.step(input);
        input[0].fire=true; world.step(input); input[0].fire=false;
        for (int i=0;i<120*9;++i) world.step(input);
        require((surface!=world.snapshot().terrain)==(kind==tw::WeaponKind::Mortar || kind==tw::WeaponKind::Missile || kind==tw::WeaponKind::Flare),"mortar, missile and sky beam change terrain");
        world.reset(players(kind,tw::WeaponKind::Mortar)); require(surface==world.snapshot().terrain,"rematch restores terrain");
    }
    tank.hp=500; tank.maxHp=1000; tank.shieldRemaining=10;
    tw::Physics::damage(tank,9999); require(tank.hp==500,"shield blocks shots");
    tank.shieldRemaining=0; tw::Physics::damage(tank,80); require(tank.hp==420,"damage returns after shield expires");
    tank.damageRemaining=15;
    require(tw::Cannon::fire(tank,c.weapon,2).damageScale==1.5f,"damage perk is captured when projectile is fired");
}
void supplies() {
    auto c=tw::MapFactory::preset({},0); c.titanMode=true;
    tw::World world(c,0x51a7f00d); world.reset(players(tw::WeaponKind::Ricochet,tw::WeaponKind::Mortar));
    std::array<tw::Input,tw::maxPlayers> input{};
    bool plane=false, falling=false, landed=false;
    unsigned lastId=0; bool health=false,damage=false,shield=false;
    // Run deterministic supply cycles; move the host to each drop using the public controls.
    for (int i=0;i<120*390 && !(health && damage && shield);++i) {
        const auto s=world.snapshot();
        if (s.elapsed<29.9f) require(!s.plane.active && s.supplies.empty(),"no early supply plane");
        plane|=s.plane.active;
        if (!s.supplies.empty()) {
            const auto& d=s.supplies.back();
            require(d.position.x>=c.worldWidth*0.25f && d.position.x<=c.worldWidth*0.75f,"drop stays within middle half");
            falling|=!d.landed; landed|=d.landed;
            if (d.id!=lastId) {
                // Damage is applied through the normal damage API to verify the health pickup cap.
                auto& fixture=const_cast<tw::Tank&>(world.tanks()[0]); fixture.hp=fixture.maxHp;
                tw::Physics::damage(fixture,300);
                lastId=d.id;
            }
            const float delta=d.position.x-s.tanks[0].position.x;
            input[0].move=std::abs(delta)>1 ? (delta>0 ? 1.f : -1.f) : 0;
        } else input[0].move=0;
        world.step(input);
        const auto after=world.snapshot(); const auto& t=after.tanks[0];
        if (t.healIndicator>1.49f) { health=true; require(near(t.hp,900),"health restores exactly 200 HP"); }
        if (t.damageRemaining>14.99f) damage=true;
        if (t.shieldRemaining>9.99f) shield=true;
        if (!t.alive()) throw std::runtime_error("supply test host unexpectedly destroyed");
    }
    require(plane && falling && landed && health && damage && shield,"plane, parachute, landing and all three pickups work");
    input={}; for (int i=0;i<120*16;++i) world.step(input);
    require(world.tanks()[0].damageRemaining==0 && world.tanks()[0].shieldRemaining==0,"timed perks expire");
    world.reset(players(tw::WeaponKind::Laser,tw::WeaponKind::Napalm));
    require(world.snapshot().supplies.empty() && !world.snapshot().plane.active && world.tanks()[0].damageRemaining==0,"new round clears supplies and perks");
}
}
int main() {
    try { selection(); maps(); mudMovement(); weapons(); supplies(); std::cout<<"Battlefields passed: selection, maps, terrain, weapons, shields and supply pickups\n"; return 0; }
    catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
