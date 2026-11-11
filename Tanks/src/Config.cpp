#include "tankwars/Config.hpp"
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace tw {
Config Config::load(const std::filesystem::path& path) {
    Config c;
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open config: " + path.string());
    std::unordered_map<std::string, float*> values{
        {"world_width", &c.worldWidth}, {"world_height", &c.worldHeight},
        {"ground_height", &c.groundHeight}, {"hill_height", &c.hillHeight},
        {"gravity", &c.gravity}, {"fixed_step", &c.fixedStep}, {"tank_speed", &c.tankSpeed},
        {"turret_speed", &c.turretSpeed}, {"power_speed", &c.powerSpeed},
        {"tank_half_width", &c.tankHalfWidth}, {"tank_half_height", &c.tankHalfHeight},
        {"spawn_inset", &c.spawnInset}, {"snapshot_rate", &c.snapshotRate},
        {"input_rate", &c.inputRate}, {"interpolation_delay", &c.interpolationDelay},
        {"input_timeout", &c.inputTimeout}, {"min_power", &c.weapon.minPower},
        {"max_power", &c.weapon.maxPower}, {"initial_power", &c.weapon.initialPower},
        {"reload_seconds", &c.weapon.reloadSeconds}, {"direct_damage", &c.weapon.directDamage},
        {"splash_damage", &c.weapon.splashDamage}, {"blast_radius", &c.weapon.blastRadius},
        {"projectile_radius", &c.weapon.projectileRadius}, {"barrel_length", &c.weapon.barrelLength},
        {"projectile_lifetime", &c.weapon.projectileLifetime}, {"laser_reload", &c.weapon.laserReload}, {"laser_damage", &c.weapon.laserDamage},
        {"laser_damage_percent", &c.weapon.laserDamagePercent},
        {"napalm_reload", &c.weapon.napalmReload}, {"napalm_direct_damage", &c.weapon.napalmDirectDamage},
        {"napalm_splash_damage", &c.weapon.napalmSplashDamage}, {"napalm_spread", &c.weapon.napalmSpread},
        {"fire_radius", &c.weapon.fireRadius}, {"fire_duration", &c.weapon.fireDuration},
        {"fire_damage_per_second", &c.weapon.fireDamagePerSecond}, {"burn_duration", &c.weapon.burnDuration},
        {"burn_damage_per_second", &c.weapon.burnDamagePerSecond}, {"jump_speed", &c.jumpSpeed},
        {"flight_speed", &c.flightSpeed}, {"flight_acceleration", &c.flightAcceleration}, {"descent_speed", &c.descentSpeed},
        {"titan_health", &c.titanHealth},
        {"mortar_reload", &c.weapon.mortarReload}, {"mortar_speed_scale", &c.weapon.mortarSpeedScale},
        {"mortar_gravity_scale", &c.weapon.mortarGravityScale}, {"mortar_blast_radius", &c.weapon.mortarBlastRadius},
        {"mortar_damage", &c.weapon.mortarDamage}, {"orb_reload", &c.weapon.orbReload},
        {"orb_speed_scale", &c.weapon.orbSpeedScale}, {"orb_growth_seconds", &c.weapon.orbGrowthSeconds},
        {"orb_initial_radius", &c.weapon.orbInitialRadius}, {"orb_max_radius", &c.weapon.orbMaxRadius}, {"orb_lifetime", &c.weapon.orbLifetime}
    };
    std::string line;
    unsigned lineNo = 0;
    while (std::getline(file, line)) {
        ++lineNo;
        line = line.substr(0, line.find('#'));
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        const auto equal = line.find('=');
        if (equal == std::string::npos) throw std::runtime_error("Missing '=' on config line " + std::to_string(lineNo));
        auto key = line.substr(0, equal);
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        std::istringstream number(line.substr(equal + 1));
        float value;
        std::string extra;
        if (!(number >> value) || number >> extra || !std::isfinite(value))
            throw std::runtime_error("Invalid config value: " + key);
        if (auto it = values.find(key); it != values.end()) {
            *it->second = value;
            if (key == "laser_damage") c.weapon.laserDamagePercent = 0;
        }
        else if (key == "port" || key == "players" || key == "window_width" || key == "window_height" || key == "frame_limit" || key == "teams" || key == "titan_mode" || key == "asymmetric_loadouts") {
            if (value < 0 || value > 65535 || std::floor(value) != value) throw std::runtime_error("Invalid integer: " + key);
            auto n = static_cast<unsigned>(value);
            if (key == "port") c.port = static_cast<unsigned short>(n);
            if (key == "players") c.playerLimit = n;
            if (key == "window_width") c.windowWidth = n;
            if (key == "window_height") c.windowHeight = n;
            if (key == "frame_limit") c.frameLimit = n;
            if (key == "teams") { if (n > 1) throw std::runtime_error("teams must be 0 or 1"); c.teams = n == 1; }
            if (key == "titan_mode") { if (n > 1) throw std::runtime_error("titan_mode must be 0 or 1"); c.titanMode = n == 1; }
            if (key == "asymmetric_loadouts") { if (n > 1) throw std::runtime_error("asymmetric_loadouts must be 0 or 1"); c.asymmetricLoadouts = n == 1; }
        } else throw std::runtime_error("Unknown config key: " + key);
    }
    c.validate();
    return c;
}
void Config::validate() const {
    const auto require = [](bool ok, const char* what) { if (!ok) throw std::runtime_error(std::string("Invalid config: ") + what); };
    require(worldWidth >= 1000 && worldWidth <= 4000 && worldHeight >= 600 && worldHeight <= 2000, "world dimensions");
    require(groundHeight > 200 && groundHeight < worldHeight - 80 && hillHeight >= 0 && hillHeight < 60, "terrain");
    require(gravity >= 50 && gravity <= 3000 && fixedStep >= 1.f / 300.f && fixedStep <= 1.f / 30.f, "gravity / time step");
    require(tankSpeed > 0 && tankSpeed <= 300 && turretSpeed > 0 && turretSpeed <= 20 && powerSpeed > 0 && powerSpeed <= 2000, "tank speeds");
    require(tankHalfWidth >= 15 && tankHalfWidth <= 40 && tankHalfHeight >= 8 && tankHalfHeight <= 22, "tank bounds");
    require(std::isfinite(titanHealth) && titanHealth >= 100 && titanHealth <= 1000000, "Titan health");
    require(spawnInset >= 100 && spawnInset < worldWidth / 3, "spawn inset");
    require(weapon.minPower >= 50 && weapon.maxPower > weapon.minPower && weapon.maxPower <= 4000, "power range");
    require(weapon.initialPower >= weapon.minPower && weapon.initialPower <= weapon.maxPower, "initial power");
    require(weapon.reloadSeconds >= 0.1f && weapon.reloadSeconds <= 30, "reload");
    require(weapon.laserReload >= 0.1f && weapon.laserReload <= 5, "laser reload");
    require(std::isfinite(weapon.laserDamage) && weapon.laserDamage > 0 && weapon.laserDamage <= 10000, "laser damage");
    require(std::isfinite(weapon.laserDamagePercent) && weapon.laserDamagePercent >= 0 && weapon.laserDamagePercent <= 100, "laser damage percent");
    require(weapon.napalmReload >= 0.05f && weapon.napalmReload <= 2 && weapon.napalmSpread >= 0 && weapon.napalmSpread <= 0.3f, "napalm fire rate / spread");
    require(weapon.napalmDirectDamage >= 0 && weapon.napalmDirectDamage <= 25 && weapon.napalmSplashDamage >= 0 && weapon.napalmSplashDamage <= 25, "napalm damage");
    require(weapon.fireRadius >= 10 && weapon.fireRadius <= 150 && weapon.fireDuration >= 0.1f && weapon.fireDuration <= 15 && weapon.fireDamagePerSecond >= 0 && weapon.fireDamagePerSecond <= 50, "ground fire");
    require(weapon.burnDuration >= 0.1f && weapon.burnDuration <= 15 && weapon.burnDamagePerSecond >= 0 && weapon.burnDamagePerSecond <= 50, "burning");
    require(weapon.mortarReload >= 0.1f && weapon.mortarReload <= 10 && weapon.mortarSpeedScale >= 0.1f && weapon.mortarSpeedScale <= 1 && weapon.mortarGravityScale >= 0.05f && weapon.mortarGravityScale <= 1, "mortar ballistics");
    require(weapon.mortarBlastRadius >= 20 && weapon.mortarBlastRadius <= 200 && weapon.mortarDamage >= 1 && weapon.mortarDamage <= 200, "mortar explosion");
    require(weapon.orbReload >= 0.1f && weapon.orbReload <= 10 && weapon.orbSpeedScale >= 0.1f && weapon.orbSpeedScale <= 2, "orb fire rate / speed");
    require(weapon.orbGrowthSeconds >= 0.5f && weapon.orbGrowthSeconds <= 20 && weapon.orbInitialRadius >= 2 && weapon.orbMaxRadius > weapon.orbInitialRadius && weapon.orbMaxRadius <= 60 && weapon.orbLifetime >= weapon.orbGrowthSeconds && weapon.orbLifetime <= 30, "orb growth");
    require(jumpSpeed >= 50 && jumpSpeed <= 1000 && flightSpeed >= jumpSpeed && flightSpeed <= 1200 && flightAcceleration > 0 && flightAcceleration <= 6000 && descentSpeed >= 50 && descentSpeed <= 1500, "flight");
    require(weapon.directDamage >= 0 && weapon.directDamage <= 200 && weapon.splashDamage >= 0 && weapon.splashDamage <= 200, "damage");
    require(weapon.blastRadius >= 10 && weapon.blastRadius <= 400 && weapon.projectileRadius >= 1 && weapon.projectileRadius <= 10, "blast / projectile radius");
    require(weapon.barrelLength >= 28 && weapon.barrelLength <= 60 && weapon.projectileLifetime >= 1 && weapon.projectileLifetime <= 20, "barrel / projectile lifetime");
    require(snapshotRate >= 10 && snapshotRate <= 60 && inputRate >= 20 && inputRate <= 120 && interpolationDelay >= 0 && interpolationDelay <= 0.3f && inputTimeout >= 0.1f && inputTimeout <= 2, "network rates");
    require(port > 0 && playerLimit >= 2 && playerLimit <= 4, "port / players");
    require(windowWidth >= 800 && windowWidth <= 7680 && windowHeight >= 450 && windowHeight <= 4320 && frameLimit <= 360, "window");
}
}
