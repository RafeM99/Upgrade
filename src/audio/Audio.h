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
    // raf's own tracks (assets/audio/boss_loop.wav, melody_title.wav): while a boss fights the boss loop
    // plays, on the title screen and the ending the melody does; the stage music fades out for them.
    void setSpecial(bool boss, bool title) { wantBoss_ = boss; wantTitle_ = title; }
    bool ready() const { return ready_; }

private:
    bool ready_ = false;
    int era_ = 0;
    float vol_[4] = {0, 0, 0, 0};
    float duck_ = 1.0f;
    float musicVolume_ = 0.6f, sfxVolume_ = 0.8f;
    Music music_[4]{};
    Music boss_{}, title_{};
    bool hasBoss_ = false, hasTitle_ = false, wantBoss_ = false, wantTitle_ = false;
    float bossVol_ = 0, titleVol_ = 0;
    bool hasMusic_[4] = {};
    std::unordered_map<std::string, Sound> sfx_[4];
};
