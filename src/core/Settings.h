// Settings.h: the player's own display, graphics and audio choices (akshat).
// Saved to settings.ini next to the executable, never to assets/config.ini.
// Each option is a small index into a list of choices; the getters turn that into
// the number the renderer, camera, particles or window actually use.
// No pixel-scaling option: the renderer always scales the pixel stages by a whole number.
#pragma once
#include <string>

enum SettingId {
    SET_FULLSCREEN, SET_VSYNC, SET_FPS_CAP, SET_SHOW_FPS,                                  // display
    SET_RETRO, SET_BLOOM, SET_BEAT, SET_SHAKE, SET_PARTICLES, SET_BRIGHTNESS,   // graphics
    SET_VOLUME,                                                                            // audio
    SET_COUNT
};

struct SettingInfo {
    const char* key;       // settings.ini key, and the strings.txt key (set_<key>, set_<key>_desc)
    int section;           // 0 display, 1 graphics, 2 audio
    int count;             // number of choices
    int def;               // default choice
};

class Settings {
public:
    static const SettingInfo& info(int id);
    static int sectionOf(int id) { return info(id).section; }

    int get(int id) const { return v_[id]; }
    void set(int id, int value);           // clamped to the valid range
    void step(int id, int dir);            // left/right: walks the choices (toggles wrap)
    std::string valueText(int id) const;   // "On", "Full", "60", "110%"...
    void resetDefaults();

    bool load(const std::string& path);
    bool save(const std::string& path) const;
    bool setFromString(const std::string& key, const std::string& value);   // "--set key=value"
    static std::string defaultPath();      // settings.ini next to the executable

    // ---- applied values
    bool fullscreen() const { return v_[SET_FULLSCREEN] != 0; }
    bool vsync() const { return v_[SET_VSYNC] != 0; }
    int fpsCap() const;                    // 0 = unlimited
    bool showFps() const { return v_[SET_SHOW_FPS] != 0; }
    float retro() const;                   // 0, 0.5, 1: LCD grid / scanlines / vignette strength (off = main's clean look)
    bool bloom() const { return v_[SET_BLOOM] != 0; }
    float beatMul() const;                 // 0, 0.5, 1
    float shakeMul() const;                // 0, 0.5, 1
    float particleMul() const;             // 0.4, 1, 1.75
    float brightness() const;              // 0.8 .. 1.2
    float volume() const;                  // 0 .. 1

private:
    int v_[SET_COUNT] = {};
    bool init_ = false;
    void ensure();
    friend Settings& settings();
};

Settings& settings();   // the one shared settings object
