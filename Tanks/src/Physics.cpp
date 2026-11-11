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
    if (tank.weapon == WeaponKind::Laser) {
        const auto laser = laserTrace(tank,c,map,tanks);
        return {laser.from,laser.to};
    }
    auto p = Cannon::fire(tank, c.weapon, 0);
    std::vector<Vec2> points{p.position};
    const int limit = static_cast<int>(c.weapon.projectileLifetime / c.fixedStep);
    for (int i = 0; i < limit; ++i) {
        const auto from = p.position;
        integrate(p, c.fixedStep, c.gravity);
        auto hit = sweep(from, p.position, c.weapon.projectileRadius, map, tanks,
                         p.age < 0.12f ? tank.id : noPlayer, c.tankHalfWidth, c.tankHalfHeight);
        if (hit) { points.push_back(hit->position); break; }
        if (i % 3 == 0) points.push_back(p.position);
        if (p.position.x < -100 || p.position.x > c.worldWidth + 100 || p.position.y > c.worldHeight + 100) break;
    }
    return points;
}
float splashDamage(Vec2 explosion, const Tank& tank, const WeaponSpec& w) {
    const float distance = std::max(0.f, length(explosion - tank.position) - 17.f);
    return w.splashDamage * std::clamp(1.f - distance / w.blastRadius, 0.f, 1.f);
}
LaserTrace laserTrace(const Tank& tank, const Config& c, const Terrain& map, std::span<const Tank> tanks) {
    const Vec2 from = tank.muzzle(c.weapon.barrelLength), ray = direction(tank.aim);
    float reach = 2 * (c.worldWidth + c.worldHeight);
    const auto bound = [&](float value, float component, float low, float high) {
        if (component > 0.00001f) reach = std::min(reach,std::max(0.f,(high-value)/component));
        if (component < -0.00001f) reach = std::min(reach,std::max(0.f,(low-value)/component));
    };
    bound(from.x,ray.x,-20,c.worldWidth+20);
    bound(from.y,ray.y,-80,c.worldHeight);
    auto end = from + ray * reach;
    auto hit = sweep(from,end,c.weapon.projectileRadius,map,tanks,tank.id,c.tankHalfWidth,c.tankHalfHeight);
    if (hit) end = hit->position;
    return {from,end,hit};
}
void damage(Tank& tank, float amount, bool lethal) {
    if (!tank.alive() || tank.titan) return;
    tank.hp = lethal ? 0.f : std::max(0.f,tank.hp-std::max(0.f,amount));
}
void crush(Tank& tank) { tank.hp = 0; }
}
