#include "tankwars/Game.hpp"
#include <atomic>
#include <charconv>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
volatile std::sig_atomic_t stopping = 0;
void signalHandler(int) { stopping = 1; }
unsigned number(const std::string& s, const char* option, unsigned max = 1000000) {
    unsigned value{};
    auto [end,error] = std::from_chars(s.data(),s.data()+s.size(),value);
    if (error != std::errc{} || end != s.data()+s.size() || value == 0 || value > max) throw std::runtime_error(std::string("Invalid value for ") + option);
    return value;
}
std::filesystem::path resourceRoot(const char* executable) {
    auto path = std::filesystem::absolute(executable).parent_path();
    if (std::filesystem::exists(path / "assets/fonts/Rajdhani-SemiBold.ttf")) return path;
    if (std::filesystem::exists(path / "../Resources/assets/fonts/Rajdhani-SemiBold.ttf")) return path / "../Resources";
    path = std::filesystem::current_path();
    if (std::filesystem::exists(path / "assets/fonts/Rajdhani-SemiBold.ttf")) return path;
    return TANKWARS_SOURCE_DIR;
}
}
int main(int argc, char** argv) {
    try {
        tw::LaunchOptions options;
        const auto root = resourceRoot(argv[0]); options.assets = root / "assets"; options.config = root / "config/game.cfg";
        unsigned port = 0, players = 0;
        for (int i=1; i<argc; ++i) {
            const std::string arg = argv[i];
            if (arg=="--version") { std::cout<<"Tank Wars " TANKWARS_VERSION "\n"; return 0; }
            const auto next = [&]() -> std::string { if (++i >= argc) throw std::runtime_error("Missing value for " + arg); return argv[i]; };
            if (arg == "--help" || arg == "-h") {
                std::cout << "Tank Wars / Nightfall\n"
                    "  --host                Create a LAN room\n  --join IP[:PORT]      Join a LAN room\n"
                    "  --port N              Listen/connect port (53000)\n  --players N           Room capacity: 2-4\n"
                    "  --demo                Start a solo duel\n  --ready               Automatically ready in lobby\n"
                    "  --server              Headless authoritative server\n  --seconds N           Headless server duration\n"
                    "  --config FILE         Gameplay configuration\n  --assets DIR          Bundled assets directory\n"
                    "  --frames N            Exit after N rendered frames\n  --screenshot FILE     Capture final frame (with --frames)\n"; return 0;
            }
            if (arg == "--host") options.host = true;
            else if (arg == "--join") options.join = next();
            else if (arg == "--port") port = number(next(),"--port",65535);
            else if (arg == "--players") players = number(next(),"--players",4);
            else if (arg == "--demo") options.demo = true;
            else if (arg == "--ready") options.ready = true;
            else if (arg == "--server") options.server = true;
            else if (arg == "--seconds") options.seconds = number(next(),"--seconds");
            else if (arg == "--frames") options.frames = number(next(),"--frames");
            else if (arg == "--screenshot") options.screenshot = next();
            else if (arg == "--config") options.config = next();
            else if (arg == "--assets") options.assets = next();
            else throw std::runtime_error("Unknown option: " + arg);
        }
        const unsigned modes = static_cast<unsigned>(options.host) + static_cast<unsigned>(!options.join.empty()) + static_cast<unsigned>(options.demo) + static_cast<unsigned>(options.server);
        if (modes > 1) throw std::runtime_error("Choose one of --host, --join, --demo, --server");
        if (!options.screenshot.empty() && !options.frames) throw std::runtime_error("--screenshot requires --frames");
        auto config = tw::Config::load(options.config);
        if (port) config.port = static_cast<unsigned short>(port);
        if (players) config.playerLimit = players;
        config.validate();
        if (options.server) {
            std::signal(SIGINT,signalHandler); std::signal(SIGTERM,signalHandler);
            tw::net::Server server(config,config.port);
            std::cout << "Tank Wars server: " << tw::net::localAddress() << ':' << server.port() << " / " << config.playerLimit << " pilots\n" << std::flush;
            const auto start = std::chrono::steady_clock::now();
            while (!stopping) {
                if (options.seconds && std::chrono::steady_clock::now()-start >= std::chrono::seconds(options.seconds)) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            return 0;
        }
        return tw::Game(config,std::move(options)).run();
    } catch (const std::exception& e) { std::cerr << "Tank Wars: " << e.what() << '\n'; return 1; }
}
