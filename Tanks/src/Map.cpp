#include "tankwars/Map.hpp"
namespace tw {
float NightfallBasin::height(float x) const {
    const float t = x / config_.worldWidth * 2.f * pi;
    return config_.groundHeight + config_.hillHeight * (0.7f * std::cos(t) + 0.3f * std::cos(3.f * t));
}
}
