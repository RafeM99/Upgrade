// MusicGen.cpp (akshat): the procedural music engine. One song ("the memory", A minor) re-arranged
// for the hardware of each sound set, plus three boss themes (stages map to sets via art::AUDIO_SET):
//   era 0  1-bit   ZX-Spectrum-style beeper: one square channel, fake chords by fast arpeggio, mono
//   era 1  8-bit   NES-style: 2 pulse channels (duty 12.5/25/50%), 4-bit triangle bass, LFSR noise drums
//   era 2  16-bit  SNES/Genesis-style: FM e-piano + FM brass, slap bass, strings, sampled-style kit, echo
//   era 3  2.5D    modern EDM/synthwave: supersaws, pluck lead, sub + reese, sidechain, big reverb
//   boss set 1     The School Bell (stage 2): NES, ringing-bell trills, "ding-ding" bell calls, ticking
//   boss set 2     The Last Train (stage 4): FM, shuffled chug-a-chug kit, two-tone FM-brass horn, steam
//                  whistle glides, galloping slap bass, the melody quoted on brass + strings, ticking
//   boss set 3     The Hourglass (stage 6, final): choir, orchestral hits, the melody re-harmonised over Am-Bb-C-E7, sand
// Synthesis: PolyBLEP oscillators, ADSR, TPT state-variable filters, unison with stereo spread, delayed
// vibrato, portamento, 2-op FM, sidechain keyed to the kick, ping-pong delay, Freeverb-style reverb and
// a look-ahead limiter. Everything renders at 44.1 kHz stereo; each loop is a whole number of bars from
// time 0 (see audio/MusicSpec.h); the effect tail is folded back onto the start so it loops seamlessly.
#include "Gen.h"
#include "raylib.h"
#include "audio/MusicSpec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

constexpr int SR = 44100;
constexpr float KPI = 3.14159265f;
constexpr float TAU = 6.2831853f;
constexpr double TAIL = 7.0;   // seconds rendered past the loop end, then folded onto the start

// ------------------------------------------------------------------ basics
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed * 2654435761u + 1013904223u) { if (!s) s = 1; }
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xffffff) / 8388607.5f - 1.0f; }
    float uni() { return (next() + 1.0f) * 0.5f; }
};

float mtof(float m) { return 440.0f * std::exp2((m - 69.0f) / 12.0f); }
float clampf(float v, float a, float b) { return std::max(a, std::min(b, v)); }

float blep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
float sawB(float ph, float dt) { return 2.0f * ph - 1.0f - blep(ph, dt); }
float pulseB(float ph, float dt, float duty)
{
    float v = ph < duty ? 1.0f : -1.0f;
    v += blep(ph, dt);
    float p2 = ph - duty;
    if (p2 < 0) p2 += 1.0f;
    v -= blep(p2, dt);
    return v - (2.0f * duty - 1.0f);   // remove the DC of narrow pulses
}

// Topology-preserving-transform state-variable filter (Cytomic / Zavalishin).
struct SVF {
    float ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0, k = 1.4f;
    void set(float fc, float q)
    {
        fc = clampf(fc, 20.0f, SR * 0.45f);
        float g = std::tan(PI * fc / SR);
        k = 1.0f / std::max(0.3f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void copyCoefs(const SVF& o) { a1 = o.a1; a2 = o.a2; a3 = o.a3; k = o.k; }
    void tick(float v0, float& lp, float& bp, float& hp)
    {
        float v3 = v0 - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
    float lp(float x) { float l, b, h; tick(x, l, b, h); return l; }
    float bp(float x) { float l, b, h; tick(x, l, b, h); return b; }
    float hp(float x) { float l, b, h; tick(x, l, b, h); return h; }
};

struct Stereo {
    std::vector<float> L, R;
    explicit Stereo(size_t n = 0) : L(n, 0.0f), R(n, 0.0f) {}
    size_t size() const { return L.size(); }
    void clear() { std::fill(L.begin(), L.end(), 0.0f); std::fill(R.begin(), R.end(), 0.0f); }
    void add(size_t i, float l, float r) { if (i < L.size()) { L[i] += l; R[i] += r; } }
};

void panGains(float pan, float& gl, float& gr)
{
    pan = clampf(pan, -1, 1);
    gl = std::cos((pan + 1) * PI / 4) * 1.41421f;
    gr = std::sin((pan + 1) * PI / 4) * 1.41421f;
}

// ------------------------------------------------------------------ voices
enum Wave { W_SAW, W_PULSE, W_TRI, W_NESTRI, W_SINE, W_FM, W_PIANO };

struct Patch {
    Wave wave = W_SAW;
    float duty = 0.5f;
    int unison = 1;
    float detune = 0;         // total unison spread in semitones
    float width = 0;          // unison stereo spread
    float sub = 0;            // sine one octave down
    float osc2 = 0, osc2semi = 0, osc2duty = 0.5f;   // optional second (pulse) oscillator
    float a = 0.003f, d = 0.3f, s = 0.7f, r = 0.08f;
    bool filt = false;
    float cutoff = 18000, q = 0.707f, fenv = 0, fdecay = 0.2f, keytrack = 0;
    float vib = 0, vibRate = 5.5f, vibDelay = 0.25f;   // vibrato depth in semitones
    float glide = 0;
    float fmRatio = 1, fmIndex = 0, fmDecay = 0.3f, fmSus = 0;
    bool fmEnv = false;       // FM index follows the amp envelope (brass)
    float fm2Ratio = 0, fm2Index = 0, fm2Decay = 0.03f;   // second modulator (e-piano tine)
    float pan = 0, gain = 0.2f;
    bool nesVol = false;      // 4-bit volume steps
};

struct Note {
    double t = 0, dur = 0;
    float pitch = 60, vel = 1, from = -1, pan = 0;
};

float envLevel(const Patch& p, float t)
{
    float a = std::max(0.001f, p.a);
    if (t < a) { float u = t / a; return u * u * (3 - 2 * u); }
    if (p.d <= 0) return p.s;
    return p.s + (1 - p.s) * std::exp(-(t - a) / p.d);
}

void renderNote(Stereo& out, const Patch& p, const Note& n, Rng& rng)
{
    const float rel = std::max(0.004f, p.r);
    const size_t i0 = (size_t)std::llround(n.t * SR);
    const size_t len = (size_t)((n.dur + rel) * SR) + 1;
    const int U = std::max(1, std::min(9, p.unison));
    float ph[9], mul[9], gl[9], gr[9];
    const float norm = 1.0f / std::sqrt((float)U);
    for (int u = 0; u < U; ++u) {
        float x = U > 1 ? (float)u / (U - 1) * 2 - 1 : 0;   // -1..1
        mul[u] = std::exp2(p.detune * 0.5f * x * (0.6f + 0.4f * std::fabs(x)) / 12.0f);
        ph[u] = (U > 1 || p.wave == W_SAW) ? rng.uni() : 0.0f;
        panGains(p.pan + n.pan + p.width * x, gl[u], gr[u]);
        gl[u] *= norm; gr[u] *= norm;
    }
    float pianoPh[8] = {}, phM = 0, phM2 = 0, phSub = 0, ph2 = 0;
    SVF fl, fr;
    const float gateLevel = envLevel(p, (float)n.dur);
    float freq = mtof(n.pitch);
    for (size_t i = 0; i < len; ++i) {
        size_t o = i0 + i;
        if (o >= out.size()) break;
        float t = (float)i / SR;
        float e = t < n.dur ? envLevel(p, t) : gateLevel * (1.0f - (t - (float)n.dur) / rel);
        if (e <= 0) continue;
        if (p.nesVol) e = std::floor(e * 15 + 0.5f) / 15.0f;
        if ((i & 15) == 0) {
            float pitch = n.pitch;
            if (n.from > 0 && p.glide > 0) pitch += (n.from - n.pitch) * std::exp(-t / (p.glide * 0.4f));
            if (p.vib > 0 && t > p.vibDelay) pitch += p.vib * std::min(1.0f, (t - p.vibDelay) / 0.35f) * std::sin(TAU * p.vibRate * (t - p.vibDelay));
            freq = mtof(pitch);
            if (p.filt) {
                float fc = p.cutoff * std::exp2(p.fenv * std::exp(-t / p.fdecay));
                if (p.keytrack != 0) fc *= std::pow(freq / 261.6f, p.keytrack);
                fl.set(fc, p.q);
                fr.copyCoefs(fl);
            }
        }
        const float dt0 = freq / SR;
        float sl = 0, sr = 0;
        if (p.wave == W_FM || p.wave == W_PIANO || p.wave == W_SINE) {
            float v;
            ph[0] += dt0; if (ph[0] >= 1) ph[0] -= 1;
            if (p.wave == W_SINE) v = std::sin(TAU * ph[0]);
            else if (p.wave == W_FM) {
                phM += dt0 * p.fmRatio; if (phM >= 1) phM -= 1;
                float idx = p.fmEnv ? p.fmIndex * e : p.fmIndex * (p.fmSus + (1 - p.fmSus) * std::exp(-t / p.fmDecay));
                float mod = idx * std::sin(TAU * phM);
                if (p.fm2Index > 0) {
                    phM2 += dt0 * p.fm2Ratio; if (phM2 >= 1) phM2 -= 1;
                    mod += p.fm2Index * std::exp(-t / p.fm2Decay) * std::sin(TAU * phM2);
                }
                v = std::sin(TAU * ph[0] + mod);
            } else {   // piano: slightly inharmonic partials, higher ones die faster, plus a hammer tick
                v = 0;
                for (int k = 0; k < 8; ++k) {
                    float kk = (float)(k + 1);
                    float fk = dt0 * kk * std::sqrt(1.0f + 0.0004f * kk * kk);
                    if (fk > 0.45f) break;
                    pianoPh[k] += fk; if (pianoPh[k] >= 1) pianoPh[k] -= 1;
                    v += std::sin(TAU * pianoPh[k]) * std::pow(kk, -1.1f) * std::exp(-t * (0.6f + 0.9f * kk));
                }
                if (t < 0.004f) v += rng.next() * 0.15f * (1 - t / 0.004f);
            }
            if (p.sub > 0) { phSub += dt0 * 0.5f; if (phSub >= 1) phSub -= 1; v += p.sub * std::sin(TAU * phSub); }
            sl = v * gl[0]; sr = v * gr[0];
        } else {
            for (int u = 0; u < U; ++u) {
                float dt = dt0 * mul[u];
                ph[u] += dt; if (ph[u] >= 1) ph[u] -= 1;
                float v;
                switch (p.wave) {
                case W_SAW: v = sawB(ph[u], dt); break;
                case W_PULSE: v = pulseB(ph[u], dt, p.duty); break;
                case W_TRI: v = 4.0f * std::fabs(ph[u] - 0.5f) - 1.0f; break;
                default: {   // NES triangle: 32 steps, 16 levels
                    int q = (int)(ph[u] * 32);
                    int lvl = q < 16 ? 15 - q : q - 16;
                    v = lvl / 7.5f - 1.0f;
                }
                }
                sl += v * gl[u]; sr += v * gr[u];
            }
            float extra = 0;
            if (p.sub > 0) { phSub += dt0 * 0.5f; if (phSub >= 1) phSub -= 1; extra += p.sub * std::sin(TAU * phSub); }
            if (p.osc2 > 0) {
                float d2 = dt0 * std::exp2(p.osc2semi / 12.0f);
                ph2 += d2; if (ph2 >= 1) ph2 -= 1;
                extra += p.osc2 * pulseB(ph2, d2, p.osc2duty);
            }
            if (extra != 0) { float a, b; panGains(p.pan + n.pan, a, b); sl += extra * a; sr += extra * b; }
        }
        if (p.filt) { sl = fl.lp(sl); sr = fr.lp(sr); }
        float g = e * p.gain * n.vel;
        out.L[o] += sl * g;
        out.R[o] += sr * g;
    }
}

void renderNotes(Stereo& out, const Patch& p, const std::vector<Note>& notes, uint32_t seed)
{
    Rng rng(seed);
    for (const Note& n : notes) renderNote(out, p, n, rng);
}

// Legato lines: a note that starts right where the previous one ends glides from it.
void linkGlides(std::vector<Note>& v, double maxGap, float maxInterval = 7)
{
    for (size_t i = 1; i < v.size(); ++i) {
        double gap = v[i].t - (v[i - 1].t + v[i - 1].dur);
        if (gap <= maxGap && std::fabs(v[i].pitch - v[i - 1].pitch) <= maxInterval) v[i].from = v[i - 1].pitch;
    }
}

// ------------------------------------------------------------------ drums & textures
void kick(Stereo& o, double t, float g, float f0 = 160, float f1 = 48, float pd = 0.032f, float ad = 0.36f,
          float click = 0.35f, float drive = 1.8f)
{
    Rng rng((uint32_t)(t * 1000) + 7);
    size_t i0 = (size_t)std::llround(t * SR), n = (size_t)(ad * 4.5f * SR);
    float ph = 0;
    const float norm = 1.0f / std::tanh(drive);
    SVF hp; hp.set(2500, 0.7f);
    for (size_t i = 0; i < n; ++i) {
        float tt = (float)i / SR;
        float f = f1 + (f0 - f1) * std::exp(-tt / pd);
        ph += f / SR; if (ph >= 1) ph -= 1;
        float env = std::min(1.0f, tt / 0.0008f) * std::exp(-tt / ad);
        float v = std::sin(TAU * ph) * env;
        v += hp.hp(rng.next()) * click * std::exp(-tt / 0.0025f);
        v = std::tanh(drive * v) * norm;
        float fade = i + 64 > n ? (float)(n - i) / 64 : 1.0f;
        o.add(i0 + i, v * g * fade, v * g * fade);
    }
}

void snare(Stereo& o, double t, float g, float tone = 190, float nd = 0.15f, float td = 0.07f, float hpHz = 900, float pan = 0)
{
    Rng a((uint32_t)(t * 997) + 3), b((uint32_t)(t * 991) + 11);
    size_t i0 = (size_t)std::llround(t * SR), n = (size_t)(nd * 5 * SR);
    SVF fa, fb; fa.set(hpHz, 0.7f); fb.copyCoefs(fa);
    float ph1 = 0, ph2 = 0, gl, gr;
    panGains(pan, gl, gr);
    for (size_t i = 0; i < n; ++i) {
        float tt = (float)i / SR;
        float att = std::min(1.0f, tt / 0.0007f);
        float tf = tone * (1 + 0.5f * std::exp(-tt / 0.01f));
        ph1 += tf / SR; ph2 += tf * 1.62f / SR;
        float body = (std::sin(TAU * ph1) + 0.5f * std::sin(TAU * ph2)) * std::exp(-tt / td) * 0.7f;
        float en = std::exp(-tt / nd) * att;
        float nl = fa.hp(a.next()) * en, nr = fb.hp(b.next()) * en;
        o.add(i0 + i, std::tanh((body + nl) * 1.3f) * g * gl, std::tanh((body + nr) * 1.3f) * g * gr);
    }
}

void clap(Stereo& o, double t, float g)
{
    Rng a((uint32_t)(t * 977) + 5), b((uint32_t)(t * 983) + 9);
    size_t i0 = (size_t)std::llround(t * SR), n = (size_t)(0.7f * SR);
    SVF fa, fb; fa.set(1150, 1.4f); fb.copyCoefs(fa);
    const float bursts[3] = {0.0f, 0.0105f, 0.021f};
    for (size_t i = 0; i < n; ++i) {
        float tt = (float)i / SR, env = 0;
        for (float bt : bursts)
            if (tt >= bt) env = std::max(env, std::exp(-(tt - bt) / 0.0045f));
        if (tt >= 0.03f) env = std::max(env, 0.75f * std::exp(-(tt - 0.03f) / 0.13f));
        env *= std::min(1.0f, tt / 0.0005f);
        o.add(i0 + i, fa.bp(a.next()) * env * g * 2.2f, fb.bp(b.next()) * env * g * 2.2f);
    }
}

void hat(Stereo& o, double t, float g, float decay = 0.035f, float pan = 0, float hpHz = 7500)
{
    Rng a((uint32_t)(t * 1009) + 13);
    size_t i0 = (size_t)std::llround(t * SR), n = (size_t)(decay * 6 * SR);
    SVF f1, f2; f1.set(hpHz, 0.8f); f2.set(hpHz * 1.4f, 1.2f);
    float gl, gr;
    panGains(pan, gl, gr);
    // a few metallic square partials (808 style) under the noise
    const float mf[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
    float mph[6] = {};
    for (size_t i = 0; i < n; ++i) {
        float tt = (float)i / SR;
        float metal = 0;
        for (int k = 0; k < 6; ++k) { mph[k] += mf[k] * 4.0f / SR; if (mph[k] >= 1) mph[k] -= 1; metal += mph[k] < 0.5f ? 1.0f : -1.0f; }
        float x = a.next() + metal * 0.12f;
        float h = f1.hp(x);
        float v = f2.bp(h) * 0.9f + h * 0.35f;
        float env = std::min(1.0f, tt / 0.0004f) * std::exp(-tt / decay);
        o.add(i0 + i, v * env * g * gl, v * env * g * gr);
    }
}

void crash(Stereo& o, double t, float g, float decay = 1.1f)
{
    Rng a((uint32_t)(t * 1013) + 17), b((uint32_t)(t * 1019) + 19);
    size_t i0 = (size_t)std::llround(t * SR), n = (size_t)(decay * 4 * SR);
    SVF fa, fb, ga, gb; fa.set(4200, 0.7f); fb.copyCoefs(fa); ga.set(9000, 0.6f); gb.copyCoefs(ga);
    for (size_t i = 0; i < n; ++i) {
        float tt = (float)i / SR;
        float env = std::min(1.0f, tt / 0.001f) * (0.6f * std::exp(-tt / decay) + 0.4f * std::exp(-tt / 0.08f));
        float l = fa.hp(a.next()) + 0.4f * ga.bp(a.next());
        float r = fb.hp(b.next()) + 0.4f * gb.bp(b.next());
        o.add(i0 + i, l * env * g, r * env * g);
    }
}

void tom(Stereo& o, double t, float g, float hz, float decay = 0.22f, float pan = 0)
{
    Rng a((uint32_t)(t * 1021) + 23);
    size_t i0 = (size_t)std::llround(t * SR), n = (size_t)(decay * 4.5f * SR);
    float ph = 0, gl, gr;
    panGains(pan, gl, gr);
    SVF f; f.set(hz * 3, 1.0f);
    for (size_t i = 0; i < n; ++i) {
        float tt = (float)i / SR;
        float f0 = hz * (1 + 0.7f * std::exp(-tt / 0.03f));
        ph += f0 / SR; if (ph >= 1) ph -= 1;
        float v = std::sin(TAU * ph) * std::exp(-tt / decay) + f.bp(a.next()) * 0.4f * std::exp(-tt / 0.02f);
        v = std::tanh(v * 1.4f) * std::min(1.0f, tt / 0.0008f);
        o.add(i0 + i, v * g * gl, v * g * gr);
    }
}

// Woodblock: the game's clock tick. Two damped modes plus a band-passed click.
void wood(Stereo& o, double t, float g, float hz, float pan = 0)
{
    Rng a((uint32_t)(t * 1031) + 29);
    size_t i0 = (size_t)std::llround(t * SR), n = (size_t)(0.12f * SR);
    float p1 = 0, p2 = 0, gl, gr;
    panGains(pan, gl, gr);
    SVF f; f.set(hz * 1.3f, 3.0f);
    for (size_t i = 0; i < n; ++i) {
        float tt = (float)i / SR;
        p1 += hz / SR; p2 += hz * 2.76f / SR;
        float v = std::sin(TAU * p1) * std::exp(-tt / 0.022f) + 0.4f * std::sin(TAU * p2) * std::exp(-tt / 0.009f)
                  + f.bp(a.next()) * 0.6f * std::exp(-tt / 0.003f);
        v *= std::min(1.0f, tt / 0.0004f);
        o.add(i0 + i, v * g * gl, v * g * gr);
    }
}

// Tick-tock on the off-beats of [bar0, bar0+bars): the TIME signature of the whole soundtrack.
void tickTock(Stereo& o, double barStart, double beat, int bars, float g, float hi = 2300, float lo = 1700)
{
    for (int k = 0; k < bars * 4; ++k)
        wood(o, barStart + k * beat + beat * 0.5, g * (k % 2 ? 0.8f : 1.0f), k % 2 ? lo : hi, k % 2 ? 0.3f : -0.3f);
}

// Filtered-noise riser with a rising saw underneath; ends at t0 + dur.
void riser(Stereo& o, double t0, double dur, float g, float f0 = 300, float f1 = 9000)
{
    Rng a((uint32_t)(t0 * 1039) + 31), b((uint32_t)(t0 * 1049) + 37);
    size_t i0 = (size_t)std::llround(t0 * SR), n = (size_t)(dur * SR);
    SVF fa, fb;
    float ph = 0;
    for (size_t i = 0; i < n; ++i) {
        float k = (float)i / n;
        if ((i & 15) == 0) { fa.set(f0 * std::pow(f1 / f0, k), 2.5f); fb.copyCoefs(fa); }
        float f = 110.0f * std::exp2(3.0f * k);
        ph += f / SR; if (ph >= 1) ph -= 1;
        float env = k * k * g * std::min(1.0f, (float)(n - i) / 400.0f);
        float saw = sawB(ph, f / SR) * 0.15f;
        o.add(i0 + i, (fa.bp(a.next()) + saw) * env, (fb.bp(b.next()) + saw) * env);
    }
}

// Impact: sub boom with a falling noise sweep (the "downlifter" at the start of a drop).
void impact(Stereo& o, double t, float g)
{
    Rng a((uint32_t)(t * 1051) + 41), b((uint32_t)(t * 1061) + 43);
    size_t i0 = (size_t)std::llround(t * SR), n = (size_t)(2.2f * SR);
    SVF fa, fb;
    float ph = 0;
    for (size_t i = 0; i < n; ++i) {
        float tt = (float)i / SR;
        if ((i & 15) == 0) { fa.set(8000 * std::exp(-tt * 2.2f) + 200, 1.2f); fb.copyCoefs(fa); }
        float f = 32 + 60 * std::exp(-tt / 0.08f);
        ph += f / SR; if (ph >= 1) ph -= 1;
        float boom = std::sin(TAU * ph) * std::exp(-tt / 0.7f) * std::min(1.0f, tt / 0.002f);
        float ne = std::exp(-tt / 0.5f) * 0.35f;
        float fade = i + 256 > n ? (float)(n - i) / 256 : 1.0f;
        o.add(i0 + i, (boom + fa.lp(a.next()) * ne) * g * fade, (boom + fb.lp(b.next()) * ne) * g * fade);
    }
}

// Reverse-cymbal swell that ends exactly at t0 + dur.
void swell(Stereo& o, double t0, double dur, float g)
{
    Rng a((uint32_t)(t0 * 1063) + 47), b((uint32_t)(t0 * 1069) + 53);
    size_t i0 = (size_t)std::llround(t0 * SR), n = (size_t)(dur * SR);
    SVF fa, fb; fa.set(3000, 0.7f); fb.copyCoefs(fa);
    for (size_t i = 0; i < n; ++i) {
        float k = (float)i / n;
        float env = std::pow(k, 3.0f) * std::min(1.0f, (float)(n - i) / 200.0f) * g;
        o.add(i0 + i, fa.hp(a.next()) * env, fb.hp(b.next()) * env);
    }
}

// Sand: trickling grains of band-passed noise (the Hourglass). density in grains/s, may rise.
void sand(Stereo& o, double t0, double dur, float g, float density, float rise = 0)
{
    Rng a((uint32_t)(t0 * 1087) + 59), b((uint32_t)(t0 * 1091) + 61);
    size_t i0 = (size_t)std::llround(t0 * SR), n = (size_t)(dur * SR);
    SVF fa, fb; fa.set(5200, 1.5f); fb.copyCoefs(fa);
    float el = 0, er = 0;
    for (size_t i = 0; i < n; ++i) {
        float k = (float)i / n;
        float dens = density * (1 + rise * k);
        if (a.uni() < dens / SR) el = 0.5f + 0.5f * a.uni();
        if (b.uni() < dens / SR) er = 0.5f + 0.5f * b.uni();
        el *= 0.9965f; er *= 0.9965f;
        float edge = std::min(1.0f, std::min((float)i, (float)(n - i)) / 2000.0f);
        float lvl = g * edge * (1 + rise * k * k);
        o.add(i0 + i, fa.bp(a.next()) * (el + 0.15f) * lvl, fb.bp(b.next()) * (er + 0.15f) * lvl);
    }
}

// NES noise channel: 15-bit LFSR clocked at clk Hz, 4-bit decaying volume.
void nesNoise(Stereo& o, double t, float g, float clk, bool shortMode, float decay, float pan = 0)
{
    size_t i0 = (size_t)std::llround(t * SR), n = (size_t)(decay * 4 * SR);
    uint16_t reg = 1;
    double acc = 0;
    float gl, gr;
    panGains(pan, gl, gr);
    for (size_t i = 0; i < n; ++i) {
        float tt = (float)i / SR;
        acc += clk / SR;
        while (acc >= 1) {
            acc -= 1;
            int fb = (reg ^ (reg >> (shortMode ? 6 : 1))) & 1;
            reg = (uint16_t)((reg >> 1) | (fb << 14));
        }
        float vol = std::floor(std::exp(-tt / decay) * 15 + 0.5f) / 15.0f;
        float v = ((reg & 1) ? 1.0f : -1.0f) * vol;
        o.add(i0 + i, v * g * gl, v * g * gr);
    }
}

// NES kick: triangle channel pitch drop + a noise thump.
void nesKick(Stereo& o, double t, float g)
{
    Patch p; p.wave = W_NESTRI; p.a = 0.001f; p.d = 0; p.s = 1; p.r = 0.004f; p.gain = g;
    size_t i0 = (size_t)std::llround(t * SR), n = (size_t)(0.09 * SR);
    float ph = 0;
    for (size_t i = 0; i < n; ++i) {
        float tt = (float)i / SR;
        float f = 45 + 260 * std::exp(-tt / 0.018f);
        ph += f / SR; if (ph >= 1) ph -= 1;
        int q = (int)(ph * 32); int lvl = q < 16 ? 15 - q : q - 16;
        float v = (lvl / 7.5f - 1.0f) * std::min(1.0f, (float)(n - i) / 200.0f);
        o.add(i0 + i, v * g, v * g);
    }
    nesNoise(o, t, g * 0.35f, 9000, false, 0.018f);
}

// ------------------------------------------------------------------ effects
struct Comb {
    std::vector<float> b; size_t i = 0; float store = 0;
    explicit Comb(size_t n) : b(n, 0.0f) {}
    float proc(float in, float fb, float damp)
    {
        float y = b[i];
        store = y * (1 - damp) + store * damp;
        b[i] = in + store * fb;
        if (++i == b.size()) i = 0;
        return y;
    }
};
struct Allpass {
    std::vector<float> b; size_t i = 0;
    explicit Allpass(size_t n) : b(n, 0.0f) {}
    float proc(float in)
    {
        float bo = b[i];
        b[i] = in + bo * 0.5f;
        if (++i == b.size()) i = 0;
        return bo - in;
    }
};

// Freeverb (Jezar) with pre-delay and a low-cut on the input.
void reverb(const Stereo& in, Stereo& out, float room, float damp, float width, float wet, float predelay = 0.02f)
{
    const int CT[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    const int AT[4] = {556, 441, 341, 225};
    std::vector<Comb> cl, cr;
    std::vector<Allpass> al, ar;
    for (int c : CT) { cl.emplace_back(c); cr.emplace_back(c + 23); }
    for (int a : AT) { al.emplace_back(a); ar.emplace_back(a + 23); }
    const float fb = room * 0.28f + 0.7f;
    const float w1 = wet * (width / 2 + 0.5f), w2 = wet * ((1 - width) / 2);
    const size_t pd = (size_t)(predelay * SR);
    float hp = 0, lpIn = 0;
    for (size_t i = 0; i < in.size(); ++i) {
        float x = i >= pd ? (in.L[i - pd] + in.R[i - pd]) * 0.015f : 0.0f;
        hp += 0.02f * (x - hp);            // ~140 Hz low-cut
        x -= hp;
        lpIn += 0.6f * (x - lpIn);         // gentle high-cut
        x = lpIn;
        float l = 0, r = 0;
        for (int k = 0; k < 8; ++k) { l += cl[k].proc(x, fb, damp); r += cr[k].proc(x, fb, damp); }
        for (int k = 0; k < 4; ++k) { l = al[k].proc(l); r = ar[k].proc(r); }
        out.L[i] += l * w1 + r * w2;
        out.R[i] += r * w1 + l * w2;
    }
}

// Ping-pong delay with a darkening feedback path (SNES-echo flavour).
void pingpong(const Stereo& in, Stereo& out, double delaySec, float fb, float lpHz)
{
    size_t D = (size_t)(delaySec * SR), n = in.size();
    std::vector<float> lL(n, 0.0f), lR(n, 0.0f);
    float a = 1.0f - std::exp(-TAU * lpHz / SR), sl = 0, sr = 0;
    for (size_t i = 0; i < n; ++i) {
        float yl = i >= D ? lL[i - D] : 0.0f;
        float yr = i >= D ? lR[i - D] : 0.0f;
        sl += a * (yl - sl);
        sr += a * (yr - sr);
        lL[i] = (in.L[i] + in.R[i]) * 0.5f + sr * fb;
        lR[i] = sl;
        out.L[i] += yl;
        out.R[i] += yr;
    }
}

// Time-varying low-pass over a whole track (intro filter sweeps, build-up opening).
void autoFilter(Stereo& s, const std::function<float(double)>& cutoffAt, float q = 0.9f)
{
    SVF fl, fr;
    float cur = cutoffAt(0);
    for (size_t i = 0; i < s.size(); ++i) {
        if ((i & 31) == 0) {
            float tgt = cutoffAt((double)i / SR);
            cur = std::exp(std::log(cur) + 0.25f * (std::log(tgt) - std::log(cur)));
            fl.set(cur, q);
            fr.copyCoefs(fl);
        }
        s.L[i] = fl.lp(s.L[i]);
        s.R[i] = fr.lp(s.R[i]);
    }
}

// Choir: vowel "ah" formants on top of a saw stack.
void formant(Stereo& s)
{
    const float F[3] = {780, 1180, 2700}, Q[3] = {6, 7, 9}, G[3] = {1.0f, 0.55f, 0.25f};
    SVF fl[3], fr[3];
    for (int k = 0; k < 3; ++k) { fl[k].set(F[k], Q[k]); fr[k].copyCoefs(fl[k]); }
    for (size_t i = 0; i < s.size(); ++i) {
        float l = 0, r = 0;
        for (int k = 0; k < 3; ++k) { l += fl[k].bp(s.L[i]) * G[k]; r += fr[k].bp(s.R[i]) * G[k]; }
        s.L[i] = l * 2.2f + s.L[i] * 0.08f;
        s.R[i] = r * 2.2f + s.R[i] * 0.08f;
    }
}

// ------------------------------------------------------------------ mixing
struct Mix {
    size_t n;
    Stereo dry, rev, dly, tmp;
    std::vector<float> duck;   // 0..1 sidechain amount
    std::vector<double> kicks;
    explicit Mix(size_t frames) : n(frames), dry(frames), rev(frames), dly(frames), tmp(frames), duck(frames, 0.0f) {}

    void buildDuck(double release)
    {
        std::fill(duck.begin(), duck.end(), 0.0f);
        const double pre = 0.004;
        for (double k : kicks) {
            long s0 = (long)((k - pre) * SR), s1 = (long)((k + release) * SR);
            for (long i = std::max(0L, s0); i < std::min((long)n, s1); ++i) {
                double x = (double)i / SR - k;
                float v = x < 0 ? (float)(0.5 - 0.5 * std::cos(PI * (x + pre) / pre))
                                : (float)(0.5 + 0.5 * std::cos(PI * x / release));
                duck[i] = std::max(duck[i], v);
            }
        }
    }
    // Route tmp into the buses, then clear it.
    void send(float gain, float revSend, float dlySend = 0, float duckDepth = 0)
    {
        for (size_t i = 0; i < n; ++i) {
            float g = gain * (1.0f - duckDepth * duck[i]);
            float l = tmp.L[i] * g, r = tmp.R[i] * g;
            dry.L[i] += l; dry.R[i] += r;
            if (revSend > 0) { rev.L[i] += l * revSend; rev.R[i] += r * revSend; }
            if (dlySend > 0) { dly.L[i] += l * dlySend; dly.R[i] += r * dlySend; }
        }
        tmp.clear();
    }
};

struct FxSettings {
    float room = 0.8f, damp = 0.4f, width = 1.0f, wet = 1.0f;
    double delay = 0.3; float delayFb = 0.35f, delayLp = 4000;
    float masterLp = 0;     // optional one-pole master high-cut (Hz), 0 = off
    float ceilingDb = -1.7f;   // QOA quantisation adds ~0.6 dB, so the decoded peak lands near -1 dBFS
    float drive = 1.6f;     // how hard the limiter is hit (linear)
    bool mono = false;
};

// Effects, fold the tail onto the start, master bus, limiter. Returns interleaved int16.
std::vector<int16_t> master(Mix& m, size_t loopFrames, const FxSettings& fx)
{
    Stereo wet(m.n);
    pingpong(m.dly, wet, fx.delay, fx.delayFb, fx.delayLp);
    for (size_t i = 0; i < m.n; ++i) { m.rev.L[i] += wet.L[i] * 0.3f; m.rev.R[i] += wet.R[i] * 0.3f; }
    reverb(m.rev, wet, fx.room, fx.damp, fx.width, fx.wet);
    Stereo out(loopFrames);
    for (size_t i = 0; i < m.n; ++i) {
        size_t j = i % loopFrames;
        out.L[j] += m.dry.L[i] + wet.L[i];
        out.R[j] += m.dry.R[i] + wet.R[i];
    }
    const size_t N = loopFrames;
    // circular one-pole filters: warm up over the last second so the loop point stays continuous
    auto circular = [&](auto&& fn) {
        size_t warm = std::min(N, (size_t)SR);
        for (size_t k = N - warm; k < N; ++k) fn(k, false);
        for (size_t k = 0; k < N; ++k) fn(k, true);
    };
    {   // DC block / 25 Hz high-pass
        float xl = 0, xr = 0, yl = 0, yr = 0, c = 1.0f - TAU * 25.0f / SR;
        circular([&](size_t k, bool write) {
            float l = out.L[k], r = out.R[k];
            float ol = l - xl + c * yl, orr = r - xr + c * yr;
            xl = l; xr = r; yl = ol; yr = orr;
            if (write) { out.L[k] = ol; out.R[k] = orr; }
        });
    }
    if (fx.masterLp > 0) {
        float a = 1.0f - std::exp(-TAU * fx.masterLp / SR), sl = 0, sr = 0, s2l = 0, s2r = 0;
        circular([&](size_t k, bool write) {
            sl += a * (out.L[k] - sl); sr += a * (out.R[k] - sr);
            s2l += a * (sl - s2l); s2r += a * (sr - s2r);
            if (write) { out.L[k] = s2l; out.R[k] = s2r; }
        });
    }
    if (fx.mono)
        for (size_t k = 0; k < N; ++k) out.L[k] = out.R[k] = 0.5f * (out.L[k] + out.R[k]);
    // normalise, then a look-ahead limiter to the ceiling
    float peak = 1e-6f;
    for (size_t k = 0; k < N; ++k) peak = std::max(peak, std::max(std::fabs(out.L[k]), std::fabs(out.R[k])));
    const float ceil = std::pow(10.0f, fx.ceilingDb / 20.0f);
    const float pre = ceil * fx.drive / peak;
    std::vector<float> req(N), g(N);
    for (size_t k = 0; k < N; ++k) {
        float a = std::max(std::fabs(out.L[k]), std::fabs(out.R[k])) * pre;
        req[k] = a > ceil ? ceil / a : 1.0f;
    }
    const size_t W = (size_t)(0.004 * SR);
    std::vector<float> mn(N);
    for (size_t k = 0; k < N; ++k) {   // min over the next W samples (circular)
        float v = 1.0f;
        for (size_t j = 0; j <= W; ++j) v = std::min(v, req[(k + j) % N]);
        mn[k] = v;
    }
    const float relc = 1.0f - std::exp(-1.0f / (0.12f * SR));
    float env = 1.0f;
    for (int pass = 0; pass < 2; ++pass)
        for (size_t k = 0; k < N; ++k) {
            env = std::min(mn[k], env + (1.0f - env) * relc);
            g[k] = env;
        }
    // box-average the gain over W so it never jumps (the min above already covers the window)
    double acc = 0;
    for (size_t j = 0; j < W; ++j) acc += g[(N - W + j) % N];
    std::vector<int16_t> pcm(N * 2);
    for (size_t k = 0; k < N; ++k) {
        acc += g[k] - g[(k + N - W) % N];
        float gk = (float)(acc / W) * pre;
        float l = clampf(out.L[k] * gk, -ceil, ceil), r = clampf(out.R[k] * gk, -ceil, ceil);
        pcm[2 * k] = (int16_t)std::lround(l * 32767.0f);
        pcm[2 * k + 1] = (int16_t)std::lround(r * 32767.0f);
    }
    return pcm;
}

bool exportMusic(const std::vector<int16_t>& stereoPcm, bool mono, const std::string& base)
{
    std::vector<int16_t> data;
    unsigned int frames = (unsigned int)(stereoPcm.size() / 2);
    if (mono) {
        data.resize(frames);
        for (unsigned int k = 0; k < frames; ++k) data[k] = stereoPcm[2 * k];
    } else data = stereoPcm;
    ::Wave w{};
    w.frameCount = frames;
    w.sampleRate = SR;
    w.sampleSize = 16;
    w.channels = mono ? 1 : 2;
    w.data = data.data();
    std::string q = base + ".qoa";
    if (ExportWave(w, q.c_str())) { std::remove((base + ".wav").c_str()); return true; }
    // fallback: 32 kHz WAV (crude decimation by linear interpolation)
    std::vector<int16_t> lo;
    unsigned int ch = w.channels, f2 = (unsigned int)((double)frames * 32000 / SR);
    lo.resize((size_t)f2 * ch);
    for (unsigned int k = 0; k < f2; ++k) {
        double src = (double)k * SR / 32000;
        unsigned int a = (unsigned int)src, b = std::min(frames - 1, a + 1);
        float fr = (float)(src - a);
        for (unsigned int c = 0; c < ch; ++c)
            lo[(size_t)k * ch + c] = (int16_t)std::lround(data[(size_t)a * ch + c] * (1 - fr) + data[(size_t)b * ch + c] * fr);
    }
    w.frameCount = f2; w.sampleRate = 32000; w.data = lo.data();
    return ExportWave(w, (base + ".wav").c_str());
}

// ------------------------------------------------------------------ the song
struct Chord { int root; std::vector<int> v; };
const Chord AM9{45, {57, 60, 64, 67, 71}}, FMAJ7{41, {57, 60, 64, 65, 67}}, CADD9{48, {55, 60, 62, 64, 67}},
    G6{43, {55, 59, 62, 64, 67}}, EM7{40, {55, 59, 62, 64, 67}}, E7{40, {56, 59, 62, 64, 68}},
    CE{40, {55, 60, 64, 67, 72}}, DM9{38, {57, 60, 62, 64, 65}}, E7B9{40, {56, 59, 62, 65, 68}},
    AM{45, {57, 60, 64, 69}}, FM{41, {57, 60, 65, 69}}, EMAJ{40, {56, 59, 64, 68}}, DM{38, {57, 62, 65, 69}},
    BB{46, {58, 62, 65, 70}}, BBMAJ7{46, {57, 58, 62, 65, 69}}, CMAJ{48, {55, 60, 64, 67}};

// Eight bars of eighths over Am F C G (x2). 0 = rest, -1 = hold.
const int MELODY[64] = {
    76, -1, 69, -1, 72, -1, 71, 69,   72, -1, -1, -1, 69, -1, 65, -1,
    67, -1, 72, -1, 76, -1, 74, 72,   74, -1, -1, -1, 71, -1, 67, -1,
    76, -1, 81, -1, 79, -1, 76, -1,   77, -1, 76, -1, 72, -1, 69, -1,
    67, -1, 72, -1, 76, -1, 79, -1,   74, -1, 71, -1, 67, -1, 0, 0};
// Countermelody: moves while the melody holds, contrary motion elsewhere.
const int COUNTER[64] = {
    60, -1, 64, -1, 67, -1, 71, -1,   69, 67, 65, 64, 65, -1, -1, -1,
    64, -1, 62, -1, 60, -1, 62, -1,   67, 69, 71, 74, 71, -1, -1, -1,
    72, -1, 71, -1, 69, -1, 67, -1,   60, -1, 64, -1, 65, -1, 67, -1,
    72, -1, 71, -1, 69, -1, 67, -1,   67, -1, 71, -1, 74, -1, 71, 72};
// Pre-chorus over Fmaj7 G6 Em7 E7: long rising notes, then a held G# and a gap before the drop.
const int PRE_MEL[32] = {69, -1, -1, -1, 72, -1, -1, -1,   71, -1, -1, -1, 74, -1, -1, -1,
                         76, -1, -1, -1, 79, -1, -1, -1,   80, -1, -1, -1, -1, -1, 0, 0};
// Breakdown over Fmaj7 C/E Dm9 E7b9: fragments of the melody, ending on the G# leading tone.
const int BREAK_MEL[32] = {72, -1, -1, -1, 69, -1, 65, -1,   67, -1, 72, -1, 76, -1, 74, 72,
                           74, -1, -1, -1, 72, -1, 69, -1,   71, -1, -1, -1, 68, -1, -1, -1};
// The melody in harmonic minor (G -> G#) for the bosses' dominant bars.
int bossMelody(int i)
{
    int bar = i / 8;
    if (bar == 3) { const int b[8] = {74, -1, -1, -1, 71, -1, 68, -1}; return b[i % 8]; }
    if (bar == 7) { const int b[8] = {74, -1, 71, -1, 68, -1, 64, -1}; return b[i % 8]; }
    return MELODY[i];
}

enum Sec { INTRO, VERSE, PRE, DROP, DROPB, BREAK, B_INTRO, B_A, B_B, B_C };

struct Bar {
    Sec sec; int idx; Chord ch;
    int mel[8]; int ctr[8];   // -2 = nothing in this bar
};

struct Song {
    float bpm; double beat, e;
    std::vector<Bar> bars;
    explicit Song(float b) : bpm(b), beat(60.0 / b), e(30.0 / b) {}
    double t(int bar, double eighth = 0) const { return (bar * 8 + eighth) * e; }
    double barLen() const { return 4 * beat; }
    double length() const { return bars.size() * barLen(); }
    const Bar& at(double time) const
    {
        int b = (int)(time / barLen());
        return bars[std::max(0, std::min((int)bars.size() - 1, b))];
    }
    void add(Sec s, int count, const std::vector<Chord>& prog, std::function<int(int)> mel = nullptr,
             std::function<int(int)> ctr = nullptr)
    {
        for (int i = 0; i < count; ++i) {
            Bar b{s, i, prog[i % prog.size()], {}, {}};
            for (int k = 0; k < 8; ++k) {
                b.mel[k] = mel ? mel(i * 8 + k) : -2;
                b.ctr[k] = ctr ? ctr(i * 8 + k) : -2;
            }
            bars.push_back(b);
        }
    }
};

const std::vector<Chord> PROG8 = {AM9, FMAJ7, CADD9, G6, AM9, FMAJ7, CADD9, G6};

Song mainSong(int era)
{
    Song s(musicspec::BPM[era]);
    auto mel = [](int i) { return MELODY[i % 64]; };
    auto ctr = [](int i) { return COUNTER[i % 64]; };
    s.add(INTRO, 4, {AM9, FMAJ7, CADD9, G6});
    s.add(VERSE, 8, PROG8, mel);
    s.add(PRE, 4, {FMAJ7, G6, EM7, E7}, [](int i) { return PRE_MEL[i]; });
    s.add(DROP, 8, PROG8, mel, ctr);
    if (era > 0) s.add(DROPB, 8, PROG8, mel, ctr);
    s.add(BREAK, 4, {FMAJ7, CE, DM9, E7B9}, [](int i) { return BREAK_MEL[i]; });
    return s;
}

// A line of eighths -> notes. octave(bar) shifts by bar; gate is the fraction of the length that sounds.
struct LN { int bar, step, len, pitch; };
std::vector<LN> lineOf(const Song& s, bool counter)
{
    std::vector<LN> out;
    const int N = (int)s.bars.size() * 8;
    auto get = [&](int i) { const Bar& b = s.bars[i / 8]; return counter ? b.ctr[i % 8] : b.mel[i % 8]; };
    for (int i = 0; i < N; ++i) {
        int v = get(i);
        if (v <= 0) continue;
        int len = 1;
        while (i + len < N && get(i + len) == -1) ++len;
        out.push_back({i / 8, i % 8, len, v});
    }
    return out;
}

int approach(int root, int next)
{
    int n = next - 1;
    while (n > root + 6) n -= 12;
    while (n < root - 6) n += 12;
    return n;
}

int nextRoot(const Song& s, int b) { return s.bars[(b + 1) % s.bars.size()].ch.root; }

// Arpeggio note k (16ths) over a voicing: up-down through it, an octave above.
int arpNote(const Chord& c, int k, int oct = 12)
{
    const int n = (int)c.v.size();
    int cyc = 2 * n - 2, j = k % cyc;
    int idx = j < n ? j : cyc - j;
    return c.v[idx] + oct;
}

// ------------------------------------------------------------------ era 0: 1-bit beeper
struct Beep { double t, dur; float f0, f1, amp; int prio; bool noise; float decay; };

void beeper(const std::vector<Beep>& evIn, Stereo& out)
{
    std::vector<Beep> ev = evIn;
    std::stable_sort(ev.begin(), ev.end(), [](const Beep& a, const Beep& b) { return a.prio != b.prio ? a.prio < b.prio : a.t < b.t; });
    std::vector<int> owner(out.size(), -1);
    for (size_t k = 0; k < ev.size(); ++k) {
        size_t a = (size_t)(ev[k].t * SR), b = std::min(out.size(), (size_t)((ev[k].t + ev[k].dur) * SR));
        for (size_t i = a; i < b; ++i) owner[i] = (int)k;
    }
    Rng rng(99);
    float ph = 0, nz = 1, nph = 0, lp = 0, lp2 = 0;
    const float a = 1.0f - std::exp(-TAU * 5200.0f / SR);
    for (size_t i = 0; i < out.size(); ++i) {
        float v = 0;
        int k = owner[i];
        if (k >= 0) {
            const Beep& b = ev[k];
            float tl = (float)((double)i / SR - b.t);
            float f = b.f0 * std::pow(b.f1 / b.f0, (float)(tl / b.dur));
            float amp = b.amp * (b.decay > 0 ? std::max(0.3f, std::exp(-tl / b.decay)) : 1.0f);
            if (b.noise) {
                nph += f / SR;
                if (nph >= 1) { nph -= 1; nz = rng.next() > 0 ? 1.0f : -1.0f; }
                v = nz * amp;
            } else {
                float dt = f / SR;
                ph += dt; if (ph >= 1) ph -= 1;
                v = pulseB(ph, dt, 0.5f) * amp;
            }
        }
        lp += a * (v - lp);         // the little speaker
        lp2 += a * (lp - lp2);
        out.L[i] += lp2;
        out.R[i] += lp2;
    }
}

void arrangeEra0(const Song& s, Mix& m)
{
    std::vector<Beep> ev;
    const double e = s.e;
    const int N = (int)s.bars.size();
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        const double t0 = s.t(b);
        auto kickB = [&](double t) { ev.push_back({t, 0.028, 220, 45, 0.6f, 4, false, 0}); };
        auto snareB = [&](double t, float v = 0.4f) { ev.push_back({t, 0.035, 5500, 3000, v, 4, true, 0.02f}); };
        auto hatB = [&](double t) { ev.push_back({t, 0.008, 9000, 9000, 0.22f, 4, true, 0}); };
        auto tickB = [&](double t, bool hi) { ev.push_back({t, 0.007, hi ? 2400.0f : 1800.0f, hi ? 2400.0f : 1800.0f, 0.32f, 4, false, 0}); };
        auto arps = [&](double from, double to, float amp) {   // fake chord: cycle the voicing every 22 ms
            int k = 0;
            for (double t = from; t < to - 0.001; t += 0.022, ++k) {
                int n = B.ch.v[k % std::min<size_t>(4, B.ch.v.size())] + 12;
                ev.push_back({t, std::min(0.022, to - t), mtof((float)n), mtof((float)n), amp, 1, false, 0});
            }
        };
        // bass blip on beats 1 and 3 (and the approach on the last eighth)
        if (B.sec != INTRO || B.idx >= 2) {
            int R = B.ch.root + 12;
            ev.push_back({t0, 0.07, mtof((float)R), mtof((float)R), 0.45f, 3, false, 0.08f});
            if (B.sec != BREAK) ev.push_back({t0 + 4 * e, 0.07, mtof((float)R + 7), mtof((float)R + 7), 0.45f, 3, false, 0.08f});
            if (B.sec == DROP) {
                int ap = approach(B.ch.root, nextRoot(s, b)) + 12;
                ev.push_back({t0 + 7 * e, 0.06, mtof((float)ap), mtof((float)ap), 0.4f, 3, false, 0.08f});
            }
        }
        switch (B.sec) {
        case INTRO:
            for (int k = 0; k < 4; ++k) tickB(t0 + (2 * k + 1) * e, k % 2 == 0);
            for (int k = 0; k < 4; ++k) arps(t0 + 2 * k * e, t0 + 2 * k * e + e * 0.9, 0.16f);
            break;
        case VERSE:
            kickB(t0); kickB(t0 + 4 * e);
            snareB(t0 + 2 * e); snareB(t0 + 6 * e);
            if (B.idx % 2) hatB(t0 + 7 * e);
            break;
        case PRE:
            kickB(t0); kickB(t0 + 4 * e);
            if (B.idx < 2) { snareB(t0 + 2 * e); snareB(t0 + 6 * e); }
            else if (B.idx == 2) for (int k = 0; k < 8; ++k) snareB(t0 + k * e, 0.25f + 0.03f * k);
            else for (int k = 0; k < 12; ++k) snareB(t0 + k * e / 2, 0.25f + 0.02f * k);
            for (int k = 0; k < 8; ++k) {   // rising arpeggio build
                if (B.idx == 3 && k >= 6) break;
                int n = arpNote(B.ch, k + B.idx * 2, 12 + (k / 4) * 12);
                ev.push_back({t0 + k * e, e * 0.5, mtof((float)n), mtof((float)n), 0.2f, 1, false, 0.05f});
            }
            break;
        case DROP:
            for (int k = 0; k < 4; ++k) kickB(t0 + 2 * k * e);
            snareB(t0 + 2 * e); snareB(t0 + 6 * e);
            for (int k = 0; k < 4; ++k) hatB(t0 + (2 * k + 1) * e);
            arps(t0, t0 + 8 * e, 0.18f);
            if (B.idx == 7) for (int k = 0; k < 4; ++k) snareB(t0 + 6 * e + k * e / 2, 0.3f + 0.05f * k);
            break;
        case BREAK:
            for (int k = 0; k < 4; ++k) tickB(t0 + (2 * k + 1) * e, k % 2 == 0);
            for (int k = 0; k < 4; ++k) arps(t0 + 2 * k * e, t0 + 2 * k * e + e * 0.6, 0.12f);
            break;
        default: break;
        }
    }
    for (const LN& l : lineOf(s, false)) {
        const Bar& B = s.bars[l.bar];
        double t = s.t(l.bar, l.step);
        double dur = l.len * e * (B.sec == BREAK || B.sec == PRE ? 0.85 : 0.62);
        float f = mtof((float)l.pitch);
        ev.push_back({t, dur, f, f, B.sec == BREAK ? 0.36f : 0.45f, 2, false, 0.22f});
    }
    beeper(ev, m.tmp);
    m.send(1.0f, 0);
}

// ------------------------------------------------------------------ era 1: NES
Patch nesPulse(float duty, float gain, float pan)
{
    Patch p; p.wave = W_PULSE; p.duty = duty; p.a = 0.002f; p.d = 0.35f; p.s = 0.55f; p.r = 0.03f;
    p.nesVol = true; p.gain = gain; p.pan = pan;
    return p;
}

void arrangeEra1(const Song& s, Mix& m)
{
    const double e = s.e;
    const int N = (int)s.bars.size();
    // ---- noise + triangle drums
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        const double t0 = s.t(b);
        auto sn = [&](double t, float g = 0.32f) { nesNoise(m.tmp, t, g, 28000, false, 0.07f); nesNoise(m.tmp, t, g * 0.4f, 7000, false, 0.03f); };
        auto hh = [&](double t, float g = 0.1f, float d = 0.018f) { nesNoise(m.tmp, t, g, 220000, false, d, 0.15f); };
        auto tk = [&](double t, bool hi) { nesNoise(m.tmp, t, 0.12f, hi ? 110000.0f : 70000.0f, true, 0.012f, hi ? -0.3f : 0.3f); };
        switch (B.sec) {
        case INTRO:
            for (int k = 0; k < 4; ++k) tk(t0 + (2 * k + 1) * e, k % 2 == 0);
            if (B.idx == 3) for (int k = 0; k < 4; ++k) sn(t0 + (4 + k) * e, 0.12f + 0.05f * k);
            break;
        case VERSE:
            nesKick(m.tmp, t0, 0.5f); nesKick(m.tmp, t0 + 4 * e, 0.5f);
            if (B.idx % 2) nesKick(m.tmp, t0 + 5 * e, 0.4f);
            sn(t0 + 2 * e); sn(t0 + 6 * e);
            for (int k = 0; k < 8; ++k) hh(t0 + k * e, k % 2 ? 0.1f : 0.06f);
            break;
        case PRE:
            for (int k = 0; k < 4; ++k) nesKick(m.tmp, t0 + 2 * k * e, 0.45f);
            if (B.idx < 2) { sn(t0 + 2 * e); sn(t0 + 6 * e); }
            else if (B.idx == 2) for (int k = 0; k < 8; ++k) sn(t0 + k * e, 0.15f + 0.02f * k);
            else for (int k = 0; k < 12; ++k) sn(t0 + k * e / 2, 0.18f + 0.015f * k);
            break;
        case DROP: case DROPB:
            if (B.idx == 0) nesNoise(m.tmp, t0, 0.3f, 50000, false, 0.35f);   // crash
            for (int k = 0; k < 4; ++k) nesKick(m.tmp, t0 + 2 * k * e, 0.5f);
            sn(t0 + 2 * e); sn(t0 + 6 * e);
            for (int k = 0; k < 16; ++k) hh(t0 + k * e / 2, k % 4 == 2 ? 0.11f : 0.06f, k % 4 == 2 ? 0.05f : 0.015f);
            if (B.idx == 7) for (int k = 0; k < 4; ++k) sn(t0 + 6 * e + k * e / 2, 0.2f + 0.04f * k);
            break;
        case BREAK:
            for (int k = 0; k < 4; ++k) tk(t0 + (2 * k + 1) * e, k % 2 == 0);
            nesKick(m.tmp, t0, 0.3f);
            break;
        default: break;
        }
    }
    m.send(1.0f, 0);

    // ---- triangle bass
    std::vector<Note> bass;
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        const int R = B.ch.root, ap = approach(R, nextRoot(s, b));
        auto put = [&](int step, int len, int p) { bass.push_back({s.t(b, step), len * e * 0.92, (float)p}); };
        switch (B.sec) {
        case INTRO: if (B.idx >= 2) { put(0, 3, R); put(4, 3, R); } break;
        case VERSE: put(0, 3, R); put(3, 1, R + 12); put(4, 2, R); put(6, 1, R + 7); put(7, 1, ap); break;
        case PRE: for (int k = 0; k < 8; ++k) if (!(B.idx == 3 && k >= 6)) put(k, 1, R + (k % 2) * 12); break;
        case DROP: case DROPB: for (int k = 0; k < 7; ++k) put(k, 1, R + (k % 2) * 12); put(7, 1, ap); break;
        case BREAK: put(0, 6, R); put(6, 2, R + 7); break;
        default: break;
        }
    }
    Patch tri; tri.wave = W_NESTRI; tri.a = 0.001f; tri.d = 0; tri.s = 1; tri.r = 0.006f; tri.gain = 0.42f;
    renderNotes(m.tmp, tri, bass, 1);
    m.send(1.0f, 0);

    // ---- pulse 1: lead (duty by section) with delayed vibrato; pulse 2: echo / counter / arps
    std::vector<Note> lead50, lead25, lead12, echo, ctr, arp;
    for (const LN& l : lineOf(s, false)) {
        const Bar& B = s.bars[l.bar];
        bool drop = B.sec == DROP || B.sec == DROPB;
        Note n{s.t(l.bar, l.step), l.len * e * 0.9, (float)l.pitch + (drop ? 12 : 0)};
        (drop ? lead25 : (B.sec == BREAK ? lead12 : lead50)).push_back(n);
        if (!drop) { Note ec = n; ec.t += 3 * e; ec.vel = 0.38f; ec.dur = std::min(ec.dur, 2 * e); echo.push_back(ec); }
    }
    for (const LN& l : lineOf(s, true)) ctr.push_back({s.t(l.bar, l.step), l.len * e * 0.85, (float)l.pitch + 12});
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        if (B.sec == INTRO || B.sec == PRE || (B.sec == DROPB && B.idx % 2 == 1))
            for (int k = 0; k < 16; ++k) {
                if (B.sec == PRE && B.idx == 3 && k >= 12) break;
                int oct = B.sec == PRE ? 12 + (B.idx >= 2 ? 12 : 0) : 12;
                arp.push_back({s.t(b, k * 0.5), e * 0.42, (float)arpNote(B.ch, k, oct), B.sec == INTRO ? 1.3f : 0.8f});
            }
    }
    Patch p1 = nesPulse(0.5f, 0.15f, -0.25f); p1.vib = 0.22f; p1.vibDelay = 0.22f; p1.vibRate = 6.0f;
    renderNotes(m.tmp, p1, lead50, 2);
    p1.duty = 0.25f; renderNotes(m.tmp, p1, lead25, 3);
    p1.duty = 0.125f; renderNotes(m.tmp, p1, lead12, 4);
    Patch p2 = nesPulse(0.25f, 0.13f, 0.25f);
    renderNotes(m.tmp, p2, echo, 5);
    p2.duty = 0.5f; p2.vib = 0.15f; p2.vibDelay = 0.3f; renderNotes(m.tmp, p2, ctr, 6);
    Patch pa = nesPulse(0.125f, 0.075f, 0.25f); pa.d = 0.08f; pa.s = 0.3f;
    renderNotes(m.tmp, pa, arp, 7);
    m.send(1.0f, 0);
}

// ------------------------------------------------------------------ era 2: 16-bit FM
Patch fmPiano()
{
    Patch p; p.wave = W_FM; p.fmRatio = 1; p.fmIndex = 1.6f; p.fmDecay = 0.5f; p.fmSus = 0.15f;
    p.fm2Ratio = 14; p.fm2Index = 0.9f; p.fm2Decay = 0.025f;
    p.a = 0.002f; p.d = 1.1f; p.s = 0.25f; p.r = 0.25f; p.gain = 0.075f; p.vib = 0.04f; p.vibDelay = 0;
    return p;
}
Patch fmBrass()
{
    Patch p; p.wave = W_FM; p.fmRatio = 1; p.fmIndex = 2.6f; p.fmEnv = true;
    p.a = 0.035f; p.d = 0.3f; p.s = 0.75f; p.r = 0.1f; p.gain = 0.17f;
    p.vib = 0.2f; p.vibRate = 5.6f; p.vibDelay = 0.18f; p.glide = 0.05f;
    return p;
}
Patch strings(float gain)
{
    Patch p; p.wave = W_SAW; p.unison = 3; p.detune = 0.18f; p.width = 0.7f; p.filt = true; p.cutoff = 2600; p.q = 0.6f;
    p.a = 0.35f; p.d = 1.0f; p.s = 0.9f; p.r = 0.5f; p.gain = gain; p.vib = 0.05f; p.vibDelay = 0.3f;
    return p;
}

// Orchestral hit: a short brassy chord stack + noise burst (renders into m.tmp).
void orchHit(Stereo& o, double t, const Chord& c, float g, uint32_t seed)
{
    Patch p; p.wave = W_SAW; p.unison = 3; p.detune = 0.2f; p.width = 0.8f; p.filt = true; p.cutoff = 900; p.fenv = 3.0f;
    p.fdecay = 0.08f; p.q = 0.9f; p.a = 0.002f; p.d = 0.18f; p.s = 0.0f; p.r = 0.15f; p.gain = g;
    std::vector<Note> v;
    for (int n : {c.root, c.root + 12, c.root + 19, c.root + 24, c.v[1] + 12}) v.push_back({t, 0.35, (float)n});
    renderNotes(o, p, v, seed);
    snare(o, t, g * 0.8f, 120, 0.06f, 0.08f, 400);
    tom(o, t, g * 1.4f, 55, 0.4f);
}

void arrangeEra2(const Song& s, Mix& m)
{
    const double e = s.e;
    const int N = (int)s.bars.size();
    // ---- kit (kick/hats dry-ish), snare/toms (more room)
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        const double t0 = s.t(b);
        auto K = [&](double t, float g = 0.85f) { kick(m.tmp, t, g, 180, 55, 0.025f, 0.22f, 0.5f, 2.4f); m.kicks.push_back(t); };
        auto H = [&](double t, float g = 0.13f, float d = 0.03f) { hat(m.tmp, t, g, d, 0.2f, 6500); };
        switch (B.sec) {
        case VERSE:
            K(t0); K(t0 + 3 * e, 0.6f); K(t0 + 4 * e);
            for (int k = 0; k < 8; ++k) H(t0 + k * e, k % 2 ? 0.14f : 0.08f);
            break;
        case PRE:
            for (int k = 0; k < 4; ++k) K(t0 + 2 * k * e);
            if (B.idx < 3) for (int k = 0; k < 8; ++k) H(t0 + k * e, 0.1f);
            break;
        case DROP: case DROPB:
            for (int k = 0; k < 4; ++k) K(t0 + 2 * k * e);
            for (int k = 0; k < 16; ++k) H(t0 + k * e / 2, k % 4 == 2 ? 0.16f : 0.08f, k % 4 == 2 ? 0.09f : 0.025f);
            if (B.idx == 0) crash(m.tmp, t0, 0.28f);
            break;
        case INTRO: if (B.idx >= 2) for (int k = 0; k < 4; ++k) H(t0 + (2 * k + 1) * e, 0.07f); break;
        case BREAK: if (B.idx == 0) crash(m.tmp, t0, 0.15f); break;
        default: break;
        }
    }
    m.send(1.0f, 0.06f);
    m.buildDuck(s.beat * 0.6);
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        const double t0 = s.t(b);
        auto S = [&](double t, float g = 0.5f) { snare(m.tmp, t, g, 210, 0.13f, 0.06f, 1100); };
        switch (B.sec) {
        case INTRO: tickTock(m.tmp, t0, s.beat, 1, 0.3f); break;
        case BREAK: tickTock(m.tmp, t0, s.beat, 1, 0.3f); break;
        case VERSE: S(t0 + 2 * e); S(t0 + 6 * e); break;
        case PRE:
            if (B.idx < 2) { S(t0 + 2 * e); S(t0 + 6 * e); }
            else if (B.idx == 2) for (int k = 0; k < 8; ++k) S(t0 + k * e, 0.2f + 0.03f * k);
            else for (int k = 0; k < 12; ++k) S(t0 + k * e / 2, 0.22f + 0.025f * k);
            break;
        case DROP: case DROPB:
            S(t0 + 2 * e); S(t0 + 6 * e);
            if (B.idx == 0) orchHit(m.tmp, t0, B.ch, 0.22f, 100 + b);
            if (B.idx == 7 && B.sec == DROPB) {
                const float th[6] = {220, 196, 165, 147, 123, 110};
                for (int k = 0; k < 6; ++k) tom(m.tmp, t0 + 5 * e + k * e / 2, 0.45f, th[k], 0.2f, 0.4f - 0.16f * k);
            } else if (B.idx == 7) for (int k = 0; k < 4; ++k) S(t0 + 6 * e + k * e / 2, 0.25f + 0.07f * k);
            break;
        default: break;
        }
    }
    m.send(1.0f, 0.22f, 0.05f);

    // ---- slap / FM bass
    std::vector<Note> bass;
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        const int R = B.ch.root, ap = approach(R, nextRoot(s, b));
        auto put = [&](int step, int len, int p, float v = 1) { bass.push_back({s.t(b, step), len * e * 0.85, (float)p, v}); };
        switch (B.sec) {
        case INTRO: if (B.idx >= 2) { put(0, 4, R, 0.7f); put(4, 4, R, 0.7f); } break;
        case VERSE: put(0, 3, R); put(3, 1, R + 12, 0.8f); put(4, 2, R); put(6, 1, R + 7, 0.8f); put(7, 1, ap, 0.8f); break;
        case PRE: for (int k = 0; k < 8; ++k) if (!(B.idx == 3 && k >= 6)) put(k, 1, R, k % 2 ? 0.75f : 1.0f); break;
        case DROP: case DROPB:
            put(0, 1, R); put(1, 1, R + 12, 0.75f); put(2, 1, R, 0.6f); put(3, 1, R + 12, 0.85f);
            put(4, 1, R + 7, 0.9f); put(5, 1, R + 12, 0.75f); put(6, 1, R, 0.8f); put(7, 1, ap, 0.85f);
            break;
        case BREAK: put(0, 7, R, 0.7f); break;
        default: break;
        }
    }
    Patch sb; sb.wave = W_FM; sb.fmRatio = 1; sb.fmIndex = 3.5f; sb.fmDecay = 0.05f; sb.fmSus = 0.2f; sb.sub = 0.5f;
    sb.a = 0.002f; sb.d = 0.25f; sb.s = 0.55f; sb.r = 0.05f; sb.gain = 0.33f;
    renderNotes(m.tmp, sb, bass, 11);
    m.send(1.0f, 0.02f, 0, 0.3f);

    // ---- strings pad + e-piano comping
    std::vector<Note> pad, ep;
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        if (B.sec != PRE)
            for (size_t k = 0; k < B.ch.v.size(); ++k) pad.push_back({s.t(b), s.barLen() * 0.98, (float)B.ch.v[k]});
        auto comp = [&](double step, double len, float vel) {
            for (size_t k = 0; k < B.ch.v.size(); ++k)
                ep.push_back({s.t(b, step), len * e, (float)B.ch.v[k], vel, -1, k % 2 ? 0.3f : -0.3f});
        };
        switch (B.sec) {
        case INTRO: comp(0, 3, 0.7f); comp(3, 2, 0.5f); comp(6, 2, 0.6f); break;
        case VERSE: comp(0, 1.5, 0.8f); comp(3, 1, 0.6f); comp(6, 1.5, 0.7f); break;
        case PRE: for (int k = 0; k < 8; ++k) if (!(B.idx == 3 && k >= 6)) comp(k, 0.7, 0.45f + 0.04f * k); break;
        case BREAK: comp(0, 6, 0.8f); break;
        default: comp(1, 0.6, 0.55f); comp(3, 0.6, 0.6f); comp(5, 0.6, 0.55f); comp(7, 0.6, 0.6f); break;   // off-beat stabs
        }
    }
    renderNotes(m.tmp, strings(0.045f), pad, 12);
    autoFilter(m.tmp, [&](double t) {
        const Bar& B = s.at(t);
        double k = std::fmod(t, s.barLen()) / s.barLen();
        if (B.sec == INTRO) return (float)(500 * std::exp2((B.idx + k) * 0.8));
        return 16000.0f;
    });
    m.send(1.0f, 0.3f, 0, 0.3f);
    renderNotes(m.tmp, fmPiano(), ep, 13);
    m.send(1.0f, 0.18f, 0.18f, 0.25f);

    // ---- brass lead (melody) + marimba counter
    std::vector<Note> lead, ctr;
    for (const LN& l : lineOf(s, false)) {
        const Bar& B = s.bars[l.bar];
        bool drop = B.sec == DROP || B.sec == DROPB;
        lead.push_back({s.t(l.bar, l.step), l.len * e * 0.92, (float)l.pitch + (drop ? 12 : 0), B.sec == BREAK ? 0.7f : 1.0f});
    }
    linkGlides(lead, e * 0.3);
    for (const LN& l : lineOf(s, true)) ctr.push_back({s.t(l.bar, l.step), l.len * e * 0.8, (float)l.pitch, 0.9f});
    renderNotes(m.tmp, fmBrass(), lead, 14);
    m.send(1.0f, 0.22f, 0.3f);
    Patch mar; mar.wave = W_FM; mar.fmRatio = 4; mar.fmIndex = 1.4f; mar.fmDecay = 0.06f;
    mar.a = 0.002f; mar.d = 0.35f; mar.s = 0.0f; mar.r = 0.1f; mar.gain = 0.14f; mar.pan = 0.35f;
    renderNotes(m.tmp, mar, ctr, 15);
    m.send(1.0f, 0.2f, 0.25f, 0.2f);

    // ---- riser into the drop
    for (int b = 0; b < N; ++b)
        if (s.bars[b].sec == PRE && s.bars[b].idx == 0) riser(m.tmp, s.t(b), s.barLen() * 4 - s.beat, 0.4f);
    m.send(1.0f, 0.2f);
}

// ------------------------------------------------------------------ era 3: modern
Patch supersaw(float gain, float cutoff, float attack)
{
    Patch p; p.wave = W_SAW; p.unison = 7; p.detune = 0.38f; p.width = 0.95f; p.filt = true; p.cutoff = cutoff; p.q = 0.8f;
    p.fenv = 0.6f; p.fdecay = 0.4f; p.a = attack; p.d = 0.6f; p.s = 0.8f; p.r = 0.3f; p.gain = gain;
    return p;
}
Patch pluck(float gain)
{
    Patch p; p.wave = W_SAW; p.unison = 2; p.detune = 0.12f; p.width = 0.35f; p.osc2 = 0.4f; p.osc2semi = 12; p.osc2duty = 0.3f;
    p.filt = true; p.cutoff = 700; p.q = 1.3f; p.fenv = 3.4f; p.fdecay = 0.11f; p.keytrack = 0.5f;
    p.a = 0.002f; p.d = 0.28f; p.s = 0.3f; p.r = 0.14f; p.gain = gain; p.glide = 0.045f; p.vib = 0.12f; p.vibDelay = 0.28f;
    return p;
}
Patch piano(float gain) { Patch p; p.wave = W_PIANO; p.a = 0.001f; p.d = 3.0f; p.s = 0.0f; p.r = 0.35f; p.gain = gain; return p; }

void arrangeEra3(const Song& s, Mix& m)
{
    const double e = s.e;
    const int N = (int)s.bars.size();
    // ---- kick + hats (dry)
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        const double t0 = s.t(b);
        auto K = [&](double t, float g = 1.0f) { kick(m.tmp, t, g); m.kicks.push_back(t); };
        auto CH = [&](double t, float g) { hat(m.tmp, t, g, 0.028f, 0.15f); };
        switch (B.sec) {
        case INTRO: if (B.idx >= 2) for (int k = 0; k < 8; ++k) CH(t0 + k * e, k % 2 ? 0.08f : 0.04f); break;
        case VERSE:
            K(t0); K(t0 + 4 * e); if (B.idx % 2) K(t0 + 7 * e, 0.7f);
            for (int k = 0; k < 8; ++k) CH(t0 + k * e, k % 2 ? 0.13f : 0.07f);
            break;
        case PRE:
            for (int k = 0; k < 4; ++k) K(t0 + 2 * k * e, B.idx == 3 && k == 3 ? 0.0f : 1.0f);
            if (B.idx < 3) for (int k = 0; k < 16; ++k) CH(t0 + k * e / 2, 0.05f + 0.006f * k);
            break;
        case DROP: case DROPB:
            for (int k = 0; k < 4; ++k) K(t0 + 2 * k * e);
            for (int k = 0; k < 16; ++k) CH(t0 + k * e / 2, k % 2 ? 0.1f : 0.05f);
            for (int k = 0; k < 4; ++k) hat(m.tmp, t0 + (2 * k + 1) * e, 0.13f, 0.11f, -0.2f, 6000);   // open hats
            break;
        case BREAK: break;
        default: break;
        }
    }
    m.send(1.0f, 0.04f);
    m.buildDuck(s.beat * 0.85);
    // ---- clap / snare roll / toms / ticks / FX (roomy)
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        const double t0 = s.t(b);
        switch (B.sec) {
        case INTRO:
            tickTock(m.tmp, t0, s.beat, 1, 0.32f);
            if (B.idx == 3) swell(m.tmp, t0 + 4 * e, 4 * e, 0.25f);
            break;
        case VERSE: clap(m.tmp, t0 + 2 * e, 0.5f); clap(m.tmp, t0 + 6 * e, 0.5f); break;
        case PRE:
            if (B.idx == 0) riser(m.tmp, t0, s.barLen() * 4 - s.beat, 0.55f);
            if (B.idx < 2) { clap(m.tmp, t0 + 2 * e, 0.45f); clap(m.tmp, t0 + 6 * e, 0.45f); }
            else if (B.idx == 2) for (int k = 0; k < 8; ++k) snare(m.tmp, t0 + k * e, 0.2f + 0.03f * k);
            else for (int k = 0; k < 12; ++k) snare(m.tmp, t0 + k * e / 2, 0.22f + 0.025f * k);
            break;
        case DROP: case DROPB:
            if (B.idx == 0) { crash(m.tmp, t0, 0.3f, 1.4f); impact(m.tmp, t0, 0.55f); }
            clap(m.tmp, t0 + 2 * e, 0.55f); clap(m.tmp, t0 + 6 * e, 0.55f);
            if (B.idx == 7 && B.sec == DROPB) {
                const float th[8] = {196, 175, 147, 131, 110, 98, 82, 73};
                for (int k = 0; k < 8; ++k) tom(m.tmp, t0 + 4 * e + k * e / 2, 0.5f, th[k], 0.25f, 0.5f - 0.14f * k);
            } else if (B.idx == 7) for (int k = 0; k < 4; ++k) snare(m.tmp, t0 + 6 * e + k * e / 2, 0.25f + 0.08f * k);
            break;
        case BREAK:
            tickTock(m.tmp, t0, s.beat, 1, 0.3f);
            if (B.idx == 0) crash(m.tmp, t0, 0.18f, 2.0f);
            if (B.idx == 3) swell(m.tmp, t0, s.barLen(), 0.35f);
            break;
        default: break;
        }
    }
    m.send(1.0f, 0.25f, 0.06f);

    // ---- sub + reese bass
    std::vector<Note> sub, reese;
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        const int R = B.ch.root - 12, ap = approach(R, nextRoot(s, b) - 12);
        auto put = [&](int step, int len, int p, float v = 1) {
            sub.push_back({s.t(b, step), len * e * 0.9, (float)p, v});
            if (B.sec == DROP || B.sec == DROPB || B.sec == PRE) reese.push_back({s.t(b, step), len * e * 0.9, (float)p + 12, v});
        };
        switch (B.sec) {
        case INTRO: if (B.idx >= 2) put(0, 8, R, 0.6f); break;
        case VERSE: put(0, 3, R, 0.75f); put(3, 1, R + 12, 0.55f); put(4, 3, R, 0.75f); put(7, 1, ap, 0.6f); break;
        case PRE: for (int k = 0; k < 8; ++k) if (!(B.idx == 3 && k >= 6)) put(k, 1, R); break;
        case DROP: case DROPB:
            put(0, 2, R); put(2, 2, R); put(4, 2, R); put(6, 1, R + 12, 0.9f); put(7, 1, ap, 0.9f);
            break;
        case BREAK: put(0, 8, R, 0.5f); break;
        default: break;
        }
    }
    Patch ps; ps.wave = W_SINE; ps.a = 0.004f; ps.d = 0.2f; ps.s = 0.9f; ps.r = 0.06f; ps.gain = 0.5f; ps.glide = 0.03f;
    renderNotes(m.tmp, ps, sub, 21);
    m.send(1.0f, 0, 0, 0.9f);
    Patch pr; pr.wave = W_SAW; pr.unison = 2; pr.detune = 0.3f; pr.width = 0.6f; pr.filt = true; pr.cutoff = 380; pr.q = 1.4f;
    pr.fenv = 1.6f; pr.fdecay = 0.12f; pr.a = 0.004f; pr.d = 0.3f; pr.s = 0.8f; pr.r = 0.06f; pr.gain = 0.14f;
    renderNotes(m.tmp, pr, reese, 22);
    m.send(1.0f, 0.02f, 0, 0.85f);

    // ---- supersaw chords / pads, arps, piano
    std::vector<Note> pads, saws, arps, keys;
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        for (int n : B.ch.v) {
            if (B.sec == DROP || B.sec == DROPB) saws.push_back({s.t(b), s.barLen() * 0.97, (float)n});
            else if (B.sec != BREAK) pads.push_back({s.t(b), s.barLen() * 0.97, (float)n});
        }
        if (B.sec == BREAK) {
            for (int n : B.ch.v) { keys.push_back({s.t(b), 3.6 * e, (float)n, 0.7f}); keys.push_back({s.t(b, 4), 3.6 * e, (float)n, 0.5f}); }
            keys.push_back({s.t(b), 7 * e, (float)B.ch.root, 0.7f});
        }
        if (B.sec == INTRO || B.sec == PRE || B.sec == DROPB || (B.sec == DROP && B.idx >= 4))
            for (int k = 0; k < 16; ++k) {
                if (B.sec == PRE && B.idx == 3 && k >= 12) break;
                arps.push_back({s.t(b, k * 0.5), e * 0.4, (float)arpNote(B.ch, k, 12), k % 4 == 0 ? 1.0f : 0.7f});
            }
    }
    auto cutoff = [&](double t) {
        const Bar& B = s.at(t);
        double k = (B.idx + std::fmod(t, s.barLen()) / s.barLen());
        if (B.sec == INTRO) return (float)(320 * std::exp2(k * 0.55));
        if (B.sec == PRE) return (float)(900 * std::pow(18.0, std::pow(k / 4.0, 1.6)));
        if (B.sec == BREAK) return 3500.0f;
        if (B.sec == VERSE) return (float)(1500 * std::exp2(k / 8.0));   // verse stays darker than the drop
        return 17000.0f;
    };
    renderNotes(m.tmp, supersaw(0.05f, 1400, 0.25f), pads, 23);
    autoFilter(m.tmp, cutoff);
    m.send(1.0f, 0.35f, 0, 0.55f);
    renderNotes(m.tmp, supersaw(0.06f, 4200, 0.01f), saws, 24);
    m.send(1.0f, 0.3f, 0, 0.7f);
    Patch pa = pluck(0.07f); pa.glide = 0; pa.vib = 0; pa.pan = 0; pa.width = 0.8f;
    renderNotes(m.tmp, pa, arps, 25);
    autoFilter(m.tmp, cutoff);
    m.send(1.0f, 0.2f, 0.35f, 0.5f);
    renderNotes(m.tmp, piano(0.11f), keys, 26);
    m.send(1.0f, 0.45f);

    // ---- lead: piano in the verse/breakdown, pluck in the build and drop (an octave up, layered)
    std::vector<Note> lead, pianoLead, ctr;
    for (const LN& l : lineOf(s, false)) {
        const Bar& B = s.bars[l.bar];
        bool drop = B.sec == DROP || B.sec == DROPB;
        Note n{s.t(l.bar, l.step), l.len * e * 0.9, (float)l.pitch + (drop ? 12 : 0)};
        if (B.sec == VERSE || B.sec == BREAK) { n.dur = l.len * e * 1.1; pianoLead.push_back(n); }
        if (B.sec != BREAK && B.sec != VERSE) lead.push_back(n);
        if (B.sec == VERSE && B.idx >= 4) { n.vel = 0.55f; lead.push_back(n); }
    }
    linkGlides(lead, e * 0.25);
    for (const LN& l : lineOf(s, true)) ctr.push_back({s.t(l.bar, l.step), l.len * e * 0.85, (float)l.pitch + 12, 0.85f});
    renderNotes(m.tmp, pluck(0.2f), lead, 27);
    m.send(1.0f, 0.25f, 0.3f, 0.25f);
    renderNotes(m.tmp, piano(0.2f), pianoLead, 28);
    m.send(1.0f, 0.35f, 0.15f);
    Patch bell; bell.wave = W_FM; bell.fmRatio = 3.5f; bell.fmIndex = 1.3f; bell.fmDecay = 0.25f; bell.a = 0.002f;
    bell.d = 0.5f; bell.s = 0.1f; bell.r = 0.2f; bell.gain = 0.1f; bell.pan = -0.3f;
    renderNotes(m.tmp, bell, ctr, 29);
    m.send(1.0f, 0.3f, 0.35f, 0.3f);
}

// ------------------------------------------------------------------ boss: The School Bell (NES)
Song schoolBellSong()
{
    Song s(musicspec::BOSS_BPM[1]);
    s.add(B_INTRO, 4, {AM});
    s.add(B_A, 8, {AM, AM, FM, EMAJ});
    s.add(B_B, 8, {DM, AM, BB, E7}, [](int i) { return bossMelody(i % 64); });
    s.add(B_C, 4, {E7B9});
    return s;
}

void arrangeSchoolBell(const Song& s, Mix& m)
{
    const double e = s.e;
    const int N = (int)s.bars.size();
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        const double t0 = s.t(b);
        auto sn = [&](double t, float g = 0.32f) { nesNoise(m.tmp, t, g, 28000, false, 0.07f); };
        // relentless ticking: metallic short-mode noise on every 16th, tick-tock accents on off-beats
        for (int k = 0; k < 16; ++k) {
            bool off = k % 4 == 2;
            if (B.sec == B_INTRO && B.idx < 2 && !off) continue;
            nesNoise(m.tmp, t0 + k * e / 2, off ? 0.13f : 0.05f, off ? (k % 8 == 2 ? 110000.0f : 70000.0f) : 220000.0f, off, off ? 0.015f : 0.01f,
                     off ? (k % 8 == 2 ? -0.3f : 0.3f) : 0.1f);
        }
        if (B.sec == B_INTRO) {
            if (B.idx >= 2) { nesKick(m.tmp, t0, 0.45f); nesKick(m.tmp, t0 + 4 * e, 0.45f); }
            if (B.idx == 3) for (int k = 0; k < 8; ++k) sn(t0 + 4 * e + k * e / 2, 0.12f + 0.03f * k);
            continue;
        }
        for (int k = 0; k < 4; ++k) nesKick(m.tmp, t0 + 2 * k * e, 0.5f);
        if (B.sec == B_C) {
            if (B.idx < 2) for (int k = 0; k < 8; ++k) sn(t0 + k * e, 0.15f + 0.02f * k);
            else if (B.idx == 2) for (int k = 0; k < 16; ++k) sn(t0 + k * e / 2, 0.15f + 0.01f * k);
            else for (int k = 0; k < 12; ++k) sn(t0 + k * e / 2, 0.3f);
        } else {
            sn(t0 + 2 * e); sn(t0 + 6 * e);
            if (B.idx == 0) nesNoise(m.tmp, t0, 0.3f, 50000, false, 0.3f);
            if (B.idx % 4 == 3) { sn(t0 + 7 * e, 0.22f); sn(t0 + 7.5 * e, 0.26f); }
        }
    }
    m.send(1.0f, 0);

    std::vector<Note> bass, bells, bells2, lead, stabs, dings;
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        const int R = B.ch.root;
        // triangle: driving 8ths, octave bounce
        if (!(B.sec == B_INTRO && B.idx < 1))
            for (int k = 0; k < 8; ++k) {
                if (B.sec == B_C && B.idx == 3 && k >= 6) break;
                int p = R + ((B.sec == B_B || B.sec == B_C) && k % 2 ? 12 : 0);
                if (B.sec == B_A && k == 7) p = approach(R, nextRoot(s, b));
                bass.push_back({s.t(b, k), e * 0.85, (float)p});
            }
        // the school bell: 32nd-note trill between the chord's top two tones, rings for two beats
        const int hi = B.ch.v.back() + 12, lo = B.ch.v[B.ch.v.size() - 2] + 12;
        auto ring = [&](std::vector<Note>& dst, double from, int count, float vel, int shift) {
            for (int k = 0; k < count; ++k)
                dst.push_back({from + k * e / 4, e / 4 * 0.9, (float)((k % 2 ? lo : hi) + shift), vel * (1.0f - 0.4f * k / count)});
        };
        // "ding-ding": the class-change bell, two struck strokes on the chord's top tone
        auto ding = [&](int step, float vel) {
            dings.push_back({s.t(b, step), e * 1.6, (float)(B.ch.v.back() + 24), vel});
            dings.push_back({s.t(b, step + 1), e * 2.6, (float)(B.ch.v.back() + 24), vel * 0.9f});
        };
        if (B.sec == B_INTRO && B.idx < 2) ding(0, 1.0f);
        if (B.sec == B_A && B.idx % 4 == 2) ding(4, 0.8f);
        if (B.sec == B_B && B.idx % 2 == 0) ding(0, 0.55f);
        switch (B.sec) {
        case B_INTRO: if (B.idx >= 2) ring(bells, s.t(b), 16, 0.8f, 0); break;
        case B_A:
            ring(bells, s.t(b), 16, 1.0f, 0);
            if (B.idx % 2) ring(bells, s.t(b, 4), 16, 0.7f, 0);
            for (int k = 1; k < 8; k += 2) stabs.push_back({s.t(b, k), e * 0.5, (float)(B.ch.v[k / 2 % B.ch.v.size()] + 12)});
            break;
        case B_B: ring(bells2, s.t(b, 4), 16, 0.6f, 12); break;
        case B_C: ring(bells, s.t(b), B.idx == 3 ? 24 : 32, 0.7f + 0.1f * B.idx, B.idx); break;
        default: break;
        }
    }
    for (const LN& l : lineOf(s, false)) lead.push_back({s.t(l.bar, l.step), l.len * e * 0.9, (float)l.pitch + 12});
    Patch tri; tri.wave = W_NESTRI; tri.a = 0.001f; tri.d = 0; tri.s = 1; tri.r = 0.006f; tri.gain = 0.42f;
    renderNotes(m.tmp, tri, bass, 31);
    m.send(1.0f, 0);
    Patch pb = nesPulse(0.125f, 0.12f, -0.25f); pb.a = 0.001f; pb.d = 0; pb.s = 1; pb.r = 0.004f;
    renderNotes(m.tmp, pb, bells, 32);
    Patch pl = nesPulse(0.25f, 0.15f, -0.25f); pl.vib = 0.25f; pl.vibDelay = 0.18f; pl.vibRate = 6.5f;
    renderNotes(m.tmp, pl, lead, 33);
    Patch ps = nesPulse(0.25f, 0.1f, 0.25f); ps.d = 0.1f; ps.s = 0.3f;
    renderNotes(m.tmp, ps, stabs, 34);
    pb.pan = 0.25f; pb.gain = 0.08f;
    renderNotes(m.tmp, pb, bells2, 35);
    Patch pd = nesPulse(0.125f, 0.11f, 0.1f); pd.a = 0.001f; pd.d = 0.22f; pd.s = 0.0f; pd.r = 0.12f; pd.osc2 = 0.5f; pd.osc2semi = 19; pd.osc2duty = 0.25f;
    renderNotes(m.tmp, pd, dings, 36);
    m.send(1.0f, 0);
}

// ------------------------------------------------------------------ boss: The Last Train (16-bit FM)
// Steam whistle: two pipes a minor third apart (a chime whistle) with breath noise at the pitch;
// slides up into the note, droops at the end.
void steamWhistle(Stereo& o, double t, double dur, float g, float m0, float m1, float pan = 0)
{
    Rng a((uint32_t)(t * 1097) + 67);
    size_t i0 = (size_t)std::llround(t * SR), n = (size_t)(dur * SR);
    float p1 = 0, p2 = 0, gl, gr, f = mtof(m0);
    panGains(pan, gl, gr);
    SVF nf;
    for (size_t i = 0; i < n; ++i) {
        float tt = (float)i / SR, k = (float)i / n;
        if ((i & 15) == 0) {
            float m = m1 + (m0 - m1) * std::exp(-tt / 0.14f);
            if (k > 0.7f) m -= 2.0f * (k - 0.7f) / 0.3f * (k - 0.7f) / 0.3f;
            m += 0.08f * std::sin(TAU * 5.2f * tt);
            f = mtof(m);
            nf.set(f, 6.0f);
        }
        p1 += f / SR; if (p1 >= 1) p1 -= 1;
        p2 += f * 1.18921f / SR; if (p2 >= 1) p2 -= 1;
        float v = std::sin(TAU * p1) + 0.7f * std::sin(TAU * p2) + 0.12f * std::sin(TAU * 2 * p1) + nf.bp(a.next()) * 0.9f;
        float env = std::min(1.0f, tt / 0.05f) * std::min(1.0f, (float)(n - i) / (0.12f * SR));
        o.add(i0 + i, v * env * g * gl, v * env * g * gr);
    }
}

Song trainSong()
{
    Song s(musicspec::BOSS_BPM[2]);
    s.add(B_INTRO, 4, {AM});
    s.add(B_A, 8, {AM, AM, FM, EMAJ});
    s.add(B_B, 8, {AM, FM, CMAJ, E7}, [](int i) { return bossMelody(i % 64); });
    s.add(B_C, 4, {FMAJ7, FMAJ7, E7B9, E7B9});
    return s;
}

void arrangeTrain(const Song& s, Mix& m)
{
    const double e = s.e, sx = e * 0.5 * 0.28;   // 16th swing: the off-16ths are late (chug-a-chug shuffle)
    const int N = (int)s.bars.size();
    auto sw = [&](int bar, int k16) { return s.t(bar) + k16 * e * 0.5 + (k16 % 2 ? sx : 0.0); };
    // ---- kick + the chug (dry-ish)
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        const double t0 = s.t(b);
        auto K = [&](double t, float g = 0.85f) { kick(m.tmp, t, g, 180, 52, 0.025f, 0.24f, 0.5f, 2.4f); m.kicks.push_back(t); };
        // the chug: every 16th on a hat, the 1st and 3rd of each beat also a brushy steam chuff
        int dens = B.sec == B_INTRO ? std::min(B.idx, 2) : 2;   // the train pulls away: chug .. chug-a .. chug-a-chug
        for (int k = 0; k < 16; ++k) {
            int pos = k % 4;
            if (dens == 0 && pos != 0) continue;
            if (dens == 1 && pos % 2) continue;
            float acc = pos == 0 ? 1.0f : pos == 2 ? 0.7f : 0.42f;
            if (B.sec == B_C) acc *= 1.0f + 0.08f * B.idx;
            double t = sw(b, k);
            hat(m.tmp, t, 0.075f * acc, pos == 0 ? 0.045f : 0.02f, pos % 2 ? 0.3f : -0.15f, 6000);
            if (pos % 2 == 0) snare(m.tmp, t, 0.14f * acc, 230, 0.05f, 0.012f, 2600, pos ? 0.25f : -0.25f);
        }
        switch (B.sec) {
        case B_INTRO: if (B.idx >= 2) { K(t0, 0.75f); K(t0 + 4 * e, 0.75f); } break;
        case B_A: K(t0); K(t0 + 3 * e, 0.55f); K(t0 + 4 * e); if (B.idx % 2) K(t0 + 7 * e, 0.6f); break;
        case B_B: for (int k = 0; k < 4; ++k) K(t0 + 2 * k * e); K(t0 + 7 * e, 0.6f); if (B.idx == 0) crash(m.tmp, t0, 0.28f); break;
        case B_C: for (int k = 0; k < 4; ++k) if (!(B.idx == 3 && k == 3)) K(t0 + 2 * k * e, 0.65f + 0.08f * B.idx); break;
        default: break;
        }
        if (B.sec == B_A && B.idx == 0) crash(m.tmp, t0, 0.25f);
    }
    m.send(1.0f, 0.06f);
    m.buildDuck(s.beat * 0.55);

    // ---- backbeat, rolls, toms, hits, the clock, whistles (roomy)
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        const double t0 = s.t(b);
        auto S = [&](double t, float g = 0.5f) { snare(m.tmp, t, g, 205, 0.14f, 0.06f, 1000); };
        // the clock keeps time over the train: tick-tock, doubling to 16ths in the build
        if (B.sec == B_C && B.idx >= 2) for (int k = 0; k < 16; ++k) wood(m.tmp, t0 + k * e / 2, 0.16f + 0.04f * (B.idx - 2), k % 2 ? 1700.0f : 2300.0f, k % 2 ? 0.3f : -0.3f);
        else tickTock(m.tmp, t0, s.beat, 1, B.sec == B_INTRO ? 0.32f : 0.2f);
        switch (B.sec) {
        case B_INTRO:
            if (B.idx == 0) steamWhistle(m.tmp, t0, s.barLen() * 0.9, 0.1f, 76, 81, -0.2f);
            if (B.idx == 3) for (int k = 0; k < 8; ++k) S(t0 + 4 * e + k * e / 2, 0.15f + 0.04f * k);
            break;
        case B_A:
            S(t0 + 2 * e); S(t0 + 6 * e);
            if (B.idx == 0) orchHit(m.tmp, t0, B.ch, 0.22f, 700 + b);
            if (B.idx == 7) { const float th[4] = {196, 165, 131, 110}; for (int k = 0; k < 4; ++k) tom(m.tmp, t0 + 6 * e + k * e / 2, 0.42f, th[k], 0.2f, 0.4f - 0.25f * k); }
            break;
        case B_B:
            S(t0 + 2 * e); S(t0 + 6 * e, 0.55f);
            if (B.idx % 4 == 0) orchHit(m.tmp, t0, B.ch, 0.24f, 800 + b);
            if (B.idx == 3) steamWhistle(m.tmp, t0 + 4 * e, 4 * e, 0.07f, 79, 83, 0.3f);
            if (B.idx == 7) for (int k = 0; k < 4; ++k) S(t0 + 6 * e + k * e / 2, 0.28f + 0.07f * k);
            break;
        case B_C:
            if (B.idx == 0) { riser(m.tmp, t0, s.barLen() * 4 - s.beat, 0.4f); orchHit(m.tmp, t0, B.ch, 0.24f, 900 + b); }
            if (B.idx < 2) { S(t0 + 2 * e); S(t0 + 6 * e); }
            else if (B.idx == 2) { for (int k = 0; k < 8; ++k) S(t0 + k * e, 0.22f + 0.03f * k); steamWhistle(m.tmp, t0, s.barLen() * 1.5, 0.11f, 74, 81, -0.2f); }
            else for (int k = 0; k < 12; ++k) S(t0 + k * e / 2, 0.24f + 0.025f * k);
            break;
        default: break;
        }
    }
    m.send(1.0f, 0.22f, 0.05f);

    // ---- galloping slap bass: 8th + two 16ths per beat (the wheels on the rails)
    std::vector<Note> bass;
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        const int R = B.ch.root, ap = approach(R, nextRoot(s, b));
        auto put = [&](double t, double len, int p, float v = 1) { bass.push_back({t, len * 0.85, (float)p, v}); };
        if (B.sec == B_INTRO) {
            if (B.idx >= 2) for (int j = 0; j < 4; ++j) put(s.t(b, 2 * j), e, R, 0.7f);
            continue;
        }
        if (B.sec == B_C) {
            for (int k = 0; k < 8; ++k) if (!(B.idx == 3 && k >= 6)) put(s.t(b, k), e, R + (k % 2) * 12, k % 2 ? 0.75f : 1.0f);
            continue;
        }
        for (int j = 0; j < 4; ++j) {
            put(sw(b, 4 * j), e, R, 1.0f);
            put(sw(b, 4 * j + 2), e * 0.5, j == 3 ? ap : R + 12, 0.75f);
            put(sw(b, 4 * j + 3), e * 0.5, j == 3 ? ap : R + 7, 0.7f);
        }
    }
    Patch sb; sb.wave = W_FM; sb.fmRatio = 1; sb.fmIndex = 3.8f; sb.fmDecay = 0.05f; sb.fmSus = 0.2f; sb.sub = 0.5f;
    sb.a = 0.002f; sb.d = 0.2f; sb.s = 0.5f; sb.r = 0.04f; sb.gain = 0.31f;
    renderNotes(m.tmp, sb, bass, 51);
    m.send(1.0f, 0.02f, 0, 0.3f);

    // ---- strings pad + e-piano off-beat stabs + swung FM arpeggio
    std::vector<Note> pad, ep, arp, bassDrone;
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        for (int n : B.ch.v) pad.push_back({s.t(b), s.barLen() * 0.98, (float)n, B.sec == B_B ? 1.0f : B.sec == B_INTRO && B.idx < 2 ? 0.6f : 0.8f});
        if (B.sec == B_INTRO && B.idx < 2) bassDrone.push_back({s.t(b), s.barLen() * 0.97, (float)B.ch.root, 0.55f});
        if (B.sec == B_A)
            for (int k = 1; k < 8; k += 2)
                for (size_t q = 0; q < B.ch.v.size(); ++q) ep.push_back({s.t(b, k), e * 0.55, (float)B.ch.v[q], 0.6f, -1, q % 2 ? 0.3f : -0.3f});
        if (B.sec == B_A || (B.sec == B_C && B.idx < 3))
            for (int k = 0; k < 16; ++k) arp.push_back({sw(b, k), e * 0.4, (float)arpNote(B.ch, k, 12), k % 4 == 0 ? 1.0f : 0.65f});
    }
    renderNotes(m.tmp, strings(0.045f), pad, 52);
    Patch dr = strings(0.07f); dr.cutoff = 900; dr.a = 0.2f;   // low rumble while the train pulls out
    renderNotes(m.tmp, dr, bassDrone, 58);
    m.send(1.0f, 0.3f, 0, 0.3f);
    renderNotes(m.tmp, fmPiano(), ep, 53);
    m.send(1.0f, 0.18f, 0.15f, 0.25f);
    Patch mar; mar.wave = W_FM; mar.fmRatio = 4; mar.fmIndex = 1.3f; mar.fmDecay = 0.05f;
    mar.a = 0.002f; mar.d = 0.2f; mar.s = 0.0f; mar.r = 0.08f; mar.gain = 0.085f; mar.pan = 0.35f;
    renderNotes(m.tmp, mar, arp, 54);
    m.send(1.0f, 0.15f, 0.3f, 0.2f);

    // ---- the train horn: a two-tone FM-brass dyad, short blast then a long one that sags (Doppler)
    std::vector<Note> horn;
    auto call = [&](int bar, double step, float vel) {
        const int lo = 57, hi = 63;   // A3 + Eb4: the uneasy tritone of a diesel horn
        for (int p : {lo, hi, lo + 12}) {
            float v = p == lo + 12 ? vel * 0.35f : vel;
            horn.push_back({s.t(bar, step), e * 1.4, (float)p, v});
            horn.push_back({s.t(bar, step + 2), e * 4.5, (float)p - 1.0f, v, (float)p});   // glides down a semitone
        }
    };
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        if (B.sec == B_INTRO && B.idx == 1) call(b, 0, 1.0f);
        if (B.sec == B_A && B.idx % 4 == 0) call(b, 0, 0.9f);
        if (B.sec == B_C && B.idx == 0) call(b, 0, 1.0f);
        if (B.sec == B_C && B.idx == 3) call(b, 0, 0.8f);
    }
    Patch hp = fmBrass(); hp.fmIndex = 3.4f; hp.a = 0.04f; hp.vib = 0.03f; hp.glide = 1.0f; hp.gain = 0.09f; hp.r = 0.18f;
    renderNotes(m.tmp, hp, horn, 55);
    m.send(1.0f, 0.3f, 0.12f, 0.2f);

    // ---- the melody, dramatic: FM brass lead with strings an octave up behind it
    std::vector<Note> lead, hiStr;
    for (const LN& l : lineOf(s, false)) {
        lead.push_back({s.t(l.bar, l.step), l.len * e * 0.92, (float)l.pitch, 1.0f});
        hiStr.push_back({s.t(l.bar, l.step), l.len * e * 0.98, (float)l.pitch + 12, 0.8f});
    }
    linkGlides(lead, e * 0.3);
    Patch br = fmBrass(); br.gain = 0.16f;
    renderNotes(m.tmp, br, lead, 56);
    m.send(1.0f, 0.24f, 0.3f);
    Patch hs = strings(0.05f); hs.a = 0.06f; hs.cutoff = 4200;
    renderNotes(m.tmp, hs, hiStr, 57);
    m.send(1.0f, 0.35f, 0.15f, 0.2f);
}

// ------------------------------------------------------------------ boss: The Hourglass (modern)
Song hourglassSong()
{
    Song s(musicspec::BOSS_BPM[3]);
    s.add(B_INTRO, 4, {AM});
    s.add(B_A, 8, {AM, BBMAJ7, CMAJ, E7}, [](int i) { return bossMelody(i % 64); });
    s.add(B_B, 8, {AM, BBMAJ7, CMAJ, E7}, [](int i) { return bossMelody(i % 64); });
    s.add(B_C, 4, {BBMAJ7, BBMAJ7, E7B9, E7B9});
    return s;
}

void arrangeHourglass(const Song& s, Mix& m)
{
    const double e = s.e;
    const int N = (int)s.bars.size();
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        const double t0 = s.t(b);
        auto K = [&](double t, float g = 1.0f) { kick(m.tmp, t, g, 150, 42, 0.035f, 0.45f); m.kicks.push_back(t); };
        switch (B.sec) {
        case B_INTRO: if (B.idx == 0 || B.idx == 2) K(t0, 0.8f); break;
        case B_A:
            K(t0); K(t0 + 3 * e, 0.7f);
            for (int k = 0; k < 8; ++k) hat(m.tmp, t0 + k * e, k % 2 ? 0.1f : 0.05f, 0.03f, 0.15f);
            break;
        case B_B:
            for (int k = 0; k < 4; ++k) K(t0 + 2 * k * e);
            for (int k = 0; k < 16; ++k) hat(m.tmp, t0 + k * e / 2, k % 2 ? 0.1f : 0.05f, 0.028f, 0.15f);
            for (int k = 0; k < 4; ++k) hat(m.tmp, t0 + (2 * k + 1) * e, 0.12f, 0.1f, -0.2f, 6000);
            break;
        case B_C: for (int k = 0; k < 4; ++k) if (!(B.idx == 3 && k == 3)) K(t0 + 2 * k * e, 0.6f + 0.1f * B.idx); break;
        default: break;
        }
    }
    m.send(1.0f, 0.05f);
    m.buildDuck(s.beat * 0.8);
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        const double t0 = s.t(b);
        // the clock never stops: tick-tock everywhere, doubling to 16ths in the build
        if (B.sec == B_C) for (int k = 0; k < 16; ++k) wood(m.tmp, t0 + k * e / 2, 0.18f + 0.03f * B.idx, k % 2 ? 1700.0f : 2300.0f, k % 2 ? 0.3f : -0.3f);
        else tickTock(m.tmp, t0, s.beat, 1, B.sec == B_INTRO ? 0.35f : 0.22f);
        switch (B.sec) {
        case B_INTRO:
            if (B.idx == 0 || B.idx == 2) orchHit(m.tmp, t0, B.ch, 0.3f, 200 + b);
            if (B.idx == 3) for (int k = 0; k < 8; ++k) tom(m.tmp, t0 + 4 * e + k * e / 2, 0.25f + 0.05f * k, k % 2 ? 73.0f : 82.0f, 0.3f, k % 2 ? 0.3f : -0.3f);
            break;
        case B_A:
            snare(m.tmp, t0 + 4 * e, 0.55f, 170, 0.22f, 0.1f, 500); clap(m.tmp, t0 + 4 * e, 0.35f);
            tom(m.tmp, t0 + 7 * e, 0.35f, 98, 0.25f, 0.3f);
            orchHit(m.tmp, t0, B.ch, B.idx % 4 == 0 ? 0.28f : 0.16f, 300 + b);
            break;
        case B_B:
            clap(m.tmp, t0 + 2 * e, 0.55f); clap(m.tmp, t0 + 6 * e, 0.55f);
            if (B.idx == 0) { crash(m.tmp, t0, 0.32f, 1.5f); impact(m.tmp, t0, 0.55f); }
            if (B.idx % 4 == 0) orchHit(m.tmp, t0, B.ch, 0.28f, 400 + b);
            if (B.idx == 7) { const float th[4] = {147, 123, 98, 82}; for (int k = 0; k < 4; ++k) tom(m.tmp, t0 + 6 * e + k * e / 2, 0.5f, th[k]); }
            break;
        case B_C:
            if (B.idx == 0) riser(m.tmp, t0, s.barLen() * 4 - s.beat, 0.5f);
            if (B.idx == 0) orchHit(m.tmp, t0, B.ch, 0.3f, 500 + b);
            if (B.idx >= 2) for (int k = 0; k < (B.idx == 3 ? 12 : 16); ++k) snare(m.tmp, t0 + k * e / 2, 0.15f + 0.02f * k + 0.1f * (B.idx - 2));
            if (B.idx == 2) orchHit(m.tmp, t0, B.ch, 0.3f, 600 + b);
            break;
        default: break;
        }
    }
    // sand runs through the whole fight, rising in the build
    sand(m.tmp, 0, s.barLen() * 20, 0.09f, 220);
    sand(m.tmp, s.t(20), s.barLen() * 4, 0.12f, 300, 3.0f);
    m.send(1.0f, 0.3f, 0.05f);

    // ---- bass: sub drone / reese, string ostinato
    std::vector<Note> sub, reese, ost;
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        const int R = B.ch.root - 12;
        if (B.sec == B_B) {
            for (int k = 0; k < 4; ++k) { sub.push_back({s.t(b, 2 * k), 2 * e * 0.9, (float)R}); reese.push_back({s.t(b, 2 * k), 2 * e * 0.9, (float)R + 12}); }
        } else if (!(B.sec == B_C && B.idx == 3)) {
            sub.push_back({s.t(b), s.barLen() * 0.97, (float)R, 0.8f});
            if (B.sec != B_INTRO) reese.push_back({s.t(b), s.barLen() * 0.97, (float)R + 12, 0.8f});
        }
        if (B.sec != B_INTRO || B.idx >= 2) {   // staccato string ostinato in 8ths
            const int pat[8] = {0, 0, 12, 0, 7, 0, 12, 10};
            for (int k = 0; k < 8; ++k) {
                if (B.sec == B_C && B.idx == 3 && k >= 6) break;
                ost.push_back({s.t(b, k), e * 0.55, (float)(B.ch.root + pat[k]), k % 2 ? 0.7f : 1.0f});
            }
        }
    }
    Patch ps; ps.wave = W_SINE; ps.a = 0.005f; ps.d = 0.2f; ps.s = 0.9f; ps.r = 0.08f; ps.gain = 0.45f;
    renderNotes(m.tmp, ps, sub, 41);
    m.send(1.0f, 0, 0, 0.8f);
    Patch pr; pr.wave = W_SAW; pr.unison = 2; pr.detune = 0.32f; pr.width = 0.6f; pr.filt = true; pr.cutoff = 420; pr.q = 1.5f;
    pr.fenv = 1.2f; pr.fdecay = 0.2f; pr.a = 0.01f; pr.d = 0.4f; pr.s = 0.8f; pr.r = 0.1f; pr.gain = 0.13f;
    renderNotes(m.tmp, pr, reese, 42);
    m.send(1.0f, 0.02f, 0, 0.8f);
    Patch po; po.wave = W_SAW; po.unison = 3; po.detune = 0.15f; po.width = 0.6f; po.filt = true; po.cutoff = 1100; po.fenv = 1.5f;
    po.fdecay = 0.06f; po.a = 0.003f; po.d = 0.12f; po.s = 0.2f; po.r = 0.06f; po.gain = 0.12f;
    renderNotes(m.tmp, po, ost, 43);
    m.send(1.0f, 0.2f, 0, 0.5f);

    // ---- choir pad
    std::vector<Note> choir, saws;
    for (int b = 0; b < N; ++b) {
        const Bar& B = s.bars[b];
        for (int n : B.ch.v) {
            choir.push_back({s.t(b), s.barLen() * 0.99, (float)n, B.sec == B_C ? 0.7f + 0.1f * B.idx : 0.8f});
            if (B.sec == B_B) saws.push_back({s.t(b), s.barLen() * 0.97, (float)n + 12});
        }
    }
    Patch ch; ch.wave = W_SAW; ch.unison = 5; ch.detune = 0.22f; ch.width = 0.8f; ch.a = 0.5f; ch.d = 1.0f; ch.s = 1.0f; ch.r = 0.7f;
    ch.vib = 0.12f; ch.vibRate = 4.8f; ch.vibDelay = 0.1f; ch.gain = 0.09f;
    renderNotes(m.tmp, ch, choir, 44);
    formant(m.tmp);
    m.send(1.0f, 0.55f, 0, 0.35f);
    renderNotes(m.tmp, supersaw(0.04f, 3500, 0.02f), saws, 45);
    m.send(1.0f, 0.3f, 0, 0.7f);

    // ---- the melody, re-harmonised: brass-like saw lead in A, pluck an octave up in B
    std::vector<Note> leadA, leadB;
    for (const LN& l : lineOf(s, false)) {
        const Bar& B = s.bars[l.bar];
        Note n{s.t(l.bar, l.step), l.len * e * 0.92, (float)l.pitch};
        if (B.sec == B_A) leadA.push_back(n);
        else { n.pitch += 12; leadB.push_back(n); n.pitch -= 12; n.vel = 0.5f; leadA.push_back(n); }
    }
    linkGlides(leadA, e * 0.25); linkGlides(leadB, e * 0.25);
    Patch br; br.wave = W_SAW; br.unison = 3; br.detune = 0.12f; br.width = 0.4f; br.filt = true; br.cutoff = 900; br.fenv = 1.6f;
    br.fdecay = 0.25f; br.q = 0.9f; br.a = 0.03f; br.d = 0.4f; br.s = 0.7f; br.r = 0.15f; br.gain = 0.13f;
    br.vib = 0.18f; br.vibDelay = 0.25f; br.glide = 0.05f;
    renderNotes(m.tmp, br, leadA, 46);
    m.send(1.0f, 0.3f, 0.2f, 0.25f);
    renderNotes(m.tmp, pluck(0.18f), leadB, 47);
    m.send(1.0f, 0.3f, 0.3f, 0.25f);
}

// ------------------------------------------------------------------ driver
bool renderTrack(const Song& s, const std::function<void(const Song&, Mix&)>& arrange, const FxSettings& fx, const std::string& base)
{
    const size_t loop = (size_t)std::llround(s.length() * SR);
    Mix m(loop + (size_t)(TAIL * SR));
    arrange(s, m);
    std::vector<int16_t> pcm = master(m, loop, fx);
    bool ok = exportMusic(pcm, fx.mono, base);
    std::printf("  %s: %.0f BPM, %zu bars, %.2f s %s\n", base.c_str(), s.bpm, s.bars.size(), (double)loop / SR, ok ? "" : "(FAILED)");
    return ok;
}

}  // namespace

int generateMusic(const std::string& dir)
{
    int n = 0;
    for (int era = 0; era < 4; ++era) {
        std::string d = dir + "/audio/era" + std::to_string(era) + "/";
        Song s = mainSong(era);
        if ((int)s.bars.size() != musicspec::BARS[era]) std::printf("  WARNING: era %d song has %zu bars, MusicSpec says %d\n", era, s.bars.size(), musicspec::BARS[era]);
        FxSettings fx;
        std::function<void(const Song&, Mix&)> arr;
        switch (era) {
        case 0: arr = arrangeEra0; fx.mono = true; fx.ceilingDb = -5.0f; fx.drive = 1.0f; break;
        case 1: arr = arrangeEra1; fx.masterLp = 15000; fx.drive = 1.2f; break;
        case 2: arr = arrangeEra2; fx.room = 0.7f; fx.damp = 0.5f; fx.wet = 0.8f; fx.delay = s.e * 3; fx.delayFb = 0.38f; fx.delayLp = 3000; fx.masterLp = 13000; break;
        default: arr = arrangeEra3; fx.room = 0.88f; fx.damp = 0.3f; fx.wet = 1.0f; fx.delay = s.e * 3; fx.delayFb = 0.4f; fx.delayLp = 5000; fx.drive = 1.8f; break;
        }
        n += renderTrack(s, arr, fx, d + "music") ? 1 : 0;
        std::remove((d + "music_boss.wav").c_str());
        if (!musicspec::HAS_BOSS[era]) { std::remove((d + "music_boss.qoa").c_str()); continue; }
        if (era == 1) {
            FxSettings bf; bf.masterLp = 15000; bf.drive = 1.3f;
            n += renderTrack(schoolBellSong(), arrangeSchoolBell, bf, d + "music_boss") ? 1 : 0;
        } else if (era == 2) {
            Song ts = trainSong();
            FxSettings bf; bf.room = 0.72f; bf.damp = 0.5f; bf.wet = 0.8f; bf.delay = ts.e * 3; bf.delayFb = 0.34f; bf.delayLp = 3000;
            bf.masterLp = 13000; bf.drive = 1.6f;
            n += renderTrack(ts, arrangeTrain, bf, d + "music_boss") ? 1 : 0;
        } else {
            Song hs = hourglassSong();
            FxSettings bf; bf.room = 0.92f; bf.damp = 0.35f; bf.delay = hs.e * 3; bf.delayFb = 0.35f; bf.delayLp = 4500; bf.drive = 1.8f;
            n += renderTrack(hs, arrangeHourglass, bf, d + "music_boss") ? 1 : 0;
        }
    }
    return n;
}
