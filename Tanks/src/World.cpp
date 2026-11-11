#include "tankwars/World.hpp"
#include "tankwars/Weapon.hpp"
#include <algorithm>

namespace tw {
World::World(Config config) : config_(config), map_(MapFactory::create(config)) { config_.validate(); }
void World::reset(const std::vector<Player>& players) {
    tanks_.clear(); projectiles_.clear(); events_.clear(); lasers_.clear(); fires_.clear(); tick_ = 0; ++round_;
    constexpr std::array<float, 4> fractions{0, 1, 0.24f, 0.76f};
    std::size_t index = 0;
    for (const auto& p : players) {
        if (!p.connected) continue;
        const float x = config_.spawnInset + fractions.at(index++) * (config_.worldWidth - 2 * config_.spawnInset);
        tanks_.push_back({p.id, p.color, p.team,
            {x, map_->height(x) - config_.tankHalfHeight}, map_->slope(x),
            x < config_.worldWidth / 2 ? -0.65f : -pi + 0.65f, config_.weapon.initialPower});
        auto& tank = tanks_.back();
        tank.titan = config_.titanMode;
        tank.maxHp = tank.hp = tank.titan ? config_.titanHealth : 100.f;
        tank.weapon = config_.asymmetricLoadouts && p.id < 2 ? (p.id==0 ? WeaponKind::GrowingOrb : WeaponKind::Mortar) :
                      (config_.titanMode ? WeaponKind::Laser : WeaponKind::Cannon);
        tank.canFly = config_.asymmetricLoadouts && p.id == 1;
        if (tank.weapon == WeaponKind::Laser) tank.power = 4000;
    }
}
void World::emit(EventKind kind, Vec2 position, PlayerId player, float radius, float intensity) {
    events_.push_back({nextEvent_++, kind, position, player,radius,intensity});
    if (events_.size() > 128) events_.erase(events_.begin(), events_.begin() + 32);
}
void World::disconnect(PlayerId id) {
    for (auto& tank : tanks_) if (tank.id == id && tank.alive()) {
        tank.hp = 0; emit(EventKind::Destroyed, tank.position, tank.id);
    }
}
void World::explode(const Hit& hit, bool laser) {
    emit(EventKind::Impact, hit.position, hit.tank);
    for (auto& tank : tanks_) {
        if (!tank.alive()) continue;
        float damage = laser ? 0.f : Physics::splashDamage(hit.position, tank, config_.weapon);
        if (tank.id == hit.tank) damage += laser ? config_.weapon.laserHitDamage(tank.maxHp) : config_.weapon.directDamage;
        Physics::damage(tank,damage);
        if (!tank.alive()) emit(EventKind::Destroyed, tank.position, tank.id);
    }
}
void World::ignite(const Hit& hit, PlayerId owner) {
    emit(EventKind::NapalmImpact, hit.position, owner);
    const auto& w = config_.weapon;
    for (auto& tank : tanks_) {
        if (!tank.alive()) continue;
        const float proximity = std::clamp(1.f - std::max(0.f, length(hit.position - tank.position) - 17.f) / w.fireRadius, 0.f, 1.f);
        if (proximity <= 0 && tank.id != hit.tank) continue;
        Physics::damage(tank, w.napalmSplashDamage * proximity + (tank.id == hit.tank ? w.napalmDirectDamage : 0.f));
        tank.burnRemaining = w.burnDuration;
        if (!tank.alive()) emit(EventKind::Destroyed, tank.position, tank.id);
    }
    // A hit high in the air burns the tank, but only nearby terrain catches fire.
    const float ground = map_->height(hit.position.x);
    if (ground - hit.position.y > w.fireRadius) return;
    const Vec2 position{std::clamp(hit.position.x, 0.f, config_.worldWidth), ground - 3};
    for (auto& fire : fires_) {
        if (std::abs(fire.position.x - position.x) < w.fireRadius * 0.5f) {
            fire.remaining = w.fireDuration;
            fire.owner = owner;
            return;
        }
    }
    if (fires_.size() >= 64) {
        const auto oldest = std::ranges::min_element(fires_, {}, &FirePatch::remaining);
        fires_.erase(oldest);
    }
    fires_.push_back({nextProjectile_++, owner, position, w.fireDuration, w.fireRadius});
}
void World::detonate(const Hit& hit, const Projectile& p) {
    const bool mortar=p.weapon==WeaponKind::Mortar;
    const float radius=mortar ? config_.weapon.mortarBlastRadius : std::max(16.f,Cannon::radius(p,config_.weapon)*2.2f);
    const float amount=mortar ? config_.weapon.mortarDamage : Cannon::orbDamage(p.age,config_.weapon);
    emit(mortar ? EventKind::MortarImpact : EventKind::OrbImpact,hit.position,p.owner,radius,mortar ? 1.f : amount/100.f);
    for (auto& tank : tanks_) {
        if (!tank.alive()) continue;
        Physics::damage(tank,tank.id==hit.tank ? amount : Physics::areaDamage(hit.position,tank,radius,amount,config_));
        if (!tank.alive()) emit(EventKind::Destroyed,tank.position,tank.id);
    }
}
void World::updateFire(float dt) {
    for (auto& fire : fires_) fire.remaining -= dt;
    std::erase_if(fires_, [](const auto& fire) { return fire.remaining <= 0; });
    for (auto& tank : tanks_) {
        if (!tank.alive()) continue;
        const bool inFire = std::ranges::any_of(fires_, [&](const auto& fire) {
            const float dx = std::max(0.f, std::abs(tank.position.x - fire.position.x) - config_.tankHalfWidth);
            const float dy = std::max(0.f, std::abs(tank.position.y - fire.position.y) - config_.tankHalfHeight);
            return std::hypot(dx, dy) <= fire.radius;
        });
        if (inFire) tank.burnRemaining = config_.weapon.burnDuration;
        const float burnTime = std::min(dt, tank.burnRemaining);
        // Overlapping pools refresh the fire without multiplying its damage.
        const float damage = inFire ? config_.weapon.fireDamagePerSecond * dt : config_.weapon.burnDamagePerSecond * burnTime;
        Physics::damage(tank, damage);
        tank.burnRemaining = std::max(0.f, tank.burnRemaining - dt);
        if (!tank.alive()) emit(EventKind::Destroyed, tank.position, tank.id);
    }
}
void World::step(const std::array<Input, maxPlayers>& inputs) {
    const float dt = config_.fixedStep;
    ++tick_;
    for (auto& tank : tanks_) {
        if (!tank.alive()) continue;
        auto in = inputs[tank.id];
        if (!std::isfinite(in.move) || !std::isfinite(in.powerDelta) || !std::isfinite(in.aim) || !std::isfinite(in.lift)) in = {};
        const auto previousPosition = tank.position;
        const float movement = std::clamp(in.move, -1.f, 1.f) * config_.tankSpeed * dt;
        float x = std::clamp(tank.position.x + movement, config_.tankHalfWidth + 8, config_.worldWidth - config_.tankHalfWidth - 8);
        for (const auto& other : tanks_) {
            if (other.id == tank.id || !other.alive()) continue;
            if (std::abs(tank.position.y - other.position.y) >= config_.tankHalfHeight * 2 + 2) continue;
            if (std::abs(x - other.position.x) < config_.tankHalfWidth * 2 + 2)
                x = tank.position.x;
        }
        tank.track += (x - tank.position.x) / 8.f;
        const float floor = map_->height(x) - config_.tankHalfHeight;
        if (tank.canFly) {
            const float lift = std::clamp(in.lift, -1.f, 1.f);
            const bool grounded = tank.position.y >= map_->height(tank.position.x) - config_.tankHalfHeight - 0.5f && tank.verticalVelocity >= 0;
            if (grounded && lift > 0) tank.verticalVelocity = -config_.jumpSpeed;
            if (grounded && lift <= 0) {
                tank.position.y = floor;
                tank.verticalVelocity = 0;
            } else {
                const float acceleration = config_.gravity - std::max(0.f, lift) * (config_.gravity + config_.flightAcceleration)
                                           + std::max(0.f, -lift) * config_.flightAcceleration;
                tank.verticalVelocity = std::clamp(tank.verticalVelocity + acceleration * dt, -config_.flightSpeed, config_.descentSpeed);
                tank.position.y += tank.verticalVelocity * dt;
                // Keep the airborne hull below the fixed scoreboard at the top of the view.
                const float ceiling = std::min(config_.worldHeight * 0.23f, floor - 60.f);
                if (tank.position.y < ceiling) { tank.position.y = ceiling; tank.verticalVelocity = std::max(0.f, tank.verticalVelocity); }
                if (tank.position.y >= floor) { tank.position.y = floor; tank.verticalVelocity = 0; }
                for (auto& other : tanks_) {
                    if (other.id == tank.id || !other.alive() || std::abs(x - other.position.x) >= config_.tankHalfWidth * 2 + 2) continue;
                    const float top = other.position.y - config_.tankHalfHeight * 2 - 2;
                    if (previousPosition.y <= top && tank.position.y >= top) {
                        Physics::crush(other);
                        emit(EventKind::Destroyed,other.position,other.id);
                        emit(EventKind::Impact,{x,top+config_.tankHalfHeight},other.id);
                        tank.position.y = top;
                        tank.verticalVelocity = -config_.jumpSpeed*0.4f;
                    }
                }
            }
            tank.position.x = x;
            tank.bodyAngle = approachAngle(tank.bodyAngle, tank.position.y >= floor - 0.5f ? map_->slope(x) : 0.f, dt * 4);
        } else {
            tank.position = {x, floor};
            tank.bodyAngle = map_->slope(x);
        }
        tank.aim = approachAngle(tank.aim, Cannon::firingAngle(tank.weapon,std::remainder(in.aim, 2 * pi)), config_.turretSpeed * dt);
        if (tank.weapon != WeaponKind::Laser) tank.power = std::clamp(tank.power + std::clamp(in.powerDelta, -1.f, 1.f) * config_.powerSpeed * dt,
                                                config_.weapon.minPower, config_.weapon.maxPower);
        tank.reload = std::max(0.f, tank.reload - dt);
        tank.recoil = std::max(0.f, tank.recoil - dt * 3.5f);
        const std::size_t pellets = tank.weapon == WeaponKind::Napalm ? 3 : 1;
        if (in.fire && tank.reload <= 0 && (tank.weapon == WeaponKind::Laser || projectiles_.size() + pellets <= 128)) {
            if (tank.weapon == WeaponKind::Laser) {
                const auto laser = Physics::laserTrace(tank,config_,*map_,tanks_);
                emit(EventKind::Shot,laser.from,tank.id);
                lasers_.push_back({nextEvent_-1,tank.id,laser.from,laser.to});
                if (lasers_.size() > 64) lasers_.erase(lasers_.begin(),lasers_.begin()+16);
                if (laser.hit) explode(*laser.hit,true);
                tank.reload = config_.weapon.laserReload;
            } else if (tank.weapon == WeaponKind::Napalm) {
                for (std::size_t i = 0; i < pellets; ++i) {
                    auto projectile = Cannon::fire(tank, config_.weapon, nextProjectile_++);
                    const float offset = (static_cast<float>(i) - 1.f) * config_.weapon.napalmSpread;
                    projectile.velocity = direction(tank.aim + offset) * tank.power;
                    projectile.napalm = true;
                    projectiles_.push_back(projectile);
                }
                tank.reload = config_.weapon.napalmReload;
                emit(EventKind::Shot, tank.muzzle(config_.weapon.barrelLength), tank.id);
            } else {
                projectiles_.push_back(Cannon::fire(tank, config_.weapon, nextProjectile_++));
                tank.reload = Cannon::reload(tank.weapon,config_.weapon);
                emit(EventKind::Shot, projectiles_.back().position, tank.id);
            }
            tank.recoil = 1;
        }
    }
    std::erase_if(projectiles_, [&](Projectile& p) {
        const auto result=Physics::advance(p,dt,config_,*map_,tanks_);
        for (unsigned i=0;i<result.ricochetCount;++i) emit(EventKind::Ricochet,result.ricochets[i],p.owner,Cannon::radius(p,config_.weapon),Cannon::orbDamage(p.age,config_.weapon)/100.f);
        if (result.hit) {
            if (p.weapon==WeaponKind::Mortar || p.weapon==WeaponKind::GrowingOrb) detonate(*result.hit,p);
            else if (p.napalm) ignite(*result.hit, p.owner); else explode(*result.hit);
            return true;
        }
        if (result.expired && p.weapon==WeaponKind::GrowingOrb) detonate({1,p.position,noPlayer},p);
        return result.expired;
    });
    updateFire(dt);
}
Snapshot World::snapshot() const {
    Snapshot s;
    s.tick = tick_; s.round = round_; s.elapsed = static_cast<float>(tick_) * config_.fixedStep;
    s.tanks = tanks_; s.projectiles = projectiles_; s.events = events_; s.lasers = lasers_; s.fires = fires_;
    s.detailedWeapons = true;
    return s;
}
}
