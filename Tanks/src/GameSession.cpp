#include "tankwars/GameSession.hpp"
#include <set>
#include "tankwars/Weapon.hpp"

namespace tw {
GameSession::GameSession(Config config) : config_(config), world_(config) {}
std::optional<PlayerId> GameSession::addPlayer(std::string name, bool bot) {
    if (phase_ != Phase::Lobby || players_.size() >= config_.playerLimit) return std::nullopt;
    PlayerId id = 0;
    while (std::ranges::any_of(players_, [&](const auto& p) { return p.id == id; })) ++id;
    std::uint8_t color = 0;
    while (std::ranges::any_of(players_, [&](const auto& p) { return p.color == color; })) ++color;
    players_.push_back({id, color, static_cast<std::uint8_t>(config_.teams ? id % 2 : id), true, false, bot, name.substr(0, 18)});
    return id;
}
void GameSession::removePlayer(PlayerId id) {
    inputs_.at(id) = {};
    if (phase_ == Phase::Lobby) std::erase_if(players_, [&](const auto& p) { return p.id == id; });
    else {
        for (auto& p : players_) if (p.id == id) { p.connected = false; p.ready = false; }
        world_.disconnect(id);
    }
}
bool GameSession::setColor(PlayerId id, std::uint8_t color) {
    if (phase_ != Phase::Lobby || color >= 6) return false;
    if (std::ranges::any_of(players_, [&](const auto& p) { return p.id != id && p.color == color; })) return false;
    for (auto& p : players_) if (p.id == id) { p.color = color; p.ready = false; mapSelected_=false; return true; }
    return false;
}
bool GameSession::setWeapon(PlayerId id, WeaponKind weapon) {
    if (phase_!=Phase::Lobby || std::ranges::find(selectableWeapons,weapon)==selectableWeapons.end()) return false;
    for (auto& p : players_) if (p.id==id && p.connected) { p.weapon=weapon; p.weaponChosen=true; p.ready=false; mapSelected_=false; return true; }
    return false;
}
bool GameSession::setMap(PlayerId id, std::uint8_t map) {
    if (id!=0 || phase_!=Phase::Lobby || map>3 || players_.size()<2 ||
        !std::ranges::all_of(players_,[](const auto& p) { return p.connected && p.ready && p.weaponChosen; })) return false;
    config_=MapFactory::preset(config_,map); config_.validate(); world_.selectMap(config_); mapSelected_=true;
    return true;
}
void GameSession::setReady(PlayerId id, bool ready) {
    if (phase_ != Phase::Lobby) return;
    for (auto& p : players_) if (p.id == id && p.connected && (!config_.loadoutSelection || p.weaponChosen)) { p.ready = ready; if (!ready) mapSelected_=false; }
}
void GameSession::setInput(PlayerId id, Input input) { if (id < maxPlayers) inputs_[id] = input; }
Input GameSession::botInput(const Tank& tank) const {
    const Tank* target = nullptr;
    for (const auto& other : world_.tanks()) if (other.alive() && other.team != tank.team) {
        if (!target || length(other.position - tank.position) < length(target->position - tank.position)) target = &other;
    }
    if (!target) return {0, tank.aim, 0, false};
    if (tank.weapon == WeaponKind::Laser) {
        const auto delta = target->pivot() - tank.pivot();
        const float aim = std::atan2(delta.y,delta.x);
        return {0,aim,0,std::abs(angleDelta(tank.aim,aim)) < 0.02f};
    }
    const auto delta = target->position - tank.pivot();
    const float x = std::abs(delta.x), speed = tank.power;
    if (tank.weapon == WeaponKind::GrowingOrb) {
        const float aim=std::atan2(delta.y,delta.x);
        return {0,aim,0,std::abs(angleDelta(tank.aim,aim))<0.04f};
    }
    const float gravity=config_.gravity*(tank.weapon==WeaponKind::Mortar ? config_.weapon.mortarGravityScale : (tank.weapon==WeaponKind::Flare ? config_.weapon.flareGravityScale : 1.f));
    const float launchSpeed=speed*(tank.weapon==WeaponKind::Mortar ? config_.weapon.mortarSpeedScale : (tank.weapon==WeaponKind::Ricochet ? config_.weapon.ricochetSpeedScale : (tank.weapon==WeaponKind::Flare ? config_.weapon.flareSpeedScale : 1.f)));
    const float disc = std::pow(launchSpeed,4) - gravity * (gravity * x * x - 2 * delta.y * launchSpeed * launchSpeed);
    if (disc <= 0 || x < 1) return {0, tank.aim, 1, false};
    float angle = -std::atan((launchSpeed * launchSpeed + (tank.weapon==WeaponKind::Mortar ? 1.f : -1.f)*std::sqrt(disc)) / (gravity * x));
    if (delta.x < 0) angle = -pi - angle;
    const float time = world_.snapshot().elapsed;
    angle += std::sin(time * 0.37f + tank.id) * 0.035f;
    return {0, angle, 0, time > 2.f && std::abs(angleDelta(tank.aim, angle)) < 0.06f};
}
void GameSession::step() {
    if (phase_ == Phase::Lobby) {
        if ((!config_.loadoutSelection || mapSelected_) && players_.size() >= 2 && std::ranges::all_of(players_, [](const auto& p) { return p.connected && p.ready; })) {
            std::set<unsigned> teams;
            for (const auto& p : players_) teams.insert(p.team);
            if (teams.size() < 2) return;
            world_.reset(players_); phase_ = Phase::Playing; winner_ = noPlayer;
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
    if (aliveTeams.size() <= 1 && state.projectiles.empty() && state.fires.empty() && state.skyStrikes.empty() && !burningSurvivor) {
        phase_ = Phase::Victory;
        for (const auto& t : world_.tanks()) if (t.alive()) { winner_ = t.id; break; }
    }
}
void GameSession::rematch() {
    if (phase_ != Phase::Victory) return;
    phase_ = Phase::Lobby; mapSelected_=false; winner_ = noPlayer; inputs_ = {};
    std::erase_if(players_, [](const auto& p) { return !p.connected; });
    for (auto& p : players_) p.ready = p.bot;
}
Snapshot GameSession::snapshot() const {
    auto state = world_.snapshot();
    state.mapSelected=mapSelected_; state.mapId=config_.mapId;
    state.players = players_; state.phase = phase_; state.winner = winner_;
    return state;
}
}
