#pragma once
#include <filesystem>
#include <string>

namespace tw {
struct WeaponSpec {
    float minPower{350}, maxPower{1100}, initialPower{850};
    float reloadSeconds{2.2f}, directDamage{48}, splashDamage{36}, blastRadius{86};
    float projectileRadius{3}, barrelLength{37}, projectileLifetime{9};
};
struct Config {
    float worldWidth{1600}, worldHeight{900}, groundHeight{636}, hillHeight{19};
    float gravity{510}, fixedStep{1.f / 120.f}, tankSpeed{72}, turretSpeed{3.8f}, powerSpeed{230};
    float tankHalfWidth{27}, tankHalfHeight{12}, spawnInset{220};
    float snapshotRate{30}, inputRate{60}, interpolationDelay{0.075f}, inputTimeout{0.35f};
    unsigned short port{53000};
    unsigned playerLimit{2}, windowWidth{1440}, windowHeight{810}, frameLimit{120};
    bool teams{false};
    WeaponSpec weapon;
    static Config load(const std::filesystem::path& path);
    void validate() const;
};
}
