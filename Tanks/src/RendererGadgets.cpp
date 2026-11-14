#include "tankwars/Renderer.hpp"
#include "tankwars/Weapon.hpp"

namespace tw {
namespace {
sf::Color faded(sf::Color c,float alpha) { c.a=static_cast<std::uint8_t>(std::clamp(alpha,0.f,255.f)); return c; }
}
void Renderer::updateBanner(float time) {
    const Vec2 center{1338,112};
    const float pulse=1+0.045f*std::sin(time*3.4f);
    sf::RectangleShape panel({350,70}); panel.setOrigin({175,35}); panel.setPosition({center.x,center.y});
    panel.setRotation(sf::degrees(-7)); panel.setScale({pulse,pulse}); panel.setFillColor({18,44,48,240});
    panel.setOutlineColor({117,246,204}); panel.setOutlineThickness(2); window_.draw(panel);
    sf::Text label(font_,"GADGETS ADDED!",32); const auto bounds=label.getLocalBounds();
    label.setOrigin(bounds.position+bounds.size/2.f); label.setPosition({center.x,center.y});
    label.setRotation(sf::degrees(-7)); label.setScale({pulse,pulse}); label.setFillColor({180,255,197}); window_.draw(label);
}
void Renderer::gadgetPreview(GadgetKind kind,WeaponKind weapon,Vec2 center,float time) {
    if (kind==GadgetKind::Jetpack) {
        for (float side : {-1.f,1.f}) {
            const Vec2 p=center+Vec2{side*22,0};
            glow(p,26,{57,167,255},65);
            line(p-Vec2{0,20},p+Vec2{0,5},{143,196,215},10);
            line(p+Vec2{0,5},p+Vec2{0,30+4*std::sin(time*24)},{48,161,255,140},12);
            line(p+Vec2{0,5},p+Vec2{0,23},{187,242,255},4);
        }
    } else if (kind==GadgetKind::Armor) {
        for (int i=-1;i<=1;++i) {
            const Vec2 p=center+Vec2{i*25.f,-17.f+std::abs(i)*5};
            box(p-Vec2{10,0},{22,39},{87,112,128},{175,205,216});
            line(p+Vec2{-7,7},p+Vec2{9,7},{165,192,199},3);
            for (float y : {13.f,32.f}) circle(p+Vec2{1,y},2,{31,51,63});
        }
    } else {
        weaponPreview(weapon,center,time);
        ring(center,51,{135,233,255,150},time*0.8f,pi*1.5f,0.65f,-0.3f);
        circle(center+Vec2{47,-24},12,{25,56,68},accent);
        text("+",center+Vec2{47,-39},24,accent,true);
    }
}
void Renderer::tankWear(const Tank& t,const Config& c,const Terrain& map,float time) {
    if (!t.alive() || !settings.particles) return;
    const bool grounded=t.position.y>=map.height(t.position.x)-c.tankHalfHeight-1;
    if (grounded && std::abs(t.velocity.x)>4) {
        const float rear=t.velocity.x>0 ? -1.f : 1.f;
        const sf::Color soil=c.mapId==0 ? sf::Color{162,111,58} : (c.mapId==1 ? sf::Color{47,85,66} : (c.mapId==2 ? sf::Color{78,91,54} : sf::Color{107,84,68}));
        for (int i=0;i<7;++i) {
            const float age=std::fmod(time*2.8f+i/7.f,1.f), v=0.7f+(i%3)*0.2f;
            const Vec2 start=t.position+rotate({rear*(c.tankHalfWidth-4),9},t.bodyAngle);
            const Vec2 p=start+Vec2{rear*age*(17+i*3.f),-age*26*v+age*age*30};
            box(p,{2.2f+0.5f*(i%2),1.8f},faded(soil,190*(1-age)));
        }
    }
    if (t.hp<500) for (int i=0;i<11;++i) {
        const float age=std::fmod(time*0.75f+i/11.f+t.id*0.17f,1.f);
        const float drift=std::sin(i*2.37f)*12;
        const Vec2 p=t.position+Vec2{-8+drift*age-t.velocity.x*age*0.22f,-19-age*95};
        const auto grey=static_cast<std::uint8_t>(39+(i%4)*9);
        circle(p,3+age*13,faded({grey,grey,static_cast<std::uint8_t>(grey+4)},130*std::pow(1-age,2.4f)));
    }
    if (t.hp<250) {
        const int cycle=static_cast<int>(std::floor(time)); const float age=time-std::floor(time);
        if (age<0.22f) {
            constexpr std::array<Vec2,3> places{{{-19,-9},{7,-18},{19,-3}}};
            const Vec2 base=t.position+rotate(places[(cycle+t.id)%3],t.bodyAngle);
            const float flicker=std::sin(time*119)>-0.5f ? 1.f : 0.4f;
            Vec2 previous=base-Vec2{6,3};
            for (int j=1;j<=5;++j) {
                const Vec2 next=base+Vec2{-6+j*2.4f,((j+cycle)%2 ? -3.f : 3.f)+std::sin(time*91+j)*2};
                line(previous,next,faded({68,170,255},180*flicker),2.1f);
                line(previous,next,faded({202,242,255},250*flicker),0.7f); previous=next;
            }
        }
    }
}
}
