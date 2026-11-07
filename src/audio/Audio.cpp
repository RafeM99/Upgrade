#include "audio/Audio.h"
#include "core/Config.h"
#include "core/Paths.h"
#include "audio/MusicSpec.h"

#include <algorithm>
#include <cmath>

namespace {
const char* NAMES[] = {"jump", "double_jump", "wall_jump", "stomp", "hit", "fade", "revive", "fragment",
                       "powerup", "unlock", "dash", "pound", "crack", "attack", "slow", "boss_hit",
                       "boss_appear", "memory_lost", "select", "join", "era_up",
                       "blip", "advance", "encounter", "slam", "swipe", "tick", "dong"};

// Music ships as QOA (44.1 kHz stereo, small); fall back to WAV if a build only has that.
std::string musicFile(const std::string& dirRel, const char* base)
{
    for (const char* ext : {".qoa", ".wav"}) {
        std::string p = assetPath(dirRel + base + ext);
        if (FileExists(p.c_str())) return p;
    }
    return "";
}
}

void Audio::init()
{
    InitAudioDevice();
    ready_ = IsAudioDeviceReady();
    if (!ready_) return;
    musicVolume_ = config().f("audio.music_volume", 0.55f);
    sfxVolume_ = config().f("audio.sfx_volume", 0.8f);
    for (int e = 0; e < 4; ++e) {
        std::string d = "audio/era" + std::to_string(e) + "/";
        for (const char* n : NAMES) {
            std::string p = assetPath(d + "sfx_" + n + ".wav");
            if (FileExists(p.c_str())) sfx_[e][n] = LoadSound(p.c_str());
        }
        std::string mp = musicFile(d, "music");
        if (!mp.empty()) {
            music_[e] = LoadMusicStream(mp.c_str());
            music_[e].looping = true;
            hasMusic_[e] = IsMusicValid(music_[e]);
        }
        std::string bp = musicFile(d, "music_boss");
        if (!bp.empty()) {
            boss_music_[e] = LoadMusicStream(bp.c_str());
            boss_music_[e].looping = true;
            hasBoss_[e] = IsMusicValid(boss_music_[e]);
        }
    }
    era_ = 0;
    vol_[0] = 1;
    if (hasMusic_[0]) PlayMusicStream(music_[0]);
}

void Audio::shutdown()
{
    if (!ready_) return;
    for (int e = 0; e < 4; ++e) {
        for (auto& kv : sfx_[e]) UnloadSound(kv.second);
        if (hasMusic_[e]) UnloadMusicStream(music_[e]);
        if (hasBoss_[e]) UnloadMusicStream(boss_music_[e]);
    }
    CloseAudioDevice();
    ready_ = false;
}

void Audio::setEra(int era)
{
    if (!ready_ || era == era_) return;
    era_ = era;
    if (hasMusic_[era] && !IsMusicStreamPlaying(music_[era])) PlayMusicStream(music_[era]);
}

void Audio::update(float dt)
{
    if (!ready_) return;
    for (int e = 0; e < 4; ++e) {   // boss tracks: start from the top each time, fade fast
        if (!hasBoss_[e]) continue;
        float target = (boss_ && e == era_) ? 1.0f : 0.0f;
        if (target > 0 && !IsMusicStreamPlaying(boss_music_[e])) { SeekMusicStream(boss_music_[e], 0); PlayMusicStream(boss_music_[e]); }
        bossVol_[e] += std::max(-dt / 0.8f, std::min(dt / 0.4f, target - bossVol_[e]));
        if (bossVol_[e] <= 0 && target <= 0) {
            if (IsMusicStreamPlaying(boss_music_[e])) StopMusicStream(boss_music_[e]);
            continue;
        }
        SetMusicVolume(boss_music_[e], bossVol_[e] * musicVolume_ * duck_);
        UpdateMusicStream(boss_music_[e]);
    }
    for (int e = 0; e < 4; ++e) {
        if (!hasMusic_[e]) continue;
        // eras without a boss track keep their own loop playing through a boss fight
        float target = (e == era_ && !(boss_ && hasBoss_[e])) ? 1.0f : 0.0f;
        if (e == era_ && !IsMusicStreamPlaying(music_[e])) PlayMusicStream(music_[e]);
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
    auto it = sfx_[era_].find(name);
    if (it == sfx_[era_].end()) return;
    SetSoundVolume(it->second, volume * sfxVolume_);
    SetSoundPitch(it->second, pitch);
    PlaySound(it->second);
}

// ---- beat clock (akshat)
float Audio::beatPos() const
{
    if (!ready_) return 0;
    bool b = bossVol_[era_] > vol_[era_] && hasBoss_[era_];
    if (b) return GetMusicTimePlayed(boss_music_[era_]) * musicspec::BOSS_BPM[era_] / 60.0f;
    if (!hasMusic_[era_]) return 0;
    return GetMusicTimePlayed(music_[era_]) * musicspec::BPM[era_] / 60.0f;
}

float Audio::beatPulse() const
{
    if (!ready_) return 0;
    float p = beatPos();
    float f = p - std::floor(p);
    return std::exp(-f * 7.0f);
}

int Audio::beatIndex() const { return (int)beatPos(); }
