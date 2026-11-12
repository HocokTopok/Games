#include "tankwars/GameSession.hpp"
#include "tankwars/Weapon.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
bool near(float a,float b,float tolerance=0.03f) { return std::abs(a-b)<tolerance; }
std::vector<tw::Player> players() {
    std::vector<tw::Player> p{{0,0,0,true,true,false,"HOST"},{1,1,1,true,true,false,"JOIN"}};
    p[0].weapon=tw::WeaponKind::Missile; p[1].weapon=tw::WeaponKind::Mortar;
    p[0].weaponChosen=p[1].weaponChosen=true; return p;
}
void steering() {
    tw::Config c; c.hillHeight=0;
    auto map=tw::MapFactory::create(c);
    tw::Tank owner; owner.id=0; owner.weapon=tw::WeaponKind::Missile; owner.position={220,600}; owner.aim=0;
    tw::Tank target; target.id=1; target.position={1000,600};
    std::array tanks{owner,target};
    auto p=tw::Cannon::fire(owner,c.weapon,1); p.position={500,500}; p.velocity={550,0}; p.age=0.3f;
    const float speed=tw::length(p.velocity);
    auto result=tw::Physics::advance(p,c.fixedStep,c,*map,tanks);
    require(!result.hit && near(tw::length(p.velocity),speed,0.001f),"rocket thrust maintains speed without gravity");
    require(p.velocity.y>0,"rocket follows the actual designator impact point below its flight path");
    const float before=std::atan2(p.velocity.y,p.velocity.x);
    tanks[0].aim=-tw::pi/2;
    result=tw::Physics::advance(p,c.fixedStep,c,*map,tanks);
    const float after=std::atan2(p.velocity.y,p.velocity.x);
    require(near(std::abs(tw::angleDelta(before,after)),c.weapon.missileTurnRate*c.fixedStep,0.0001f),"abrupt retargeting cannot exceed the turn limit");
    require(p.velocity.x>500,"rocket cannot instantly make a right angle turn");
    tanks[0].hp=0; const auto velocity=p.velocity;
    tw::Physics::advance(p,c.fixedStep,c,*map,tanks);
    require(tw::length(p.velocity-velocity)<0.001f,"lost designator leaves missile flying straight");
    require(c.weapon.missileSpeedScale>c.weapon.mortarSpeedScale && c.weapon.missileSpeedScale<c.weapon.mortarSpeedScale*1.25f,"missile is slightly faster than mortar");
    // A nearby ridge must stop the pointer before a tank hidden behind it.
    auto hills=tw::MapFactory::preset(c,1); auto forest=tw::MapFactory::create(hills);
    tanks[0].hp=1000; tanks[0].position={100,forest->height(100)-12}; tanks[0].aim=0;
    tanks[1].position={2100,forest->height(2100)-12};
    const auto beam=tw::Physics::guidanceTrace(tanks[0],hills,*forest,tanks);
    require(beam.hit && beam.hit->tank==tw::noPlayer && beam.to.x<800,"mountain blocks the designator's line of sight");
}
void damageAndReload(bool shielded,bool boosted) {
    tw::Config c; c.hillHeight=0; c.titanMode=true; c.asymmetricLoadouts=false;
    tw::World world(c); world.reset(players());
    auto& shooter=const_cast<tw::Tank&>(world.tanks()[0]);
    auto& target=const_cast<tw::Tank&>(world.tanks()[1]);
    target.position.x=shooter.position.x+500;
    shooter.aim=-0.01f; shooter.damageRemaining=boosted ? 15.f : 0.f;
    target.shieldRemaining=shielded ? 10.f : 0.f;
    std::array<tw::Input,tw::maxPlayers> input{}; input[0]={0,shooter.aim,0,true};
    const auto terrain=world.snapshot().terrain;
    world.step(input); input[0].fire=false;
    require(world.snapshot().lasers.empty() && world.snapshot().projectiles.size()==1,"launch creates a real rocket instead of instant laser damage");
    require(target.hp==1000 && near(shooter.reload,4),"launch has no instant damage and sets four-second reload");
    for (int step=0;step<240 && !world.snapshot().projectiles.empty();++step) world.step(input);
    require(world.snapshot().projectiles.empty(),"guided rocket reaches a stationary target");
    require(near(target.hp,shielded ? 1000.f : 1000-120*(boosted ? 1.5f : 1.f)),"direct hit deals exactly 120, captures boost and respects shield");
    require(world.snapshot().terrain==terrain,"rocket blast does not change terrain");
    require(std::ranges::any_of(world.snapshot().events,[](const auto& e){return e.kind==tw::EventKind::MissileImpact && near(tw::length(e.normal),1);}),"blast has a unit outward direction for graphics");
    input[0].fire=true;
    while (world.snapshot().elapsed<3.98f) world.step(input);
    require(std::ranges::count_if(world.snapshot().events,[](const auto& e){return e.kind==tw::EventKind::Shot;})==1,"holding fire respects four-second reload");
    for (int i=0;i<5;++i) world.step(input);
    require(std::ranges::count_if(world.snapshot().events,[](const auto& e){return e.kind==tw::EventKind::Shot;})==2,"next missile fires after the reload");
}
void splashAndGround() {
    tw::Config c; tw::Tank t; t.position={600,500};
    require(tw::Physics::missileDamage(t.position,t,true,c)==120,"direct damage is not added to splash");
    float previous=121;
    for (int d=0;d<52;++d) {
        const float damage=tw::Physics::missileDamage({t.position.x+c.tankHalfWidth+d,t.position.y},t,false,c);
        require(damage<=previous && damage>=1,"small blast decreases smoothly toward one damage"); previous=damage;
    }
    require(tw::Physics::missileDamage({t.position.x+c.tankHalfWidth+53,t.position.y},t,false,c)==0,"outside the smaller blast there is no damage");
    require(c.weapon.missileBlastRadius<c.weapon.mortarBlastRadius,"rocket blast is smaller than mortar");
    c.hillHeight=0; c.titanMode=true; tw::World world(c); world.reset(players());
    auto& owner=const_cast<tw::Tank&>(world.tanks()[0]); owner.aim=tw::pi/2;
    std::array<tw::Input,tw::maxPlayers> input{}; input[0]={0,tw::pi/2,0,true}; world.step(input);
    const auto state=world.snapshot();
    const auto e=std::ranges::find(state.events,tw::EventKind::MissileImpact,&tw::GameEvent::kind);
    require(e!=state.events.end() && e->normal.y< -0.99f,"ground explosion points away from the surface");
}
void profiles() {
    auto desert=tw::MapFactory::preset({},0); auto first=tw::MapFactory::create(desert);
    for (float x=0;x<1600;x+=4) {
        const float t=x/1600*2*tw::pi;
        require(near(first->height(x),desert.groundHeight+desert.hillHeight*(0.7f*std::cos(t)+0.3f*std::cos(3*t)),0.002f),"first battlefield is unchanged");
    }
    auto c=tw::MapFactory::preset({},1); auto forest=tw::MapFactory::create(c);
    require(c.worldWidth==2200 && c.worldHeight==1237.5f,"forest dimensions unchanged");
    const float a=forest->height(c.worldWidth*0.21f), b=forest->height(c.worldWidth*0.43f), d=forest->height(c.worldWidth*0.78f);
    require(b<d && d<a && a<c.worldHeight*0.51f && b<c.worldHeight*0.30f && d<c.worldHeight*0.37f,"three tall peaks have the requested height order");
    const float distant=forest->height(c.worldWidth*0.78f);
    forest->crater({200,forest->height(200)},52,14);
    require(near(forest->height(c.worldWidth*0.78f),distant,0.001f),"a local mortar crater cannot flatten remote mountains");
    c=tw::MapFactory::preset({},2); auto ravine=tw::MapFactory::create(c);
    require(c.worldWidth==2600 && c.worldHeight==1462.5f,"ravine dimensions unchanged");
    const float middle=ravine->height(1300);
    require(middle-ravine->height(0)>850 && middle-ravine->height(2600)>850,"ravine descends deeply from both edges");
    require(std::abs(ravine->height(800)-ravine->height(1800))>15,"ravine walls are asymmetric");
    c=tw::MapFactory::preset({},3); auto city=tw::MapFactory::create(c);
    require(c.worldWidth==3200 && c.worldHeight==1800,"city dimensions unchanged");
    int peaks=0; float steepest=0;
    for (float x=4;x<3196;x+=4) {
        peaks+=city->height(x)<city->height(x-4) && city->height(x)<city->height(x+4);
        steepest=std::max(steepest,std::abs(city->height(x+4)-city->height(x))/4);
    }
    require(peaks>=7 && steepest>2,"city has numerous sharply rising cover hills");
    require(near(c.weapon.napalmDirectDamage,2.7f/1.2f) && near(c.weapon.napalmSplashDamage,1.8f/1.2f) && near(c.weapon.fireDamagePerSecond,5) && near(c.weapon.burnDamagePerSecond,2.5f/1.2f),"all napalm damage reduced by the same 1.2 factor");
    require(c.weapon.napalmReload==0.12f && c.weapon.skyRadius==115 && c.weapon.skyDamagePerSecond==140,"fire cadence and beam DPS unchanged while beam is wider");
}
}
int main() {
    try { steering(); damageAndReload(false,false); damageAndReload(false,true); damageAndReload(true,false); splashAndGround(); profiles();
        std::cout<<"Guided missiles and terrain profiles passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
