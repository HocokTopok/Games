#include "tankwars/World.hpp"
#include "tankwars/Weapon.hpp"
#include <algorithm>

namespace tw {
World::World(Config config) : config_(config), map_(MapFactory::create(config)) { config_.validate(); }
void World::reset(const std::vector<Player>& players) {
    tanks_.clear(); projectiles_.clear(); events_.clear(); tick_ = 0; ++round_;
    constexpr std::array<float, 4> fractions{0, 1, 0.24f, 0.76f};
    std::size_t index = 0;
    for (const auto& p : players) {
        if (!p.connected) continue;
        const float x = config_.spawnInset + fractions.at(index++) * (config_.worldWidth - 2 * config_.spawnInset);
        tanks_.push_back({p.id, p.color, p.team,
            {x, map_->height(x) - config_.tankHalfHeight}, map_->slope(x),
            x < config_.worldWidth / 2 ? -0.65f : -pi + 0.65f, config_.weapon.initialPower});
    }
}
void World::emit(EventKind kind, Vec2 position, PlayerId player) {
    events_.push_back({nextEvent_++, kind, position, player});
    if (events_.size() > 128) events_.erase(events_.begin(), events_.begin() + 32);
}
void World::disconnect(PlayerId id) {
    for (auto& tank : tanks_) if (tank.id == id && tank.alive()) {
        tank.hp = 0; emit(EventKind::Destroyed, tank.position, tank.id);
    }
}
void World::explode(const Hit& hit) {
    emit(EventKind::Impact, hit.position, hit.tank);
    for (auto& tank : tanks_) {
        if (!tank.alive()) continue;
        float damage = Physics::splashDamage(hit.position, tank, config_.weapon);
        if (tank.id == hit.tank) damage += config_.weapon.directDamage;
        tank.hp = std::max(0.f, tank.hp - damage);
        if (!tank.alive()) emit(EventKind::Destroyed, tank.position, tank.id);
    }
}
void World::step(const std::array<Input, maxPlayers>& inputs) {
    const float dt = config_.fixedStep;
    ++tick_;
    for (auto& tank : tanks_) {
        if (!tank.alive()) continue;
        auto in = inputs[tank.id];
        if (!std::isfinite(in.move) || !std::isfinite(in.powerDelta) || !std::isfinite(in.aim)) in = {};
        const float movement = std::clamp(in.move, -1.f, 1.f) * config_.tankSpeed * dt;
        float x = std::clamp(tank.position.x + movement, config_.tankHalfWidth + 8, config_.worldWidth - config_.tankHalfWidth - 8);
        for (const auto& other : tanks_) {
            if (other.id == tank.id || !other.alive()) continue;
            if (std::abs(x - other.position.x) < config_.tankHalfWidth * 2 + 2)
                x = tank.position.x;
        }
        tank.track += (x - tank.position.x) / 8.f;
        tank.position = {x, map_->height(x) - config_.tankHalfHeight};
        tank.bodyAngle = map_->slope(x);
        tank.aim = approachAngle(tank.aim, std::remainder(in.aim, 2 * pi), config_.turretSpeed * dt);
        tank.power = std::clamp(tank.power + std::clamp(in.powerDelta, -1.f, 1.f) * config_.powerSpeed * dt,
                                config_.weapon.minPower, config_.weapon.maxPower);
        tank.reload = std::max(0.f, tank.reload - dt);
        tank.recoil = std::max(0.f, tank.recoil - dt * 3.5f);
        if (in.fire && tank.reload <= 0 && projectiles_.size() < 128) {
            projectiles_.push_back(Cannon::fire(tank, config_.weapon, nextProjectile_++));
            tank.reload = config_.weapon.reloadSeconds; tank.recoil = 1;
            emit(EventKind::Shot, projectiles_.back().position, tank.id);
        }
    }
    std::erase_if(projectiles_, [&](Projectile& p) {
        const auto from = p.position;
        Physics::integrate(p, dt, config_.gravity);
        if (auto hit = Physics::sweep(from, p.position, config_.weapon.projectileRadius, *map_, tanks_,
                p.age < 0.12f ? p.owner : noPlayer, config_.tankHalfWidth, config_.tankHalfHeight)) {
            explode(*hit); return true;
        }
        return p.age > config_.weapon.projectileLifetime || p.position.x < -100 ||
               p.position.x > config_.worldWidth + 100 || p.position.y > config_.worldHeight + 100;
    });
}
Snapshot World::snapshot() const {
    Snapshot s;
    s.tick = tick_; s.round = round_; s.elapsed = static_cast<float>(tick_) * config_.fixedStep;
    s.tanks = tanks_; s.projectiles = projectiles_; s.events = events_;
    return s;
}
}
