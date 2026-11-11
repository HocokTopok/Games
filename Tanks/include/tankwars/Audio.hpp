#pragma once
#include "Config.hpp"
#include "Types.hpp"
#include <filesystem>
#include <memory>

namespace tw {
class SoundSystem {
public:
    explicit SoundSystem(const std::filesystem::path& assets);
    ~SoundSystem();
    void update(float dt,const Snapshot& state,const Config& config,PlayerId local,bool audible,bool focused);
    void reset();
    void uiClick();
    void nextTrack();
    void toggleMute();
    bool muted() const;
    float effectsVolume() const;
    float musicVolume() const;
    void setEffectsVolume(float value);
    void setMusicVolume(float value);
    std::string trackName() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
