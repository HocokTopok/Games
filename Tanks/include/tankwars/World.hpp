#pragma once
#include "Physics.hpp"
#include <array>

namespace tw {
class World {
public:
    explicit World(Config config);
    void reset(const std::vector<Player>& players);
    void step(const std::array<Input, maxPlayers>& inputs);
    void disconnect(PlayerId id);
    Snapshot snapshot() const;
    const Terrain& map() const { return *map_; }
    const std::vector<Tank>& tanks() const { return tanks_; }
    const Config& config() const { return config_; }
private:
    void emit(EventKind kind, Vec2 position, PlayerId player);
    void explode(const Hit& hit);
    Config config_;
    std::unique_ptr<Terrain> map_;
    std::vector<Tank> tanks_;
    std::vector<Projectile> projectiles_;
    std::vector<GameEvent> events_;
    std::uint32_t tick_{}, round_{}, nextProjectile_{1}, nextEvent_{1};
};
}
