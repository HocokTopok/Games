#include "tankwars/GameSession.hpp"
#include "tankwars/Weapon.hpp"
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
bool near(float a,float b,float tolerance=0.2f) { return std::abs(a-b)<tolerance; }
std::vector<tw::Player> pilots(tw::WeaponKind weapon) {
    std::vector<tw::Player> p{{0,0,0,true,true,false,"HOST"},{1,1,1,true,true,false,"JOIN"}};
    for (auto& player : p) { player.weapon=weapon; player.weaponChosen=true; player.seat=player.id; }
    return p;
}
void teams() {
    tw::Config c; c.loadoutSelection=true;
    for (bool teamMode : {false,true}) for (unsigned capacity : {3u,8u}) {
        auto invalid=c; invalid.teams=teamMode; invalid.playerLimit=capacity;
        bool rejected=false; try { invalid.validate(); } catch (...) { rejected=true; }
        require(rejected,"unsupported room capacities are rejected");
    }
    tw::GameSession game(c); game.addPlayer("HOST");
    require(!game.setMode(0,4) && !game.setMode(1,2) && game.setMode(0,2),"4v4 is unavailable and only host can choose 2v2");
    auto s=game.snapshot(); require(s.players.size()==4 && game.config().playerLimit==4,"all three open seats have bots");
    require(std::ranges::count(s.players,true,&tw::Player::bot)==3,"one human and three bots");
    require(std::ranges::count(s.players,0,&tw::Player::team)==2 && std::ranges::count(s.players,1,&tw::Player::team)==2,"teams have two seats each");
    auto id=game.addPlayer("GUEST"); require(id && *id!=0,"joining human replaces a bot");
    s=game.snapshot(); require(s.players.size()==4 && std::ranges::count(s.players,true,&tw::Player::bot)==2,"capacity remains four");
    require(!game.setSeat(*id,0,3) && game.setSeat(*id,*id,3),"guest can move self but cannot move host");
    s=game.snapshot(); const auto guest=std::ranges::find(s.players,*id,&tw::Player::id);
    require(guest->team==1 && guest->seat==3,"guest joins other team and slot");
    require(game.setSeat(0,*id,2),"host can move another pilot");
    // Keep the snapshot alive while resolving the bot ID.
    s=game.snapshot(); const auto botId=std::ranges::find(s.players,true,&tw::Player::bot)->id;
    require(!game.setBotWeapon(*id,botId,tw::WeaponKind::Napalm) && game.setBotWeapon(0,botId,tw::WeaponKind::Napalm),"host controls bot weapons");
    game.setWeapon(0,tw::WeaponKind::Mortar); game.setWeapon(*id,tw::WeaponKind::Missile);
    game.setReady(0,true); game.setReady(*id,true);
    require(game.setMap(0,2),"mixed human and bot roster can deploy"); game.step(); s=game.snapshot();
    require(s.phase==tw::Phase::Playing && s.tanks.size()==4,"four tanks spawn");
    std::set<int> locations;
    for (const auto& t : s.tanks) {
        require(locations.insert(static_cast<int>(t.position.x)).second,"starting seats do not overlap");
        require(t.team==0 ? t.position.x<game.config().worldWidth/2 : t.position.x>game.config().worldWidth/2,"teams spawn together on opposite sides");
    }
    require(!game.setMode(0,2) && !game.setSeat(0,0,7),"roster locks during battle");
    const auto before=s.tanks;
    for (int i=0;i<120*9;++i) game.step();
    s=game.snapshot(); bool moved=false;
    for (const auto& t : s.tanks) if (t.id!=0 && t.id!=*id) moved|=std::abs(t.position.x-tw::findTank(tw::Snapshot{.tanks=before},t.id)->position.x)>10;
    require(moved,"team bots actively reposition");
    game.removePlayer(*id);
    for (auto& t : const_cast<std::vector<tw::Tank>&>(game.world().tanks())) t.hp=0;
    for (int i=0;i<120*25 && game.snapshot().phase==tw::Phase::Playing;++i) game.step();
    require(game.snapshot().phase==tw::Phase::Victory,"team fight resolves after pending projectiles");
    game.rematch(); require(game.snapshot().players.size()==4,"departed players are replaced by bots on rematch");
}
void botCombat() {
    for (std::uint8_t map=0;map<4;++map) for (auto kind : tw::selectableWeapons) {
        auto c=tw::MapFactory::preset({},map); c.titanMode=true; c.asymmetricLoadouts=false;
        tw::GameSession game(c); game.addPlayer("TARGET"); game.addPlayer("BOT",true);
        game.setWeapon(0,tw::WeaponKind::Mortar); game.setWeapon(1,kind); game.setReady(0,true); game.setReady(1,true); game.step();
        unsigned shots=0; std::uint32_t last=0;
        const float x=game.snapshot().tanks[1].position.x;
        for (int i=0;i<120*30;++i) {
            // Keep the encounter alive to exercise every weapon's repeated decisions.
            for (auto& t : const_cast<std::vector<tw::Tank>&>(game.world().tanks())) { t.shieldRemaining=10; t.hp=1000; }
            game.setInput(0,{std::sin(i/120.f*1.1f)>0 ? 0.7f : -0.7f,-0.6f,0,false}); game.step();
            for (const auto& e : game.snapshot().events) if (e.id>last) { if (e.kind==tw::EventKind::Shot && e.player==1) ++shots; last=e.id; }
        }
        require(shots>0 && std::abs(game.snapshot().tanks[1].position.x-x)>20,"each bot weapon moves and finds shots against a moving target on every terrain");
    }
}
void duelSeats() {
    tw::Config c; c.loadoutSelection=true; tw::GameSession game(c); game.addPlayer("HOST");
    game.setMode(0,0); game.setWeapon(0,tw::WeaponKind::Mortar);
    require(game.setSeat(0,0,1),"duel pilots can swap sides"); game.setReady(0,true);
    require(game.setMap(0,0),"swapped duel can start"); game.step();
    const auto s=game.snapshot(); require(tw::findTank(s,0)->position.x>c.worldWidth/2,"duel starting position follows its lobby seat");
}
void timer() {
    tw::Config c; c.asymmetricLoadouts=false; tw::GameSession game(c);
    game.addPlayer("HOST"); game.addPlayer("GUEST"); game.setReady(0,true); game.setReady(1,true); game.step();
    for (int i=0;i<120*179;++i) game.step();
    require(game.snapshot().phase==tw::Phase::Playing,"match continues before deadline");
    for (int i=0;i<122;++i) game.step();
    const auto s=game.snapshot(); require(s.phase==tw::Phase::Victory && s.winner==tw::noPlayer && near(s.elapsed,180,0.02f),"exact three-minute deadline declares draw");
}
void missileLock() {
    tw::Config c; c.hillHeight=0; c.titanMode=true; tw::World world(c); world.reset(pilots(tw::WeaponKind::Missile));
    auto& owner=const_cast<tw::Tank&>(world.tanks()[0]); owner.position={800,300}; owner.aim=-tw::pi/2; owner.canFly=true;
    std::array<tw::Input,tw::maxPlayers> input{}; input[0]={0,-tw::pi/2,0,true,1};
    world.step(input); owner.hp=0; input[0].fire=false;
    for (int i=0;i<120*19;++i) world.step(input);
    owner.hp=1000; owner.reload=0; input[0].fire=true; world.step(input);
    auto s=world.snapshot(); require(s.projectiles.size()==1 && s.projectiles[0].age>18,"only one missile can remain active past normal reload");
    require(std::ranges::count(s.events,tw::EventKind::Shot,&tw::GameEvent::kind)==1,"holding fire cannot launch another missile");
    input[0].fire=false; owner.hp=0;
    for (int i=0;i<125;++i) world.step(input);
    s=world.snapshot(); require(s.projectiles.empty() && std::ranges::count(s.events,tw::EventKind::MissileImpact,&tw::GameEvent::kind)==1,"twenty-second timeout creates an air explosion");
    owner.hp=1000; input[0].fire=true; world.step(input); require(world.snapshot().projectiles.size()==1,"launch unlocks after previous explosion");
}
void aircraft() {
    for (std::uint8_t mapId=0;mapId<4;++mapId) {
        auto c=tw::MapFactory::preset({},mapId); c.titanMode=true;
        tw::World world(c,123); world.reset(pilots(tw::WeaponKind::Missile)); std::array<tw::Input,tw::maxPlayers> input{};
        for (int i=0;i<120*32;++i) world.step(input);
        auto s=world.snapshot(); require(s.plane.active && s.plane.direction==1 && s.plane.hp==200,"first plane has 200 HP and flies right");
        require(s.plane.position.y/c.worldHeight>=0.04f && s.plane.position.y/c.worldHeight<=0.071f,"plane flies just below the top edge on every map");
        for (float x=0;x<=c.worldWidth;x+=4) require(s.plane.position.y+96<=world.map().height(x)+0.01f,"flight path clears every mountain with a wide margin");
        if (mapId==0) {
            auto& owner=const_cast<tw::Tank&>(world.tanks()[0]);
            for (int hit=0;hit<2;++hit) {
                s=world.snapshot(); owner.position={s.plane.position.x,s.plane.position.y+100}; owner.canFly=true; owner.aim=-tw::pi/2; owner.reload=0;
                input[0]={0,-tw::pi/2,0,true,0}; world.step(input); input[0].fire=false;
                for (int i=0;i<120 && !world.snapshot().projectiles.empty();++i) world.step(input);
                s=world.snapshot();
                require(hit==0 ? near(s.plane.hp,80) && !s.plane.crashing : s.plane.crashing,"two direct 120-damage missiles bring down aircraft");
            }
            owner.hp=0;
            auto& victim=const_cast<tw::Tank&>(world.tanks()[1]); victim.canFly=false; victim.shieldRemaining=0;
            bool crashed=false;
            for (int i=0;i<120*10 && !crashed;++i) {
                s=world.snapshot(); victim.position={std::clamp(s.plane.position.x,35.f,c.worldWidth-35.f),world.map().height(s.plane.position.x)-12};
                world.step(input); crashed=std::ranges::any_of(world.snapshot().events,[](const auto& e){return e.kind==tw::EventKind::PlaneCrash;});
            }
            require(crashed && !world.snapshot().plane.active && victim.hp<=810 && victim.hp>=799,"aircraft crashes into terrain with up to 200 area damage");
        }
        while (world.snapshot().elapsed<62) world.step(input);
        require(world.snapshot().plane.active && world.snapshot().plane.direction==-1,"next plane flies left");
    }
}
void erosionAndDescent() {
    tw::Config c; c.hillHeight=0;
    auto map=tw::MapFactory::create(c); const float before=map->height(800);
    map->erodeColumn(800,115,2.4f);
    require(near(map->height(750),before+2.4f) && near(map->height(850),before+2.4f),"sky beam lowers the whole core evenly");
    tw::World world(c); world.reset(pilots(tw::WeaponKind::Mortar));
    auto& tank=const_cast<tw::Tank&>(world.tanks()[0]); tank.position.y-=120; tank.canFly=false;
    const float start=tank.position.y; std::array<tw::Input,tw::maxPlayers> input{}; world.step(input);
    require(tank.position.y>start && tank.position.y<start+1,"ground tanks fall gradually rather than snapping down");
    for (int i=0;i<150;++i) world.step(input);
    require(near(tank.position.y,world.map().height(tank.position.x)-12,0.01f),"vertical descent lands exactly on the terrain");
}
}
int main() {
    try { teams(); duelSeats(); botCombat(); timer(); missileLock(); aircraft(); erosionAndDescent(); std::cout<<"Nightfall checks passed: teams, bots, timer, missile lock, planes, erosion and descent\n"; return 0; }
    catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
