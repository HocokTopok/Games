#include "tankwars/Game.hpp"
#include <iostream>
#include "tankwars/Weapon.hpp"

namespace tw {
Game::Game(Config config, LaunchOptions options)
    : config_(config), options_(std::move(options)),
      window_(sf::VideoMode({config.windowWidth,config.windowHeight}), "Tank Wars / Nightfall " TANKWARS_VERSION, sf::Style::Default,
              sf::State::Windowed, sf::ContextSettings{.antiAliasingLevel = 4}),
      renderer_(window_, options_.assets), audio_(options_.assets), localAddress_(net::localAddress()) {
    pointerPixel_=sf::Mouse::getPosition(window_);
    window_.setFramerateLimit(config.frameLimit); window_.setKeyRepeatEnabled(false);
    renderer_.settings.frameLimit = config.frameLimit; renderer_.resize(config_);
    if (options_.demo) play();
    else if (options_.host) host();
    else if (!options_.join.empty()) { address_ = options_.join; join(); }
}
const Config& Game::activeConfig() const { return client_ && client_->welcomed() ? client_->config() : (solo_ ? solo_->config() : config_); }
void Game::play() {
    leave();
    auto config = config_; if (!config.teams) config.playerLimit=2;
    solo_ = std::make_unique<GameSession>(config);
    solo_->addPlayer("PILOT 01"); if (!config.teams) solo_->addPlayer("SENTINEL",true);
    if (config.loadoutSelection) solo_->setWeapon(1,WeaponKind::Mortar);
    solo_->setReady(1,true);
    if (options_.demo || !config.loadoutSelection) {
        if (config.loadoutSelection) solo_->setWeapon(0,WeaponKind::Ricochet);
        solo_->setReady(0,true);
        if (config.loadoutSelection) solo_->setMap(0,config.mapId);
        solo_->step();
    }
    state_ = solo_->snapshot(); previousSolo_ = state_;
    screen_ = state_.phase==Phase::Lobby ? Screen::Lobby : Screen::Playing;
    renderer_.resize(activeConfig()); renderer_.resetEffects();
}
void Game::host() {
    leave();
    try {
        server_ = std::make_unique<net::Server>(config_,config_.port);
        client_ = std::make_unique<net::Client>();
        if (!client_->connect("127.0.0.1",server_->port(),"PILOT 01")) throw std::runtime_error(client_->error());
        screen_ = Screen::Lobby;
    } catch (const std::exception& e) { const std::string message = e.what(); leave(); status_ = message; }
}
void Game::join() {
    client_ = std::make_unique<net::Client>();
    if (client_->connect(address_,config_.port,"PILOT 02")) { status_.clear(); screen_ = Screen::Lobby; autoReadySent_ = false; }
    else { status_ = client_->error(); client_.reset(); screen_ = Screen::Join; }
}
void Game::leave() {
    client_.reset(); server_.reset(); solo_.reset(); state_ = {}; effectState_ = {}; draggedPlayer_=noPlayer;
    selectedMap_.reset();
    paused_ = false; accumulator_ = 0; inputTimer_ = 0; wheelTime_ = 0; autoReadySent_ = false;
    controls_.reset();
    screen_ = Screen::Main; status_.clear(); renderer_.resetEffects(); renderer_.resize(config_);
    audio_.reset();
}
void Game::events() {
    clicked_ = false; released_=false;
    while (const auto e = window_.pollEvent()) {
        controls_.handle(*e);
        if (e->is<sf::Event::Closed>()) window_.close();
        if (e->is<sf::Event::Resized>()) renderer_.resize(activeConfig());
        if (e->is<sf::Event::FocusLost>()) {
            wheelTime_ = 0;
            if (screen_ == Screen::Playing) paused_ = true;
        }
        if (const auto* m = e->getIf<sf::Event::MouseButtonPressed>(); m && m->button == sf::Mouse::Button::Left) { clicked_=true; pressPixel_=pointerPixel_=m->position; }
        if (const auto* m=e->getIf<sf::Event::MouseButtonReleased>(); m && m->button==sf::Mouse::Button::Left) { released_=true; releasePixel_=pointerPixel_=m->position; }
        if (const auto* m=e->getIf<sf::Event::MouseMoved>()) pointerPixel_=m->position;
        if (const auto* wheel = e->getIf<sf::Event::MouseWheelScrolled>(); wheel && wheel->wheel == sf::Mouse::Wheel::Vertical) {
            wheelDirection_ = wheel->delta > 0 ? 1.f : -1.f; wheelTime_ = 0.16f * std::abs(wheel->delta);
        }
        if (const auto* text = e->getIf<sf::Event::TextEntered>(); text && screen_ == Screen::Join) {
            const auto c = text->unicode;
            if (c == 8 && !address_.empty()) address_.pop_back();
            else if (c >= 32 && c < 127 && address_.size() < 80) address_ += static_cast<char>(c);
        }
        if (const auto* key = e->getIf<sf::Event::KeyPressed>()) {
            if (key->scancode == sf::Keyboard::Scancode::F3) showFps_ = !showFps_;
            if (key->scancode == sf::Keyboard::Scancode::M && screen_!=Screen::Join) audio_.toggleMute();
            if (key->scancode == sf::Keyboard::Scancode::F7) audio_.nextTrack();
            if (key->scancode == sf::Keyboard::Scancode::Enter && screen_ == Screen::Join) join();
            if (key->scancode == sf::Keyboard::Scancode::Escape) {
                controls_.reset();
                if (screen_ == Screen::Settings) screen_ = settingsReturn_;
                else if (screen_ == Screen::Playing) paused_ = !paused_;
                else if (screen_ == Screen::Join || screen_ == Screen::Lobby) leave();
            }
        }
    }
}
Input Game::input() const {
    const auto local = client_ ? client_->id() : 0;
    const auto* tank = findTank(state_,local);
    Input result{0,tank ? tank->aim : -0.6f,0,false};
    if (paused_ || screen_ != Screen::Playing || !window_.hasFocus() || state_.phase != Phase::Playing) return result;
    const auto mouse = renderer_.worldMouse(pointerPixel_);
    if (tank) { const auto delta = mouse - tank->pivot(); result.aim = std::atan2(delta.y,delta.x); }
    result = controls_.sample(result.aim);
    if (wheelTime_ > 0) result.powerDelta = std::clamp(result.powerDelta + wheelDirection_,-1.f,1.f);
    return result;
}
void Game::update(float dt) {
    time_ += dt; wheelTime_ = std::max(0.f,wheelTime_-dt); renderer_.update(dt);
    fps_ += (1.f / std::max(0.0001f,dt) - fps_) * std::min(1.f,dt * 3);
    if (client_) {
        const bool wasWelcomed = client_->welcomed(); client_->poll();
        if (!wasWelcomed && client_->welcomed()) renderer_.resize(client_->config());
        state_ = client_->interpolated();
        if (options_.ready && client_->welcomed() && state_.players.size()>=2) {
            const auto me=std::ranges::find(state_.players,client_->id(),&Player::id);
            if (me!=state_.players.end() && !autoReadySent_) {
                if (activeConfig().loadoutSelection) client_->weapon(client_->id()==0 ? WeaponKind::Ricochet : WeaponKind::Mortar);
                client_->ready(true); autoReadySent_=true;
            }
            if (activeConfig().loadoutSelection && client_->id()==0 && !state_.mapSelected && std::ranges::all_of(state_.players,[](const auto& p){return p.ready && p.weaponChosen;})) client_->map(config_.mapId);
        }
        if (state_.phase != Phase::Lobby && screen_ == Screen::Lobby) { screen_ = Screen::Playing; renderer_.resetEffects(); }
        if (state_.phase == Phase::Lobby && screen_ == Screen::Playing) { screen_ = Screen::Lobby; paused_ = false; }
        inputTimer_ += dt;
        if (inputTimer_ >= 1.f / activeConfig().inputRate) { client_->sendInput(input()); inputTimer_ = std::fmod(inputTimer_,1.f/activeConfig().inputRate); }
        effectState_=client_->consumeEffects(); renderer_.observe(effectState_);
    }
    if (solo_ && screen_==Screen::Lobby) {
        solo_->step(); state_=solo_->snapshot();
        if (state_.phase!=Phase::Lobby) { screen_=Screen::Playing; previousSolo_=state_; renderer_.resetEffects(); }
    }
    if (solo_ && screen_ == Screen::Playing && !paused_) {
        accumulator_ += dt;
        while (accumulator_ >= solo_->config().fixedStep) {
            previousSolo_ = solo_->snapshot();
            solo_->setInput(0,input()); solo_->step(); accumulator_ -= solo_->config().fixedStep;
        }
        state_ = solo_->snapshot(); effectState_=state_; renderer_.observe(effectState_);
    }
    if (activeConfig().worldWidth!=viewportWidth_ || activeConfig().worldHeight!=viewportHeight_) {
        renderer_.resize(activeConfig()); viewportWidth_=activeConfig().worldWidth; viewportHeight_=activeConfig().worldHeight;
    }
    window_.setMouseCursorVisible(screen_ != Screen::Playing || paused_ || state_.phase != Phase::Playing || (client_ && !client_->connected()));
    audio_.update(dt,effectState_,activeConfig(),client_ ? client_->id() : 0,
        screen_==Screen::Playing && !paused_ && window_.hasFocus() && (!client_ || client_->connected()),window_.hasFocus());
    if (clicked_ && screen_!=Screen::Playing) audio_.uiClick();
}
void Game::mainMenu(Vec2 mouse) {
    renderer_.text("TANK",{90,150},132,Renderer::white);
    renderer_.text("WARS",{90,262},132,Renderer::white);
    if (renderer_.button({100,483},{390,74},"PLAY",mouse,clicked_,true)) { play(); return; }
    if (renderer_.button({100,572},{185,62},"HOST",mouse,clicked_)) { host(); return; }
    if (renderer_.button({305,572},{185,62},"JOIN",mouse,clicked_)) { screen_ = Screen::Join; status_.clear(); }
    if (renderer_.button({100,650},{185,60},"SETTINGS",mouse,clicked_)) { settingsReturn_ = Screen::Main; screen_ = Screen::Settings; }
    if (renderer_.button({305,650},{185,60},"EXIT",mouse,clicked_)) window_.close();
    renderer_.text("BUILD " TANKWARS_VERSION,{1494,866},14,Renderer::muted,true);
    if (!status_.empty()) renderer_.text(status_,{100,755},19,Renderer::playerColor(1));
}
void Game::joinMenu(Vec2 mouse) {
    renderer_.dim(0.6f);
    renderer_.box({440,202},{720,462},{10,21,35,250},{47,71,88});
    renderer_.text("JOIN",{800,235},46,Renderer::white,true);
    renderer_.text("IP ADDRESS : PORT",{490,359},17,Renderer::accent);
    renderer_.box({490,394},{620,64},{7,15,27},Renderer::accent);
    renderer_.text(address_ + (static_cast<int>(time_*2)%2 ? "_" : ""),{510,404},30,Renderer::white);
    if (renderer_.button({490,538},{295,66},"CONNECT",mouse,clicked_,true)) join();
    if (renderer_.button({815,538},{295,66},"BACK",mouse,clicked_)) leave();
    if (!status_.empty()) renderer_.text(status_,{800,686},20,Renderer::playerColor(1),true);
}
void Game::lobby(Vec2 mouse) {
    renderer_.dim(0.84f);
    const PlayerId local=client_ ? client_->id() : 0;
    const auto it=std::ranges::find(state_.players,local,&Player::id);
    const Player* me=it==state_.players.end() ? nullptr : &*it;
    const auto& c=activeConfig();
    const auto press=renderer_.uiMouse(pressPixel_), release=renderer_.uiMouse(releasePixel_);
    std::optional<std::uint8_t> dropSeat;
    const bool allReady=state_.players.size()>=2 && std::ranges::all_of(state_.players,[](const auto& p){return p.ready && p.weaponChosen;});
    const bool chooseMap=c.loadoutSelection && allReady;
    if (!chooseMap) selectedMap_.reset();
    renderer_.text("LOBBY",{90,42},44,Renderer::white);
    const auto endpoint=solo_ ? "SOLO / SENTINELS" : (server_ ? localAddress_+":"+std::to_string(server_->port()) : address_);
    renderer_.text(endpoint,{94,105},20,Renderer::accent);
    for (unsigned i=0;i<2;++i) {
        const unsigned size=i==0 ? 0 : 2;
        const bool selected=size ? c.teams && c.playerLimit==size*2 : !c.teams;
        if (renderer_.button({1046.f+i*236,60},{220,66},i==0 ? "1 VS 1" : "2 VS 2",mouse,clicked_,selected,local==0)) {
            if (client_) client_->mode(static_cast<std::uint8_t>(size)); else solo_->setMode(local,size);
        }
    }
    const unsigned perSide=c.teams ? c.playerLimit/2 : (c.playerLimit+1)/2;
    for (unsigned side=0;side<2;++side) {
        const float x=90+side*720.f;
        renderer_.text(c.teams ? "TEAM "+std::to_string(side+1) : (side==0 ? "LEFT SIDE" : "RIGHT SIDE"),{x,183},19,Renderer::accent);
        for (unsigned row=0;row<perSide;++row) {
            const auto seat=static_cast<std::uint8_t>(c.teams ? side*perSide+row : row*2+side);
            if (seat>=c.playerLimit) continue;
            const float y=218+row*46.f;
            const auto occupant=std::ranges::find(state_.players,seat,&Player::seat);
            const auto inside=[&](Vec2 point) { return point.x>=x && point.x<x+700 && point.y>=y && point.y<y+41; };
            const bool hover=inside(mouse), pressed=clicked_ && inside(press);
            if (released_ && inside(release)) dropSeat=seat;
            renderer_.box({x,y},{700,41},hover ? sf::Color{27,48,61,250} : sf::Color{11,25,39,245},hover && draggedPlayer_!=noPlayer ? Renderer::accent : sf::Color{44,65,81});
            if (occupant!=state_.players.end()) {
                const auto& p=*occupant;
                renderer_.text(std::to_string(row+1)+"  "+p.name+(p.id==local ? " / YOU" : (p.bot ? " / BOT" : "")),{x+14,y+5},19,Renderer::playerColor(p.color));
                renderer_.text(p.weaponChosen ? weaponName(p.weapon) : "--",{x+350,y+8},16,Renderer::white);
                renderer_.text(p.ready ? "READY" : "PREPARING",{x+584,y+9},15,p.ready ? Renderer::accent : Renderer::muted);
                if (pressed && local==0 && p.bot && press.x>x+340 && press.x<x+570) {
                    auto found=std::ranges::find(selectableWeapons,p.weapon);
                    const auto index=found==selectableWeapons.end() ? 0 : (found-selectableWeapons.begin()+1)%selectableWeapons.size();
                    if (client_) client_->botWeapon(p.id,selectableWeapons[index]); else solo_->setBotWeapon(local,p.id,selectableWeapons[index]);
                } else if (pressed && (local==0 || p.id==local)) draggedPlayer_=p.id;
            } else renderer_.text("OPEN",{x+14,y+7},18,Renderer::muted);
        }
    }
    if (released_) {
        if (dropSeat && draggedPlayer_!=noPlayer) {
            if (client_) client_->seat(draggedPlayer_,*dropSeat); else solo_->setSeat(local,draggedPlayer_,*dropSeat);
        }
        draggedPlayer_=noPlayer;
    }
    renderer_.text(chooseMap ? "BATTLEFIELD" : "WEAPON",{94,419},21,Renderer::accent);
    if (!chooseMap) {
        for (std::size_t i=0;i<selectableWeapons.size();++i) {
            const float x=90+i*290.f; const auto kind=selectableWeapons[i];
            const bool selected=me && me->weaponChosen && me->weapon==kind;
            renderer_.box({x,458},{272,181},{11,24,39,245},selected ? Renderer::accent : sf::Color{49,71,86});
            renderer_.weaponPreview(kind,{x+136,503},time_);
            if (renderer_.button({x+8,540},{256,66},weaponName(kind),mouse,clicked_,selected,me!=nullptr)) {
                if (client_) client_->weapon(kind); else solo_->setWeapon(local,kind);
            }
        }
    } else for (std::uint8_t i=0;i<4;++i) {
        const float x=90+i*363.f;
        renderer_.mapPreview(i,{x,458},{343,127});
        if (renderer_.button({x,597},{343,77},MapFactory::name(i),mouse,clicked_,selectedMap_==i,local==0)) selectedMap_=i;
    }
    for (std::uint8_t i=0;i<maxPlayers;++i) {
        const Vec2 center{113.f+i*53,736};
        const bool taken=std::ranges::any_of(state_.players,[&](const auto& p){return p.id!=local && p.color==i;});
        renderer_.circle(center,14,taken ? sf::Color{40,49,62} : Renderer::playerColor(i));
        if (me && me->color==i) renderer_.circle(center,20,sf::Color::Transparent,Renderer::white);
        if (clicked_ && !chooseMap && length(mouse-center)<20 && !taken) {
            if (client_) client_->color(i); else solo_->setColor(local,i);
        }
    }
    if (chooseMap) {
        if (renderer_.button({520,705},{480,74},"CHANGE LOADOUT",mouse,clicked_,true,me!=nullptr)) {
            if (client_) client_->ready(false); else solo_->setReady(local,false);
            selectedMap_.reset();
        }
        if (renderer_.button({1020,705},{480,74},"CONFIRM MAP",mouse,clicked_,true,local==0 && selectedMap_.has_value())) {
            if (client_) client_->map(*selectedMap_); else solo_->setMap(local,*selectedMap_);
        }
        if (local!=0) renderer_.text("HOST IS CHOOSING THE BATTLEFIELD",{1020,797},15,Renderer::muted);
    } else if (renderer_.button({1020,705},{480,74},me && me->ready ? "CHANGE LOADOUT" : "CONFIRM WEAPON",mouse,clicked_,true,me && (me->weaponChosen || !c.loadoutSelection))) {
        if (client_) client_->ready(!me->ready); else solo_->setReady(local,!me->ready);
    }
    if (renderer_.button({90,811},{255,61},solo_ ? "MAIN MENU" : "LEAVE ROOM",mouse,clicked_)) leave();
}
void Game::settingsMenu(Vec2 mouse) {
    renderer_.dim(0.65f);
    renderer_.box({430,65},{740,750},{10,21,35,250},{46,69,86});
    renderer_.text("SETTINGS",{800,91},44,Renderer::white,true);
    auto toggle = [&](float y, const std::string& title, bool& value) {
        renderer_.text(title,{480,y+12},25,Renderer::white);
        if (renderer_.button({950,y},{170,61},value ? "ON" : "OFF",mouse,clicked_,value)) value = !value;
        renderer_.line({480,y+79},{1120,y+79},{35,53,71});
    };
    toggle(170,"CAMERA SHAKE",renderer_.settings.shake);
    toggle(255,"PARTICLES",renderer_.settings.particles);
    toggle(340,"AIMING GUIDE",renderer_.settings.trajectory);
    renderer_.text("FRAME LIMIT",{480,448},25,Renderer::white);
    if (renderer_.button({950,437},{170,61},renderer_.settings.frameLimit ? std::to_string(renderer_.settings.frameLimit) + " FPS" : "UNLIMITED",mouse,clicked_)) {
        auto& limit = renderer_.settings.frameLimit; limit = limit == 60 ? 120 : (limit == 120 ? 0 : 60); window_.setFramerateLimit(limit);
    }
    const auto volume=[&](float y,const std::string& title,float level,bool music) {
        renderer_.text(title,{480,y+9},25,Renderer::white);
        if (renderer_.button({820,y},{70,57},"-",mouse,clicked_)) {
            if (music) audio_.setMusicVolume(level-10); else audio_.setEffectsVolume(level-10);
        }
        renderer_.text(std::to_string(static_cast<int>(level))+"%",{963,y+10},25,Renderer::accent,true);
        if (renderer_.button({1050,y},{70,57},"+",mouse,clicked_)) {
            if (music) audio_.setMusicVolume(level+10); else audio_.setEffectsVolume(level+10);
        }
    };
    volume(518,"SOUND EFFECTS",audio_.effectsVolume(),false);
    volume(591,"MUSIC",audio_.musicVolume(),true);
    if (renderer_.button({480,725},{640,61},"BACK",mouse,clicked_,true)) screen_ = settingsReturn_;
}
void Game::matchMenu(Vec2 mouse) {
    if (client_ && !client_->connected()) return;
    if (state_.phase == Phase::Victory) {
        renderer_.dim(0.76f);
        const auto* winner = findTank(state_,state_.winner);
        const auto color = winner ? Renderer::playerColor(winner->color) : Renderer::accent;
        renderer_.box({440,234},{720,438},{11,23,37,245},{49,75,91}); renderer_.box({440,234},{720,3},color);
        const auto* you = findTank(state_,client_ ? client_->id() : 0);
        const bool won = winner && you && winner->team == you->team;
        renderer_.text(winner ? (won ? "VICTORY" : "DEFEAT") : "DRAW",{800,320},winner ? 76 : 46,Renderer::white,true);
        if (!client_ || client_->id() == 0) {
            if (renderer_.button({490,510},{295,76},"PLAY AGAIN",mouse,clicked_,true)) {
                if (client_) client_->rematch(); else play(); return;
            }
        } else renderer_.text("WAITING",{630,534},21,Renderer::muted,true);
        if (renderer_.button({815,510},{295,76},"MAIN MENU",mouse,clicked_)) leave();
    } else if (paused_) {
        renderer_.dim(0.7f); renderer_.box({540,220},{520,484},{10,21,35,245},{47,70,87});
        renderer_.text(client_ ? "FIELD MENU" : "PAUSED",{800,255},60,Renderer::white,true);
        if (renderer_.button({590,395},{420,66},"RESUME",mouse,clicked_,true)) paused_ = false;
        if (renderer_.button({590,479},{420,66},"SETTINGS",mouse,clicked_)) { settingsReturn_ = Screen::Playing; screen_ = Screen::Settings; }
        if (renderer_.button({590,563},{420,66},"MAIN MENU",mouse,clicked_)) leave();
    }
}
void Game::draw() {
    window_.clear({5,9,17});
    renderer_.scenery(activeConfig(),time_,screen_ == Screen::Main || (screen_ == Screen::Settings && settingsReturn_ == Screen::Main),screen_==Screen::Playing ? &state_ : nullptr);
    const auto mouse = renderer_.uiMouse(clicked_ ? pressPixel_ : pointerPixel_);
    switch (screen_) {
    case Screen::Main: mainMenu(mouse); break;
    case Screen::Join: joinMenu(mouse); break;
    case Screen::Lobby: lobby(mouse); break;
    case Screen::Playing: {
        auto visual = state_;
        if (solo_ && !paused_ && state_.phase == Phase::Playing) {
            const float t = std::clamp(accumulator_ / solo_->config().fixedStep,0.f,1.f);
            for (auto& tank : visual.tanks) if (const auto* prev = findTank(previousSolo_,tank.id)) {
                tank.position = mix(prev->position,tank.position,t);
                tank.aim = prev->aim + angleDelta(prev->aim,tank.aim)*t;
                tank.bodyAngle = prev->bodyAngle + angleDelta(prev->bodyAngle,tank.bodyAngle)*t;
                tank.track = prev->track + (tank.track-prev->track)*t;
            }
            for (auto& p : visual.projectiles) {
                const auto prev = std::ranges::find(previousSolo_.projectiles,p.id,&Projectile::id);
                if (prev != previousSolo_.projectiles.end()) p.position = mix(prev->position,p.position,t);
            }
            for (auto& bomb : visual.bombs) {
                const auto prev=std::ranges::find(previousSolo_.bombs,bomb.id,&AirBomb::id);
                if (prev!=previousSolo_.bombs.end()) bomb.position=mix(prev->position,bomb.position,t);
            }
        }
        renderer_.battle(activeConfig(),visual,client_ ? client_->id() : 0,renderer_.worldMouse(pointerPixel_),!paused_ && window_.hasFocus());
        renderer_.hud(activeConfig(),state_,client_ ? client_->id() : 0);
        matchMenu(mouse); break;
    }
    case Screen::Settings: settingsMenu(mouse); break;
    }
    if (client_ && !client_->connected()) {
        renderer_.dim(0.85f); renderer_.text("SIGNAL LOST",{800,320},64,Renderer::white,true);
        renderer_.text(client_->error(),{800,420},24,Renderer::muted,true);
        if (renderer_.button({600,500},{400,75},"MAIN MENU",mouse,clicked_,true)) leave();
    }
    if (showFps_) {
        renderer_.text(std::to_string(static_cast<int>(fps_)) + " FPS / " TANKWARS_VERSION,{1450,4},16,Renderer::accent,true);
        if (client_ && client_->welcomed()) renderer_.text(client_->latest().detailedWeapons ? "PROJECTILE FX / FULL" : "PROJECTILE FX / LEGACY HOST",{800,54},14,client_->latest().detailedWeapons ? Renderer::accent : Renderer::playerColor(1),true);
    }
    window_.display();
}
int Game::run() {
    sf::Clock clock;
    while (window_.isOpen()) {
        const float dt = std::min(0.1f,clock.restart().asSeconds());
        events(); if (!window_.isOpen()) break;
        update(dt); draw(); ++frame_;
        if (options_.frames && frame_ >= options_.frames) {
            if (!options_.screenshot.empty()) {
                sf::Texture texture;
                if (!texture.resize(window_.getSize())) throw std::runtime_error("Screenshot texture failed");
                texture.update(window_);
                if (!texture.copyToImage().saveToFile(options_.screenshot)) throw std::runtime_error("Screenshot save failed");
                std::cout << "Screenshot: " << options_.screenshot << '\n';
            }
            window_.close();
        }
    }
    return 0;
}
}
