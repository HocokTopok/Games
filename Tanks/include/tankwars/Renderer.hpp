#pragma once
#include "World.hpp"
#include <SFML/Graphics.hpp>
#include <filesystem>
#include <unordered_map>

namespace tw {
struct VisualSettings { bool shake{true}, particles{true}, trajectory{true}; unsigned frameLimit{120}; };
class Renderer {
public:
    Renderer(sf::RenderTarget& window, const std::filesystem::path& assets);
    void resize(const Config& config);
    void update(float dt);
    void resetEffects();
    void observe(const Snapshot& state);
    void scenery(const Config& config, float time, bool showcase = false, const Snapshot* state = nullptr);
    void battle(const Config& config, const Snapshot& state, PlayerId local, Vec2 mouse, bool cursor);
    void updateBanner(float time);
    void gadgetPreview(GadgetKind kind, WeaponKind weapon, Vec2 center, float time);
    void weaponPreview(WeaponKind kind, Vec2 center, float time);
    void mapPreview(std::uint8_t id, Vec2 position, Vec2 size);
    void hud(const Config& config, const Snapshot& state, PlayerId local);
    void text(const std::string& value, Vec2 position, unsigned size, sf::Color color, bool centered = false);
    void box(Vec2 position, Vec2 size, sf::Color fill, sf::Color outline = sf::Color::Transparent);
    void line(Vec2 from, Vec2 to, sf::Color color, float thickness = 1);
    void circle(Vec2 center, float radius, sf::Color fill, sf::Color outline = sf::Color::Transparent, float thickness = 1);
    bool button(Vec2 position, Vec2 size, const std::string& label,
                Vec2 mouse, bool clicked, bool primary = false, bool enabled = true);
    void dim(float opacity = 0.75f);
    Vec2 uiMouse(sf::Vector2i pixel) const;
    Vec2 worldMouse(sf::Vector2i pixel) const;
    const sf::View& uiView() const { return uiView_; }
    VisualSettings settings;
    static sf::Color playerColor(std::uint8_t index);
    static inline const sf::Color white{230, 240, 249}, muted{119, 145, 166}, accent{114, 246, 208}, dark{9, 16, 29};
private:
    void tank(const Tank& tank, const Config& config, float scale = 1);
    void decorations(const Config& config, const Terrain& map, float time);
    void celestial(const Config& config);
    void foregroundFog(const Config& config, const Terrain& map, float time);
    void supplies(const Config& config, const Snapshot& state);
    void perkIcon(PickupKind kind, Vec2 position, float size, sf::Color color, float rotation = 0);
    void terrain(const Config& config, const Terrain& map);
    void effects();
    void flame(Vec2 position, float size, float phase, bool blue = false);
    void tankWear(const Tank& tank, const Config& config, const Terrain& map, float time);
    void glow(Vec2 position, float radius, sf::Color color, float opacity);
    void ring(Vec2 position, float radius, sf::Color color, float start = 0, float sweep = 2*pi, float squash = 1, float tilt = 0);
    struct TrailPoint { Vec2 position; std::uint8_t bounces{}; };
    void weaponProjectile(const Projectile& projectile, const Config& config, const std::vector<TrailPoint>& trail);
    void jetExhaust(Vec2 position, float angle, float size, float phase);
    void distantExplosions(const Config& config, float time);
    void addWeaponEffect(const GameEvent& event);
    void weaponEffects();
    void skyStrikes(const Config& config, const Snapshot& state);
    sf::RenderTarget& window_;
    sf::Font font_;
    sf::View uiView_{{800, 450}, {1600, 900}}, worldView_{{800, 450}, {1600, 900}};
    struct Particle { Vec2 position, velocity; sf::Color color; float life, total, size; };
    struct Burst { Vec2 position; sf::Color color; float age{}, duration{}, radius{}; };
    struct BeamEffect { Vec2 from, to; sf::Color color; float age{}; };
    struct WeaponEffect { Vec2 position; EventKind kind; float age{}, duration{}, radius{}, intensity{}; std::uint32_t seed{}; Vec2 normal{0,-1}; };
    struct ArmorShard { Vec2 position, velocity; float age{}, spin{}; };
    std::vector<ArmorShard> armorShards_;
    std::vector<Particle> particles_;
    std::vector<Burst> bursts_;
    std::vector<BeamEffect> beams_;
    std::vector<WeaponEffect> weaponEffects_;
    std::unordered_map<std::uint32_t, std::vector<TrailPoint>> trails_;
    std::uint32_t lastEvent_{}, lastBeam_{}, round_{};
    float time_{}, shake_{};
};
}
