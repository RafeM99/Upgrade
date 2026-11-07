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

// The 32-colour palette of the 16-bit stage: hand-picked ramps (each colour has a cooler,
// darker step and a warmer, lighter step) so shading shifts hue instead of only darkening.
const uint32_t PALETTE32[32] = {
    0x140f1e, 0x2a2140, 0x463a66, 0x6e6a9e, 0xa8a4d0, 0xf4f0e6,   // night to white (outlines, fog)
    0x2a3a6a, 0x3a6ab0, 0x5aa0e0, 0x9ad0f0,                       // sky blues
    0x1a3a2a, 0x2a6a3a, 0x4a9a3a, 0x8ac84a, 0xd0e870,             // greens to spring yellow
    0x3a2018, 0x6a3a22, 0x9a5a30, 0xc88a4a, 0xf0c070,             // wood and earth to sand
    0x5a1a2a, 0xa02a3a, 0xe04a3a, 0xf08a4a, 0xf8d070,             // reds to warm light
    0x7a3a5a, 0xc06a8a, 0xf0a0b0,                                 // dusk pinks
    0x6a4a3a, 0xc89a7a, 0xf0c8a0,                                 // skin
    0x4a7a9a};
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
    float tk = 1, tox = 0, toy = 0;   // drawing transform: scale about (tox, toy) offsets, for smaller figures

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
        x0 = x0 * tk + tox; x1 = x1 * tk + tox; y0 = y0 * tk + toy; y1 = y1 * tk + toy;
        int sx0 = std::max(0, (int)std::floor(x0 * ppu)), sy0 = std::max(0, (int)std::floor(y0 * ppu));
        int sx1 = std::min(w - 1, (int)std::ceil(x1 * ppu)), sy1 = std::min(h - 1, (int)std::ceil(y1 * ppu));
        for (int sy = sy0; sy <= sy1; ++sy)
            for (int sx = sx0; sx <= sx1; ++sx) {
                Frag f;
                f.x = ((sx + 0.5f) / ppu - tox) / tk;
                f.y = ((sy + 0.5f) / ppu - toy) / tk;
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
        int tx = (int)std::floor((x * tk + tox) * tpu), ty = (int)std::floor((y * tk + toy) * tpu);
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
        // shadows lean blue-purple, highlights lean yellow: hue-shifted ramps, one light from top-left
        if (d < 0.32f && strength > 0.2f) o = mixc(mul(c, 0.78f), hex(0x3a2a7a), 0.28f);
        else if (g_era >= 4 && d > 0.86f && strength > 0.5f) o = mixc(c, hex(0xfff0a0), 0.3f);
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
        // 1-bit: solid shapes, no grain. Dither only where a pattern is drawn on purpose.
        float l = lum(c);
        bool light = l >= 0.42f;
        (void)ix; (void)iy;
        Col o = light ? ONEBIT_LIGHT : ONEBIT_DARK;
        o.a = c.a;
        return o;
    }
    if (g_era == 1) return shade4(c, ix, iy);
    if (g_era == 2) return nearestPalette(c);
    if (g_era == 3) {
        int best = 0;
        float bd = 1e9f;
        for (int i = 0; i < 32; ++i) {
            Col p = hex(PALETTE32[i]);
            float dr = p.r - c.r, dg = p.g - c.g, db = p.b - c.b;
            float d = dr * dr * 0.3f + dg * dg * 0.59f + db * db * 0.11f;
            if (d < bd) { bd = d; best = i; }
        }
        Col o = hex(PALETTE32[best]);
        o.a = c.a;
        return o;
    }
    // 32-bit and later: 32 levels per channel
    auto q = [](float v) { return std::round(clamp01(v) * 31.0f) / 31.0f; };
    return {q(c.r), q(c.g), q(c.b), c.a};
}

Col tone(int i);
int toneOf(Col c);

// Downsample a canvas into the sheet image at (ox, oy) in pixels.
void blit(const Canvas& cv, Image& sheet, int ox, int oy, bool outline)
{
    int tw = cv.w / cv.ss, th = cv.h / cv.ss;
    std::vector<Col> out((size_t)tw * th);
    for (int y = 0; y < th; ++y)
        for (int x = 0; x < tw; ++x) {
            // Every stage is pixel art: a pixel is covered or not, and takes the colour most of
            // its samples have. Colours are never averaged, so edges never blend.
            float wa = 0;
            Col pick{};
            int pickN = 0;
            for (int sy = 0; sy < cv.ss; ++sy)
                for (int sx = 0; sx < cv.ss; ++sx) {
                    const Col& s = cv.px[(size_t)(y * cv.ss + sy) * cv.w + x * cv.ss + sx];
                    wa += s.a;
                    if (s.a < 0.5f) continue;
                    int same = 0;
                    for (int ty = 0; ty < cv.ss; ++ty)
                        for (int tx = 0; tx < cv.ss; ++tx) {
                            const Col& t = cv.px[(size_t)(y * cv.ss + ty) * cv.w + x * cv.ss + tx];
                            if (t.a >= 0.5f && t.r == s.r && t.g == s.g && t.b == s.b) ++same;
                        }
                    if (same > pickN) { pickN = same; pick = s; }
                }
            float n = (float)(cv.ss * cv.ss);
            Col c{};
            if (wa / n >= 0.5f && pickN > 0) {
                c = pick;
                c.a = 1.0f;
                // the 4-shade stage: exactly one of the four greys, no checker between them
                c = g_era == 1 ? withA(tone(toneOf(c)), 1.0f) : quantize(c, x, y);
            }
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

// ------------------------------------------------------------------ sprite helpers
// Character, enemy and pickup sprites follow one set of rules:
//   * one light, from the top-left
//   * flat colour bands, never a gradient: shadows lean blue-purple, lit edges lean warm yellow
//   * each stage has its own colour limit (1-bit, four greys, 16 colours, 32 colours, 15-bit)
//   * a 1 px outline (dark in the early stages, a deep tint of the colour inside from 32-bit on)

// How much a point on a shape faces the light: about 1 at the top-left edge, 0 or less at the
// bottom-right edge.
float lightAt(const Frag& f)
{
    float nz = std::sqrt(std::max(0.0f, 1 - f.nx * f.nx * 0.8f - f.ny * f.ny * 0.8f));
    return f.nx * -0.5f + f.ny * -0.6f + nz * 0.62f;
}
Col coolShadow(Col c, float k = 1.0f)
{
    Col o = mixc(mul(c, 1 - 0.26f * k), hex(0x2a2266), 0.16f + 0.14f * k);
    o.a = c.a;
    return o;
}
Col warmLight(Col c)
{
    Col o = mixc(c, hex(0xfff0a8), 0.4f);
    o.a = c.a;
    return o;
}

// The four greys of the 4-shade stage, and which one a colour is.
Col tone(int i) { return hex(SHADES4[std::max(0, std::min(3, i))]); }
int toneOf(Col c)
{
    int best = 0;
    float bd = 1e9f;
    for (int i = 0; i < 4; ++i) {
        float d = std::fabs(lum(tone(i)) - lum(c));
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

// A sprite colour: the normal colour, one of the four greys (4-shade), or light/dark (1-bit).
Col pc(uint32_t normal, int tone4)
{
    if (g_era == 0) return tone4 >= 2 ? ONEBIT_LIGHT : ONEBIT_DARK;
    if (g_era == 1) return tone(tone4);
    return hex(normal);
}
// The same colour one step further from the light: for limbs on the far side of the body.
Col farSide(Col c)
{
    if (g_era == 0) return c;
    if (g_era == 1) return tone(std::max(0, toneOf(c) - 1));
    return coolShadow(c, 0.8f);
}

// Cel shading for sprites. Hard bands only.
//   4-shade: one flat grey per part.               8-bit: colour and shadow (3 colours with the outline).
//   16-bit: shadow, colour, a warm lit edge.        32-bit on: deep shadow, shadow, colour, lit edge.
Fill lit(Col c, bool litEdge = true)
{
    return [c, litEdge](const Frag& f) {
        if (g_era == 0) return c;
        float d = lightAt(f);
        float r2 = f.nx * f.nx + f.ny * f.ny;
        if (g_era == 1) return c;   // flat greys: the silhouette and the outline do the work
        if (g_era == 2) return d < 0.2f ? coolShadow(c) : c;
        if (g_era == 3) {
            if (d < 0.22f) return coolShadow(c);
            if (litEdge && d > 0.8f && r2 > 0.45f) return warmLight(c);
            return c;
        }
        if (d < -0.02f) return coolShadow(c, 1.9f);
        if (d < 0.3f) return coolShadow(c);
        if (litEdge && d > 0.78f && r2 > 0.62f) return warmLight(c);
        return c;
    };
}

// Faces stay lit (they look the way the character runs): only a thin shadow under the jaw.
Fill litFace(Col c)
{
    return [c](const Frag& f) {
        if (g_era == 0) return c;
        if (g_era >= 4 && f.ny > 0.66f && f.nx > -0.1f) return coolShadow(c, 0.6f);
        if (g_era >= 3 && f.nx < -0.55f && f.ny < -0.2f) return warmLight(c);
        return c;
    };
}

// 1-bit ordered dither for big areas (the brute's chest, the boss): light where the Bayer
// threshold is under `density`.
Fill dither1(float density)
{
    return [density](const Frag& f) { return bayer4f(f.ix, f.iy) < density ? ONEBIT_LIGHT : ONEBIT_DARK; };
}

// Hand-placed pixels for the tiny 1-bit sprites: '#' light, 'o' dark, ':' checker dither,
// anything else clear. (ox, oy) is in target pixels.
using Rows = std::vector<std::string>;
void stamp(Canvas& c, const Rows& rows, int ox, int oy)
{
    for (int y = 0; y < (int)rows.size(); ++y)
        for (int x = 0; x < (int)rows[y].size(); ++x) {
            char ch = rows[y][x];
            int X = ox + x, Y = oy + y;
            Col col;
            if (ch == '#') col = ONEBIT_LIGHT;
            else if (ch == 'o') col = ONEBIT_DARK;
            else if (ch == ':') col = ((X + Y) & 1) ? ONEBIT_DARK : ONEBIT_LIGHT;
            else continue;
            for (int sy = 0; sy < c.ss; ++sy)
                for (int sx = 0; sx < c.ss; ++sx) c.put(X * c.ss + sx, Y * c.ss + sy, col);
        }
}
Rows cat(std::initializer_list<Rows> parts)
{
    Rows r;
    for (const auto& p : parts) r.insert(r.end(), p.begin(), p.end());
    return r;
}
Rows shiftRight(Rows r, int n)
{
    for (auto& s : r) s = std::string(n, '.') + s.substr(0, s.size() - n);
    return r;
}

// ------------------------------------------------------------------ players
struct Pose {
    float bob = 0, lean = 0;
    Vector2 footF{0.6f, 1.45f}, footB{0.4f, 1.45f};
    Vector2 handF{0.66f, 1.0f}, handB{0.34f, 1.0f};
};

Pose playerPose(int frame)
{
    Pose p;
    switch (frame) {
    case PF_IDLE1: p.bob = 0.03f; p.handF.y += 0.02f; p.handB.y += 0.02f; break;
    case PF_RUN0: case PF_RUN1: case PF_RUN2: case PF_RUN3: {
        float ph = (frame - PF_RUN0) * 1.5707963f + 0.6f;
        float s = std::sin(ph), c = std::cos(ph);
        p.lean = 0.05f;
        p.bob = -0.035f * std::fabs(s);
        p.footF = {0.52f + 0.25f * s, 1.45f - 0.13f * std::max(0.0f, c)};
        p.footB = {0.52f - 0.25f * s, 1.45f - 0.13f * std::max(0.0f, -c)};
        p.handF = {0.56f - 0.22f * s, 0.98f};
        p.handB = {0.56f + 0.22f * s, 0.98f};
        break;
    }
    case PF_JUMP: p.bob = -0.04f; p.footF = {0.66f, 1.3f}; p.footB = {0.4f, 1.38f};
        p.handF = {0.8f, 0.56f}; p.handB = {0.24f, 0.62f}; break;
    case PF_FALL: p.footF = {0.64f, 1.46f}; p.footB = {0.38f, 1.42f};
        p.handF = {0.86f, 0.74f}; p.handB = {0.16f, 0.76f}; break;
    case PF_DASH: p.lean = 0.14f; p.footF = {0.76f, 1.42f}; p.footB = {0.22f, 1.36f};
        p.handF = {0.36f, 0.95f}; p.handB = {0.2f, 0.9f}; break;
    case PF_SPECIAL: p.bob = -0.02f; p.footF = {0.58f, 1.46f}; p.footB = {0.42f, 1.43f};
        p.handF = {0.58f, 0.44f}; p.handB = {0.3f, 0.52f}; break;
    case PF_POUND: p.bob = 0.12f; p.footF = {0.66f, 1.38f}; p.footB = {0.34f, 1.38f};
        p.handF = {0.74f, 1.2f}; p.handB = {0.26f, 1.2f}; break;
    case PF_WALL: p.lean = 0.06f; p.footF = {0.74f, 1.38f}; p.footB = {0.5f, 1.46f};
        p.handF = {0.9f, 0.7f}; p.handB = {0.4f, 1.0f}; break;
    case PF_ATTACK: p.lean = 0.08f; p.footF = {0.7f, 1.45f}; p.footB = {0.32f, 1.45f};
        p.handF = {0.97f, 0.84f}; p.handB = {0.3f, 0.95f}; break;
    default: break;
    }
    return p;
}

// 1-bit Gran and Sam: 8 x 12 pixels, hand placed. Solid light figures with a dark halo (the
// sheet outline), so they read against the line-drawn 1-bit scenery.
void drawPlayer1bit(Canvas& c, bool gran, int frame)
{
    // Gran: bun, glasses-less young-ish face, cardigan, skirt.  Sam: a small child in a cap.
    static const Rows gHead = {"..##....", ".#####..", ".##o##..", "...###.."};
    static const Rows sHead = {"..###...", ".######.", "..#o##..", "..###..."};
    static const Rows gDown = {".#####..", "#.###.#.", "#.###.#.", "..###...", ".#####.."};
    static const Rows gRun = {".#####..", "#.###..#", "#.###...", "..###...", ".#####.."};
    static const Rows gUp = {"#######.", "..###...", "..###...", "..###...", ".#####.."};
    static const Rows gFwd = {".#####..", "..######", "..###...", "..###...", ".#####.."};
    static const Rows gBack = {"..#####.", ".####.#.", "#.###...", "..###...", ".#####.."};
    static const Rows gWall = {".#####.#", "#.####..", "#.###...", "..###...", ".#####.."};
    static const Rows gStand = {".#####..", "..#.#...", ".##.##.."};
    static const Rows gWide = {".#####..", ".#...#..", "##...##."};
    static const Rows gPass = {".#####..", "..#.#...", "..####.."};
    static const Rows gTuck = {".#####..", ".##.##.."};
    static const Rows gSquat = {"##...##."};
    static const Rows sDown = {".#####..", "#.###.#.", "..###..."};
    static const Rows sUp = {".#####..", "..###...", "..###..."};
    static const Rows sFwd = {"..###...", ".#######", "..###..."};
    static const Rows sBack = {"..####..", "#####...", "..###..."};
    static const Rows sWall = {".####.#.", "#.####..", "..###..."};
    static const Rows sStand = {"..#.#...", ".##.##.."};
    static const Rows sWide = {".#...#..", "##...##."};
    static const Rows sPass = {"..##....", "..###..."};
    static const Rows sTuck = {".##.##.."};
    static const Rows sSquat = {"##...##."};
    const int H = 12;

    if (frame == PF_SPECIAL && gran) {   // gliding under her umbrella
        stamp(c, {"..####..", ".######.", "########", "..##...#", ".#####.#", ".##o####", "...###.#",
                  ".######.", "#.###...", "..###...", ".#####..", "..#.#..."}, 0, 0);
        return;
    }
    const Rows& head = gran ? gHead : sHead;
    Rows body = gran ? gDown : sDown, legs = gran ? gStand : sStand;
    int bob = 0, headDx = 0;
    bool headDown = false;
    switch (frame) {
    case PF_IDLE1: headDown = true; break;
    case PF_RUN0: case PF_RUN2: body = gran ? gRun : sDown; legs = gran ? gWide : sWide; break;
    case PF_RUN1: case PF_RUN3: legs = gran ? gPass : sPass; bob = -1; break;
    case PF_JUMP: body = gran ? gUp : sUp; legs = gran ? gTuck : sTuck; bob = -1; break;
    case PF_FALL: body = gran ? gUp : sUp; legs = gran ? gWide : sWide; break;
    case PF_DASH: body = gran ? gBack : sBack; legs = gran ? gWide : sWide; headDx = 1; break;
    case PF_SPECIAL: body = sUp; break;   // Sam's time pulse: arms up, sparks round him
    case PF_POUND: legs = gran ? gSquat : sSquat; break;
    case PF_WALL: body = gran ? gWall : sWall; break;
    case PF_ATTACK: body = gran ? gFwd : sFwd; legs = gran ? gWide : sWide; break;
    default: break;
    }
    Rows lower = cat({body, legs});
    int total = (int)(head.size() + lower.size());
    int oy = H - total + bob;
    if (frame == PF_JUMP) oy = H - total - 1;
    stamp(c, lower, 0, oy + (int)head.size());
    stamp(c, headDx ? shiftRight(head, headDx) : head, 0, oy + (headDown ? 1 : 0));
    if (frame == PF_JUMP || frame == PF_FALL || (frame == PF_SPECIAL && !gran))
        stamp(c, {"#.....#."}, 0, oy + (int)head.size() - 2 + (gran ? 0 : 1));   // hands up by the head
    if (frame == PF_SPECIAL && !gran) {
        stamp(c, {"#......#"}, 0, oy - 1);
        stamp(c, {"#......#"}, 0, H - 1);
    }
    if (frame == PF_ATTACK) stamp(c, {"#", ".", "#"}, 7, oy + (int)head.size());
}

void drawPlayer(Canvas& c, int who, int frame)
{
    // who 0 = Player 1 (Gran, warm orange). who 1 = Player 2 (Sam, cool blue).
    const bool gran = who == 0;
    if (g_era == 0) { drawPlayer1bit(c, gran, frame); return; }
    const int e = g_era;
    Pose p = playerPose(frame);
    // Gran's hair greys and she stoops a little more each stage; Sam grows from a small child
    // (4-shade) through a teenager (16-bit) to a grown man (2.5D).
    const uint32_t granHair[ERA_COUNT] = {0, 0x5a4a44, 0x5a5468, 0x9a92a0, 0xc4c0d0, 0xe0dcea, 0xe8e6f0};
    const float granHead[ERA_COUNT] = {0, 0.27f, 0.26f, 0.235f, 0.215f, 0.2f, 0.2f};
    const float stoop[ERA_COUNT] = {0, 0, 0.015f, 0.03f, 0.045f, 0.06f, 0.06f};
    const float samSize[ERA_COUNT] = {0, 0.78f, 0.8f, 0.88f, 0.95f, 1.0f, 1.0f};

    Col skin = pc(gran ? 0xf2c29b : 0xe9b58c, 3);
    Col top = gran ? pc(0xf08a24, 1) : pc(0x3c8cf0, 2);
    Col lower = gran ? pc(0x7a4a3a, 2) : pc(0x2b3f6b, 1);     // skirt / jeans
    Col legs = gran ? pc(0x5a4048, 1) : lower;                 // stockings / jeans
    Col shoes = gran ? pc(0x3a2422, 0) : pc(0xf4f4f4, 3);
    Col hair = gran ? pc(granHair[e], 2) : pc(0x6b3e22, 0);
    Col cap = pc(0x22c4b0, 1);
    Col ink = pc(0x1c1420, 0);
    if (e == 2) { hair = gran ? hex(0x5a5468) : hex(0x8c4a2f); cap = hex(0x1e8a4c); }
    if (e == 3) cap = hex(0x4a9a3a);

    float hr = granHead[e], st = gran ? stoop[e] : 0.0f;
    if (!gran) {
        float k = samSize[e];
        c.tk = k;
        c.tox = 0.5f * (1 - k);
        c.toy = 1.49f * (1 - k);
        hr = granHead[e] * (1.0f + (1.0f - k) * 0.9f);   // children have bigger heads for their size
        if (e >= 5) hr = 0.19f;
    }
    const int tp = TILE_PX[e];
    const int eyePx = tp >= 64 ? 3 : (tp >= 32 ? 2 : 1);

    Vector2 hip{0.5f + p.lean * 0.3f, 1.0f + p.bob};
    Vector2 sh{0.5f + p.lean * 0.8f + st, 0.68f + p.bob + st * 0.5f};
    float hx = 0.53f + p.lean + st * 1.5f, hy = sh.y - hr * 0.95f - 0.02f + st * 0.3f;
    if (frame == PF_POUND) hy += 0.02f;

    // dash: three hard speed lines behind
    if (frame == PF_DASH)
        for (int i = 0; i < 3; ++i)
            c.rect(0.0f + i * 0.04f, 0.74f + i * 0.16f, 0.16f, 0.035f, solid(i == 1 ? top : farSide(top)));

    // Gran's umbrella (glide), behind her
    if (frame == PF_SPECIAL && gran) {
        Col canopy = pc(0xb03a6a, 1);
        c.capsule(p.handF.x, p.handF.y, p.handF.x, 0.14f, 0.022f, solid(pc(0x3a2a2a, 0)));
        c.poly({{0.04f, 0.24f}, {0.18f, 0.08f}, {0.5f, 0.0f}, {0.82f, 0.08f}, {0.98f, 0.24f},
                {0.82f, 0.2f}, {0.66f, 0.24f}, {0.5f, 0.2f}, {0.34f, 0.24f}, {0.18f, 0.2f}}, lit(canopy));
    }
    // Sam's time pulse ring
    if (frame == PF_SPECIAL && !gran) {
        Col ring = pc(0x7fe8ff, 2);
        c.shape(0, 0, 1, 1.5f, [&](Frag& f) {
            float dx = f.x - 0.5f, dy = (f.y - 0.85f) * 0.75f;
            float d = std::sqrt(dx * dx + dy * dy);
            f.nx = dx; f.ny = dy;
            return d > 0.43f && d < 0.49f;
        }, solid(ring));
    }

    const float limb = 0.085f, arm = 0.068f;
    auto shoe = [&](Vector2 ft, Col col) {
        c.rect(ft.x - 0.09f, ft.y - 0.07f, 0.22f, 0.1f, lit(col, false), 0.045f);
    };
    // far arm and far leg
    c.capsule(sh.x - 0.05f, sh.y + 0.06f, p.handB.x, p.handB.y, arm, lit(farSide(top)));
    c.circle(p.handB.x, p.handB.y, 0.065f, lit(farSide(skin)));
    c.capsule(hip.x - 0.07f, hip.y, p.footB.x, p.footB.y - 0.06f, limb, lit(farSide(legs)));
    shoe(p.footB, farSide(shoes));

    // body
    if (gran) {
        c.capsule(hip.x, hip.y - 0.1f, sh.x, sh.y + 0.1f, 0.21f, lit(top));
        c.poly({{hip.x - 0.2f, hip.y - 0.08f}, {hip.x + 0.2f, hip.y - 0.08f}, {hip.x + 0.26f, hip.y + 0.2f},
                {hip.x - 0.26f, hip.y + 0.2f}}, lit(lower));
        if (e >= 2) {   // cardigan buttons
            Col btn = pc(0xfaf3e6, 3);
            c.dot(sh.x + 0.13f, sh.y + 0.08f, btn);
            c.dot(sh.x + 0.12f, sh.y + 0.2f, btn);
        }
    } else {
        c.capsule(hip.x - 0.02f, hip.y + 0.04f, hip.x + 0.06f, hip.y + 0.04f, 0.13f, lit(lower));   // seat of the jeans
        c.circle(sh.x - 0.13f, sh.y + 0.02f, 0.1f, lit(farSide(top)));   // hood
        c.capsule(hip.x, hip.y - 0.08f, sh.x, sh.y + 0.1f, 0.2f, lit(top));
        if (e >= 3) c.rect(hip.x - 0.08f, hip.y - 0.13f, 0.24f, 0.08f, solid(farSide(top)), 0.02f);   // front pocket
        if (e >= 4) { c.dot(sh.x + 0.08f, sh.y + 0.1f, pc(0xfaf3e6, 3)); c.dot(sh.x + 0.12f, sh.y + 0.14f, pc(0xfaf3e6, 3)); }
    }

    // near leg
    c.capsule(hip.x + 0.06f, hip.y, p.footF.x, p.footF.y - 0.06f, limb, lit(legs));
    shoe(p.footF, shoes);

    // head
    c.circle(hx, hy, hr, litFace(skin));
    float ex = hx + hr * 0.5f, ey = hy + hr * 0.1f;
    if (gran) {
        c.ellipse(hx - hr * 0.64f, hy + hr * 0.02f, hr * 0.42f, hr * 0.66f, lit(hair));   // back of the hair
        c.ellipse(hx - hr * 0.32f, hy - hr * 0.66f, hr * 0.8f, hr * 0.44f, lit(hair));    // top
        c.circle(hx - hr * 0.62f, hy - hr * 0.98f, hr * 0.36f, lit(hair));                // bun
    } else {
        c.ellipse(hx - hr * 0.64f, hy + hr * 0.0f, hr * 0.42f, hr * 0.58f, lit(hair));
        c.rect(hx - hr * 1.02f, hy - hr * 1.12f, hr * 1.86f, hr * 0.7f, lit(cap), hr * 0.32f);   // cap
        c.rect(hx + hr * 0.3f, hy - hr * 0.6f, hr * 0.98f, hr * 0.24f, solid(farSide(cap)), hr * 0.1f);   // brim
        if (e >= 5) c.rect(hx + hr * 0.05f, hy + hr * 0.62f, hr * 0.5f, hr * 0.2f, solid(farSide(hair)), hr * 0.08f);   // stubble
    }
    // face: one dark eye, then glasses for Gran, a mouth and cheek from 16-bit on
    if (gran && e >= 3) {   // glasses: a thin rim over the eye and the arm back to the ear
        Col frameC = pc(0x3a2a3a, 0);
        c.rect(ex - 1.0f / tp, ey - (eyePx + 1.0f) / tp, (eyePx + 2.0f) / tp, 1.0f / tp, solid(frameC));
        c.rect(hx - hr * 0.3f, ey - (eyePx + 1.0f) / tp, hr * 0.62f, 1.0f / tp, solid(frameC));
    }
    c.dot(ex, ey, ink, eyePx);
    if (e >= 3) c.dot(hx + hr * 0.55f, hy + hr * 0.6f, pc(0x9a4a3a, 1), std::max(1, eyePx - 1));
    if (e >= 4) c.dot(hx + hr * 0.1f, hy + hr * 0.42f, pc(gran ? 0xf08a7a : 0xe8907a, 2), eyePx);

    // near arm
    c.capsule(sh.x + 0.05f, sh.y + 0.06f, p.handF.x, p.handF.y, arm, lit(top));
    c.circle(p.handF.x, p.handF.y, 0.065f, lit(skin));

    if (frame == PF_ATTACK) {
        Col spark = pc(0xfff2a0, 3);
        c.poly({{0.98f, 0.64f}, {1.03f, 0.8f}, {1.0f, 0.86f}, {0.84f, 0.84f}, {0.98f, 0.9f}, {0.92f, 1.04f}}, solid(spark));
    }
}

// ------------------------------------------------------------------ enemies
// Creatures of the Forgetting. Each stage has its own take on them:
//   1-bit: hand-placed blocks, dither only on the big ones.   4-shade: blocky, two greys each.
//   8-bit: chunky, colour + shadow + outline.   16-bit: rounder, three-tone.
//   32-bit on: rounded, four-tone, coloured outline and small details (teeth, cracks, feelers).
void drawBlob(Canvas& c, int frame)
{
    if (g_era == 0) {
        static const Rows walk[2] = {
            {"....#...", "...#....", "..####..", ".######.", ".###o#o#", "########", "########", ".##..##."},
            {"........", "...#....", "..#.....", ".######.", "####o#o#", "########", "########", "##....##"}};
        if (frame == BF_SQUASH) stamp(c, {".######.", "###o#o##"}, 0, 6);
        else stamp(c, walk[frame % 2], 0, 0);
        return;
    }
    const int e = g_era;
    Col body = pc(0x8a7fb4, 2), eyeW = pc(0xfaf3e6, 3), pupil = pc(0x0f0d1a, 0), dark = farSide(body);
    if (frame == BF_SQUASH) {
        c.rect(0.06f, 0.78f, 0.88f, 0.2f, lit(body), 0.08f);
        c.rect(0.3f, 0.84f, 0.14f, 0.04f, solid(pupil));
        c.rect(0.6f, 0.84f, 0.14f, 0.04f, solid(pupil));
        return;
    }
    float sq = 0.07f * std::sin(frame * 1.5707963f);
    float step = (frame % 2) ? 0.05f : -0.05f;
    // feet
    c.rect(0.18f + step, 0.86f, 0.2f, 0.12f, lit(dark, false), 0.05f);
    c.rect(0.62f - step, 0.86f, 0.2f, 0.12f, lit(dark, false), 0.05f);
    float ry = 0.36f * (1 - sq), rx = 0.42f * (1 + sq), top = 0.92f - 2 * ry;
    if (e <= 2) {
        // blocky dome: round top corners, flat bottom
        float R = e == 1 ? 0.24f : 0.3f, cx = 0.5f, cyy = top + ry;
        c.shape(cx - rx, top, cx + rx, top + 2 * ry, [&](Frag& f) {
            f.nx = (f.x - cx) / rx;
            f.ny = (f.y - cyy) / ry;
            if (f.x < cx - rx || f.x > cx + rx || f.y < top || f.y > top + 2 * ry) return false;
            if (f.y > top + R) return true;
            float qx = std::max(std::fabs(f.x - cx) - (rx - R), 0.0f), qy = top + R - f.y;
            return qx * qx + qy * qy <= R * R;
        }, lit(body));
    } else {
        c.ellipse(0.5f, 0.92f - ry, rx, ry, lit(body));
        if (e >= 4) {   // patches of static on its skin, and a drip
            c.dot(0.3f, top + ry * 0.8f, dark, 2); c.dot(0.42f, top + ry * 1.4f, dark, 1); c.dot(0.24f, top + ry * 1.3f, dark, 1);
            c.capsule(0.36f, 0.9f, 0.36f, 0.97f, 0.035f, solid(body));
        }
    }
    // antenna of static
    if (e >= 2) {
        c.capsule(0.4f, top + 0.04f, 0.33f, top - 0.1f, 0.025f, solid(dark));
        c.circle(0.32f, top - 0.12f, 0.05f, solid(pc(0xf28cb4, 2)));
    }
    // eyes (looking ahead) and a mouth
    float ey = top + ry * 0.75f;
    if (e <= 2) {
        c.rect(0.46f, ey - 0.12f, 0.18f, 0.25f, solid(eyeW));
        c.rect(0.7f, ey - 0.12f, 0.16f, 0.25f, solid(eyeW));
        c.rect(0.565f, ey - 0.05f, 0.07f, 0.13f, solid(pupil));
        c.rect(0.79f, ey - 0.05f, 0.07f, 0.13f, solid(pupil));
    } else {
        c.ellipse(0.6f, ey, 0.1f, 0.12f, solid(eyeW));
        c.ellipse(0.8f, ey, 0.08f, 0.1f, solid(eyeW));
        c.dot(0.64f, ey, pupil, e >= 4 ? 2 : 1);
        c.dot(0.82f, ey, pupil, e >= 4 ? 2 : 1);
        c.capsule(0.62f, ey + 0.2f, 0.8f, ey + 0.18f, 0.03f, solid(pc(0x26305e, 0)));
        if (e >= 4) c.dot(0.7f, ey + 0.2f, eyeW);
    }
}

void drawFlyer(Canvas& c, int frame)
{
    if (g_era == 0) {
        static const Rows flap[4] = {
            {"#......#", "##....##", ".##..##.", "..####..", "..##o#..", "...##...", "........", "........"},
            {"........", "........", "###..###", ".######.", "..##o#..", "...##...", "........", "........"},
            {"........", "........", "...##...", "..####..", ".###o##.", "##.##.##", "#......#", "........"},
            {"........", "........", "###..###", ".######.", "..##o#..", "...##...", "........", "........"}};
        if (frame == FF_HURT) stamp(c, {"........", "#......#", ".#.##.#.", "..####..", ".#o##o#.", "..####..", "...##...", "........"}, 0, 0);
        else stamp(c, flap[frame % 4], 0, 0);
        return;
    }
    const int e = g_era;
    Col body = pc(0xe8364f, 2), wing = pc(0x6a2c6e, 1), membrane = pc(0xb05aa0, 2);
    Col eye = pc(0xfaf3e6, 3), pupil = pc(0x0f0d1a, 0);
    if (frame == FF_HURT) { body = pc(0xfaf3e6, 3); wing = pc(0xf7d73a, 2); membrane = wing; }
    float flap = frame == FF_HURT ? 0.0f : std::sin(frame * 1.5707963f);
    float wy = 0.48f - 0.3f * flap;
    // wings: two torn-paper sails
    c.poly({{0.42f, 0.5f}, {0.02f, wy - 0.08f}, {0.08f, wy + 0.08f}, {0.2f, wy + 0.16f}}, lit(wing, false));
    c.poly({{0.58f, 0.5f}, {0.98f, wy - 0.08f}, {0.92f, wy + 0.08f}, {0.8f, wy + 0.16f}}, lit(wing, false));
    if (e >= 3) {
        c.poly({{0.4f, 0.5f}, {0.12f, wy - 0.02f}, {0.22f, wy + 0.1f}}, solid(membrane));
        c.poly({{0.6f, 0.5f}, {0.88f, wy - 0.02f}, {0.78f, wy + 0.1f}}, solid(membrane));
    }
    // body
    if (e <= 2) c.poly({{0.5f, 0.2f}, {0.78f, 0.5f}, {0.5f, 0.82f}, {0.22f, 0.5f}}, lit(body));
    else {
        c.ellipse(0.5f, 0.52f, 0.25f, 0.27f, lit(body));
        c.poly({{0.44f, 0.74f}, {0.56f, 0.74f}, {0.5f, 0.9f}}, lit(body));   // tail spike
    }
    if (e >= 4) {   // feelers
        c.capsule(0.46f, 0.3f, 0.38f, 0.14f, 0.02f, solid(farSide(body)));
        c.capsule(0.56f, 0.3f, 0.64f, 0.14f, 0.02f, solid(farSide(body)));
    }
    // one big eye
    if (e <= 2) {
        c.rect(0.46f, 0.38f, 0.22f, 0.22f, solid(eye));
        c.rect(0.58f, 0.44f, 0.08f, 0.1f, solid(pupil));
    } else {
        c.circle(0.56f, 0.48f, 0.12f, solid(eye));
        c.dot(0.6f, 0.48f, pupil, e >= 4 ? 2 : 1);
    }
}

void drawBrute(Canvas& c, int frame)
{
    if (g_era == 0) {
        Rows top = {"................", "................", ".....######.....", "...##########...", "..############..",
                    "..#########o#o#.", ".##############.", "################", "##::::::::::::##",
                    "##.:::::::::::.#", "##.:::::::::::.#", "##.:::::::::::.#", "....########...."};
        static const Rows legs[4] = {{"....###..###....", "....###..###....", "...####..####..."},
                                     {"....###...###...", "...###....###...", "..####....####.."},
                                     {"....###..###....", "....###..###....", "...####..####..."},
                                     {"...###...###....", "...###....###...", "..####....####.."}};
        if (frame == RF_HURT)
            for (auto& r : top) for (auto& ch : r) if (ch == ':') ch = '#';
        if (frame == RF_CHARGE) { top = shiftRight(top, 1); top[5] = "...#########oo#o"; }
        stamp(c, top, 0, 0);
        stamp(c, legs[frame <= RF_WALK3 ? frame : 0], 0, 13);
        return;
    }
    const int e = g_era;
    Col fog = pc(0x6e6a8e, 1), fogL = pc(0xa8a4c8, 2), eye = pc(0xff4040, 3), deep = farSide(fog);
    if (frame == RF_HURT) { fog = pc(0xd8d4f0, 2); fogL = pc(0xffffff, 3); deep = farSide(fog); }
    float lean = frame == RF_CHARGE ? 0.16f : 0.0f;
    float step = (frame <= RF_WALK3) ? 0.1f * std::sin(frame * 1.5707963f) : 0.0f;
    float rad = e <= 2 ? 0.12f : 0.16f;
    // legs
    c.rect(0.58f + step, 1.5f, 0.32f, 0.46f, lit(deep, false), rad);
    c.rect(1.1f - step, 1.5f, 0.32f, 0.46f, lit(deep, false), rad);
    // far arm
    c.capsule(0.62f + lean, 0.95f, 0.36f + lean - step, 1.46f, 0.16f, lit(deep));
    c.circle(0.36f + lean - step, 1.5f, 0.19f, lit(deep));
    // body and head
    if (e <= 2) {
        c.rect(0.42f + lean, 0.62f, 1.2f, 1.04f, lit(fog), 0.3f);
        c.rect(0.82f + lean, 0.28f, 0.74f, 0.56f, lit(fogL), 0.22f);
    } else {
        c.ellipse(1.0f + lean, 1.12f, 0.62f, 0.56f, lit(fog));
        c.ellipse(1.18f + lean, 0.58f, 0.42f, 0.36f, lit(fogL));
        if (e >= 4) {   // cracks of static across the chest
            Col crack = farSide(deep);
            c.capsule(0.7f + lean, 0.96f, 0.86f + lean, 1.08f, 0.025f, solid(crack));
            c.capsule(0.86f + lean, 1.08f, 0.8f + lean, 1.26f, 0.025f, solid(crack));
            c.capsule(1.1f + lean, 1.3f, 1.26f + lean, 1.4f, 0.025f, solid(crack));
        }
    }
    // brow and eyes
    float ey = frame == RF_CHARGE ? 0.6f : 0.58f;
    c.rect(1.1f + lean, ey - 0.13f, 0.5f, 0.07f, solid(deep));
    float eh = frame == RF_CHARGE ? 0.1f : 0.07f;
    if (e <= 2) {
        c.rect(1.2f + lean, ey - eh / 2, 0.12f, eh + 0.02f, solid(eye));
        c.rect(1.44f + lean, ey - eh / 2, 0.1f, eh + 0.02f, solid(eye));
    } else {
        c.ellipse(1.27f + lean, ey, 0.08f, eh, solid(eye));
        c.ellipse(1.5f + lean, ey + 0.01f, 0.07f, eh * 0.9f, solid(eye));
    }
    // near arm and fist
    c.capsule(1.36f + lean, 0.95f, 1.64f + lean + step, 1.42f, 0.17f, lit(fogL));
    c.circle(1.66f + lean + step, 1.48f, 0.2f, lit(fogL));
}

void drawBoss(Canvas& c, int frame)
{
    const int e = g_era;
    float wob = frame == XF_FLOAT1 ? 0.06f : 0.0f;
    float slam = frame == XF_SLAM ? 0.25f : 0.0f;
    float cy = 1.4f + wob + slam, ry = 1.08f - slam * 0.4f;
    const bool hurt = frame == XF_HURT;

    if (e == 0) {
        // 1-bit: a solid rim round a dithered body, a plain white eye
        for (int i = 0; i < 6; ++i) {
            float x = 0.55f + i * 0.38f, sway = std::sin(i * 1.7f + frame * 1.3f) * 0.18f;
            c.capsule(x, 1.9f + slam, x + sway, 2.85f - slam * 0.3f, 0.1f, solid(ONEBIT_LIGHT));
        }
        c.ellipse(1.5f, cy, 1.25f, ry, solid(ONEBIT_LIGHT));
        c.ellipse(1.5f, cy, 1.1f, ry - 0.15f, hurt ? solid(ONEBIT_LIGHT) : dither1(0.25f));
        c.ellipse(1.5f, cy - 0.06f, 0.66f, 0.46f - slam * 0.3f, solid(ONEBIT_DARK));
        c.ellipse(1.5f, cy - 0.06f, 0.54f, 0.34f - slam * 0.3f, solid(ONEBIT_LIGHT));
        if (!hurt) c.circle(1.64f, cy - 0.06f, 0.16f - slam * 0.08f, solid(ONEBIT_DARK));
        else {
            c.capsule(1.2f, cy - 0.3f, 1.8f, cy + 0.15f, 0.05f, solid(ONEBIT_DARK));
            c.capsule(1.2f, cy + 0.15f, 1.8f, cy - 0.3f, 0.05f, solid(ONEBIT_DARK));
        }
        return;
    }
    Col body = pc(0x2a2a5e, 1), ring = pc(0x8a3a8e, 0), white = pc(0xfaf3e6, 3);
    Col iris = pc(0xf7d73a, 2), pupil = pc(0x0f0d1a, 0), tend = pc(0x6a2c6e, 1);
    if (hurt) { body = pc(0xb8b6c8, 2); ring = pc(0xfaf3e6, 3); tend = pc(0xd8d4e8, 2); }
    // tendrils
    for (int i = 0; i < 6; ++i) {
        float x = 0.55f + i * 0.38f, sway = std::sin(i * 1.7f + frame * 1.3f) * 0.18f;
        float r = 0.12f - (i % 2) * 0.03f;
        if (e <= 2) {
            c.rect(x - r, 1.9f + slam, 2 * r, 0.5f, lit(tend, false));
            c.rect(x - r + sway, 2.3f + slam * 0.5f, 2 * r, 0.5f - slam * 0.3f, lit(tend, false));
        } else {
            c.capsule(x, 1.9f + slam, x + sway, 2.85f - slam * 0.3f, r, lit(tend));
            if (e >= 4) c.circle(x + sway, 2.85f - slam * 0.3f, r * 0.7f, solid(farSide(tend)));
        }
    }
    // spikes round the rim (32-bit on)
    if (e >= 4)
        for (int i = 0; i < 9; ++i) {
            float a = 3.14159f + i * 3.14159f / 8.0f, ca = std::cos(a), sa = std::sin(a);
            float bx = 1.5f + ca * 1.15f, by = cy + sa * (ry - 0.05f);
            c.poly({{bx - sa * 0.12f, by + ca * 0.12f}, {bx + sa * 0.12f, by - ca * 0.12f},
                    {1.5f + ca * 1.42f, cy + sa * (ry + 0.25f)}}, lit(farSide(body), false));
        }
    // body
    if (e <= 2) c.rect(0.3f, cy - ry, 2.4f, 2 * ry, lit(body), 0.7f);
    else c.ellipse(1.5f, cy, 1.25f, ry, lit(body));
    // rings of the swirl
    auto annulus = [&](float rxo, float ryo, float t, Col col) {
        c.shape(0, 0, 3, 3, [&](Frag& f) {
            float dx = (f.x - 1.5f) / rxo, dy = (f.y - cy + 0.04f) / ryo;
            float d = std::sqrt(dx * dx + dy * dy);
            f.nx = dx; f.ny = dy;
            return d > 1.0f - t && d < 1.0f;
        }, solid(col));
    };
    annulus(1.02f, ry - 0.2f, 0.1f, ring);
    if (e >= 3) annulus(0.84f, ry - 0.42f, 0.08f, farSide(body));
    // the eye
    c.ellipse(1.5f, cy - 0.06f, 0.62f, 0.42f - slam * 0.3f, lit(white, false));
    if (!hurt) {
        c.circle(1.62f, cy - 0.06f, 0.28f - slam * 0.2f, lit(iris));
        c.circle(1.66f, cy - 0.06f, 0.13f - slam * 0.1f, solid(pupil));
        c.dot(1.56f, cy - 0.2f, white, e >= 4 ? 2 : 1);
    } else {
        c.capsule(1.1f, cy - 0.28f, 1.9f, cy + 0.17f, 0.05f, solid(pupil));
        c.capsule(1.1f, cy + 0.17f, 1.9f, cy - 0.28f, 0.05f, solid(pupil));
    }
}

// ------------------------------------------------------------------ story bosses
// Each stage's boss is the Forgetting wearing the shape of something from Gran's memory of
// that time: a clock, Arthur's shirt, the school bell, a storm, the last train, a scarecrow.
// They keep the Forgetting's one eye (the same eye and hurt "X" as drawBoss) and use the same
// frames: two gentle float frames, a pale hurt flash and a squashed slam. Saved as
// boss_story.png beside enemy_boss.png, with the same sheet layout.

// Shapes drawn through a squash-and-stretch transform anchored at the bottom of the frame,
// so one description gives the float, hurt and slam poses.
struct Rig {
    Canvas& c;
    float sx = 1, sy = 1, dx = 0, dy = 0, ax = 1.5f, ay = 2.92f;

    Vector2 P(float x, float y) const { return {ax + (x - ax) * sx + dx, ay + (y - ay) * sy + dy}; }
    float R(float r) const { return r * std::sqrt(sx * sy); }
    void ellipse(float cx, float cy, float rx, float ry, const Fill& f)
    {
        Vector2 p = P(cx, cy);
        c.ellipse(p.x, p.y, rx * sx, ry * sy, f);
    }
    void circle(float cx, float cy, float r, const Fill& f) { ellipse(cx, cy, r, r, f); }
    void rect(float x, float y, float w, float h, const Fill& f, float rad = 0)
    {
        Vector2 p = P(x, y);
        c.rect(p.x, p.y, w * sx, h * sy, f, rad * std::min(sx, sy));
    }
    void capsule(float x0, float y0, float x1, float y1, float r, const Fill& f)
    {
        Vector2 a = P(x0, y0), b = P(x1, y1);
        c.capsule(a.x, a.y, b.x, b.y, R(r), f);
    }
    void poly(std::vector<Vector2> pts, const Fill& f)
    {
        for (auto& p : pts) p = P(p.x, p.y);
        c.poly(pts, f);
    }
    void dot(float x, float y, Col col, int n = 1)
    {
        Vector2 p = P(x, y);
        c.dot(p.x, p.y, col, n);
    }
    // An elliptical band (rx, ry are the outer radii, t the band's share of the radius).
    void ring(float cx, float cy, float rx, float ry, float t, const Fill& f)
    {
        Vector2 p = P(cx, cy);
        float RX = rx * sx, RY = ry * sy;
        c.shape(p.x - RX, p.y - RY, p.x + RX, p.y + RY, [&](Frag& fr) {
            float ex = (fr.x - p.x) / RX, ey = (fr.y - p.y) / RY;
            float d = std::sqrt(ex * ex + ey * ey);
            fr.nx = ex; fr.ny = ey;
            return d > 1.0f - t && d < 1.0f;
        }, f);
    }
};

// The pose of every story boss frame: the float frames bob, the slam frame squashes down.
Rig storyRig(Canvas& c, int frame)
{
    Rig r{c};
    if (frame == XF_FLOAT1) r.dy = 0.06f;
    if (frame == XF_SLAM) { r.sx = 1.07f; r.sy = 0.8f; }
    return r;
}

// The Forgetting's eye, as drawBoss draws it, at scale s (1 = the boss's own eye), with an
// optional ring round it so it reads on any surface.
void storyEye(Rig& r, float cx, float cy, float s, bool hurt, Col ring, float ringW)
{
    const int e = g_era;
    Col white = pc(0xfaf3e6, 3), iris = pc(0xf7d73a, 2), pupil = pc(0x0f0d1a, 0);
    if (ringW > 0) r.ellipse(cx, cy, 0.62f * s + ringW, 0.42f * s + ringW, solid(ring));
    r.ellipse(cx, cy, 0.62f * s, 0.42f * s, lit(white, false));
    if (!hurt) {
        r.circle(cx + 0.12f * s, cy, 0.28f * s, lit(iris));
        r.circle(cx + 0.16f * s, cy, 0.13f * s, solid(pupil));
        r.dot(cx + 0.06f * s, cy - 0.14f * s, white, e >= 4 ? 2 : 1);
    } else {
        float w = std::max(0.05f * s, 0.035f);
        r.capsule(cx - 0.4f * s, cy - 0.22f * s, cx + 0.4f * s, cy + 0.23f * s, w, solid(pupil));
        r.capsule(cx - 0.4f * s, cy + 0.23f * s, cx + 0.4f * s, cy - 0.22f * s, w, solid(pupil));
    }
}

// Stage 0, THE 3 A.M. CLOCK: a twin-bell alarm clock, both hands at three, dripping feet.
void drawBossClock(Canvas& c, int frame)
{
    // 1-bit, so pixels are placed by hand (24 x 24, like the brute's stamps). '#' light,
    // 'o' dark, 'f' the face (dark; a white flash when hurt), 'h' the hands and hour marks
    // (light; dark when hurt). Both hands point at three: the minute hand up, the hour hand
    // out to the right. The Forgetting's eye is the centre of the face.
    static const Rows clock = {
        "........................",
        "...###.....##.....###...",
        "..#####....##....#####..",
        ".#######..o##o..#######.",
        ".######oo######oo######.",
        "..####o##########o####..",
        "..###o##ffffffff##o###..",
        "....o#ffhffhhffhff#o....",
        "...o#ffffffhhffffff#o...",
        "...o#fhffffhhffffhf#o...",
        "...#ffffffffffffffff#...",
        "..o#ffffff####ffffff#o..",
        "..o#fhfff###oo#fhhhf#o..",
        "..o#fhfff###oo#fhhhf#o..",
        "...#ffffff####ffffff#...",
        "...o#ffffffffffffff#o...",
        "...o#fhffffffffffhf#o...",
        "....o#ffhffhhffhff#o....",
        ".....o##ffffffff##o.....",
        "......o##########o......",
        ".....##oo######oo##.....",
        "....##.....oo.....##....",
        "...##..............##...",
        "........................"};
    static const Rows hurtEye = {".o....o.", "..o..o..", "...oo...", "...oo...", "..o..o..", ".o....o."};
    const bool hurt = frame == XF_HURT;
    Rows g = clock;
    if (frame == XF_FLOAT1) {   // the hammer swings over to strike the left bell
        for (int y = 1; y <= 3; ++y) g[y].replace(10, 4, "....");
        g[1].replace(9, 2, "##");
        g[2].replace(10, 2, "##");
        g[3].replace(10, 4, "o##o");
    }
    if (hurt)
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 8; ++x) g[10 + y][8 + x] = hurtEye[y][x] == 'o' ? 'o' : 'f';
    // drips of static under the case, a little different each frame
    const int drip[XF_COUNT][2] = {{2, 1}, {1, 2}, {1, 1}, {1, 1}};
    for (int k = 0; k < 2; ++k)
        for (int i = 0; i < drip[frame][k]; ++i) g[21 + i][k ? 13 : 10] = '#';
    for (auto& row : g)
        for (auto& ch : row) {
            if (ch == 'f') ch = hurt ? '#' : 'o';
            else if (ch == 'h') ch = hurt ? 'o' : '#';
        }
    if (frame == XF_SLAM) {   // landing: squashed three rows shorter, feet on the ground
        Rows s(3, std::string(24, '.'));
        for (int y = 0; y < (int)g.size(); ++y)
            if (y != 2 && y != 9 && y != 16) s.push_back(g[y]);
        g = s;
    }
    stamp(c, g, 0, 0);
}

// Stage 1, THE EMPTY SHIRT: Arthur's shirt pegged on the line, nobody in it.
void drawBossShirt(Canvas& c, int frame)
{
    const bool hurt = frame == XF_HURT;
    Rig r = storyRig(c, frame);
    Col cloth = pc(0xa8a297, hurt ? 3 : 2), fold = pc(0x5b5662, hurt ? 2 : 1);
    Col collar = pc(0xe8e2d2, hurt ? 2 : 3), voidc = pc(0x1f1d24, 0);
    Col peg = pc(0x5b5662, 1), line = pc(0x1f1d24, 0);
    float ph = frame * 1.7f, sway = frame == XF_FLOAT1 ? 0.05f : (frame == XF_SLAM ? 0.0f : -0.03f);
    // the washing line, sagging between the pegs
    r.capsule(0.2f, 0.32f, 1.5f, 0.4f, 0.03f, solid(line));
    r.capsule(1.5f, 0.4f, 2.8f, 0.32f, 0.03f, solid(line));
    // body with a fluttering tail
    std::vector<Vector2> body = {{0.66f, 0.42f}, {2.34f, 0.42f}, {2.16f, 0.62f}, {2.18f, 1.5f}};
    for (int i = 0; i <= 8; ++i) {
        float t = i / 8.0f, x = 2.2f - t * 1.4f;
        float tail = 2.3f + 0.12f * std::sin(t * 3.14159f) + 0.07f * std::sin(t * 9.4f + ph);
        body.push_back({x + 0.05f * std::sin(t * 6.0f + ph), tail});
    }
    body.push_back({0.82f, 1.5f});
    body.push_back({0.84f, 0.62f});
    r.poly(body, lit(cloth));
    // sleeves hanging limp beside it, a crease down each, cuffs at the bottom
    for (int s = -1; s <= 1; s += 2) {
        auto X = [&](float x) { return 1.5f + s * (x - 1.5f); };
        float sw = sway * s;
        r.poly({{X(0.62f), 0.44f}, {X(0.88f), 0.5f}, {X(0.84f), 0.98f}, {X(0.68f) + sw, 1.66f}, {X(0.38f) + sw, 1.62f}},
               lit(cloth));
        r.capsule(X(0.86f), 0.52f, X(0.84f), 0.98f, 0.03f, solid(fold));                 // shoulder seam
        r.capsule(X(0.6f), 0.66f, X(0.54f) + sw, 1.44f, 0.025f, solid(fold));            // crease
        r.poly({{X(0.38f) + sw, 1.6f}, {X(0.68f) + sw, 1.64f}, {X(0.67f) + sw, 1.78f}, {X(0.37f) + sw, 1.74f}},
               solid(collar));
    }
    // the open neck: dark, empty; the collar stands round it
    r.capsule(1.3f, 0.4f, 1.7f, 0.4f, 0.07f, solid(collar));
    r.ellipse(1.5f, 0.5f, 0.2f, 0.1f, solid(voidc));
    r.poly({{1.22f, 0.42f}, {1.5f, 0.58f}, {1.42f, 0.86f}, {1.14f, 0.62f}}, solid(collar));
    r.poly({{1.78f, 0.42f}, {1.5f, 0.58f}, {1.58f, 0.86f}, {1.86f, 0.62f}}, solid(collar));
    // button line and buttons
    r.capsule(1.5f, 0.6f, 1.5f, 2.32f, 0.03f, solid(fold));
    for (float by : {1.86f, 2.12f}) r.dot(1.5f, by, collar);
    // pegs on the shoulders
    for (float px_ : {0.76f, 2.24f}) {
        r.rect(px_ - 0.07f, 0.2f, 0.14f, 0.4f, solid(peg), 0.04f);
        r.rect(px_ - 0.07f, 0.34f, 0.14f, 0.06f, solid(collar));
    }
    // the eye on the chest
    storyEye(r, 1.5f, 1.28f, 0.62f, hurt, pc(0x1f1d24, 0), 0.07f);
}

// Stage 2, THE SCHOOL BELL: a brass hand bell, wooden handle, clapper swinging under it.
void drawBossBell(Canvas& c, int frame)
{
    const bool hurt = frame == XF_HURT;
    Rig r = storyRig(c, frame);
    Col brass = pc(0xf28a1e, 2), rim = pc(0xf7d73a, 3), mouth = pc(0x0f0d1a, 0);
    Col wood = pc(0x8c4a2f, 1), iron = pc(0x5a5468, 1), ringc = pc(0x6a2c6e, 0), wave = pc(0xfaf3e6, 3);
    if (hurt) { brass = pc(0xb8b6c8, 2); rim = pc(0xfaf3e6, 3); wood = pc(0xb8b6c8, 2); iron = pc(0xb8b6c8, 2); ringc = pc(0xfaf3e6, 3); }
    float swing = frame == XF_FLOAT0 ? -0.2f : (frame == XF_FLOAT1 ? 0.2f : 0.0f);
    // clapper (behind the lip)
    r.capsule(1.5f, 2.1f, 1.5f + swing, 2.5f, 0.05f, solid(iron));
    r.circle(1.5f + swing, 2.58f, 0.2f, lit(iron));
    // handle and collar
    r.capsule(1.5f, 0.3f, 1.5f, 0.66f, 0.13f, solid(wood));
    r.ellipse(1.5f, 0.24f, 0.2f, 0.16f, solid(wood));
    if (!hurt) r.capsule(1.44f, 0.2f, 1.44f, 0.6f, 0.03f, solid(pc(0xf5c9a0, 2)));
    r.rect(1.24f, 0.62f, 0.52f, 0.16f, lit(rim), 0.05f);
    // the bell: a dome flaring out to the lip
    const float prof[][2] = {{0.76f, 0.3f}, {0.82f, 0.46f}, {0.94f, 0.55f}, {1.15f, 0.59f}, {1.4f, 0.61f},
                             {1.62f, 0.66f}, {1.82f, 0.76f}, {1.98f, 0.9f}, {2.1f, 1.06f}, {2.2f, 1.24f}};
    std::vector<Vector2> bell;
    for (auto& p : prof) bell.push_back({1.5f + p[1], p[0]});
    for (int i = 9; i >= 0; --i) bell.push_back({1.5f - prof[i][1], prof[i][0]});
    r.poly(bell, lit(brass));
    if (g_era >= 2 && !hurt) r.capsule(1.0f, 1.0f, 0.62f, 1.9f, 0.05f, solid(rim));   // highlight
    // lip and the dark mouth under it
    r.ellipse(1.5f, 2.2f, 1.24f, 0.17f, lit(rim));
    r.ellipse(1.5f, 2.24f, 1.06f, 0.09f, solid(mouth));
    r.rect(1.24f, 0.98f, 0.52f, 0.05f, solid(farSide(brass)));                        // a cast band
    // the eye on the body
    storyEye(r, 1.5f, 1.52f, 0.66f, hurt, ringc, 0.07f);
    // ringing: sound marks on the side the clapper strikes
    if (swing != 0) {
        float s = swing < 0 ? -1.0f : 1.0f;
        r.capsule(1.5f + s * 0.86f, 0.9f, 1.5f + s * 1.04f, 0.74f, 0.03f, solid(wave));
        r.capsule(1.5f + s * 0.98f, 1.14f, 1.5f + s * 1.22f, 1.06f, 0.03f, solid(wave));
        r.capsule(1.5f + s * 1.12f, 1.4f, 1.5f + s * 1.36f, 1.4f, 0.03f, solid(wave));
    }
}

// Stage 3, THE STORM: a heavy thundercloud, a bolt hanging under it, rain.
void drawBossStorm(Canvas& c, int frame)
{
    const bool hurt = frame == XF_HURT, slam = frame == XF_SLAM;
    Rig r = storyRig(c, frame);
    Col cloud = pc(0x463a66, 1), belly = pc(0x2a2140, 0), puffL = pc(0x6e6a9e, 2);
    Col bolt = pc(0xf8d070, 3), core = pc(0xf4f0e6, 3), rain = pc(0x5aa0e0, 2), ringc = pc(0x7a3a5a, 0);
    if (hurt) { cloud = pc(0xa8a4d0, 2); belly = pc(0x6e6a9e, 1); puffL = pc(0xf4f0e6, 3); bolt = core; ringc = pc(0xf4f0e6, 3); }
    // rain streaks, falling a step each frame
    for (int i = 0; i < 6; ++i) {
        float x = 0.52f + i * 0.42f + (i % 2) * 0.08f;
        if (x > 1.1f && x < 1.95f) continue;   // keep the bolt clear
        float y = 1.85f + std::fmod(i * 0.37f + frame * 0.23f, 0.55f);
        r.capsule(x, y, x - 0.08f, y + 0.26f, 0.035f, solid(rain));
    }
    // the bolt, flickering between two shapes
    float k = frame == XF_FLOAT1 ? -1.0f : 1.0f;
    float tipY = slam ? 2.9f : 2.86f, tipX = 1.5f - 0.32f * k;
    r.poly({{1.5f + 0.18f * k, 1.58f}, {1.5f - 0.28f * k, 2.14f}, {1.5f + 0.02f * k, 2.14f},
            {tipX, tipY}, {1.5f + 0.4f * k, 1.98f}, {1.5f + 0.1f * k, 1.98f}, {1.5f + 0.44f * k, 1.58f}},
           solid(bolt));
    r.capsule(1.5f + 0.24f * k, 1.68f, 1.5f - 0.12f * k, 2.08f, 0.035f, solid(core));
    r.capsule(1.5f + 0.06f * k, 2.2f, 1.5f - 0.18f * k, 2.6f, 0.03f, solid(core));
    if (slam)   // the strike sparks where it lands
        for (int sd = -1; sd <= 1; sd += 2) {
            r.capsule(tipX + sd * 0.12f, tipY - 0.06f, tipX + sd * 0.34f, tipY - 0.18f, 0.03f, solid(bolt));
            r.capsule(tipX + sd * 0.1f, tipY - 0.16f, tipX + sd * 0.2f, tipY - 0.34f, 0.03f, solid(bolt));
        }
    // the cloud: puffs over a flat dark belly
    r.rect(0.3f, 1.05f, 2.4f, 0.66f, lit(belly, false), 0.33f);
    const float puffs[][3] = {{0.72f, 1.18f, 0.44f}, {1.18f, 0.84f, 0.52f}, {1.84f, 0.78f, 0.58f},
                              {2.32f, 1.16f, 0.42f}, {1.5f, 1.22f, 0.56f}};
    for (auto& p : puffs) r.circle(p[0], p[1], p[2], lit(cloud));
    if (g_era >= 3)
        for (auto& p : puffs)
            r.ellipse(p[0] - p[2] * 0.3f, p[1] - p[2] * 0.42f, p[2] * 0.42f, p[2] * 0.26f, solid(puffL));
    r.rect(0.46f, 1.5f, 2.08f, 0.2f, solid(belly), 0.1f);
    // the eye in the middle of the cloud
    storyEye(r, 1.5f, 1.12f, 0.74f, hurt, ringc, 0.06f);
}

// Stage 4, THE LAST TRAIN: an old steam engine coming head-on, the headlamp its eye.
void drawBossTrain(Canvas& c, int frame)
{
    const bool hurt = frame == XF_HURT;
    Rig r = storyRig(c, frame);
    Col green = pc(0x3f8a5e, 2), cab = pc(0x24523e, 1), black = pc(0x2a2a36, 0), door = pc(0x3c3c4c, 0);
    Col red = pc(0xc8382e, 1), brass = pc(0xe8b84a, 2), copper = pc(0xc87a3a, 2), steel = pc(0xb8bcc8, 2);
    Col smoke = pc(0xd8d6e2, 3), glass = pc(0xf5c96a, 3), lining = pc(0xe8c860, 2);
    if (hurt) {
        green = pc(0xc8c6d8, 2); cab = pc(0xb8b6c8, 2); black = pc(0x8a88a0, 1); door = pc(0x9a98b0, 1);
        red = pc(0xd8d4e8, 2); brass = pc(0xfaf3e6, 3); copper = brass; steel = pc(0xd8d4e8, 2);
        glass = pc(0xfaf3e6, 3); lining = glass;
    }
    // smoke from the funnel, drifting back
    float drift = frame == XF_FLOAT1 ? 0.12f : 0.0f;
    if (frame != XF_SLAM) {
        r.circle(1.36f - drift, 0.26f, 0.19f, lit(smoke));
        r.circle(1.04f - drift, 0.2f, 0.15f, lit(smoke));
        r.circle(0.78f - drift * 1.4f, 0.17f, 0.11f, lit(smoke));
    } else {   // squashed flat out of the top
        r.circle(1.14f, 0.36f, 0.18f, lit(smoke));
        r.circle(1.86f, 0.36f, 0.18f, lit(smoke));
    }
    // steam venting low at both sides
    float puff = frame == XF_FLOAT1 ? 0.03f : 0.0f;
    for (int s = -1; s <= 1; s += 2) {
        r.circle(1.5f + s * 1.12f, 2.66f - puff, 0.15f + puff, lit(smoke));
        r.circle(1.5f + s * 1.3f, 2.56f, 0.11f, lit(smoke));
    }
    // wheels just showing under the beam
    r.rect(0.66f, 2.4f, 0.16f, 0.36f, lit(black), 0.06f);
    r.rect(2.18f, 2.4f, 0.16f, 0.36f, lit(black), 0.06f);
    // the cab behind the boiler: curved roof, lined panels, round spectacle windows
    r.rect(0.3f, 0.78f, 2.4f, 1.42f, lit(cab), 0.08f);
    r.ellipse(1.5f, 0.8f, 1.3f, 0.12f, lit(black));
    r.rect(0.4f, 0.98f, 2.2f, 1.14f, solid(lining), 0.06f);
    r.rect(0.43f, 1.01f, 2.14f, 1.08f, lit(cab), 0.05f);
    for (int s = -1; s <= 1; s += 2) {
        r.circle(1.5f + s * 0.92f, 1.18f, 0.16f, solid(brass));
        r.circle(1.5f + s * 0.92f, 1.18f, 0.12f, solid(glass));
    }
    // funnel with a copper cap
    r.rect(1.31f, 0.4f, 0.38f, 0.52f, lit(black), 0.03f);
    r.rect(1.22f, 0.34f, 0.56f, 0.12f, lit(copper), 0.04f);
    // the boiler front: a green band round the dark round smokebox door, rivets, a strap
    r.circle(1.5f, 1.48f, 0.82f, solid(black));
    r.circle(1.5f, 1.48f, 0.78f, lit(green));
    r.circle(1.5f, 1.48f, 0.68f, lit(black));
    r.circle(1.5f, 1.5f, 0.58f, lit(door));
    for (int i = 0; i < 14; ++i) {
        float a = i * 0.4488f;
        r.dot(1.5f + std::cos(a) * 0.63f, 1.48f + std::sin(a) * 0.63f, steel, 2);
    }
    r.capsule(1.06f, 1.86f, 1.94f, 1.86f, 0.035f, solid(steel));
    r.capsule(1.5f, 1.86f, 1.5f, 2.0f, 0.035f, solid(steel));
    // buffer beam, buffers and the coupling
    r.rect(0.22f, 2.12f, 2.56f, 0.3f, lit(red), 0.04f);
    for (int s = -1; s <= 1; s += 2) {
        float bx = 1.5f + s * 1.04f;
        r.circle(bx, 2.27f, 0.18f, lit(steel));
        r.circle(bx, 2.27f, 0.08f, solid(farSide(steel)));
    }
    r.rect(1.42f, 2.3f, 0.16f, 0.2f, solid(black));
    r.capsule(1.5f, 2.48f, 1.5f, 2.64f, 0.05f, solid(steel));
    // the headlamp is the eye: a brass bezel with a little hood
    r.rect(1.32f, 1.04f, 0.36f, 0.1f, solid(brass), 0.03f);
    storyEye(r, 1.5f, 1.42f, 0.68f, hurt, brass, 0.08f);
}

// Stage 5, THE SCARECROW: straw hat, sack face, an old coat on a crossed stick, one pole leg.
void drawBossScarecrow(Canvas& c, int frame)
{
    const bool hurt = frame == XF_HURT;
    Rig r = storyRig(c, frame);
    Col wood = pc(0x7a5a3a, 1), coat = pc(0x55664a, 1), coatD = pc(0x3e4a38, 0), patch = pc(0x9a5a3a, 1);
    Col straw = pc(0xe6c460, 3), strawD = pc(0xb89038, 2), sack = pc(0xc8a070, 2), band = pc(0x8a3a3a, 1);
    Col stitch = pc(0x3a2418, 0), rope = pc(0x8a6a40, 1), ringc = pc(0x5a2a4a, 0);
    if (hurt) {
        wood = pc(0xb8b6c8, 2); coat = pc(0xc8c6d8, 2); coatD = pc(0xa8a6bc, 2); patch = pc(0xd8d4e8, 2);
        straw = pc(0xfaf3e6, 3); strawD = pc(0xd8d4e8, 2); sack = pc(0xe0dcea, 3); band = pc(0xb8b6c8, 2);
        rope = pc(0xb8b6c8, 2); ringc = pc(0xfaf3e6, 3);
    }
    float flap = frame == XF_FLOAT1 ? 1.0f : (frame == XF_FLOAT0 ? -1.0f : 0.0f);
    float droop = frame == XF_SLAM ? 0.08f : 0.0f;
    // the pole leg
    r.rect(1.42f, 1.8f, 0.16f, 1.08f, lit(wood), 0.06f);
    // the cross stick, its ends showing past the cuffs
    r.capsule(0.12f, 1.2f + droop, 2.88f, 1.2f + droop, 0.05f, lit(wood));
    // straw poking from the cuffs
    for (int s = -1; s <= 1; s += 2) {
        float ex = 1.5f + s * 1.06f, ey = 1.24f + droop;
        for (int i = 0; i < 5; ++i) {
            float a = -0.7f + i * 0.35f + flap * 0.08f * s;
            float len = 0.26f + 0.06f * (i % 2);
            r.capsule(ex, ey, ex + s * std::cos(a) * len, ey + std::sin(a) * len, 0.03f, solid(i % 2 ? strawD : straw));
        }
    }
    // sleeves along the stick
    for (int s = -1; s <= 1; s += 2) {
        float sh = 1.5f + s * 0.5f, cuff = 1.5f + s * 1.06f;
        r.poly({{sh, 1.0f}, {cuff, 1.06f + droop}, {cuff, 1.42f + droop + flap * 0.03f * s}, {sh, 1.5f}}, lit(coat));
        r.ellipse(cuff, 1.24f + droop, 0.05f, 0.18f, solid(coatD));
    }
    // coat body with a ragged hem
    std::vector<Vector2> body = {{1.02f, 0.98f}, {1.98f, 0.98f}, {2.12f, 2.0f}};
    const float rag[] = {0.0f, 0.14f, 0.02f, 0.18f, 0.04f, 0.12f, 0.0f};
    for (int i = 0; i < 7; ++i) {
        float t = i / 6.0f;
        body.push_back({2.16f - t * 1.32f + flap * 0.03f, 2.04f + rag[i] + (i % 2) * flap * 0.03f});
    }
    body.push_back({0.88f, 2.0f});
    r.poly(body, lit(coat));
    // lapels, buttons and a patch
    r.poly({{1.28f, 0.98f}, {1.5f, 1.04f}, {1.5f, 1.54f}, {1.32f, 1.24f}}, solid(coatD));
    r.poly({{1.72f, 0.98f}, {1.5f, 1.04f}, {1.5f, 1.54f}, {1.68f, 1.24f}}, solid(coatD));
    r.capsule(1.5f, 1.54f, 1.5f, 2.08f, 0.015f, solid(coatD));
    for (float by : {1.66f, 1.88f}) r.circle(1.56f, by, 0.04f, solid(coatD));
    r.rect(1.68f, 1.62f, 0.26f, 0.24f, lit(patch, false), 0.03f);
    for (int i = 0; i < 4; ++i) r.dot(1.71f + i * 0.065f, 1.64f, stitch);
    // straw at the neck
    for (int i = 0; i < 6; ++i) {
        float x = 1.3f + i * 0.08f;
        r.capsule(x, 0.96f, x + (i - 2.5f) * 0.05f, 1.1f, 0.025f, solid(i % 2 ? strawD : straw));
    }
    // the sack head, tied with rope, a little weave showing
    r.ellipse(1.5f, 0.74f, 0.42f, 0.36f, lit(sack));
    r.poly({{1.36f, 1.0f}, {1.64f, 1.0f}, {1.72f, 1.08f}, {1.28f, 1.08f}}, lit(sack));
    r.capsule(1.32f, 1.02f, 1.68f, 1.02f, 0.03f, solid(rope));
    Col weave = farSide(sack);
    for (int i = 0; i < 9; ++i) {
        float wx = 1.2f + rnd(i, 3, 41) * 0.6f, wy = 0.56f + rnd(i, 4, 41) * 0.38f;
        float ex = (wx - 1.5f) / 0.4f, ey = (wy - 0.74f) / 0.34f;
        if (ex * ex + ey * ey < 0.8f && (std::fabs(wx - 1.5f) > 0.26f || wy > 0.88f))
            r.capsule(wx, wy, wx + 0.04f, wy, 0.008f, solid(weave));
    }
    // stitched mouth, a crooked line of cross stitches
    r.capsule(1.3f, 0.92f, 1.5f, 0.95f, 0.012f, solid(stitch));
    r.capsule(1.5f, 0.95f, 1.7f, 0.91f, 0.012f, solid(stitch));
    for (int i = 0; i < 6; ++i) {
        float x = 1.33f + i * 0.068f, y = 0.93f + (i > 0 && i < 5 ? 0.015f : 0.0f);
        r.capsule(x, y - 0.045f, x, y + 0.045f, 0.012f, solid(stitch));
    }
    // the eye on the face
    storyEye(r, 1.5f, 0.69f, 0.42f, hurt, ringc, 0.035f);
    // straw hat
    r.ellipse(1.5f, 0.42f, 0.74f, 0.12f, lit(straw));
    r.rect(1.16f, 0.08f, 0.68f, 0.36f, lit(straw), 0.12f);
    r.rect(1.16f, 0.3f, 0.68f, 0.08f, solid(band));
    for (int i = 0; i < 3; ++i) {   // frayed brim
        float x = 0.86f + i * 0.6f + (i == 1 ? 0.3f : 0.0f);
        r.capsule(x, 0.5f, x - 0.04f, 0.58f, 0.015f, solid(strawD));
    }
}

void drawStoryBoss(Canvas& c, int frame)
{
    switch (g_era) {
    case 0: drawBossClock(c, frame); break;
    case 1: drawBossShirt(c, frame); break;
    case 2: drawBossBell(c, frame); break;
    case 3: drawBossStorm(c, frame); break;
    case 4: drawBossTrain(c, frame); break;
    default: drawBossScarecrow(c, frame); break;
    }
}

// ------------------------------------------------------------------ tiles
void drawTile(Canvas& c, int t)
{
    switch (t) {
    case T_GROUND_TOP:
    case T_GROUND: {
        if (g_era == 0) {
            // 1-bit: plain black earth under one solid white surface line, exactly 1 px thick.
            // (A pebble here would repeat every 8 px and read as a dotted rail, so none.)
            c.rect(0, 0, 1, 1, solid(ONEBIT_DARK));
            if (t == T_GROUND_TOP) stamp(c, {"########"}, 0, 0);
            break;
        }
        // hand-placed earth: a base colour, a darker wavy layer low down, two pebbles and a root
        Col dirt = eraCol(0x8c4a2f, 0.0f), dirtDark = mixc(eraCol(0x5e3020, 0.0f), hex(0x3a2a5a), 0.2f);
        Col stone = eraCol(0xb8b6c8, 1.0f), stoneDark = mixc(stone, hex(0x4a3a6a), 0.45f), root = eraCol(0x5a3a22, 0.0f);
        c.rect(0, 0, 1, 1, solid(dirt));
        c.shape(0, 0.5f, 1, 1, [&](Frag& f) {
            f.nx = 0; f.ny = 0;
            return f.y > 0.62f + 0.06f * std::sin(f.x * 6.2831853f * 2);   // tiles across: whole periods
        }, solid(dirtDark));
        c.ellipse(0.27f, 0.42f, 0.11f, 0.075f, solid(stoneDark));
        c.ellipse(0.25f, 0.4f, 0.09f, 0.055f, solid(stone));
        c.ellipse(0.74f, 0.8f, 0.08f, 0.055f, solid(stoneDark));
        c.ellipse(0.73f, 0.78f, 0.06f, 0.04f, solid(stone));
        c.capsule(0.55f, 0.3f, 0.62f, 0.48f, 0.022f, solid(root));
        c.capsule(0.62f, 0.48f, 0.56f, 0.6f, 0.02f, solid(root));
        if (t == T_GROUND_TOP) {
            Col grass = eraCol(0x6fdc4a, 1.0f), grassDark = mixc(eraCol(0x1e8a4c, 1.0f), hex(0x1a3a5a), 0.15f);
            Col grassLight = mixc(grass, hex(0xfff0a0), 0.35f);
            // light top line, grass body, then a dark lip where it overhangs the earth
            c.rect(0, 0, 1, 0.28f, [&](const Frag& f) { return f.y < 0.07f ? grassLight : (f.y < 0.21f ? grass : grassDark); });
            for (int i = 0; i < 5; ++i) {   // uneven blades hanging over the lip
                float gx = 0.1f + i * 0.2f, len = 0.33f + 0.06f * ((i * 7) % 3);
                c.poly({{gx - 0.05f, 0.26f}, {gx + 0.05f, 0.26f}, {gx + 0.01f, len}}, solid(grassDark));
            }
            for (int i = 0; i < 3; ++i) c.dot(0.18f + i * 0.31f, 0.12f, grassLight);
        }
        break;
    }
    case T_BLOCK:
    case T_CRACKED: {
        // each stage builds its platforms from a different material, with a dark underside
        Col line = eraCol(0x2a1a1a, 0.0f);
        switch (g_era) {
        case 0:   // 1-bit: a white outlined box
            c.rect(0, 0, 1, 1, solid(ONEBIT_LIGHT));
            c.rect(0.13f, 0.13f, 0.74f, 0.74f, solid(ONEBIT_DARK));
            c.rect(0.38f, 0.38f, 0.24f, 0.24f, solid(ONEBIT_LIGHT));
            break;
        case 1:   // 4-shade: wooden planks of a garden bench
        case 6: { // 3D: the same, as garden decking
            Col wood = eraCol(0xb07a4a, 0.7f), woodDark = mixc(mul(wood, 0.7f), hex(0x3a2a5a), 0.2f);
            c.rect(0, 0, 1, 1, solid(woodDark));
            for (int k = 0; k < 3; ++k) c.rect(0.0f, k * 0.33f + 0.03f, 1.0f, 0.27f, shade(wood, 0.5f));
            c.dot(0.12f, 0.15f, woodDark); c.dot(0.85f, 0.48f, woodDark); c.dot(0.4f, 0.8f, woodDark);
            break;
        }
        case 2: { // 8-bit: red bricks
            Col brick = eraCol(0xc8503a, 0.9f), mortar = eraCol(0x4a2a24, 0.0f);
            c.rect(0, 0, 1, 1, solid(mortar));
            for (int row = 0; row < 2; ++row)
                for (int col = -1; col < 2; ++col) {
                    float x = col * 0.5f + 0.25f + (row ? 0.25f : -0.25f);
                    c.rect(x + 0.04f, row * 0.5f + 0.05f, 0.42f, 0.4f, solid(brick));
                    c.rect(x + 0.04f, row * 0.5f + 0.05f, 0.42f, 0.08f, solid(mixc(brick, hex(0xf8d070), 0.35f)));
                }
            break;
        }
        case 3: { // 16-bit: rough stone blocks
            Col st = hex(0x8a8aa8), stD = mixc(mul(st, 0.65f), hex(0x2a2140), 0.3f), stL = mixc(st, hex(0xf8f0c0), 0.3f);
            c.rect(0, 0, 1, 1, solid(stD));
            c.rect(0.05f, 0.05f, 0.55f, 0.42f, shade(st, 0.8f), 0.06f);
            c.rect(0.65f, 0.05f, 0.3f, 0.42f, shade(st, 0.8f), 0.06f);
            c.rect(0.05f, 0.53f, 0.3f, 0.42f, shade(st, 0.8f), 0.06f);
            c.rect(0.4f, 0.53f, 0.55f, 0.42f, shade(st, 0.8f), 0.06f);
            c.rect(0.08f, 0.07f, 0.45f, 0.05f, solid(stL));
            c.rect(0.43f, 0.55f, 0.45f, 0.05f, solid(stL));
            break;
        }
        case 4: { // 32-bit: a steel girder with rivets
            Col steel = hex(0x4a7ab0), sD = mixc(mul(steel, 0.6f), hex(0x2a1a4a), 0.3f), sL = mixc(steel, hex(0xfff0b0), 0.35f);
            c.rect(0, 0, 1, 1, solid(sD));
            c.rect(0, 0.06f, 1, 0.2f, solid(steel));
            c.rect(0, 0.74f, 1, 0.2f, solid(steel));
            c.rect(0, 0.06f, 1, 0.05f, solid(sL));
            c.poly({{0.06f, 0.26f}, {0.2f, 0.26f}, {0.94f, 0.74f}, {0.8f, 0.74f}}, solid(steel));
            for (int k = 0; k < 4; ++k) { c.dot(0.1f + k * 0.26f, 0.15f, sL); c.dot(0.1f + k * 0.26f, 0.83f, sL); }
            break;
        }
        default: { // 2.5D: concrete slabs
            Col con = hex(0xb8b4b0), cD = mixc(mul(con, 0.7f), hex(0x3a3a5a), 0.25f);
            c.rect(0, 0, 1, 1, solid(cD));
            c.rect(0.03f, 0.03f, 0.94f, 0.44f, shade(con, 0.5f));
            c.rect(0.03f, 0.53f, 0.94f, 0.44f, shade(con, 0.5f));
            c.dot(0.3f, 0.2f, cD); c.dot(0.7f, 0.75f, cD);
            break;
        }
        }
        if (t == T_CRACKED) {
            Col crack = g_era == 0 ? ONEBIT_LIGHT : line;
            c.capsule(0.5f, 0.0f, 0.38f, 0.32f, 0.045f, solid(crack));
            c.capsule(0.38f, 0.32f, 0.6f, 0.58f, 0.045f, solid(crack));
            c.capsule(0.6f, 0.58f, 0.46f, 1.0f, 0.045f, solid(crack));
            c.capsule(0.6f, 0.58f, 0.85f, 0.7f, 0.035f, solid(crack));
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
        // Memory fragments: each stage has its own, pixel-crisp and inside the stage's colours.
        // FRAG1 is the twinkle frame (a touch smaller, with a sparkle).
        const bool tw = t == T_FRAG1;
        float s = tw ? 0.85f : 1.0f;
        if (g_era == 0) {   // 1-bit: a hollow diamond, filled on the twinkle
            if (tw) stamp(c, {"........", "......#.", "...#....", "..###...", ".#####..", "..###...", "...#....", "........"}, 0, 0);
            else stamp(c, {"........", "...#....", "..###...", ".##o##..", "###o###.", ".##o##..", "..###...", "...#...."}, 0, 0);
            break;
        }
        if (g_era == 1) {   // 4-shade: a little star, lightest grey
            std::vector<Vector2> st;
            for (int k = 0; k < 10; ++k) {
                float a = -1.5708f + k * 0.6283f, rr = (k % 2 ? 0.17f : 0.4f) * s;
                st.push_back({0.5f + std::cos(a) * rr, 0.52f + std::sin(a) * rr});
            }
            c.poly(st, solid(tone(3)));
            break;
        }
        Col white = hex(0xfaf3e6);
        if (g_era == 2) {   // 8-bit: a chunky heart, three colours
            Col fc = hex(0xf7d73a);
            c.circle(0.36f, 0.42f, 0.17f * s, lit(fc, false));
            c.circle(0.64f, 0.42f, 0.17f * s, lit(fc, false));
            c.poly({{0.2f, 0.46f}, {0.8f, 0.46f}, {0.5f, 0.84f * s + 0.1f}}, lit(fc, false));
            c.dot(0.32f, 0.36f, white);
            break;
        }
        if (g_era == 4) {   // 32-bit: a tiny snapshot, a white frame round a bit of sky and grass
            c.rect(0.2f, 0.2f, 0.6f * s, 0.62f * s, lit(hex(0xfaf3e6), false));
            c.rect(0.26f, 0.26f, 0.48f * s, 0.36f * s, solid(hex(0x5aa0e0)));
            c.rect(0.26f, 0.5f, 0.48f * s, 0.12f * s, solid(hex(0x4a9a3a)));
            c.dot(0.42f, 0.42f, hex(0xf08a4a), 2);
            if (tw) c.dot(0.7f, 0.28f, white, 2);
            break;
        }
        // 16-bit and 2.5D: a cut gem, lit from the top-left, with one facet line
        Col gold = hex(0xf7d73a);
        c.poly({{0.5f, 0.5f - 0.36f * s}, {0.5f + 0.24f * s, 0.5f}, {0.5f, 0.5f + 0.36f * s}, {0.5f - 0.24f * s, 0.5f}}, lit(gold));
        c.poly({{0.5f, 0.5f - 0.36f * s}, {0.5f - 0.24f * s, 0.5f}, {0.5f, 0.5f + 0.06f}}, solid(warmLight(gold)));
        c.poly({{0.5f, 0.5f + 0.36f * s}, {0.5f + 0.24f * s, 0.5f}, {0.5f, 0.5f + 0.06f}}, solid(coolShadow(gold)));
        if (tw) {
            c.rect(0.8f, 0.08f, 0.04f, 0.2f, solid(white));
            c.rect(0.72f, 0.16f, 0.2f, 0.04f, solid(white));
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

// ------------------------------------------------------------------ backgrounds, one scene per stage
// Each stage gets its own hand-laid scene, painted straight into pixels at the stage's native
// size. Far layers fade toward the horizon colour so Gran and Sam always stand out in front.
// Gran's home is the landmark that ages with the stages: a toy house, her cottage, the house
// on the school road, the treehouse, her flat by the station, the city, home again (in 3D).
struct Scene {
    Px& p;
    int W, H;
    float s;                // pixels per "1-bit pixel" (H / 88)
    Col fadeCol;
    float fade = 0;         // 0..1 toward the horizon colour for this layer

    Col fin(Col c, int x, int y) const
    {
        if (fade > 0) c = mixc(c, fadeCol, fade);
        if (g_era == 0) return c;   // 1-bit scenes are drawn in exactly two colours already
        return lowRes(g_era) ? quantize(c, x, y) : c;
    }
    void put(int x, int y, Col c) { p.set(x, y, fin(c, x, y)); }
    void rect(float x0, float y0, float w, float h, Col c)
    {
        for (int y = (int)std::floor(y0); y < (int)std::floor(y0 + h); ++y)
            for (int x = (int)std::floor(x0); x < (int)std::floor(x0 + w); ++x) put(x, y, c);
    }
    void frame(float x0, float y0, float w, float h, Col c)   // 1-px outline
    {
        rect(x0, y0, w, 1, c); rect(x0, y0 + h - 1, w, 1, c);
        rect(x0, y0, 1, h, c); rect(x0 + w - 1, y0, 1, h, c);
    }
    void tri(float ax, float ay, float bx, float by, float cx, float cy, Col c)
    {
        int x0 = (int)std::floor(std::min({ax, bx, cx})), x1 = (int)std::ceil(std::max({ax, bx, cx}));
        int y0 = (int)std::floor(std::min({ay, by, cy})), y1 = (int)std::ceil(std::max({ay, by, cy}));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                float px = x + 0.5f, py = y + 0.5f;
                float d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by);
                float d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy);
                float d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay);
                bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
                if (!(neg && pos)) put(x, y, c);
            }
    }
    // A flat disc with one shadow band on its lower right (light from the top left).
    void disc(float cx, float cy, float rx, float ry, Col c, Col shadow, bool shadeIt = true)
    {
        for (int y = (int)(cy - ry); y <= (int)(cy + ry); ++y)
            for (int x = (int)(cx - rx); x <= (int)(cx + rx); ++x) {
                float nx = (x + 0.5f - cx) / rx, ny = (y + 0.5f - cy) / ry;
                if (nx * nx + ny * ny > 1) continue;
                put(x, y, shadeIt && (0.45f * nx + 0.75f * ny) > 0.35f ? shadow : c);
            }
    }
    // A cloud built from blocks, so its edges step like real pixel art.
    void cloud(float cx, float cy, float size, Col c, Col shadow)
    {
        int b = std::max(1, (int)std::lround(s * 2));
        float r1 = size, r2 = size * 0.7f, r3 = size * 0.6f;
        for (float y = cy - size * 1.4f; y < cy + size; y += b)
            for (float x = cx - size * 2.4f; x < cx + size * 2.4f; x += b) {
                float mx = x + b * 0.5f, my = y + b * 0.5f;
                auto in = [&](float ox, float oy, float r) { return (mx - ox) * (mx - ox) + (my - oy) * (my - oy) * 2.0f < r * r; };
                bool inside = in(cx, cy - size * 0.2f, r1) || in(cx - size * 1.1f, cy + size * 0.1f, r2) || in(cx + size * 1.2f, cy + size * 0.15f, r3);
                if (my > cy + size * 0.45f) inside = false;   // flat bottom
                if (inside) rect(x, y, (float)b, (float)b, my > cy + size * 0.1f ? shadow : c);
            }
    }
};

// Hills along the bottom of a layer: a ridge snapped to steps of `step` pixels.
void hills(Scene& sc, float baseFrac, int seed, float amp, Col fill, Col edge, int step)
{
    for (int x = 0; x < sc.W; ++x) {
        int sx = (x / step) * step;
        float top = sc.H * baseFrac + ridge((float)sx, sc.W, seed, amp, amp * 0.5f, amp * 0.2f);
        top = std::floor(top / step) * step;
        for (int y = (int)top; y < sc.H; ++y) sc.put(x, y, y < top + std::max(1, step / 2) ? edge : fill);
    }
}

// Trees in several shapes, so no two neighbours match. kind: 0 round, 1 tall oval, 2 two-lobed,
// 3 pine, 4 leaning. All flat colour with one shadow band.
void tree(Scene& sc, float x, float baseY, int kind, float k, bool flip, Col leaf, Col leafD, Col trunk)
{
    float s = sc.s * k;
    float f = flip ? -1.0f : 1.0f;
    if (kind == 3) {   // pine: jagged tiers of needles
        sc.rect(x - 1.5f * s, baseY - 8 * s, 3 * s, 8 * s, trunk);
        for (int t = 0; t < 4; ++t) {
            float ty = baseY - 8 * s - t * 7 * s, half = (14 - t * 3) * s * 0.5f;
            sc.tri(x - half, ty, x + half, ty, x, ty - 11 * s, leaf);
            sc.tri(x + f * half * 0.1f, ty, x + f * half, ty, x, ty - 11 * s, leafD);
            for (int j = 0; j < 4; ++j) sc.put((int)(x - half + j * half * 0.6f), (int)ty, leafD);   // needle tips
        }
        return;
    }
    float lean = kind == 4 ? 4 * s * f : 0;
    for (int y = 0; y < (int)(14 * s); ++y)
        sc.rect(x - 1.5f * s + lean * y / (14 * s), baseY - y, 3 * s, 1, trunk);
    float cx = x + lean, cy = baseY - 18 * s;
    if (kind == 1) sc.disc(cx, cy - 3 * s, 7 * s, 11 * s, leaf, leafD);
    else if (kind == 2) { sc.disc(cx - 5 * s * f, cy, 7 * s, 6 * s, leaf, leafD); sc.disc(cx + 4 * s * f, cy - 4 * s, 7 * s, 7 * s, leaf, leafD); }
    else sc.disc(cx, cy, 9 * s, 8 * s, leaf, leafD);
}

// A house front: walls, gable roof, door and windows. Used for Gran's home in several stages.
void house(Scene& sc, float x, float baseY, float w, float h, Col wall, Col wallD, Col roof, Col roofD, Col window, Col door, bool chimney)
{
    sc.rect(x, baseY - h, w, h, wall);
    sc.rect(x + w * 0.8f, baseY - h, w * 0.2f, h, wallD);   // shaded side
    sc.tri(x - w * 0.08f, baseY - h, x + w * 1.08f, baseY - h, x + w * 0.5f, baseY - h - w * 0.45f, roof);
    sc.tri(x + w * 0.5f, baseY - h - w * 0.45f, x + w * 1.08f, baseY - h, x + w * 0.7f, baseY - h, roofD);
    if (chimney) sc.rect(x + w * 0.68f, baseY - h - w * 0.42f, w * 0.1f, w * 0.22f, wallD);
    sc.rect(x + w * 0.42f, baseY - h * 0.5f, w * 0.18f, h * 0.5f, door);
    sc.rect(x + w * 0.12f, baseY - h * 0.75f, w * 0.2f, h * 0.24f, window);
    sc.rect(x + w * 0.66f, baseY - h * 0.75f, w * 0.18f, h * 0.24f, window);
}

Col horizonOf(int stage)
{
    const uint32_t h[ERA_COUNT] = {0x101216, 0xe8e2d2, 0xbcdcf8, 0xf0b088, 0xf8dcc0, 0xf8b880, 0xf4ecdc};
    return hex(h[stage]);
}

void bgLayer(Px& p, int layer)
{
    Scene sc{p, p.w, p.h, p.h / 88.0f, horizonOf(g_era)};
    const int W = sc.W, H = sc.H;
    const float s = sc.s;
    const float fades[4] = {0, 0.6f, 0.35f, 0.12f};
    sc.fade = g_era == 0 ? 0 : fades[layer];
    const Col L = ONEBIT_LIGHT, D = ONEBIT_DARK;

    // ---------------- 1-bit: a black night with a few dotted outlines that sit back
    // The scenery recedes: every line is dotted (every other pixel), there is very little of it,
    // and nothing is filled, so Gran, Sam, enemies, platforms and fragments (solid white with a
    // black halo) always read in front of it.
    if (g_era == 0) {
        // a dotted line: Bresenham, lighting every other step
        auto dl = [&](int x0, int y0, int x1, int y1) {
            int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
            int err = dx + dy, n = 0;
            for (;;) {
                if ((n++ & 1) == 0) sc.put(((x0 % W) + W) % W, y0, L);
                if (x0 == x1 && y0 == y1) break;
                int e2 = 2 * err;
                if (e2 >= dy) { err += dy; x0 += sx; }
                if (e2 <= dx) { err += dx; y0 += sy; }
            }
        };
        if (layer == 0) {
            sc.rect(0, 0, (float)W, (float)H, D);
            const int stars[6][2] = {{12, 8}, {71, 5}, {150, 11}, {190, 21}, {231, 7}, {290, 15}};
            for (auto& st : stars) sc.put(st[0] % W, st[1], L);
            sc.disc(W * 0.3f, 14, 6, 6, L, L, false);    // the moon, a crescent
            sc.disc(W * 0.3f + 3, 12, 6, 6, D, D, false);
        } else if (layer == 1) {
            // one far ridge, gentle and dotted, kept above the roofs and tree tops of the near layer
            for (int x = 0; x < W; x += 2) {
                int top = (int)std::lround(H * 0.52f + ridge((float)x, W, 3, 3.5f, 1.5f, 0));
                sc.put(x, top, L);
            }
        } else if (layer == 3) {
            int g = (int)(H * 0.84f);
            // Gran's home: a small toy house in dotted outline (walls, gable, door)
            int hx = (int)(W * 0.15f);
            dl(hx, g - 12, hx + 16, g - 12);
            dl(hx, g - 12, hx, g);
            dl(hx + 16, g - 12, hx + 16, g);
            dl(hx - 2, g - 12, hx + 8, g - 20);
            dl(hx + 18, g - 12, hx + 8, g - 20);
            dl(hx + 6, g - 6, hx + 10, g - 6);
            dl(hx + 6, g - 6, hx + 6, g);
            dl(hx + 10, g - 6, hx + 10, g);
            // two bare trees, different shapes
            int t1 = (int)(W * 0.52f), t2 = (int)(W * 0.82f);
            dl(t1, g, t1, g - 10);
            for (int a = 0; a < 18; ++a)
                sc.put((int)std::lround(t1 + std::cos(a * 0.349f) * 5), (int)std::lround(g - 15 + std::sin(a * 0.349f) * 4), L);
            dl(t2, g, t2 + 1, g - 9);
            dl(t2 + 1, g - 9, t2 + 6, g - 14);
            dl(t2 + 1, g - 9, t2 - 4, g - 15);
        }
        return;
    }

    // ---------------- 4-shade: Gran's garden on a pale handheld screen
    if (g_era == 1) {
        Col c0 = hex(SHADES4[0]), c1 = hex(SHADES4[1]), c2 = hex(SHADES4[2]), c3 = hex(SHADES4[3]);
        sc.fade = 0;   // the four greys do the work
        if (layer == 0) {
            sc.rect(0, 0, (float)W, (float)H, c3);
            for (int i = 0; i < 4; ++i) sc.cloud(W * (0.12f + i * 0.25f), H * (0.14f + 0.08f * (i % 2)), 7 * s, c3, c2);
            for (int i = 0; i < 4; ++i) sc.frame(W * (0.12f + i * 0.25f) - 1, H * 0.14f, 1, 1, c3);
        } else if (layer == 1) {
            hills(sc, 0.62f, 3, 5 * s, c2, c2, 2);
        } else if (layer == 2) {
            float g = H * 0.8f;
            house(sc, W * 0.55f, g, 30 * s, 18 * s, c3, c2, c1, c0, c1, c1, true);   // Gran's cottage
            sc.rect(W * 0.55f - 4 * s, g, 38 * s, 2 * s, c1);
            tree(sc, W * 0.2f, g, 0, 1.0f, false, c2, c1, c1);
            tree(sc, W * 0.85f, g, 1, 0.9f, true, c2, c1, c1);
        } else {
            float g = H * 0.86f;
            for (int seg = 0; seg < 3; ++seg) {   // fence in sections, with gaps
                float fx = W * (0.05f + seg * 0.33f);
                for (int k = 0; k < 9; ++k) sc.rect(fx + k * 4 * s, g - 8 * s + (k % 3 == 1 ? s : 0), 2 * s, 8 * s, c3);
                sc.rect(fx, g - 6 * s, 36 * s, s, c3);
                sc.frame(fx - s, g - 9 * s, 38 * s, 1, c2);
            }
            // washing line with clothes
            float wx = W * 0.42f;
            sc.rect(wx, g - 22 * s, s, 22 * s, c0);
            sc.rect(wx + 40 * s, g - 22 * s, s, 22 * s, c0);
            sc.rect(wx, g - 21 * s, 40 * s, 1, c1);
            sc.rect(wx + 5 * s, g - 21 * s, 6 * s, 8 * s, c3);
            sc.rect(wx + 15 * s, g - 21 * s, 8 * s, 5 * s, c2);
            sc.rect(wx + 27 * s, g - 21 * s, 5 * s, 9 * s, c3);
        }
        return;
    }

    // ---------------- 8-bit: the school road, flat colour, stepped edges
    if (g_era == 2) {
        if (layer == 0) {
            sc.rect(0, 0, (float)W, H * 0.45f, hex(0x3c9cf0));
            sc.rect(0, H * 0.45f, (float)W, H * 0.55f, hex(0x8ccff0));
            for (int i = 0; i < 5; ++i) sc.cloud(W * (0.1f + i * 0.2f), H * (0.16f + 0.07f * (i % 3)), (6 + 2 * (i % 2)) * s, hex(0xfaf3e6), hex(0xb8b6c8));
        } else if (layer == 1) {
            hills(sc, 0.5f, 3, 9 * s, hex(0x8a7fb4), hex(0xb8b6c8), 4);
        } else if (layer == 2) {
            float g = H * 0.8f;
            hills(sc, 0.78f, 7, 2 * s, hex(0x1e8a4c), hex(0x6fdc4a), 2);
            house(sc, W * 0.3f, g, 28 * s, 16 * s, hex(0xf5c9a0), hex(0x8c4a2f), hex(0xe8364f), hex(0x6a2c6e), hex(0x3c9cf0), hex(0x8c4a2f), true);
            // the school
            float sx = W * 0.62f;
            sc.rect(sx, g - 26 * s, 60 * s, 26 * s, hex(0xb8b6c8));
            sc.rect(sx + 26 * s, g - 34 * s, 10 * s, 8 * s, hex(0xb8b6c8));
            sc.rect(sx + 28 * s, g - 32 * s, 6 * s, 4 * s, hex(0xfaf3e6));   // clock
            for (int k = 0; k < 5; ++k) sc.rect(sx + 4 * s + k * 11 * s, g - 20 * s, 6 * s, 6 * s, hex(0x26305e));
            sc.rect(sx + 27 * s, g - 10 * s, 8 * s, 10 * s, hex(0x8c4a2f));
        } else {
            float g = H * 0.86f;
            const int kinds[7] = {0, 1, 3, 2, 0, 4, 1};
            for (int i = 0; i < 7; ++i)
                tree(sc, W * (0.06f + i * 0.14f) + rnd(i, 1, 22) * 6 * s, g, kinds[i], 0.8f + 0.25f * rnd(i, 2, 22), i % 2,
                     hex(0x1e8a4c), hex(0x26305e), hex(0x8c4a2f));
            // a kite stuck in the second tree
            float kx = W * 0.2f + 6 * s, ky = g - 30 * s;
            sc.tri(kx, ky - 5 * s, kx + 4 * s, ky, kx, ky + 5 * s, hex(0xf7d73a));
            sc.tri(kx, ky - 5 * s, kx - 4 * s, ky, kx, ky + 5 * s, hex(0xe8364f));
            for (int k = 0; k < 8; ++k) sc.put((int)(kx + k * 0.5f * s), (int)(ky + 5 * s + k * s), hex(0xfaf3e6));
            // fence sections, a red mailbox and a bike against the fence
            for (int seg = 0; seg < 2; ++seg) {
                float fx = W * (0.3f + seg * 0.4f);
                for (int k = 0; k < 6; ++k) sc.rect(fx + k * 4 * s, g - 7 * s, 2 * s, 7 * s, hex(0xfaf3e6));
                sc.rect(fx, g - 5 * s, 24 * s, s, hex(0xfaf3e6));
            }
            float mx = W * 0.56f;
            sc.rect(mx, g - 10 * s, 2 * s, 10 * s, hex(0x5a5468));
            sc.rect(mx - 2 * s, g - 14 * s, 6 * s, 5 * s, hex(0xe8364f));
            float bx = W * 0.79f;
            for (int w2 = 0; w2 < 2; ++w2) sc.frame(bx + w2 * 10 * s, g - 7 * s, 7 * s, 7 * s, hex(0x26305e));
            sc.rect(bx + 3 * s, g - 9 * s, 12 * s, s, hex(0xe8364f));
            sc.rect(bx + 13 * s, g - 12 * s, s, 4 * s, hex(0x26305e));
        }
        return;
    }

    // ---------------- 16-bit: dusk in the woods, the treehouse
    if (g_era == 3) {
        if (layer == 0) {
            const uint32_t bands[5] = {0x463a66, 0x7a3a5a, 0xc06a8a, 0xf0a0b0, 0xf8d070};
            for (int b = 0; b < 5; ++b) sc.rect(0, H * (b * 0.13f), (float)W, H * 0.14f + 1, hex(bands[b]));
            sc.rect(0, H * 0.65f, (float)W, H * 0.35f, hex(0xf8d070));
            for (int i = 0; i < 4; ++i) sc.cloud(W * (0.14f + i * 0.25f), H * (0.18f + 0.06f * (i % 2)), 6 * s, hex(0xf0a0b0), hex(0xc06a8a));
        } else if (layer == 1) {
            hills(sc, 0.48f, 3, 12 * s, hex(0x463a66), hex(0xa8a4d0), 2);
        } else if (layer == 2) {
            for (int i = 0; i < 26; ++i)
                tree(sc, i * W / 26.0f + rnd(i, 1, 33) * 8 * s, H * 0.8f + rnd(i, 3, 33) * 4 * s, 3, 0.75f + 0.5f * rnd(i, 2, 33), i % 2,
                     hex(0x2a6a3a), hex(0x1a3a2a), hex(0x3a2018));
        } else {
            float g = H * 0.88f;
            for (int i = 0; i < 9; ++i) {
                float x = i * W / 9.0f + rnd(i, 4, 34) * 10 * s;
                sc.disc(x, g - 3 * s, (7 + 3 * rnd(i, 5, 34)) * s, 5 * s, hex(0x4a9a3a), hex(0x2a6a3a));
            }
            // the treehouse: a big oak, a plank house, a ladder and a rope swing
            float tx = W * 0.35f;
            sc.rect(tx - 4 * s, g - 50 * s, 8 * s, 50 * s, hex(0x6a3a22));
            sc.rect(tx + 2 * s, g - 50 * s, 2 * s, 50 * s, hex(0x3a2018));
            sc.disc(tx, g - 60 * s, 26 * s, 16 * s, hex(0x4a9a3a), hex(0x2a6a3a));
            sc.disc(tx - 18 * s, g - 52 * s, 12 * s, 9 * s, hex(0x4a9a3a), hex(0x2a6a3a));
            float hx = tx - 14 * s, hy = g - 48 * s;
            for (int k = 0; k < 5; ++k) sc.rect(hx, hy + k * 3 * s, 28 * s, 3 * s - 1, k % 2 ? hex(0x9a5a30) : hex(0xc88a4a));
            sc.tri(hx - 3 * s, hy, hx + 31 * s, hy, hx + 14 * s, hy - 10 * s, hex(0xa02a3a));
            sc.rect(hx + 10 * s, hy + 3 * s, 7 * s, 6 * s, hex(0xf8d070));
            sc.rect(hx + 13 * s, hy + 3 * s, s, 6 * s, hex(0x6a3a22));   // window frame
            sc.rect(hx - 2 * s, hy + 15 * s, 32 * s, 2 * s, hex(0x6a3a22));   // floor beam
            for (int k = 0; k < 9; ++k) sc.rect(tx + 6 * s, hy + 17 * s + k * 3.5f * s, 7 * s, s, hex(0xc88a4a));   // ladder
            sc.rect(tx + 6 * s, hy + 17 * s, s, 32 * s, hex(0x9a5a30));
            sc.rect(tx + 12 * s, hy + 17 * s, s, 32 * s, hex(0x9a5a30));
            sc.rect(tx - 20 * s, hy + 6 * s, s, 22 * s, hex(0xf0c070));   // rope swing
            sc.rect(tx - 23 * s, hy + 28 * s, 7 * s, 2 * s, hex(0x6a3a22));
        }
        return;
    }

    // ---------------- 32-bit: the town by the station, Gran's flat
    if (g_era == 4) {
        if (layer == 0) {
            const uint32_t bands[5] = {0x3a6ab0, 0x4a80c8, 0x5a98d8, 0x8ec8f0, 0xf8dcc0};
            for (int b = 0; b < 5; ++b) sc.rect(0, H * (b * 0.13f), (float)W, H * 0.14f + 1, hex(bands[b]));
            sc.rect(0, H * 0.65f, (float)W, H * 0.35f, hex(0xf8dcc0));
            for (int i = 0; i < 5; ++i) sc.cloud(W * (0.1f + i * 0.2f), H * (0.12f + 0.07f * (i % 3)), (5 + i % 2) * s, hex(0xffffff), hex(0xc8d8f0));
        } else if (layer == 1) {
            hills(sc, 0.56f, 3, 6 * s, hex(0x7a8ac8), hex(0xa8b8e0), 2);
        } else if (layer == 2) {
            float g = H * 0.7f;
            for (int i = 0; i < 12; ++i) {
                float x = i * W / 12.0f, w = (20 + 8 * rnd(i, 4, 44)) * s, h = (22 + 18 * rnd(i, 5, 44)) * s;
                bool granFlat = i == 4;
                Col wall = granFlat ? hex(0xe8c8a0) : mixc(hex(0xd8b890), hex(0xa86a5a), rnd(i, 6, 44));
                Col wallD = mixc(mul(wall, 0.75f), hex(0x3a2a6a), 0.2f);
                sc.rect(x, g - h, w, h + 40 * s, wall);
                sc.rect(x + w - 3 * s, g - h, 3 * s, h + 40 * s, wallD);
                sc.rect(x - s, g - h - 2 * s, w + 2 * s, 2 * s, wallD);   // cornice
                for (int wy = 0; wy < (int)(h / (9 * s)); ++wy)
                    for (int wx = 0; wx < 2; ++wx) {
                        float u = rnd(i, wx + wy * 3, 45);
                        Col win = u < 0.35f ? hex(0xfff0b0) : (u < 0.7f ? hex(0x3a5a8a) : hex(0x6a4a7a));
                        float ox = x + 4 * s + wx * w * 0.45f, oy = g - h + 4 * s + wy * 9 * s;
                        sc.rect(ox, oy, 5 * s, 6 * s, win);
                        if (u > 0.7f) sc.rect(ox, oy, 2 * s, 6 * s, hex(0xf0a0b0));   // curtain
                        sc.rect(ox - s, oy + 6 * s, 7 * s, s, wallD);                 // sill
                    }
                if (granFlat) {   // Gran's flat: flower boxes and a blue door
                    sc.rect(x + 3 * s, g - h + 10 * s, 8 * s, 2 * s, hex(0xe04a3a));
                    sc.rect(x + w * 0.45f + 3 * s, g - h + 10 * s, 8 * s, 2 * s, hex(0xf8d070));
                }
                if (i % 3 == 1) {   // a shop: striped awning and a sign
                    for (int k = 0; k < 6; ++k) sc.rect(x + k * w / 6, g - 6 * s, w / 6 + 1, 3 * s, k % 2 ? hex(0xfaf3e6) : hex(0x2a8a6a));
                    sc.rect(x + w * 0.2f, g - 11 * s, w * 0.6f, 4 * s, hex(0x2a2140));
                }
            }
            // the rail bridge: deck, rails, arches, and the train standing on it
            float vy = H * 0.66f;
            sc.rect(0, vy, (float)W, 6 * s, hex(0x8a6a5a));
            sc.rect(0, vy, (float)W, s, hex(0xb08a7a));
            sc.rect(0, vy - s, (float)W, s, hex(0x4a4a5a));   // rails
            for (int a = 0; a < 18; ++a) {
                float ax = a * W / 18.0f, aw = W / 18.0f;
                for (int y = 0; y < (int)(26 * s); ++y)
                    for (int x = 0; x < (int)aw; ++x) {
                        float nx = (x - aw / 2) / (aw * 0.38f), ny = (y - 26 * s) / (20 * s);
                        if (nx * nx + ny * ny > 1) sc.put((int)(ax + x), (int)(vy + 6 * s + y), hex(0x7a5a4a));
                    }
            }
            for (int car = 0; car < 4; ++car) {
                float cx = W * 0.55f + car * 24 * s;
                sc.rect(cx, vy - 11 * s, 22 * s, 10 * s, car == 0 ? hex(0x2a5a8a) : hex(0x3a7ab0));
                sc.rect(cx, vy - 11 * s, 22 * s, s, hex(0x8ac8f0));
                for (int w2 = 0; w2 < 4; ++w2) sc.rect(cx + 2 * s + w2 * 5 * s, vy - 9 * s, 3 * s, 3 * s, hex(0xfff0b0));
                sc.disc(cx + 4 * s, vy - s, 2 * s, 2 * s, hex(0x2a2140), hex(0x2a2140), false);
                sc.disc(cx + 18 * s, vy - s, 2 * s, 2 * s, hex(0x2a2140), hex(0x2a2140), false);
            }
        } else {
            float g = H * 0.9f;
            for (int i = 0; i < 14; ++i) {
                float x = (i + 0.5f) * W / 14;
                sc.disc(x, g - 4 * s, (9 + 3 * rnd(i, 1, 46)) * s, (5 + 2 * rnd(i, 2, 46)) * s, hex(0x3a9a4a), hex(0x2a6a3a));
                if (i % 3 == 0) {   // street lamp: post, arm, lamp head
                    sc.rect(x + 10 * s, g - 30 * s, 2 * s, 30 * s, hex(0x2a2a3a));
                    sc.rect(x + 10 * s, g - 30 * s, 6 * s, s, hex(0x2a2a3a));
                    sc.rect(x + 14 * s, g - 29 * s, 3 * s, 2 * s, hex(0xfff0b0));
                }
            }
        }
        return;
    }

    // ---------------- 2.5D and 3D: big sky, far hills; the 2.5D city row
    if (layer == 0) {
        bool morning = g_era == 6;
        const uint32_t dusk[6] = {0x2a2a6a, 0x5a3a7a, 0x9a4a7a, 0xd86a6a, 0xf09a6a, 0xf8c080};
        const uint32_t morn[6] = {0x3a6ab8, 0x5a88c8, 0x7aa8d8, 0x9cc8ea, 0xc8e0f0, 0xf4ecdc};
        for (int b = 0; b < 6; ++b) sc.rect(0, H * (b * 0.11f), (float)W, H * 0.12f + 1, hex(morning ? morn[b] : dusk[b]));
        sc.rect(0, H * 0.66f, (float)W, H * 0.34f, hex(morning ? morn[5] : dusk[5]));
        for (int half = 0; half < 2; ++half)
            sc.disc(W * (morning ? 0.36f : 0.3f) + half * W / 2, H * (morning ? 0.2f : 0.55f), 0.07f * H, 0.07f * H, hex(0xfff0c0), hex(0xfff0c0), false);
        return;
    }
    if (layer == 1) {
        hills(sc, 0.6f, 3, 14, g_era == 6 ? hex(0x7a98c8) : hex(0x8a5a8a), g_era == 6 ? hex(0xa8c0e0) : hex(0xb07aa0), 1);
        return;
    }
    if (g_era == 6) return;   // the 3D stage builds its own trees and houses
    if (layer == 2) {
        float g = H * 0.76f;
        for (int i = 0; i < 9; ++i) {
            float x = i * W / 9.0f + rnd(i, 3, 3) * 30, w = 60 + rnd(i, 4, 4) * 40, h = 50 + rnd(i, 5, 5) * 40;
            Col wall = mixc(hex(0x4a3a5a), hex(0x7a5a6a), rnd(i, 6, 6));
            house(sc, x, g + 30, w, h + 30, wall, mul(wall, 0.8f), hex(0x3a2a3a), hex(0x2a1a2a),
                  rnd(i, 7, 7) < 0.6f ? hex(0xffd080) : hex(0x2a2a4a), hex(0x2a1a2a), i % 2 == 0);
        }
    } else {
        float g = H * 0.86f;
        for (int i = 0; i < 12; ++i) {
            float x = (i + 0.5f) * W / 12;
            tree(sc, x, g, i % 5 == 3 ? 1 : 0, 2.6f + 0.6f * rnd(i, 8, 9), i % 2, hex(0x3a4a3a), hex(0x2a3a3a), hex(0x2a1a2a));
            if (i % 2 == 0) {
                sc.rect(x + 30, g - 50, 3, 52, hex(0x2a2a3a));
                sc.rect(x + 27, g - 54, 9, 5, hex(0xffe0a0));
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
        // the story boss of each stage (stage 6 keeps the Forgetting itself, modelled in 3D)
        if (era < LAST_ERA) save(makeSheet(XF_COUNT, BOSS_FS, BOSS_FS, true, drawStoryBoss), d + "boss_story.png");
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
            bgLayer(p, l);
            save(p.img, d + "bg_" + std::to_string(l) + ".png");
        }
        save(wallTexture(era), d + "wall.png");
    }
    for (int i = 0; i < ENDING_COUNT; ++i) save(endingPicture(i), dir + "/art/ending_" + std::to_string(i) + ".png");
    return n;
}
