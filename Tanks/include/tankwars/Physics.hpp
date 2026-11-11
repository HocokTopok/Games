#pragma once
#include "Map.hpp"
#include <optional>
#include <span>

namespace tw {
struct Hit { float fraction{1}; Vec2 position{}; PlayerId tank{noPlayer}; };
namespace Physics {
void integrate(Projectile& projectile, float dt, float gravity);
std::optional<Hit> sweep(Vec2 from, Vec2 to, float radius, const Terrain& map,
                         std::span<const Tank> tanks, PlayerId ignore = noPlayer,
                         float halfWidth = 27, float halfHeight = 12);
std::vector<Vec2> trajectory(const Tank& tank, const Config& config, const Terrain& map, std::span<const Tank> tanks);
float splashDamage(Vec2 explosion, const Tank& tank, const WeaponSpec& weapon);
}
}
