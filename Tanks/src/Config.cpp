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
        {"missile_reload", &c.weapon.missileReload}, {"missile_speed_scale", &c.weapon.missileSpeedScale},
        {"missile_turn_rate", &c.weapon.missileTurnRate}, {"missile_blast_radius", &c.weapon.missileBlastRadius}, {"missile_damage", &c.weapon.missileDamage},
        {"ricochet_reload", &c.weapon.ricochetReload}, {"ricochet_speed_scale", &c.weapon.ricochetSpeedScale},
        {"orb_speed_scale", &c.weapon.orbSpeedScale}, {"orb_growth_seconds", &c.weapon.orbGrowthSeconds},
        {"orb_initial_radius", &c.weapon.orbInitialRadius}, {"orb_max_radius", &c.weapon.orbMaxRadius}, {"orb_lifetime", &c.weapon.orbLifetime},
        {"flare_reload", &c.weapon.flareReload}, {"flare_speed_scale", &c.weapon.flareSpeedScale}, {"flare_gravity_scale", &c.weapon.flareGravityScale},
        {"sky_warmup", &c.weapon.skyWarmup}, {"sky_duration", &c.weapon.skyDuration}, {"sky_radius", &c.weapon.skyRadius}, {"sky_damage_per_second", &c.weapon.skyDamagePerSecond}
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
        else if (key == "port" || key == "players" || key == "window_width" || key == "window_height" || key == "frame_limit" || key == "teams" || key == "titan_mode" || key == "asymmetric_loadouts" || key == "loadout_selection" || key == "map_id") {
            if (value < 0 || value > 65535 || std::floor(value) != value) throw std::runtime_error("Invalid integer: " + key);
            auto n = static_cast<unsigned>(value);
            if (key == "loadout_selection") { if (n > 1) throw std::runtime_error("loadout_selection must be 0 or 1"); c.loadoutSelection = n == 1; }
            if (key == "map_id") { if (n > 3) throw std::runtime_error("map_id must be 0-3"); c.mapId = static_cast<std::uint8_t>(n); }
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
    require(groundHeight > 200 && groundHeight < worldHeight - 80 && hillHeight >= 0 && hillHeight <= 300 && groundHeight + hillHeight + 40 < worldHeight, "terrain");
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
    require(mapId < 4, "map ID");
    require(weapon.missileReload >= 0.1f && weapon.missileReload <= 30 && weapon.missileSpeedScale >= 0.1f && weapon.missileSpeedScale <= 3 && weapon.missileTurnRate >= 0.1f && weapon.missileTurnRate <= 5, "missile flight / reload");
    require(weapon.missileBlastRadius >= 10 && weapon.missileBlastRadius <= 200 && weapon.missileDamage >= 1 && weapon.missileDamage <= 500, "missile damage");
    require(weapon.ricochetReload >= 0.1f && weapon.ricochetReload <= 10 && weapon.ricochetSpeedScale >= 0.5f && weapon.ricochetSpeedScale <= 3, "ricochet gun");
    require(weapon.orbReload >= 0.1f && weapon.orbReload <= 10 && weapon.orbSpeedScale >= 0.1f && weapon.orbSpeedScale <= 2, "orb fire rate / speed");
    require(weapon.orbGrowthSeconds >= 0.5f && weapon.orbGrowthSeconds <= 20 && weapon.orbInitialRadius >= 2 && weapon.orbMaxRadius > weapon.orbInitialRadius && weapon.orbMaxRadius <= 60 && weapon.orbLifetime >= weapon.orbGrowthSeconds && weapon.orbLifetime <= 30, "orb growth");
    require(weapon.flareReload >= 5 && weapon.flareReload <= 60 && weapon.flareSpeedScale >= 0.1f && weapon.flareSpeedScale <= 2 && weapon.flareGravityScale >= 0.1f && weapon.flareGravityScale <= 1, "flare ballistics / reload");
    require(weapon.skyWarmup >= 0.1f && weapon.skyWarmup <= 5 && weapon.skyDuration >= 1 && weapon.skyDuration <= 10 && weapon.skyRadius >= 20 && weapon.skyRadius <= 200 && weapon.skyDamagePerSecond > 0 && weapon.skyDamagePerSecond <= 500, "sky strike");
    require(jumpSpeed >= 50 && jumpSpeed <= 1000 && flightSpeed >= jumpSpeed && flightSpeed <= 1200 && flightAcceleration > 0 && flightAcceleration <= 6000 && descentSpeed >= 50 && descentSpeed <= 1500, "flight");
    require(weapon.directDamage >= 0 && weapon.directDamage <= 200 && weapon.splashDamage >= 0 && weapon.splashDamage <= 200, "damage");
    require(weapon.blastRadius >= 10 && weapon.blastRadius <= 400 && weapon.projectileRadius >= 1 && weapon.projectileRadius <= 10, "blast / projectile radius");
    require(weapon.barrelLength >= 28 && weapon.barrelLength <= 60 && weapon.projectileLifetime >= 1 && weapon.projectileLifetime <= 20, "barrel / projectile lifetime");
    require(snapshotRate >= 10 && snapshotRate <= 60 && inputRate >= 20 && inputRate <= 120 && interpolationDelay >= 0 && interpolationDelay <= 0.3f && inputTimeout >= 0.1f && inputTimeout <= 2, "network rates");
    require(port > 0 && playerLimit >= 2 && playerLimit <= 8 && (!teams || playerLimit==4 || playerLimit==8), "port / players / team size");
    require(windowWidth >= 800 && windowWidth <= 7680 && windowHeight >= 450 && windowHeight <= 4320 && frameLimit <= 360, "window");
}
}
