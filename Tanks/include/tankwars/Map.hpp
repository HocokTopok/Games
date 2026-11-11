#pragma once
#include "Config.hpp"
#include "Types.hpp"
#include <memory>

namespace tw {
class Terrain {
public:
    virtual ~Terrain() = default;
    virtual float height(float x) const = 0;
    float slope(float x) const { return std::atan2(height(x + 2) - height(x - 2), 4.f); }
};
class NightfallBasin final : public Terrain {
public:
    explicit NightfallBasin(const Config& config) : config_(config) {}
    float height(float x) const override;
private:
    Config config_;
};
class MapFactory {
public:
    static std::unique_ptr<Terrain> create(const Config& config) { return std::make_unique<NightfallBasin>(config); }
};
}
