// Audio.h: plays the generated sounds. Each era has its own set of sound effects
// and its own music loop; on an era change the music crossfades.
#pragma once
#include "raylib.h"
#include <string>
#include <unordered_map>

class Audio {
public:
    void init();
    void shutdown();
    void setEra(int era);              // crossfades the music
    void update(float dt);
    void play(const std::string& name, float volume = 1.0f, float pitch = 1.0f);
    void setMusicDuck(float k) { duck_ = k; }
    void setBoss(bool on) { boss_ = on; }   // crossfades to the era's boss track and back
    // 0..1, peaks at 1 on every beat of whatever music is loudest and decays before the next one.
    float beatPulse() const;
    int beatIndex() const;                 // beats since the loudest track started (for flashes)
    bool ready() const { return ready_; }

private:
    bool ready_ = false;
    int era_ = 0;
    float vol_[4] = {0, 0, 0, 0};
    float duck_ = 1.0f;
    float musicVolume_ = 0.6f, sfxVolume_ = 0.8f;
    Music music_[4]{};
    bool hasMusic_[4] = {};
    Music boss_music_[4]{};
    bool hasBoss_[4] = {};
    float bossVol_[4] = {0, 0, 0, 0};
    bool boss_ = false;
    float beatPos() const;
    std::unordered_map<std::string, Sound> sfx_[4];
};
