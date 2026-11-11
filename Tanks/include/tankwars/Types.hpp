#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

namespace tw {
constexpr float pi = std::numbers::pi_v<float>;
constexpr std::size_t maxPlayers = 4;
using PlayerId = std::uint8_t;
constexpr PlayerId noPlayer = 255;

struct Vec2 {
    float x{}, y{};
    Vec2 operator+(Vec2 b) const { return {x + b.x, y + b.y}; }
    Vec2 operator-(Vec2 b) const { return {x - b.x, y - b.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 b) { x += b.x; y += b.y; return *this; }
};
inline float length(Vec2 v) { return std::hypot(v.x, v.y); }
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline Vec2 direction(float a) { return {std::cos(a), std::sin(a)}; }
inline Vec2 rotate(Vec2 v, float a) { return {v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a)}; }
inline float angleDelta(float a, float b) { return std::remainder(b - a, 2.f * pi); }
inline float approachAngle(float a, float b, float step) { return a + std::clamp(angleDelta(a, b), -step, step); }
inline Vec2 mix(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }
inline bool finite(Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }

struct Input {
    float move{}, aim{-0.7f}, powerDelta{};
    bool fire{};
    float lift{};
};
enum class Phase : std::uint8_t { Lobby, Playing, Victory };
enum class EventKind : std::uint8_t { Shot, Impact, Destroyed, NapalmImpact };
enum class WeaponKind : std::uint8_t { Cannon, Laser, Napalm };
struct GameEvent {
    std::uint32_t id{};
    EventKind kind{};
    Vec2 position{};
    PlayerId player{noPlayer};
};
struct Player {
    PlayerId id{};
    std::uint8_t color{}, team{};
    bool connected{}, ready{}, bot{};
    std::string name;
};
struct Tank {
    PlayerId id{};
    std::uint8_t color{}, team{};
    Vec2 position{};
    float bodyAngle{}, aim{}, power{}, hp{100}, reload{}, track{}, recoil{};
    bool titan{};
    WeaponKind weapon{WeaponKind::Cannon};
    bool canFly{};
    float verticalVelocity{}, burnRemaining{};
    float maxHp{100};
    bool alive() const { return hp > 0.f; }
    float healthRatio() const { return std::clamp(hp / std::max(1.f,maxHp),0.f,1.f); }
    Vec2 pivot() const { return position + rotate({0, -12}, bodyAngle); }
    Vec2 muzzle(float barrelLength) const { return pivot() + direction(aim) * barrelLength; }
};
struct Projectile {
    std::uint32_t id{};
    PlayerId owner{};
    Vec2 position{}, velocity{};
    float age{};
    bool napalm{};
};
struct LaserBeam {
    std::uint32_t id{};
    PlayerId owner{};
    Vec2 from{}, to{};
};
struct FirePatch {
    std::uint32_t id{};
    PlayerId owner{};
    Vec2 position{};
    float remaining{}, radius{};
};
struct Snapshot {
    std::uint32_t tick{}, round{};
    Phase phase{Phase::Lobby};
    PlayerId winner{noPlayer};
    float elapsed{};
    std::vector<Player> players;
    std::vector<Tank> tanks;
    std::vector<Projectile> projectiles;
    std::vector<GameEvent> events;
    std::vector<LaserBeam> lasers;
    std::vector<FirePatch> fires;
};
inline const Tank* findTank(const Snapshot& state, PlayerId id) {
    for (const auto& tank : state.tanks) if (tank.id == id) return &tank;
    return nullptr;
}
}
