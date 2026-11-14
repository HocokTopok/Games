#include "tankwars/GameSession.hpp"
#include <set>
#include "tankwars/Weapon.hpp"

namespace tw {
GameSession::GameSession(Config config) : config_(config), world_(config) { config_.validate(); }
std::optional<PlayerId> GameSession::addPlayer(std::string name, bool bot) {
    if (phase_ != Phase::Lobby) return std::nullopt;
    std::optional<std::uint8_t> replaceSeat;
    if (!bot && players_.size()>=config_.playerLimit) {
        auto vacant=std::ranges::find_if(players_,[](const auto& p){return p.bot;});
        if (vacant==players_.end()) return std::nullopt;
        replaceSeat=vacant->seat; players_.erase(vacant);
    }
    if (players_.size()>=config_.playerLimit) return std::nullopt;
    PlayerId id = 0;
    while (std::ranges::any_of(players_, [&](const auto& p) { return p.id == id; })) ++id;
    std::uint8_t color = 0;
    while (std::ranges::any_of(players_, [&](const auto& p) { return p.color == color; })) ++color;
    std::uint8_t seat=replaceSeat.value_or(0);
    while (std::ranges::any_of(players_,[&](const auto& p){return p.seat==seat;})) ++seat;
    players_.push_back({id,color,static_cast<std::uint8_t>(config_.teams ? seat/(config_.playerLimit/2) : seat),true,false,bot,name.substr(0,18)});
    players_.back().seat=seat;
    if (bot) {
        auto& pilot=players_.back();
        pilot.weapon=selectableWeapons[(id+1)%selectableWeapons.size()];
        pilot.weaponChosen=pilot.ready=pilot.weaponConfirmed=true;
        pilot.gadgetChosen=config_.loadoutSelection;
        pilot.gadget=std::uniform_int_distribution<int>(0,1)(botRandom_) ? GadgetKind::Armor : GadgetKind::Specialist;
    }
    if (!bot && config_.teams) { invalidateReady(); fillBots(); }
    return id;
}
void GameSession::removePlayer(PlayerId id) {
    inputs_.at(id) = {};
    if (phase_ == Phase::Lobby) { std::erase_if(players_, [&](const auto& p) { return p.id == id; }); invalidateReady(); if (id!=0) fillBots(); }
    else {
        for (auto& p : players_) if (p.id == id) { p.connected = false; p.ready = false; }
        world_.disconnect(id);
    }
}
bool GameSession::setColor(PlayerId id, std::uint8_t color) {
    if (phase_ != Phase::Lobby || color >= maxPlayers) return false;
    if (std::ranges::any_of(players_, [&](const auto& p) { return p.id != id && p.color == color; })) return false;
    for (auto& p : players_) if (p.id == id) { p.color = color; p.ready = false; return true; }
    return false;
}
bool GameSession::setWeapon(PlayerId id, WeaponKind weapon) {
    if (phase_!=Phase::Lobby || std::ranges::find(selectableWeapons,weapon)==selectableWeapons.end()) return false;
    for (auto& p : players_) if (p.id==id && p.connected) {
        if (config_.loadoutSelection && !mapSelected_ && !p.bot) return false;
        p.weapon=weapon; p.weaponChosen=true; p.ready=false; p.weaponConfirmed=false; p.gadgetChosen=false; return true;
    }
    return false;
}
bool GameSession::setMap(PlayerId id, std::uint8_t map) {
    if (id!=0 || phase_!=Phase::Lobby) return false;
    if (map==255) { mapSelected_=false; invalidateReady(); return true; }
    if (map>3) return false;
    config_=MapFactory::preset(config_,map); config_.validate(); world_.selectMap(config_); mapSelected_=true;
    invalidateReady();
    return true;
}
bool GameSession::confirmWeapon(PlayerId id, bool confirmed) {
    if (phase_!=Phase::Lobby || (config_.loadoutSelection && !mapSelected_)) return false;
    for (auto& p : players_) if (p.id==id && p.connected && p.weaponChosen) {
        p.weaponConfirmed=confirmed; p.ready=false; return true;
    }
    return false;
}
bool GameSession::setGadget(PlayerId id, GadgetKind gadget) {
    if (phase_!=Phase::Lobby || static_cast<unsigned>(gadget)>2) return false;
    for (auto& p : players_) if (p.id==id && p.connected && p.weaponConfirmed) {
        if (p.bot && gadget==GadgetKind::Jetpack) return false;
        p.gadget=gadget; p.gadgetChosen=true; p.ready=false; return true;
    }
    return false;
}
void GameSession::setReady(PlayerId id, bool ready) {
    if (phase_ != Phase::Lobby) return;
    for (auto& p : players_) if (p.id == id && p.connected && (!config_.loadoutSelection || (mapSelected_ && p.weaponConfirmed && p.gadgetChosen))) { p.ready = ready; }
}
void GameSession::invalidateReady() {
    for (auto& p : players_) { p.ready=p.bot; p.weaponConfirmed=p.bot; }
}
void GameSession::fillBots() {
    if (!config_.teams || players_.empty()) return;
    while (players_.size()<config_.playerLimit) addPlayer("SENTINEL "+std::to_string(players_.size()+1),true);
}
bool GameSession::setMode(PlayerId actor,unsigned teamSize) {
    if (actor!=0 || phase_!=Phase::Lobby || (teamSize!=0 && teamSize!=2)) return false;
    const unsigned limit=teamSize ? teamSize*2 : 2;
    if (std::ranges::count_if(players_,[](const auto& p){return !p.bot;})>limit) return false;
    std::erase_if(players_,[](const auto& p){return p.bot;});
    config_.teams=teamSize!=0; config_.playerLimit=limit;
    for (std::size_t i=0;i<players_.size();++i) {
        players_[i].seat=static_cast<std::uint8_t>(teamSize ? (i%2)*teamSize+i/2 : i);
        players_[i].team=static_cast<std::uint8_t>(teamSize ? i%2 : i);
    }
    invalidateReady(); fillBots(); world_.selectMap(config_);
    // Solo duels also retain a bot opponent.
    if (!teamSize && players_.size()==1) addPlayer("SENTINEL",true);
    return true;
}
bool GameSession::setSeat(PlayerId actor,PlayerId target,std::uint8_t seat) {
    if (phase_!=Phase::Lobby || (actor!=0 && actor!=target) || seat>=config_.playerLimit) return false;
    auto moving=std::ranges::find(players_,target,&Player::id);
    if (moving==players_.end()) return false;
    if (moving->seat==seat) return true;
    auto other=std::ranges::find(players_,seat,&Player::seat);
    if (other!=players_.end()) { other->seat=moving->seat; other->team=config_.teams ? other->seat/(config_.playerLimit/2) : other->seat; }
    moving->seat=seat; moving->team=config_.teams ? seat/(config_.playerLimit/2) : seat;
    invalidateReady(); return true;
}
bool GameSession::setBotWeapon(PlayerId actor,PlayerId target,WeaponKind weapon) {
    if (actor!=0 || phase_!=Phase::Lobby) return false;
    const auto p=std::ranges::find(players_,target,&Player::id);
    if (p==players_.end() || !p->bot || !setWeapon(target,weapon)) return false;
    p->weaponConfirmed=true; p->gadgetChosen=true; setReady(target,true); return true;
}
bool GameSession::setBotGadget(PlayerId actor,PlayerId target,GadgetKind gadget) {
    if (actor!=0 || phase_!=Phase::Lobby || gadget==GadgetKind::Jetpack) return false;
    const auto p=std::ranges::find(players_,target,&Player::id);
    if (p==players_.end() || !p->bot || !setGadget(target,gadget)) return false;
    setReady(target,true); return true;
}
void GameSession::setInput(PlayerId id, Input input) { if (id < maxPlayers) inputs_[id] = input; }
Input GameSession::botInput(const Tank& tank) {
    const auto state=world_.snapshot(); const float time=state.elapsed;
    // Gadget decisions run every simulation step, including between aim updates.
    const auto useGadget=[&](Input in) {
        in.gadget=false;
        if (!tank.hasGadget || tank.gadget!=GadgetKind::Specialist || tank.gadgetHeld) return in;
        if (tank.weapon==WeaponKind::Missile) {
            for (const auto& missile : state.projectiles) if (missile.owner==tank.id && missile.weapon==WeaponKind::Missile) {
                const Vec2 heading=missile.velocity/std::max(1.f,length(missile.velocity));
                for (const auto& enemy : state.tanks) if (enemy.alive() && enemy.team!=tank.team) {
                    const Vec2 delta=enemy.position-missile.position;
                    const float forward=dot(delta,heading), lateral=std::abs(delta.x*heading.y-delta.y*heading.x);
                    if (forward<25 || forward>240 || lateral>forward*0.22f+config_.tankHalfHeight) continue;
                    const auto hit=Physics::sweep(missile.position,enemy.position,4.5f,world_.map(),world_.tanks(),tank.id,config_.tankHalfWidth,config_.tankHalfHeight);
                    if (!hit || hit->tank!=enemy.id) continue;
                    bool allyAhead=false;
                    for (const auto& ally : state.tanks) if (ally.alive() && ally.team==tank.team) {
                        const Vec2 a=ally.position-missile.position; const float along=dot(a,heading);
                        allyAhead|=along>0 && along<forward && std::abs(a.x*heading.y-a.y*heading.x)<along*0.27f+config_.tankHalfWidth;
                    }
                    if (!allyAhead) in.gadget=true;
                }
            }
        } else if (in.fire) {
            if (tank.weapon==WeaponKind::Flare) in.gadget=!tank.specialistUsed;
            else if (tank.weapon==WeaponKind::Napalm || tank.weapon==WeaponKind::Ricochet)
                in.gadget=tank.gadgetCooldown<=0 && tank.gadgetRemaining<=0;
        }
        return in;
    };
    auto& plan=botPlans_[tank.id];
    if (time<plan.nextThink) return useGadget(plan.input);
    plan.nextThink=time+0.20f+0.017f*tank.id;
    std::vector<const Tank*> enemies;
    for (const auto& other : world_.tanks()) if (other.alive() && other.team!=tank.team) enemies.push_back(&other);
    if (enemies.empty()) return plan.input={0,tank.aim,0,false};
    // Different allies cover different opponents and rotate assignments during a fight.
    const auto* target=enemies[(tank.id+static_cast<unsigned>(time/5.5f))%enemies.size()];
    const float dx=target->position.x-tank.position.x, distance=std::abs(dx), toward=dx>=0 ? 1.f : -1.f;
    const float weave=std::sin(time*1.73f+tank.id*2.8f)+0.55f*std::sin(time*3.13f+tank.id);
    float desired=420, move=weave>0.35f ? 0.8f : (weave< -0.25f ? -0.8f : 0.f);
    switch (tank.weapon) {
    case WeaponKind::Mortar: desired=460; move*=0.75f; break;
    case WeaponKind::Ricochet: desired=680; move=weave>0 ? 1.f : -1.f; break;
    case WeaponKind::Missile: desired=350; break;
    case WeaponKind::Flare: desired=tank.reload>5 ? 580.f : 220.f; break;
    case WeaponKind::Napalm: desired=std::sin(time*0.37f+tank.id)>0.3f ? 80.f : 260.f; move=weave>0 ? 1.f : -1.f; break;
    default: break;
    }
    if (distance<desired*0.70f) move=-toward;
    else if (distance>desired*1.15f) move=toward;
    if (tank.position.x<50 && move<0) move=1;
    if (tank.position.x>config_.worldWidth-50 && move>0) move=-1;
    const float spread=std::sin(time*2.7f+tank.id*3.1f)*22+std::sin(time*0.91f+tank.id)*12;
    const Vec2 velocity{std::clamp(target->velocity.x,-config_.tankSpeed,config_.tankSpeed),std::clamp(target->velocity.y,-300.f,300.f)};
    if (tank.weapon==WeaponKind::Laser || tank.weapon==WeaponKind::Missile || tank.weapon==WeaponKind::GrowingOrb) {
        const float flight=tank.weapon==WeaponKind::Laser ? 0.f : std::min(2.5f,length(target->pivot()-tank.pivot())/std::max(150.f,tank.power*config_.weapon.missileSpeedScale));
        const Vec2 point=target->pivot()+velocity*flight+Vec2{spread*0.45f,0};
        const Vec2 delta=point-tank.pivot(); const float aim=std::atan2(delta.y,delta.x);
        Tank probe=tank; probe.aim=aim;
        const auto trace=Physics::guidanceTrace(probe,config_,world_.map(),world_.tanks());
        const bool clear=(trace.hit && trace.hit->tank==target->id) || length(trace.to-point)<45;
        if (!clear) move=toward;
        const float power=tank.weapon==WeaponKind::Missile ? std::sin(time*0.8f+tank.id)*0.6f : 0;
        return useGadget(plan.input={move,aim,power,time>1 && clear && std::abs(angleDelta(tank.aim,aim))<0.10f});
    }
    const float gravity=config_.gravity*(tank.weapon==WeaponKind::Mortar ? config_.weapon.mortarGravityScale : (tank.weapon==WeaponKind::Flare ? config_.weapon.flareGravityScale : 1.f));
    const float scale=tank.weapon==WeaponKind::Mortar ? config_.weapon.mortarSpeedScale : (tank.weapon==WeaponKind::Ricochet ? config_.weapon.ricochetSpeedScale : (tank.weapon==WeaponKind::Flare ? config_.weapon.flareSpeedScale : 1.f));
    float best=1e9f, bestAim=tank.aim, bestPower=tank.power;
    const auto assess=[&](float aim,float power,bool ceilingOnly=false) {
        Tank probe=tank; probe.aim=Cannon::firingAngle(tank.weapon,aim); probe.power=power;
        auto shot=Cannon::fire(probe,config_.weapon,0);
        float closest=1e9f; bool ceilingBounce=false;
        // Replay uses the same swept terrain collisions and ricochets as real shots.
        for (int i=0;i<360;++i) {
            const auto result=Physics::advance(shot,1.f/30.f,config_,world_.map(),{});
            for (unsigned bounce=0;bounce<result.ricochetCount;++bounce)
                ceilingBounce|=result.ricochets[bounce].y<=Cannon::radius(shot,config_.weapon)+0.5f;
            const float lead=std::min(shot.age,3.f);
            Vec2 expected=target->position+velocity*lead+Vec2{spread,0};
            expected.x=std::clamp(expected.x,35.f,config_.worldWidth-35.f);
            if (!target->canFly) expected.y=world_.map().height(expected.x)-config_.tankHalfHeight;
            if (!ceilingOnly || ceilingBounce) closest=std::min(closest,length(shot.position-expected));
            if (result.hit || result.expired) break;
        }
        const float score=closest+std::abs(power-tank.power)*0.018f;
        if (score<best) { best=score; bestAim=probe.aim; bestPower=power; }
    };
    for (float power : {tank.power,std::clamp(tank.power-230.f,config_.weapon.minPower,config_.weapon.maxPower),std::clamp(tank.power+230.f,config_.weapon.minPower,config_.weapon.maxPower),config_.weapon.maxPower}) {
        const float speed=power*scale;
        for (int arc=0;arc<2;++arc) {
            Vec2 point=target->position+Vec2{spread,0}; float angle=tank.aim;
            bool reachable=true;
            for (int iteration=0;iteration<3;++iteration) {
                const auto delta=point-tank.pivot(); const float x=std::max(1.f,std::abs(delta.x));
                const float disc=std::pow(speed,4)-gravity*(gravity*x*x-2*delta.y*speed*speed);
                if (disc<0) { reachable=false; break; }
                angle=-std::atan((speed*speed+(arc ? 1.f : -1.f)*std::sqrt(disc))/(gravity*x));
                if (delta.x<0) angle=-pi-angle;
                const float flight=x/std::max(20.f,std::abs(speed*std::cos(angle)));
                point=target->position+velocity*std::min(flight,3.f)+Vec2{spread,0};
                if (!target->canFly) point.y=world_.map().height(std::clamp(point.x,0.f,config_.worldWidth))-config_.tankHalfHeight;
            }
            if (reachable) assess(angle,power);
        }
    }
    if (tank.weapon==WeaponKind::Ricochet && static_cast<int>(time/3+tank.id)%3==0) {
        const float directScore=best, directAim=bestAim, directPower=bestPower;
        best=1e9f;
        for (float power : {tank.power,config_.weapon.maxPower*0.8f,config_.weapon.maxPower})
            for (float angle : {-0.85f,-0.97f,-1.09f,-1.21f,-1.33f,-1.45f})
                assess(toward>0 ? angle : -pi-angle,power,true);
        if (best>65) { best=directScore; bestAim=directAim; bestPower=directPower; }
    }
    const float powerDelta=std::clamp((bestPower-tank.power)/80.f,-1.f,1.f);
    const float tolerance=tank.weapon==WeaponKind::Mortar ? 85.f : (tank.weapon==WeaponKind::Flare ? 65.f : 65.f);
    const bool fire=time>1 && best<tolerance && std::abs(bestPower-tank.power)<65 && std::abs(angleDelta(tank.aim,bestAim))<0.07f;
    return useGadget(plan.input={move,bestAim,powerDelta,fire});
}
void GameSession::step() {
    if (phase_ == Phase::Lobby) {
        if ((!config_.loadoutSelection || mapSelected_) && players_.size() >= 2 && std::ranges::all_of(players_, [](const auto& p) { return p.connected && p.ready; })) {
            std::set<unsigned> teams;
            for (const auto& p : players_) teams.insert(p.team);
            if (teams.size() < 2) return;
            world_.reset(players_); botPlans_={}; phase_ = Phase::Playing; winner_ = noPlayer;
        }
        return;
    }
    if (phase_ != Phase::Playing) return;
    for (const auto& p : players_) if (p.bot) {
        for (const auto& t : world_.tanks()) if (t.id == p.id) inputs_[p.id] = botInput(t);
    }
    world_.step(inputs_);
    std::set<unsigned> aliveTeams;
    for (const auto& t : world_.tanks()) if (t.alive()) aliveTeams.insert(t.team);
    const auto state = world_.snapshot();
    const bool burningSurvivor = std::ranges::any_of(state.tanks, [](const auto& t) { return t.alive() && t.burnRemaining > 0; });
    if (aliveTeams.size() <= 1 && state.projectiles.empty() && state.bombs.empty() && state.fires.empty() && state.skyStrikes.empty() && !burningSurvivor && (!state.plane.active || (!state.plane.crashing && !state.plane.bombing))) {
        phase_ = Phase::Victory;
        for (const auto& t : world_.tanks()) if (t.alive()) { winner_ = t.id; break; }
    }
}
void GameSession::rematch() {
    if (phase_ != Phase::Victory) return;
    phase_ = Phase::Lobby; mapSelected_=false; winner_ = noPlayer; inputs_ = {};
    std::erase_if(players_, [](const auto& p) { return !p.connected; });
    for (auto& p : players_) { p.ready = p.bot; p.weaponConfirmed=p.bot; p.gadgetChosen=p.bot; }
    fillBots();
}
Snapshot GameSession::snapshot() const {
    auto state = world_.snapshot();
    state.mapSelected=mapSelected_; state.mapId=config_.mapId;
    state.players = players_; state.phase = phase_; state.winner = winner_;
    state.teamBattle=config_.teams; state.roomSize=static_cast<std::uint8_t>(config_.playerLimit); state.modernRules=true;
    return state;
}
}
