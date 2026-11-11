#include "tankwars/Renderer.hpp"
#include "tankwars/Weapon.hpp"
#include <random>

namespace tw {
namespace {
sf::Vector2f point(Vec2 v) { return {v.x,v.y}; }
sf::Color fade(sf::Color c,float opacity) { c.a=static_cast<std::uint8_t>(std::clamp(opacity,0.f,255.f)); return c; }
}
void Renderer::glow(Vec2 position,float radius,sf::Color color,float opacity) {
    if (radius<0.1f || opacity<0.1f) return;
    sf::VertexArray fan(sf::PrimitiveType::TriangleFan);
    fan.append({point(position),fade(color,opacity)});
    for (int i=0;i<=48;++i) fan.append({point(position+direction(i*2*pi/48)*radius),fade(color,0)});
    window_.draw(fan,sf::RenderStates(sf::BlendAdd));
}
void Renderer::ring(Vec2 position,float radius,sf::Color color,float start,float sweep,float squash,float tilt) {
    sf::VertexArray vertices(sf::PrimitiveType::LineStrip);
    for (int i=0;i<=64;++i) {
        const float angle=start+sweep*i/64;
        vertices.append({point(position+rotate({std::cos(angle)*radius,std::sin(angle)*radius*squash},tilt)),color});
    }
    window_.draw(vertices,sf::RenderStates(sf::BlendAdd));
}
void Renderer::weaponProjectile(const Projectile& p,const Config& c,const std::vector<Vec2>& trail) {
    const bool orb=p.weapon==WeaponKind::GrowingOrb;
    const float charge=orb ? Cannon::orbCharge(p.age,c.weapon) : 0.f;
    const float radius=Cannon::radius(p,c.weapon);
    const sf::Color color=orb ? sf::Color{146,119,255} : sf::Color{255,131,45};
    sf::VertexArray ribbon(sf::PrimitiveType::Triangles);
    for (std::size_t i=1;i<trail.size();++i) {
        const float t=static_cast<float>(i)/static_cast<float>(trail.size());
        const auto delta=trail[i]-trail[i-1];
        const auto normal=direction(std::atan2(delta.y,delta.x)+pi/2)*(orb ? radius*0.55f*t : 3.f*t);
        const auto ink=fade(color,t*t*(orb ? 110 : 145));
        for (auto v : {trail[i-1]-normal,trail[i-1]+normal,trail[i]+normal,trail[i-1]-normal,trail[i]+normal,trail[i]-normal}) ribbon.append({point(v),ink});
    }
    window_.draw(ribbon,sf::RenderStates(sf::BlendAdd));
    if (orb) {
        glow(p.position,radius*3.8f,{116,82,255},40+charge*45);
        glow(p.position,radius*2.1f,{88,202,255},100);
        circle(p.position,radius,fade({67,51,146},200),fade({173,151,255},210));
        sf::VertexArray corona(sf::PrimitiveType::LineStrip);
        for (int i=0;i<=80;++i) {
            const float a=i*2*pi/80;
            const float ripple=1+0.07f*std::sin(a*7+time_*9+p.id)+0.035f*std::sin(a*13-time_*13);
            corona.append({point(p.position+direction(a)*(radius*ripple)),{171,202,255,190}});
        }
        window_.draw(corona,sf::RenderStates(sf::BlendAdd));
        ring(p.position,radius*1.3f,{174,129,255,185},time_*2.5f,pi*1.7f,0.43f,time_*0.9f+p.id);
        ring(p.position,radius*1.18f,{111,237,255,210},-time_*3,pi*1.65f,0.55f,-time_*1.1f);
        glow(p.position,radius*0.95f,{110,225,255},210);
        glow(p.position+Vec2{-radius*0.15f,-radius*0.16f},radius*0.52f,{220,242,255},230);
        circle(p.position,radius*0.17f,{243,245,255,245});
        for (int i=0;i<5;++i) {
            const float a=time_*(1.4f+i*0.12f)+i*2*pi/5+p.id;
            const Vec2 mote=p.position+direction(a)*(radius*(1.4f+0.12f*std::sin(time_*4+i)));
            glow(mote,4+charge*3,{148,196,255},180);
            circle(mote,1+charge,{220,235,255,235});
        }
        // The two orbiting pips disappear as the two ricochets are spent.
        for (unsigned i=p.bounces;i<2;++i) circle(p.position+Vec2{(static_cast<float>(i)*2-1)*5,-radius-11},1.8f,{207,191,255,210});
    } else {
        if (settings.particles) for (std::size_t i=0;i<trail.size();i+=5) {
            const float t=static_cast<float>(i)/static_cast<float>(trail.size());
            circle(trail[i]+Vec2{std::sin(i+time_*2)*4,-(1-t)*10},3+(1-t)*6,fade({84,78,88},t*65));
            if (i%10==0) glow(trail[i],5,{255,148,64},t*65);
        }
        const float angle=std::atan2(p.velocity.y,p.velocity.x);
        glow(p.position,25,{255,122,38},80);
        const std::array<Vec2,6> hull{{{-7,-5},{4,-5},{10,0},{4,5},{-7,5},{-4,0}}};
        sf::ConvexShape shell(hull.size());
        for (std::size_t i=0;i<hull.size();++i) shell.setPoint(i,point(p.position+rotate(hull[i],angle)));
        shell.setFillColor({55,40,38}); shell.setOutlineColor({255,189,114}); shell.setOutlineThickness(1); window_.draw(shell);
        line(p.position+rotate({-10,-7},angle),p.position+rotate({-3,0},angle),{252,139,60},2);
        line(p.position+rotate({-10,7},angle),p.position+rotate({-3,0},angle),{252,139,60},2);
        glow(p.position+rotate({5,0},angle),7,{255,216,122},170);
        circle(p.position+rotate({5,0},angle),2,{255,235,174});
    }
}
void Renderer::addWeaponEffect(const GameEvent& e) {
    const bool mortar=e.kind==EventKind::MortarImpact, bounce=e.kind==EventKind::Ricochet;
    weaponEffects_.push_back({e.position,e.kind,0,bounce ? 0.55f : (mortar ? 1.8f : 1.35f),e.radius,e.intensity,e.id});
    shake_=std::max(shake_,bounce ? 1.4f : (mortar ? 5.2f : 2+3*e.intensity));
    if (!settings.particles) return;
    std::mt19937 random(e.id*977+31);
    std::uniform_real_distribution<float> unit(0,1);
    const int count=bounce ? 22 : (mortar ? 84 : 52);
    for (int i=0;i<count;++i) {
        const float angle=unit(random)*2*pi;
        const float speed=(bounce ? 50 : 70)+unit(random)*(bounce ? 170 : 280);
        const float life=0.3f+unit(random)*(mortar ? 1.1f : 0.7f);
        const sf::Color color=mortar ? (i%3 ? sf::Color{255,155,64} : sf::Color{255,223,139}) : (i%2 ? sf::Color{163,125,255} : sf::Color{132,226,255});
        particles_.push_back({e.position,direction(angle)*speed+Vec2{0,mortar ? -60.f : -15.f},color,life,life,1+unit(random)*2.5f});
    }
}
void Renderer::weaponEffects() {
    for (const auto& e : weaponEffects_) {
        const float t=e.age/e.duration;
        const bool mortar=e.kind==EventKind::MortarImpact, bounce=e.kind==EventKind::Ricochet;
        const sf::Color color=mortar ? sf::Color{255,137,49} : sf::Color{156,130,255};
        const float radius=std::max(10.f,e.radius);
        const float expand=1-std::exp(-t*9);
        if (bounce) {
            glow(e.position,radius*(1.5f+t*3),{139,125,255},120*(1-t));
            ring(e.position,radius*(0.5f+t*2),fade({165,219,255},230*(1-t)));
            for (int i=0;i<8;++i) {
                const auto ray=direction(i*2*pi/8+e.seed);
                line(e.position+ray*(radius*t),e.position+ray*(radius*(0.5f+t*2)),fade(color,(1-t)*180),2*(1-t));
            }
            continue;
        }
        if (mortar) {
            // Smoke rises behind the hot lobes while the fireball cools to embers.
            for (int i=0;i<9;++i) {
                const float a=i*2*pi/9+e.seed;
                const Vec2 p=e.position+direction(a)*(radius*0.45f*expand)+Vec2{std::sin(t*5+i)*9,-t*radius*0.85f};
                circle(p,radius*(0.17f+t*0.28f),fade({61,57,67},80*std::sin(pi*t)));
            }
        }
        glow(e.position,radius*(1.8f+expand),color,100*std::pow(1-t,2));
        const float shock=std::clamp(e.age/0.6f,0.f,1.f);
        ring(e.position,radius*(0.12f+1.05f*(1-std::pow(1-shock,3))),fade(color,240*std::pow(1-shock,1.5f)));
        ring(e.position,radius*(0.2f+expand*1.6f),fade(color,80*std::pow(1-t,3)),0,2*pi,0.23f);
        const float hot=std::max(0.f,1-e.age/(mortar ? 0.95f : 0.7f));
        const float fireSize=radius*(0.18f+expand*0.55f)*hot;
        if (mortar) {
            for (int i=0;i<12;++i) {
                const float a=i*2*pi/12+e.seed;
                const Vec2 p=e.position+direction(a)*(fireSize*(0.6f+0.15f*std::sin(i*4.f)));
                glow(p,fireSize*0.8f,{255,94,22},110*hot);
                circle(p,fireSize*(0.32f+0.08f*std::sin(e.age*18+i)),fade({255,119,36},190*hot));
                if (i%2==0 && hot>0.1f) flame(p+Vec2{0,fireSize*0.15f},fireSize*0.4f,i+e.seed);
            }
            glow(e.position,fireSize*1.3f,{255,209,105},220*hot);
        } else {
            ring(e.position,fireSize*1.2f,fade({115,232,255},220*hot),time_*4,pi*1.7f,0.5f,e.seed+e.age*2);
            for (int i=0;i<12;++i) {
                const float a=i*2*pi/12+e.seed;
                const auto ray=direction(a);
                const auto middle=e.position+direction(a+0.07f*std::sin(i*3+time_*16))*(radius*expand*0.65f);
                line(e.position+ray*(radius*expand*0.15f),middle,fade({139,221,255},180*hot),1.4f);
                line(middle,e.position+ray*(radius*expand),fade(color,180*hot),1);
            }
            glow(e.position,fireSize*1.6f,{113,224,255},200*hot);
        }
        const float flash=std::max(0.f,1-e.age/0.13f);
        glow(e.position,radius*0.7f,{255,237,205},230*flash);
        circle(e.position,radius*0.23f*flash,fade({255,246,223},240*flash));
    }
}
}
