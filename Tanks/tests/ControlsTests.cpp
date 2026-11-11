#include "tankwars/Controls.hpp"
#include "tankwars/GameSession.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using Scan = sf::Keyboard::Scancode;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void press(tw::Controls& controls, Scan scan) {
    controls.handle(sf::Event::KeyPressed{.code = sf::Keyboard::Key::Unknown, .scancode = scan});
}
void release(tw::Controls& controls, Scan scan) {
    controls.handle(sf::Event::KeyReleased{.code = sf::Keyboard::Key::Unknown, .scancode = scan});
}
void run() {
    tw::Config config;
    config.asymmetricLoadouts = false;
    tw::GameSession session(config);
    session.addPlayer("LOCAL"); session.addPlayer("REMOTE");
    session.setReady(0, true); session.setReady(1, true); session.step();
    const auto before = session.snapshot().tanks[0];
    tw::Controls controls;
    press(controls, Scan::D); press(controls, Scan::W); press(controls, Scan::Space);
    for (int i = 0; i < 60; ++i) {
        session.setInput(0, controls.sample(-0.6f));
        session.step();
    }
    const auto held = session.snapshot();
    require(held.tanks[0].position.x > before.position.x + 30, "held D moves tank without repeated key events");
    require(held.tanks[0].power > before.power + 100, "held W continuously raises power");
    require(held.tanks[0].reload > 0 && held.projectiles.size() == 1, "Space fires without global keyboard polling");
    release(controls, Scan::D); release(controls, Scan::W); release(controls, Scan::Space);
    const auto stopped = controls.sample(-0.6f);
    for (int i = 0; i < 60; ++i) { session.setInput(0, stopped); session.step(); }
    const auto released = session.snapshot().tanks[0];
    require(released.position.x == held.tanks[0].position.x && released.power == held.tanks[0].power, "release stops movement and power changes");
    press(controls, Scan::A); press(controls, Scan::D);
    press(controls, Scan::W); press(controls, Scan::S);
    require(controls.sample(0).move == 0 && controls.sample(0).powerDelta == 0, "opposite directions cancel");
    release(controls, Scan::D); release(controls, Scan::W);
    require(controls.sample(0).move == -1 && controls.sample(0).powerDelta == -1, "remaining held direction survives release");
    press(controls, Scan::Up);
    require(controls.sample(0).lift == 1, "Up holds jump and flight thrust");
    press(controls, Scan::Down);
    require(controls.sample(0).lift == 0, "opposite flight arrows cancel");
    release(controls, Scan::Up);
    require(controls.sample(0).lift == -1, "Down holds descent");
    press(controls, Scan::Space);
    controls.handle(sf::Event::FocusLost{});
    controls.handle(sf::Event::FocusGained{});
    auto neutral = controls.sample(0);
    require(neutral.move == 0 && neutral.powerDelta == 0 && !neutral.fire && neutral.lift == 0, "focus loss clears keys even if release happens in another window");
    press(controls, Scan::Unknown);
    require(controls.sample(0).move == 0, "unknown scancode ignored");
    press(controls, Scan::A); press(controls, Scan::A);
    require(controls.sample(0).move == -1, "repeated keydown is idempotent");
    controls.reset();
    require(controls.sample(0).move == 0, "new session clears held keys");
    press(controls,Scan::Up);
    require(controls.sample(0).lift == 1,"Up activates lift independently of cannon power");
    press(controls,Scan::Down);
    require(controls.sample(0).lift == 0,"opposite vertical commands cancel");
    controls.handle(sf::Event::FocusLost{});
    require(controls.sample(0).lift == 0,"focus loss clears flight controls");
}
}
int main() {
    try { run(); std::cout << "Keyboard regression passed: held input, release, physical keys, focus, combat\n"; return 0; }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
