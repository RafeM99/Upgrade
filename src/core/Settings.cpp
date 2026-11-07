#include "core/Settings.h"
#include "core/Strings.h"
#include "raylib.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace {

// key, section, number of choices, default. The defaults are the game's original look.
const SettingInfo INFO[SET_COUNT] = {
    {"fullscreen", 0, 2, 0},
    {"vsync", 0, 2, 1},
    {"fps_cap", 0, 4, 1},        // 30 / 60 / 120 / unlimited
    {"show_fps", 0, 2, 0},
    {"retro", 1, 3, 2},          // off / light / full
    {"bloom", 1, 2, 1},
    {"beat_pulse", 1, 3, 2},     // off / low / full
    {"shake", 1, 3, 2},          // off / 50% / 100%
    {"particles", 1, 3, 1},      // low / normal / high
    {"pixel_scale", 1, 2, 0},    // sharp / integer only
    {"brightness", 1, 9, 4},     // 80% .. 120% in 5% steps
    {"volume", 2, 11, 10},       // 0% .. 100% in 10% steps
};

std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string pct(int p) { return std::to_string(p) + "%"; }

}  // namespace

Settings& settings()
{
    static Settings s;
    s.ensure();
    return s;
}

const SettingInfo& Settings::info(int id) { return INFO[std::max(0, std::min(SET_COUNT - 1, id))]; }

void Settings::ensure()
{
    if (init_) return;
    init_ = true;
    resetDefaults();
}

void Settings::resetDefaults()
{
    for (int i = 0; i < SET_COUNT; ++i) v_[i] = INFO[i].def;
}

void Settings::set(int id, int value)
{
    if (id < 0 || id >= SET_COUNT) return;
    v_[id] = std::max(0, std::min(INFO[id].count - 1, value));
}

void Settings::step(int id, int dir)
{
    if (id < 0 || id >= SET_COUNT) return;
    int n = INFO[id].count;
    if (n == 2) v_[id] = 1 - v_[id];   // on/off: either arrow flips it
    else set(id, v_[id] + dir);
}

std::string Settings::valueText(int id) const
{
    int v = v_[id];
    switch (id) {
    case SET_FULLSCREEN: case SET_VSYNC: case SET_SHOW_FPS: case SET_BLOOM: return T(v ? "opt_on" : "opt_off");
    case SET_FPS_CAP: return v == 3 ? std::string(T("opt_unlimited")) : std::to_string(fpsCap());
    case SET_RETRO: { const char* k[3] = {"opt_off", "opt_light", "opt_full"}; return T(k[v]); }
    case SET_BEAT: { const char* k[3] = {"opt_off", "opt_low", "opt_full"}; return T(k[v]); }
    case SET_SHAKE: return v == 0 ? std::string(T("opt_off")) : pct(v * 50);
    case SET_PARTICLES: { const char* k[3] = {"opt_low", "opt_normal", "opt_high"}; return T(k[v]); }
    case SET_PIXEL_SCALE: return T(v ? "opt_integer" : "opt_sharp");
    case SET_BRIGHTNESS: return pct(80 + v * 5);
    case SET_VOLUME: return pct(v * 10);
    }
    return "";
}

int Settings::fpsCap() const { const int caps[4] = {30, 60, 120, 0}; return caps[v_[SET_FPS_CAP]]; }
float Settings::retro() const { return v_[SET_RETRO] * 0.5f; }
float Settings::beatMul() const { return v_[SET_BEAT] * 0.5f; }
float Settings::shakeMul() const { return v_[SET_SHAKE] * 0.5f; }
float Settings::particleMul() const { const float m[3] = {0.4f, 1.0f, 1.75f}; return m[v_[SET_PARTICLES]]; }
float Settings::brightness() const { return 0.8f + v_[SET_BRIGHTNESS] * 0.05f; }
float Settings::volume() const { return v_[SET_VOLUME] * 0.1f; }

std::string Settings::defaultPath() { return std::string(GetApplicationDirectory()) + "settings.ini"; }

bool Settings::setFromString(const std::string& key, const std::string& value)
{
    for (int i = 0; i < SET_COUNT; ++i) {
        if (key != INFO[i].key) continue;
        std::string v = trim(value);
        if (v == "on" || v == "true" || v == "yes") set(i, 1);
        else if (v == "off" || v == "false" || v == "no") set(i, 0);
        else set(i, std::atoi(v.c_str()));
        return true;
    }
    return false;
}

bool Settings::load(const std::string& path)
{
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        size_t hash = line.find_first_of(";#");
        if (hash != std::string::npos) line = line.substr(0, hash);
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        setFromString(trim(line.substr(0, eq)), line.substr(eq + 1));
    }
    return true;
}

bool Settings::save(const std::string& path) const
{
    std::ofstream out(path);
    if (!out) return false;
    out << "# Upgrade: your settings. Written by the game's Settings menu.\n"
           "# Each value is the index of the choice (0 = first). Delete this file to reset.\n";
    const char* sections[3] = {"display", "graphics", "audio"};
    int last = -1;
    for (int i = 0; i < SET_COUNT; ++i) {
        if (INFO[i].section != last) { last = INFO[i].section; out << "\n# " << sections[last] << "\n"; }
        out << INFO[i].key << " = " << v_[i] << "\n";
    }
    return true;
}
