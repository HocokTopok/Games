#pragma once
#include "Config.hpp"
#include "Types.hpp"

namespace tw {
class Cannon {
public:
    static Projectile fire(const Tank& tank, const WeaponSpec& spec, std::uint32_t id) {
        return {id, tank.id, tank.muzzle(spec.barrelLength), direction(tank.aim) * tank.power, 0};
    }
};
}
