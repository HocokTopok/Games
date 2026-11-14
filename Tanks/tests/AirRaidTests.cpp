#include "tankwars/GameSession.hpp"
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
std::vector<tw::Player> pilots() {
    std::vector<tw::Player> players{{0,0,0,true,true,false,"HOST"},{1,1,1,true,true,false,"JOIN"}};
    for (auto& p : players) { p.weapon=tw::WeaponKind::Mortar; p.weaponChosen=true; p.seat=p.id; }
    return players;
}
void coverage() {
    for (std::uint8_t id=0;id<4;++id) {
        auto c=tw::MapFactory::preset({},id); c.titanMode=true;
        tw::World world(c,123); world.reset(pilots());
        const auto advance=[&]() {
            for (auto& tank : const_cast<std::vector<tw::Tank>&>(world.tanks())) tank.shieldRemaining=10;
            world.step({});
        };
        while (world.snapshot().elapsed<tw::matchDuration-c.fixedStep*1.5f) advance();
        require(!world.snapshot().plane.bombing && world.snapshot().bombs.empty(),"no air raid before deadline");
        while (world.snapshot().elapsed<tw::matchDuration) advance();
        auto state=world.snapshot();
        require(state.plane.active && state.plane.bombing,"bomber starts exactly at deadline");
        const float direction=state.plane.direction;
        std::map<std::uint32_t,tw::AirBomb> last;
        std::map<std::uint32_t,tw::Vec2> release;
        std::map<std::uint32_t,tw::GameEvent> impacts;
        unsigned accelerated=0;
        while (world.snapshot().elapsed<tw::matchDuration+14) {
            advance(); state=world.snapshot();
            for (const auto& bomb : state.bombs) {
                if (!release.contains(bomb.id)) {
                    release[bomb.id]=bomb.position;
                    require(bomb.velocity.y<30 && bomb.velocity.x==0,"bomb starts slowly and falls vertically");
                }
                if (last.contains(bomb.id)) {
                    const auto& previous=last.at(bomb.id);
                    require(std::abs(bomb.velocity.y-previous.velocity.y-c.gravity*c.fixedStep)<0.01f,"bomb accelerates under gravity");
                    require(bomb.position.y>previous.position.y,"falling bomb moves downward"); ++accelerated;
                }
                last[bomb.id]=bomb;
            }
            for (const auto& e : state.events) if (e.kind==tw::EventKind::BombImpact) impacts[e.id]=e;
        }
        require(!state.plane.active && state.bombs.empty() && impacts.size()==release.size() && impacts.size()>=8 && accelerated>100,"every released bomb detonates before the next pass");
        const auto original=tw::MapFactory::create(c);
        float worst=1e9f, best=0;
        for (float x=c.tankHalfWidth;x<=c.worldWidth-c.tankHalfWidth;x+=4) {
            tw::Tank probe; probe.position={x,world.map().height(x)-c.tankHalfHeight}; probe.bodyAngle=world.map().slope(x);
            float total=0;
            for (const auto& [eventId,e] : impacts) {
                (void)eventId;
                const float linear=tw::Physics::areaDamage(e.position,probe,c.weapon.mortarBlastRadius,c.weapon.mortarDamage,c);
                total+=linear*linear/c.weapon.mortarDamage;
            }
            worst=std::min(worst,total); best=std::max(best,total);
            require(total>0,"there is no permanent safe ground between impacts, including ravine walls");
            require(world.map().height(x)>=original->height(x)-0.01f && world.map().height(x)<=world.map().bedrock(x)+0.01f,"bomb craters lower terrain without penetrating bedrock");
        }
        require(worst<35 && best>100,"moving between blast centers greatly reduces but does not remove damage");
        while (world.snapshot().elapsed<tw::matchDuration+tw::airRaidInterval) advance();
        state=world.snapshot(); require(state.plane.active && state.plane.bombing && state.plane.direction==-direction,"next raid starts after fifteen seconds from the opposite side");
        world.reset(pilots()); require(world.snapshot().bombs.empty() && !world.snapshot().plane.active,"rematch clears pending raids");
        std::cout<<"Map "<<int(id)<<": "<<impacts.size()<<" impacts, ground coverage damage "<<worst<<".."<<best<<'\n';
    }
}
void damageAndVictory() {
    tw::Config c; c.titanMode=true; c.asymmetricLoadouts=false; c.hillHeight=0;
    tw::GameSession game(c); game.addPlayer("HOST"); game.addPlayer("JOIN"); game.setReady(0,true); game.setReady(1,true); game.step();
    while (game.snapshot().elapsed<181) game.step();
    require(game.snapshot().phase==tw::Phase::Playing,"timeout does not declare a draw");
    auto& tanks=const_cast<std::vector<tw::Tank>&>(game.world().tanks());
    std::uint32_t bombId=0; float hp=0;
    for (int i=0;i<120*5 && bombId==0;++i) {
        const auto state=game.snapshot();
        const auto target=std::ranges::find_if(state.bombs,[&](const auto& b){return b.position.x>c.tankHalfWidth+8 && b.position.x<c.worldWidth-c.tankHalfWidth-8;});
        if (target!=state.bombs.end()) {
            const auto& bomb=*target;
            tanks[0].position={bomb.position.x,game.world().map().height(bomb.position.x)-c.tankHalfHeight};
            bombId=bomb.id; hp=tanks[0].hp;
        } else game.step();
    }
    require(bombId!=0,"an active bomb can be targeted in a running match");
    while (std::ranges::any_of(game.snapshot().bombs,[&](const auto& b){return b.id==bombId;})) game.step();
    require(std::abs((hp-tanks[0].hp)-c.weapon.mortarDamage)<0.1f,"direct bomb damage equals mortar damage");
    tanks[1].hp=0; game.step(); require(game.snapshot().phase==tw::Phase::Playing,"victory waits for the active bombing pass");
    for (int i=0;i<120*14 && game.snapshot().phase==tw::Phase::Playing;++i) { tanks[0].shieldRemaining=10; game.step(); }
    require(game.snapshot().phase==tw::Phase::Victory && game.snapshot().winner==0,"survivor wins after pending bombs finish");
}
}
int main() {
    try { coverage(); damageAndVictory(); std::cout<<"Air raid checks passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
