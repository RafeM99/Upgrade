#include "audio/Audio.h"
#include "core/Config.h"
#include "core/Paths.h"

#include <algorithm>

namespace {
const char* NAMES[] = {"jump", "double_jump", "wall_jump", "stomp", "hit", "fade", "revive", "fragment",
                       "powerup", "unlock", "dash", "pound", "crack", "attack", "slow", "boss_hit",
                       "boss_appear", "memory_lost", "select", "join", "era_up"};
}

void Audio::init()
{
    InitAudioDevice();
    ready_ = IsAudioDeviceReady();
    if (!ready_) return;
    musicVolume_ = config().f("audio.music_volume", 0.55f);
    sfxVolume_ = config().f("audio.sfx_volume", 0.8f);
    for (int e = 0; e < AUDIO_ERAS; ++e) {
        std::string d = "audio/era" + std::to_string(e) + "/";
        for (const char* n : NAMES) {
            std::string p = assetPath(d + "sfx_" + n + ".wav");
            if (FileExists(p.c_str())) sfx_[e][n] = LoadSound(p.c_str());
        }
        std::string mp = assetPath(d + "music.wav");
        // era 4 without its own music: play era 3's loop (a stream of its own, so crossfades still work)
        if (!FileExists(mp.c_str()) && e > 0) mp = assetPath("audio/era" + std::to_string(e - 1) + "/music.wav");
        if (FileExists(mp.c_str())) {
            music_[e] = LoadMusicStream(mp.c_str());
            music_[e].looping = true;
            hasMusic_[e] = true;
        }
    }
    era_ = 0;
    vol_[0] = 1;
    if (hasMusic_[0]) PlayMusicStream(music_[0]);
}

void Audio::shutdown()
{
    if (!ready_) return;
    for (int e = 0; e < AUDIO_ERAS; ++e) {
        for (auto& kv : sfx_[e]) UnloadSound(kv.second);
        if (hasMusic_[e]) UnloadMusicStream(music_[e]);
    }
    CloseAudioDevice();
    ready_ = false;
}

void Audio::setEra(int era)
{
    era = std::max(0, std::min(AUDIO_ERAS - 1, era));
    if (!ready_ || era == era_) return;
    era_ = era;
    if (hasMusic_[era] && !IsMusicStreamPlaying(music_[era])) PlayMusicStream(music_[era]);
}

void Audio::update(float dt)
{
    if (!ready_) return;
    for (int e = 0; e < AUDIO_ERAS; ++e) {
        if (!hasMusic_[e]) continue;
        float target = e == era_ ? 1.0f : 0.0f;
        vol_[e] += std::max(-dt / 1.5f, std::min(dt / 1.5f, target - vol_[e]));
        if (vol_[e] <= 0 && e != era_) {
            if (IsMusicStreamPlaying(music_[e])) StopMusicStream(music_[e]);
            continue;
        }
        SetMusicVolume(music_[e], vol_[e] * musicVolume_ * duck_);
        UpdateMusicStream(music_[e]);
    }
}

void Audio::play(const std::string& name, float volume, float pitch)
{
    if (!ready_) return;
    // a sound the era does not have falls back to the eras before it
    const Sound* snd = nullptr;
    for (int e = era_; e >= 0 && !snd; --e) {
        auto it = sfx_[e].find(name);
        if (it != sfx_[e].end()) snd = &it->second;
    }
    if (!snd) return;
    SetSoundVolume(*snd, volume * sfxVolume_);
    SetSoundPitch(*snd, pitch);
    PlaySound(*snd);
}
