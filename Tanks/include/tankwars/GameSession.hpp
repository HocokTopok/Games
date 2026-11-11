#pragma once
#include "World.hpp"

namespace tw {
class GameSession {
public:
    explicit GameSession(Config config);
    std::optional<PlayerId> addPlayer(std::string name, bool bot = false);
    void removePlayer(PlayerId id);
    bool setColor(PlayerId id, std::uint8_t color);
    bool setWeapon(PlayerId id, WeaponKind weapon);
    bool setMap(PlayerId id, std::uint8_t map);
    void setReady(PlayerId id, bool ready);
    void setInput(PlayerId id, Input input);
    void step();
    void rematch();
    Snapshot snapshot() const;
    const Config& config() const { return config_; }
    const World& world() const { return world_; }
private:
    Input botInput(const Tank& tank) const;
    Config config_;
    World world_;
    std::vector<Player> players_;
    std::array<Input, maxPlayers> inputs_{};
    Phase phase_{Phase::Lobby};
    bool mapSelected_{};
    PlayerId winner_{noPlayer};
};
}
