#pragma once
#include <cstdint>
#include <vector>

namespace tw::sound {
constexpr unsigned sampleRate=48000;
enum class Cue : unsigned { MortarShot, MortarWhistle, MortarBlast, OrbShot, OrbHum, OrbBlast, Ricochet,
    Thruster, Fire, Engine, Destroyed, Impact, CannonShot, LaserShot, Ui, Victory, Count };
struct Clip { std::vector<std::int16_t> samples; bool loop{}; };
Clip synthesize(Cue cue);
}
