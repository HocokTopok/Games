#pragma once
#include "Network.hpp"
#include "Renderer.hpp"
#include "Controls.hpp"
#include "Audio.hpp"

namespace tw {
struct LaunchOptions {
    std::filesystem::path assets, config, screenshot;
    std::string join;
    bool host{}, demo{}, ready{}, server{};
    unsigned frames{}, seconds{};
};
class Game {
public:
    Game(Config config, LaunchOptions options);
    int run();
private:
    enum class Screen { Main, Join, Lobby, Playing, Settings };
    void events();
    void update(float dt);
    void draw();
    void mainMenu(Vec2 mouse);
    void joinMenu(Vec2 mouse);
    void lobby(Vec2 mouse);
    void settingsMenu(Vec2 mouse);
    void matchMenu(Vec2 mouse);
    void play();
    void host();
    void join();
    void leave();
    Input input() const;
    const Config& activeConfig() const;
    Config config_;
    LaunchOptions options_;
    sf::RenderWindow window_;
    Renderer renderer_;
    SoundSystem audio_;
    Controls controls_;
    Screen screen_{Screen::Main}, settingsReturn_{Screen::Main};
    bool paused_{}, clicked_{}, autoReadySent_{}, showFps_{};
    float accumulator_{}, inputTimer_{}, wheelTime_{}, wheelDirection_{}, time_{}, fps_{};
    float viewportWidth_{}, viewportHeight_{};
    unsigned frame_{};
    std::string address_{"127.0.0.1"}, localAddress_, status_;
    std::unique_ptr<GameSession> solo_;
    std::unique_ptr<net::Server> server_;
    std::unique_ptr<net::Client> client_;
    Snapshot state_;
    Snapshot effectState_;
    Snapshot previousSolo_;
};
}
