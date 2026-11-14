#include "tankwars/Renderer.hpp"
#include "tankwars/Weapon.hpp"

namespace tw {
namespace {
sf::Color faded(sf::Color c,float alpha) { c.a=static_cast<std::uint8_t>(std::clamp(alpha,0.f,255.f)); return c; }
void softSmoke(sf::RenderTarget& target,Vec2 p,float radius,sf::Color color,float alpha) {
    sf::VertexArray mesh(sf::PrimitiveType::Triangles);
    const auto vertex=[&](int ring,int segment) {
        const float r=ring/3.f, a=segment*2*pi/24;
        const Vec2 q=p+direction(a)*(radius*r);
        return sf::Vertex{{q.x,q.y},faded(color,alpha*std::pow(std::max(0.f,1-r*r),1.2f))};
    };
    for (int r=0;r<3;++r) for (int a=0;a<24;++a)
        for (const auto v : {vertex(r,a),vertex(r+1,a),vertex(r+1,a+1),vertex(r,a),vertex(r+1,a+1),vertex(r,a+1)}) mesh.append(v);
    target.draw(mesh);
}
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
void Renderer::applicationIcon() {
    window_.setView(sf::View(sf::FloatRect({0,0},{1024,1024})));
    sf::VertexArray badge(sf::PrimitiveType::TriangleFan), border(sf::PrimitiveType::LineStrip);
    badge.append({{512,512},{36,67,78}});
    constexpr std::array<Vec2,4> corners{{{756,268},{756,756},{268,756},{268,268}}};
    for (int corner=0;corner<4;++corner) for (int step=0;step<=24;++step) {
        const float angle=-pi/2+corner*pi/2+step*pi/48;
        const Vec2 p=corners[corner]+direction(angle)*204;
        badge.append({{p.x,p.y},{10,24,34}});
        border.append({{p.x,p.y},{83,133,143,160}});
    }
    badge.append(badge[1]); border.append(border[0]);
    window_.draw(badge); window_.draw(border);
    glow({470,700},370,{64,171,155},28);
    for (int i=0;i<3;++i) {
        sf::CircleShape shadow(260.f-i*30,80);
        shadow.setOrigin({260.f-i*30,260.f-i*30}); shadow.setPosition({460,790}); shadow.setScale({1.15f,0.17f});
        shadow.setFillColor({2,9,14,static_cast<std::uint8_t>(50+i*15)}); window_.draw(shadow);
    }
    Config c; Tank hero; hero.position={456,710}; hero.aim=-pi/4;
    hero.weapon=WeaponKind::Mortar; hero.titan=true; hero.hp=hero.maxHp=1000;
    constexpr float scale=10.8f;
    tank(hero,c,scale);
    const Vec2 muzzle=hero.position+Vec2{0,-12*scale}+direction(hero.aim)*(c.weapon.barrelLength*scale);
    for (int i=0;i<10;++i) {
        const Vec2 p=muzzle+Vec2{std::sin(i*0.7f)*15+i*1.5f,-12-i*15.f};
        softSmoke(window_,p,13+i*4.2f,{163,180,184},100*(1-i/11.f));
    }
}
void Renderer::gadgetPreview(GadgetKind kind,WeaponKind weapon,Vec2 center,float time) {
    if (kind==GadgetKind::Jetpack) {
        for (float side : {-1.f,1.f}) {
            const Vec2 p=center+Vec2{side*22,0};
            line(p-Vec2{0,20},p+Vec2{0,5},{72,96,112},12);
            line(p-Vec2{2,18},p+Vec2{-2,2},{153,183,199},3);
            jetExhaust(p+Vec2{0,6},0,1.1f,time+side);
        }
    } else if (kind==GadgetKind::Armor) {
        for (int i=-1;i<=1;++i) {
            const Vec2 p=center+Vec2{i*25.f,-17.f+std::abs(i)*5};
            box(p-Vec2{10,0},{22,39},{42,55,67},{117,141,156});
            line(p+Vec2{-7,7},p+Vec2{9,7},{86,111,128},3);
            for (float y : {13.f,32.f}) circle(p+Vec2{1,y},2,{31,51,63});
        }
    } else {
        weaponPreview(weapon,center,time);
        ring(center,51,{135,233,255,150},time*0.8f,pi*1.5f,0.65f,-0.3f);
        circle(center+Vec2{47,-24},12,{25,56,68},accent);
        text("+",center+Vec2{47,-39},24,accent,true);
    }
}
void Renderer::jetExhaust(Vec2 position,float angle,float size,float phase) {
    const auto at=[&](Vec2 v) { return position+rotate(v*size,angle); };
    const float pulse=0.88f+0.12f*std::sin(time_*43+phase*3);
    glow(at({0,12}),34*size,{43,128,255},75);
    sf::VertexArray plume(sf::PrimitiveType::TriangleStrip);
    for (int row=0;row<=14;++row) {
        const float t=row/14.f, y=t*39*pulse;
        const float w=(1-t)*(5.5f+2*std::sin(t*12-time_*34+phase));
        const float sway=t*t*std::sin(time_*31+phase+t*8)*3;
        const auto ink=faded({45,140,255},(1-t)*190);
        for (float side : {-1.f,1.f}) { const auto p=at({sway+side*w,y}); plume.append({{p.x,p.y},ink}); }
    }
    window_.draw(plume,sf::RenderStates(sf::BlendAdd));
    line(at({0,0}),at({0,19*pulse}),{157,231,255,240},4*size);
    line(at({0,0}),at({0,10*pulse}),{235,253,255},2.2f*size);
    for (int i=0;i<3;++i) {
        const float y=7+i*8*pulse;
        ring(at({0,y}),size*(3.2f-i*0.7f),faded({144,223,255},170-i*40.f),0,2*pi,0.4f,angle);
    }
    line(at({-5,-2}),at({5,-2}),{116,154,177},3*size);
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
    if (t.healthRatio()<0.5f) for (int i=0;i<27;++i) {
        const float age=std::fmod(time*0.68f+i/27.f+t.id*0.17f,1.f);
        const float drift=std::sin(i*2.37f)*23+std::sin(time*0.9f+i)*5;
        const Vec2 p=t.position+Vec2{-5+drift*age-t.velocity.x*age*0.27f,-17-age*113};
        const auto grey=static_cast<std::uint8_t>(32+(i%4)*8);
        softSmoke(window_,p,6+age*22,{grey,grey,static_cast<std::uint8_t>(grey+5)},195*std::pow(1-age,1.55f));
    }
    if (t.healthRatio()<0.25f) {
        const int cycle=static_cast<int>(std::floor(time)); const float age=time-std::floor(time);
        if (age<0.3f) {
            constexpr std::array<Vec2,3> places{{{-19,-9},{7,-18},{19,-3}}};
            const Vec2 base=t.position+rotate(places[(cycle+t.id)%3],t.bodyAngle);
            const float flicker=std::sin(time*119)>-0.5f ? 1.f : 0.4f;
            glow(base,19,{59,161,255},95);
            Vec2 previous=base-Vec2{9,5};
            for (int j=1;j<=7;++j) {
                const Vec2 next=base+Vec2{-9+j*2.8f,((j+cycle)%2 ? -5.f : 5.f)+std::sin(time*91+j)*3};
                line(previous,next,faded({68,170,255},240*flicker),3.5f);
                line(previous,next,faded({228,250,255},255*flicker),1.4f); previous=next;
            }
        }
    }
    if (t.healthRatio()<0.1f) {
        const Vec2 engine=t.position+rotate({-2,-13},t.bodyAngle);
        glow(engine,37,{255,98,22},65);
        flame(engine+Vec2{-4,2},14,t.id*7.f+0.5f,false,time);
        flame(engine+Vec2{5,0},11,t.id*7.f+2.2f,false,time);
    }
}
}
