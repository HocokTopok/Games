#pragma once
#include "Physics.hpp"
#include <array>

namespace tw {
class World {
public:
    explicit World(Config config, std::uint32_t supplySeed = 0);
    void selectMap(Config config);
    void reset(const std::vector<Player>& players);
    void step(const std::array<Input, maxPlayers>& inputs);
    void disconnect(PlayerId id);
    Snapshot snapshot() const;
    const Terrain& map() const { return *map_; }
    const std::vector<Tank>& tanks() const { return tanks_; }
    const Config& config() const { return config_; }
private:
    void emit(EventKind kind, Vec2 position, PlayerId player, float radius = 0, float intensity = 1);
    void explode(const Hit& hit, bool laser = false, float damageScale = 1);
    void ignite(const Hit& hit, PlayerId owner, float damageScale);
    void updateFire(float dt);
    void summonSkyStrike(const Hit& hit, const Projectile& projectile);
    void updateSkyStrikes(float dt);
    void detonate(const Hit& hit, const Projectile& projectile);
    void detonateMissile(const Hit& hit, const Projectile& projectile);
    void updateSupplies(float dt);
    void damagePlane(Vec2 center, float radius, float amount);
    bool hitPlane(Vec2 from, Vec2 to, float radius, float& fraction) const;
    Config config_;
    std::unique_ptr<Terrain> map_;
    std::vector<Tank> tanks_;
    std::vector<Projectile> projectiles_;
    std::vector<GameEvent> events_;
    std::vector<LaserBeam> lasers_;
    std::vector<FirePatch> fires_;
    std::vector<SkyStrike> skyStrikes_;
    std::vector<SupplyDrop> supplies_;
    SupplyPlane plane_;
    float supplyTimer_{30}, planeDirection_{1};
    std::uint32_t randomState_{};
    std::uint32_t tick_{}, round_{}, nextProjectile_{1}, nextEvent_{1};
};
}
