// Era4Sound.cpp: synthesises era 4 ("modern day") audio under <assets>/audio/era4/.
//   Sound effects: clean, soft, rounded sine and FM tones with short envelopes.
//   Music: a warm modern indie loop. FM electric piano chords, a soft pad,
//   a plucked melody with delay, sine bass, light brushed drums, all through a
//   small reverb. It plays the same melody as eras 0-3 so the memory feels
//   continuous, reharmonised with sevenths and ninths (Am9 Fmaj9 Cadd9 G6).
// Same format as the other eras: 22050 Hz, mono, 16-bit. The music loop is
// rendered with extra tail and folded back onto its start, so it loops seamlessly.
#include "Gen.h"
#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

constexpr int SR = 22050;
constexpr float PI2 = 6.2831853f;

uint32_t g_seed = 424242;
float noise()
{
    g_seed ^= g_seed << 13; g_seed ^= g_seed >> 17; g_seed ^= g_seed << 5;
    return (g_seed & 0xffff) / 32767.5f - 1.0f;
}

float midiHz(float m) { return 440.0f * std::pow(2.0f, (m - 69) / 12.0f); }

struct Buf {
    std::vector<float> s;
    explicit Buf(float seconds) : s((size_t)(seconds * SR), 0.0f) {}
    void add(size_t i, float v) { if (i < s.size()) s[i] += v; }
    void mix(const Buf& o, float g = 1.0f) { for (size_t i = 0; i < o.s.size() && i < s.size(); ++i) s[i] += o.s[i] * g; }
};

// ------------------------------------------------------------------ voices

// A two-operator FM voice. Pitch glides f0 -> f1 (exponential). The modulator
// runs at ratio * f; its index starts at idx0 and decays at idxDecay per second.
// Amplitude: linear attack, exponential decay, linear release at the end.
struct Fm {
    float f0 = 440, f1 = 440, ratio = 1, idx0 = 0, idxFloor = 0, idxDecay = 8;
    float vol = 0.5f, attack = 0.004f, decay = 4.0f, release = 0.05f;
    float vib = 0, trem = 0;
};

void fm(Buf& b, float start, float dur, const Fm& p)
{
    size_t i0 = (size_t)(start * SR), n = (size_t)(dur * SR);
    double pc = 0, pm = 0;
    for (size_t i = 0; i < n; ++i) {
        float t = (float)i / SR, u = (float)i / n;
        float f = p.f0 * std::pow(p.f1 / p.f0, u);
        if (p.vib > 0) f *= 1.0f + p.vib * std::sin(t * PI2 * 5.5f);
        pc += f / SR;
        pm += f * p.ratio / SR;
        float idx = p.idxFloor + p.idx0 * std::exp(-t * p.idxDecay);
        float y = std::sin((float)(pc - std::floor(pc)) * PI2 + idx * std::sin((float)(pm - std::floor(pm)) * PI2));
        float env = std::min(1.0f, t / p.attack) * std::exp(-t * p.decay) * std::min(1.0f, (dur - t) / p.release);
        if (p.trem > 0) env *= 1.0f - p.trem * (0.5f + 0.5f * std::sin(t * PI2 * 4.5f));
        b.add(i0 + i, y * p.vol * std::max(0.0f, env));
    }
}

// Plain sine with a pitch glide and soft envelope.
void sine(Buf& b, float start, float dur, float f0, float f1, float vol, float attack = 0.004f,
          float decay = 3.0f, float release = 0.05f, float vib = 0.0f)
{
    Fm p;
    p.f0 = f0; p.f1 = f1; p.vol = vol; p.attack = attack; p.decay = decay; p.release = release; p.vib = vib;
    fm(b, start, dur, p);
}

// Rhodes-like electric piano: ratio 1 body with a fast-decaying index, plus a
// quiet high "tine" partner, with a gentle suitcase tremolo.
void epiano(Buf& b, float start, float dur, float hz, float vol)
{
    Fm body;
    body.f0 = body.f1 = hz; body.ratio = 1; body.idx0 = 1.6f; body.idxFloor = 0.25f; body.idxDecay = 5;
    body.vol = vol; body.attack = 0.003f; body.decay = 1.1f; body.release = 0.18f; body.trem = 0.18f;
    fm(b, start, dur, body);
    Fm tine;
    tine.f0 = tine.f1 = hz; tine.ratio = 14; tine.idx0 = 0.6f; tine.idxDecay = 30;
    tine.vol = vol * 0.12f; tine.attack = 0.001f; tine.decay = 14; tine.release = 0.02f;
    fm(b, start, std::min(dur, 0.25f), tine);
}

// A soft bell: inharmonic ratio, quick index decay, long-ish ring.
void bell(Buf& b, float start, float dur, float hz, float vol, float decay = 3.5f)
{
    Fm p;
    p.f0 = p.f1 = hz; p.ratio = 3.5f; p.idx0 = 1.2f; p.idxDecay = 9; p.vol = vol;
    p.attack = 0.002f; p.decay = decay; p.release = 0.08f;
    fm(b, start, dur, p);
    sine(b, start, dur, hz * 2, hz * 2, vol * 0.15f, 0.002f, decay * 2.0f, 0.05f);
}

// Karplus-Strong plucked string, softened: the excitation is low-passed noise
// plus a sine, and a quiet sine body rides under it.
void pluck(Buf& b, float start, float dur, float hz, float vol)
{
    int len = std::max(2, (int)std::lround(SR / hz));
    std::vector<float> line(len);
    float lp = 0;
    for (int k = 0; k < len; ++k) {
        lp += 0.35f * (noise() - lp);
        line[k] = lp * 1.4f + 0.5f * std::sin(PI2 * k / len);
    }
    size_t i0 = (size_t)(start * SR), n = (size_t)(dur * SR);
    int p = 0;
    for (size_t i = 0; i < n; ++i) {
        float t = (float)i / SR;
        int q = (p + 1) % len;
        float y = line[p];
        line[p] = 0.4985f * (line[p] + line[q]);
        p = q;
        float env = std::min(1.0f, t / 0.002f) * std::min(1.0f, (dur - t) / 0.08f);
        b.add(i0 + i, y * vol * std::max(0.0f, env));
    }
    sine(b, start, dur, hz, hz, vol * 0.35f, 0.003f, 3.0f, 0.08f);
}

// ------------------------------------------------------------------ filters and effects

void lowpass(std::vector<float>& s, float cutoff)
{
    float a = 1.0f - std::exp(-PI2 * cutoff / SR), y = 0;
    for (float& x : s) { y += a * (x - y); x = y; }
}

void highpass(std::vector<float>& s, float cutoff)
{
    float a = 1.0f - std::exp(-PI2 * cutoff / SR), y = 0;
    for (float& x : s) { y += a * (x - y); x -= y; }
}

// Filtered noise with a band-pass centre gliding c0 -> c1 (state-variable filter).
void noiseSweep(Buf& b, float start, float dur, float c0, float c1, float q, float vol,
                float attack = 0.01f, float release = 0.05f)
{
    size_t i0 = (size_t)(start * SR), n = (size_t)(dur * SR);
    float low = 0, band = 0;
    for (size_t i = 0; i < n; ++i) {
        float t = (float)i / SR, u = (float)i / n;
        float c = c0 * std::pow(c1 / c0, u);
        float f = 2.0f * std::sin(3.14159265f * std::min(c, SR * 0.2f) / SR);
        float high = noise() - low - q * band;
        band += f * high;
        low += f * band;
        float env = std::min(1.0f, t / attack) * std::min(1.0f, (dur - t) / release);
        b.add(i0 + i, band * vol * std::max(0.0f, env));
    }
}

// Feedback delay with a darkening filter in the loop. Returns only the echoes.
Buf delayWet(const Buf& in, float delaySec, float fb, float tone)
{
    Buf out((float)in.s.size() / SR);
    size_t d = (size_t)(delaySec * SR);
    float a = 1.0f - std::exp(-PI2 * tone / SR), y = 0;
    for (size_t i = d; i < out.s.size(); ++i) {
        float x = in.s[i - d] + out.s[i - d] * fb;
        y += a * (x - y);
        out.s[i] = y;
    }
    return out;
}

// Small Freeverb-style room: six damped combs in parallel, two all-passes in series.
// Returns only the wet signal.
Buf reverbWet(const Buf& in, float size = 0.82f, float damp = 0.35f)
{
    const int combs[6] = {1116, 1188, 1277, 1356, 1422, 1491};
    const int aps[2] = {556, 225};
    Buf out((float)in.s.size() / SR);
    for (int d : combs) {
        std::vector<float> line(d, 0.0f);
        float store = 0;
        size_t p = 0;
        for (size_t i = 0; i < in.s.size(); ++i) {
            float y = line[p];
            store = y * (1 - damp) + store * damp;
            line[p] = in.s[i] * 0.03f + store * size;
            out.s[i] += y;
            p = (p + 1) % d;
        }
    }
    for (int d : aps) {
        std::vector<float> line(d, 0.0f);
        size_t p = 0;
        for (size_t i = 0; i < out.s.size(); ++i) {
            float bufout = line[p];
            float x = out.s[i];
            line[p] = x + bufout * 0.5f;
            out.s[i] = bufout - x;
            p = (p + 1) % d;
        }
    }
    return out;
}

void addReverb(Buf& b, float mix, float size = 0.82f)
{
    Buf w = reverbWet(b, size);
    b.mix(w, mix);
}

// Scales the buffer so its peak is exactly `peak` (never above 0.9) and writes it.
bool save(Buf& b, const std::string& path, float peak)
{
    float p = 0.0001f;
    for (float v : b.s) p = std::max(p, std::fabs(v));
    float k = std::min(peak, 0.9f) / p;
    std::vector<int16_t> pcm(b.s.size());
    for (size_t i = 0; i < b.s.size(); ++i)
        pcm[i] = (int16_t)std::lround(std::max(-1.0f, std::min(1.0f, b.s[i] * k)) * 32767);
    ::Wave w{};
    w.frameCount = (unsigned int)pcm.size();
    w.sampleRate = SR;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = pcm.data();
    return ExportWave(w, path.c_str());
}

// ------------------------------------------------------------------ sound effects

void soft(Buf& b, float start, float dur, float f0, float f1, float vol, float decay = 6.0f)
{
    Fm p;
    p.f0 = f0; p.f1 = f1; p.ratio = 2; p.idx0 = 0.5f; p.idxDecay = 20; p.vol = vol;
    p.attack = 0.004f; p.decay = decay; p.release = 0.04f;
    fm(b, start, dur, p);
}

void thump(Buf& b, float start, float dur, float f0, float f1, float vol)
{
    sine(b, start, dur, f0, f1, vol, 0.002f, 7.0f, 0.05f);
    Buf puff(dur + 0.05f);
    for (size_t i = 0; i < (size_t)(0.05f * SR); ++i) puff.s[i] = noise() * (1.0f - (float)i / (0.05f * SR));
    lowpass(puff.s, 700);
    lowpass(puff.s, 700);
    for (size_t i = 0; i < puff.s.size(); ++i) b.add((size_t)(start * SR) + i, puff.s[i] * vol * 0.9f);
}

void sfx(Buf& b, const std::string& name)
{
    float rev = 0.22f;
    auto arpBell = [&](std::initializer_list<int> notes, float step, float base, float vol, float ring) {
        float t = 0;
        for (int n : notes) { bell(b, t, ring, midiHz(base + n), vol, 4.0f); t += step; }
    };
    auto arpEp = [&](std::initializer_list<int> notes, float step, float base, float vol, float ring) {
        float t = 0;
        for (int n : notes) { epiano(b, t, ring, midiHz(base + n), vol); t += step; }
    };
    if (name == "jump") {
        soft(b, 0, 0.16f, 330, 700, 0.6f, 9);
        sine(b, 0.01f, 0.12f, 660, 1400, 0.12f, 0.004f, 14);
    } else if (name == "double_jump") {
        soft(b, 0, 0.11f, 523, 880, 0.5f, 10);
        soft(b, 0.07f, 0.16f, 784, 1320, 0.5f, 9);
        bell(b, 0.09f, 0.3f, 1568, 0.1f, 9);
    } else if (name == "wall_jump") {
        Fm wood;
        wood.f0 = wood.f1 = 300; wood.ratio = 3.5f; wood.idx0 = 1.0f; wood.idxDecay = 40; wood.vol = 0.35f;
        wood.decay = 30; wood.release = 0.02f;
        fm(b, 0, 0.08f, wood);
        soft(b, 0.02f, 0.13f, 440, 760, 0.5f, 10);
    } else if (name == "stomp") {
        thump(b, 0, 0.18f, 240, 60, 0.7f);
        pluck(b, 0, 0.25f, 196, 0.35f);
    } else if (name == "hit") {
        Fm p;
        p.f0 = 360; p.f1 = 170; p.ratio = 1.41f; p.idx0 = 1.5f; p.idxDecay = 14; p.vol = 0.55f;
        p.decay = 9; p.release = 0.05f;
        fm(b, 0, 0.24f, p);
        thump(b, 0, 0.12f, 150, 70, 0.35f);
    } else if (name == "fade") {
        sine(b, 0, 0.8f, 660, 110, 0.45f, 0.01f, 2.5f, 0.25f, 0.015f);
        sine(b, 0, 0.8f, 666, 111, 0.3f, 0.01f, 2.5f, 0.25f, 0.015f);
        Buf hp(0.8f);
        noiseSweep(hp, 0, 0.6f, 2400, 300, 0.7f, 0.04f, 0.05f, 0.3f);
        b.mix(hp);
        rev = 0.35f;
    } else if (name == "revive") {
        arpEp({0, 4, 7, 12, 16}, 0.07f, 72, 0.4f, 0.55f);
        bell(b, 0.35f, 0.6f, midiHz(91), 0.12f, 5);
        rev = 0.35f;
    } else if (name == "fragment") {
        bell(b, 0, 0.5f, midiHz(88), 0.45f, 6);   // E6
        bell(b, 0.06f, 0.6f, midiHz(93), 0.45f, 5);   // A6
        sine(b, 0.1f, 0.4f, midiHz(100), midiHz(100), 0.06f, 0.01f, 8);
        Buf w = delayWet(b, 0.11f, 0.3f, 4000);
        b.mix(w, 0.35f);
        rev = 0.3f;
    } else if (name == "powerup") {
        float t = 0;
        for (int n : {0, 7, 12, 4, 11, 16, 7, 19}) { pluck(b, t, 0.3f, midiHz(67.0f + n), 0.35f); t += 0.045f; }
        bell(b, 0.36f, 0.6f, midiHz(86), 0.18f, 4.5f);
        rev = 0.3f;
    } else if (name == "unlock") {
        arpEp({0, 4, 7, 12}, 0.1f, 60, 0.35f, 0.6f);
        for (int n : {72, 76, 79, 84}) epiano(b, 0.4f, 0.6f, midiHz((float)n), 0.18f);
        bell(b, 0.4f, 0.6f, midiHz(96), 0.12f, 4);
        rev = 0.3f;
    } else if (name == "dash") {
        noiseSweep(b, 0, 0.2f, 500, 2600, 0.6f, 0.6f, 0.03f, 0.12f);
        sine(b, 0, 0.14f, 600, 300, 0.15f, 0.01f, 10);
    } else if (name == "pound") {
        thump(b, 0, 0.36f, 130, 36, 0.8f);
        sine(b, 0, 0.3f, 60, 40, 0.4f, 0.004f, 6);
        noiseSweep(b, 0, 0.18f, 900, 250, 0.9f, 0.3f, 0.002f, 0.12f);
    } else if (name == "crack") {
        for (int k = 0; k < 5; ++k) noiseSweep(b, k * 0.025f + (k % 2) * 0.008f, 0.04f, 1600, 1100, 0.5f, 0.5f - k * 0.07f, 0.001f, 0.03f);
        Fm wood;
        wood.f0 = 110; wood.f1 = 80; wood.ratio = 2.3f; wood.idx0 = 1.2f; wood.idxDecay = 25; wood.vol = 0.5f;
        wood.decay = 14; wood.release = 0.04f;
        fm(b, 0, 0.22f, wood);
    } else if (name == "attack") {
        noiseSweep(b, 0, 0.12f, 2200, 800, 0.6f, 0.45f, 0.01f, 0.07f);
        soft(b, 0, 0.09f, 900, 520, 0.22f, 14);
    } else if (name == "slow") {
        sine(b, 0, 0.75f, 784, 196, 0.4f, 0.01f, 1.8f, 0.3f, 0.02f);
        sine(b, 0, 0.75f, 1175, 294, 0.22f, 0.01f, 1.8f, 0.3f, 0.02f);
        rev = 0.35f;
    } else if (name == "boss_hit") {
        Fm p;
        p.f0 = 160; p.f1 = 70; p.ratio = 1; p.idx0 = 2.5f; p.idxDecay = 10; p.vol = 0.6f;
        p.decay = 7; p.release = 0.06f;
        fm(b, 0, 0.3f, p);
        thump(b, 0, 0.2f, 110, 50, 0.5f);
    } else if (name == "boss_appear") {
        for (float hz : {55.0f, 58.27f, 82.41f}) {
            Fm p;
            p.f0 = hz; p.f1 = hz * 1.06f; p.ratio = 1; p.idx0 = -1.2f; p.idxFloor = 1.4f; p.idxDecay = 1.5f;
            p.vol = 0.3f; p.attack = 0.5f; p.decay = 0.2f; p.release = 0.45f; p.vib = 0.01f;
            fm(b, 0, 1.3f, p);
        }
        lowpass(b.s, 1400);
        rev = 0.3f;
    } else if (name == "memory_lost") {
        arpEp({12, 7, 3, 0, -5}, 0.14f, 64, 0.4f, 0.5f);
        rev = 0.4f;
    } else if (name == "select") {
        Fm p;
        p.f0 = p.f1 = 880; p.ratio = 4; p.idx0 = 0.7f; p.idxDecay = 40; p.vol = 0.4f; p.decay = 25; p.release = 0.02f;
        fm(b, 0, 0.09f, p);
        rev = 0.12f;
    } else if (name == "join") {
        arpBell({0, 7, 12}, 0.06f, 72, 0.35f, 0.45f);
    } else if (name == "era_up") {
        for (float det : {-0.006f, 0.0f, 0.006f}) sine(b, 0, 1.2f, 220 * (1 + det), 880 * (1 + det), 0.15f, 0.3f, 0.3f, 0.3f);
        noiseSweep(b, 0, 1.1f, 400, 5000, 0.6f, 0.08f, 0.6f, 0.2f);
        for (int n : {48, 60, 64, 67, 71, 74}) epiano(b, 1.0f, 1.2f, midiHz((float)n), 0.16f);   // Cmaj9
        bell(b, 1.0f, 1.2f, midiHz(88), 0.12f, 2.5f);
        bell(b, 1.12f, 1.1f, midiHz(91), 0.1f, 2.5f);
        rev = 0.4f;
    }
    highpass(b.s, 40);
    addReverb(b, rev, 0.78f);
}

const char* SFX_NAMES[] = {"jump", "double_jump", "wall_jump", "stomp", "hit", "fade", "revive", "fragment",
                           "powerup", "unlock", "dash", "pound", "crack", "attack", "slow", "boss_hit",
                           "boss_appear", "memory_lost", "select", "join", "era_up"};
// Peak level per effect, matched to era 3's files so switching era keeps the mix.
const float SFX_PEAK[] = {0.90f, 0.90f, 0.79f, 0.90f, 0.89f, 0.90f, 0.90f, 0.53f,
                          0.90f, 0.90f, 0.61f, 0.90f, 0.83f, 0.56f, 0.90f, 0.90f,
                          0.50f, 0.90f, 0.49f, 0.90f, 0.90f};

// ------------------------------------------------------------------ music
// The shared melody (eight bars of eight eighth-notes, 0 = rest, -1 = hold).
const int MELODY[64] = {
    76, -1, 69, -1, 72, -1, 71, 69,
    72, -1, -1, -1, 69, -1, 65, -1,
    67, -1, 72, -1, 76, -1, 74, 72,
    74, -1, -1, -1, 71, -1, 67, -1,
    76, -1, 81, -1, 79, -1, 76, -1,
    77, -1, 76, -1, 72, -1, 69, -1,
    67, -1, 72, -1, 76, -1, 79, -1,
    74, -1, 71, -1, 67, -1, 0, 0};
const int ROOT[8] = {45, 41, 48, 43, 45, 41, 48, 43};
// Rootless warm voicings: Am9, Fmaj9, Cadd9, G6, Am9, Fmaj9, Cadd9, Gsus-G6.
const int CHORD[8][4] = {{60, 64, 67, 71}, {57, 60, 64, 67}, {55, 60, 62, 64}, {59, 62, 64, 67},
                         {60, 64, 67, 71}, {57, 60, 64, 67}, {55, 60, 62, 64}, {60, 62, 67, 69}};

constexpr float BPM = 88.0f;

void music(Buf& out, float e)
{
    float len = (float)out.s.size() / SR;
    Buf keys(len), lead(len), pad(len), low(len), drums(len);

    // plucked melody, an octave as written, every note left to ring a little
    for (int i = 0; i < 64; ++i) {
        int n = MELODY[i];
        if (n <= 0) continue;
        int hold = 1;
        while (i + hold < 64 && MELODY[i + hold] == -1) ++hold;
        float dur = hold * e + 0.35f;
        pluck(lead, i * e, dur, midiHz((float)n), 0.32f);
    }

    for (int bar = 0; bar < 8; ++bar) {
        float t0 = bar * 8 * e;
        // electric piano: a soft strum on beat 1, lighter pushes on the "and" of 2 and on beat 4
        const float hits[3][2] = {{0, 1.0f}, {3, 0.55f}, {6, 0.7f}};
        for (auto& h : hits) {
            float lenE = h[0] == 0 ? 3.0f : 2.0f;
            for (int k = 0; k < 4; ++k)
                epiano(keys, t0 + h[0] * e + k * 0.012f, lenE * e + 0.15f, midiHz((float)CHORD[bar][k]), 0.11f * h[1]);
        }
        // pad: detuned soft triangles an octave above, crossfading bar to bar
        Buf p(8 * e + 1.6f);
        for (int k = 0; k < 4; ++k)
            for (float det : {-0.07f, 0.07f}) {
                float hz = midiHz(CHORD[bar][k] + 12 + det);
                size_t n = p.s.size();
                double ph = 0;
                for (size_t i = 0; i < n; ++i) {
                    float t = (float)i / SR;
                    ph += hz / SR;
                    float x = (float)(ph - std::floor(ph));
                    float tri = x < 0.5f ? 4 * x - 1 : 3 - 4 * x;
                    float env = std::min(1.0f, t / 0.8f) * std::min(1.0f, (n / (float)SR - t) / 1.2f);
                    p.s[i] += tri * 0.045f * env;
                }
            }
        lowpass(p.s, 1100);
        lowpass(p.s, 1800);
        for (size_t i = 0; i < p.s.size(); ++i) pad.add((size_t)(t0 * SR) + i, p.s[i]);
        // sine bass: half notes, the second one dipping to the fifth on alternate bars
        sine(low, t0, 5 * e, midiHz((float)ROOT[bar]), midiHz((float)ROOT[bar]), 0.42f, 0.01f, 0.5f, 0.12f);
        int second = ROOT[bar] + (bar % 2 ? 7 : 0);
        sine(low, t0 + 6 * e, 2 * e, midiHz((float)second), midiHz((float)second), 0.34f, 0.01f, 0.8f, 0.1f);
        // light drums: soft kick on 1 and the "and" of 3, a rimmy snap on 3, brushed shaker on eighths
        for (int k : {0, 5}) {
            float t = t0 + k * e;
            sine(drums, t, 0.22f, 110, 45, 0.5f, 0.002f, 12, 0.05f);
        }
        {
            float t = t0 + 4 * e;
            noiseSweep(drums, t, 0.12f, 1800, 1500, 0.9f, 0.25f, 0.001f, 0.1f);
            sine(drums, t, 0.06f, 420, 380, 0.12f, 0.001f, 40, 0.03f);
        }
        for (int k = 0; k < 8; ++k)
            noiseSweep(drums, t0 + k * e + 0.004f, 0.07f, 6500, 6000, 0.8f, k % 2 ? 0.09f : 0.05f, 0.012f, 0.05f);
    }

    // effects: dotted-eighth delay on the pluck, reverb on everything melodic
    Buf echoes = delayWet(lead, 1.5f * e, 0.35f, 2500);
    lead.mix(echoes, 0.45f);
    highpass(keys.s, 90);
    Buf bus(len);
    bus.mix(keys, 1.0f);
    bus.mix(lead, 0.9f);
    bus.mix(pad, 0.8f);
    Buf wet = reverbWet(bus, 0.86f, 0.4f);
    Buf dwet = reverbWet(drums, 0.7f, 0.5f);
    lowpass(low.s, 400);
    out.mix(bus, 1.0f);
    out.mix(wet, 0.32f);
    out.mix(low, 1.0f);
    out.mix(drums, 0.8f);
    out.mix(dwet, 0.15f);
    highpass(out.s, 30);
}

}  // namespace

int generateEra4Sounds(const std::string& dir)
{
    std::string d = dir + "/audio/era4/";
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    g_seed = 424242;   // same files every run
    int n = 0;
    for (size_t k = 0; k < sizeof(SFX_NAMES) / sizeof(SFX_NAMES[0]); ++k) {
        std::string name = SFX_NAMES[k];
        Buf b(name == "era_up" ? 2.4f : (name == "boss_appear" ? 1.4f : 1.0f));
        sfx(b, name);
        // trim trailing silence, then a short fade so a cut reverb tail never clicks
        size_t end = b.s.size();
        while (end > 1 && std::fabs(b.s[end - 1]) < 0.0005f) --end;
        b.s.resize(std::max<size_t>(end, 64));
        size_t fade = std::min<size_t>(b.s.size(), (size_t)(0.04f * SR));
        for (size_t i = 0; i < fade; ++i) b.s[b.s.size() - 1 - i] *= (float)i / fade;
        n += save(b, d + "sfx_" + name + ".wav", SFX_PEAK[k]) ? 1 : 0;
    }
    float e = 60.0f / BPM / 2;
    float loop = 64 * e;
    Buf extra(loop + 5.0f);
    music(extra, e);
    Buf m(loop);
    for (size_t i = 0; i < extra.s.size(); ++i) m.s[i % m.s.size()] += extra.s[i];
    n += save(m, d + "music.wav", 0.9f) ? 1 : 0;
    return n;
}
