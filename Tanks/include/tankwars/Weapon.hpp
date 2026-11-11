#pragma once
#include "Config.hpp"
#include "Types.hpp"

namespace tw {
class Cannon {
public:
    static float firingAngle(WeaponKind kind, float aim) {
        if (kind != WeaponKind::Mortar) return aim;
        aim = std::remainder(aim,2*pi);
        if (std::cos(aim) >= 0) return std::clamp(aim,-pi/2+0.12f,-0.6f);
        if (aim > 0) aim -= 2*pi;
        return std::clamp(aim,-pi+0.6f,-pi/2-0.12f);
    }
    static Projectile fire(const Tank& tank, const WeaponSpec& spec, std::uint32_t id) {
        const float aim = firingAngle(tank.weapon,tank.aim);
        const float scale = tank.weapon == WeaponKind::Mortar ? spec.mortarSpeedScale :
                            (tank.weapon == WeaponKind::GrowingOrb ? spec.orbSpeedScale : 1.f);
        Projectile p{id, tank.id, tank.pivot() + direction(aim)*spec.barrelLength, direction(aim) * (tank.power*scale), 0};
        p.weapon = tank.weapon; p.napalm = tank.weapon == WeaponKind::Napalm;
        return p;
    }
    static float orbCharge(float age, const WeaponSpec& spec) { return std::clamp(age/spec.orbGrowthSeconds,0.f,1.f); }
    static float orbDamage(float age, const WeaponSpec& spec) { const float t=orbCharge(age,spec); return 1.f+99.f*t*t; }
    static float radius(const Projectile& p, const WeaponSpec& spec) {
        if (p.weapon == WeaponKind::GrowingOrb) return spec.orbInitialRadius+(spec.orbMaxRadius-spec.orbInitialRadius)*orbCharge(p.age,spec);
        return p.weapon == WeaponKind::Mortar ? 6.f : spec.projectileRadius;
    }
    static float reload(WeaponKind kind, const WeaponSpec& spec) {
        switch (kind) {
        case WeaponKind::Mortar: return spec.mortarReload;
        case WeaponKind::GrowingOrb: return spec.orbReload;
        case WeaponKind::Laser: return spec.laserReload;
        case WeaponKind::Napalm: return spec.napalmReload;
        default: return spec.reloadSeconds;
        }
    }
    static float lifetime(const Projectile& p, const WeaponSpec& spec) { return p.weapon == WeaponKind::GrowingOrb ? spec.orbLifetime : spec.projectileLifetime; }
};
}
