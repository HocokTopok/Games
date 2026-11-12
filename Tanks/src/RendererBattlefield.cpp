#include "tankwars/Renderer.hpp"
#include "tankwars/Weapon.hpp"

namespace tw {
namespace {
sf::Vector2f point(Vec2 p) { return {p.x,p.y}; }
sf::Color opacity(sf::Color c,float a) { c.a=static_cast<std::uint8_t>(std::clamp(a,0.f,255.f)); return c; }
struct Palette { sf::Color sky, horizon, soil, deep, edge, distant, foliage; };
Palette palette(unsigned id) {
    switch (id) {
    case 0: return {{35,33,47},{154,104,65},{141,102,60},{46,42,40},{227,189,116},{92,75,64},{107,91,59}};
    case 1: return {{12,24,38},{68,97,104},{51,73,60},{17,31,35},{128,166,118},{35,60,67},{30,67,54}};
    case 2: return {{20,30,40},{83,108,103},{70,79,54},{24,35,34},{156,164,110},{46,69,69},{53,72,58}};
    default: return {{32,29,40},{129,91,79},{89,77,70},{27,29,34},{177,150,121},{66,62,65},{81,74,69}};
    }
}
}
void Renderer::terrain(const Config& c,const Terrain& map) {
    const auto colors=palette(c.mapId);
    sf::VertexArray fill(sf::PrimitiveType::TriangleStrip), edge(sf::PrimitiveType::LineStrip);
    sf::VertexArray rock(sf::PrimitiveType::TriangleStrip), rockEdge(sf::PrimitiveType::LineStrip);
    for (float x=0;x<=c.worldWidth+4;x+=4) {
        const float y=map.height(x), base=std::max(y,map.bedrock(x));
        fill.append({{x,y},colors.soil}); fill.append({{x,c.worldHeight},colors.deep});
        edge.append({{x,y},colors.edge});
        rock.append({{x,base},{36,43,45}}); rock.append({{x,c.worldHeight},{17,24,31}});
        rockEdge.append({{x,base},{102,114,105,95}});
    }
    window_.draw(fill); window_.draw(rock); window_.draw(rockEdge); window_.draw(edge);
    for (int band=0;band<4;++band) {
        sf::VertexArray contour(sf::PrimitiveType::LineStrip);
        for (float x=0;x<=c.worldWidth+8;x+=8) {
            const float y=std::min(map.bedrock(x)-2,map.height(x)+10+band*15.f+3*std::sin(x*0.012f+band));
            contour.append({{x,y},opacity(colors.edge,45-band*8.f)});
        }
        window_.draw(contour);
    }
    for (int i=0;i<120;++i) {
        const float x=static_cast<float>((i*233+71)%static_cast<int>(c.worldWidth));
        const float y=map.height(x)+8+(i*77)%120;
        if (y<map.bedrock(x)) line({x,y},{x+3,y},opacity(colors.edge,50));
    }
    // Water occupies only the low, soft pockets; raised ground stays traversable.
    if (c.mapId==2) for (float x=c.worldWidth*0.3f;x<c.worldWidth*0.7f;x+=5) {
        if (map.movementScale(x)>=1) continue;
        const float y=map.height(x);
        line({x,y+1},{x+5,map.height(x+5)+1},{87,133,119,175},4);
        if (static_cast<int>(x)%25==0) line({x,y-0.5f},{x+12,y-0.5f},{164,190,150,110});
    }
}
void Renderer::decorations(const Config& c,const Terrain& map,float time) {
    const auto colors=palette(c.mapId);
    const auto polygon=[&](std::initializer_list<Vec2> points,sf::Color color) {
        sf::ConvexShape shape(points.size()); std::size_t i=0;
        for (auto p : points) shape.setPoint(i++,point(p));
        shape.setFillColor(color); window_.draw(shape);
    };
    const int count=c.mapId==1 ? 68 : (c.mapId==3 ? 30 : 24);
    for (int i=0;i<count;++i) {
        const float x=static_cast<float>((i*173+53)%static_cast<int>(c.worldWidth));
        const float y=map.height(x)+2, size=38+(i*37)%70;
        if (c.mapId==1) {
            line({x,y},{x-3,y-size},{48,52,43},5);
            for (int tier=0;tier<4;++tier) {
                const float top=y-size-12+tier*size*0.19f, w=14+tier*8.f;
                polygon({{x,top},{x-w,top+size*0.43f},{x+w,top+size*0.43f}},i%2 ? colors.foliage : sf::Color{38,79,63});
                line({x-w+3,top+size*0.43f},{x+3,top+6},{89,120,91,55});
            }
        } else if (c.mapId==2) {
            line({x,y},{x-6,y-size},{53,56,48},5);
            for (int j=0;j<4;++j) {
                const float h=size*(0.36f+j*0.15f), side=j%2 ? -1.f : 1.f;
                line({x-3,y-h},{x+side*(17+j*3),y-h-18},{62,65,54},3);
                line({x+side*(17+j*3),y-h-18},{x+side*(21+j*4),y-h-34},{62,65,54},1.5f);
            }
            for (int j=0;j<3;++j) { const float a=x+j*9-14; line({a,y},{a-3,y-20-j*5.f},{109,117,73},1.3f); circle({a-3,y-21-j*5.f},2,{82,81,50}); }
        } else if (c.mapId==3) {
            const float w=50+(i*19)%54, h=size+35;
            polygon({{x-w/2,y},{x-w/2,y-h},{x-w/4,y-h+14},{x,y-h-8},{x+w/5,y-h+24},{x+w/2,y-h+6},{x+w/2,y}},colors.distant);
            for (float yy=y-h+30;yy<y-12;yy+=25) for (float xx=x-w/2+9;xx<x+w/2-7;xx+=20) {
                box({xx,yy},{8,12},{30,34,39,210});
                if ((i+static_cast<int>(yy))%7==0) box({xx+2,yy+3},{3,6},{197,113,58,100});
            }
            line({x-w/2,y-h},{x-w/2,y},{116,102,89,100},2);
            line({x+7,y-h+8},{x-3,y-23},{24,29,34,180},2);
            for (int j=0;j<5;++j) box({x-w/2+j*15,y-5-j%2*7.f},{11,7},{86,73,64});
            if (i%5==0) for (int j=0;j<5;++j) {
                const float rise=std::fmod(time*9+j*18,100.f);
                circle({x+std::sin(j+time*0.3f)*11,y-h-rise},9+rise*0.15f,opacity({61,58,61},50*(1-rise/100)));
            }
        } else {
            if (i%3==0) {
                // Broken anti-tank obstacles, wire and abandoned field pieces.
                line({x-12,y},{x+10,y-23},{76,68,54},4); line({x+13,y},{x-10,y-23},{76,68,54},4);
                line({x-16,y-12},{x+17,y-12},{93,80,61},3);
            } else {
                for (int j=0;j<4;++j) box({x-22+j*13,y-8-(j%2)*4.f},{14,8},{139,117,76},{91,81,59});
                if (i%5==1) { circle({x,y-15},11,{60,59,48}); line({x,y-18},{x+39,y-31},{79,75,58},6); }
            }
        }
    }
    // Small distant silhouettes and shell bursts give the horizon life.
    for (int i=0;i<8;++i) {
        const float x=std::fmod(i*c.worldWidth/8+time*(i%2 ? 9.f : -7.f)+c.worldWidth*4,c.worldWidth);
        const float y=map.height(x)-4;
        const sf::Color ink=opacity(colors.distant,220);
        circle({x,y-15},2.5f,ink); line({x,y-12},{x,y-5},ink,3);
        const float stride=std::sin(time*8+i)*4;
        line({x,y-5},{x-3-stride,y},ink,1.5f); line({x,y-5},{x+3+stride,y},ink,1.5f);
        line({x-4,y-10},{x+7,y-12},ink,1.6f);
    }
    for (int i=0;i<4;++i) {
        const float cycle=std::fmod(time+i*3.7f,12.f);
        if (cycle>2.2f) continue;
        const float x=c.worldWidth*(0.17f+i*0.22f), y=map.height(x)-32;
        const float hot=std::max(0.f,1-cycle/0.7f);
        glow({x,y},25+cycle*30,{255,164,73},hot*85);
        for (int j=0;j<3;++j) circle({x+(j-1)*9.f,y-cycle*22-j*4},7+cycle*8,opacity(colors.distant,65*(1-cycle/2.2f)));
        if (hot>0) line({x-12,y+20},{x+12,y+20},{255,189,94,110},hot*3);
    }
    if (c.mapId==0) for (int i=0;i<5;++i) {
        const float x=std::fmod(time*20+i*367.f,c.worldWidth), y=map.height(x)-9;
        ring({x,y},8,{132,111,68,180},time*2+i,2*pi,0.9f);
        for (int j=0;j<4;++j) line({x,y},{x+std::cos(time*2+j*1.6f)*10,y+std::sin(time*2+j*1.6f)*8},{136,115,71,150});
    }
}
void Renderer::scenery(const Config& c,float time,bool showcase,const Snapshot* state) {
    window_.setView(worldView_);
    const auto colors=palette(c.mapId);
    sf::VertexArray sky(sf::PrimitiveType::TriangleStrip,4);
    sky[0]={{0,0},colors.sky}; sky[1]={{c.worldWidth,0},colors.sky};
    sky[2]={{0,c.worldHeight},colors.horizon}; sky[3]={{c.worldWidth,c.worldHeight},colors.horizon}; window_.draw(sky);
    const Vec2 sun{c.worldWidth*0.74f,c.worldHeight*0.32f};
    const sf::Color sunlight=c.mapId==0 ? sf::Color{255,189,109} : (c.mapId==3 ? sf::Color{248,151,105} : sf::Color{180,209,196});
    glow(sun,c.worldWidth*0.22f,sunlight,25);
    circle(sun,c.mapId==0 ? 43.f : 33.f,opacity(sunlight,95));
    for (int i=0;i<9;++i) {
        const float x=std::fmod(i*c.worldWidth/8+time*(1+i%3),c.worldWidth+200)-100;
        const float y=c.worldHeight*(0.17f+(i%4)*0.09f);
        for (int puff=0;puff<5;++puff) {
            sf::CircleShape cloud(35+puff%2*12.f,32);
            cloud.setOrigin({35+puff%2*12.f,35+puff%2*12.f});
            cloud.setPosition({x+puff*42.f,y+std::sin(static_cast<float>(i+puff))*5}); cloud.setScale({2.1f,0.20f});
            cloud.setFillColor(opacity(colors.horizon,8)); window_.draw(cloud);
        }
    }
    for (int layer=0;layer<3;++layer) {
        sf::VertexArray hills(sf::PrimitiveType::TriangleStrip);
        for (float x=-20;x<=c.worldWidth+20;x+=15) {
            const float y=c.groundHeight-105+layer*30+45*std::sin(x*0.003f+layer*2)+19*std::sin(x*0.009f+layer);
            hills.append({{x,y},opacity(colors.distant,105+layer*35.f)}); hills.append({{x,c.worldHeight},colors.deep});
        }
        window_.draw(hills);
    }
    auto map=state ? MapFactory::create(c,state->terrain) : MapFactory::create(c);
    decorations(c,*map,time);
    terrain(c,*map);
    // Thin drifting dust, fog and ash in front of the terrain, with no gameplay collision.
    for (int i=0;i<28;++i) {
        const float x=std::fmod(i*193.f+time*(c.mapId==0 ? 15 : 5),c.worldWidth);
        const float y=map->height(x)-18-std::fmod(i*31.f+time*2,120.f);
        if (c.mapId==0 || c.mapId==3) circle({x,y},i%3 ? 1 : 2,opacity(colors.edge,55));
        else line({x,y},{x+60,y},opacity(colors.horizon,12),7);
    }
    if (showcase) {
        Tank hero; hero.color=0; hero.position={c.worldWidth*0.72f,map->height(c.worldWidth*0.72f)-43};
        hero.aim=-0.42f; hero.track=time*0.2f; hero.titan=c.titanMode; hero.weapon=WeaponKind::Ricochet;
        glow(hero.position,220,{114,246,208},16); tank(hero,c,3.1f);
    }
    window_.setView(uiView_);
}
void Renderer::perkIcon(PickupKind kind,Vec2 p,float size,sf::Color color,float rotation) {
    const auto at=[&](Vec2 v) { return p+rotate(v*size,rotation); };
    if (kind==PickupKind::Health) {
        line(at({0,-1}),at({0,1}),color,size*0.6f);
        line(at({-1,0}),at({1,0}),color,size*0.6f);
    } else if (kind==PickupKind::Damage) {
        line(at({-0.55f,0.65f}),at({0.6f,-0.8f}),color,size*0.4f);
        line(at({-0.7f,-0.05f}),at({0.1f,0.6f}),color,size*0.3f);
        circle(at({0.62f,-0.84f}),size*0.2f,white);
    } else {
        sf::ConvexShape shield(5);
        constexpr std::array<Vec2,5> vertices{{{-0.8f,-0.9f},{0.8f,-0.9f},{0.7f,0.3f},{0,1},{-0.7f,0.3f}}};
        for (std::size_t i=0;i<vertices.size();++i) shield.setPoint(i,point(at(vertices[i])));
        shield.setFillColor(opacity(color,80)); shield.setOutlineColor(color); shield.setOutlineThickness(size*0.18f); window_.draw(shield);
        line(at({0,-0.6f}),at({0,0.4f}),white,size*0.12f);
    }
}
void Renderer::supplies(const Config& c,const Snapshot& s) {
    if (s.plane.active) {
        const auto p=s.plane.position;
        const auto polygon=[&](std::initializer_list<Vec2> points,sf::Color color) {
            sf::ConvexShape shape(points.size()); std::size_t i=0;
            for (auto v : points) shape.setPoint(i++,point(p+v));
            shape.setFillColor(color); shape.setOutlineColor({145,162,139}); shape.setOutlineThickness(1); window_.draw(shape);
        };
        line(p+Vec2{-130,3},p+Vec2{-40,3},{190,203,193,35},3);
        polygon({{-51,0},{-32,-8},{25,-8},{46,-2},{51,3},{36,9},{-33,8}}, {69,85,76});
        polygon({{-9,-5},{-23,-37},{-10,-39},{17,-6}}, {85,102,86});
        polygon({{-9,6},{-24,35},{-9,37},{19,6}}, {60,75,66});
        polygon({{-42,-3},{-53,-21},{-44,-22},{-28,-3}}, {92,105,87});
        box(p+Vec2{23,-7},{10,8},{160,195,194});
        for (float dx : {-4.f,12.f}) circle(p+Vec2{dx,13},5,{25,34,35});
        const float spin=std::sin(time_*55)*22;
        line(p+Vec2{48,-spin},p+Vec2{48,spin},{206,217,191,170},3);
        line(p+Vec2{-33,1},p+Vec2{17,1},{164,173,140},2);
        circle(p+Vec2{2,-22},4,{180,193,163}, {54,70,64});
        glow(p+Vec2{-47,-2},8,{255,99,66},85);
    }
    for (const auto& d : s.supplies) {
        const auto p=d.position;
        if (!d.landed) {
            const Vec2 canopy=p+Vec2{0,-60};
            sf::ConvexShape cloth(9);
            for (int i=0;i<=8;++i) { const float a=pi+i*pi/8; cloth.setPoint(i,point(canopy+Vec2{std::cos(a)*33,std::sin(a)*23})); }
            cloth.setFillColor({155,172,143,230}); cloth.setOutlineColor({205,215,180}); cloth.setOutlineThickness(1); window_.draw(cloth);
            for (int i=-1;i<=1;++i) line(canopy+Vec2{i*29.f,-2},p+Vec2{i*8.f,-10},{203,213,184,180},1);
        }
        box(p-Vec2{13,10},{26,22},{100,79,46},{194,161,96});
        box(p-Vec2{3,10},{6,22},{175,146,86});
        line(p+Vec2{-11,-7},p+Vec2{11,10},{173,141,80},2);
        if (!d.landed) continue;
        line(p+Vec2{-13,-10},p+Vec2{-25,-18},{184,151,88},4);
        line(p+Vec2{13,-10},p+Vec2{25,-18},{184,151,88},4);
        const sf::Color ink=d.kind==PickupKind::Health ? sf::Color{104,255,154} : (d.kind==PickupKind::Damage ? sf::Color{255,166,62} : sf::Color{88,186,255});
        const float reveal=std::clamp(d.age/0.5f,0.f,1.f), bob=std::sin(time_*2.8f+d.id)*5;
        const Vec2 icon=p+Vec2{0,-27-bob};
        glow(p+Vec2{0,-6},60,ink,40*reveal);
        sf::VertexArray light(sf::PrimitiveType::Triangles,3);
        light[0]={point(p+Vec2{-5,10}),opacity(ink,75*reveal)};
        light[1]={point(icon+Vec2{-22,-10}),opacity(ink,0)};
        light[2]={point(icon+Vec2{22,-10}),opacity(ink,0)}; window_.draw(light,sf::RenderStates(sf::BlendAdd));
        ring(p+Vec2{0,10},26,opacity(ink,150*reveal),time_*1.5f,pi*1.6f,0.23f);
        const float turn=0.72f+0.28f*std::cos(time_*2+d.id);
        glow(icon,24,ink,80*reveal); perkIcon(d.kind,icon,12*turn*reveal,ink,0.22f*std::sin(time_*1.7f+d.id));
    }
    (void)c;
}
void Renderer::weaponPreview(WeaponKind kind,Vec2 center,float time) {
    Config c; Tank t; t.position=center; t.weapon=kind; t.color=(kind==WeaponKind::Laser || kind==WeaponKind::Missile) ? 4 : 0;
    t.titan=true; t.aim=-0.23f; t.track=time*0.25f;
    glow(center,76,kind==WeaponKind::Napalm || kind==WeaponKind::Mortar ? sf::Color{255,153,65} : sf::Color{120,176,255},22);
    tank(t,c,1.4f);
    if (kind==WeaponKind::Missile) {
        const Vec2 end=center+Vec2{111,-33};
        line(center+Vec2{46,-27},end,{255,61,39,190},1.2f);
        glow(end,9,{255,45,25},90); circle(end,2,{255,161,125});
        Projectile p; p.id=21; p.weapon=kind; p.position=center+Vec2{80,7}; p.velocity={500,-75}; p.age=time;
        weaponProjectile(p,c,{});
    } else if (kind==WeaponKind::Ricochet) {
        Projectile p; p.id=19; p.weapon=kind; p.position=center+Vec2{79,-10}; p.age=2.3f+0.3f*std::sin(time); p.velocity={400,-80};
        weaponProjectile(p,c,{});
    } else if (kind==WeaponKind::Flare) {
        const Vec2 base=center+Vec2{78,14};
        line(base-Vec2{0,53},base,{255,120,18,65},28);
        line(base-Vec2{0,53},base,{255,199,56,180},15);
        line(base-Vec2{0,53},base,{255,244,177},5);
        glow(base,25,{255,162,38},90);
        ring(base,18,{255,218,103,210},time,2*pi,0.24f);
    }
}
void Renderer::mapPreview(std::uint8_t id,Vec2 p,Vec2 size) {
    const auto colors=palette(id); box(p,size,colors.sky,colors.edge);
    auto config=MapFactory::preset(Config{},id); auto map=MapFactory::create(config);
    circle(p+Vec2{size.x*0.76f,size.y*0.25f},12,opacity(colors.edge,100));
    sf::VertexArray surface(sf::PrimitiveType::TriangleStrip);
    for (float x=0;x<=size.x;x+=2) {
        const float y=map->height(x/size.x*config.worldWidth)/config.worldHeight*size.y;
        surface.append({point(p+Vec2{x,y}),colors.edge}); surface.append({point(p+Vec2{x,size.y}),colors.deep});
    }
    window_.draw(surface);
    for (int i=0;i<12;++i) {
        const float x=14+i*27.f, y=map->height(x/size.x*config.worldWidth)/config.worldHeight*size.y;
        if (id==1 || id==2) {
            line(p+Vec2{x,y},p+Vec2{x,y-16-i%3*3.f},colors.foliage,2);
            if (id==1) for (int j=0;j<3;++j) line(p+Vec2{x-6+j,y-5-j*4.f},p+Vec2{x+6-j,y-5-j*4.f},colors.foliage,4);
        } else if (id==3) box(p+Vec2{x,y-15-i%3*6.f},{17,25},colors.distant);
    }
}
}
