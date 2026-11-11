#pragma once
#include "Config.hpp"
#include "Types.hpp"
#include <memory>
#include <span>

namespace tw {
class Terrain {
public:
    virtual ~Terrain() = default;
    virtual float height(float x) const = 0;
    virtual float bedrock(float x) const { return height(x); }
    virtual void crater(Vec2, float, float) {}
    virtual const std::vector<float>& samples() const { static const std::vector<float> empty; return empty; }
    virtual float movementScale(float) const { return 1.f; }
    float slope(float x) const { return std::atan2(height(x + 2) - height(x - 2), 4.f); }
};
class NightfallBasin final : public Terrain {
public:
    explicit NightfallBasin(const Config& config, std::span<const float> surface = {});
    float height(float x) const override;
    float bedrock(float x) const override;
    void crater(Vec2 center, float radius, float depth) override;
    const std::vector<float>& samples() const override { return heights_; }
    float movementScale(float x) const override;
private:
    float originalHeight(float x) const;
    Config config_;
    std::vector<float> heights_;
};
class MapFactory {
public:
    static std::unique_ptr<Terrain> create(const Config& config, std::span<const float> surface = {}) { return std::make_unique<NightfallBasin>(config,surface); }
    static Config preset(Config config, std::uint8_t id);
    static const char* name(std::uint8_t id);
    static const char* description(std::uint8_t id);
};
}
