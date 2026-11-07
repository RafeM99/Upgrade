// SoundGen.cpp: synthesises every sound effect, per era (mono 22050 Hz WAV).
//   era 0: 1-bit square beeps
//   era 1: chiptune (pulse + triangle + LFSR noise)
//   era 2: 16-bit (FM tones, echo)
//   era 3: soft modern (sines, bells, echo + reverb)
// Oscillators are band-limited (PolyBLEP) and every note has a smooth attack/release, so no clicks.
// The music lives in MusicGen.cpp.
#include "Gen.h"
#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
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

enum Osc { SQUARE, PULSE25, PULSE12, TRIANGLE, SAW, SINE, PIANO, FM, NOISE };

float blep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
float pulse(float t, float dt, float duty)
{
    float v = t < duty ? 1.0f : -1.0f;
    v += blep(t, dt);
    float t2 = t - duty;
    if (t2 < 0) t2 += 1.0f;
    v -= blep(t2, dt);
    return v - (2 * duty - 1);
}

float osc(Osc w, float ph, float dt)   // ph in cycles, dt = cycles per sample
{
    float t = ph - std::floor(ph);
    switch (w) {
    case SQUARE: return pulse(t, dt, 0.5f) * 0.55f;
    case PULSE25: return pulse(t, dt, 0.25f) * 0.6f;
    case PULSE12: return pulse(t, dt, 0.125f) * 0.7f;
    case TRIANGLE: return (t < 0.5f ? 4 * t - 1 : 3 - 4 * t) * 0.8f;
    case SAW: return (2 * t - 1 - blep(t, dt)) * 0.5f;
    case SINE: return std::sin(t * PI2);
    case PIANO: return std::sin(t * PI2) * 0.7f + std::sin(t * PI2 * 2) * 0.2f + std::sin(t * PI2 * 3) * 0.08f;
    case FM: return std::sin(t * PI2 + 1.3f * std::sin(t * PI2 * 2));   // 2:1 FM, a bright 16-bit tone
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

// Chamberlin state-variable filter (band-pass / high-pass for noise).
struct SVF {
    float lp = 0, bp = 0;
    void tick(float x, float fc, float q, float& outLp, float& outBp, float& outHp)
    {
        float f = 2 * std::sin(3.14159f * std::min(fc, SR * 0.2f) / SR);
        float hp = x - lp - bp / q;
        bp += f * hp;
        lp += f * bp;
        outLp = lp; outBp = bp; outHp = hp;
    }
};

// One note: frequency sweep f0 -> f1 (exponential), smooth attack, exponential-ish release, optional vibrato.
void tone(Buf& b, float start, float dur, Osc w, float f0, float f1, float vol, float attack = 0.004f,
          float release = 0.05f, float vib = 0.0f)
{
    size_t i0 = (size_t)(start * SR), n = (size_t)(dur * SR);
    float ph = 0;
    attack = std::max(attack, 0.0015f);
    release = std::max(release, 0.004f);
    for (size_t i = 0; i < n; ++i) {
        float t = (float)i / SR, u = (float)i / n;
        float f = f0 * std::pow(f1 / f0, u);
        if (vib > 0) f *= 1.0f + vib * std::sin(t * PI2 * 6) * std::min(1.0f, t / 0.1f);
        float dt = f / SR;
        ph += dt;
        float a = std::min(1.0f, t / attack);
        float r = std::min(1.0f, (dur - t) / release);
        float env = a * a * (3 - 2 * a) * r * r;
        if (w == PIANO || w == FM) env *= std::exp(-t * 3.0f);
        b.add(i0 + i, osc(w, ph, dt) * vol * std::max(0.0f, env));
    }
}

// Filtered noise with a band-pass sweep fc0 -> fc1 and a hump envelope (whooshes).
void whoosh(Buf& b, float start, float dur, float fc0, float fc1, float vol, float q = 1.5f, float peakAt = 0.5f)
{
    size_t i0 = (size_t)(start * SR), n = (size_t)(dur * SR);
    SVF f;
    for (size_t i = 0; i < n; ++i) {
        float u = (float)i / n;
        float fc = fc0 * std::pow(fc1 / fc0, u);
        float env = u < peakAt ? u / peakAt : (1 - u) / (1 - peakAt);
        float l, bp, h;
        f.tick(noise(), fc, q, l, bp, h);
        b.add(i0 + i, bp * vol * env * env);
    }
}

// NES-style LFSR noise burst (clk = shift-register clock in Hz), 4-bit volume decay.
void nesNoise(Buf& b, float start, float dur, float clk0, float clk1, float vol, bool shortMode = false)
{
    size_t i0 = (size_t)(start * SR), n = (size_t)(dur * SR);
    uint16_t reg = 1;
    double acc = 0;
    for (size_t i = 0; i < n; ++i) {
        float u = (float)i / n;
        acc += clk0 * std::pow(clk1 / clk0, u) / SR;
        while (acc >= 1) {
            acc -= 1;
            int fb = (reg ^ (reg >> (shortMode ? 6 : 1))) & 1;
            reg = (uint16_t)((reg >> 1) | (fb << 14));
        }
        float v = std::floor((1 - u) * 15 + 0.5f) / 15.0f;
        b.add(i0 + i, ((reg & 1) ? 0.5f : -0.5f) * v * vol);
    }
}

// A struck bell: inharmonic partials (hum, prime, tierce, quint, nominal...) per era.
void bell(Buf& b, float start, float f, float vol, float decay, int era)
{
    if (era <= 1) {   // chip bell: two pulse partials + a triangle hum
        Osc w = era == 0 ? SQUARE : PULSE25;
        size_t i0 = (size_t)(start * SR), n = (size_t)(decay * 3 * SR);
        float p1 = 0, p2 = 0, p3 = 0;
        for (size_t i = 0; i < n; ++i) {
            float t = (float)i / SR;
            p1 += f / SR; p2 += f * 2.0f / SR; p3 += f * 0.5f / SR;
            float e1 = std::exp(-t / decay), e2 = std::exp(-t / (decay * 0.4f));
            float v = osc(w, p1, f / SR) * e1 + osc(w, p2, 2 * f / SR) * 0.5f * e2;
            if (era == 1) v += osc(TRIANGLE, p3, 0.5f * f / SR) * 0.6f * e1;
            if (era == 0) v = std::floor(v * 4 + 0.5f) / 4;   // crude 1-bit-ish levels
            v *= std::min(1.0f, t / 0.002f) * std::min(1.0f, (float)(n - i) / 200);
            b.add(i0 + i, v * vol);
        }
        return;
    }
    const float ratio[8] = {0.5f, 1.0f, 1.19f, 1.5f, 2.0f, 2.52f, 3.0f, 4.2f};
    const float amp[8] = {0.55f, 1.0f, 0.6f, 0.35f, 0.75f, 0.3f, 0.25f, 0.15f};
    const float dec[8] = {1.4f, 1.0f, 0.7f, 0.55f, 0.5f, 0.35f, 0.3f, 0.18f};
    const int parts = era == 2 ? 5 : 8;
    size_t i0 = (size_t)(start * SR), n = (size_t)(decay * 3.5f * SR);
    float ph[8] = {}, ph2[8] = {};
    for (size_t i = 0; i < n; ++i) {
        float t = (float)i / SR, v = 0;
        for (int k = 0; k < parts; ++k) {
            float fk = f * ratio[k];
            if (fk > SR * 0.45f) continue;
            ph[k] += fk / SR; ph2[k] += fk * 1.0025f / SR;   // a slight beat between twin partials
            float e = std::exp(-t / (decay * dec[k]));
            v += (std::sin(PI2 * ph[k]) + 0.4f * std::sin(PI2 * ph2[k])) * amp[k] * e;
        }
        if (t < 0.006f) v += noise() * 0.3f * (1 - t / 0.006f);   // the clapper strike
        v *= std::min(1.0f, t / 0.0015f) * std::min(1.0f, (float)(n - i) / 400);
        b.add(i0 + i, v * vol * 0.45f);
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
        float damp = 0;
        for (size_t i = 0; i < b.s.size(); ++i) {
            float y = line[p];
            damp += 0.5f * (y - damp);
            line[p] = b.s[i] + damp * 0.78f;
            out[i] += y * 0.25f;
            p = (p + 1) % d;
        }
    }
    for (size_t i = 0; i < b.s.size(); ++i) b.s[i] += out[i] * mix;
}

void echo(Buf& b, float delaySec, float fb)
{
    size_t d = (size_t)(delaySec * SR);
    float lp = 0;
    for (size_t i = d; i < b.s.size(); ++i) { lp += 0.5f * (b.s[i - d] - lp); b.s[i] += lp * fb; }
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
Osc leadWave(int era) { const Osc w[4] = {SQUARE, PULSE25, FM, SINE}; return w[era]; }

void sfx(Buf& b, const std::string& name, int era)
{
    Osc w = leadWave(era);
    float v = era == 3 ? 0.7f : 0.5f;
    float vib = era >= 2 ? 0.01f : 0.0f;
    Osc nz = NOISE;
    auto noiseBurst = [&](float start, float dur, float vol) {
        if (era == 1) nesNoise(b, start, dur, 30000, 12000, vol * 1.4f);
        else tone(b, start, dur, nz, 1, 1, vol, 0.001f, dur * 0.8f);
    };
    auto arp = [&](std::initializer_list<int> notes, float step, float base, float start = 0) {
        float t = start;
        for (int n : notes) { tone(b, t, step * 1.6f, w, midiHz(base + n), midiHz(base + n), v, 0.003f, step); t += step; }
    };
    if (name == "jump") tone(b, 0, 0.16f, w, 280, 640, v, 0.003f, 0.06f, vib);
    else if (name == "double_jump") tone(b, 0, 0.16f, w, 480, 980, v, 0.003f, 0.06f, vib);
    else if (name == "wall_jump") tone(b, 0, 0.12f, w, 380, 720, v, 0.003f, 0.05f, vib);
    else if (name == "stomp") { tone(b, 0, 0.12f, w == FM ? SQUARE : w, 260, 70, v); if (era >= 1) noiseBurst(0, 0.08f, v * 0.6f); }
    else if (name == "hit") { tone(b, 0, 0.1f, w == FM ? SAW : w, 200, 90, v); noiseBurst(0, 0.06f, v * 0.5f); }
    else if (name == "fade") tone(b, 0, 0.6f, w, 520, 70, v, 0.005f, 0.2f, 0.03f);
    else if (name == "revive") arp({0, 4, 7, 12, 16}, 0.07f, 72);
    else if (name == "fragment") { tone(b, 0, 0.05f, w, 1320, 1320, v * 0.7f); tone(b, 0.045f, 0.08f, w, 1760, 1760, v * 0.7f); }
    else if (name == "powerup") arp({0, 7, 12, 4, 11, 16, 7, 19}, 0.045f, 67);
    else if (name == "unlock") { arp({0, 4, 7, 12}, 0.1f, 60); tone(b, 0.4f, 0.6f, w, midiHz(84), midiHz(84), v, 0.01f, 0.3f, 0.02f); }
    else if (name == "dash") { if (era >= 2) whoosh(b, 0, 0.2f, 600, 4000, v * 1.4f); else noiseBurst(0, 0.18f, v * 0.7f); tone(b, 0, 0.12f, w, 600, 300, v * 0.4f); }
    else if (name == "pound") { tone(b, 0, 0.3f, era >= 2 ? SINE : w, 140, 35, v); noiseBurst(0, 0.2f, v * 0.8f); }
    else if (name == "crack") { noiseBurst(0, 0.25f, v); tone(b, 0, 0.1f, era >= 2 ? TRIANGLE : w, 90, 60, v * 0.6f); }
    else if (name == "attack") { if (era >= 2) whoosh(b, 0, 0.09f, 1500, 6000, v * 1.2f); else noiseBurst(0, 0.08f, v * 0.5f); tone(b, 0, 0.08f, w, 900, 500, v * 0.5f); }
    else if (name == "slow") tone(b, 0, 0.7f, w, 800, 200, v, 0.01f, 0.3f, 0.05f);
    else if (name == "boss_hit") { tone(b, 0, 0.25f, era >= 2 ? SAW : SQUARE, 160, 70, v); noiseBurst(0, 0.2f, v * 0.7f); }
    else if (name == "boss_appear") tone(b, 0, 1.2f, SAW, 50, 110, v, 0.2f, 0.4f, 0.04f);
    else if (name == "memory_lost") arp({12, 7, 3, 0, -5}, 0.14f, 64);
    else if (name == "select") tone(b, 0, 0.06f, w, 880, 880, v * 0.7f);
    else if (name == "join") arp({0, 7, 12}, 0.06f, 72);
    else if (name == "era_up") {
        tone(b, 0, 1.2f, SAW, 110, 880, v * 0.5f, 0.05f, 0.3f);
        for (int i = 0; i < 3; ++i) tone(b, 1.0f, 1.0f, w, midiHz(60 + i * 4 + (i == 2 ? -1 : 0)), midiHz(60 + i * 4 + (i == 2 ? -1 : 0)), v * 0.5f, 0.01f, 0.6f, vib);
    }
    // ---- cutscene sounds
    else if (name == "blip") {   // dialogue typewriter blip; the game varies the pitch
        const Osc bw[4] = {SQUARE, PULSE12, FM, SINE};
        float f = era == 3 ? 1050.0f : 900.0f;
        tone(b, 0, 0.032f, bw[era], f * 1.06f, f, v * 0.8f, 0.0015f, 0.012f);
        if (era == 3) tone(b, 0, 0.032f, TRIANGLE, f * 2, f * 2, v * 0.15f, 0.0015f, 0.01f);
    }
    else if (name == "advance") {   // page-advance confirm chirp
        tone(b, 0, 0.045f, w, midiHz(79), midiHz(79), v * 0.7f, 0.002f, 0.015f);
        tone(b, 0.045f, 0.08f, w, midiHz(86), midiHz(86), v * 0.7f, 0.002f, 0.04f);
    }
    else if (name == "encounter") {   // battle-encounter stinger: falling then rising arpeggio, whoosh, stab
        const float step = 0.034f;
        for (int i = 0; i < 16; ++i) {
            float n = 96.0f - i * 2.0f + (i % 2 ? 5 : 0);
            tone(b, i * step, step * 1.3f, w, midiHz(n), midiHz(n), v * 0.6f, 0.002f, step * 0.5f);
        }
        for (int i = 0; i < 16; ++i) {
            float n = 57.0f + i * 2.0f + (i % 2 ? 12 : 0);
            tone(b, 0.56f + i * step, step * 1.3f, w, midiHz(n), midiHz(n), v * 0.6f, 0.002f, step * 0.5f);
        }
        for (int i = 0; i < 12; ++i)   // tremolo stab on A minor
            for (int n : {69, 72, 76})
                tone(b, 1.12f + i * 0.03f, 0.03f, w, midiHz((float)n + (i % 2 ? 12 : 0)), midiHz((float)n + (i % 2 ? 12 : 0)), v * 0.3f * (1 - i / 14.0f), 0.002f, 0.012f);
        if (era <= 1) nesNoise(b, 0, 1.1f, 4000, 120000, v * (era == 0 ? 0.35f : 0.5f));
        else whoosh(b, 0, 1.2f, 300, 7000, v * 1.6f, 2.0f, 0.75f);
        tone(b, 1.1f, 0.4f, era >= 2 ? SINE : TRIANGLE, 110, 45, v * 0.8f, 0.002f, 0.3f);
    }
    else if (name == "slam") {   // heavy impact + a clock-tower dong
        tone(b, 0, 0.6f, era == 0 ? SQUARE : (era == 1 ? TRIANGLE : SINE), 130, 32, v * 1.2f, 0.001f, 0.45f);
        noiseBurst(0, 0.3f, v * 0.9f);
        bell(b, 0.01f, 110.0f, v * 1.1f, 1.2f, era);
    }
    else if (name == "swipe") {
        if (era == 0) for (int i = 0; i < 10; ++i) tone(b, i * 0.025f, 0.02f, SQUARE, 300.0f + i * 260, 300.0f + i * 260, v * 1.1f, 0.002f, 0.008f);
        else if (era == 1) nesNoise(b, 0, 0.28f, 8000, 200000, v * 1.6f);
        else whoosh(b, 0, 0.3f, 400, 7000, v * 1.8f, 1.2f, 0.6f);
    }
    else if (name == "tick") {   // one clock tick: a woodblock
        if (era == 0) tone(b, 0, 0.012f, SQUARE, 2200, 2000, v * 0.8f, 0.001f, 0.006f);
        else if (era == 1) { tone(b, 0, 0.03f, PULSE12, 1760, 1760, v * 0.7f, 0.001f, 0.02f); nesNoise(b, 0, 0.01f, 110000, 110000, v * 0.4f, true); }
        else {
            tone(b, 0, 0.09f, SINE, 2050, 1980, v, 0.0008f, 0.08f);
            tone(b, 0, 0.04f, SINE, 5600, 5500, v * 0.3f, 0.0008f, 0.035f);
            whoosh(b, 0, 0.006f, 3000, 3000, v * 2.0f, 3.0f, 0.1f);
        }
    }
    else if (name == "dong") bell(b, 0, 146.83f, v * 1.2f, 1.6f, era);   // clock-tower bell, D3

    if (name == "blip") return;   // played every few letters: keep it dry and short
    if (era >= 2) echo(b, 0.09f, name == "tick" || name == "advance" ? 0.1f : 0.25f);
    if (era == 3) reverb(b, name == "tick" || name == "advance" ? 0.15f : 0.35f);
}

const char* SFX_NAMES[] = {"jump", "double_jump", "wall_jump", "stomp", "hit", "fade", "revive", "fragment",
                           "powerup", "unlock", "dash", "pound", "crack", "attack", "slow", "boss_hit",
                           "boss_appear", "memory_lost", "select", "join", "era_up",
                           "blip", "advance", "encounter", "slam", "swipe", "tick", "dong"};

float sfxLength(const std::string& n, int era)
{
    if (n == "blip") return 0.1f;
    if (n == "era_up") return 2.4f;
    if (n == "boss_appear") return 1.4f;
    if (n == "encounter") return 2.2f;
    if (n == "slam") return 3.5f;
    if (n == "dong") return 4.5f;
    return era == 3 ? 1.6f : 1.0f;   // room for the reverb tail
}

}  // namespace

int generateSounds(const std::string& dir)
{
    int n = 0;
    for (int era = 0; era < 4; ++era) {
        std::string d = dir + "/audio/era" + std::to_string(era) + "/";
        for (const char* name : SFX_NAMES) {
            Buf b(sfxLength(name, era));
            sfx(b, name, era);
            // trim trailing silence, with a short fade so the cut never clicks
            size_t end = b.s.size();
            while (end > 1 && std::fabs(b.s[end - 1]) < 0.0005f) --end;
            end = std::max<size_t>(end, 64);
            b.s.resize(end);
            for (size_t i = 0; i < std::min<size_t>(64, end); ++i) b.s[end - 1 - i] *= (float)i / 64;
            n += save(b, d + "sfx_" + name + ".wav", 1.0f) ? 1 : 0;
        }
    }
    return n;
}
