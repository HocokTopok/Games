#pragma once
#include "Map.hpp"
#include <optional>
#include <span>

namespace tw {
struct Hit { float fraction{1}; Vec2 position{}; PlayerId tank{noPlayer}; };
namespace Physics {
void integrate(Projectile& projectile, float dt, float gravity);
struct FlightResult { std::optional<Hit> hit; std::array<Vec2,3> ricochets{}; unsigned ricochetCount{}; bool expired{}; };
FlightResult advance(Projectile& projectile, float dt, const Config& config, const Terrain& map, std::span<const Tank> tanks);
float areaDamage(Vec2 explosion, const Tank& tank, float radius, float damage, const Config& config);
std::optional<Hit> sweep(Vec2 from, Vec2 to, float radius, const Terrain& map,
                         std::span<const Tank> tanks, PlayerId ignore = noPlayer,
                         float halfWidth = 27, float halfHeight = 12);
std::vector<Vec2> trajectory(const Tank& tank, const Config& config, const Terrain& map, std::span<const Tank> tanks);
float splashDamage(Vec2 explosion, const Tank& tank, const WeaponSpec& weapon);
struct LaserTrace { Vec2 from, to; std::optional<Hit> hit; };
LaserTrace laserTrace(const Tank& tank, const Config& config, const Terrain& map, std::span<const Tank> tanks);
void damage(Tank& tank, float amount, bool lethal = false);
void crush(Tank& tank);
}
}
