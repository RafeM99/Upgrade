#include "audio/Voice.h"
#include "core/Paths.h"
#include "raylib.h"

#include <cstdint>
#include <cstdio>
#include <unordered_map>

namespace vo {
namespace {
std::unordered_map<std::string, Sound> g_cache;   // key -> clip (a missing file caches an empty Sound)
std::string g_current;
float g_volume = 1.0f;

// Trim spaces (the generator does the same).
std::string clean(const std::string& s)
{
    size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
}  // namespace

std::string key(const std::string& speaker, const std::string& text)
{
    uint64_t h = 0xcbf29ce484222325ull;
    std::string s = speaker + "|" + clean(text);
    for (unsigned char c : s) { h ^= c; h *= 0x100000001b3ull; }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
    return buf;
}

void stop()
{
    auto it = g_cache.find(g_current);
    if (it != g_cache.end() && it->second.frameCount > 0 && IsSoundPlaying(it->second)) StopSound(it->second);
    g_current.clear();
}

void play(const std::string& speaker, const std::string& text)
{
    if (!IsAudioDeviceReady()) return;
    stop();
    std::string k = key(speaker, text);
    auto it = g_cache.find(k);
    if (it == g_cache.end()) {
        std::string path = assetPath("vo/" + k + ".mp3");
        Sound s{};
        if (FileExists(path.c_str())) s = LoadSound(path.c_str());
        it = g_cache.emplace(k, s).first;
    }
    if (it->second.frameCount == 0) return;   // no voiceover for this line
    SetSoundVolume(it->second, g_volume);
    PlaySound(it->second);
    g_current = k;
}

bool playing()
{
    auto it = g_cache.find(g_current);
    return it != g_cache.end() && it->second.frameCount > 0 && IsSoundPlaying(it->second);
}

// akshat (memory scenes): how long a line's clip runs, so the subtitles can wait for the voice.
float length(const std::string& speaker, const std::string& text)
{
    static std::unordered_map<std::string, float> lens;
    std::string k = key(speaker, text);
    auto c = g_cache.find(k);
    if (c != g_cache.end() && c->second.frameCount > 0 && c->second.stream.sampleRate > 0)
        return c->second.frameCount / (float)c->second.stream.sampleRate;
    auto it = lens.find(k);
    if (it != lens.end()) return it->second;
    float len = 0;
    std::string path = assetPath("vo/" + k + ".mp3");
    if (FileExists(path.c_str())) {
        Wave w = LoadWave(path.c_str());
        if (w.frameCount > 0 && w.sampleRate > 0) len = w.frameCount / (float)w.sampleRate;
        UnloadWave(w);
    }
    lens[k] = len;
    return len;
}

void setVolume(float v) { g_volume = v < 0 ? 0 : (v > 1 ? 1 : v); }

void shutdown()
{
    stop();
    for (auto& kv : g_cache)
        if (kv.second.frameCount > 0) UnloadSound(kv.second);
    g_cache.clear();
}
}  // namespace vo
