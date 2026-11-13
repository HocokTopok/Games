#pragma once
#include <filesystem>
#include <cstdint>
#include <string>

namespace tw {
struct WeaponSpec {
    float minPower{350}, maxPower{1100}, initialPower{850};
    float reloadSeconds{2.2f}, directDamage{48}, splashDamage{36}, blastRadius{86};
    float projectileRadius{3}, barrelLength{37}, projectileLifetime{9};
    float laserReload{2.4f}, laserDamage{80}, laserDamagePercent{8};
    float missileReload{4}, missileSpeedScale{0.65f}, missileTurnRate{1.35f}, missileBlastRadius{52}, missileDamage{120};
    float ricochetReload{0.42f}, ricochetSpeedScale{1.4f};
    float napalmReload{0.12f}, napalmDirectDamage{2.7f/1.2f}, napalmSplashDamage{1.8f/1.2f}, napalmSpread{0.075f};
    float fireRadius{60}, fireDuration{4.f/1.5f}, fireDamagePerSecond{6.f/1.2f};
    float burnDuration{2.5f}, burnDamagePerSecond{2.5f/1.2f};
    float mortarReload{1.5f}, mortarSpeedScale{0.61f}, mortarGravityScale{0.3f}, mortarBlastRadius{110}, mortarDamage{150};
    float orbReload{0.85f}, orbSpeedScale{0.75f}, orbGrowthSeconds{5}, orbInitialRadius{4}, orbMaxRadius{30}, orbLifetime{20};
    float flareReload{12}, flareSpeedScale{0.8f}, flareGravityScale{0.6f};
    float skyWarmup{0.65f}, skyDuration{4}, skyRadius{115}, skyDamagePerSecond{140};
    float laserHitDamage(float maxHp) const { return laserDamagePercent > 0 ? maxHp * (laserDamagePercent / 100.f) : laserDamage; }
};
struct Config {
    float worldWidth{1600}, worldHeight{900}, groundHeight{636}, hillHeight{19};
    float gravity{510}, fixedStep{1.f / 120.f}, tankSpeed{72}, turretSpeed{3.8f}, powerSpeed{230};
    float tankHalfWidth{27}, tankHalfHeight{12}, spawnInset{220};
    float snapshotRate{30}, inputRate{60}, interpolationDelay{0.075f}, inputTimeout{0.35f};
    unsigned short port{53000};
    unsigned playerLimit{2}, windowWidth{1440}, windowHeight{810}, frameLimit{120};
    bool teams{false}, titanMode{false};
    float titanHealth{1000};
    bool laserDamageKnown{true};
    bool asymmetricLoadouts{true};
    bool loadoutSelection{false};
    std::uint8_t mapId{};
    float jumpSpeed{300}, flightSpeed{340}, flightAcceleration{1100}, descentSpeed{420};
    WeaponSpec weapon;
    static Config load(const std::filesystem::path& path);
    void validate() const;
};
}
