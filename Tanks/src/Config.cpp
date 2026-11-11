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
        {"projectile_lifetime", &c.weapon.projectileLifetime}
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
        if (auto it = values.find(key); it != values.end()) *it->second = value;
        else if (key == "port" || key == "players" || key == "window_width" || key == "window_height" || key == "frame_limit" || key == "teams") {
            if (value < 0 || value > 65535 || std::floor(value) != value) throw std::runtime_error("Invalid integer: " + key);
            auto n = static_cast<unsigned>(value);
            if (key == "port") c.port = static_cast<unsigned short>(n);
            if (key == "players") c.playerLimit = n;
            if (key == "window_width") c.windowWidth = n;
            if (key == "window_height") c.windowHeight = n;
            if (key == "frame_limit") c.frameLimit = n;
            if (key == "teams") { if (n > 1) throw std::runtime_error("teams must be 0 or 1"); c.teams = n == 1; }
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
    require(spawnInset >= 100 && spawnInset < worldWidth / 3, "spawn inset");
    require(weapon.minPower >= 50 && weapon.maxPower > weapon.minPower && weapon.maxPower <= 4000, "power range");
    require(weapon.initialPower >= weapon.minPower && weapon.initialPower <= weapon.maxPower, "initial power");
    require(weapon.reloadSeconds >= 0.1f && weapon.reloadSeconds <= 30, "reload");
    require(weapon.directDamage >= 0 && weapon.directDamage <= 200 && weapon.splashDamage >= 0 && weapon.splashDamage <= 200, "damage");
    require(weapon.blastRadius >= 10 && weapon.blastRadius <= 400 && weapon.projectileRadius >= 1 && weapon.projectileRadius <= 10, "blast / projectile radius");
    require(weapon.barrelLength >= 28 && weapon.barrelLength <= 60 && weapon.projectileLifetime >= 1 && weapon.projectileLifetime <= 20, "barrel / projectile lifetime");
    require(snapshotRate >= 10 && snapshotRate <= 60 && inputRate >= 20 && inputRate <= 120 && interpolationDelay >= 0 && interpolationDelay <= 0.3f && inputTimeout >= 0.1f && inputTimeout <= 2, "network rates");
    require(port > 0 && playerLimit >= 2 && playerLimit <= 4, "port / players");
    require(windowWidth >= 800 && windowWidth <= 7680 && windowHeight >= 450 && windowHeight <= 4320 && frameLimit <= 360, "window");
}
}
