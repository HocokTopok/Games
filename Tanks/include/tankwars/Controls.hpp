#pragma once
#include "Types.hpp"
#include <SFML/Window/Event.hpp>
#include <array>

namespace tw {
class Controls {
public:
    void handle(const sf::Event& event) {
        if (const auto* key = event.getIf<sf::Event::KeyPressed>()) set(key->scancode, true);
        if (const auto* key = event.getIf<sf::Event::KeyReleased>()) set(key->scancode, false);
        if (event.is<sf::Event::FocusLost>() || event.is<sf::Event::Closed>()) reset();
    }
    void reset() { held_.fill(false); }
    Input sample(float aim) const {
        using Scan = sf::Keyboard::Scancode;
        return {static_cast<float>(down(Scan::D)) - static_cast<float>(down(Scan::A)), aim,
                static_cast<float>(down(Scan::W)) - static_cast<float>(down(Scan::S)), down(Scan::Space),
                static_cast<float>(down(Scan::Up)) - static_cast<float>(down(Scan::Down))};
    }
private:
    void set(sf::Keyboard::Scancode code, bool value) {
        const int index = static_cast<int>(code);
        if (index >= 0 && static_cast<std::size_t>(index) < held_.size()) held_[static_cast<std::size_t>(index)] = value;
    }
    bool down(sf::Keyboard::Scancode code) const { return held_[static_cast<std::size_t>(code)]; }
    std::array<bool, sf::Keyboard::ScancodeCount> held_{};
};
}
