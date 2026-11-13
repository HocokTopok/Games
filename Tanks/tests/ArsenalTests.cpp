#include "tankwars/GameSession.hpp"
#include "tankwars/Weapon.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
bool near(float a,float b,float tolerance=0.12f) { return std::abs(a-b)<tolerance; }
struct FlatTerrain : tw::Terrain { float height(float) const override { return 640; } };
struct SlopedTerrain : tw::Terrain { float height(float x) const override { return 500+0.3f*x; } };
std::vector<tw::Player> players(tw::WeaponKind first=tw::WeaponKind::Flare) {
    std::vector<tw::Player> p{{0,0,0,true,true,false,"HOST"},{1,1,1,true,true,false,"JOIN"}};
    p[0].weapon=first; p[1].weapon=tw::WeaponKind::Laser; p[0].weaponChosen=p[1].weaponChosen=true; return p;
}
void bouncingStone() {
    tw::Config c; FlatTerrain flat; SlopedTerrain slope;
    tw::Projectile p{1,0,{800,635},{1000,300},0.2f}; p.weapon=tw::WeaponKind::Ricochet;
    const float initial=tw::Cannon::radius(p,c.weapon);
    auto r=tw::Physics::advance(p,c.fixedStep,c,flat,{});
    require(!r.hit && r.ricochetCount==1 && p.bounces==1 && near(p.velocity.x,500) && p.velocity.y<0,"ground landing reflects the stone and halves its velocity");
    r=tw::Physics::advance(p,c.fixedStep,c,flat,{});
    require(!r.hit && p.bounces==1 && tw::Cannon::radius(p,c.weapon)>initial,"growth does not spend another bounce while leaving the ground");
    for (unsigned bounce=2;bounce<=3;++bounce) {
        p.position={800,flat.height(800)-tw::Cannon::radius(p,c.weapon)-0.2f}; p.velocity={1000,300};
        r=tw::Physics::advance(p,c.fixedStep,c,flat,{});
        require(!r.hit && p.bounces==bounce && tw::Cannon::ricochetDamage(bounce)==30+10*bounce,"second and third ground bounces add exactly ten damage");
    }
    p.position={800,flat.height(800)-tw::Cannon::radius(p,c.weapon)-0.2f}; p.velocity={1000,300};
    r=tw::Physics::advance(p,c.fixedStep,c,flat,{});
    require(r.hit && r.hit->tank==tw::noPlayer && p.bounces==3,"fourth ground contact terminates the projectile");
    p={2,0,{800,734},{800,400},0.2f}; p.weapon=tw::WeaponKind::Ricochet;
    r=tw::Physics::advance(p,c.fixedStep,c,slope,{});
    const float a=slope.slope(p.position.x);
    require(!r.hit && p.bounces==1 && tw::dot(p.velocity,{std::sin(a),-std::cos(a)})>0,"sloped terrain reflects along its surface normal");
    p={3,0,{750,300},{3000,0},1}; p.weapon=tw::WeaponKind::Ricochet; p.bounces=3;
    tw::Tank target; target.id=1; target.position={800,300};
    r=tw::Physics::advance(p,0.1f,c,flat,std::span<const tw::Tank>(&target,1));
    require(r.hit && r.hit->tank==1 && p.bounces==3,"fast spheres hit tanks continuously even with all bounces spent");
}
void flareDamage(bool boosted,bool shielded) {
    tw::Config c; c.titanMode=true; c.hillHeight=0;
    tw::World world(c); world.reset(players());
    auto& tank=const_cast<tw::Tank&>(world.tanks()[0]); tank.aim=tw::pi/2;
    tank.damageRemaining=boosted ? 15.f : 0.f; tank.shieldRemaining=shielded ? 10.f : 0.f;
    std::array<tw::Input,tw::maxPlayers> input{}; input[0]={0,tw::pi/2,0,true}; world.step(input);
    require(world.snapshot().skyStrikes.size()==1 && tank.hp==1000 && near(tank.reload,12),"flare impact marks a strike with a long reload and no immediate damage");
    const float scale=boosted ? 1.5f : 1.f;
    require(world.snapshot().skyStrikes[0].damageScale==scale,"strike captures the damage multiplier of its flare");
    int steps=0; bool active=false;
    while (!world.snapshot().skyStrikes.empty() && steps++<120*6) {
        const auto before=world.snapshot().skyStrikes[0].age;
        const float hp=tank.hp; world.step(input);
        if (before+c.fixedStep<c.weapon.skyWarmup) require(tank.hp==1000,"warning phase cannot damage a tank");
        if (before>c.weapon.skyWarmup && before<c.weapon.skyWarmup+1 && !shielded) {
            require(near(hp-tank.hp,c.weapon.skyDamagePerSecond*c.fixedStep*scale,0.001f),"beam applies gradual damage each simulation step"); active=true;
        }
    }
    require(steps<120*6 && (active || shielded),"strike finishes after its configured duration");
    require(near(tank.hp,shielded ? 1000.f : 1000-c.weapon.skyDamagePerSecond*c.weapon.skyDuration*scale),"beam deals exactly its DPS times duration and respects shields");
    require(std::ranges::count_if(world.snapshot().events,[](const auto& e){return e.kind==tw::EventKind::Shot;})==1,"holding fire cannot bypass the twelve-second reload");
    world.reset(players()); require(world.snapshot().skyStrikes.empty(),"rematch clears all sky strikes");
}
void escapeAndFlight() {
    tw::Config c; c.titanMode=true; c.hillHeight=0;
    tw::World world(c); world.reset(players());
    auto& owner=const_cast<tw::Tank&>(world.tanks()[0]); owner.aim=tw::pi/2;
    std::array<tw::Input,tw::maxPlayers> input{}; input[0]={0,tw::pi/2,0,true}; world.step(input); input[0].fire=false;
    const auto strike=world.snapshot().skyStrikes[0];
    owner.position.x=strike.position.x+strike.radius+c.tankHalfWidth+15;
    for (int i=0;i<120;++i) world.step(input);
    require(owner.hp==1000,"escaping the marked column avoids damage");
    auto& flyer=const_cast<tw::Tank&>(world.tanks()[1]); flyer.position={strike.position.x,strike.position.y-200}; flyer.canFly=true;
    world.step(input);
    require(flyer.hp<1000 && flyer.position.y<strike.position.y-150,"the visible vertical beam also hits airborne tanks");
    const float hp=flyer.hp; flyer.shieldRemaining=1;
    world.step(input); require(flyer.hp==hp,"shield blocks sustained damage inside the beam");
}
void napalmLifetime() {
    tw::Config c; c.titanMode=true; c.hillHeight=0;
    tw::World world(c); world.reset(players(tw::WeaponKind::Napalm));
    auto& owner=const_cast<tw::Tank&>(world.tanks()[0]); owner.aim=tw::pi/2;
    std::array<tw::Input,tw::maxPlayers> input{}; input[0]={0,tw::pi/2,0,true}; world.step(input); input[0].fire=false;
    require(world.snapshot().fires.size()==1,"napalm still merges nearby pellets into a single ground pool");
    require(near(world.snapshot().fires[0].remaining,4.f/1.5f-c.fixedStep,0.001f),"ground fire lifetime is exactly one and a half times shorter");
    for (int i=0;i<120*3;++i) world.step(input);
    require(world.snapshot().fires.empty() && owner.burnRemaining>0,"pool expires while existing residual burning remains intact");
}
void pendingStrikeVictory(bool draw) {
    tw::Config c; c.titanMode=true; c.hillHeight=0;
    tw::GameSession game(c); game.addPlayer("HOST"); game.addPlayer("JOIN");
    game.setWeapon(0,tw::WeaponKind::Flare); game.setWeapon(1,tw::WeaponKind::Missile);
    game.setReady(0,true); game.setReady(1,true); game.step();
    auto& owner=const_cast<tw::Tank&>(game.world().tanks()[0]); owner.aim=tw::pi/2;
    if (draw) owner.hp=200;
    game.setInput(0,{0,tw::pi/2,0,true}); game.step();
    require(game.snapshot().skyStrikes.size()==1,"session contains the pending orbital strike");
    tw::Physics::damage(const_cast<tw::Tank&>(game.world().tanks()[1]),c.titanHealth);
    game.setInput(0,{0,tw::pi/2,0,false}); game.step();
    require(game.snapshot().phase==tw::Phase::Playing,"killing the opponent cannot skip a pending sky strike");
    int steps=0;
    while (game.snapshot().phase==tw::Phase::Playing && steps++<120*7) game.step();
    require(steps<120*7 && game.snapshot().skyStrikes.empty() && game.snapshot().phase==tw::Phase::Victory,"match ends only once the beam expires");
    require(game.snapshot().winner==(draw ? tw::noPlayer : 0),"late beam damage is included in victory or draw resolution");
}
void mortarDirectHit() {
    tw::Config c; c.titanMode=true; c.hillHeight=0;
    tw::World world(c); world.reset(players(tw::WeaponKind::Mortar));
    auto& shooter=const_cast<tw::Tank&>(world.tanks()[0]); shooter.position={500,world.map().height(500)-c.tankHalfHeight}; shooter.aim=-0.6f;
    auto& target=const_cast<tw::Tank&>(world.tanks()[1]); target.position={570,world.map().height(570)-c.tankHalfHeight};
    // Put the target directly on the first segment of the shell's arc.
    target.canFly=true; target.position.y=shooter.pivot().y-43;
    std::array<tw::Input,tw::maxPlayers> input{}; input[0]={0,-0.6f,0,true}; world.step(input); input[0].fire=false;
    for (int i=0;i<20 && target.hp==1000;++i) world.step(input);
    require(near(target.hp,850),"a direct mortar hit deals exactly 150 damage");
    const auto state=world.snapshot();
    require(std::ranges::any_of(state.events,[](const auto& e){return e.kind==tw::EventKind::MortarImpact && e.radius==110;}),"larger mortar blast radius reaches the renderer");
}
}
int main() {
    try { bouncingStone(); flareDamage(false,false); flareDamage(true,false); flareDamage(false,true); escapeAndFlight(); napalmLifetime(); pendingStrikeVictory(false); pendingStrikeVictory(true); mortarDirectHit();
        std::cout<<"Arsenal passed: terrain bounces, damage, sustained sky beam, shields, escape, flight and mortar direct hits\n"; return 0; }
    catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
