#include "tankwars/AudioSynth.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace tw::sound {
namespace {
constexpr double tau=2*std::numbers::pi;
struct Noise {
    std::uint32_t state{0x91e10da5};
    double sample() { state^=state<<13; state^=state>>17; state^=state<<5; return static_cast<double>(state)/2147483648.-1.; }
};
double decay(double t,double seconds) { return std::exp(-t/seconds); }
double chirp(double t,double start,double end,double seconds) { return std::sin(tau*(end*t+(start-end)*seconds*(1-std::exp(-t/seconds)))); }
}
Clip synthesize(Cue cue) {
    const bool loop=cue==Cue::OrbHum || cue==Cue::Thruster || cue==Cue::Fire || cue==Cue::Engine || cue==Cue::MortarWhistle;
    const double duration=loop ? 2. : (cue==Cue::MortarBlast || cue==Cue::Destroyed ? 2.4 : (cue==Cue::OrbBlast ? 1.6 : (cue==Cue::Victory ? 1.9 : 0.8)));
    std::vector<double> samples(static_cast<std::size_t>(duration*sampleRate));
    Noise random; random.state+=static_cast<unsigned>(cue)*977;
    double low=0,mid=0;
    for (std::size_t i=0;i<samples.size();++i) {
        const double t=static_cast<double>(i)/sampleRate,n=random.sample();
        low+=0.025*(n-low); mid+=0.19*(n-mid);
        const double high=n-mid;
        double value=0;
        switch (cue) {
        case Cue::MortarShot:
        case Cue::CannonShot:
            value=0.85*chirp(t,150,44,0.07)*decay(t,0.19)+mid*2.7*decay(t,0.13)+high*0.45*decay(t,0.018);
            value+=0.22*std::sin(tau*1730*t)*decay(t,0.035); break;
        case Cue::MortarBlast:
        case Cue::Destroyed:
            value=low*6.2*decay(t,0.63)+mid*1.7*decay(t,0.25)+high*0.45*decay(t,0.05);
            value+=0.8*chirp(t,104,31,0.13)*decay(t,0.48)+0.24*std::sin(tau*58*t)*decay(t,0.9);
            value+=0.6*mid*std::max(0.,std::sin(tau*23*t))*decay(t,0.8); break;
        case Cue::OrbShot:
            value=0.65*chirp(t,90,620,0.12)*decay(t,0.19)+0.25*chirp(t,2100,280,0.08)*decay(t,0.15);
            value+=mid*0.65*decay(t,0.075)+0.25*std::sin(tau*62*t)*decay(t,0.2); break;
        case Cue::OrbBlast:
            value=0.7*chirp(t,280,35,0.15)*decay(t,0.43)+0.34*chirp(t,1650,105,0.14)*decay(t,0.26);
            value+=low*4.4*decay(t,0.55)+mid*1.4*decay(t,0.16)+0.15*std::sin(tau*69*t)*decay(t,0.7); break;
        case Cue::Ricochet:
            value=0.35*chirp(t,2800,650,0.035)*decay(t,0.13)+0.28*std::sin(tau*1437*t)*decay(t,0.19);
            value+=0.2*std::sin(tau*2213*t)*decay(t,0.11)+high*0.5*decay(t,0.018)+0.28*chirp(t,145,65,0.035)*decay(t,0.07); break;
        case Cue::OrbHum:
            value=0.22*std::sin(tau*74*t+1.5*std::sin(tau*2*t))+0.12*std::sin(tau*149*t);
            value+=0.08*std::sin(tau*301*t)*(0.6+0.4*std::sin(tau*8*t))+low*0.12; break;
        case Cue::Thruster:
            value=mid*1.9*(0.8+0.2*std::sin(tau*29*t))+low*2+0.1*std::sin(tau*85*t); break;
        case Cue::Fire:
            value=low*3+mid*0.45+high*(std::pow(std::max(0.,n),18)*2); break;
        case Cue::Engine:
            value=0.27*std::sin(tau*47*t)*(0.7+0.3*std::sin(tau*13*t))+0.16*std::sin(tau*94*t)+low*1.4; break;
        case Cue::MortarWhistle:
            value=(mid-low)*1.5*(0.78+0.12*std::sin(tau*1.7*t))+high*0.12+low*0.35;
            value+=0.045*std::sin(tau*510*t+0.55*std::sin(tau*0.8*t))*(0.65+0.35*mid); break;
        case Cue::LaserShot:
            value=0.45*chirp(t,2200,230,0.07)*decay(t,0.18)+mid*0.4*decay(t,0.08); break;
        case Cue::Impact:
            value=0.7*chirp(t,175,48,0.08)*decay(t,0.21)+mid*1.5*decay(t,0.13)+low*2*decay(t,0.3); break;
        case Cue::Ui:
            value=0.35*std::sin(tau*950*t)*decay(t,0.025)+0.15*std::sin(tau*1425*t)*decay(t,0.04); break;
        case Cue::Victory:
            for (int note=0;note<4;++note) {
                const double age=t-note*0.12;
                if (age>=0) {
                    const double frequency=std::array{196.,246.94,293.66,392.}[note];
                    value+=(0.2*std::sin(tau*frequency*age)+0.08*std::sin(tau*frequency*2*age))*decay(age,0.55)*std::min(1.,age/0.006);
                }
            } break;
        case Cue::Count: break;
        }
        const double attack=std::min(1.,t/(loop ? 0.03 : 0.003));
        const double release=std::min(1.,(duration-t)/(loop ? 0.03 : 0.06));
        // Mild saturation adds body before the final peak normalization.
        samples[i]=std::tanh(value*1.25)*attack*release;
    }
    if (!loop && cue!=Cue::Ui) {
        const auto dry=samples;
        for (const auto [seconds,gain] : std::array<std::pair<double,double>,3>{{{0.073,0.13},{0.131,0.09},{0.197,0.055}}}) {
            const auto delay=static_cast<std::size_t>(seconds*sampleRate);
            for (std::size_t i=delay;i<samples.size();++i) samples[i]+=dry[i-delay]*gain;
        }
    }
    if (!loop) for (std::size_t i=0;i<samples.size();++i) {
        samples[i]*=std::min(1.,static_cast<double>(samples.size()-1-i)/(sampleRate*0.06));
    }
    if (loop) {
        // Crossfade the tail into the head to make continuous loops click-free.
        const std::size_t seam=sampleRate/25;
        for (std::size_t i=0;i<seam;++i) {
            const double t=static_cast<double>(i)/seam;
            samples[i]=samples[samples.size()-seam+i]*(1-t)+samples[i]*t;
        }
        samples.resize(samples.size()-seam);
        const auto endpoint=samples.front();
        for (std::size_t i=0;i<256;++i) {
            const double blend=static_cast<double>(i)/255;
            auto& value=samples[samples.size()-256+i];
            value=value*(1-blend)+endpoint*blend;
        }
    }
    double peak=0; for (auto v : samples) peak=std::max(peak,std::abs(v));
    Clip clip; clip.loop=loop; clip.samples.reserve(samples.size());
    for (auto v : samples) clip.samples.push_back(static_cast<std::int16_t>(std::lround(v/std::max(0.01,peak)*0.82*32767)));
    return clip;
}
}
