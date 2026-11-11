#include "tankwars/Game.hpp"
#include <iostream>

namespace tw {
Game::Game(Config config, LaunchOptions options)
    : config_(config), options_(std::move(options)),
      window_(sf::VideoMode({config.windowWidth,config.windowHeight}), "Tank Wars / Nightfall", sf::Style::Default,
              sf::State::Windowed, sf::ContextSettings{.antiAliasingLevel = 4}),
      renderer_(window_, options_.assets), localAddress_(net::localAddress()) {
    window_.setFramerateLimit(config.frameLimit); window_.setKeyRepeatEnabled(false);
    renderer_.settings.frameLimit = config.frameLimit; renderer_.resize(config_);
    if (options_.demo) play();
    else if (options_.host) host();
    else if (!options_.join.empty()) { address_ = options_.join; join(); }
}
const Config& Game::activeConfig() const { return client_ && client_->welcomed() ? client_->config() : config_; }
void Game::play() {
    leave();
    auto config = config_; config.playerLimit = 2; config.teams = false;
    solo_ = std::make_unique<GameSession>(config);
    solo_->addPlayer("PILOT 01"); solo_->addPlayer("SENTINEL",true);
    solo_->setReady(0,true); solo_->setReady(1,true); solo_->step();
    state_ = solo_->snapshot(); previousSolo_ = state_; screen_ = Screen::Playing; renderer_.resetEffects();
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
    auto address = address_;
    auto port = config_.port;
    if (const auto colon = address.rfind(':'); colon != std::string::npos) {
        try {
            std::size_t used{}; const auto number = std::stoul(address.substr(colon+1), &used);
            if (used != address.size() - colon - 1 || number == 0 || number > 65535) throw std::invalid_argument("port");
            port = static_cast<unsigned short>(number); address.resize(colon);
        } catch (...) { status_ = "Use an address like 192.168.1.12:53000"; return; }
    }
    client_ = std::make_unique<net::Client>();
    if (client_->connect(address,port,"PILOT 02")) { status_.clear(); screen_ = Screen::Lobby; autoReadySent_ = false; }
    else { status_ = client_->error(); client_.reset(); screen_ = Screen::Join; }
}
void Game::leave() {
    client_.reset(); server_.reset(); solo_.reset(); state_ = {};
    paused_ = false; accumulator_ = 0; inputTimer_ = 0; wheelTime_ = 0; autoReadySent_ = false;
    controls_.reset();
    screen_ = Screen::Main; status_.clear(); renderer_.resetEffects(); renderer_.resize(config_);
}
void Game::events() {
    clicked_ = false;
    while (const auto e = window_.pollEvent()) {
        controls_.handle(*e);
        if (e->is<sf::Event::Closed>()) window_.close();
        if (e->is<sf::Event::Resized>()) renderer_.resize(activeConfig());
        if (e->is<sf::Event::FocusLost>()) {
            wheelTime_ = 0;
            if (screen_ == Screen::Playing) paused_ = true;
        }
        if (const auto* m = e->getIf<sf::Event::MouseButtonPressed>(); m && m->button == sf::Mouse::Button::Left) clicked_ = true;
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
    const auto mouse = renderer_.worldMouse(sf::Mouse::getPosition(window_));
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
        if (options_.ready && client_->welcomed() && !autoReadySent_) { client_->ready(true); autoReadySent_ = true; }
        if (state_.phase != Phase::Lobby && screen_ == Screen::Lobby) { screen_ = Screen::Playing; renderer_.resetEffects(); }
        if (state_.phase == Phase::Lobby && screen_ == Screen::Playing) { screen_ = Screen::Lobby; paused_ = false; }
        inputTimer_ += dt;
        if (inputTimer_ >= 1.f / activeConfig().inputRate) { client_->sendInput(input()); inputTimer_ = std::fmod(inputTimer_,1.f/activeConfig().inputRate); }
        renderer_.observe(client_->latest());
    }
    if (solo_ && screen_ == Screen::Playing && !paused_) {
        accumulator_ += dt;
        while (accumulator_ >= solo_->config().fixedStep) {
            previousSolo_ = solo_->snapshot();
            solo_->setInput(0,input()); solo_->step(); accumulator_ -= solo_->config().fixedStep;
        }
        state_ = solo_->snapshot(); renderer_.observe(state_);
    }
    window_.setMouseCursorVisible(screen_ != Screen::Playing || paused_ || state_.phase != Phase::Playing || (client_ && !client_->connected()));
}
void Game::mainMenu(Vec2 mouse) {
    renderer_.text("REAL-TIME ARTILLERY / VOL. 01",{100,105},20,Renderer::accent);
    renderer_.text("TANK",{90,150},132,Renderer::white);
    renderer_.text("WARS",{90,262},132,Renderer::white);
    renderer_.text(config_.titanMode ? "Heavy armor. Unstoppable light." : "A quiet battlefield. A perfect shot.",{101,418},25,Renderer::muted);
    if (renderer_.button({100,483},{390,74},"PLAY",config_.titanMode ? (config_.asymmetricLoadouts ? "Titan duel / napalm vs photon" : "Titan vs Titan / photon sandbox") : "Solo duel against Sentinel",mouse,clicked_,true)) { play(); return; }
    if (renderer_.button({100,572},{185,62},"HOST","Create LAN room",mouse,clicked_)) { host(); return; }
    if (renderer_.button({305,572},{185,62},"JOIN","Connect by IP",mouse,clicked_)) { screen_ = Screen::Join; status_.clear(); }
    if (renderer_.button({100,650},{185,60},"SETTINGS","Tune your view",mouse,clicked_)) { settingsReturn_ = Screen::Main; screen_ = Screen::Settings; }
    if (renderer_.button({305,650},{185,60},"EXIT","Until next time",mouse,clicked_)) window_.close();
    renderer_.text("01 / NIGHTFALL BASIN",{1050,730},19,Renderer::accent,true);
    renderer_.text("2-4 PILOTS     /     ONE BATTLEFIELD     /     NO TURNS",{1050,761},16,Renderer::muted,true);
    renderer_.line({100,813},{1500,813},{43,65,80});
    renderer_.text("LOCAL NETWORK. GLOBAL RIVALRY.",{100,836},17,Renderer::muted);
    renderer_.text("C++23 / SFML 3",{1400,836},17,Renderer::muted,true);
    if (!status_.empty()) renderer_.text(status_,{100,755},19,Renderer::playerColor(1));
}
void Game::joinMenu(Vec2 mouse) {
    renderer_.dim(0.6f);
    renderer_.box({440,202},{720,462},{10,21,35,250},{47,71,88});
    renderer_.text("FIND YOUR RIVAL",{800,235},46,Renderer::white,true);
    renderer_.text("Enter the host's local IP. Use 127.0.0.1 on this Mac.",{800,302},21,Renderer::muted,true);
    renderer_.text("IP ADDRESS : PORT",{490,359},17,Renderer::accent);
    renderer_.box({490,394},{620,64},{7,15,27},Renderer::accent);
    renderer_.text(address_ + (static_cast<int>(time_*2)%2 ? "_" : ""),{510,404},30,Renderer::white);
    renderer_.text("Default port: " + std::to_string(config_.port),{490,471},18,Renderer::muted);
    if (renderer_.button({490,538},{295,66},"CONNECT","ENTER",mouse,clicked_,true)) join();
    if (renderer_.button({815,538},{295,66},"BACK","ESC",mouse,clicked_)) leave();
    if (!status_.empty()) renderer_.text(status_,{800,686},20,Renderer::playerColor(1),true);
}
void Game::lobby(Vec2 mouse) {
    renderer_.dim(0.35f);
    renderer_.text(server_ ? "YOUR ROOM" : "CONNECTED",{100,80},56,Renderer::white);
    const std::string endpoint = server_ ? localAddress_ + ":" + std::to_string(server_->port()) : address_;
    renderer_.text("LAN  /  " + endpoint,{103,154},25,Renderer::accent);
    const auto health = std::to_string(static_cast<int>(activeConfig().titanHealth));
    renderer_.text(activeConfig().titanMode ? "TITAN DUEL / " + health + " HP per pilot. Lasers and aerial stomps are lethal." : "Choose a unique color. The match begins when everyone is ready.",{103,201},23,Renderer::muted);
    const auto local = client_ ? client_->id() : noPlayer;
    const Player* me = nullptr;
    const unsigned limit = activeConfig().playerLimit;
    const float width = limit > 2 ? 320.f : 650.f;
    for (unsigned i = 0; i < limit; ++i) {
        const float x = 100 + i * (width + 32);
        const Player* p = i < state_.players.size() ? &state_.players[i] : nullptr;
        const auto color = p ? Renderer::playerColor(p->color) : Renderer::muted;
        renderer_.box({x,288},{width,270},{11,23,37,240},{42,64,80}); renderer_.box({x,288},{width,3},color);
        renderer_.text("PILOT / 0" + std::to_string(i+1),{x+27,314},18,Renderer::muted);
        renderer_.text(p ? p->name : "WAITING...",{x+27,357},limit > 2 ? 31 : 43,p ? color : Renderer::muted);
        if (p) {
            if (p->id == local) me = p;
            renderer_.text(p->id == local ? "THIS IS YOU" : "CONNECTED ON LAN",{x+27,420},18,Renderer::muted);
            renderer_.circle({x+35,504},5,p->ready ? Renderer::accent : Renderer::playerColor(3));
            renderer_.text(p->ready ? "READY" : "CHOOSING LOADOUT",{x+52,488},22,p->ready ? Renderer::accent : Renderer::muted);
        } else renderer_.text("Share the room address to invite a friend.",{x+27,431},limit > 2 ? 15 : 22,Renderer::muted);
    }
    renderer_.text("YOUR COLOR",{103,605},17,Renderer::muted);
    for (std::uint8_t i = 0; i < 6; ++i) {
        const Vec2 center{123.f + i*58,665};
        const bool taken = std::ranges::any_of(state_.players,[&](const auto& p) { return p.id != local && p.color == i; });
        renderer_.circle(center,17,taken ? sf::Color{37,48,63} : Renderer::playerColor(i));
        if (me && me->color == i) renderer_.circle(center,23,sf::Color::Transparent,Renderer::white);
        if (taken) renderer_.line(center - Vec2{9,9},center + Vec2{9,9},Renderer::muted,2);
        if (clicked_ && length(mouse-center)<23 && !taken && client_) client_->color(i);
    }
    if (renderer_.button({1050,621},{350,77},me && me->ready ? "CANCEL READY" : "READY UP", "All pilots must confirm", mouse,clicked_,true,me != nullptr)) client_->ready(!me->ready);
    if (renderer_.button({100,766},{250,64},"LEAVE ROOM","ESC",mouse,clicked_)) leave();
    renderer_.text(server_ ? "SAME COMPUTER? JOIN 127.0.0.1:" + std::to_string(config_.port) : "WAITING FOR ALL PILOTS",{850,795},19,Renderer::muted,true);
}
void Game::settingsMenu(Vec2 mouse) {
    renderer_.dim(0.65f);
    renderer_.box({430,130},{740,640},{10,21,35,250},{46,69,86});
    renderer_.text("MAKE IT YOURS",{800,165},47,Renderer::white,true);
    auto toggle = [&](float y, const std::string& title, const std::string& detail, bool& value) {
        renderer_.text(title,{480,y},25,Renderer::white); renderer_.text(detail,{480,y+31},17,Renderer::muted);
        if (renderer_.button({950,y},{170,61},value ? "ON" : "OFF","",mouse,clicked_,value)) value = !value;
        renderer_.line({480,y+79},{1120,y+79},{35,53,71});
    };
    toggle(265,"CAMERA SHAKE","A little impact with every explosion",renderer_.settings.shake);
    toggle(367,"PARTICLES","Neon sparks and fragments",renderer_.settings.particles);
    toggle(469,"AIMING GUIDE","Your trajectory stays on your screen",renderer_.settings.trajectory);
    renderer_.text("FRAME LIMIT",{480,574},25,Renderer::white);
    if (renderer_.button({950,571},{170,61},renderer_.settings.frameLimit ? std::to_string(renderer_.settings.frameLimit) + " FPS" : "UNLIMITED","",mouse,clicked_)) {
        auto& limit = renderer_.settings.frameLimit; limit = limit == 60 ? 120 : (limit == 120 ? 0 : 60); window_.setFramerateLimit(limit);
    }
    renderer_.text("Gameplay balance: config/game.cfg",{480,614},18,Renderer::muted);
    if (renderer_.button({480,678},{640,61},"BACK","",mouse,clicked_,true)) screen_ = settingsReturn_;
}
void Game::matchMenu(Vec2 mouse) {
    if (client_ && !client_->connected()) return;
    if (state_.phase == Phase::Victory) {
        renderer_.dim(0.76f);
        const auto* winner = findTank(state_,state_.winner);
        const auto color = winner ? Renderer::playerColor(winner->color) : Renderer::accent;
        renderer_.box({440,234},{720,438},{11,23,37,245},{49,75,91}); renderer_.box({440,234},{720,3},color);
        renderer_.text("BATTLE / COMPLETE",{800,272},20,color,true);
        const auto* you = findTank(state_,client_ ? client_->id() : 0);
        const bool won = winner && you && winner->team == you->team;
        renderer_.text(winner ? (won ? "VICTORY" : "WELL FOUGHT") : "MUTUAL DESTRUCTION",{800,320},winner ? 76 : 46,Renderer::white,true);
        std::string name;
        for (const auto& p : state_.players) if (p.id == state_.winner) name = p.name;
        renderer_.text(winner ? (activeConfig().teams ? "TEAM " + std::to_string(winner->team+1) + " WINS" : name + " TAKES THE FIELD") : "NO TANK LEFT STANDING",{800,425},24,color,true);
        if (!client_ || client_->id() == 0) {
            if (renderer_.button({490,510},{295,76},"PLAY AGAIN",client_ ? "Return to the room" : "A fresh duel",mouse,clicked_,true)) {
                if (client_) client_->rematch(); else play(); return;
            }
        } else renderer_.text("WAITING FOR HOST'S REMATCH",{630,534},21,Renderer::muted,true);
        if (renderer_.button({815,510},{295,76},"MAIN MENU","Leave the battlefield",mouse,clicked_)) leave();
    } else if (paused_) {
        renderer_.dim(0.7f); renderer_.box({540,220},{520,484},{10,21,35,245},{47,70,87});
        renderer_.text(client_ ? "FIELD MENU" : "PAUSED",{800,255},60,Renderer::white,true);
        renderer_.text(client_ ? "The LAN battle continues while this menu is open." : "Take your time. Line up the next shot.",{800,337},19,Renderer::muted,true);
        if (renderer_.button({590,395},{420,66},"RESUME","ESC",mouse,clicked_,true)) paused_ = false;
        if (renderer_.button({590,479},{420,66},"SETTINGS","Adjust your view",mouse,clicked_)) { settingsReturn_ = Screen::Playing; screen_ = Screen::Settings; }
        if (renderer_.button({590,563},{420,66},"MAIN MENU","Leave the battlefield",mouse,clicked_)) leave();
    }
}
void Game::draw() {
    window_.clear({5,9,17});
    renderer_.scenery(activeConfig(),time_,screen_ == Screen::Main || (screen_ == Screen::Settings && settingsReturn_ == Screen::Main));
    const auto mouse = renderer_.uiMouse(sf::Mouse::getPosition(window_));
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
        }
        renderer_.battle(activeConfig(),visual,client_ ? client_->id() : 0,renderer_.worldMouse(sf::Mouse::getPosition(window_)),!paused_ && window_.hasFocus());
        renderer_.hud(activeConfig(),state_,client_ ? client_->id() : 0,client_ != nullptr);
        matchMenu(mouse); break;
    }
    case Screen::Settings: settingsMenu(mouse); break;
    }
    if (client_ && !client_->connected()) {
        renderer_.dim(0.85f); renderer_.text("SIGNAL LOST",{800,320},64,Renderer::white,true);
        renderer_.text(client_->error(),{800,420},24,Renderer::muted,true);
        if (renderer_.button({600,500},{400,75},"MAIN MENU","Reconnect from the menu",mouse,clicked_,true)) leave();
    }
    if (showFps_) renderer_.text(std::to_string(static_cast<int>(fps_)) + " FPS",{1490,4},16,Renderer::accent,true);
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
