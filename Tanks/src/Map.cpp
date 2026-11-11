#include "tankwars/Map.hpp"

namespace tw {
const char* MapFactory::name(std::uint8_t id) {
    constexpr const char* names[]{"EL ALAMEIN", "ARDENNES", "VITEBSK MARSHES", "STALINGRAD"};
    return names[std::min<unsigned>(id,3)];
}
const char* MapFactory::description(std::uint8_t id) {
    constexpr const char* descriptions[]{"01 / 1600 m / dunes & desert wind", "02 / 2200 m / rolling forest hills", "03 / 2600 m / twin peaks & muddy lowland", "04 / 3200 m / ruins, ridges & ash"};
    return descriptions[std::min<unsigned>(id,3)];
}
Config MapFactory::preset(Config c, std::uint8_t id) {
    c.mapId=std::min<std::uint8_t>(id,3);
    constexpr float widths[]{1600,2200,2600,3200};
    constexpr float hills[]{45,95,205,170};
    c.worldWidth=widths[c.mapId]; c.worldHeight=c.worldWidth*9.f/16;
    c.groundHeight=c.worldHeight*0.735f; c.hillHeight=hills[c.mapId];
    c.spawnInset=c.worldWidth*0.1375f;
    return c;
}
NightfallBasin::NightfallBasin(const Config& config, std::span<const float> surface) : config_(config) {
    const auto count=static_cast<std::size_t>(std::ceil(config_.worldWidth/4))+1;
    if (surface.size()==count) heights_.assign(surface.begin(),surface.end());
    else {
        heights_.resize(count);
        for (std::size_t i=0;i<count;++i) heights_[i]=originalHeight(static_cast<float>(i)*config_.worldWidth/(count-1));
    }
}
float NightfallBasin::originalHeight(float x) const {
    const float u=x/config_.worldWidth, t=u*2*pi, h=config_.hillHeight;
    if (config_.mapId==1) return config_.groundHeight-h*(0.55f+0.30f*std::cos(t*3)+0.15f*std::cos(t*7));
    if (config_.mapId==2) {
        const auto mountain=[&](float center) { const float d=(u-center)/0.13f; return std::exp(-d*d); };
        return config_.groundHeight-h*(mountain(0.17f)+mountain(0.83f))+h*0.045f*std::cos(t*9);
    }
    if (config_.mapId==3) return config_.groundHeight-h*(0.45f+0.25f*std::cos(t*2)+0.20f*std::cos(t*5)+0.10f*std::cos(t*9));
    return config_.groundHeight+h*(0.7f*std::cos(t)+0.3f*std::cos(3*t));
}
float NightfallBasin::height(float x) const {
    const float f=std::clamp(x/config_.worldWidth,0.f,1.f)*(heights_.size()-1);
    const auto i=static_cast<std::size_t>(f), j=std::min(i+1,heights_.size()-1);
    return heights_[i]+(heights_[j]-heights_[i])*(f-i);
}
float NightfallBasin::bedrock(float x) const { return config_.groundHeight+config_.hillHeight+32+5*std::cos(x/config_.worldWidth*4*pi); }
void NightfallBasin::crater(Vec2 center, float radius, float depth) {
    const float gap=std::max(0.f,height(center.x)-center.y);
    if (gap>=radius) return;
    depth*=1-gap/radius;
    for (std::size_t i=0;i<heights_.size();++i) {
        const float x=static_cast<float>(i)*config_.worldWidth/(heights_.size()-1);
        const float d=std::abs(x-center.x)/radius;
        if (d>=1) continue;
        const float smooth=0.5f+0.5f*std::cos(d*pi);
        heights_[i]=std::min(bedrock(x),heights_[i]+depth*smooth*smooth);
    }
    // Repeated impacts erode the rim into ramps instead of leaving vertical steps.
    const float maxStep=config_.worldWidth/(heights_.size()-1);
    for (std::size_t i=1;i<heights_.size();++i) heights_[i]=std::max(heights_[i],heights_[i-1]-maxStep);
    for (std::size_t i=heights_.size()-1;i>0;--i) heights_[i-1]=std::max(heights_[i-1],heights_[i]-maxStep);
}
float NightfallBasin::movementScale(float x) const {
    const float u=x/config_.worldWidth;
    if (config_.mapId!=2 || u<0.3f || u>0.7f) return 1;
    // Raised hummocks remain firm; the shallow pools between them slow tracks.
    return std::cos(u*18*pi)>0.25f ? 1.f : 0.48f;
}
}
