// Audio.h: plays the generated sounds. Each era has its own set of sound effects
// and its own music loop; on an era change the music crossfades.
// An era with no sound of its own (era 4 before its sounds exist) uses the era before.
#pragma once
#include "raylib.h"
#include <string>
#include <unordered_map>

constexpr int AUDIO_ERAS = 5;   // 1-bit, 8-bit, 16-bit, 2.5D, modern day

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
    float vol_[AUDIO_ERAS] = {};
    float duck_ = 1.0f;
    float musicVolume_ = 0.6f, sfxVolume_ = 0.8f;
    Music music_[AUDIO_ERAS]{};
    bool hasMusic_[AUDIO_ERAS] = {};
    std::unordered_map<std::string, Sound> sfx_[AUDIO_ERAS];
};
