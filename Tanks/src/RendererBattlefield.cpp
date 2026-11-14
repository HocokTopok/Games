#include "tankwars/Renderer.hpp"
#include "tankwars/Weapon.hpp"

namespace tw {
namespace {
sf::Vector2f point(Vec2 p) { return {p.x,p.y}; }
sf::Color opacity(sf::Color c,float a) { c.a=static_cast<std::uint8_t>(std::clamp(a,0.f,255.f)); return c; }
struct Palette { sf::Color sky, horizon, soil, deep, edge, distant, foliage; };
Palette palette(unsigned id) {
    switch (id) {
    case 0: return {{48,32,64},{209,125,79},{162,111,58},{49,37,38},{251,203,116},{121,78,69},{130,103,59}};
    case 1: return {{10,23,45},{69,111,124},{47,85,66},{16,33,40},{146,187,139},{37,73,86},{33,88,65}};
    case 2: return {{15,29,44},{95,131,120},{78,91,54},{24,39,37},{174,184,116},{48,82,80},{67,91,63}};
    default: return {{44,43,59},{177,130,96},{107,84,68},{32,31,37},{208,173,131},{85,76,79},{97,81,66}};
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
    for (int i=0;i<260;++i) {
        const float x=static_cast<float>((i*233+71)%static_cast<int>(c.worldWidth));
        const float y=map.height(x)+8+(i*77)%120;
        if (y<map.bedrock(x)) {
            line({x,y},{x+3+i%4,y-1},opacity(colors.edge,64));
            if (i%4==0) line({x,y+2},{x+5,y+2},opacity(colors.deep,120),2);
        }
    }
    for (float x=3;x<c.worldWidth;x+=11) {
        const float y=map.height(x), slope=map.slope(x);
        line({x,y+2},{x+7,y+2+std::tan(slope)*7},opacity(colors.edge,110),2);
        if ((c.mapId==1 || c.mapId==2) && static_cast<int>(x)%3==0) {
            for (int j=0;j<3;++j) line({x+j*2.f,y},{x+j*3.f-3,y-4-j*2.f},opacity(colors.edge,155),1);
        }
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
            line({x+1,y-2},{x-2,y-size},{111,108,76,180},1.3f);
            for (int tier=0;tier<4;++tier) {
                const float top=y-size-12+tier*size*0.19f, w=14+tier*8.f;
                polygon({{x,top},{x-w,top+size*0.43f},{x+w,top+size*0.43f}},i%2 ? colors.foliage : sf::Color{38,79,63});
                line({x-w+3,top+size*0.43f},{x+3,top+6},{89,120,91,55});
                for (int branch=1;branch<=4;++branch) {
                    const float yy=top+size*(0.10f+branch*0.065f), reach=w*(0.12f+branch*0.17f);
                    line({x-reach,yy+6},{x,yy},{105,158,116,150},1.3f);
                    line({x,yy},{x+reach,yy+7},{14,50,45,175},1.8f);
                }
            }
        } else if (c.mapId==2) {
            line({x,y},{x-6,y-size},{53,56,48},5);
            line({x+1,y-1},{x-4,y-size},{135,144,106,185},1.3f);
            for (int j=0;j<4;++j) {
                const float h=size*(0.36f+j*0.15f), side=j%2 ? -1.f : 1.f;
                line({x-3,y-h},{x+side*(17+j*3),y-h-18},{62,65,54},3);
                line({x+side*(17+j*3),y-h-18},{x+side*(21+j*4),y-h-34},{62,65,54},1.5f);
            }
            for (int j=0;j<3;++j) { const float a=x+j*9-14; line({a,y},{a-3,y-20-j*5.f},{109,117,73},1.3f); circle({a-3,y-21-j*5.f},2,{82,81,50}); }
        } else if (c.mapId==3) {
            const float w=50+(i*19)%54, h=size+35;
            if (map.height(x)<c.worldHeight*0.71f || std::abs(map.height(x-w/2)-map.height(x+w/2))>14 ||
                std::abs(map.slope(x))>0.20f) continue;
            polygon({{x-w/2,y},{x-w/2,y-h},{x-w/4,y-h+14},{x,y-h-8},{x+w/5,y-h+24},{x+w/2,y-h+6},{x+w/2,y}},colors.distant);
            polygon({{x+w/4,y-h+18},{x+w/2,y-h+6},{x+w/2,y},{x+w/4,y}},{55,52,58});
            for (float floor=y-17;floor>y-h+25;floor-=25) {
                line({x-w/2+2,floor},{x+w/4,floor},{148,121,98,120},2);
                for (float xx=x-w/2+4;xx<x+w/4;xx+=13) {
                    line({xx,floor-6},{xx+9,floor-6},{163,127,101,65});
                    line({xx+5,floor-12},{xx+14,floor-12},{163,127,101,55});
                }
            }
            for (float yy=y-h+30;yy<y-12;yy+=25) for (float xx=x-w/2+9;xx<x+w/2-7;xx+=20) {
                box({xx,yy},{8,12},{30,34,39,210});
                line({xx-1,yy-1},{xx+9,yy-1},{175,144,116,180},1.4f);
                line({xx+9,yy},{xx+9,yy+13},{43,40,44,230},2);
                if ((i+static_cast<int>(yy))%7==0) box({xx+2,yy+3},{3,6},{197,113,58,100});
            }
            line({x-w/2,y-h},{x-w/2,y},{116,102,89,100},2);
            line({x+7,y-h+8},{x-3,y-23},{24,29,34,180},2);
            line({x-3,y-48},{x+11,y-62},{28,29,34,225},1.5f);
            line({x+11,y-62},{x+4,y-76},{28,29,34,225},1.5f);
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
                circle({x,y-12},2,{198,167,106});
                for (int j=0;j<4;++j) line({x+18+j*7.f,y-10},{x+24+j*7.f,y-16},{122,101,73,175});
            } else {
                for (int j=0;j<4;++j) box({x-22+j*13,y-8-(j%2)*4.f},{14,8},{139,117,76},{91,81,59});
                for (int j=0;j<4;++j) line({x-20+j*13,y-6-(j%2)*4.f},{x-11+j*13,y-6-(j%2)*4.f},{212,173,102,160});
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
void Renderer::celestial(const Config& c) {
    constexpr std::array<Vec2,4> positions{{{0.76f,0.35f},{0.60f,0.30f},{0.38f,0.30f},{0.22f,0.29f}}};
    const Vec2 center{positions[c.mapId].x*c.worldWidth,positions[c.mapId].y*c.worldHeight};
    const float radius=c.worldWidth*(c.mapId==0 ? 0.057f : (c.mapId==3 ? 0.024f : 0.027f));
    const bool night=c.mapId==1 || c.mapId==2;
    const sf::Color halo=night ? sf::Color{161,218,211} : (c.mapId==0 ? sf::Color{255,118,103} : sf::Color{255,211,145});
    glow(center,radius*4.5f,halo,night ? 25.f : 55.f);
    glow(center,radius*1.6f,halo,night ? 45.f : 80.f);
    if (c.mapId==2) {
        // Build the crescent itself; its dark side leaves the sky visible through it.
        sf::VertexArray crescent(sf::PrimitiveType::TriangleStrip);
        for (int row=0;row<=80;++row) {
            const float y=-radius+2*radius*row/80;
            const float outer=std::sqrt(std::max(0.f,radius*radius-y*y));
            const float inner=0.42f*radius-std::sqrt(std::max(0.f,radius*radius-y*y));
            crescent.append({point(center+Vec2{-outer,y}),{185,220,202,255}});
            crescent.append({point(center+Vec2{std::min(outer,inner),y}),{222,239,213,255}});
        }
        window_.draw(crescent);
        return;
    }
    sf::VertexArray disc(sf::PrimitiveType::TriangleStrip);
    for (int row=0;row<=100;++row) {
        const float t=row/100.f, y=radius*(2*t-1), extent=std::sqrt(std::max(0.f,radius*radius-y*y));
        const sf::Color ink=night ? sf::Color{static_cast<std::uint8_t>(226-47*t),static_cast<std::uint8_t>(240-26*t),static_cast<std::uint8_t>(223-20*t)} :
            (c.mapId==0 ? sf::Color{255,static_cast<std::uint8_t>(181-69*t),static_cast<std::uint8_t>(100+49*t)} : sf::Color{255,static_cast<std::uint8_t>(237-37*t),static_cast<std::uint8_t>(174-34*t)});
        disc.append({point(center+Vec2{-extent,y}),ink}); disc.append({point(center+Vec2{extent,y}),ink});
    }
    window_.draw(disc);
    if (night) {
        for (int i=0;i<15;++i) {
            const float a=i*2.39996f, r=radius*(0.18f+0.55f*((i*7)%13)/13.f), size=radius*(0.045f+(i%4)*0.025f);
            const Vec2 crater=center+direction(a)*r;
            circle(crater,size,{121,159,157,85});
            ring(crater+Vec2{0,size*0.1f},size,{236,247,221,85},0.15f,pi*0.9f,0.8f);
        }
        ring(center,radius,{227,246,225,180},-pi*0.85f,pi*1.3f);
    } else if (c.mapId==0) {
        for (int band=0;band<6;++band) {
            const float y=radius*(0.20f+band*0.12f), extent=std::sqrt(radius*radius-y*y);
            line(center+Vec2{-extent,y},center+Vec2{extent,y},{226,102,112,static_cast<std::uint8_t>(35+band*5)},1+band*0.7f);
        }
    }
}
void Renderer::foregroundFog(const Config& c,const Terrain& map,float time) {
    if (c.mapId!=2) return;
    // Smooth, soft-edged horizontal banks surge, thin out and reverse independently.
    // Drawn after tanks and their labels, using match time on both clients.
    constexpr std::array<float,7> centers{0.13f,0.29f,0.43f,0.56f,0.69f,0.82f,0.92f};
    for (int bank=0;bank<7;++bank) {
        const float phase=bank*2.37f;
        const float x=c.worldWidth*centers[bank]+std::sin(time*0.17f+phase)*95+std::sin(time*0.43f+phase*1.7f)*28;
        const float surge=0.5f+0.5f*std::sin(time*0.29f+phase+0.8f*std::sin(time*0.13f+bank));
        const float density=0.08f+0.92f*std::pow(surge,1.6f);
        const float width=150+bank%3*40.f;
        const float floor=map.height(x);
        for (int layer=0;layer<4;++layer) {
            const float height=15+layer*6.f, altitude=10+layer*17.f;
            sf::VertexArray mist(sf::PrimitiveType::Triangles);
            const auto vertex=[&](float u,float v) {
                const float xx=x+u*width;
                const float yy=floor-altitude+v*height+std::sin(u*5+time*(0.16f+bank*0.013f)+phase)*4;
                const float clip=std::clamp((map.height(xx)+2-yy)/6.f,0.f,1.f);
                const float strength=std::pow(std::max(0.f,1-u*u),1.4f)*std::pow(std::max(0.f,1-v*v),1.4f)*density*235*clip;
                return sf::Vertex{{xx,yy},opacity({153,179,163},xx<0 || xx>c.worldWidth ? 0 : strength)};
            };
            for (int segment=0;segment<32;++segment) for (int row=0;row<8;++row) {
                const float u=-1+segment/16.f, v=-1+row/4.f;
                const auto a=vertex(u,v), b=vertex(u+1.f/16,v), d=vertex(u,v+0.25f), e=vertex(u+1.f/16,v+0.25f);
                for (const auto& p : {a,b,e,a,e,d}) mist.append(p);
            }
            window_.draw(mist);
        }
    }
}
void Renderer::scenery(const Config& c,float time,bool showcase,const Snapshot* state) {
    window_.setView(worldView_);
    const auto colors=palette(c.mapId);
    sf::VertexArray sky(sf::PrimitiveType::TriangleStrip,4);
    sky[0]={{0,0},colors.sky}; sky[1]={{c.worldWidth,0},colors.sky};
    sky[2]={{0,c.worldHeight},colors.horizon}; sky[3]={{c.worldWidth,c.worldHeight},colors.horizon}; window_.draw(sky);
    if (c.mapId==1 || c.mapId==2) for (int i=0;i<85;++i) {
        const Vec2 star{static_cast<float>((i*397+37)%static_cast<int>(c.worldWidth)),c.worldHeight*(0.04f+((i*61)%43)/100.f)};
        circle(star,i%9==0 ? 1.5f : 0.7f,opacity({198,224,217},65+(i*19)%130));
        if (i%9==0) line(star-Vec2{3,0},star+Vec2{3,0},{174,208,218,80});
    }
    celestial(c);
    for (int i=0;i<9;++i) {
        const float x=std::fmod(i*c.worldWidth/8+time*(1+i%3),c.worldWidth+200)-100;
        const float y=c.worldHeight*(0.17f+(i%4)*0.09f);
        for (int puff=0;puff<5;++puff) {
            sf::CircleShape cloud(35+puff%2*12.f,32);
            cloud.setOrigin({35+puff%2*12.f,35+puff%2*12.f});
            cloud.setPosition({x+puff*42.f,y+std::sin(static_cast<float>(i+puff))*5}); cloud.setScale({2.1f,0.20f});
            cloud.setFillColor(opacity(colors.horizon,c.mapId==0 ? 19.f : 13.f)); window_.draw(cloud);
        }
    }
    for (int layer=0;layer<3;++layer) {
        sf::VertexArray hills(sf::PrimitiveType::TriangleStrip);
        for (float x=-20;x<=c.worldWidth+20;x+=15) {
            const float y=c.groundHeight-105+layer*30+45*std::sin(x*0.003f+layer*2)+19*std::sin(x*0.009f+layer);
            hills.append({{x,y},opacity(colors.distant,105+layer*35.f)}); hills.append({{x,c.worldHeight},colors.deep});
        }
        window_.draw(hills);
        for (int i=0;i<55;++i) {
            const float x=i*c.worldWidth/54, y=c.groundHeight-105+layer*30+45*std::sin(x*0.003f+layer*2)+19*std::sin(x*0.009f+layer);
            const sf::Color ink=opacity(colors.distant,125+layer*35.f);
            if (c.mapId==1 || c.mapId==2) {
                const float h=20+(i*31+layer*17)%44;
                line({x,y+3},{x,y-h},ink,2+layer);
                if (c.mapId==1) for (int tier=0;tier<4;++tier) {
                    const float top=y-h+tier*h*0.18f;
                    line({x-4-tier*3.f,top+11},{x,top},ink,4+layer);
                    line({x,top},{x+4+tier*3.f,top+11},ink,4+layer);
                } else for (float side : {-1.f,1.f}) line({x,y-h*0.45f},{x+side*12,y-h*0.8f},ink,2);
            } else if (c.mapId==3) {
                const float h=25+(i*23+layer*31)%70;
                box({x,y-h},{c.worldWidth/70,h+12},ink);
                if (i%4==0) { line({x+8,y-h},{x+8,y-h-25},ink,5); line({x,y-h-8},{x+15,y-h-8},ink,2); }
                for (int row=0;row<3;++row) box({x+5,y-h+8+row*15.f},{3,5},opacity(colors.horizon,80));
            } else if (i%4==0) {
                line({x,y},{x+45,y-5},opacity(colors.edge,32),2);
            }
        }
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
        const auto at=[&](Vec2 v) { v.x*=s.plane.direction; return p+rotate(v,s.plane.direction*s.plane.angle); };
        const auto polygon=[&](std::initializer_list<Vec2> points,sf::Color color) {
            sf::ConvexShape shape(points.size()); std::size_t i=0;
            for (auto v : points) shape.setPoint(i++,point(at(v)));
            shape.setFillColor(color); shape.setOutlineColor({145,162,139}); shape.setOutlineThickness(1); window_.draw(shape);
        };
        line(at({-130,3}),at({-40,3}),{190,203,193,35},3);
        polygon({{-51,0},{-32,-8},{25,-8},{46,-2},{51,3},{36,9},{-33,8}}, s.plane.bombing ? sf::Color{76,72,67} : sf::Color{69,85,76});
        polygon({{-9,-5},{-23,-37},{-10,-39},{17,-6}}, {85,102,86});
        polygon({{-9,6},{-24,35},{-9,37},{19,6}}, {60,75,66});
        polygon({{-42,-3},{-53,-21},{-44,-22},{-28,-3}}, {92,105,87});
        polygon({{23,-7},{33,-7},{33,1},{23,1}},{160,195,194});
        for (float dx : {-4.f,12.f}) circle(at({dx,13}),5,{25,34,35});
        const float spin=std::sin(time_*55)*22;
        line(at({48,-spin}),at({48,spin}),{206,217,191,170},3);
        line(at({-33,1}),at({17,1}),{164,173,140},2);
        circle(at({2,-22}),4,{180,193,163}, {54,70,64});
        if (s.plane.bombing) {
            for (float dx : {-17.f,-5.f,7.f}) line(at({dx,5}),at({dx,10}),{42,38,34},3);
            line(at({-14,-6}),at({16,-6}),{208,139,84},2);
        }
        glow(at({-47,-2}),8,{255,99,66},85);
        box(p+Vec2{-35,-57},{70,4},{24,31,34});
        box(p+Vec2{-35,-57},{70*s.plane.hp/200,4},s.plane.crashing ? sf::Color{255,101,48} : sf::Color{180,210,150});
        if (s.plane.crashing) for (int i=0;i<7;++i) {
            const float age=std::fmod(time_*1.2f+i*0.14f,1.f);
            const Vec2 smoke=at({-25-age*95,-age*65});
            circle(smoke,10+age*22,opacity({56,53,51},180*(1-age)));
            if (i<3) flame(at({-32.f-i*8,0}),12+i*2.f,i);
        }
    }
    for (const auto& bomb : s.bombs) {
        const auto p=bomb.position;
        const float angle=std::atan2(bomb.velocity.y,bomb.velocity.x);
        const auto at=[&](Vec2 v) { return p+rotate(v,angle); };
        sf::ConvexShape hull(6);
        const std::array<Vec2,6> vertices{{{-10,-5},{4,-6},{11,-3},{13,0},{7,5},{-10,5}}};
        for (std::size_t i=0;i<vertices.size();++i) hull.setPoint(i,point(at(vertices[i])));
        hull.setFillColor({62,62,52}); hull.setOutlineColor({210,195,143}); hull.setOutlineThickness(1); window_.draw(hull);
        line(at({-14,-8}),at({-6,0}),{159,163,132},2.5f);
        line(at({-14,8}),at({-6,0}),{159,163,132},2.5f);
        line(at({4,-5}),at({4,5}),{213,145,67},2);
        if (bomb.velocity.y>120) line(at({-18,0}),at({-36,0}),{194,211,197,65},1.5f);
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
    constexpr std::array<Vec2,4> positions{{{0.76f,0.35f},{0.60f,0.30f},{0.38f,0.30f},{0.22f,0.29f}}};
    const Vec2 light=p+Vec2{size.x*positions[id].x,size.y*positions[id].y};
    const float radius=id==0 ? 18.f : 9.f;
    circle(light,radius,id==0 ? sf::Color{255,145,119} : (id==3 ? sf::Color{255,225,162} : sf::Color{204,231,211}));
    if (id==2) circle(light+Vec2{4,-2},8,colors.sky);
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
