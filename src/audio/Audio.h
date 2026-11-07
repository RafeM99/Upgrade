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
    bool ready() const { return ready_; }

private:
    bool ready_ = false;
    int era_ = 0;
    float vol_[4] = {0, 0, 0, 0};
    float duck_ = 1.0f;
    float musicVolume_ = 0.6f, sfxVolume_ = 0.8f;
    Music music_[4]{};
    bool hasMusic_[4] = {};
    std::unordered_map<std::string, Sound> sfx_[4];
};
