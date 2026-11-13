#include "tankwars/Renderer.hpp"
#include "tankwars/Weapon.hpp"
#include <iomanip>
#include <random>
#include <sstream>

namespace tw {
namespace {
sf::Vector2f sfv(Vec2 v) { return {v.x, v.y}; }
sf::Color alpha(sf::Color c, float a) { c.a = static_cast<std::uint8_t>(std::clamp(a, 0.f, 255.f)); return c; }
sf::FloatRect viewport(sf::Vector2u size, float aspect) {
    const float ratio = static_cast<float>(size.x) / static_cast<float>(std::max(1u, size.y));
    if (ratio > aspect) { const float width = aspect / ratio; return {{(1 - width) / 2, 0}, {width, 1}}; }
    const float height = ratio / aspect; return {{0, (1 - height) / 2}, {1, height}};
}
}
Renderer::Renderer(sf::RenderTarget& window, const std::filesystem::path& assets) : window_(window) {
    if (!font_.openFromFile(assets / "fonts/Rajdhani-SemiBold.ttf")) throw std::runtime_error("Cannot load bundled font. Keep assets beside the executable.");
}
sf::Color Renderer::playerColor(std::uint8_t i) {
    constexpr std::array<sf::Color, 8> colors{sf::Color{114,246,208}, sf::Color{255,120,148}, sf::Color{150,162,255}, sf::Color{255,202,112}, sf::Color{100,195,255}, sf::Color{230,151,255}, sf::Color{200,232,110}, sf::Color{255,164,95}};
    return colors[i % colors.size()];
}
void Renderer::resize(const Config& c) {
    uiView_.setViewport(viewport(window_.getSize(), 1600.f / 900.f));
    worldView_.setSize({c.worldWidth, c.worldHeight}); worldView_.setCenter({c.worldWidth / 2, c.worldHeight / 2});
    worldView_.setViewport(viewport(window_.getSize(), c.worldWidth / c.worldHeight));
    window_.setView(uiView_);
}
Vec2 Renderer::uiMouse(sf::Vector2i pixel) const { const auto p = window_.mapPixelToCoords(pixel, uiView_); return {p.x, p.y}; }
Vec2 Renderer::worldMouse(sf::Vector2i pixel) const { const auto p = window_.mapPixelToCoords(pixel, worldView_); return {p.x, p.y}; }
void Renderer::text(const std::string& value, Vec2 p, unsigned size, sf::Color color, bool centered) {
    sf::Text label(font_, value, size);
    label.setFillColor(color);
    if (centered) { const auto b = label.getLocalBounds(); label.setOrigin({b.position.x + b.size.x / 2, 0}); }
    label.setPosition(sfv(p)); window_.draw(label);
}
void Renderer::box(Vec2 p, Vec2 size, sf::Color fill, sf::Color outline) {
    sf::RectangleShape shape(sfv(size)); shape.setPosition(sfv(p)); shape.setFillColor(fill);
    if (outline.a) { shape.setOutlineThickness(1); shape.setOutlineColor(outline); } window_.draw(shape);
}
void Renderer::line(Vec2 a, Vec2 b, sf::Color c, float width) {
    const auto d = b - a;
    sf::RectangleShape shape({length(d), width}); shape.setOrigin({0, width / 2});
    shape.setPosition(sfv(a)); shape.setRotation(sf::radians(std::atan2(d.y, d.x))); shape.setFillColor(c); window_.draw(shape);
}
void Renderer::circle(Vec2 p, float r, sf::Color fill, sf::Color outline, float thickness) {
    sf::CircleShape shape(r, 48); shape.setOrigin({r, r}); shape.setPosition(sfv(p)); shape.setFillColor(fill);
    shape.setOutlineColor(outline); shape.setOutlineThickness(outline.a ? thickness : 0); window_.draw(shape);
}
bool Renderer::button(Vec2 p, Vec2 size, const std::string& title, Vec2 mouse, bool clicked, bool primary, bool enabled) {
    primary=primary && enabled;
    const bool hovered = mouse.x >= p.x && mouse.y >= p.y && mouse.x <= p.x + size.x && mouse.y <= p.y + size.y && enabled;
    const auto ink = primary ? dark : (enabled ? white : muted);
    box(p, size, primary ? (hovered ? sf::Color{158,255,226} : accent) : (hovered ? sf::Color{30,48,64,245} : sf::Color{16,28,43,240}),
        primary ? accent : (hovered ? muted : sf::Color{40,60,77}));
    box(p, {3, size.y}, primary ? dark : (hovered ? accent : sf::Color{50,72,88}));
    sf::Text label(font_, title, 26);
    while (label.getLocalBounds().size.x > size.x - 24 && label.getCharacterSize() > 14)
        label.setCharacterSize(label.getCharacterSize() - 1);
    const auto bounds = label.getLocalBounds();
    label.setOrigin(bounds.position + bounds.size / 2.f);
    label.setPosition(sfv(p + size / 2.f)); label.setFillColor(ink); window_.draw(label);
    return hovered && clicked;
}
void Renderer::dim(float opacity) { window_.setView(uiView_); box({0,0}, {1600,900}, {4,9,18,static_cast<std::uint8_t>(opacity * 255)}); }
void Renderer::resetEffects() { particles_.clear(); bursts_.clear(); beams_.clear(); weaponEffects_.clear(); trails_.clear(); lastEvent_ = 0; lastBeam_ = 0; round_ = 0; shake_ = 0; }
void Renderer::update(float dt) {
    time_ += dt; shake_ = std::max(0.f, shake_ - dt * 20);
    for (auto& p : particles_) { p.life -= dt; p.position += p.velocity * dt; p.velocity.y += 180 * dt; p.velocity = p.velocity * std::exp(-dt * 0.8f); }
    for (auto& b : bursts_) b.age += dt;
    for (auto& beam : beams_) beam.age += dt;
    for (auto& effect : weaponEffects_) effect.age += dt;
    std::erase_if(particles_, [](const auto& p) { return p.life <= 0; });
    std::erase_if(bursts_, [](const auto& b) { return b.age >= b.duration; });
    std::erase_if(beams_, [](const auto& beam) { return beam.age >= 0.32f; });
    std::erase_if(weaponEffects_, [](const auto& effect) { return effect.age >= effect.duration; });
}
void Renderer::observe(const Snapshot& state) {
    if (state.round != round_) { resetEffects(); round_ = state.round; }
    for (const auto& beam : state.lasers) {
        if (beam.id <= lastBeam_) continue;
        lastBeam_ = beam.id;
        const auto* owner = findTank(state,beam.owner);
        beams_.push_back({beam.from,beam.to,owner ? playerColor(owner->color) : accent});
    }
    for (const auto& e : state.events) {
        if (e.id <= lastEvent_) continue;
        lastEvent_ = e.id;
        if (e.kind==EventKind::SupplyLanded || e.kind==EventKind::Pickup || e.kind==EventKind::FlareImpact) { bursts_.push_back({e.position,e.kind==EventKind::Pickup ? accent : (e.kind==EventKind::FlareImpact ? sf::Color{255,188,64} : sf::Color{219,188,114}),0,0.7f,45}); continue; }
        if (e.kind==EventKind::SkyStrike) { bursts_.push_back({e.position,{255,211,93},0,0.7f,e.radius*1.5f}); shake_=std::max(shake_,7.f); continue; }
        if (e.kind==EventKind::MortarImpact || e.kind==EventKind::OrbImpact || e.kind==EventKind::Ricochet || e.kind==EventKind::MissileImpact || e.kind==EventKind::PlaneCrash) { addWeaponEffect(e); continue; }
        const bool shot = e.kind == EventKind::Shot, death = e.kind == EventKind::Destroyed;
        const auto* owner = findTank(state, e.player);
        const bool napalm = e.kind == EventKind::NapalmImpact || (shot && owner && owner->weapon == WeaponKind::Napalm);
        sf::Color color = death ? sf::Color{255,150,105} : sf::Color{144,255,228};
        if (const auto* t = findTank(state, e.player)) color = playerColor(t->color);
        if (napalm) color = {255,133,42};
        if (shot && owner && owner->weapon==WeaponKind::Mortar) color={255,158,70};
        if (shot && owner && owner->weapon==WeaponKind::Missile) color={255,104,48};
        if (shot && owner && owner->weapon==WeaponKind::GrowingOrb) color={147,126,255};
        if (shot && owner && owner->weapon==WeaponKind::Ricochet) color={147,126,255};
        if (shot && owner && owner->weapon==WeaponKind::Flare) color={255,208,82};
        bursts_.push_back({e.position, color, 0, shot ? 0.17f : 0.85f, shot ? 27.f : (death ? 145.f : (napalm ? 40.f : 88.f))});
        if (!shot) shake_ = std::max(shake_, death ? 7.f : (napalm ? 0.8f : 3.5f));
        if (!settings.particles) continue;
        std::mt19937 random(e.id * 731 + 19);
        std::uniform_real_distribution<float> unit(0,1);
        const int count = napalm ? (shot ? 4 : 12) : (shot ? 10 : (death ? 100 : 54));
        for (int i = 0; i < count; ++i) {
            const float angle = unit(random) * 2 * pi, life = 0.25f + unit(random) * (death ? 1.5f : 0.8f);
            particles_.push_back({e.position, direction(angle) * (30 + unit(random) * (death ? 380 : 220)), color, life, life, 1 + unit(random) * 2.5f});
        }
    }
}
void Renderer::tank(const Tank& t, const Config& c, float scale) {
    const auto color = playerColor(t.color);
    sf::RenderStates states;
    states.transform.translate(sfv(t.position)).rotate(sf::radians(t.bodyAngle)).scale({scale,scale});
    auto rectangle = [&](Vec2 p, Vec2 size, sf::Color fill, sf::Color stroke = sf::Color::Transparent) {
        sf::RectangleShape s(sfv(size)); s.setPosition(sfv(p)); s.setFillColor(fill); s.setOutlineColor(stroke); s.setOutlineThickness(stroke.a ? 1 : 0); window_.draw(s,states);
    };
    sf::CircleShape shadow(33); shadow.setOrigin({33,33}); shadow.setScale({scale,scale * 0.13f}); shadow.setPosition(sfv(t.position + Vec2{0,11 * scale})); shadow.setFillColor({0,0,0,65}); window_.draw(shadow);
    rectangle({-c.tankHalfWidth,-5}, {c.tankHalfWidth * 2,17}, {8,15,25}, {75,102,116});
    for (int i = 0; i < 6; ++i) {
        sf::CircleShape wheel(4,12); wheel.setOrigin({4,4}); wheel.setPosition({-20.f + i * 8,3.5f}); wheel.setFillColor({51,73,87}); window_.draw(wheel,states);
        const float a = t.track + i;
        const auto from = Vec2{-20.f + i * 8,3.5f};
        sf::VertexArray spokes(sf::PrimitiveType::Lines,2);
        spokes[0] = {sfv(from), {114,150,162}}; spokes[1] = {sfv(from + direction(a) * 3), {114,150,162}}; window_.draw(spokes,states);
    }
    for (int i = 0; i < 12; ++i) {
        const float x = -25 + std::fmod(i * 4.5f + t.track * 2 + 10000.f, 52.f);
        rectangle({x,-5}, {2,2}, {130,159,169}); rectangle({x,10}, {2,2}, {130,159,169});
    }
    sf::ConvexShape hull(6);
    hull.setPoint(0,{-27,-8}); hull.setPoint(1,{-19,-17}); hull.setPoint(2,{16,-17}); hull.setPoint(3,{27,-8}); hull.setPoint(4,{23,0}); hull.setPoint(5,{-23,0});
    hull.setFillColor(t.alive() ? sf::Color{33,57,67} : sf::Color{24,30,37}); hull.setOutlineColor(t.alive() ? alpha(color,180) : muted); hull.setOutlineThickness(1); window_.draw(hull,states);
    rectangle({-17,-13}, {32,3}, alpha(color,t.alive() ? 220 : 30));
    rectangle({-11,-6}, {6,2}, {93,125,137}); rectangle({0,-6}, {13,2}, {93,125,137});
    if (t.titan && t.alive()) {
        rectangle({-26,-11},{13,10},{15,28,40},alpha(color,210));
        rectangle({13,-11},{13,10},{15,28,40},alpha(color,210));
        for (int i = 0; i < 3; ++i) {
            rectangle({-23.f + i*3,-9},{1,6},alpha(color,130 + 80*std::sin(time_*4+i)));
            rectangle({16.f + i*3,-9},{1,6},alpha(color,130 + 80*std::sin(time_*4+i)));
        }
        rectangle({-14,-18},{28,4},{63,87,98},alpha(color,130));
    }
    if (!t.alive()) { rectangle({-8,-20}, {17,6}, {31,39,48}); return; }
    const auto pivot = t.position + rotate({0,-12}, t.bodyAngle) * scale;
    const auto barrelStart = pivot + direction(t.aim) * (3 * scale);
    const auto barrelEnd = pivot + direction(t.aim) * ((c.weapon.barrelLength - t.recoil * 5) * scale);
    line(barrelStart, barrelEnd, {17,29,40}, (t.titan ? 12 : 8) * scale);
    line(barrelStart, barrelEnd, alpha(color,220), 3 * scale);
    line(barrelEnd - direction(t.aim) * (5 * scale), barrelEnd + direction(t.aim) * scale, {181,206,211}, 7 * scale);
    circle(pivot, 10 * scale, {34,58,69}, alpha(color,200), scale);
    circle(pivot, 3 * scale, color);
    if (t.weapon == WeaponKind::Mortar) {
        const auto normal=direction(t.aim+pi/2);
        line(barrelStart,barrelEnd,{38,42,51},16*scale);
        line(barrelStart+normal*(5*scale),barrelEnd+normal*(5*scale),{255,163,83},2*scale);
        line(barrelStart-normal*(5*scale),barrelEnd-normal*(5*scale),{120,73,57},2*scale);
        line(barrelEnd-normal*(9*scale),barrelEnd+normal*(9*scale),{241,182,122},4*scale);
        glow(barrelEnd,14*scale,{255,128,48},45+40*t.recoil);
        circle(pivot,6*scale,{255,172,82});
    } else if (t.weapon == WeaponKind::Missile) {
        const auto ray=direction(t.aim), normal=direction(t.aim+pi/2);
        line(barrelStart,barrelEnd,{43,55,65},15*scale);
        for (float side : {-1.f,1.f}) line(barrelStart+normal*(side*5*scale),barrelEnd+normal*(side*5*scale),{194,177,135},2*scale);
        // Compact red designator physically mounted above the launch tube.
        const Vec2 emitter=barrelEnd-normal*(7*scale);
        line(emitter-ray*(13*scale),emitter,{27,36,46},5*scale);
        circle(emitter,2.4f*scale,{255,66,54});
        glow(emitter,9*scale,{255,36,24},75);
        line(barrelEnd-normal*(8*scale),barrelEnd+normal*(8*scale),{211,205,178},3*scale);
        circle(pivot,5*scale,{231,125,76});
    } else if (t.weapon == WeaponKind::Ricochet) {
        const auto normal=direction(t.aim+pi/2);
        for (float side : {-1.f,1.f}) {
            line(barrelStart+normal*(side*4*scale),barrelEnd+normal*(side*4*scale),{82,157,192},3*scale);
            line(barrelEnd+normal*(side*4*scale),barrelEnd+direction(t.aim)*(5*scale)+normal*(side*6*scale),{172,228,255},2*scale);
        }
        for (int i=0;i<2;++i) circle(pivot+direction(t.aim)*((14+i*10.f)*scale),2.5f*scale,{139,215,255});
        glow(barrelEnd,18*scale,{78,184,255},65+100*t.recoil);
    } else if (t.weapon == WeaponKind::GrowingOrb) {
        const auto normal=direction(t.aim+pi/2);
        for (int i=0;i<3;++i) {
            const auto coil=pivot+direction(t.aim)*((13+i*8.f-t.recoil*3)*scale);
            line(coil-normal*(6*scale),coil+normal*(6*scale),{151,132,255},3*scale);
        }
        glow(barrelEnd,19*scale,{111,125,255},70+60*t.recoil);
        ring(barrelEnd,7*scale,{172,223,255,230},time_*3,pi*1.6f,0.65f,t.aim);
        circle(pivot,5*scale,{174,152,255});
    } else if (t.weapon == WeaponKind::Flare) {
        const auto normal=direction(t.aim+pi/2);
        line(barrelStart,barrelEnd,{68,48,30},14*scale);
        for (float side : {-1.f,1.f}) line(barrelStart+normal*(side*5*scale),barrelEnd+normal*(side*5*scale),{255,184,56},2*scale);
        ring(barrelEnd,8*scale,{255,213,104,230},time_*2,2*pi,0.65f,t.aim);
        glow(barrelEnd,24*scale,{255,162,32},65+120*t.recoil);
        circle(pivot,5*scale,{255,209,90});
    } else if (t.weapon == WeaponKind::Napalm) {
        const auto normal = direction(t.aim + pi/2);
        for (int i = -1; i <= 1; ++i) {
            line(barrelStart + normal * (i * 4.f * scale), barrelEnd + normal * (i * 4.f * scale), {255,132,45}, 2 * scale);
        }
        circle(pivot, 5 * scale, {255,160,48});
    } else if (t.titan) {
        const auto normal = direction(t.aim + pi/2);
        for (int i = 0; i < 3; ++i) {
            const auto coil = pivot + direction(t.aim) * ((12 + i*8.f - t.recoil*3) * scale);
            line(coil-normal*(5*scale),coil+normal*(5*scale),alpha(color,200),2*scale);
        }
        circle(pivot,14*scale,sf::Color::Transparent,alpha(color,100),scale);
        circle(pivot,7*scale,alpha(color,80)); circle(pivot,3.5f*scale,white);
        for (int i = 0; i < 3; ++i) circle(pivot+direction(time_*1.4f+i*2*pi/3)*(17*scale),1.5f*scale,color);
        circle(t.position,40*scale,sf::Color::Transparent,alpha(color,22+8*std::sin(time_*3)));
    }
    line(t.position + rotate({-18,-16},t.bodyAngle) * scale, t.position + rotate({-20,-29},t.bodyAngle) * scale, alpha(color,150), scale);
}
void Renderer::flame(Vec2 position, float size, float phase) {
    const float flicker = 0.8f + 0.2f * std::sin(time_ * 19 + phase);
    const float sway = std::sin(time_ * 11 + phase) * size * 0.3f;
    circle(position + Vec2{0,-size * 0.3f}, size * 1.15f, {255,95,20,17});
    sf::ConvexShape shape(5);
    shape.setPoint(0, sfv(position + Vec2{-size * 0.5f,0}));
    shape.setPoint(1, sfv(position + Vec2{-size * 0.4f,-size * 0.5f}));
    shape.setPoint(2, sfv(position + Vec2{sway,-size * 1.9f * flicker}));
    shape.setPoint(3, sfv(position + Vec2{size * 0.55f,-size * 0.55f}));
    shape.setPoint(4, sfv(position + Vec2{size * 0.5f,0}));
    shape.setFillColor({255,100,25,205}); window_.draw(shape);
    circle(position + Vec2{0,-size * 0.3f}, size * 0.3f, {255,218,94,235});
    if (settings.particles) {
        const float rise = std::fmod(time_ * 25 + phase * 9, 55.f);
        circle(position + Vec2{sway * 2,-size - rise}, 3 + rise * 0.08f, alpha({94,91,105}, 55 - rise));
    }
}
void Renderer::effects() {
    weaponEffects();
    for (const auto& beam : beams_) {
        const float fade = 1-beam.age/0.32f;
        line(beam.from,beam.to,alpha(beam.color,32*fade),38*fade);
        line(beam.from,beam.to,alpha(beam.color,95*fade),18*fade);
        line(beam.from,beam.to,alpha(beam.color,250*fade),7*fade);
        line(beam.from,beam.to,alpha(white,255*fade),3*fade);
        const auto delta = beam.to-beam.from;
        const auto normal = direction(std::atan2(delta.y,delta.x)+pi/2);
        Vec2 previous = beam.from;
        for (int i = 1; i <= 18; ++i) {
            const float t = static_cast<float>(i)/18;
            const auto p = mix(beam.from,beam.to,t)+normal*(std::sin(time_*80+i*4)*std::sin(t*pi)*4*fade);
            line(previous,p,alpha(beam.color,150*fade),0.9f);
            previous = p;
        }
        circle(beam.from,12*fade,alpha(beam.color,90*fade));
        circle(beam.to,8*fade,alpha(white,220*fade));
    }
    for (const auto& b : bursts_) {
        const float t = b.age / b.duration;
        circle(b.position, b.radius * (0.2f + 0.8f * t), alpha(b.color, (1 - t) * 12), alpha(b.color, (1 - t) * 210), 1 + (1 - t) * 3);
        if (t < 0.3f) circle(b.position, 20 * (1 - t / 0.3f), alpha(white,(1 - t / 0.3f) * 240));
        circle(b.position, b.radius * 0.7f * t, sf::Color::Transparent, alpha(b.color,(1 - t) * 55));
    }
    if (settings.particles) for (const auto& p : particles_) {
        const float t = p.life / p.total;
        line(p.position - p.velocity * 0.018f, p.position, alpha(p.color,t * 200), p.size * t);
        circle(p.position, p.size * t, alpha(white,t * 230));
    }
}
void Renderer::battle(const Config& c, const Snapshot& s, PlayerId local, Vec2 mouse, bool cursor) {
    auto camera = worldView_;
    if (settings.shake) camera.move({std::sin(time_ * 131) * shake_, std::cos(time_ * 107) * shake_ * 0.7f});
    window_.setView(camera);
    auto map = MapFactory::create(c,s.terrain);
    supplies(c,s);
    for (const auto& fire : s.fires) {
        const float strength = std::min(1.f, fire.remaining / 0.6f);
        for (float dx = -fire.radius; dx <= fire.radius; dx += 12) {
            const float x = std::clamp(fire.position.x + dx, 0.f, c.worldWidth);
            const float size = (9 + 7 * (1 - std::abs(dx) / fire.radius)) * strength;
            flame({x,map->height(x) - 2}, size, static_cast<float>(fire.id) + dx);
        }
    }
    for (const auto& t : s.tanks) {
        if (!t.alive() || t.weapon!=WeaponKind::Missile || !std::ranges::any_of(s.projectiles,[&](const auto& p) { return p.weapon==WeaponKind::Missile && p.owner==t.id; })) continue;
        const auto pointer=Physics::guidanceTrace(t,c,*map,s.tanks);
        line(pointer.from,pointer.to,{255,29,21,35},5);
        line(pointer.from,pointer.to,{255,55,42,170},1.2f);
        glow(pointer.to,14,{255,39,25},95);
        circle(pointer.to,2.5f,{255,175,134});
        ring(pointer.to,6,{255,65,43,190},time_*3,2*pi,0.6f);
    }
    if (const auto* t = findTank(s,local); t && t->alive() && s.phase == Phase::Playing && settings.trajectory && t->weapon!=WeaponKind::Missile) {
        const auto points = Physics::trajectory(*t,c,*map,s.tanks);
        sf::VertexArray dots(sf::PrimitiveType::Points);
        float distance = 0;
        for (std::size_t i = 1; i < points.size(); ++i) {
            const float segment = length(points[i] - points[i-1]);
            if (segment < 0.01f) continue;
            for (float along = 0; along < segment; along += 1.5f) {
                if (std::fmod(distance + along, 15.f) > 4) continue;
                const auto p = mix(points[i-1],points[i],along / segment);
                dots.append({sfv(p),{230,243,250,115}});
            }
            distance += segment;
        }
        window_.draw(dots);
        if (!points.empty()) { circle(points.back(),5,sf::Color::Transparent,{230,243,250,115}); line(points.back() - Vec2{9,0},points.back() + Vec2{9,0},{230,243,250,65}); }
    }
    std::erase_if(trails_,[&](const auto& entry) { return std::ranges::none_of(s.projectiles,[&](const auto& p) { return p.id == entry.first; }); });
    for (const auto& p : s.projectiles) {
        auto& trail = trails_[p.id];
        if (trail.empty() || length(p.position - trail.back()) > 3) trail.push_back(p.position);
        if (trail.size() > 42) trail.erase(trail.begin());
        if (p.weapon==WeaponKind::Mortar || p.weapon==WeaponKind::GrowingOrb || p.weapon==WeaponKind::Ricochet || p.weapon==WeaponKind::Napalm || p.weapon==WeaponKind::Flare || p.weapon==WeaponKind::Missile) { weaponProjectile(p,c,trail); continue; }
        const sf::Color color = p.napalm ? sf::Color{255,135,38} : accent;
        for (std::size_t i = 1; i < trail.size(); ++i) line(trail[i-1],trail[i],alpha(color,180.f * static_cast<float>(i) / trail.size()),p.napalm ? 3 : 2);
        circle(p.position,9,alpha(color,24)); circle(p.position,5,alpha(color,95)); circle(p.position,c.weapon.projectileRadius,p.napalm ? sf::Color{255,235,156} : white);
    }
    for (const auto& t : s.tanks) {
        tank(t,c);
        if (!t.alive()) continue;
        if (t.canFly && t.position.y < map->height(t.position.x) - c.tankHalfHeight - 2) {
            const float exhaust = 16 + 9 * std::sin(time_ * 28 + t.id);
            for (float dx : {-18.f,18.f}) {
                line(t.position + Vec2{dx,8}, t.position + Vec2{dx,8 + exhaust}, {99,187,255,95}, 10);
                line(t.position + Vec2{dx,8}, t.position + Vec2{dx,8 + exhaust * 0.7f}, {190,239,255,230}, 3);
            }
        }
        if (t.inMud) {
            for (int i=0;i<7;++i) { const float a=time_*8+i*2.3f; const Vec2 p=t.position+Vec2{std::sin(a)*32,9-std::abs(std::cos(a))*9}; circle(p,2.2f,{105,82,48,190}); }
            line(t.position+Vec2{-26,10},t.position+Vec2{26,10},{65,54,35,190},5);
        }
        if (t.shieldRemaining>0) {
            glow(t.position,64,{70,165,255},35);
            ring(t.position,43,{99,194,255,160},time_*0.7f,2*pi,0.8f);
            ring(t.position,47,{183,232,255,100},-time_*2,pi*1.2f,0.8f);
        }
        if (t.damageRemaining>0) glow(t.pivot(),38,{255,136,42},36+15*std::sin(time_*5));
        if (t.burnRemaining > 0) for (int i = 0; i < 4; ++i) flame(t.position + Vec2{-19.f + i * 12,-9}, 10, t.id * 10.f + i);
        const auto col = playerColor(t.color);
        box(t.position + Vec2{-25,-49},{50,3},{33,47,60}); box(t.position + Vec2{-25,-49},{50 * t.healthRatio(),3},col);
        if (t.id == local) { text("YOU",t.position + Vec2{0,-80},17,col,true); line(t.position + Vec2{-3,-58},t.position + Vec2{0,-55},col); line(t.position + Vec2{0,-55},t.position + Vec2{3,-58},col); }
    }
    skyStrikes(c,s);
    effects();
    if (cursor && s.phase == Phase::Playing) {
        circle(mouse,10,sf::Color::Transparent,alpha(white,130));
        line(mouse + Vec2{-16,0},mouse + Vec2{-7,0},white); line(mouse + Vec2{7,0},mouse + Vec2{16,0},white);
        line(mouse + Vec2{0,-16},mouse + Vec2{0,-7},white); line(mouse + Vec2{0,7},mouse + Vec2{0,16},white); circle(mouse,1.5f,accent);
    }
    window_.setView(uiView_);
}
void Renderer::hud(const Config& c, const Snapshot& s, PlayerId local) {
    text("T / W",{42,24},28,white);
    text(MapFactory::name(c.mapId),{134,34},16,muted);
    const auto total=std::max(0,static_cast<int>(std::ceil(matchDuration-s.elapsed)));
    std::ostringstream clock; clock << std::setfill('0') << std::setw(2) << total/60 << ':' << std::setw(2) << total%60;
    text(clock.str(),{800,29},25,total<=30 ? playerColor(1) : white,true);
    line({42,73},{1558,73},{44,65,82});
    const float cardWidth = s.players.size() > 2 ? 275.f : 320.f;
    for (const auto& p : s.players) {
        const auto* t = findTank(s,p.id);
        const float x = s.players.size() <= 2 ? (p.seat == 0 ? 42.f : 1238.f) : 42.f + p.seat * 393.f;
        const auto color = playerColor(p.color);
        box({x,94},{cardWidth,83},{11,22,36,225},{39,58,75}); box({x,94},{3,83},color);
        auto name=p.name; if (name.size()>18) name=name.substr(0,18);
        text(name,{x+18,104},19,color);
        const auto hp = t ? std::to_string(static_cast<int>(std::ceil(t->hp))) : "--";
        text(hp,{x+cardWidth-20-static_cast<float>(hp.size())*14,99},28,white);
        box({x+18,146},{cardWidth-36,5},{38,54,69});
        if (t) {
            box({x+18,146},{(cardWidth-36)*t->healthRatio(),5},color);
            const auto perk=[&](PickupKind kind,float remaining,float duration,Vec2 at,sf::Color ink) {
                if (remaining<=0) return;
                circle(at,9,alpha(ink,35),alpha(ink,100));
                ring(at,11,ink,-pi/2,2*pi*std::clamp(remaining/duration,0.f,1.f));
                perkIcon(kind,at,5,ink);
                text(std::to_string(static_cast<int>(std::ceil(remaining))),at+Vec2{16,-9},13,ink);
            };
            perk(PickupKind::Damage,t->damageRemaining,15,{x+31,165},{255,160,61});
            perk(PickupKind::Shield,t->shieldRemaining,10,{x+110,165},{91,186,255});
            perk(PickupKind::Health,t->healIndicator,1.5f,{x+189,165},{108,245,158});
        }
    }
    const auto* tank = findTank(s,local);
    if (tank) {
        const auto color = playerColor(tank->color);
        box({510,760},{580,104},{10,21,35,240},{43,65,81});
        text(weaponName(tank->weapon),{535,774},16,muted);
        const float power = tank->weapon==WeaponKind::Laser ? 1.f : std::clamp((tank->power-c.weapon.minPower)/(c.weapon.maxPower-c.weapon.minPower),0.f,1.f);
        text("POWER",{535,805},17,white);
        text(std::to_string(static_cast<int>(power*100))+"%",{735,801},24,color);
        box({535,842},{230,4},{39,57,70}); box({535,842},{230*power,4},color);
        line({795,783},{795,845},{42,63,79});
        const bool guiding=tank->weapon==WeaponKind::Missile && std::ranges::any_of(s.projectiles,[&](const auto& p){return p.owner==tank->id && p.weapon==WeaponKind::Missile;});
        const float loaded=guiding ? 0.f : 1-tank->reload/Cannon::reload(tank->weapon,c.weapon);
        text(guiding ? "GUIDING" : (tank->reload>0 ? "RELOADING" : "READY"),{825,779},20,tank->reload>0 ? muted : color);
        if (tank->reload>0 && !guiding) text(std::to_string(tank->reload).substr(0,3)+" s",{825,807},21,white);
        box({825,842},{235,4},{39,57,70}); box({825,842},{235*std::clamp(loaded,0.f,1.f),4},color);
    }
}
}
