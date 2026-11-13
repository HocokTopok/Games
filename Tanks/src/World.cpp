#include "tankwars/World.hpp"
#include "tankwars/Weapon.hpp"
#include <algorithm>
#include <random>

namespace tw {
World::World(Config config, std::uint32_t supplySeed) : config_(config) {
    config_.validate(); map_=MapFactory::create(config_);
    randomState_=supplySeed ? supplySeed : std::random_device{}();
    if (!randomState_) randomState_=0x51a7f00d;
}
void World::selectMap(Config config) { config_=config; map_=MapFactory::create(config_); }
void World::reset(const std::vector<Player>& players) {
    map_=MapFactory::create(config_); supplies_.clear(); plane_={}; supplyTimer_=30;
    tanks_.clear(); projectiles_.clear(); events_.clear(); lasers_.clear(); fires_.clear(); skyStrikes_.clear(); tick_ = 0; ++round_;
    constexpr std::array<float, 8> fractions{0, 1, 0.24f, 0.76f, 0.12f, 0.88f, 0.36f, 0.64f};
    std::array<bool,maxPlayers> occupied{}; bool validSeats=true;
    for (const auto& p : players) if (p.connected) {
        if (p.seat>=config_.playerLimit || occupied[p.seat]) validSeats=false;
        else occupied[p.seat]=true;
    }
    std::size_t index = 0;
    for (const auto& p : players) {
        if (!p.connected) continue;
        const auto seat=validSeats ? p.seat : static_cast<std::uint8_t>(index);
        ++index;
        const float fraction=config_.teams ? (p.team==0 ? 0.08f+0.10f*(seat%(config_.playerLimit/2)) : 0.92f-0.10f*(seat%(config_.playerLimit/2))) : fractions.at(seat);
        const float x = config_.spawnInset + fraction * (config_.worldWidth - 2 * config_.spawnInset);
        tanks_.push_back({p.id, p.color, p.team,
            {x, map_->height(x) - config_.tankHalfHeight}, map_->slope(x),
            x < config_.worldWidth / 2 ? -0.65f : -pi + 0.65f, config_.weapon.initialPower});
        auto& tank = tanks_.back();
        tank.titan = config_.titanMode;
        tank.maxHp = tank.hp = tank.titan ? config_.titanHealth : 100.f;
        tank.weapon = config_.asymmetricLoadouts && p.id < 2 ? (p.id==0 ? WeaponKind::GrowingOrb : WeaponKind::Mortar) :
                      (config_.titanMode ? WeaponKind::Laser : WeaponKind::Cannon);
        if (p.weaponChosen) tank.weapon=p.weapon;
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
void World::explode(const Hit& hit, bool laser, float damageScale) {
    emit(EventKind::Impact, hit.position, hit.tank==planeTarget ? noPlayer : hit.tank);
    damagePlane(hit.tank==planeTarget ? plane_.position : hit.position,hit.tank==planeTarget ? 1.f : config_.weapon.blastRadius,(laser ? config_.weapon.laserDamage : config_.weapon.directDamage+config_.weapon.splashDamage)*damageScale);
    for (auto& tank : tanks_) {
        if (!tank.alive()) continue;
        float damage = laser ? 0.f : Physics::splashDamage(hit.position, tank, config_.weapon);
        if (tank.id == hit.tank) damage += laser ? config_.weapon.laserHitDamage(tank.maxHp) : config_.weapon.directDamage;
        Physics::damage(tank,damage*damageScale);
        if (!tank.alive()) emit(EventKind::Destroyed, tank.position, tank.id);
    }
}
void World::ignite(const Hit& hit, PlayerId owner, float damageScale) {
    emit(EventKind::NapalmImpact, hit.position, owner);
    const auto& w = config_.weapon;
    damagePlane(hit.tank==planeTarget ? plane_.position : hit.position,hit.tank==planeTarget ? 1.f : w.fireRadius,(w.napalmDirectDamage+w.napalmSplashDamage)*damageScale);
    for (auto& tank : tanks_) {
        if (!tank.alive()) continue;
        const float proximity = std::clamp(1.f - std::max(0.f, length(hit.position - tank.position) - 17.f) / w.fireRadius, 0.f, 1.f);
        if (proximity <= 0 && tank.id != hit.tank) continue;
        Physics::damage(tank, damageScale*(w.napalmSplashDamage * proximity + (tank.id == hit.tank ? w.napalmDirectDamage : 0.f)));
        tank.burnRemaining = w.burnDuration;
        tank.burnDamageScale=std::max(tank.burnDamageScale,damageScale);
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
            fire.damageScale=std::max(fire.damageScale,damageScale);
            return;
        }
    }
    if (fires_.size() >= 64) {
        const auto oldest = std::ranges::min_element(fires_, {}, &FirePatch::remaining);
        fires_.erase(oldest);
    }
    fires_.push_back({nextProjectile_++, owner, position, w.fireDuration, w.fireRadius,damageScale});
}
void World::detonate(const Hit& hit, const Projectile& p) {
    const bool mortar=p.weapon==WeaponKind::Mortar;
    const float radius=mortar ? config_.weapon.mortarBlastRadius : std::max(16.f,Cannon::radius(p,config_.weapon)*2.2f);
    const float base=mortar ? config_.weapon.mortarDamage : (p.weapon==WeaponKind::Ricochet ? Cannon::ricochetDamage(p.bounces) : Cannon::orbDamage(p.age,config_.weapon));
    const float amount=base*p.damageScale;
    emit(mortar ? EventKind::MortarImpact : EventKind::OrbImpact,hit.position,p.owner,radius,mortar ? 1.f : std::min(1.f,amount/100.f));
    if (mortar) map_->crater(hit.position,86.f,38.f);
    damagePlane(hit.tank==planeTarget ? plane_.position : hit.position,hit.tank==planeTarget ? 1.f : radius,amount);
    for (auto& tank : tanks_) {
        if (!tank.alive()) continue;
        Physics::damage(tank,tank.id==hit.tank ? amount : Physics::areaDamage(hit.position,tank,radius,amount,config_));
        if (!tank.alive()) emit(EventKind::Destroyed,tank.position,tank.id);
    }
}
void World::detonateMissile(const Hit& hit, const Projectile& p) {
    Vec2 normal;
    if (hit.tank==noPlayer) {
        const float slope=map_->slope(hit.position.x); normal={std::sin(slope),-std::cos(slope)};
    } else {
        const float speed=length(p.velocity); normal=speed>0 ? p.velocity*(-1.f/speed) : Vec2{0,-1};
    }
    emit(EventKind::MissileImpact,hit.position,p.owner,config_.weapon.missileBlastRadius);
    events_.back().normal=normal;
    map_->crater(hit.position,62.f,25.f);
    damagePlane(hit.tank==planeTarget ? plane_.position : hit.position,hit.tank==planeTarget ? 1.f : config_.weapon.missileBlastRadius,config_.weapon.missileDamage*p.damageScale);
    for (auto& tank : tanks_) {
        if (!tank.alive()) continue;
        Physics::damage(tank,Physics::missileDamage(hit.position,tank,tank.id==hit.tank,config_)*p.damageScale);
        if (!tank.alive()) emit(EventKind::Destroyed,tank.position,tank.id);
    }
}
void World::updateFire(float dt) {
    for (auto& fire : fires_) { fire.remaining -= dt; fire.position.y=map_->height(fire.position.x)-3; }
    std::erase_if(fires_, [](const auto& fire) { return fire.remaining <= 0; });
    for (auto& tank : tanks_) {
        if (!tank.alive()) continue;
        float fireScale=1;
        bool inFire=false;
        for (const auto& fire : fires_) {
            const float dx=std::max(0.f,std::abs(tank.position.x-fire.position.x)-config_.tankHalfWidth);
            const float dy=std::max(0.f,std::abs(tank.position.y-fire.position.y)-config_.tankHalfHeight);
            if (std::hypot(dx,dy)<=fire.radius) { inFire=true; fireScale=std::max(fireScale,fire.damageScale); }
        }
        if (inFire) { tank.burnRemaining=config_.weapon.burnDuration; tank.burnDamageScale=fireScale; }
        const float burnTime = std::min(dt, tank.burnRemaining);
        // Overlapping pools refresh the fire without multiplying its damage.
        const float damage = inFire ? config_.weapon.fireDamagePerSecond * dt : config_.weapon.burnDamagePerSecond * burnTime;
        Physics::damage(tank, damage*tank.burnDamageScale);
        tank.burnRemaining = std::max(0.f, tank.burnRemaining - dt);
        if (tank.burnRemaining<=0) tank.burnDamageScale=1;
        if (!tank.alive()) emit(EventKind::Destroyed, tank.position, tank.id);
    }
}
void World::summonSkyStrike(const Hit& hit, const Projectile& p) {
    const float x=std::clamp(hit.position.x,0.f,config_.worldWidth);
    const Vec2 position{x,map_->height(x)};
    // Bound persistent effects even in eight-player rooms.
    if (skyStrikes_.size()>=16) skyStrikes_.erase(skyStrikes_.begin());
    skyStrikes_.push_back({nextProjectile_++,p.owner,position,0,config_.weapon.skyRadius,p.damageScale});
    emit(EventKind::FlareImpact,position,p.owner,config_.weapon.skyRadius);
}
void World::updateSkyStrikes(float dt) {
    const auto& w=config_.weapon;
    for (auto& strike : skyStrikes_) {
        const float before=strike.age;
        strike.age+=dt;
        if (before<w.skyWarmup && strike.age>=w.skyWarmup) emit(EventKind::SkyStrike,strike.position,strike.owner,strike.radius);
        const float active=std::max(0.f,std::min(strike.age,w.skyWarmup+w.skyDuration)-std::max(before,w.skyWarmup));
        if (active<=0) continue;
        map_->erodeColumn(strike.position.x,strike.radius,24.f*active);
        strike.position.y=map_->height(strike.position.x);
        if (plane_.active && !plane_.crashing && std::abs(plane_.position.x-strike.position.x)<strike.radius+50)
            damagePlane(plane_.position,1,w.skyDamagePerSecond*active*strike.damageScale);
        for (auto& tank : tanks_) {
            if (!tank.alive() || tank.position.y-config_.tankHalfHeight>strike.position.y) continue;
            const float distance=std::max(0.f,std::abs(tank.position.x-strike.position.x)-config_.tankHalfWidth);
            // Full damage in the bright core, a softer edge in the orange halo.
            const float falloff=std::clamp((strike.radius-distance)/(strike.radius*0.35f),0.f,1.f);
            Physics::damage(tank,w.skyDamagePerSecond*active*falloff*strike.damageScale);
            if (!tank.alive()) emit(EventKind::Destroyed,tank.position,tank.id);
        }
    }
    std::erase_if(skyStrikes_,[&](const auto& strike) { return strike.age>=w.skyWarmup+w.skyDuration; });
}
void World::step(const std::array<Input, maxPlayers>& inputs) {
    const float dt = config_.fixedStep;
    ++tick_;
    for (auto& tank : tanks_) {
        tank.damageRemaining=std::max(0.f,tank.damageRemaining-dt);
        tank.shieldRemaining=std::max(0.f,tank.shieldRemaining-dt);
        tank.healIndicator=std::max(0.f,tank.healIndicator-dt);
    }
    updateSupplies(dt);
    for (auto& tank : tanks_) {
        if (!tank.alive()) continue;
        auto in = inputs[tank.id];
        if (!std::isfinite(in.move) || !std::isfinite(in.powerDelta) || !std::isfinite(in.aim) || !std::isfinite(in.lift)) in = {};
        const auto previousPosition = tank.position;
        tank.inMud=map_->movementScale(tank.position.x)<1 && tank.position.y>=map_->height(tank.position.x)-config_.tankHalfHeight-1;
        const float movement = std::clamp(in.move, -1.f, 1.f) * config_.tankSpeed * dt * (tank.inMud ? map_->movementScale(tank.position.x) : 1.f);
        float x = std::clamp(tank.position.x + movement, config_.tankHalfWidth + 8, config_.worldWidth - config_.tankHalfWidth - 8);
        for (const auto& other : tanks_) {
            if (other.id == tank.id || !other.alive()) continue;
            if (std::abs(tank.position.y - other.position.y) >= config_.tankHalfHeight * 2 + 2) continue;
            if (std::abs(x - other.position.x) < config_.tankHalfWidth * 2 + 2 &&
                std::abs(x-other.position.x)<std::abs(tank.position.x-other.position.x)) x=tank.position.x;
        }
        tank.track += (x - tank.position.x) / 8.f;
        const float floor = map_->height(x) - config_.tankHalfHeight;
        {
            const float lift = tank.canFly ? std::clamp(in.lift, -1.f, 1.f) : 0.f;
            const bool grounded = tank.position.y >= map_->height(tank.position.x) - config_.tankHalfHeight - 0.5f && tank.verticalVelocity >= 0;
            if (grounded && lift > 0) tank.verticalVelocity = -config_.jumpSpeed;
            if (grounded && lift <= 0 && floor-tank.position.y<=3.f) {
                tank.position.y = floor;
                tank.verticalVelocity = 0;
            } else {
                const float acceleration = config_.gravity - std::max(0.f, lift) * (config_.gravity + config_.flightAcceleration)
                                           + std::max(0.f, -lift) * config_.flightAcceleration;
                tank.verticalVelocity = std::clamp(tank.verticalVelocity + acceleration * dt, -config_.flightSpeed, config_.descentSpeed);
                tank.position.y += tank.verticalVelocity * dt;
                // HUD has its own frame; the flight ceiling belongs to world geometry.
                const float ceiling = 42.f;
                if (tank.position.y < ceiling) { tank.position.y = ceiling; tank.verticalVelocity = std::max(0.f, tank.verticalVelocity); }
                if (tank.position.y >= floor) { tank.position.y = floor; tank.verticalVelocity = 0; }
                for (auto& other : tanks_) {
                    if (other.id == tank.id || !other.alive() || std::abs(x - other.position.x) >= config_.tankHalfWidth * 2 + 2) continue;
                    const float top = other.position.y - config_.tankHalfHeight * 2 - 2;
                    if (previousPosition.y <= top && tank.position.y >= top) {
                        Physics::crush(other);
                        if (!other.alive()) emit(EventKind::Destroyed,other.position,other.id);
                        emit(EventKind::Impact,{x,top+config_.tankHalfHeight},other.id);
                        tank.position.y = top;
                        tank.verticalVelocity = -config_.jumpSpeed*0.4f;
                    }
                }
            }
            tank.position.x = x;
            tank.bodyAngle = approachAngle(tank.bodyAngle, tank.position.y >= floor - 0.5f ? map_->slope(x) : 0.f, dt * 4);
        }
        tank.velocity=(tank.position-previousPosition)/dt;
        tank.aim = approachAngle(tank.aim, Cannon::firingAngle(tank.weapon,std::remainder(in.aim, 2 * pi)), config_.turretSpeed * dt);
        if (tank.weapon != WeaponKind::Laser) tank.power = std::clamp(tank.power + std::clamp(in.powerDelta, -1.f, 1.f) * config_.powerSpeed * dt,
                                                config_.weapon.minPower, config_.weapon.maxPower);
        tank.reload = std::max(0.f, tank.reload - dt);
        tank.recoil = std::max(0.f, tank.recoil - dt * 3.5f);
        const std::size_t pellets = tank.weapon == WeaponKind::Napalm ? 3 : 1;
        const bool guiding=tank.weapon==WeaponKind::Missile && std::ranges::any_of(projectiles_,[&](const auto& p) { return p.owner==tank.id && p.weapon==WeaponKind::Missile; });
        if (in.fire && !guiding && tank.reload <= 0 && (tank.weapon == WeaponKind::Laser || projectiles_.size() + pellets <= 128)) {
            if (tank.weapon == WeaponKind::Laser) {
                const auto laser = Physics::laserTrace(tank,config_,*map_,tanks_);
                emit(EventKind::Shot,laser.from,tank.id);
                lasers_.push_back({nextEvent_-1,tank.id,laser.from,laser.to});
                if (lasers_.size() > 64) lasers_.erase(lasers_.begin(),lasers_.begin()+16);
                if (laser.hit) explode(*laser.hit,true,tank.damageRemaining>0 ? 1.5f : 1.f);
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
        const Vec2 before=p.position;
        auto result=Physics::advance(p,dt,config_,*map_,tanks_);
        float planeFraction=0;
        const bool planeHit=hitPlane(before,p.position,Cannon::radius(p,config_.weapon),planeFraction);
        if (planeHit) {
            p.position=mix(before,p.position,planeFraction);
            result.hit=Hit{planeFraction,p.position,planeTarget};
        }
        for (unsigned i=0;i<result.ricochetCount;++i) emit(EventKind::Ricochet,result.ricochets[i],p.owner,Cannon::radius(p,config_.weapon),p.weapon==WeaponKind::Ricochet ? Cannon::ricochetDamage(p.bounces)/100.f : Cannon::orbDamage(p.age,config_.weapon)/100.f);
        if (result.hit) {
            if (p.weapon==WeaponKind::Missile) detonateMissile(*result.hit,p);
            else if (p.weapon==WeaponKind::Flare) summonSkyStrike(*result.hit,p);
            else if (p.weapon==WeaponKind::Mortar || p.weapon==WeaponKind::GrowingOrb || p.weapon==WeaponKind::Ricochet) detonate(*result.hit,p);
            else if (p.napalm) ignite(*result.hit, p.owner,p.damageScale); else explode(*result.hit,false,p.damageScale);
            return true;
        }
        if (result.expired && p.weapon==WeaponKind::GrowingOrb) detonate({1,p.position,noPlayer},p);
        if (result.expired && p.weapon==WeaponKind::Missile) detonateMissile({1,p.position,noPlayer},p);
        return result.expired;
    });
    updateFire(dt);
    updateSkyStrikes(dt);
}
bool World::hitPlane(Vec2 from, Vec2 to, float radius, float& fraction) const {
    if (!plane_.active || plane_.crashing) return false;
    const Vec2 extent{53+radius,23+radius};
    Vec2 a=from-plane_.position, d=to-from;
    a={a.x/extent.x,a.y/extent.y}; d={d.x/extent.x,d.y/extent.y};
    const float aa=dot(d,d), bb=2*dot(a,d), cc=dot(a,a)-1;
    if (cc<=0) { fraction=0; return true; }
    if (aa<1e-8f || bb*bb-4*aa*cc<0) return false;
    fraction=(-bb-std::sqrt(bb*bb-4*aa*cc))/(2*aa);
    return fraction>=0 && fraction<=1;
}
void World::damagePlane(Vec2 center, float radius, float amount) {
    if (!plane_.active || plane_.crashing) return;
    const float distance=std::hypot(std::max(0.f,std::abs(center.x-plane_.position.x)-53),std::max(0.f,std::abs(center.y-plane_.position.y)-23));
    if (distance>=radius) return;
    plane_.hp=std::max(0.f,plane_.hp-amount*(1-distance/radius));
    if (plane_.hp<=0) { plane_.crashing=true; plane_.fallSpeed=35; }
}
void World::updateSupplies(float dt) {
    supplyTimer_-=dt;
    const auto random=[&]() { randomState_^=randomState_<<13; randomState_^=randomState_>>17; randomState_^=randomState_<<5; return randomState_; };
    if (supplyTimer_<=0) {
        supplyTimer_+=30;
        const float direction=planeDirection_; planeDirection_=-planeDirection_;
        float highest=config_.worldHeight;
        for (float x=0;x<=config_.worldWidth;x+=4) highest=std::min(highest,map_->height(x));
        plane_={true,{direction>0 ? -150.f : config_.worldWidth+150.f,std::max(58.f,highest-96.f)},
            config_.worldWidth*(0.25f+0.5f*(random()%10001)/10000.f),false};
        plane_.direction=direction;
    }
    if (plane_.active) {
        const Vec2 previous=plane_.position;
        plane_.position.x+=plane_.direction*config_.worldWidth/9.f*dt;
        if (plane_.crashing) {
            plane_.fallSpeed+=config_.gravity*0.65f*dt;
            plane_.position.y+=plane_.fallSpeed*dt;
            plane_.angle=std::min(1.25f,plane_.angle+dt*0.65f);
            if (auto hit=Physics::sweep(previous,plane_.position,20,*map_,{},noPlayer,0,0)) {
                plane_.position=hit->position; plane_.active=false;
                constexpr float radius=170;
                emit(EventKind::PlaneCrash,plane_.position,noPlayer,radius);
                map_->crater(plane_.position,125,52);
                for (auto& tank : tanks_) if (tank.alive()) {
                    const float damage=Physics::areaDamage(plane_.position,tank,radius,200,config_);
                    if (damage>0) Physics::damage(tank,1+199*damage/200);
                    if (!tank.alive()) emit(EventKind::Destroyed,tank.position,tank.id);
                }
            }
        }
        if (!plane_.crashing && !plane_.released && (plane_.position.x-plane_.targetX)*plane_.direction>=0) {
            plane_.released=true;
            if (supplies_.size()>=8) supplies_.erase(supplies_.begin());
            supplies_.push_back({nextProjectile_++,{plane_.targetX,plane_.position.y+15},0,0,static_cast<PickupKind>(random()%3),false});
        }
        if (plane_.position.x>config_.worldWidth+160 || plane_.position.x< -160) {
            if (plane_.crashing) { plane_.position.x=std::clamp(plane_.position.x,20.f,config_.worldWidth-20.f); }
            else plane_.active=false;
        }
    }
    for (auto& drop : supplies_) {
        drop.age+=dt;
        const float floor=map_->height(drop.position.x)-12;
        if (!drop.landed) {
            drop.velocity=std::min(125.f,drop.velocity+90.f*dt);
            drop.position.y+=drop.velocity*dt;
            if (drop.position.y>=floor) { drop.position.y=floor; drop.landed=true; drop.age=0; emit(EventKind::SupplyLanded,drop.position,noPlayer,25); }
        } else drop.position.y=floor;
    }
    std::erase_if(supplies_,[&](const auto& drop) {
        if (!drop.landed || drop.age<0.45f) return false;
        for (auto& tank : tanks_) {
            if (!tank.alive() || std::abs(tank.position.x-drop.position.x)>config_.tankHalfWidth+18 || std::abs(tank.position.y-drop.position.y)>config_.tankHalfHeight+30) continue;
            if (drop.kind==PickupKind::Health) { tank.hp=std::min(tank.maxHp,tank.hp+200); tank.healIndicator=1.5f; }
            if (drop.kind==PickupKind::Damage) tank.damageRemaining=15;
            if (drop.kind==PickupKind::Shield) tank.shieldRemaining=10;
            emit(EventKind::Pickup,drop.position,tank.id,32,1);
            return true;
        }
        return drop.age>75;
    });
}
Snapshot World::snapshot() const {
    Snapshot s;
    s.tick = tick_; s.round = round_; s.elapsed = static_cast<float>(tick_) * config_.fixedStep;
    s.tanks = tanks_; s.projectiles = projectiles_; s.events = events_; s.lasers = lasers_; s.fires = fires_; s.skyStrikes=skyStrikes_;
    s.detailedWeapons = true; s.battlefieldData=true; s.mapId=config_.mapId;
    s.terrain=map_->samples(); s.supplies=supplies_; s.plane=plane_;
    return s;
}
}
