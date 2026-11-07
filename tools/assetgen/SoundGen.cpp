// SoundGen.cpp: synthesises every sound effect and music loop, per era.
//   era 0: square-wave beeps
//   era 1: chiptune (pulse + triangle + noise drums)
//   era 2: fuller SNES-ish (two pulses, arpeggios, vibrato, echo)
//   era 3: soft full soundtrack (pads, piano-ish lead, bass, drums, reverb)
// All four music loops play the same melody so the memory feels continuous.
#include "Gen.h"
#include "raylib.h"
#include "audio/MusicSpec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr int SR = 22050;
constexpr float PI2 = 6.2831853f;

uint32_t g_noise = 12345;
float noise()
{
    g_noise ^= g_noise << 13; g_noise ^= g_noise >> 17; g_noise ^= g_noise << 5;
    return (g_noise & 0xffff) / 32767.5f - 1.0f;
}

enum Osc { SQUARE, PULSE25, TRIANGLE, SAW, SINE, PIANO, NOISE };

float osc(Osc w, float ph)   // ph in cycles
{
    float t = ph - std::floor(ph);
    switch (w) {
    case SQUARE: return t < 0.5f ? 0.6f : -0.6f;
    case PULSE25: return t < 0.25f ? 0.6f : -0.6f;
    case TRIANGLE: return (t < 0.5f ? 4 * t - 1 : 3 - 4 * t) * 0.8f;
    case SAW: return (2 * t - 1) * 0.5f;
    case SINE: return std::sin(t * PI2);
    case PIANO: return std::sin(t * PI2) * 0.7f + std::sin(t * PI2 * 2) * 0.2f + std::sin(t * PI2 * 3) * 0.08f;
    case NOISE: return noise() * 0.6f;
    }
    return 0;
}

float midiHz(float m) { return 440.0f * std::pow(2.0f, (m - 69) / 12.0f); }

struct Buf {
    std::vector<float> s;
    explicit Buf(float seconds) : s((size_t)(seconds * SR), 0.0f) {}
    void add(size_t i, float v) { if (i < s.size()) s[i] += v; }
};

// One note: frequency sweep f0 -> f1, attack/decay envelope, optional vibrato.
void tone(Buf& b, float start, float dur, Osc w, float f0, float f1, float vol, float attack = 0.005f,
          float release = 0.05f, float vib = 0.0f)
{
    size_t i0 = (size_t)(start * SR), n = (size_t)(dur * SR);
    float ph = 0;
    for (size_t i = 0; i < n; ++i) {
        float t = (float)i / SR, u = (float)i / n;
        float f = f0 * std::pow(f1 / f0, u);
        if (vib > 0) f *= 1.0f + vib * std::sin(t * PI2 * 6);
        ph += f / SR;
        float env = std::min(1.0f, t / attack) * std::min(1.0f, (dur - t) / release);
        if (w == PIANO) env *= std::exp(-t * 3.0f);
        b.add(i0 + i, osc(w, ph) * vol * std::max(0.0f, env));
    }
}

void kick(Buf& b, float t, float vol) { tone(b, t, 0.16f, SINE, 130, 40, vol, 0.002f, 0.08f); }
void snare(Buf& b, float t, float vol)
{
    tone(b, t, 0.12f, NOISE, 1, 1, vol * 0.8f, 0.001f, 0.1f);
    tone(b, t, 0.08f, TRIANGLE, 200, 150, vol * 0.5f, 0.001f, 0.05f);
}
void hat(Buf& b, float t, float vol)
{
    size_t i0 = (size_t)(t * SR), n = (size_t)(0.04f * SR);
    float prev = 0;
    for (size_t i = 0; i < n; ++i) {
        float x = noise(), hp = x - prev;
        prev = x;
        b.add(i0 + i, hp * vol * 0.5f * (1 - (float)i / n));
    }
}

// Simple reverb: four feedback comb filters, mixed back in.
void reverb(Buf& b, float mix)
{
    const int delays[4] = {1116, 1188, 1277, 1356};
    std::vector<float> out(b.s.size(), 0.0f);
    for (int d : delays) {
        std::vector<float> line(d, 0.0f);
        size_t p = 0;
        for (size_t i = 0; i < b.s.size(); ++i) {
            float y = line[p];
            line[p] = b.s[i] + y * 0.78f;
            out[i] += y * 0.25f;
            p = (p + 1) % d;
        }
    }
    // wrap the tail around so the loop stays seamless
    for (size_t i = 0; i < b.s.size(); ++i) b.s[i] += out[i] * mix;
}

void echo(Buf& b, float delaySec, float fb)
{
    size_t d = (size_t)(delaySec * SR);
    for (size_t i = d; i < b.s.size(); ++i) b.s[i] += b.s[i - d] * fb;
}

// One-pole low-pass, for the soft 2.5D pads.
void lowpass(std::vector<float>& s, float cutoff)
{
    float a = 1.0f - std::exp(-PI2 * cutoff / SR), y = 0;
    for (float& x : s) { y += a * (x - y); x = y; }
}

bool save(Buf& b, const std::string& path, float gain = 1.0f)
{
    float peak = 0.0001f;
    for (float v : b.s) peak = std::max(peak, std::fabs(v));
    float k = std::min(gain, 0.9f / peak);
    std::vector<int16_t> pcm(b.s.size());
    for (size_t i = 0; i < b.s.size(); ++i) pcm[i] = (int16_t)std::lround(std::max(-1.0f, std::min(1.0f, b.s[i] * k)) * 32767);
    ::Wave w{};
    w.frameCount = (unsigned int)pcm.size();
    w.sampleRate = SR;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = pcm.data();
    return ExportWave(w, path.c_str());
}

// ------------------------------------------------------------------ sound effects
Osc leadWave(int era) { const Osc w[4] = {SQUARE, PULSE25, SQUARE, SINE}; return w[era]; }

void sfx(Buf& b, const std::string& name, int era)
{
    Osc w = leadWave(era);
    float v = era == 3 ? 0.7f : 0.5f;
    float vib = era >= 2 ? 0.01f : 0.0f;
    auto arp = [&](std::initializer_list<int> notes, float step, float base) {
        float t = 0;
        for (int n : notes) { tone(b, t, step * 1.6f, w, midiHz(base + n), midiHz(base + n), v, 0.003f, step); t += step; }
    };
    if (name == "jump") tone(b, 0, 0.16f, w, 280, 640, v, 0.003f, 0.06f, vib);
    else if (name == "double_jump") tone(b, 0, 0.16f, w, 480, 980, v, 0.003f, 0.06f, vib);
    else if (name == "wall_jump") tone(b, 0, 0.12f, w, 380, 720, v, 0.003f, 0.05f, vib);
    else if (name == "stomp") { tone(b, 0, 0.12f, w, 260, 70, v); if (era >= 1) tone(b, 0, 0.08f, NOISE, 1, 1, v * 0.6f); }
    else if (name == "hit") { tone(b, 0, 0.1f, w, 200, 90, v); tone(b, 0, 0.06f, NOISE, 1, 1, v * 0.5f); }
    else if (name == "fade") tone(b, 0, 0.6f, w, 520, 70, v, 0.005f, 0.2f, 0.03f);
    else if (name == "revive") arp({0, 4, 7, 12, 16}, 0.07f, 72);
    else if (name == "fragment") { tone(b, 0, 0.05f, w, 1320, 1320, v * 0.7f); tone(b, 0.045f, 0.08f, w, 1760, 1760, v * 0.7f); }
    else if (name == "powerup") arp({0, 7, 12, 4, 11, 16, 7, 19}, 0.045f, 67);
    else if (name == "unlock") { arp({0, 4, 7, 12}, 0.1f, 60); tone(b, 0.4f, 0.6f, w, midiHz(84), midiHz(84), v, 0.01f, 0.3f, 0.02f); }
    else if (name == "dash") { tone(b, 0, 0.18f, NOISE, 1, 1, v * 0.7f, 0.003f, 0.12f); tone(b, 0, 0.12f, w, 600, 300, v * 0.4f); }
    else if (name == "pound") { tone(b, 0, 0.3f, w, 140, 35, v); tone(b, 0, 0.2f, NOISE, 1, 1, v * 0.8f, 0.002f, 0.15f); }
    else if (name == "crack") { tone(b, 0, 0.25f, NOISE, 1, 1, v, 0.001f, 0.2f); tone(b, 0, 0.1f, w, 90, 60, v * 0.6f); }
    else if (name == "attack") { tone(b, 0, 0.08f, NOISE, 1, 1, v * 0.5f); tone(b, 0, 0.08f, w, 900, 500, v * 0.5f); }
    else if (name == "slow") tone(b, 0, 0.7f, w, 800, 200, v, 0.01f, 0.3f, 0.05f);
    else if (name == "boss_hit") { tone(b, 0, 0.25f, SQUARE, 160, 70, v); tone(b, 0, 0.2f, NOISE, 1, 1, v * 0.7f); }
    else if (name == "boss_appear") tone(b, 0, 1.2f, SAW, 50, 110, v, 0.2f, 0.4f, 0.04f);
    else if (name == "memory_lost") arp({12, 7, 3, 0, -5}, 0.14f, 64);
    else if (name == "select") tone(b, 0, 0.06f, w, 880, 880, v * 0.7f);
    else if (name == "join") arp({0, 7, 12}, 0.06f, 72);
    else if (name == "era_up") {
        tone(b, 0, 1.2f, SAW, 110, 880, v * 0.5f, 0.05f, 0.3f);
        for (int i = 0; i < 3; ++i) tone(b, 1.0f, 1.0f, w, midiHz(60 + i * 4 + (i == 2 ? -1 : 0)), midiHz(60 + i * 4 + (i == 2 ? -1 : 0)), v * 0.5f, 0.01f, 0.6f, vib);
    }
    if (era >= 2) echo(b, 0.09f, 0.25f);
    if (era == 3) reverb(b, 0.35f);
}

const char* SFX_NAMES[] = {"jump", "double_jump", "wall_jump", "stomp", "hit", "fade", "revive", "fragment",
                           "powerup", "unlock", "dash", "pound", "crack", "attack", "slow", "boss_hit",
                           "boss_appear", "memory_lost", "select", "join", "era_up"};

// ------------------------------------------------------------------ music
// Eight bars of 8 eighth-notes in A minor over Am F C G. 0 = rest, -1 = hold.
const int MELODY[64] = {
    76, -1, 69, -1, 72, -1, 71, 69,    // Am
    72, -1, -1, -1, 69, -1, 65, -1,    // F
    67, -1, 72, -1, 76, -1, 74, 72,    // C
    74, -1, -1, -1, 71, -1, 67, -1,    // G
    76, -1, 81, -1, 79, -1, 76, -1,    // Am
    77, -1, 76, -1, 72, -1, 69, -1,    // F
    67, -1, 72, -1, 76, -1, 79, -1,    // C
    74, -1, 71, -1, 67, -1, 0, 0};     // G
const int ROOT[8] = {45, 41, 48, 43, 45, 41, 48, 43};
const int CHORD[8][3] = {{57, 60, 64}, {53, 57, 60}, {55, 60, 64}, {55, 59, 62},
                         {57, 60, 64}, {53, 57, 60}, {55, 60, 64}, {55, 59, 62}};

// A short rising noise + saw sweep over [t0, t0 + len]: the build-up into the drop.
void riser(Buf& b, float t0, float len, float vol)
{
    size_t i0 = (size_t)(t0 * SR), n = (size_t)(len * SR);
    float ph = 0, lp = 0;
    for (size_t i = 0; i < n; ++i) {
        float k = (float)i / n;
        float f = 180.0f * std::pow(8.0f, k);
        ph += f / SR;
        lp += (0.05f + 0.6f * k) * (noise() - lp);
        b.add(i0 + i, (lp * 0.7f + osc(SAW, ph) * 0.3f) * vol * k * k);
    }
}

// Geometry-Dash-style loop (akshat): the shared melody over a build-up and a drop, with four-on-the-
// floor drums, off-beat bass, 16th arpeggios and sidechain pumping. Each era is the same song on
// the hardware of its time. `boss` makes a darker, always-on-the-drop variant with a riff instead.
void music(Buf& out, int era, float bpm, bool boss)
{
    const float e = 60.0f / bpm / 2;   // one eighth note
    const float beat = 2 * e;
    const int bars = boss ? musicspec::BOSS_BARS : musicspec::BARS;
    const float secs = (float)out.s.size() / SR;
    Buf syn(secs), drm(secs);
    Osc lead = leadWave(era);
    const int transpose = boss ? -2 : 0;   // boss: a tone lower, heavier

    for (int bar = 0; bar < bars; ++bar) {
        const int cb = bar % 8;
        const bool drop = boss || bar >= 8;
        const float t0 = bar * 8 * e;
        const int root = ROOT[cb] + transpose;

        // ---- melody (main loop only): plain in the build, an octave up in the drop
        if (!boss) {
            for (int k = 0; k < 8; ++k) {
                int i = cb * 8 + k, n = MELODY[i];
                if (n <= 0) continue;
                int len = 1;
                while (i + len < 64 && MELODY[i + len] == -1) ++len;
                if (era == 0 && !drop && k % 2 == 1) continue;
                float dur = len * e * (era == 0 ? 0.6f : 0.9f);
                float note = (float)n + (drop && era >= 1 ? 12.0f : 0.0f);
                Osc w = era == 3 ? (drop ? SAW : PIANO) : lead;
                float v = era == 3 && drop ? 0.16f : 0.26f;
                tone(syn, t0 + k * e, dur, w, midiHz(note), midiHz(note), v, 0.005f, 0.04f, era >= 2 ? 0.006f : 0.0f);
                if (era == 3 && drop) tone(syn, t0 + k * e, dur, PIANO, midiHz(note - 12), midiHz(note - 12), 0.18f);
            }
        } else {
            // boss riff: 16ths jumping between root octaves and the fifth
            const int riff[16] = {0, 12, 0, 7, 12, 0, 10, 0, 0, 12, 0, 7, 15, 12, 10, 7};
            for (int k = 0; k < 16; ++k) {
                float note = root + 24.0f + riff[k];
                tone(syn, t0 + k * e / 2, e / 2 * 0.8f, era == 3 ? SAW : (era == 0 ? SQUARE : PULSE25), midiHz(note),
                     midiHz(note), era == 0 ? 0.12f : 0.14f);
            }
        }

        // ---- bass
        if (era == 0) {
            for (int b4 = 0; b4 < 4; ++b4)
                if (drop || b4 % 2 == 0)
                    tone(syn, t0 + b4 * beat, e * 0.5f, SQUARE, midiHz(root + 12.0f), midiHz(root + 12.0f), 0.13f);
        } else {
            Osc bw = era == 1 ? TRIANGLE : (era == 2 ? PULSE25 : SAW);
            float bv = era == 2 ? 0.14f : 0.3f;
            for (int k = 0; k < 8; ++k) {
                bool on = drop ? (k % 2 == 1) : (k % 2 == 0);   // off-beat bass in the drop
                if (!on) continue;
                int note = root + (drop && k % 4 == 3 ? 12 : 0);
                tone(syn, t0 + k * e, e * 0.9f, bw, midiHz((float)note), midiHz((float)note), bv);
                if (era == 3) tone(syn, t0 + k * e, e * 0.9f, SINE, midiHz(note - 12.0f), midiHz(note - 12.0f), 0.35f);
            }
        }

        // ---- 16th arpeggio in the drop (8-bit and up), a soft pad under the 2.5D build
        if (drop && era >= 1) {
            for (int k = 0; k < 16; ++k) {
                int note = CHORD[cb][k % 3] + transpose + 12 + (k % 6 >= 3 ? 12 : 0);
                tone(syn, t0 + k * e / 2, e / 2 * 0.7f, era == 3 ? TRIANGLE : PULSE25, midiHz((float)note), midiHz((float)note),
                     era == 3 ? 0.07f : 0.06f);
            }
        }
        if (era == 3 && !drop) {
            Buf pad(8 * e + 0.5f);
            for (int k = 0; k < 3; ++k)
                for (float det : {-0.08f, 0.08f})
                    tone(pad, 0, 8 * e + 0.3f, SAW, midiHz(CHORD[cb][k] + det), midiHz(CHORD[cb][k] + det), 0.08f, 0.4f, 0.4f);
            lowpass(pad.s, 900);
            for (size_t i = 0; i < pad.s.size(); ++i) syn.add((size_t)(t0 * SR) + i, pad.s[i]);
        }

        // ---- drums
        float dv = era == 0 ? 0.25f : 0.35f;
        for (int b4 = 0; b4 < 4; ++b4) {
            float t = t0 + b4 * beat;
            if (era == 0) {   // 1-bit: just a clicky square thump
                if (drop || b4 % 2 == 0) tone(drm, t, 0.06f, SQUARE, 160, 60, dv);
                continue;
            }
            if (drop || b4 % 2 == 0) kick(drm, t, dv * 1.5f);
            if (b4 % 2 == 1) snare(drm, t, dv * (drop ? 1.0f : 0.8f));
            hat(drm, t + e, dv * (drop ? 1.0f : 0.6f));                      // off-beat hat
            if (drop && era >= 2) { hat(drm, t + e / 2, dv * 0.4f); hat(drm, t + e * 1.5f, dv * 0.4f); }
        }
        // the last bar of the build: snare roll and a riser into the drop
        if (!boss && bar == 7) {
            if (era >= 1)
                for (int k = 0; k < 16; ++k) snare(drm, t0 + k * e / 2, dv * (0.25f + 0.75f * k / 16.0f));
            riser(syn, t0, 8 * e, era == 0 ? 0.08f : 0.18f);
        }
    }

    // sidechain: the synths duck on every kick in the drop, the pump that makes it feel fast
    for (size_t i = 0; i < syn.s.size(); ++i) {
        float t = (float)i / SR;
        int bar = (int)(t / (8 * e));
        bool drop = boss || bar % bars >= 8;
        if (!drop || era == 0) continue;
        float since = std::fmod(t, beat);
        syn.s[i] *= 0.35f + 0.65f * std::min(1.0f, since / (beat * 0.45f));
    }
    if (era == 2) echo(syn, e * 1.5f, 0.18f);
    if (era == 3) reverb(syn, 0.3f);
    for (size_t i = 0; i < out.s.size(); ++i) out.s[i] += syn.s[i] + drm.s[i];
}

}  // namespace

int generateSounds(const std::string& dir)
{
    int n = 0;

    for (int era = 0; era < 4; ++era) {
        std::string d = dir + "/audio/era" + std::to_string(era) + "/";
        for (const char* name : SFX_NAMES) {
            Buf b(std::string(name) == "era_up" ? 2.4f : (std::string(name) == "boss_appear" ? 1.4f : 1.0f));
            sfx(b, name, era);
            // trim trailing silence
            size_t end = b.s.size();
            while (end > 1 && std::fabs(b.s[end - 1]) < 0.0005f) --end;
            b.s.resize(std::max<size_t>(end, 64));
            n += save(b, d + "sfx_" + name + ".wav", 1.0f) ? 1 : 0;
        }
        for (int boss = 0; boss < 2; ++boss) {
            float bpm = boss ? musicspec::BOSS_BPM[era] : musicspec::BPM[era];
            int bars = boss ? musicspec::BOSS_BARS : musicspec::BARS;
            float loop = bars * 4 * 60.0f / bpm;
            Buf extra(loop + 3.0f);
            music(extra, era, bpm, boss != 0);
            Buf m(loop);   // fold the tail back to the start so the loop is seamless
            for (size_t i = 0; i < extra.s.size(); ++i) m.s[i % m.s.size()] += extra.s[i];
            n += save(m, d + (boss ? "music_boss.wav" : "music.wav"), 1.0f) ? 1 : 0;
        }
    }
    return n;
}
