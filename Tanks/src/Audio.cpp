#include "tankwars/Audio.hpp"
#include "tankwars/AudioSynth.hpp"
#include "tankwars/Weapon.hpp"
#include <SFML/Audio.hpp>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

namespace tw {
namespace {
using Cue=sound::Cue;
constexpr auto cueCount=static_cast<unsigned>(Cue::Count);
unsigned index(Cue cue) { return static_cast<unsigned>(cue); }
bool musicFile(const std::filesystem::path& path) {
    auto extension=path.extension().string();
    std::ranges::transform(extension,extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return extension==".mp3" || extension==".ogg" || extension==".flac" || extension==".wav";
}
float spatialGain(Vec2 from,Vec2 listener) { return 1.f/(1.f+length(from-listener)/1100.f); }
}
struct SoundSystem::Impl {
    std::array<sf::SoundBuffer,cueCount> buffers;
    struct Voice { std::unique_ptr<sf::Sound> sound; float gain{}; unsigned priority{}; bool ui{}; };
    std::vector<Voice> voices;
    struct Loop { std::unique_ptr<sf::Sound> sound; float gain{}, target{}; };
    std::array<Loop,5> loops;
    const std::array<Cue,5> loopCues{Cue::OrbHum,Cue::MortarWhistle,Cue::Thruster,Cue::Fire,Cue::Engine};
    sf::Music music;
    std::vector<std::filesystem::path> tracks;
    std::map<std::filesystem::path,std::string> trackTitles;
    std::size_t track{};
    std::uint32_t round{},lastEvent{};
    Phase phase{Phase::Lobby};
    float effects{75},musicLevel{32},duck{},focusGain{1},musicGain{},lastTrack{};
    bool muted{},musicOpened{},nextRequested{};
    Vec2 listener{};
    explicit Impl(const std::filesystem::path& assets) {
        for (unsigned i=0;i<cueCount;++i) {
            const auto clip=sound::synthesize(static_cast<Cue>(i));
            if (!buffers[i].loadFromSamples(clip.samples.data(),clip.samples.size(),1,sound::sampleRate,{sf::SoundChannel::Mono}))
                throw std::runtime_error("Cannot initialize game sound");
        }
        for (unsigned i=0;i<loops.size();++i) {
            loops[i].sound=std::make_unique<sf::Sound>(buffers[index(loopCues[i])]);
            loops[i].sound->setLooping(true); loops[i].sound->setVolume(0);
            loops[i].sound->setRelativeToListener(true); loops[i].sound->setAttenuation(0);
        }
        const auto directory=assets/"music";
        std::set<std::filesystem::path> seen;
        const auto add=[&](std::filesystem::path path,const std::string& title=std::string{}) {
            if (path.is_relative()) path=directory/path;
            path=std::filesystem::absolute(path).lexically_normal();
            if (musicFile(path) && std::filesystem::is_regular_file(path) && seen.insert(path).second) {
                if (!title.empty()) trackTitles[path]=title;
                tracks.push_back(std::move(path));
            }
        };
        std::ifstream playlist(directory/"playlist.m3u");
        std::string title;
        for (std::string line;std::getline(playlist,line);) {
            if (!line.empty() && line.back()=='\r') line.pop_back();
            if (line.starts_with("#EXTINF:")) {
                const auto comma=line.find(','); title=comma==std::string::npos ? "" : line.substr(comma+1);
            } else if (!line.empty() && line.front()!='#') { add(line,title); title.clear(); }
        }
        if (std::filesystem::is_directory(directory)) {
            std::vector<std::filesystem::path> extras;
            for (const auto& entry : std::filesystem::directory_iterator(directory)) if (entry.is_regular_file() && musicFile(entry.path())) extras.push_back(entry.path());
            std::ranges::sort(extras); for (const auto& path : extras) add(std::filesystem::absolute(path));
        }
        openTrack();
    }
    void openTrack() {
        musicOpened=false; musicGain=0; music.stop();
        for (std::size_t attempts=0;attempts<tracks.size();++attempts) {
            if (music.openFromFile(tracks[track])) { musicOpened=true; music.setVolume(0); music.play(); return; }
            track=(track+1)%tracks.size();
        }
    }
    void pan(sf::Sound& sound,Vec2 position) {
        const float side=std::clamp((position.x-listener.x)/900.f,-0.85f,0.85f);
        sound.setPosition({side,0,-std::sqrt(1-side*side)});
    }
    void play(Cue cue,Vec2 position,float gain,unsigned priority,std::uint32_t seed=0) {
        if (muted || effects<=0) return;
        std::erase_if(voices,[](const auto& voice){return voice.sound->getStatus()==sf::SoundSource::Status::Stopped;});
        if (voices.size()>=24) {
            const auto quiet=std::ranges::min_element(voices,{},&Voice::priority);
            if (quiet->priority>priority) return;
            voices.erase(quiet);
        }
        auto sound=std::make_unique<sf::Sound>(buffers[index(cue)]);
        sound->setRelativeToListener(true); sound->setAttenuation(0); pan(*sound,position);
        sound->setPitch(0.96f+static_cast<float>((seed*71)%89)/1000.f);
        sound->setVolume(0); sound->play();
        voices.push_back({std::move(sound),gain*spatialGain(position,listener),priority,cue==Cue::Ui});
    }
};
SoundSystem::SoundSystem(const std::filesystem::path& assets) : impl_(std::make_unique<Impl>(assets)) {}
SoundSystem::~SoundSystem()=default;
void SoundSystem::reset() {
    impl_->voices.clear();
    for (auto& loop : impl_->loops) { loop.sound->stop(); loop.gain=loop.target=0; }
    impl_->lastEvent=impl_->round=0; impl_->phase=Phase::Lobby; impl_->lastTrack=0;
}
void SoundSystem::update(float dt,const Snapshot& s,const Config& c,PlayerId local,bool audible,bool focused) {
    auto& a=*impl_;
    const auto* me=findTank(s,local);
    a.listener=me ? me->position : Vec2{c.worldWidth/2,c.groundHeight};
    if (s.round!=a.round) { reset(); a.round=s.round; }
    for (const auto& e : s.events) {
        if (e.id<=a.lastEvent) continue;
        a.lastEvent=e.id;
        if (!audible || s.phase==Phase::Lobby) continue;
        const auto* owner=findTank(s,e.player);
        Cue cue=Cue::Impact; float gain=0.65f; unsigned priority=2;
        switch (e.kind) {
        case EventKind::Shot:
            cue=owner && (owner->weapon==WeaponKind::Mortar || owner->weapon==WeaponKind::Flare) ? Cue::MortarShot :
                (owner && (owner->weapon==WeaponKind::GrowingOrb || owner->weapon==WeaponKind::Ricochet) ? Cue::OrbShot : (owner && owner->weapon==WeaponKind::Laser ? Cue::LaserShot : Cue::CannonShot));
            gain=0.58f; break;
        case EventKind::MortarImpact: cue=Cue::MortarBlast; gain=0.95f; priority=4; a.duck=1; break;
        case EventKind::SkyStrike: cue=Cue::MortarBlast; gain=1; priority=4; a.duck=1; break;
        case EventKind::OrbImpact: cue=Cue::OrbBlast; gain=0.5f+e.intensity*0.45f; priority=3; a.duck=std::max(a.duck,0.7f); break;
        case EventKind::Ricochet: cue=Cue::Ricochet; gain=0.48f; priority=1; break;
        case EventKind::Destroyed: cue=Cue::Destroyed; gain=1; priority=5; a.duck=1; break;
        default: break;
        }
        a.play(cue,e.position,gain,priority,e.id);
    }
    if (audible && s.phase==Phase::Victory && a.phase!=Phase::Victory) a.play(Cue::Victory,a.listener,0.5f,4);
    a.phase=s.phase;
    for (auto& loop : a.loops) loop.target=0;
    if (audible && s.phase==Phase::Playing) {
        for (const auto& p : s.projectiles) {
            const float near=spatialGain(p.position,a.listener);
            if (p.weapon==WeaponKind::GrowingOrb || p.weapon==WeaponKind::Ricochet) {
                auto& loop=a.loops[0]; const float charge=Cannon::orbCharge(p.age,c.weapon);
                const float volume=(0.055f+charge*0.09f)*near;
                if (volume>loop.target) { loop.target=volume; loop.sound->setPitch(0.8f+charge*0.48f); a.pan(*loop.sound,p.position); }
            } else if (p.weapon==WeaponKind::Mortar && p.velocity.y>0) {
                auto& loop=a.loops[1]; const float volume=0.13f*near*std::clamp(p.velocity.y/240.f,0.f,1.f);
                if (volume>loop.target) { loop.target=volume; loop.sound->setPitch(0.8f+std::clamp(p.velocity.y/600.f,0.f,0.8f)); a.pan(*loop.sound,p.position); }
            }
        }
        for (const auto& strike : s.skyStrikes) if (strike.age>=c.weapon.skyWarmup) {
            auto& loop=a.loops[0];
            const float volume=0.24f*spatialGain(strike.position,a.listener);
            if (volume>loop.target) { loop.target=volume; loop.sound->setPitch(0.55f); a.pan(*loop.sound,strike.position); }
        }
        for (const auto& tank : s.tanks) if (tank.alive()) {
            if (tank.canFly && tank.position.y<c.groundHeight-c.hillHeight-c.tankHalfHeight-3) {
                a.loops[2].target=0.13f*spatialGain(tank.position,a.listener);
                a.loops[2].sound->setPitch(0.85f+std::abs(tank.verticalVelocity)/700.f); a.pan(*a.loops[2].sound,tank.position);
            }
            if (tank.burnRemaining>0) a.loops[3].target=std::max(a.loops[3].target,0.1f*spatialGain(tank.position,a.listener));
        }
        for (const auto& fire : s.fires) a.loops[3].target=std::max(a.loops[3].target,0.08f*spatialGain(fire.position,a.listener));
        if (me && dt>0) a.loops[4].target=std::min(0.1f,std::abs(me->track-a.lastTrack)/dt*0.01f);
    }
    if (me) a.lastTrack=me->track;
    a.duck=std::max(0.f,a.duck-dt*1.5f);
    a.focusGain+=(focused && !a.muted ? 1.f-a.focusGain : -a.focusGain)*std::min(1.f,dt*9);
    if (a.musicOpened && a.music.getStatus()==sf::SoundSource::Status::Stopped && focused && !a.muted) a.nextRequested=true;
    const float musicTarget=a.nextRequested ? 0.f : 0.55f*a.musicLevel/100.f*a.focusGain*(1-0.55f*a.duck);
    a.musicGain+=(musicTarget-a.musicGain)*std::min(1.f,dt*7);
    if (a.nextRequested && a.musicGain<0.004f && !a.tracks.empty()) {
        a.nextRequested=false; a.track=(a.track+1)%a.tracks.size(); a.openTrack();
    }
    if (a.musicOpened) a.music.setVolume(100*a.musicGain);
    std::erase_if(a.voices,[](const auto& voice){return voice.sound->getStatus()==sf::SoundSource::Status::Stopped;});
    float requested=0;
    for (const auto& voice : a.voices) if (audible || voice.ui) requested+=voice.gain;
    for (auto& loop : a.loops) {
        loop.gain+=(loop.target-loop.gain)*std::min(1.f,dt*8);
        requested+=loop.gain;
        if (loop.gain>0.001f && loop.sound->getStatus()!=sf::SoundSource::Status::Playing) loop.sound->play();
        if (loop.gain<=0.001f && loop.target==0) loop.sound->stop();
    }
    const float effectsGain=a.effects/100.f*a.focusGain;
    const float limiter=std::min(1.f,std::max(0.f,0.9f-a.musicGain)/std::max(0.001f,requested*effectsGain));
    for (auto& voice : a.voices) voice.sound->setVolume((audible || voice.ui) ? 100*voice.gain*effectsGain*limiter : 0);
    for (auto& loop : a.loops) loop.sound->setVolume(100*loop.gain*effectsGain*limiter);
}
void SoundSystem::uiClick() { impl_->play(Cue::Ui,impl_->listener,0.15f,1); }
void SoundSystem::nextTrack() { if (!impl_->tracks.empty()) impl_->nextRequested=true; }
void SoundSystem::toggleMute() { impl_->muted=!impl_->muted; }
bool SoundSystem::muted() const { return impl_->muted; }
float SoundSystem::effectsVolume() const { return impl_->effects; }
float SoundSystem::musicVolume() const { return impl_->musicLevel; }
void SoundSystem::setEffectsVolume(float value) { impl_->effects=std::clamp(value,0.f,100.f); }
void SoundSystem::setMusicVolume(float value) { impl_->musicLevel=std::clamp(value,0.f,100.f); }
std::string SoundSystem::trackName() const {
    if (!impl_->musicOpened) return "NO MUSIC LOADED";
    const auto& path=impl_->tracks[impl_->track]; const auto title=impl_->trackTitles.find(path);
    return title==impl_->trackTitles.end() ? path.stem().string() : title->second;
}
}
