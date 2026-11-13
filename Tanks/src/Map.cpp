#include "tankwars/Map.hpp"

namespace tw {
const char* MapFactory::name(std::uint8_t id) {
    constexpr const char* names[]{"EL ALAMEIN", "ARDENNES", "VITEBSK MARSHES", "STALINGRAD"};
    return names[std::min<unsigned>(id,3)];
}
const char* MapFactory::description(std::uint8_t id) {
    constexpr const char* descriptions[]{"01 / 1600 m / dunes & desert wind", "02 / 2200 m / three towering peaks", "03 / 2600 m / deep jagged ravine", "04 / 3200 m / districts, ravines & hills"};
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
    const float height=config_.worldHeight;
    const auto peak=[&](float center,float width) { const float d=(u-center)/width; return std::exp(-d*d); };
    if (config_.mapId==1) {
        // Two peaks on the left, the tallest at 43%; a slightly lower giant on the right.
        return height*(0.82f-0.34f*peak(0.21f,0.095f)-0.55f*peak(0.43f,0.105f)-0.48f*peak(0.78f,0.125f)
                       +0.008f*std::sin(t*9)*std::sin(pi*u));
    }
    if (config_.mapId==2) {
        const float sides=2*u-1, envelope=4*u*(1-u);
        const float rough=envelope*(0.021f*std::sin(t*7+0.4f)+0.012f*std::sin(t*13+1.3f)+0.008f*std::sin(t*23));
        return height*(0.88f-0.65f*sides*sides+rough);
    }
    if (config_.mapId==3) {
        // Low, gently rolling districts separated by two hills and shallow gullies.
        return height*(0.73f + 0.003f*std::sin(t*5) + 0.002f*std::sin(t*11)
                       -0.15f*peak(0.32f,0.043f)-0.19f*peak(0.72f,0.052f)
                       +0.075f*peak(0.47f,0.027f)+0.065f*peak(0.87f,0.030f)
                       +0.055f*peak(0.17f,0.025f));
    }
    return config_.groundHeight+h*(0.7f*std::cos(t)+0.3f*std::cos(3*t));
}
float NightfallBasin::height(float x) const {
    const float f=std::clamp(x/config_.worldWidth,0.f,1.f)*(heights_.size()-1);
    const auto i=static_cast<std::size_t>(f), j=std::min(i+1,heights_.size()-1);
    return heights_[i]+(heights_[j]-heights_[i])*(f-i);
}
float NightfallBasin::bedrock(float x) const {
    const float base=config_.groundHeight+config_.hillHeight+32+5*std::cos(x/config_.worldWidth*4*pi);
    return config_.mapId==2 ? std::max(base,config_.worldHeight*0.965f) : base;
}
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
    std::vector<float> original(heights_.size());
    for (std::size_t i=0;i<original.size();++i) original[i]=originalHeight(i*maxStep);
    // Smooth only the erosion depth, so a crater cannot flatten an existing mountain.
    for (std::size_t i=1;i<heights_.size();++i)
        heights_[i]=std::min(bedrock(i*maxStep),std::max(heights_[i],original[i]+heights_[i-1]-original[i-1]-maxStep));
    for (std::size_t i=heights_.size()-1;i>0;--i)
        heights_[i-1]=std::min(bedrock((i-1)*maxStep),std::max(heights_[i-1],original[i-1]+heights_[i]-original[i]-maxStep));
}
void NightfallBasin::erodeColumn(float center, float radius, float depth) {
    // Move high ground down first: the beam cuts a level floor across its bright core.
    float bottom=height(center);
    for (float x=std::max(0.f,center-radius*0.82f);x<=std::min(config_.worldWidth,center+radius*0.82f);x+=4)
        bottom=std::max(bottom,height(x));
    for (std::size_t i=0;i<heights_.size();++i) {
        const float x=i*config_.worldWidth/(heights_.size()-1), d=std::abs(x-center)/radius;
        if (d>=1) continue;
        const float edge=std::clamp((1-d)/0.18f,0.f,1.f);
        const float cut=depth + std::min(depth*3.f,std::max(0.f,bottom-heights_[i]));
        heights_[i]=std::min(bedrock(x),heights_[i]+cut*edge);
    }
}
float NightfallBasin::movementScale(float x) const {
    const float u=x/config_.worldWidth;
    if (config_.mapId!=2 || u<0.3f || u>0.7f) return 1;
    // Raised hummocks remain firm; the shallow pools between them slow tracks.
    return std::cos(u*18*pi)>0.25f ? 1.f : 0.48f;
}
}
