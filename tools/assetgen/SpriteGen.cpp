// SpriteGen.cpp: draws every sprite for every era from code.
//
// Each sprite is described once with simple shapes (ellipses, rounded rects,
// capsules, polygons) in tile units. The same description is rasterised seven
// times, once per graphics stage, with that stage's resolution and colour rules:
//   stage 0  1-bit:    8 px per tile, two colours plus checker dither
//   stage 1  4-shade: 16 px per tile, four warm greys with ordered dither, outlined
//   stage 2  8-bit:   16 px per tile, snapped to a 16-colour palette, outlined
//   stage 3  16-bit:  32 px per tile, shaded, 4096 colours, outlined
//   stage 4  32-bit:  48 px per tile, lit 4-tone ramps with hue-shifted shadows,
//                     a rim light and coloured (not black) outlines
//   stage 5  2.5D:    64 px per tile, smooth lit shading, anti-aliased
//   stage 6  3D:      64 px textures; characters are modelled in the game instead
// g_style picks the older drawing branch a stage builds on (see art::STYLE).
#include "Gen.h"
#include "raylib.h"
#include "render/ArtSpec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

using namespace art;

namespace {

// ------------------------------------------------------------------ colour
struct Col { float r = 0, g = 0, b = 0, a = 0; };

Col hex(uint32_t h, float a = 1.0f)
{
    return {((h >> 16) & 255) / 255.0f, ((h >> 8) & 255) / 255.0f, (h & 255) / 255.0f, a};
}
Col mixc(Col a, Col b, float t)
{
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
Col mul(Col c, float k) { return {c.r * k, c.g * k, c.b * k, c.a}; }
Col withA(Col c, float a) { c.a = a; return c; }
float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
float lum(Col c) { return 0.299f * c.r + 0.587f * c.g + 0.114f * c.b; }

uint32_t hash3(int x, int y, int z)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float rnd(int x, int y, int z) { return (hash3(x, y, z) & 0xffff) / 65535.0f; }

// Smooth value noise that wraps every `period` units on x.
float vnoise(float x, float y, int period, int seed)
{
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    auto at = [&](int ix, int iy) {
        int wx = ((ix % period) + period) % period;
        return rnd(wx, iy, seed);
    };
    float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    float a = at(xi, yi) + (at(xi + 1, yi) - at(xi, yi)) * sx;
    float b = at(xi, yi + 1) + (at(xi + 1, yi + 1) - at(xi, yi + 1)) * sx;
    return a + (b - a) * sy;
}

// The 16-colour palette used by the 8-bit era (our own; also used by the era 1 shader).
const uint32_t PALETTE16[16] = {
    0x0f0d1a, 0x26305e, 0x6a2c6e, 0x1e8a4c, 0x8c4a2f, 0x5a5468, 0xb8b6c8, 0xfaf3e6,
    0xe8364f, 0xf28a1e, 0xf7d73a, 0x6fdc4a, 0x3c9cf0, 0x8a7fb4, 0xf28cb4, 0xf5c9a0};

const Col ONEBIT_DARK = hex(0x101216);
const Col ONEBIT_LIGHT = hex(0xebebe1);

int g_era = 0;     // the stage being drawn (0..6)
int g_style = 0;   // the older drawing style it builds on: STYLE[g_era]

void setStage(int e) { g_era = e; g_style = STYLE[e]; }

// ------------------------------------------------------------------ canvas
struct Frag {
    float x, y;    // sample position in sprite units
    float nx, ny;  // -1..1 position inside the shape (a fake surface normal)
    int ix, iy;    // target pixel coordinate (for pixel-sized noise)
};
using Fill = std::function<Col(const Frag&)>;

struct Canvas {
    int w = 0, h = 0;     // samples
    int ss = 1;           // samples per target pixel on each axis
    float ppu = 1;        // samples per sprite unit
    float tpu = 1;        // target pixels per unit
    std::vector<Col> px;

    Canvas(float unitsW, float unitsH, float targetPxPerUnit, int superSample)
    {
        ss = superSample;
        tpu = targetPxPerUnit;
        ppu = targetPxPerUnit * ss;
        w = (int)std::lround(unitsW * targetPxPerUnit) * ss;
        h = (int)std::lround(unitsH * targetPxPerUnit) * ss;
        px.assign((size_t)w * h, Col{});
    }

    void put(int x, int y, Col c)
    {
        if (x < 0 || y < 0 || x >= w || y >= h || c.a <= 0) return;
        Col& d = px[(size_t)y * w + x];
        float a = c.a + d.a * (1 - c.a);
        if (a <= 0) return;
        d.r = (c.r * c.a + d.r * d.a * (1 - c.a)) / a;
        d.g = (c.g * c.a + d.g * d.a * (1 - c.a)) / a;
        d.b = (c.b * c.a + d.b * d.a * (1 - c.a)) / a;
        d.a = a;
    }

    template <class Inside>
    void shape(float x0, float y0, float x1, float y1, Inside inside, const Fill& fill)
    {
        int sx0 = std::max(0, (int)std::floor(x0 * ppu)), sy0 = std::max(0, (int)std::floor(y0 * ppu));
        int sx1 = std::min(w - 1, (int)std::ceil(x1 * ppu)), sy1 = std::min(h - 1, (int)std::ceil(y1 * ppu));
        for (int sy = sy0; sy <= sy1; ++sy)
            for (int sx = sx0; sx <= sx1; ++sx) {
                Frag f;
                f.x = (sx + 0.5f) / ppu;
                f.y = (sy + 0.5f) / ppu;
                f.ix = sx / ss;
                f.iy = sy / ss;
                if (inside(f)) put(sx, sy, fill(f));
            }
    }

    void ellipse(float cx, float cy, float rx, float ry, const Fill& fill)
    {
        shape(cx - rx, cy - ry, cx + rx, cy + ry, [&](Frag& f) {
            f.nx = (f.x - cx) / rx;
            f.ny = (f.y - cy) / ry;
            return f.nx * f.nx + f.ny * f.ny <= 1.0f;
        }, fill);
    }
    void circle(float cx, float cy, float r, const Fill& fill) { ellipse(cx, cy, r, r, fill); }

    void rect(float x, float y, float rw, float rh, const Fill& fill, float rad = 0)
    {
        shape(x, y, x + rw, y + rh, [&](Frag& f) {
            f.nx = 2 * (f.x - x) / rw - 1;
            f.ny = 2 * (f.y - y) / rh - 1;
            if (f.x < x || f.y < y || f.x > x + rw || f.y > y + rh) return false;
            if (rad <= 0) return true;
            float qx = std::max(std::fabs(f.x - (x + rw / 2)) - (rw / 2 - rad), 0.0f);
            float qy = std::max(std::fabs(f.y - (y + rh / 2)) - (rh / 2 - rad), 0.0f);
            return qx * qx + qy * qy <= rad * rad;
        }, fill);
    }

    void capsule(float ax, float ay, float bx, float by, float r, const Fill& fill)
    {
        shape(std::min(ax, bx) - r, std::min(ay, by) - r, std::max(ax, bx) + r, std::max(ay, by) + r, [&](Frag& f) {
            float dx = bx - ax, dy = by - ay;
            float len2 = dx * dx + dy * dy;
            float t = len2 > 0 ? ((f.x - ax) * dx + (f.y - ay) * dy) / len2 : 0;
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            float px_ = ax + dx * t, py_ = ay + dy * t;
            f.nx = (f.x - px_) / r;
            f.ny = (f.y - py_) / r;
            return f.nx * f.nx + f.ny * f.ny <= 1.0f;
        }, fill);
    }

    void poly(const std::vector<Vector2>& pts, const Fill& fill)
    {
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (auto& p : pts) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
        shape(x0, y0, x1, y1, [&](Frag& f) {
            bool in = false;
            for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
                if (((pts[i].y > f.y) != (pts[j].y > f.y)) &&
                    (f.x < (pts[j].x - pts[i].x) * (f.y - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x))
                    in = !in;
            }
            f.nx = 2 * (f.x - x0) / std::max(0.001f, x1 - x0) - 1;
            f.ny = 2 * (f.y - y0) / std::max(0.001f, y1 - y0) - 1;
            return in;
        }, fill);
    }

    // One target pixel (or more), always visible however small: eyes, buttons.
    void dot(float x, float y, Col c, int sizePx = 1)
    {
        int tx = (int)std::floor(x * tpu), ty = (int)std::floor(y * tpu);
        for (int py = 0; py < sizePx * ss; ++py)
            for (int pxx = 0; pxx < sizePx * ss; ++pxx) put(tx * ss + pxx, ty * ss + py, c);
    }

    // Glitch: shift horizontal bands of target pixels sideways.
    void glitchRows(int seed, float maxShiftUnits)
    {
        int bandH = std::max(1, ss * (int)std::max(1.0f, tpu / 8));
        std::vector<Col> row(w);
        for (int y0 = 0; y0 < h; y0 += bandH) {
            float r = rnd(y0 / bandH, seed, 77);
            if (r < 0.55f) continue;
            int shift = (int)std::lround((rnd(y0, seed, 78) * 2 - 1) * maxShiftUnits * ppu / ss) * ss;
            for (int y = y0; y < std::min(h, y0 + bandH); ++y) {
                for (int x = 0; x < w; ++x) row[x] = px[(size_t)y * w + x];
                for (int x = 0; x < w; ++x) {
                    int s = x - shift;
                    px[(size_t)y * w + x] = (s >= 0 && s < w) ? row[s] : Col{};
                }
            }
        }
    }
};

// ------------------------------------------------------------------ era fills
Fill solid(Col c) { return [c](const Frag&) { return c; }; }

// Era-aware shading: flat in 1-bit and 8-bit, banded in 16-bit, lit in 2.5D.
Fill shade(Col c, float strength = 1.0f)
{
    // Hand-pixelled cel shading: flat colours in hard bands, never a gradient or a gloss.
    //   1-bit, 4-shade, 8-bit: one flat colour (8-bit keeps a single highlight cluster).
    //   16-bit: base + one shadow tone.  32-bit, 2.5D and 3D textures: shadow, base, highlight.
    return [c, strength](const Frag& f) {
        if (g_style <= 1) {
            if (g_style == 1 && f.nx * f.nx + f.ny * f.ny < 0.2f && f.nx < -0.1f && f.ny < -0.1f && strength > 0.5f)
                return mixc(c, hex(0xffffff), 0.25f);
            return c;
        }
        float nz = std::sqrt(std::max(0.0f, 1 - f.nx * f.nx * 0.8f - f.ny * f.ny * 0.8f));
        float d = std::max(0.0f, f.nx * -0.45f + f.ny * -0.65f + nz * 0.62f);
        Col o = c;
        if (d < 0.32f && strength > 0.2f) o = mixc(mul(c, 0.72f), hex(0x2a2050), 0.12f);   // shadow, a little cool
        else if (g_era >= 4 && d > 0.86f && strength > 0.5f) o = mixc(c, hex(0xfff6dc), 0.22f);   // highlight
        o.a = c.a;
        return o;
    };
}

float bayer4f(int x, int y)
{
    static const int m[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return m[(y & 3) * 4 + (x & 3)] / 16.0f;
}

// The Forgetting's "static", as an ordered dither pattern between two colours: a clean,
// regular pixel texture instead of random noise. The seed shifts the pattern per frame.
Fill staticFill(Col a, Col b, int seed, float density = 0.5f)
{
    return [a, b, seed, density](const Frag& f) {
        int ox = seed % 4, oy = (seed / 4) % 4;
        Col c = bayer4f(f.ix + ox, f.iy + oy) < density * 0.6f ? b : a;
        if (g_style >= 2) {
            float nz = std::sqrt(std::max(0.0f, 1 - f.nx * f.nx * 0.8f - f.ny * f.ny * 0.8f));
            float d = std::max(0.0f, f.nx * -0.45f + f.ny * -0.65f + nz * 0.62f);
            if (d < 0.32f) c = mul(c, 0.75f);
        }
        c.a = a.a;
        return c;
    };
}

// Era 0 colour helper: pick the light, dark or dither level explicitly.
Col eraCol(uint32_t normal, float onebitLevel)
{
    if (g_style == 0) return mixc(ONEBIT_DARK, ONEBIT_LIGHT, onebitLevel);
    return hex(normal);
}

// ------------------------------------------------------------------ finalise
Color toColor(Col c)
{
    return Color{(unsigned char)std::lround(clamp01(c.r) * 255), (unsigned char)std::lround(clamp01(c.g) * 255),
                 (unsigned char)std::lround(clamp01(c.b) * 255), (unsigned char)std::lround(clamp01(c.a) * 255)};
}

Col nearestPalette(Col c)
{
    int best = 0;
    float bd = 1e9f;
    for (int i = 0; i < 16; ++i) {
        Col p = hex(PALETTE16[i]);
        float dr = p.r - c.r, dg = p.g - c.g, db = p.b - c.b;
        float d = dr * dr * 0.3f + dg * dg * 0.59f + db * db * 0.11f;
        if (d < bd) { bd = d; best = i; }
    }
    Col o = hex(PALETTE16[best]);
    o.a = c.a;
    return o;
}

// The four greys of the 4-shade stage. Colours sitting right on the edge between two greys
// get a checker of both; everything else is a clean flat grey, like a handheld LCD.
Col shade4(Col c, int ix, int iy)
{
    const float cut[3] = {0.2f, 0.41f, 0.62f};
    float l = lum(c);
    int idx = 0;
    for (int i = 0; i < 3; ++i) {
        if (l > cut[i] + 0.025f) idx = i + 1;
        else if (l > cut[i] - 0.025f) idx = (((ix + iy) & 1) == 0) ? i + 1 : i;
    }
    Col o = hex(SHADES4[idx]);
    o.a = c.a;
    return o;
}

Col quantize(Col c, int ix, int iy)
{
    if (g_era == 0) {
        float l = lum(c);
        bool light;
        if (l < 0.28f) light = false;
        else if (l < 0.62f) light = ((ix + iy) & 1) == 0;
        else light = true;
        Col o = light ? ONEBIT_LIGHT : ONEBIT_DARK;
        o.a = c.a;
        return o;
    }
    if (g_era == 1) return shade4(c, ix, iy);
    if (g_era == 2) return nearestPalette(c);
    if (g_era == 3) {
        auto q = [](float v) { return std::round(clamp01(v) * 15.0f) / 15.0f; };
        return {q(c.r), q(c.g), q(c.b), c.a};
    }
    // 32-bit and later: 32 levels per channel
    auto q = [](float v) { return std::round(clamp01(v) * 31.0f) / 31.0f; };
    return {q(c.r), q(c.g), q(c.b), c.a};
}

// Downsample a canvas into the sheet image at (ox, oy) in pixels.
void blit(const Canvas& cv, Image& sheet, int ox, int oy, bool outline)
{
    int tw = cv.w / cv.ss, th = cv.h / cv.ss;
    std::vector<Col> out((size_t)tw * th);
    for (int y = 0; y < th; ++y)
        for (int x = 0; x < tw; ++x) {
            Col acc{};
            float wa = 0;
            for (int sy = 0; sy < cv.ss; ++sy)
                for (int sx = 0; sx < cv.ss; ++sx) {
                    const Col& s = cv.px[(size_t)(y * cv.ss + sy) * cv.w + x * cv.ss + sx];
                    acc.r += s.r * s.a; acc.g += s.g * s.a; acc.b += s.b * s.a; wa += s.a;
                }
            float n = (float)(cv.ss * cv.ss);
            Col c{};
            if (wa > 0) c = {acc.r / wa, acc.g / wa, acc.b / wa, wa / n};
            c.a = c.a >= 0.5f ? 1.0f : 0.0f;   // every stage is pixel art: hard edges
            if (c.a > 0) c = quantize(c, x, y);
            out[(size_t)y * tw + x] = c;
        }
    if (outline) {
        std::vector<Col> res = out;
        Col oc = g_era == 0 ? ONEBIT_DARK : (g_era == 1 ? hex(SHADES4[0]) : hex(0x140f1e));
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < tw; ++x) {
                if (out[(size_t)y * tw + x].a > 0) continue;
                bool near = false;
                const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
                for (int k = 0; k < 4; ++k) {
                    int nx = x + dx[k], ny = y + dy[k];
                    if (nx >= 0 && ny >= 0 && nx < tw && ny < th && out[(size_t)ny * tw + nx].a > 0) near = true;
                }
                if (near && g_era >= 4) {
                    // coloured outline: a deep, slightly purple version of the colour inside
                    Col in{};
                    for (int k = 0; k < 4 && in.a <= 0; ++k) {
                        int nx = x + dx[k], ny = y + dy[k];
                        if (nx >= 0 && ny >= 0 && nx < tw && ny < th) in = out[(size_t)ny * tw + nx];
                    }
                    Col o = mixc(mul(in, 0.32f), hex(0x1a1030), 0.35f);
                    o.a = 1;
                    res[(size_t)y * tw + x] = o;
                } else if (near) res[(size_t)y * tw + x] = oc;
            }
        out = res;
    }
    auto* data = (Color*)sheet.data;
    for (int y = 0; y < th; ++y)
        for (int x = 0; x < tw; ++x) {
            int X = ox + x, Y = oy + y;
            if (X < 0 || Y < 0 || X >= sheet.width || Y >= sheet.height) continue;
            data[(size_t)Y * sheet.width + X] = toColor(out[(size_t)y * tw + x]);
        }
}

const int SUPER[ERA_COUNT] = {1, 2, 2, 2, 2, 2, 2};

// Builds a one-row sheet of `frames` frames, each fw x fh tiles.
Image makeSheet(int frames, float fw, float fh, bool outline, const std::function<void(Canvas&, int)>& draw)
{
    int tp = TILE_PX[g_era];
    int pw = (int)std::lround(fw * tp), ph = (int)std::lround(fh * tp);
    Image sheet = GenImageColor(pw * frames, ph, BLANK);
    for (int f = 0; f < frames; ++f) {
        Canvas cv(fw, fh, (float)tp, SUPER[g_era]);
        draw(cv, f);
        blit(cv, sheet, f * pw, 0, outline);
    }
    return sheet;
}

// ------------------------------------------------------------------ players
struct Pose {
    float bob = 0, lean = 0;
    Vector2 footF{0.58f, 1.45f}, footB{0.42f, 1.45f};
    Vector2 handF{0.64f, 1.0f}, handB{0.36f, 1.0f};
};

Pose playerPose(int frame)
{
    Pose p;
    switch (frame) {
    case PF_IDLE1: p.bob = 0.025f; p.handF.y += 0.02f; p.handB.y += 0.02f; break;
    case PF_RUN0: case PF_RUN1: case PF_RUN2: case PF_RUN3: {
        float ph = (frame - PF_RUN0) * 1.5707963f + 0.6f;
        float s = std::sin(ph), c = std::cos(ph);
        p.lean = 0.05f;
        p.bob = -0.035f * std::fabs(s);
        p.footF = {0.52f + 0.25f * s, 1.45f - 0.13f * std::max(0.0f, c)};
        p.footB = {0.52f - 0.25f * s, 1.45f - 0.13f * std::max(0.0f, -c)};
        p.handF = {0.55f - 0.2f * s, 0.98f};
        p.handB = {0.55f + 0.2f * s, 0.98f};
        break;
    }
    case PF_JUMP: p.bob = -0.04f; p.footF = {0.64f, 1.3f}; p.footB = {0.4f, 1.38f};
        p.handF = {0.78f, 0.56f}; p.handB = {0.26f, 0.62f}; break;
    case PF_FALL: p.footF = {0.62f, 1.46f}; p.footB = {0.4f, 1.42f};
        p.handF = {0.84f, 0.74f}; p.handB = {0.18f, 0.76f}; break;
    case PF_DASH: p.lean = 0.14f; p.footF = {0.72f, 1.42f}; p.footB = {0.22f, 1.36f};
        p.handF = {0.36f, 0.95f}; p.handB = {0.22f, 0.9f}; break;
    case PF_SPECIAL: p.bob = -0.02f; p.footF = {0.56f, 1.46f}; p.footB = {0.44f, 1.43f};
        p.handF = {0.56f, 0.42f}; p.handB = {0.3f, 0.5f}; break;
    case PF_POUND: p.bob = 0.12f; p.footF = {0.62f, 1.36f}; p.footB = {0.38f, 1.36f};
        p.handF = {0.7f, 1.22f}; p.handB = {0.3f, 1.22f}; break;
    case PF_WALL: p.lean = 0.06f; p.footF = {0.72f, 1.38f}; p.footB = {0.5f, 1.46f};
        p.handF = {0.86f, 0.7f}; p.handB = {0.4f, 1.0f}; break;
    case PF_ATTACK: p.lean = 0.08f; p.footF = {0.68f, 1.45f}; p.footB = {0.34f, 1.45f};
        p.handF = {0.96f, 0.84f}; p.handB = {0.3f, 0.95f}; break;
    default: break;
    }
    return p;
}

void drawPlayer(Canvas& c, int who, int frame)
{
    // who 0 = Player 1 (Gran, warm orange). who 1 = Player 2 (Sam, cool blue).
    const bool gran = who == 0;
    Pose p = playerPose(frame);
    Col skin = eraCol(gran ? 0xf2c29b : 0xe9b58c, 0.95f);
    Col top = gran ? eraCol(0xf08a24, 0.9f) : eraCol(0x3c8cf0, 0.45f);
    Col topDark = gran ? eraCol(0xc4621a, 0.8f) : eraCol(0x2a64b8, 0.35f);
    Col legs = gran ? eraCol(0x6b4a3a, 0.7f) : eraCol(0x2b3f6b, 0.3f);
    Col shoes = gran ? eraCol(0x4a2e22, 0.2f) : eraCol(0xf4f4f4, 0.95f);
    Col hair = gran ? eraCol(0xdcd8ea, 0.95f) : eraCol(0x6b3e22, 0.15f);
    Col dark = eraCol(0x1c1420, 0.0f);

    float hx = 0.52f + p.lean, hy = 0.42f + p.bob;
    Vector2 shoulder{0.5f + p.lean * 0.8f, 0.72f + p.bob};
    Vector2 hip{0.5f + p.lean * 0.3f, 1.06f + p.bob};

    // motion streaks for the dash
    if (frame == PF_DASH)
        for (int i = 0; i < 3; ++i)
            c.capsule(0.02f, 0.75f + i * 0.18f, 0.2f + i * 0.04f, 0.75f + i * 0.18f, 0.025f, solid(withA(top, 0.7f)));

    // umbrella (P1 glide) behind everything else in the frame
    if (frame == PF_SPECIAL && gran) {
        Col canopy = eraCol(0xb03a6a, 0.85f);
        c.capsule(p.handF.x, p.handF.y, p.handF.x, 0.16f, 0.025f, solid(eraCol(0x3a2a2a, 0.1f)));
        c.poly({{0.06f, 0.24f}, {0.2f, 0.08f}, {0.52f, 0.02f}, {0.84f, 0.08f}, {0.98f, 0.24f},
                {0.82f, 0.2f}, {0.68f, 0.24f}, {0.52f, 0.2f}, {0.36f, 0.24f}, {0.22f, 0.2f}}, shade(canopy));
    }
    // time pulse ring (P2 special)
    if (frame == PF_SPECIAL && !gran) {
        Col ring = eraCol(0x7fe8ff, 1.0f);
        c.shape(0, 0, 1, 1.5f, [&](Frag& f) {
            float dx = f.x - 0.5f, dy = (f.y - 0.8f) * 0.8f;
            float d = std::sqrt(dx * dx + dy * dy);
            f.nx = dx; f.ny = dy;
            return d > 0.42f && d < 0.48f;
        }, solid(withA(ring, 0.9f)));
    }

    float limb = 0.075f;
    // back arm and back leg
    c.capsule(shoulder.x - 0.06f, shoulder.y + 0.04f, p.handB.x, p.handB.y, 0.06f, shade(topDark));
    c.circle(p.handB.x, p.handB.y, 0.055f, shade(skin));
    c.capsule(hip.x - 0.05f, hip.y, p.footB.x, p.footB.y - 0.04f, limb, shade(mul(legs, 0.85f)));
    c.ellipse(p.footB.x + 0.04f, p.footB.y - 0.02f, 0.1f, 0.055f, shade(shoes));

    // torso
    float tx = 0.32f + p.lean * 0.6f;
    if (gran) {
        c.rect(tx, 0.62f + p.bob, 0.36f, 0.52f, shade(top), 0.1f);
        c.rect(tx + 0.03f, 0.98f + p.bob, 0.3f, 0.18f, shade(legs), 0.04f);  // skirt
        if (g_style >= 1) {
            c.dot(tx + 0.22f, 0.74f + p.bob, eraCol(0xfaf3e6, 1));
            c.dot(tx + 0.22f, 0.86f + p.bob, eraCol(0xfaf3e6, 1));
        }
    } else {
        c.circle(tx + 0.08f, 0.66f + p.bob, 0.1f, shade(topDark));  // hood
        c.rect(tx, 0.62f + p.bob, 0.36f, 0.5f, shade(top), 0.08f);
        if (g_style >= 1) c.rect(tx + 0.08f, 0.92f + p.bob, 0.22f, 0.08f, solid(topDark));  // pocket
    }

    // front leg
    c.capsule(hip.x + 0.05f, hip.y, p.footF.x, p.footF.y - 0.04f, limb, shade(legs));
    c.ellipse(p.footF.x + 0.04f, p.footF.y - 0.02f, 0.1f, 0.055f, shade(shoes));

    // head
    c.ellipse(hx, hy, 0.21f, 0.2f, shade(skin));
    if (gran) {
        c.ellipse(hx - 0.05f, hy - 0.09f, 0.2f, 0.12f, shade(hair));
        c.ellipse(hx - 0.13f, hy + 0.0f, 0.09f, 0.15f, shade(hair));
        c.ellipse(hx - 0.09f, hy - 0.23f, 0.09f, 0.075f, shade(hair));   // bun
    } else {
        c.ellipse(hx - 0.04f, hy - 0.08f, 0.21f, 0.13f, shade(hair));
        c.ellipse(hx - 0.14f, hy + 0.0f, 0.08f, 0.13f, shade(hair));
        Col cap = eraCol(0x22c4b0, 0.6f);
        c.rect(hx - 0.2f, hy - 0.25f, 0.36f, 0.14f, shade(cap), 0.07f);
        c.rect(hx + 0.08f, hy - 0.14f, 0.24f, 0.05f, solid(mul(cap, 0.8f)), 0.02f);   // brim
    }
    // face
    c.dot(hx + 0.09f, hy - 0.01f, dark);
    if (gran && g_style >= 1) {
        Col glass = eraCol(0x3a2a3a, 0.0f);
        c.dot(hx + 0.04f, hy - 0.01f, glass);
        c.dot(hx + 0.14f, hy - 0.01f, glass);
    }
    if (g_style >= 2) c.dot(hx + 0.1f, hy + 0.09f, mul(skin, 0.7f));  // smile

    // front arm
    c.capsule(shoulder.x + 0.05f, shoulder.y + 0.04f, p.handF.x, p.handF.y, 0.06f, shade(top));
    c.circle(p.handF.x, p.handF.y, 0.055f, shade(skin));

    if (frame == PF_ATTACK) {
        Col spark = eraCol(0xfff2a0, 1.0f);
        c.poly({{0.98f, 0.66f}, {1.02f, 0.82f}, {1.0f, 0.86f}, {0.86f, 0.84f}, {0.98f, 0.9f}, {0.92f, 1.02f}},
               solid(withA(spark, 0.95f)));
    }
}

// ------------------------------------------------------------------ enemies
void drawBlob(Canvas& c, int frame)
{
    float sq = 0;
    if (frame <= BF_WALK3) sq = 0.08f * std::sin(frame * 1.5707963f);
    Col body = eraCol(0x8a7fb4, 0.5f);
    Col speck = eraCol(0x26305e, 0.0f);
    Col eye = eraCol(0xfaf3e6, 1.0f);
    Col pupil = eraCol(0x0f0d1a, 0.0f);
    if (frame == BF_SQUASH) {
        c.ellipse(0.5f, 0.88f, 0.46f, 0.12f, staticFill(body, speck, 9, 0.35f));
        c.dot(0.36f, 0.86f, pupil);
        c.dot(0.62f, 0.86f, pupil);
        return;
    }
    // little feet
    float step = (frame % 2) ? 0.04f : -0.04f;
    c.ellipse(0.3f + step, 0.93f, 0.1f, 0.06f, shade(mul(body, 0.7f)));
    c.ellipse(0.7f - step, 0.93f, 0.1f, 0.06f, shade(mul(body, 0.7f)));
    float ry = 0.36f * (1 - sq), rx = 0.42f * (1 + sq);
    c.ellipse(0.5f, 0.92f - ry, rx, ry, staticFill(body, speck, 10 + frame, 0.3f));
    float ey = 0.92f - ry * 1.15f;
    c.ellipse(0.62f, ey, 0.1f, 0.12f, solid(eye));
    c.ellipse(0.82f, ey, 0.08f, 0.1f, solid(eye));
    c.dot(0.66f, ey, pupil);
    c.dot(0.84f, ey, pupil);
    // antenna of static
    c.capsule(0.4f, 0.92f - 2 * ry + 0.05f, 0.32f, 0.92f - 2 * ry - 0.08f, 0.025f, solid(speck));
    c.dot(0.31f, 0.92f - 2 * ry - 0.1f, eye);
}

void drawFlyer(Canvas& c, int frame)
{
    Col body = eraCol(0xe8364f, 0.9f);
    Col wing = eraCol(0x3c9cf0, 0.45f);
    Col eye = eraCol(0xfaf3e6, 1.0f);
    Col pupil = eraCol(0x0f0d1a, 0.0f);
    if (frame == FF_HURT) { body = eraCol(0xfaf3e6, 1.0f); wing = eraCol(0xf7d73a, 0.9f); }
    float flap = std::sin(frame * 1.5707963f);
    float wy = 0.5f - 0.3f * flap;
    c.poly({{0.42f, 0.5f}, {0.04f, wy - 0.05f}, {0.16f, wy + 0.12f}}, shade(wing));
    c.poly({{0.58f, 0.5f}, {0.96f, wy - 0.05f}, {0.84f, wy + 0.12f}}, shade(wing));
    c.poly({{0.5f, 0.18f}, {0.78f, 0.5f}, {0.5f, 0.84f}, {0.22f, 0.5f}}, shade(body));
    c.circle(0.56f, 0.46f, 0.12f, solid(eye));
    c.dot(0.6f, 0.46f, pupil, g_style >= 2 ? 2 : 1);
    if (frame != FF_HURT) c.glitchRows(40 + frame, 0.12f);
}

void drawBrute(Canvas& c, int frame)
{
    Col fog = eraCol(0x6e6a8e, 0.45f);
    Col fogLight = eraCol(0xa8a4c8, 0.75f);
    Col eye = eraCol(0xff4040, 1.0f);
    if (frame == RF_HURT) { fog = eraCol(0xd8d4f0, 0.9f); fogLight = eraCol(0xffffff, 1.0f); }
    float lean = frame == RF_CHARGE ? 0.18f : 0.0f;
    float step = (frame <= RF_WALK3) ? 0.1f * std::sin(frame * 1.5707963f) : 0.0f;
    // legs
    c.rect(0.55f + step, 1.5f, 0.3f, 0.45f, shade(mul(fog, 0.75f)), 0.1f);
    c.rect(1.15f - step, 1.5f, 0.3f, 0.45f, shade(mul(fog, 0.75f)), 0.1f);
    // back arm
    c.capsule(0.6f + lean, 0.9f, 0.35f + lean - step, 1.45f, 0.16f, shade(mul(fog, 0.8f)));
    // body: a cluster of fog puffs
    c.circle(1.0f + lean, 1.05f, 0.58f, shade(fog));
    c.circle(0.7f + lean, 0.7f, 0.36f, shade(fogLight));
    c.circle(1.25f + lean, 0.62f, 0.4f, shade(fog));
    c.circle(1.0f + lean, 0.45f, 0.34f, shade(fogLight));
    c.circle(1.45f + lean, 1.05f, 0.3f, shade(fog));
    // eyes
    float ey = frame == RF_CHARGE ? 0.72f : 0.68f;
    c.ellipse(1.28f + lean, ey, 0.1f, frame == RF_CHARGE ? 0.1f : 0.07f, solid(eye));
    c.ellipse(1.58f + lean, ey + 0.02f, 0.08f, frame == RF_CHARGE ? 0.09f : 0.06f, solid(eye));
    // front arm
    c.capsule(1.4f + lean, 0.95f, 1.65f + lean + step, 1.45f, 0.17f, shade(fogLight));
}

void drawBoss(Canvas& c, int frame)
{
    Col dark = eraCol(0x26305e, 0.15f);
    Col swirl = eraCol(0x6a2c6e, 0.45f);
    Col iris = eraCol(0xf7d73a, 0.95f);
    Col white = eraCol(0xfaf3e6, 1.0f);
    float wob = frame == XF_FLOAT1 ? 0.06f : 0.0f;
    float slam = frame == XF_SLAM ? 0.25f : 0.0f;
    if (frame == XF_HURT) { dark = eraCol(0xb8b6c8, 0.8f); swirl = eraCol(0xfaf3e6, 1.0f); }
    // tendrils
    for (int i = 0; i < 6; ++i) {
        float x = 0.55f + i * 0.38f;
        float sway = std::sin(i * 1.7f + frame * 1.3f) * 0.18f;
        c.capsule(x, 1.9f + slam, x + sway, 2.85f - slam * 0.3f, 0.12f - i % 2 * 0.03f, staticFill(swirl, dark, 60 + frame, 0.5f));
    }
    // body: a big disc of static with a ring
    c.ellipse(1.5f, 1.4f + wob + slam, 1.25f, 1.1f - slam * 0.4f, staticFill(dark, swirl, 70 + frame, 0.35f));
    c.shape(0, 0, 3, 3, [&](Frag& f) {
        float dx = (f.x - 1.5f) / 1.0f, dy = (f.y - 1.35f - wob - slam) / 0.85f;
        float d = std::sqrt(dx * dx + dy * dy);
        f.nx = dx; f.ny = dy;
        return d > 0.86f && d < 0.98f;
    }, solid(swirl));
    // the eye
    c.ellipse(1.5f, 1.32f + wob + slam, 0.62f, 0.42f - slam * 0.3f, shade(white, 0.5f));
    if (frame != XF_HURT) {
        c.circle(1.62f, 1.32f + wob + slam, 0.28f - slam * 0.2f, shade(iris));
        c.circle(1.66f, 1.32f + wob + slam, 0.13f - slam * 0.1f, solid(eraCol(0x0f0d1a, 0.0f)));
        c.dot(1.55f, 1.2f + wob + slam, white, g_style >= 2 ? 2 : 1);
    } else {
        c.capsule(1.1f, 1.1f, 1.9f, 1.55f, 0.05f, solid(eraCol(0x0f0d1a, 0.0f)));
        c.capsule(1.1f, 1.55f, 1.9f, 1.1f, 0.05f, solid(eraCol(0x0f0d1a, 0.0f)));
    }
    if (frame == XF_FLOAT1) c.glitchRows(90, 0.2f);
}

// ------------------------------------------------------------------ tiles
void drawTile(Canvas& c, int t)
{
    switch (t) {
    case T_GROUND_TOP:
    case T_GROUND: {
        Col dirt = eraCol(0x8c4a2f, 0.0f);
        Col dirtDark = eraCol(0x5e3020, 0.0f);
        Col stone = eraCol(0xb8b6c8, 1.0f);
        c.rect(0, 0, 1, 1, [&](const Frag& f) {
            Col d = dirt;
            if (g_style == 0) {
                // 1-bit: a clean brick-and-mortar pattern, light lines on dark (8 x 8 tile)
                int lx = f.ix & 7, ly = f.iy & 7;
                bool mortar = ly == 7 || (lx == 3 && ly < 7 && ly > 1);
                return mortar ? ONEBIT_LIGHT : ONEBIT_DARK;
            }
            if (g_style >= 2 && f.y > 0.62f) d = mixc(dirt, dirtDark, 0.45f);   // one darker band low in the tile
            if (rnd(f.ix, f.iy, 5) < 0.12f) d = dirtDark;
            return d;
        });
        // a few stones
        for (int i = 0; i < (g_style == 0 ? 0 : 3); ++i) {   // stones would be noise in 1-bit
            float sx = 0.15f + rnd(i, t, 6) * 0.7f, sy = 0.4f + rnd(i, t, 7) * 0.5f;
            c.ellipse(sx, sy, 0.09f, 0.06f, shade(g_style == 0 ? ONEBIT_LIGHT : stone, 0.8f));
        }
        if (t == T_GROUND_TOP) {
            Col grass = eraCol(0x6fdc4a, 1.0f), grassDark = eraCol(0x1e8a4c, 1.0f);
            c.rect(0, 0, 1, 0.28f, [&](const Frag& f) {
                if (g_style == 0) return (f.y < 0.14f) ? ONEBIT_LIGHT : ONEBIT_DARK;   // one bright top edge
                Col g = f.y < 0.12f ? grass : grassDark;
                if (g_style >= 2) g = f.y < 0.1f ? grass : (f.y < 0.19f ? mixc(grass, grassDark, 0.5f) : grassDark);
                return g;
            });
            // grass tufts hanging down
            for (int i = 0; i < (g_style == 0 ? 2 : 4); ++i) {
                float gx = 0.12f + i * 0.25f;
                c.poly({{gx - 0.06f, 0.27f}, {gx + 0.06f, 0.27f}, {gx, 0.38f + 0.04f * (i % 2)}},
                       solid(g_style == 0 ? ONEBIT_LIGHT : grassDark));
            }
        }
        break;
    }
    case T_BLOCK:
    case T_CRACKED: {
        Col brick = t == T_BLOCK ? eraCol(0xc8503a, 0.9f) : eraCol(0xa8a4b8, 0.9f);
        Col mortar = eraCol(0x4a2a24, 0.0f);
        c.rect(0, 0, 1, 1, solid(mortar));
        for (int row = 0; row < 2; ++row)
            for (int col = -1; col < 2; ++col) {
                float off = row ? 0.25f : -0.25f;
                float x = col * 0.5f + 0.25f + off;
                c.rect(x + 0.04f, row * 0.5f + 0.05f, 0.42f, 0.4f, shade(brick, 0.7f), 0.03f);
            }
        if (t == T_CRACKED) {
            Col crack = eraCol(0x1c1420, 0.0f);
            c.capsule(0.5f, 0.0f, 0.38f, 0.32f, 0.04f, solid(crack));
            c.capsule(0.38f, 0.32f, 0.6f, 0.58f, 0.04f, solid(crack));
            c.capsule(0.6f, 0.58f, 0.46f, 1.0f, 0.04f, solid(crack));
            c.capsule(0.6f, 0.58f, 0.85f, 0.7f, 0.03f, solid(crack));
        }
        break;
    }
    case T_P1:
    case T_P2: {
        Col base = t == T_P1 ? eraCol(0xf28a1e, 0.85f) : eraCol(0x3c9cf0, 0.45f);
        Col glow = t == T_P1 ? eraCol(0xf7d73a, 1.0f) : eraCol(0x7fe8ff, 1.0f);
        float a = g_style == 3 ? 0.88f : 1.0f;
        c.rect(0.0f, 0.0f, 1.0f, 0.55f, shade(withA(base, a), 0.6f), 0.12f);
        c.rect(0.12f, 0.1f, 0.76f, 0.12f, solid(withA(glow, a)), 0.05f);
        // dotted "memory" pattern so the 1-bit era can tell them apart too
        for (int i = 0; i < 3; ++i) c.dot(0.2f + i * 0.3f, 0.38f, t == T_P1 ? glow : eraCol(0x0f0d1a, 0.0f));
        break;
    }
    case T_WALL: {
        Col stone = eraCol(0x8a8aa0, 0.8f), dark = eraCol(0x3a3a50, 0.0f);
        c.rect(0, 0, 1, 1, solid(dark));
        c.rect(0.06f, 0.04f, 0.88f, 0.42f, shade(stone, 0.7f), 0.05f);
        c.rect(0.06f, 0.54f, 0.88f, 0.42f, shade(stone, 0.7f), 0.05f);
        Col vine = eraCol(0x1e8a4c, 0.0f);
        if (g_style >= 1) {
            c.capsule(0.2f, 0.0f, 0.3f, 0.5f, 0.04f, solid(vine));
            c.capsule(0.3f, 0.5f, 0.18f, 1.0f, 0.04f, solid(vine));
            c.circle(0.36f, 0.3f, 0.06f, solid(eraCol(0x6fdc4a, 1)));
        }
        break;
    }
    case T_FRAG0:
    case T_FRAG1: {
        float s = t == T_FRAG0 ? 1.0f : 0.85f;
        Col gold = eraCol(0xf7d73a, 1.0f), white = eraCol(0xfffbe8, 1.0f);
        if (g_style == 3) c.circle(0.5f, 0.5f, 0.42f, [&](const Frag& f) {
            float d = std::sqrt(f.nx * f.nx + f.ny * f.ny);
            return withA(gold, (1 - d) * 0.45f);
        });
        c.poly({{0.5f, 0.5f - 0.36f * s}, {0.5f + 0.22f * s, 0.5f}, {0.5f, 0.5f + 0.36f * s}, {0.5f - 0.22f * s, 0.5f}}, shade(gold));
        c.poly({{0.5f, 0.5f - 0.24f * s}, {0.5f + 0.08f * s, 0.5f}, {0.5f, 0.5f + 0.1f * s}, {0.5f - 0.1f * s, 0.46f}}, solid(white));
        if (t == T_FRAG1) {
            c.capsule(0.82f, 0.1f, 0.82f, 0.3f, 0.025f, solid(white));
            c.capsule(0.72f, 0.2f, 0.92f, 0.2f, 0.025f, solid(white));
        }
        break;
    }
    case T_PU_SHIELD: {   // Keepsake locket: absorbs one hit
        Col red = eraCol(0xe8364f, 0.9f), chain = eraCol(0xf7d73a, 1.0f);
        c.capsule(0.3f, 0.08f, 0.5f, 0.26f, 0.025f, solid(chain));
        c.capsule(0.7f, 0.08f, 0.5f, 0.26f, 0.025f, solid(chain));
        c.circle(0.36f, 0.42f, 0.17f, shade(red));
        c.circle(0.64f, 0.42f, 0.17f, shade(red));
        c.poly({{0.2f, 0.48f}, {0.8f, 0.48f}, {0.5f, 0.88f}}, shade(red));
        c.dot(0.36f, 0.38f, eraCol(0xffffff, 1.0f), g_style >= 2 ? 2 : 1);
        break;
    }
    case T_PU_SLOW: {    // Hourglass: pushes the Forgetting Wall back and slows it
        Col frame = eraCol(0xab7a3a, 1.0f), glass = eraCol(0xd8f0ff, 0.6f), sand = eraCol(0xf7d73a, 1.0f);
        c.rect(0.18f, 0.06f, 0.64f, 0.1f, shade(frame), 0.03f);
        c.rect(0.18f, 0.84f, 0.64f, 0.1f, shade(frame), 0.03f);
        c.poly({{0.26f, 0.16f}, {0.74f, 0.16f}, {0.54f, 0.5f}, {0.74f, 0.84f}, {0.26f, 0.84f}, {0.46f, 0.5f}},
               shade(withA(glass, g_style == 3 ? 0.8f : 1.0f)));
        c.poly({{0.36f, 0.62f}, {0.64f, 0.62f}, {0.72f, 0.82f}, {0.28f, 0.82f}}, shade(sand));
        c.poly({{0.32f, 0.2f}, {0.68f, 0.2f}, {0.5f, 0.42f}}, shade(sand));
        break;
    }
    case T_PU_MAGNET: {  // Lullaby note: pulls fragments to you
        Col note = eraCol(0x8a7fb4, 0.8f), glow = eraCol(0xf28cb4, 1.0f);
        c.ellipse(0.34f, 0.74f, 0.17f, 0.13f, shade(note));
        c.ellipse(0.74f, 0.64f, 0.17f, 0.13f, shade(note));
        c.capsule(0.48f, 0.74f, 0.48f, 0.18f, 0.04f, solid(note));
        c.capsule(0.88f, 0.64f, 0.88f, 0.1f, 0.04f, solid(note));
        c.capsule(0.48f, 0.18f, 0.88f, 0.1f, 0.06f, solid(note));
        c.dot(0.3f, 0.7f, glow);
        break;
    }
    case T_PU_PHOTO: {   // Old photograph: a big chunk of memory
        Col paper = eraCol(0xfaf3e6, 1.0f), sky = eraCol(0x3c9cf0, 0.4f), grass = eraCol(0x6fdc4a, 0.7f);
        c.rect(0.12f, 0.12f, 0.76f, 0.8f, shade(paper, 0.4f), 0.03f);
        c.rect(0.2f, 0.2f, 0.6f, 0.5f, solid(sky));
        c.rect(0.2f, 0.52f, 0.6f, 0.18f, solid(grass));
        c.circle(0.42f, 0.46f, 0.06f, solid(eraCol(0xf28a1e, 1.0f)));
        c.circle(0.6f, 0.48f, 0.05f, solid(eraCol(0x26305e, 0.0f)));
        break;
    }
    case T_PU_SPEED: {   // Old trainers: run faster for a while
        Col shoe = eraCol(0xe8364f, 0.9f), sole = eraCol(0xfaf3e6, 1.0f), wing = eraCol(0xfaf3e6, 1.0f);
        c.poly({{0.38f, 0.28f}, {0.56f, 0.28f}, {0.6f, 0.52f}, {0.9f, 0.62f}, {0.9f, 0.76f}, {0.14f, 0.76f}, {0.16f, 0.5f}},
               shade(shoe));
        c.rect(0.12f, 0.74f, 0.8f, 0.1f, solid(sole), 0.03f);
        c.poly({{0.14f, 0.4f}, {0.0f, 0.3f}, {0.04f, 0.46f}, {-0.0f, 0.52f}, {0.14f, 0.54f}}, solid(wing));
        break;
    }
    default: break;
    }
}

// ------------------------------------------------------------------ backgrounds
// Background layers are drawn straight into pixels (no shapes), tiling on x.
struct Px {
    Image img;
    int w, h;
    explicit Px(int W, int H) : w(W), h(H) { img = GenImageColor(W, H, BLANK); }
    void set(int x, int y, Col c)
    {
        if (y < 0 || y >= h) return;
        x = ((x % w) + w) % w;
        Color* d = (Color*)img.data;
        Color& o = d[(size_t)y * w + x];
        float a = c.a + o.a / 255.0f * (1 - c.a);
        if (a <= 0) return;
        auto ch = [&](float s, unsigned char dd) {
            return (unsigned char)std::lround(clamp01((s * c.a + dd / 255.0f * (o.a / 255.0f) * (1 - c.a)) / a) * 255);
        };
        o = Color{ch(c.r, o.r), ch(c.g, o.g), ch(c.b, o.b), (unsigned char)std::lround(a * 255)};
    }
};

// Sum of sines with whole periods, so the result tiles across the width.
float ridge(float x, int w, int seed, float a1, float a2, float a3)
{
    float t = x / w * 6.2831853f;
    return a1 * std::sin(t * 2 + seed) + a2 * std::sin(t * 5 + seed * 1.7f) + a3 * std::sin(t * 11 + seed * 2.3f);
}

// Background colours. In the 4-shade stage the backdrop is lifted toward the light greys,
// so the dark-outlined sprites in front always read clearly.
Col q(Col c, int x, int y) { return quantize(g_era == 1 ? mixc(c, hex(0xffffff), 0.2f) : c, x, y); }

void bgLayer(Px& p, int era, int layer)
{
    const int W = p.w, H = p.h;
    float s = H / 112.0f;   // scale relative to the 1-bit size
    if (layer == 0) {       // sky
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float t = (float)y / H;
                if (g_era >= 1) t = std::floor(t * 7.0f) / 7.0f;   // the sky in flat bands, not a gradient
                Col c;
                if (era == 0) {
                    c = ONEBIT_DARK;
                    if (rnd(x, y, 3) < 0.0025f && y < H * 0.5f) c = ONEBIT_LIGHT;   // a few stars
                } else if (g_era == 1) c = mixc(hex(0xd8e0e0), hex(0x8ccff0), t * 0.4f);   // a pale handheld sky
                else if (era == 1) c = t < 0.55f ? hex(0x3c9cf0) : hex(0x8ccff0);
                else if (g_era == 4) c = mixc(mixc(hex(0x3a78c8), hex(0x8ec8f0), clamp01(t * 1.4f)), hex(0xffd8b0), clamp01(t * 2 - 0.9f));
                else if (era == 2) c = mixc(mixc(hex(0x4a3a8a), hex(0xd06a8a), clamp01(t * 1.5f)), hex(0xf7b06a), clamp01(t * 2 - 1));
                else if (g_era == 6) {
                    // morning: deep blue overhead, warm white at the horizon, the sun up high
                    c = mixc(mixc(hex(0x2e64b8), hex(0x9ccaf0), clamp01(t * 1.5f)), hex(0xfff0dc), clamp01(t * 2.2f - 1.0f));
                    float dx = (x % (W / 2)) - W * 0.36f, dy = y - H * 0.22f;
                    float d = std::sqrt(dx * dx + dy * dy) / H;
                    if (d < 0.06f) c = hex(0xfffbea);
                } else {
                    c = mixc(mixc(hex(0x2a2a6a), hex(0xe86a6a), clamp01(t * 1.6f)), hex(0xffc070), clamp01(t * 2.2f - 1.1f));
                    float dx = (x % (W / 2)) - W * 0.3f, dy = y - H * 0.55f;   // sun
                    float d = std::sqrt(dx * dx + dy * dy) / H;
                    if (d < 0.09f) c = hex(0xfff0c0);
                    else if (d < 0.13f) c = mixc(c, hex(0xffd090), 0.4f);
                }
                p.set(x, y, era <= 2 ? q(c, x, y) : c);
            }
        // clouds for 8-bit and 16-bit
        if (era == 1 || era == 2)
            for (int i = 0; i < 6; ++i) {
                float cx = (i + 0.3f) * W / 6.0f, cy = H * (0.15f + 0.1f * (i % 3));
                for (int k = 0; k < 4; ++k) {
                    float r = (10 + 4 * (k % 2)) * s;
                    float ox = cx + (k - 1.5f) * 9 * s, oy = cy - (k % 2) * 4 * s;
                    for (int y = (int)(oy - r); y <= oy + r; ++y)
                        for (int x = (int)(ox - r); x <= ox + r; ++x)
                            if ((x - ox) * (x - ox) + (y - oy) * (y - oy) * 2.2f < r * r)
                                p.set(x, y, q(era == 1 ? hex(0xfaf3e6) : ((y - oy) > r * 0.35f ? (g_era == 4 ? hex(0xc8d8f0) : hex(0xd8a0c0))
                                                                                       : (g_era == 4 ? hex(0xffffff) : hex(0xfff0f0))), x, y));
                }
            }
        return;
    }
    // hills / mountains / houses
    for (int x = 0; x < W; ++x) {
        float base, top;
        if (layer == 1) { base = H * 0.55f; top = base + ridge((float)x, W, 3, 8 * s, 4 * s, 1.5f * s); }
        else if (layer == 2) { base = H * 0.68f; top = base + ridge((float)x, W, 7, 6 * s, 3 * s, 1.0f * s); }
        else { base = H * 0.82f; top = base + ridge((float)x, W, 11, 3 * s, 2 * s, 1.5f * s); }
        if (era == 2 && g_era != 4 && layer == 1) top -= std::max(0.0f, std::fabs(std::sin(x / (float)W * 6.2831853f * 4)) - 0.6f) * 40 * s;  // peaks
        for (int y = (int)top; y < H; ++y) {
            float d = (y - top) / (H - top + 1);
            if (g_era >= 1) d = std::floor(d * 3.0f) / 3.0f;   // hills in flat bands
            Col c;
            if (era == 0) {
                // 1-bit: outline ridges with a little dither under them
                // Ordered halftone, not random dots: far hills get a sparse grid, near hills a
                // checker that fades out with depth. Reads as shading, not static.
                bool edge = y - (int)top < 1;
                bool on = edge;
                if (layer == 1) on = on || ((x % 4) == 0 && (y % 4) == 0);
                if (layer == 2) on = on || (((x + y) % 2) == 0 && (y % 2) == 0 && d < 0.35f);
                c = on ? ONEBIT_LIGHT : ONEBIT_DARK;
                if (!on && layer == 1) continue;
            } else if (era == 1) {
                const uint32_t cols[4] = {0, 0x8a7fb4, 0x1e8a4c, 0x26305e};
                c = hex(cols[layer]);
                if (y - (int)top < 1 * s) c = layer == 2 ? hex(0x6fdc4a) : hex(0xb8b6c8);
            } else if (g_era == 4) {
                const uint32_t cols[4] = {0, 0x7a8ac8, 0x4a8a6a, 0x2a6a4a};
                c = mixc(hex(cols[layer]), hex(0x1a2a3a), d * 0.5f);
                if (y - (int)top < 2 * s) c = mixc(c, hex(0xfff0c0), 0.35f);   // sunlit ridge
            } else if (g_era == 6) {
                const uint32_t cols[4] = {0, 0x8aa0d0, 0x6a9a8a, 0x4a7a5a};
                c = mixc(hex(cols[layer]), hex(0xfff0dc), (1 - d) * 0.3f * (4 - layer) / 3);
            } else if (era == 2) {
                const uint32_t cols[4] = {0, 0x6a5aa0, 0x2a5a5a, 0x1a3a3a};
                c = mixc(hex(cols[layer]), hex(0x10102a), d * 0.6f);
                if (layer == 1 && top < H * 0.45f && y < top + 6 * s) c = hex(0xf0e8ff);   // snow caps
            } else {
                const uint32_t cols[4] = {0, 0x9a6a9a, 0x5a4a7a, 0x2a2a4a};
                c = mixc(hex(cols[layer]), hex(0xffb080), (1 - d) * 0.25f * (4 - layer) / 3);
            }
            p.set(x, y, era <= 2 ? q(c, x, y) : c);
        }
    }
    // layer decorations
    auto blobAt = [&](float cx, float cy, float rx, float ry, Col c, bool shadeIt) {
        for (int y = (int)(cy - ry); y <= cy + ry; ++y)
            for (int x = (int)(cx - rx); x <= cx + rx; ++x) {
                float nx = (x - cx) / rx, ny = (y - cy) / ry;
                if (nx * nx + ny * ny > 1) continue;
                Col o = c;
                if (shadeIt && (-0.4f * nx - 0.7f * ny) < -0.25f) o = mul(c, 0.78f);   // one flat shadow band
                p.set(x, y, era <= 2 ? q(o, x, y) : o);
            }
    };
    auto rectAt = [&](float x0, float y0, float w, float h, Col c) {
        for (int y = (int)y0; y < y0 + h; ++y)
            for (int x = (int)x0; x < x0 + w; ++x) p.set(x, y, era <= 2 ? q(c, x, y) : c);
    };
    if (layer == 2 && g_era == 4) {   // the town, and the railway viaduct out of it
        for (int i = 0; i < 16; ++i) {
            float x = i * W / 16.0f + rnd(i, 3, 44) * 40 * s / 6, w = (22 + rnd(i, 4, 44) * 16) * s, hgt = (20 + rnd(i, 5, 44) * 26) * s;
            float y = H * 0.66f - hgt;
            Col wall = mixc(hex(0xd8b890), hex(0xb07a6a), rnd(i, 6, 44));
            rectAt(x, y, w, hgt + 40 * s, wall);
            rectAt(x, y, w * 0.2f, hgt + 40 * s, mul(wall, 0.82f));   // shaded side
            for (int r = 0; r < (int)(w * 0.55f); ++r)
                rectAt(x - 2 * s + r, y - (r < w * 0.28f ? r : w * 0.55f - r) * 0.8f, 1, (r < w * 0.28f ? r : w * 0.55f - r) * 0.8f + 1,
                       hex(0x9a4a3a));
            for (int wx = 0; wx < 2; ++wx)
                for (int wy = 0; wy < 2; ++wy)
                    rectAt(x + 5 * s + wx * w * 0.45f, y + 6 * s + wy * 10 * s, 4 * s, 5 * s, rnd(i, wx + wy * 2, 45) < 0.5f ? hex(0x3a5a8a) : hex(0xfff0b0));
            if (i == 5) {   // church spire
                rectAt(x + w * 0.35f, y - 30 * s, w * 0.3f, 30 * s, hex(0xc8b8a0));
                for (int r = 0; r < 20 * s; ++r) rectAt(x + w * 0.5f - (20 * s - r) * 0.25f, y - 30 * s - r, (20 * s - r) * 0.5f, 1, hex(0x5a6a8a));
            }
        }
        float vy = H * 0.62f;   // viaduct deck with arches, a train on top
        rectAt(0, vy, (float)W, 5 * s, hex(0x8a6a5a));
        for (int a = 0; a < 24; ++a) {
            float ax = a * W / 24.0f;
            rectAt(ax, vy + 5 * s, 4 * s, 30 * s, hex(0x7a5a4a));
        }
        for (int c = 0; c < 5; ++c) {
            float cx = W * 0.15f + c * 26 * s;
            rectAt(cx, vy - 10 * s, 24 * s, 10 * s, c == 0 ? hex(0x3a6a9a) : hex(0x4a8ab8));
            for (int w = 0; w < 4; ++w) rectAt(cx + 2 * s + w * 5.5f * s, vy - 8 * s, 3.5f * s, 3 * s, hex(0xfff0b0));
        }
    }
    if (layer == 2 && era == 2 && g_era != 4) {   // pine forest
        for (int i = 0; i < 40; ++i) {
            float x = i * W / 40.0f + rnd(i, 1, 2) * 10 * s, hgt = (26 + rnd(i, 2, 2) * 18) * s;
            float baseY = H * 0.7f + ridge(x, W, 7, 6 * s, 3 * s, 1 * s);
            for (int y = 0; y < hgt; ++y) {
                float half = (y / hgt) * 7 * s;
                for (int dx = (int)-half; dx <= half; ++dx)
                    p.set((int)(x + dx), (int)(baseY - hgt + y), q(mixc(hex(0x1a4a3a), hex(0x2a7a5a), dx < 0 ? 0.6f : 0.1f), (int)x + dx, y));
            }
        }
    }
    if (layer == 2 && era == 3 && g_era != 6) {   // a row of houses with lit windows
        for (int i = 0; i < 9; ++i) {
            float x = i * W / 9.0f + rnd(i, 3, 3) * 30, w = (60 + rnd(i, 4, 4) * 40), hgt = (50 + rnd(i, 5, 5) * 40);
            float y = H * 0.74f - hgt;
            rectAt(x, y, w, hgt + 30, mixc(hex(0x4a3a5a), hex(0x7a5a6a), rnd(i, 6, 6)));
            for (int r = 0; r < w * 0.6f; ++r)     // roof
                rectAt(x - 6 + r * 0.5f, y - (w * 0.6f - r) * 0.5f + 0.5f * r - r * 0.5f - 1, w + 12 - r, 1, hex(0x3a2a3a));
            for (int wx = 0; wx < 3; ++wx)
                for (int wy = 0; wy < 2; ++wy)
                    if (rnd(i, wx * 3 + wy, 7) < 0.7f) rectAt(x + 10 + wx * (w - 20) / 3, y + 12 + wy * 28, 10, 14, hex(0xffd080));
        }
    }
    if (layer == 3 && g_era == 1) {   // Gran's garden: a picket fence, flowers and a swing
        float fy = H * 0.86f;
        for (int x = 0; x < W; x += (int)(5 * s)) rectAt((float)x, fy - 9 * s, 3 * s, 10 * s, hex(0xe8e8e0));
        rectAt(0, fy - 7 * s, (float)W, 1.5f * s, hex(0xe8e8e0));
        rectAt(0, fy - 3 * s, (float)W, 1.5f * s, hex(0xe8e8e0));
        for (int i = 0; i < 30; ++i) {
            float x = rnd(i, 1, 61) * W, y = fy + 2 * s + rnd(i, 2, 61) * 6 * s;
            blobAt(x, y, 1.5f * s, 1.5f * s, hex(0xf0f0f0), false);
        }
        for (int k = 0; k < 2; ++k) {   // swing frames
            float x = W * (0.3f + 0.5f * k);
            for (int y = 0; y < 34 * s; ++y) {
                p.set((int)(x - y * 0.3f), (int)(fy - y), q(hex(0x403c48), (int)x, y));
                p.set((int)(x + 30 * s + y * 0.3f), (int)(fy - y), q(hex(0x403c48), (int)x, y));
            }
            rectAt(x - 10 * s, fy - 34 * s, 50 * s, 2 * s, hex(0x403c48));
            rectAt(x + 8 * s, fy - 32 * s, 1, 20 * s, hex(0x807a88));
            rectAt(x + 18 * s, fy - 32 * s, 1, 20 * s, hex(0x807a88));
            rectAt(x + 6 * s, fy - 12 * s, 14 * s, 2 * s, hex(0x5a4a3a));
        }
        return;
    }
    if (layer == 3 && g_era == 4) {   // hedges and street lamps by the station road
        for (int i = 0; i < 18; ++i) {
            float x = (i + 0.5f) * W / 18, baseY = H * 0.86f + ridge(x, W, 11, 3 * s, 2 * s, 1.5f * s);
            blobAt(x, baseY - 4 * s, 12 * s, 7 * s, hex(0x2a7a3a), true);
            blobAt(x + 7 * s, baseY - 6 * s, 8 * s, 6 * s, hex(0x3a9a4a), true);
            if (i % 3 == 0) {
                rectAt(x + 14 * s, baseY - 34 * s, 2 * s, 34 * s, hex(0x2a2a3a));
                blobAt(x + 15 * s, baseY - 35 * s, 3.5f * s, 3 * s, hex(0xffe8a0), false);
            }
        }
        return;
    }
    if (layer >= 2 && g_era == 6) return;   // the 3D stage builds its own trees and houses
    if (layer == 3) {
        int n = era == 0 ? 8 : 14;
        for (int i = 0; i < n; ++i) {
            float x = (i + 0.5f) * W / n + rnd(i, 9, era) * 12 * s;
            float baseY = H * 0.84f + ridge(x, W, 11, 3 * s, 2 * s, 1.5f * s);
            if (era == 0) {   // bare 1-bit tree outlines
                for (int y = 0; y < 18 * s; ++y) p.set((int)x, (int)(baseY - y), ONEBIT_LIGHT);
                for (int a = 0; a < 40; ++a) {
                    float ang = a / 40.0f * 6.2831853f;
                    p.set((int)(x + std::cos(ang) * 7 * s), (int)(baseY - 22 * s + std::sin(ang) * 6 * s), ONEBIT_LIGHT);
                }
            } else if (era == 1) {   // round park trees and a fence
                rectAt(x - 2 * s, baseY - 16 * s, 4 * s, 18 * s, hex(0x8c4a2f));
                blobAt(x, baseY - 22 * s, 10 * s, 9 * s, hex(0x1e8a4c), false);
                blobAt(x - 3 * s, baseY - 25 * s, 4 * s, 3 * s, hex(0x6fdc4a), false);
                for (int f = 0; f < 4; ++f) rectAt(x + 14 * s + f * 4 * s, baseY - 6 * s, 2 * s, 7 * s, hex(0xfaf3e6));
                rectAt(x + 13 * s, baseY - 4 * s, 16 * s, 1 * s, hex(0xfaf3e6));
            } else if (era == 2) {   // bushes, and one treehouse
                blobAt(x, baseY - 4 * s, 9 * s, 6 * s, hex(0x2a6a3a), true);
                blobAt(x + 6 * s, baseY - 6 * s, 6 * s, 5 * s, hex(0x3a8a4a), true);
                if (i == 3) {
                    rectAt(x - 3 * s, baseY - 60 * s, 7 * s, 60 * s, hex(0x6a3a2a));
                    blobAt(x, baseY - 66 * s, 22 * s, 16 * s, hex(0x2a7a3a), true);
                    rectAt(x - 12 * s, baseY - 54 * s, 24 * s, 12 * s, hex(0xa06a3a));
                    for (int r = 0; r < 14 * s; ++r) rectAt(x - 14 * s + r, baseY - 54 * s - r * 0.6f, 28 * s - 2 * r, 1, hex(0x8a3a2a));
                    rectAt(x - 3 * s, baseY - 51 * s, 6 * s, 6 * s, hex(0xf7d73a));
                }
            } else {   // trees and lamp posts at sunset
                rectAt(x - 3, baseY - 40, 6, 42, hex(0x3a2a2a));
                blobAt(x, baseY - 54, 26, 22, hex(0x3a4a3a), true);
                blobAt(x - 8, baseY - 62, 12, 10, hex(0x5a6a4a), true);
                float lx = x + W / n * 0.5f;
                rectAt(lx - 1.5f, baseY - 50, 3, 52, hex(0x2a2a3a));
                blobAt(lx, baseY - 52, 5, 5, hex(0xffe0a0), false);

            }
        }
    }
}

// ------------------------------------------------------------------ Forgetting Wall texture
Image wallTexture(int stage)
{
    int tp = TILE_PX[stage];
    int era = STYLE[stage];
    int W = (int)(WALL_TEX_W * tp), H = (int)(WALL_TEX_H * tp);
    Image img = GenImageColor(W, H, BLANK);
    Color* d = (Color*)img.data;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float fx = (float)x / W;     // 0 = far left (solid), 1 = leading edge
            Col c;
            if (era == 0) {
                c = rnd(x, y, 1) < 0.5f ? ONEBIT_LIGHT : ONEBIT_DARK;
            } else if (era == 1) {
                int bx = x / 4, by = y / 2;
                float r = rnd(bx, by, 2);
                c = hex(r < 0.4f ? 0x0f0d1a : (r < 0.7f ? 0x6a2c6e : (r < 0.9f ? 0x8a7fb4 : 0xfaf3e6)));
                if (stage == 1) c = quantize(c, x, y);
            } else {
                int per = 8;
                float n = vnoise(y / (float)H * per, fx * 3, per, 3) * 0.6f + vnoise(y / (float)H * per * 2, fx * 6, per * 2, 4) * 0.4f;
                n = std::floor(n * 4.0f) / 3.0f;   // four flat tones of fog
                c = mixc(hex(0x1a1030), era == 2 ? hex(0x8a7fb4) : hex(0xb0a8d0), n);
                if (era == 2) c = quantize(c, x, y);
            }
            c.a = 1.0f;
            if (era == 3) c.a = clamp01(1.2f - fx * 0.6f);
            d[(size_t)y * W + x] = toColor(c);
        }
    return img;
}

// ------------------------------------------------------------------ ending pictures
// Four soft 2.5D-style memories, drawn with shapes in pixel units.
void figure(Canvas& c, float x, float y, float sc, bool gran, bool child)
{
    float s = sc * (child ? 0.75f : 1.0f);
    Col top = gran ? hex(0xf08a24) : hex(0x3c8cf0);
    Col hair = gran ? hex(0xdcd8ea) : hex(0x6b3e22);
    if (gran && child) hair = hex(0x6b3e22);   // never used, kept simple
    Col skin = hex(0xf2c29b);
    c.capsule(x - 6 * s, y - 30 * s, x - 7 * s, y, 5 * s, shade(gran ? hex(0x6b4a3a) : hex(0x2b3f6b)));
    c.capsule(x + 6 * s, y - 30 * s, x + 7 * s, y, 5 * s, shade(gran ? hex(0x6b4a3a) : hex(0x2b3f6b)));
    c.rect(x - 14 * s, y - 70 * s, 28 * s, 44 * s, shade(top), 9 * s);
    c.circle(x, y - 84 * s, 15 * s, shade(skin));
    c.ellipse(x, y - 92 * s, 16 * s, 10 * s, shade(hair));
    if (gran) c.circle(x - 4 * s, y - 104 * s, 7 * s, shade(hair));
}

Image endingPicture(int idx)
{
    setStage(5);
    Canvas c((float)ENDING_W, (float)ENDING_H, 1.0f, 2);
    const float W = ENDING_W, H = ENDING_H;
    auto sky = [&](uint32_t a, uint32_t b) {
        c.rect(0, 0, W, H, [&](const Frag& f) { return mixc(hex(a), hex(b), f.y / H); });
    };
    switch (idx) {
    case 0: {   // The first bike ride: park path, Gran holding the seat of a small bike
        sky(0x6ab8f0, 0xd8f0ff);
        c.ellipse(140, 300, 260, 90, shade(hex(0x5aba4a), 0.4f));
        c.ellipse(520, 320, 260, 100, shade(hex(0x4aa83a), 0.4f));
        c.rect(0, 280, W, 80, solid(hex(0x6aba4a)));
        c.poly({{0, 330}, {W, 300}, {W, 320}, {0, 360}}, solid(hex(0xe0c89a)));
        for (int i = 0; i < 5; ++i) {
            float x = 60 + i * 140;
            c.rect(x - 6, 170, 12, 120, shade(hex(0x7a4a2a)));
            c.circle(x, 160, 50, shade(hex(0x3a9a4a), 0.6f));
        }
        // bike
        c.circle(330, 318, 22, [&](const Frag& f) { float d = std::sqrt(f.nx * f.nx + f.ny * f.ny); return d > 0.8f ? hex(0x202020) : withA(hex(0, 0), 0.0f); });
        c.circle(410, 318, 22, [&](const Frag& f) { float d = std::sqrt(f.nx * f.nx + f.ny * f.ny); return d > 0.8f ? hex(0x202020) : withA(hex(0, 0), 0.0f); });
        c.capsule(330, 318, 370, 290, 3, solid(hex(0xe8364f)));
        c.capsule(370, 290, 410, 318, 3, solid(hex(0xe8364f)));
        c.capsule(370, 290, 360, 270, 3, solid(hex(0xe8364f)));
        c.capsule(405, 280, 410, 318, 3, solid(hex(0xe8364f)));
        figure(c, 375, 300, 0.8f, false, true);
        figure(c, 300, 330, 1.2f, true, false);
        c.capsule(312, 260, 355, 275, 5, shade(hex(0xf08a24)));
        break;
    }
    case 1: {   // The treehouse
        sky(0xe08a6a, 0xffd8a0);
        c.rect(0, 290, W, 70, solid(hex(0x3a7a3a)));
        c.rect(300, 80, 40, 230, shade(hex(0x6a3a2a)));
        c.circle(320, 90, 150, shade(hex(0x2a7a3a), 0.5f));
        c.circle(220, 130, 80, shade(hex(0x3a8a4a), 0.5f));
        c.circle(430, 120, 90, shade(hex(0x3a8a4a), 0.5f));
        c.rect(250, 140, 140, 70, shade(hex(0xb07a4a), 0.4f), 4);
        c.poly({{240, 142}, {320, 90}, {400, 142}}, shade(hex(0x8a3a2a)));
        c.rect(300, 160, 30, 30, solid(hex(0xffd860)));
        for (int i = 0; i < 6; ++i) c.rect(350, 210 + i * 14, 24, 4, solid(hex(0x8a5a3a)));
        c.capsule(350, 210, 350, 300, 2, solid(hex(0x8a5a3a)));
        c.capsule(374, 210, 374, 300, 2, solid(hex(0x8a5a3a)));
        figure(c, 300, 205, 0.6f, false, true);
        figure(c, 420, 300, 1.1f, true, false);
        break;
    }
    case 2: {   // The phone call: two windows, far apart, the same moon
        sky(0x1a1a4a, 0x4a3a7a);
        c.circle(320, 70, 34, shade(hex(0xfff6d0), 0.3f));
        for (int side = 0; side < 2; ++side) {
            float x0 = side ? 360 : 40;
            c.rect(x0, 110, 240, 220, shade(hex(side ? 0x5a4a6a : 0x6a4a3a), 0.3f), 6);
            c.rect(x0 + 20, 130, 200, 140, solid(hex(0xffd890)));
            c.rect(x0 + 116, 130, 8, 140, solid(hex(side ? 0x5a4a6a : 0x6a4a3a)));
            figure(c, x0 + 120, 268, 0.95f, side == 0, false);
            c.rect(x0 + 128, 172, 10, 18, solid(hex(0x202030)), 3);
        }
        for (int i = 0; i < 20; ++i) c.dot(330 + std::sin(i * 0.9f) * 6, 110 + i * 10.0f, hex(0xfff6d0));
        break;
    }
    default: {   // Home: the house at sunset, two figures at the door
        sky(0xf08a5a, 0xffd8a0);
        c.circle(480, 120, 50, solid(hex(0xfff0c0)));
        c.rect(0, 280, W, 80, solid(hex(0x4a7a3a)));
        c.rect(180, 150, 280, 160, shade(hex(0xd8c8b0), 0.3f));
        c.poly({{160, 155}, {320, 60}, {480, 155}}, shade(hex(0x8a3a3a), 0.5f));
        c.rect(290, 220, 60, 90, solid(hex(0x6a3a2a)));
        c.rect(300, 230, 40, 70, solid(hex(0xffd060)));
        c.rect(205, 180, 50, 40, solid(hex(0xffd890)));
        c.rect(385, 180, 50, 40, solid(hex(0xffd890)));
        c.poly({{290, 310}, {350, 310}, {420, 360}, {220, 360}}, solid(hex(0xd8c09a)));
        figure(c, 300, 340, 1.0f, true, false);
        figure(c, 345, 340, 1.05f, false, false);
        c.capsule(312, 290, 333, 290, 5, solid(hex(0xf2c29b)));   // holding hands
        break;
    }
    }
    // soft vignette
    c.rect(0, 0, W, H, [&](const Frag& f) {
        float dx = f.x / W - 0.5f, dy = f.y / H - 0.5f;
        return withA(hex(0x100818), clamp01((dx * dx + dy * dy) * 1.6f - 0.15f));
    });
    Image img = GenImageColor(ENDING_W, ENDING_H, BLANK);
    blit(c, img, 0, 0, false);
    return img;
}

std::string eraDir(const std::string& base, int era) { return base + "/art/era" + std::to_string(era) + "/"; }

}  // namespace

int generateSprites(const std::string& dir)
{
    int n = 0;
    auto save = [&](Image img, const std::string& path) {
        ExportImage(img, path.c_str());
        UnloadImage(img);
        ++n;
    };
    for (int era = 0; era < ERA_COUNT; ++era) {
        setStage(era);
        std::string d = eraDir(dir, era);
        for (int who = 0; who < 2; ++who)
            save(makeSheet(PF_COUNT, PLAYER_FW, PLAYER_FH, true, [&](Canvas& c, int f) { drawPlayer(c, who, f); }),
                 d + (who == 0 ? "player_p1.png" : "player_p2.png"));
        save(makeSheet(BF_COUNT, BLOB_FS, BLOB_FS, true, drawBlob), d + "enemy_blob.png");
        save(makeSheet(FF_COUNT, FLYER_FS, FLYER_FS, true, drawFlyer), d + "enemy_flyer.png");
        save(makeSheet(RF_COUNT, BRUTE_FS, BRUTE_FS, true, drawBrute), d + "enemy_brute.png");
        save(makeSheet(XF_COUNT, BOSS_FS, BOSS_FS, true, drawBoss), d + "enemy_boss.png");
        // tiles: the solid ones get no outline so they join seamlessly; pickups get one
        {
            int tp = TILE_PX[era];
            Image sheet = GenImageColor(tp * T_COUNT, tp, BLANK);
            for (int t = 0; t < T_COUNT; ++t) {
                Canvas cv(1, 1, (float)tp, SUPER[era]);
                drawTile(cv, t);
                bool outline = t >= T_P1 && t != T_WALL;
                blit(cv, sheet, t * tp, 0, outline);
            }
            save(sheet, d + "tiles.png");
        }
        for (int l = 0; l < BG_LAYERS; ++l) {
            Px p(bgW(era), BG_H[era]);
            bgLayer(p, STYLE[era], l);
            save(p.img, d + "bg_" + std::to_string(l) + ".png");
        }
        save(wallTexture(era), d + "wall.png");
    }
    for (int i = 0; i < ENDING_COUNT; ++i) save(endingPicture(i), dir + "/art/ending_" + std::to_string(i) + ".png");
    return n;
}
