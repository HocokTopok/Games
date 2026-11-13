#include "tankwars/Physics.hpp"
#include "tankwars/Weapon.hpp"
#include <limits>

namespace tw::Physics {
void integrate(Projectile& p, float dt, float gravity) {
    p.position += p.velocity * dt + Vec2{0, gravity * 0.5f * dt * dt};
    p.velocity.y += gravity * dt;
    p.age += dt;
}
namespace {
std::optional<float> segmentBox(Vec2 a, Vec2 b, Vec2 extent) {
    float lo = 0, hi = 1;
    const auto axis = [&](float origin, float delta, float size) {
        if (std::abs(delta) < 0.00001f) return std::abs(origin) <= size;
        float t0 = (-size - origin) / delta, t1 = (size - origin) / delta;
        if (t0 > t1) std::swap(t0, t1);
        lo = std::max(lo, t0); hi = std::min(hi, t1);
        return lo <= hi;
    };
    if (axis(a.x, b.x - a.x, extent.x) && axis(a.y, b.y - a.y, extent.y)) return lo;
    return std::nullopt;
}
std::optional<float> segmentCircle(Vec2 a, Vec2 b, Vec2 center, float radius) {
    const auto d = b - a, f = a - center;
    const float aa = dot(d, d), cc = dot(f, f) - radius * radius;
    if (cc <= 0) return 0;
    if (aa < 0.000001f) return std::nullopt;
    const float bb = 2 * dot(f, d), discriminant = bb * bb - 4 * aa * cc;
    if (discriminant < 0) return std::nullopt;
    const float t = (-bb - std::sqrt(discriminant)) / (2 * aa);
    if (t >= 0 && t <= 1) return t;
    return std::nullopt;
}
}
std::optional<Hit> sweep(Vec2 a, Vec2 b, float radius, const Terrain& map,
                         std::span<const Tank> tanks, PlayerId ignore, float halfWidth, float halfHeight) {
    std::optional<Hit> hit;
    auto consider = [&](float t, PlayerId tank) {
        if (!hit || t < hit->fraction) hit = Hit{t, mix(a, b, t), tank};
    };
    for (const auto& tank : tanks) {
        if (!tank.alive() || tank.id == ignore) continue;
        const auto localA = rotate(a - tank.position, -tank.bodyAngle);
        const auto localB = rotate(b - tank.position, -tank.bodyAngle);
        if (auto t = segmentBox(localA, localB, {halfWidth + radius, halfHeight + radius})) consider(*t, tank.id);
        if (auto t = segmentCircle(a, b, tank.pivot(), 12 + radius)) consider(*t, tank.id);
    }
    const int steps = std::max(1, static_cast<int>(std::ceil(length(b - a) / 2.f)));
    for (int i = 0; i <= steps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(steps);
        const auto p = mix(a, b, t);
        if (p.y + radius < map.height(p.x)) continue;
        float lo = static_cast<float>(std::max(0, i - 1)) / static_cast<float>(steps), hi = t;
        for (int n = 0; n < 12; ++n) {
            const float mid = (lo + hi) * 0.5f;
            const auto q = mix(a, b, mid);
            if (q.y + radius >= map.height(q.x)) hi = mid; else lo = mid;
        }
        consider(hi, noPlayer);
        break;
    }
    return hit;
}
std::vector<Vec2> trajectory(const Tank& tank, const Config& c, const Terrain& map, std::span<const Tank> tanks) {
    if (tank.weapon == WeaponKind::Laser || tank.weapon == WeaponKind::Missile) {
        const auto laser = tank.weapon == WeaponKind::Missile ? guidanceTrace(tank,c,map,tanks) : laserTrace(tank,c,map,tanks);
        return {laser.from,laser.to};
    }
    auto p = Cannon::fire(tank, c.weapon, 0);
    std::vector<Vec2> points{p.position};
    const int limit = static_cast<int>(Cannon::lifetime(p,c.weapon) / c.fixedStep)+1;
    for (int i = 0; i < limit; ++i) {
        const auto result=advance(p,c.fixedStep,c,map,tanks);
        for (unsigned n=0;n<result.ricochetCount;++n) points.push_back(result.ricochets[n]);
        if (result.hit || result.expired) { points.push_back(p.position); break; }
        if (i % 6 == 0) points.push_back(p.position);
    }
    return points;
}
FlightResult advance(Projectile& p, float dt, const Config& c, const Terrain& map, std::span<const Tank> tanks) {
    FlightResult result;
    const bool orb=p.weapon==WeaponKind::GrowingOrb;
    const bool ricochet=p.weapon==WeaponKind::Ricochet;
    if (p.weapon==WeaponKind::Missile) {
        const auto owner=std::ranges::find(tanks,p.owner,&Tank::id);
        if (owner!=tanks.end() && owner->alive()) {
            steerMissile(p,guidanceTrace(*owner,c,map,tanks).to,dt,c.weapon.missileTurnRate);
            const float speed=length(p.velocity), desired=owner->power*c.weapon.missileSpeedScale;
            if (speed>1) p.velocity=p.velocity*((speed+std::clamp(desired-speed,-120.f*dt,120.f*dt))/speed);
        }
    }
    const float gravity=(orb || p.weapon==WeaponKind::Missile) ? 0.f : c.gravity*(p.weapon==WeaponKind::Mortar ? c.weapon.mortarGravityScale : (p.weapon==WeaponKind::Flare ? c.weapon.flareGravityScale : 1.f));
    const float lifetime=Cannon::lifetime(p,c.weapon);
    float remaining=std::min(dt,std::max(0.f,lifetime-p.age));
    for (int segment=0;segment<4 && remaining>0.000001f;++segment) {
        auto next=p; integrate(next,remaining,gravity);
        const float radius=Cannon::radius(next,c.weapon);
        const auto hit=sweep(p.position,next.position,radius,map,tanks,next.age<0.12f ? p.owner : noPlayer,c.tankHalfWidth,c.tankHalfHeight);
        float wallFraction=2; bool reflectX=false,reflectY=false;
        const auto wall=[&](float from,float to,float boundary,bool xAxis) {
            const float delta=to-from;
            const float fraction=std::abs(delta)>0.000001f ? std::clamp((boundary-from)/delta,0.f,1.f) : 0.f;
            if (fraction<wallFraction-0.00001f) { wallFraction=fraction; reflectX=xAxis; reflectY=!xAxis; }
            else if (std::abs(fraction-wallFraction)<0.00001f) { reflectX|=xAxis; reflectY|=!xAxis; }
        };
        if (orb || ricochet) {
            if (next.position.x<radius) wall(p.position.x,next.position.x,radius,true);
            if (next.position.x>c.worldWidth-radius) wall(p.position.x,next.position.x,c.worldWidth-radius,true);
            if (next.position.y<radius) wall(p.position.y,next.position.y,radius,false);
        }
        if (hit && hit->fraction<=wallFraction) {
            integrate(p,remaining*hit->fraction,gravity); p.position=hit->position;
            if (ricochet && hit->tank==noPlayer && p.bounces<3) {
                const float slope=map.slope(p.position.x);
                const Vec2 normal{std::sin(slope),-std::cos(slope)};
                const float incoming=dot(p.velocity,normal);
                if (incoming<0) p.velocity=p.velocity-normal*(2*incoming);
                p.velocity=p.velocity*0.5f; ++p.bounces;
                // Clear the enlarged sphere from the slope before consuming the
                // rest of the step; otherwise one landing can spend all bounces.
                const float clearance=Cannon::radius(p,c.weapon)+0.15f;
                p.position.y=std::min(p.position.y,map.height(p.position.x)-clearance);
                result.ricochets[result.ricochetCount++]=p.position;
                remaining*=1-hit->fraction;
                continue;
            }
            result.hit=hit; return result;
        }
        if (wallFraction<=1) {
            integrate(p,remaining*wallFraction,gravity);
            p.position.x=std::clamp(p.position.x,radius,c.worldWidth-radius);
            p.position.y=std::max(radius,p.position.y);
            if (p.bounces>=(ricochet ? 3 : 2)) { result.hit=Hit{wallFraction,p.position,noPlayer}; return result; }
            if (reflectX) p.velocity.x=-p.velocity.x;
            if (reflectY) p.velocity.y=-p.velocity.y;
            p.velocity=p.velocity*0.5f; ++p.bounces;
            // A ricochet makes the round larger. Keep its new radius clear of the
            // wall so a slow round cannot immediately count the same wall twice.
            const float bounceRadius=Cannon::radius(p,c.weapon);
            p.position.x=std::clamp(p.position.x,bounceRadius,c.worldWidth-bounceRadius);
            p.position.y=std::max(bounceRadius,p.position.y);
            result.ricochets[result.ricochetCount++]=p.position;
            remaining*=1-wallFraction;
        } else { p=next; remaining=0; }
    }
    result.expired=p.age>=lifetime-0.00001f || (!orb && !ricochet && (p.position.x < -100 || p.position.x > c.worldWidth+100 || p.position.y > c.worldHeight+100));
    return result;
}
float areaDamage(Vec2 explosion, const Tank& tank, float radius, float amount, const Config& c) {
    const Vec2 local=rotate(explosion-tank.position,-tank.bodyAngle);
    const float dx=std::max(0.f,std::abs(local.x)-c.tankHalfWidth), dy=std::max(0.f,std::abs(local.y)-c.tankHalfHeight);
    return amount*std::clamp(1.f-std::hypot(dx,dy)/radius,0.f,1.f);
}
float splashDamage(Vec2 explosion, const Tank& tank, const WeaponSpec& w) {
    const float distance = std::max(0.f, length(explosion - tank.position) - 17.f);
    return w.splashDamage * std::clamp(1.f - distance / w.blastRadius, 0.f, 1.f);
}
namespace {
LaserTrace trace(const Tank& tank, const Config& c, const Terrain& map, std::span<const Tank> tanks, bool pointer) {
    const Vec2 from = tank.muzzle(c.weapon.barrelLength) + (pointer ? direction(tank.aim+pi/2)*-7.f : Vec2{}), ray = direction(tank.aim);
    float reach = 2 * (c.worldWidth + c.worldHeight);
    const auto bound = [&](float value, float component, float low, float high) {
        if (component > 0.00001f) reach = std::min(reach,std::max(0.f,(high-value)/component));
        if (component < -0.00001f) reach = std::min(reach,std::max(0.f,(low-value)/component));
    };
    bound(from.x,ray.x,-20,c.worldWidth+20);
    bound(from.y,ray.y,-80,c.worldHeight);
    auto end = from + ray * reach;
    auto hit = sweep(from,end,pointer ? 0.75f : c.weapon.projectileRadius,map,tanks,tank.id,c.tankHalfWidth,c.tankHalfHeight);
    if (hit) end = hit->position;
    return {from,end,hit};
}
}
LaserTrace laserTrace(const Tank& tank, const Config& c, const Terrain& map, std::span<const Tank> tanks) { return trace(tank,c,map,tanks,false); }
LaserTrace guidanceTrace(const Tank& tank, const Config& c, const Terrain& map, std::span<const Tank> tanks) { return trace(tank,c,map,tanks,true); }
void steerMissile(Projectile& p, Vec2 target, float dt, float turnRate) {
    const Vec2 delta=target-p.position;
    if (length(delta)<0.01f) return;
    const float angle=std::atan2(p.velocity.y,p.velocity.x);
    const float heading=approachAngle(angle,std::atan2(delta.y,delta.x),turnRate*dt);
    p.velocity=direction(heading)*length(p.velocity);
}
float missileDamage(Vec2 explosion, const Tank& tank, bool direct, const Config& c) {
    if (direct) return c.weapon.missileDamage;
    const float falloff=areaDamage(explosion,tank,c.weapon.missileBlastRadius,1,c);
    return falloff>0 ? 1+(c.weapon.missileDamage-1)*falloff : 0;
}
void damage(Tank& tank, float amount, bool lethal) {
    if (!tank.alive() || tank.shieldRemaining > 0) return;
    tank.hp = lethal ? 0.f : std::max(0.f,tank.hp-std::max(0.f,amount));
}
}
