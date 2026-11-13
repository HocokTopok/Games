#include "tankwars/Renderer.hpp"
#include "tankwars/Weapon.hpp"
#include <random>

namespace tw {
namespace {
sf::Vector2f point(Vec2 v) { return {v.x,v.y}; }
sf::Color fade(sf::Color c,float opacity) { c.a=static_cast<std::uint8_t>(std::clamp(opacity,0.f,255.f)); return c; }
float variation(std::uint32_t seed, std::uint32_t salt) {
    auto v=seed^(salt*0x9e3779b9u); v^=v>>16; v*=0x7feb352du; v^=v>>15; v*=0x846ca68bu; v^=v>>16;
    return (v&0xffffu)/65535.f;
}
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
    if (p.weapon==WeaponKind::Missile) {
        const float angle=std::atan2(p.velocity.y,p.velocity.x);
        const Vec2 ray=direction(angle), normal=direction(angle+pi/2), tail=p.position-ray*11;
        if (settings.particles) for (std::size_t i=0;i<trail.size();++i) {
            const float t=static_cast<float>(i+1)/trail.size();
            const Vec2 smoke=trail[i]+Vec2{std::sin(i*1.7f+p.id)*3,-(1-t)*12};
            circle(smoke,3+(1-t)*9,fade({111,114,123},t*(1-t)*240));
            if (i>0) line(trail[i-1],trail[i],fade({255,147,61},t*t*125),1+t*2);
        }
        const float flicker=0.85f+0.15f*std::sin(p.age*87+p.id);
        glow(tail,27,{255,89,22},100);
        line(tail,tail-ray*(27*flicker),{255,91,20,95},10);
        line(tail,tail-ray*(21*flicker),{255,177,67,220},4);
        line(tail,tail-ray*(10*flicker),{255,244,191},2);
        sf::ConvexShape hull(5);
        const std::array<Vec2,5> shape{{{-11,-3.5f},{6,-3.5f},{15,0},{6,3.5f},{-11,3.5f}}};
        for (std::size_t i=0;i<shape.size();++i) hull.setPoint(i,point(p.position+rotate(shape[i],angle)));
        hull.setFillColor({166,179,184}); hull.setOutlineColor({231,228,206}); hull.setOutlineThickness(0.8f); window_.draw(hull);
        for (float side : {-1.f,1.f}) line(p.position-ray*9+normal*(side*7),p.position-ray*3+normal*(side*2),{194,113,69},2.5f);
        line(p.position-ray*3,p.position+ray*5,{244,96,65},3);
        glow(p.position+ray*10,5,{255,66,35},100);
        return;
    }
    if (p.weapon==WeaponKind::Napalm) {
        for (std::size_t i=1;i<trail.size();++i) {
            const float t=static_cast<float>(i)/trail.size();
            line(trail[i-1],trail[i],fade({255,100,24},t*190),1+t*4);
            if (i%4==0) glow(trail[i],8,{255,139,34},t*55);
        }
        glow(p.position,24,{255,79,18},100);
        glow(p.position,11,{255,179,55},180);
        flame(p.position,7,p.id);
        circle(p.position,3,{255,242,170});
        return;
    }
    if (p.weapon==WeaponKind::Flare) {
        for (std::size_t i=0;i<trail.size();++i) {
            const float t=static_cast<float>(i)/trail.size();
            circle(trail[i]+Vec2{std::sin(i*2.f+p.age*7)*5,-(1-t)*14},3+(1-t)*7,fade({99,89,79},t*80));
            if (i>0) line(trail[i-1],trail[i],fade({255,145,32},t*t*230),2+t*5);
        }
        glow(p.position,48,{255,94,17},100);
        glow(p.position,22,{255,185,32},210);
        flame(p.position,12,p.id);
        circle(p.position,5,{255,250,181});
        ring(p.position,10,{255,221,87,220},p.age*9,pi*1.5f,0.6f,p.age*3);
        return;
    }
    const bool orb=p.weapon==WeaponKind::GrowingOrb || p.weapon==WeaponKind::Ricochet;
    const float charge=orb ? Cannon::orbCharge(p.age,c.weapon) : 0.f;
    const float radius=Cannon::radius(p,c.weapon);
    const sf::Color color=orb ? (p.bounces<2 ? sf::Color{146,119,255} : sf::Color{255,163,98}) : sf::Color{255,131,45};
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
        glow(p.position,radius*3.8f,color,40+charge*45+p.bounces*12);
        glow(p.position,radius*2.1f,{88,202,255},100);
        circle(p.position,radius,fade({67,51,146},200),fade(color,210));
        sf::VertexArray corona(sf::PrimitiveType::LineStrip);
        for (int i=0;i<=80;++i) {
            const float a=i*2*pi/80;
            const float ripple=1+0.07f*std::sin(a*7+time_*9+p.id)+0.035f*std::sin(a*13-time_*13);
            corona.append({point(p.position+direction(a)*(radius*ripple)),{171,202,255,190}});
        }
        window_.draw(corona,sf::RenderStates(sf::BlendAdd));
        ring(p.position,radius*1.3f,{174,129,255,185},p.age*5,pi*1.7f,0.43f,p.age*1.8f+p.id);
        ring(p.position,radius*1.18f,{111,237,255,210},-p.age*6,pi*1.65f,0.55f,-p.age*2.2f);
        glow(p.position,radius*0.95f,{110,225,255},210);
        glow(p.position+Vec2{-radius*0.15f,-radius*0.16f},radius*0.52f,{220,242,255},230);
        circle(p.position,radius*0.17f,{243,245,255,245});
        for (int i=0;i<5;++i) {
            const float a=time_*(1.4f+i*0.12f)+i*2*pi/5+p.id;
            const Vec2 mote=p.position+direction(a)*(radius*(1.4f+0.12f*std::sin(time_*4+i)));
            glow(mote,4+charge*3,{148,196,255},180);
            circle(mote,1+charge,{220,235,255,235});
        }
        // Small pips show how many ricochets remain.
        const unsigned limit=p.weapon==WeaponKind::Ricochet ? 3 : 2;
        for (unsigned i=p.bounces;i<limit;++i) circle(p.position+Vec2{(static_cast<float>(i)*2-(limit-1))*5,-radius-11},1.8f,{207,191,255,210});
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
void Renderer::skyStrikes(const Config& c,const Snapshot& state) {
    for (const auto& strike : state.skyStrikes) {
        const float radius=strike.radius, age=strike.age, warmup=c.weapon.skyWarmup;
        const auto base=strike.position;
        if (age<warmup) {
            const float t=age/warmup, pulse=0.65f+0.35f*std::sin(age*28);
            glow(base,radius*1.7f,{255,141,24},45*t);
            ring(base,radius*(1.2f-0.2f*t),fade({255,171,45},200*pulse),age*3,2*pi,0.27f);
            ring(base,radius*0.62f,fade({255,224,113},220*t),-age*5,pi*1.6f,0.27f);
            line(base-Vec2{radius,0},base+Vec2{radius,0},fade({255,193,62},150*t),2);
            for (float y=15;y<base.y;y+=40) line({base.x,y},{base.x,y+15},fade({255,192,61},80*t),2);
            continue;
        }
        const float active=age-warmup;
        const float envelope=std::min(1.f,active/0.12f)*std::clamp((c.weapon.skyDuration-active)/0.45f,0.f,1.f);
        const float pulse=0.94f+0.06f*std::sin(age*37+strike.id);
        // Layers share the authoritative age and seed on Host and Join.
        for (int layer=0;layer<4;++layer) {
            const float width=radius*(layer==0 ? 1.5f : (layer==1 ? 1.f : (layer==2 ? 0.63f : 0.25f)))*envelope*pulse;
            const sf::Color ink=layer==0 ? sf::Color{255,81,9} : (layer==1 ? sf::Color{255,136,15} : (layer==2 ? sf::Color{255,207,42} : sf::Color{255,249,183}));
            const float opacity=(layer==0 ? 32.f : (layer==1 ? 75.f : (layer==2 ? 135.f : 230.f)))*envelope;
            for (float side : {-1.f,1.f}) {
                sf::VertexArray column(sf::PrimitiveType::TriangleStrip);
                for (int i=0;i<=40;++i) {
                    const float t=i/40.f, y=-30+(base.y+30)*t;
                    const float ripple=1+0.06f*std::sin(y*0.04f-age*16+strike.id)+0.035f*std::sin(y*0.11f+age*23);
                    column.append({{base.x,y},fade(ink,opacity)});
                    column.append({{base.x+side*width*ripple,y},fade(ink,0)});
                }
                window_.draw(column,sf::RenderStates(sf::BlendAdd));
            }
        }
        for (int i=0;i<12;++i) {
            const float phase=variation(strike.id,i+20), x=base.x+(phase-0.5f)*radius*1.7f;
            const float y=std::fmod(active*(530+phase*300)+phase*base.y,base.y+80)-40;
            line({x,y},{x,std::min(base.y,y+35+phase*65)},fade({255,244,165},150*envelope),1.2f+phase*2);
        }
        const Vec2 source{base.x,25};
        glow(source,radius*1.6f,{255,167,32},115*envelope);
        ring(source,radius*0.85f,fade({255,227,115},150*envelope),age*2,2*pi,0.18f);
        glow(base,radius*2.5f,{255,105,12},135*envelope);
        glow(base,radius*1.3f,{255,209,58},220*envelope);
        ring(base,radius*(1+0.08f*std::sin(age*9)),fade({255,193,53},220*envelope),age*4,2*pi,0.25f);
        ring(base,radius*0.65f,fade({255,243,163},230*envelope),-age*6,2*pi,0.23f);
        if (settings.particles) for (int i=0;i<20;++i) {
            const float phase=variation(strike.id,i+50), rise=std::fmod(active*(95+phase*75)+phase*180,180.f);
            const Vec2 p=base+Vec2{(phase-0.5f)*radius*2.6f+std::sin(rise*0.04f+i)*10,-rise};
            glow(p,4+phase*4,{255,169,39},130*envelope*(1-rise/180));
            circle(p,1+phase*1.8f,fade({255,228,114},230*envelope*(1-rise/180)));
        }
    }
}
void Renderer::addWeaponEffect(const GameEvent& e) {
    const bool mortar=e.kind==EventKind::MortarImpact || e.kind==EventKind::PlaneCrash, bounce=e.kind==EventKind::Ricochet;
    weaponEffects_.push_back({e.position,e.kind,0,bounce ? 0.55f : (mortar ? 3.1f+0.4f*variation(e.id,71) : 1.35f),e.radius,e.intensity,e.id,e.normal});
    shake_=std::max(shake_,bounce ? 1.4f : (mortar ? 5.2f : 2+3*e.intensity));
    if (!settings.particles) return;
    std::mt19937 random(e.id*977+31);
    std::uniform_real_distribution<float> unit(0,1);
    const bool missile=e.kind==EventKind::MissileImpact;
    const int count=missile ? 100 : (bounce ? 22 : (mortar ? 84 : 52));
    for (int i=0;i<count;++i) {
        const float angle=missile ? std::atan2(e.normal.y,e.normal.x)+(unit(random)-0.5f)*2.2f : unit(random)*2*pi;
        const float speed=(bounce ? 50 : 70)+unit(random)*(bounce ? 170 : 280);
        const float life=0.3f+unit(random)*(mortar ? 1.1f : 0.7f);
        const sf::Color color=(mortar || missile) ? (i%3 ? sf::Color{255,155,64} : sf::Color{255,223,139}) : (i%2 ? sf::Color{163,125,255} : sf::Color{132,226,255});
        particles_.push_back({e.position,direction(angle)*speed+Vec2{0,mortar ? -60.f : -15.f},color,life,life,1+unit(random)*2.5f});
    }
}
void Renderer::weaponEffects() {
    for (const auto& e : weaponEffects_) {
        const float t=e.age/e.duration;
        const bool missile=e.kind==EventKind::MissileImpact;
        const bool mortar=e.kind==EventKind::MortarImpact || e.kind==EventKind::PlaneCrash, bounce=e.kind==EventKind::Ricochet;
        const sf::Color color=(mortar || missile) ? sf::Color{255,137,49} : sf::Color{156,130,255};
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
        if (e.kind==EventKind::MissileImpact) {
            const float axis=std::atan2(e.normal.y,e.normal.x), hot=std::max(0.f,1-e.age/0.65f);
            glow(e.position,radius*(1+expand*1.6f),{255,105,28},170*(1-t)*(1-t));
            ring(e.position,radius*(0.2f+expand*1.1f),fade({255,191,99},210*(1-t)),0,2*pi,0.28f,axis+pi/2);
            for (int i=0;i<22;++i) {
                const float v=variation(e.seed,i+120), spread=variation(e.seed,i+170);
                const Vec2 out=direction(axis+(v-0.5f)*2.35f);
                const Vec2 at=e.position+out*(radius*expand*(0.35f+spread*1.45f))+e.normal*(e.age*28);
                const float size=radius*(0.09f+spread*0.15f+t*0.16f);
                const auto grey=static_cast<std::uint8_t>(62+v*43);
                circle(at,size,fade({grey,grey,static_cast<std::uint8_t>(grey+5)},145*std::sin(pi*t)));
                glow(at,size*2,{255,86,17},160*hot);
                circle(at,size*hot,fade({255,static_cast<std::uint8_t>(121+spread*79),43},205*hot));
                glow(at-e.normal*(size*0.2f),size*0.75f,{255,232,158},190*hot*hot);
                if (i%3==0) line(e.position+out*(radius*expand*0.25f),at+out*(20*hot),fade({255,211,136},230*hot),2*hot+0.5f);
            }
            glow(e.position+e.normal*(radius*expand*0.4f),radius*0.8f,{255,230,155},230*hot*hot);
            continue;
        }
        if (mortar) {
            const float rise=radius*(0.15f+0.95f*(1-std::exp(-e.age*1.6f)));
            const float growth=1-std::exp(-e.age*5), fadeOut=std::clamp((1-t)*4.f,0.f,1.f);
            const float heat=std::max(0.f,1-e.age/1.25f);
            // Layered opaque lobes, rolling outward around a rising central column.
            for (int i=0;i<42;++i) {
                const float v=variation(e.seed,i+310), q=variation(e.seed,i+360);
                const bool cap=i>=15;
                const float a=(i-15)*2.39996f;
                const float spread=radius*(0.25f+0.60f*growth)*std::sqrt(q);
                const Vec2 at=e.position+(cap ? Vec2{std::cos(a)*spread,-rise+std::sin(a)*spread*0.32f} : Vec2{(v-0.5f)*radius*0.27f,-rise*(i/15.f)});
                const float size=radius*(cap ? 0.20f+0.12f*v : 0.12f+0.08f*v)*(0.4f+0.6f*growth);
                const auto grey=static_cast<std::uint8_t>(46+v*38);
                circle(at,size,fade({grey,static_cast<std::uint8_t>(grey-4),static_cast<std::uint8_t>(grey-7)},255*fadeOut));
                const float ember=heat*(0.6f+0.4f*std::sin(e.age*10+i));
                circle(at+Vec2{0,size*0.22f},size*0.78f,fade({255,static_cast<std::uint8_t>(85+v*100),24},235*ember*fadeOut));
                glow(at,size*1.7f,{255,92,19},70*ember*fadeOut);
            }
            const float shock=std::clamp(e.age/0.7f,0.f,1.f);
            ring(e.position,radius*(0.1f+shock*1.5f),fade({255,206,130},230*(1-shock)),0,2*pi,0.24f);
            glow(e.position,radius*1.8f,{255,144,51},120*heat);
            const float flash=std::max(0.f,1-e.age/0.16f);
            circle(e.position,radius*0.40f*flash,fade({255,245,206},255*flash));
            continue;
        }
        glow(e.position,radius*(1.8f+expand),color,100*std::pow(1-t,2));
        const float shock=std::clamp(e.age/0.6f,0.f,1.f);
        ring(e.position,radius*(0.12f+1.05f*(1-std::pow(1-shock,3))),fade(color,240*std::pow(1-shock,1.5f)));
        ring(e.position,radius*(0.2f+expand*1.6f),fade(color,80*std::pow(1-t,3)),0,2*pi,0.23f);
        const float hot=std::max(0.f,1-e.age/(mortar ? 0.95f : 0.7f));
        const float fireSize=radius*(0.18f+expand*0.55f)*hot;
        if (mortar) {
            const int lobes=9+static_cast<int>(variation(e.seed,93)*7);
            for (int i=0;i<lobes;++i) {
                const float shape=variation(e.seed,i*7+3), speed=variation(e.seed,i*7+4);
                const float a=i*2*pi/lobes+shape*0.5f;
                const Vec2 p=e.position+direction(a)*(fireSize*(0.35f+shape*0.65f))+Vec2{0,-e.age*speed*40};
                const float size=fireSize*(0.25f+shape*0.23f)*(1+0.12f*std::sin(e.age*(14+speed*12)+i));
                glow(p,size*2.3f,{255,85,17},150*hot);
                circle(p,size,fade({255,static_cast<std::uint8_t>(100+speed*70),31},210*hot));
                glow(p-Vec2{size*0.2f,size*0.2f},size,{255,222,121},210*hot*hot);
                if (i%2==0 && hot>0.1f) flame(p+Vec2{0,size*0.4f},size,i+e.seed);
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
