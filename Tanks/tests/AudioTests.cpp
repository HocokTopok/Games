#include "tankwars/AudioSynth.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void run() {
    for (unsigned i=0;i<static_cast<unsigned>(tw::sound::Cue::Count);++i) {
        const auto clip=tw::sound::synthesize(static_cast<tw::sound::Cue>(i));
        require(clip.samples.size()>=tw::sound::sampleRate/2,"all effects contain playable audio");
        double energy=0; int peak=0;
        for (auto sample : clip.samples) { peak=std::max(peak,std::abs(static_cast<int>(sample))); energy+=static_cast<double>(sample)*sample; }
        require(peak>20000 && peak<30000,"effects have headroom below clipping");
        require(std::sqrt(energy/clip.samples.size())>500,"effects are not silent");
        if (clip.loop) require(std::abs(static_cast<int>(clip.samples.front())-clip.samples.back())<4000,"continuous effects have no large seam discontinuity");
        else require(clip.samples.front()==0 && std::abs(clip.samples.back())<200,"one-shots have click-free attack and release");
    }
    const auto shot=tw::sound::synthesize(tw::sound::Cue::MortarShot), orb=tw::sound::synthesize(tw::sound::Cue::OrbShot);
    require(shot.samples!=orb.samples,"weapons have distinct sound designs");
}
void preview(const char* path) {
    const std::vector<tw::sound::Cue> cues{tw::sound::Cue::OrbShot,tw::sound::Cue::Ricochet,tw::sound::Cue::OrbBlast,
        tw::sound::Cue::MortarShot,tw::sound::Cue::MortarWhistle,tw::sound::Cue::MortarBlast,tw::sound::Cue::Thruster};
    std::vector<std::int16_t> samples;
    for (auto cue : cues) {
        const auto clip=tw::sound::synthesize(cue);
        samples.insert(samples.end(),clip.samples.begin(),clip.samples.end()); samples.resize(samples.size()+tw::sound::sampleRate/4);
    }
    std::ofstream file(path,std::ios::binary);
    const auto write=[&](std::uint32_t value,unsigned bytes) { for (unsigned i=0;i<bytes;++i) file.put(static_cast<char>(value>>(8*i))); };
    file.write("RIFF",4); write(36+static_cast<std::uint32_t>(samples.size()*2),4); file.write("WAVEfmt ",8); write(16,4); write(1,2); write(1,2);
    write(tw::sound::sampleRate,4); write(tw::sound::sampleRate*2,4); write(2,2); write(16,2);
    file.write("data",4); write(static_cast<std::uint32_t>(samples.size()*2),4);
    for (auto sample : samples) write(static_cast<std::uint16_t>(sample),2);
    if (!file) throw std::runtime_error("Cannot save sound preview");
}
}
int main(int argc,char** argv) {
    try { run(); if (argc==2) preview(argv[1]); std::cout<<"Audio checks passed: levels, loops, envelopes, weapon signatures\n"; return 0; }
    catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
