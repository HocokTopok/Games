#pragma once
#include "Config.hpp"
#include "Types.hpp"

namespace tw {
inline constexpr std::array<WeaponKind,5> selectableWeapons{WeaponKind::Napalm,WeaponKind::Mortar,WeaponKind::Missile,WeaponKind::Ricochet,WeaponKind::Flare};
inline const char* weaponName(WeaponKind kind) {
    switch (kind) {
    case WeaponKind::Napalm: return "NAPALM";
    case WeaponKind::Mortar: return "MORTAR";
    case WeaponKind::Laser: return "RAILGUN";
    case WeaponKind::Missile: return "GUIDED MISSILE";
    case WeaponKind::Ricochet: return "RICOCHET";
    case WeaponKind::GrowingOrb: return "PLASMA ORB";
    case WeaponKind::Flare: return "SOLAR FLARE";
    default: return "CANNON";
    }
}
inline const char* gadgetName(GadgetKind kind, WeaponKind weapon) {
    if (kind==GadgetKind::Jetpack) return "JET ENGINE";
    if (kind==GadgetKind::Armor) return "ARMOR";
    switch (weapon) {
    case WeaponKind::Napalm: return "BLUE INFERNO";
    case WeaponKind::Mortar: return "TRIPLE SALVO";
    case WeaponKind::Ricochet: return "OVERDRIVE";
    case WeaponKind::Missile: return "CLUSTER WARHEAD";
    case WeaponKind::Flare: return "SUPERNOVA";
    default: return "SPECIALIST";
    }
}
// x = 100 world units: 25 at the burst, 20 at x, 15 at 3x and beyond.
inline constexpr float clusterFalloffDistance=100.f;
inline float clusterDamage(float distance) {
    const float x=clusterFalloffDistance;
    if (distance<=x) return 25.f-5.f*std::clamp(distance/x,0.f,1.f);
    return 20.f-5.f*std::clamp((distance-x)/(2*x),0.f,1.f);
}
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
        const float scale = tank.weapon == WeaponKind::Missile ? spec.missileSpeedScale : (tank.weapon == WeaponKind::Mortar ? spec.mortarSpeedScale :
                            (tank.weapon == WeaponKind::GrowingOrb ? spec.orbSpeedScale : (tank.weapon == WeaponKind::Ricochet ? spec.ricochetSpeedScale : (tank.weapon == WeaponKind::Flare ? spec.flareSpeedScale : 1.f))));
        Projectile p{id, tank.id, tank.pivot() + direction(aim)*spec.barrelLength, direction(aim) * (tank.power*scale), 0};
        p.damageScale = tank.damageRemaining > 0 ? 1.5f : 1.f;
        p.weapon = tank.weapon; p.napalm = tank.weapon == WeaponKind::Napalm;
        p.blueFire=tank.hasGadget && tank.gadget==GadgetKind::Specialist && tank.weapon==WeaponKind::Napalm && tank.gadgetRemaining>0;
        if (p.blueFire) p.damageScale*=1.5f;
        p.empowered=tank.specialistArmed && tank.weapon==WeaponKind::Flare;
        return p;
    }
    static float ricochetDamage(unsigned bounces) { return 10.f + 15.f * std::min(bounces,3u); }
    static float orbCharge(float age, const WeaponSpec& spec) { return std::clamp(age/spec.orbGrowthSeconds,0.f,1.f); }
    static float orbDamage(float age, const WeaponSpec& spec) { const float t=orbCharge(age,spec); return 1.f+99.f*t*t; }
    static float radius(const Projectile& p, const WeaponSpec& spec) {
        if (p.clusterPellet) return 4.5f;
        if (p.weapon == WeaponKind::GrowingOrb || p.weapon == WeaponKind::Ricochet) return spec.orbInitialRadius+(spec.orbMaxRadius-spec.orbInitialRadius)*orbCharge(p.age,spec);
        if (p.weapon == WeaponKind::Flare) return 5.f;
        if (p.weapon == WeaponKind::Missile) return 4.f;
        return p.weapon == WeaponKind::Mortar ? 6.f : spec.projectileRadius;
    }
    static float reload(WeaponKind kind, const WeaponSpec& spec) {
        switch (kind) {
        case WeaponKind::Ricochet: return spec.ricochetReload;
        case WeaponKind::Mortar: return spec.mortarReload;
        case WeaponKind::GrowingOrb: return spec.orbReload;
        case WeaponKind::Laser: return spec.laserReload;
        case WeaponKind::Missile: return spec.missileReload;
        case WeaponKind::Napalm: return spec.napalmReload;
        case WeaponKind::Flare: return spec.flareReload;
        default: return spec.reloadSeconds;
        }
    }
    static float lifetime(const Projectile& p, const WeaponSpec& spec) { return p.weapon == WeaponKind::Missile ? 20.f : (p.weapon == WeaponKind::GrowingOrb ? spec.orbLifetime : spec.projectileLifetime); }
};
}
