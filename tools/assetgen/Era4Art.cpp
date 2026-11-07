// Era4Art.cpp: art for era 4, "modern day".
//
// Same file names, image sizes and sheet layouts as era 3 (64 px per tile,
// 1280 x 360 backgrounds), drawn in a modern indie "hand-painted HD" look:
//   - one golden-hour palette and one light for everything:
//     warm key from the top left, cool violet shadow, cool blue bounce/rim
//     on the lower right edge of every rounded form
//   - smooth anti-aliased shapes (4x4 supersampling) and soft two-tone shading
//   - a soft 1 px plum outline on characters, enemies and pickups
//   - background layers use atmospheric perspective: far = pale and hazy,
//     near = darker and more saturated
// No random speckle: every texture is a smooth function.
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

// Era 4 shares era 3's resolution and sizes.
constexpr int E4_TILE = 64;
constexpr int E4_SS = 4;
constexpr int E4_BG_H = 360;
constexpr int E4_BG_W = E4_BG_H * 32 / 9;   // 1280, same rule as bgW()

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
float smooth(float e0, float e1, float x)
{
    float t = clamp01((x - e0) / (e1 - e0));
    return t * t * (3 - 2 * t);
}

uint32_t hash3(int x, int y, int z)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float rnd(int x, int y, int z) { return (hash3(x, y, z) & 0xffff) / 65535.0f; }

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

// ------------------------------------------------------------------ the one light and palette
const Col KEY = hex(0xffe0ae);      // warm golden-hour key light, top left
const Col SHADOW = hex(0x3a3268);   // cool violet shadow
const Col BOUNCE = hex(0x9cc6ec);   // cool sky bounce, rim on the lower right
const Col SPEC = hex(0xfff4e0);
const Col INK = hex(0x2a1d33);      // outline and darkest accents (deep plum)
const Col CREAM = hex(0xfff3e2);

// ------------------------------------------------------------------ canvas
struct Frag {
    float x, y;
    float nx, ny;
    int ix, iy;
};
using Fill = std::function<Col(const Frag&)>;

struct Canvas {
    int w = 0, h = 0;
    int ss = 1;
    float ppu = 1;
    float tpu = 1;
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

    // A soft round glow (alpha falls off to the edge).
    void glow(float cx, float cy, float r, Col c, float a)
    {
        circle(cx, cy, r, [c, a](const Frag& f) {
            float d = std::sqrt(f.nx * f.nx + f.ny * f.ny);
            return withA(c, a * (1 - d) * (1 - d));
        });
    }
};

// ------------------------------------------------------------------ fills
Fill solid(Col c) { return [c](const Frag&) { return c; }; }

// Soft painted shading under the one golden-hour light, with a cool rim on
// the lower right and a faint warm edge on the upper left.
Col litColor(Col c, float nx, float ny, float strength)
{
    float r2 = nx * nx + ny * ny;
    float nz = std::sqrt(std::max(0.0f, 1 - 0.85f * r2));
    float d = -0.5f * nx - 0.62f * ny + 0.6f * nz;
    float t = smooth(-0.05f, 0.6f, d);
    Col sh = mixc(mul(c, 0.6f), SHADOW, 0.22f);
    Col li = mixc(c, KEY, 0.14f);
    Col o = mixc(sh, li, 1 - (1 - t) * strength);
    float edge = smooth(0.55f, 1.0f, r2);
    float side = clamp01(0.7f * nx + 0.7f * ny + 0.1f);
    o = mixc(o, mixc(c, BOUNCE, 0.65f), edge * side * 0.55f * strength);
    float side2 = clamp01(-0.7f * nx - 0.7f * ny);
    o = mixc(o, KEY, edge * side2 * 0.22f * strength);
    float spec = std::pow(std::max(0.0f, d), 18.0f) * 0.22f * strength;
    o = mixc(o, SPEC, clamp01(spec));
    o.a = c.a;
    return o;
}
Fill paint(Col c, float strength = 1.0f)
{
    return [c, strength](const Frag& f) { return litColor(c, f.nx, f.ny, strength); };
}
// Vertical gradient (top lit, bottom in cool shadow), for flat panels.
Fill panel(Col c, float strength = 1.0f)
{
    return [c, strength](const Frag& f) {
        float t = (f.ny + 1) * 0.5f;
        Col o = mixc(mixc(c, KEY, 0.12f * strength), mixc(mul(c, 0.72f), SHADOW, 0.15f), t * strength);
        o.a = c.a;
        return o;
    };
}

// ------------------------------------------------------------------ finalise
Color toColor(Col c)
{
    return Color{(unsigned char)std::lround(clamp01(c.r) * 255), (unsigned char)std::lround(clamp01(c.g) * 255),
                 (unsigned char)std::lround(clamp01(c.b) * 255), (unsigned char)std::lround(clamp01(c.a) * 255)};
}

Col over(Col top, Col under)
{
    float a = top.a + under.a * (1 - top.a);
    if (a <= 0) return Col{};
    return {(top.r * top.a + under.r * under.a * (1 - top.a)) / a, (top.g * top.a + under.g * under.a * (1 - top.a)) / a,
            (top.b * top.a + under.b * under.a * (1 - top.a)) / a, a};
}

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
            out[(size_t)y * tw + x] = c;
        }
    if (outline) {
        // Soft anti-aliased plum outline: each pixel sits over the strongest
        // neighbouring alpha, so silhouettes read on any background.
        std::vector<Col> res = out;
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < tw; ++x) {
                float m = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= tw || ny >= th) continue;
                        float a = out[(size_t)ny * tw + nx].a * ((dx && dy) ? 0.6f : 1.0f);
                        m = std::max(m, a);
                    }
                Col& o = res[(size_t)y * tw + x];
                if (m > o.a) o = over(out[(size_t)y * tw + x], withA(INK, m * 0.9f));
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

Image makeSheet(int frames, float fw, float fh, bool outline, const std::function<void(Canvas&, int)>& draw)
{
    int pw = (int)std::lround(fw * E4_TILE), ph = (int)std::lround(fh * E4_TILE);
    Image sheet = GenImageColor(pw * frames, ph, BLANK);
    for (int f = 0; f < frames; ++f) {
        Canvas cv(fw, fh, (float)E4_TILE, E4_SS);
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
    // who 0 = Gran (warm orange cardigan), who 1 = Sam (cool blue hoodie, headphones).
    const bool gran = who == 0;
    Pose p = playerPose(frame);
    Col skin = gran ? hex(0xf0c09a) : hex(0xe6b088);
    Col top = gran ? hex(0xe8843a) : hex(0x3f86c8);
    Col topDark = gran ? hex(0xb85a2c) : hex(0x2c5e9a);
    Col legs = gran ? hex(0x6e4658) : hex(0x2e3e66);
    Col shoes = gran ? hex(0x4a3040) : hex(0xf2ece4);
    Col hair = gran ? hex(0xe6dee8) : hex(0x5a3626);

    float hx = 0.52f + p.lean, hy = 0.42f + p.bob;
    Vector2 shoulder{0.5f + p.lean * 0.8f, 0.72f + p.bob};
    Vector2 hip{0.5f + p.lean * 0.3f, 1.06f + p.bob};

    if (frame == PF_DASH)   // soft speed trails in the player's colour
        for (int i = 0; i < 3; ++i)
            c.capsule(0.02f, 0.75f + i * 0.18f, 0.24f + i * 0.04f, 0.75f + i * 0.18f, 0.028f, [&](const Frag& f) {
                return withA(mixc(top, KEY, 0.3f), clamp01((f.x - 0.0f) / 0.26f) * 0.75f);
            });

    if (frame == PF_SPECIAL && gran) {   // Gran's umbrella glide
        Col canopy = hex(0xc0466a);
        c.capsule(p.handF.x, p.handF.y, p.handF.x, 0.16f, 0.022f, paint(hex(0x4a3040)));
        c.poly({{0.06f, 0.24f}, {0.2f, 0.08f}, {0.52f, 0.02f}, {0.84f, 0.08f}, {0.98f, 0.24f},
                {0.82f, 0.2f}, {0.68f, 0.24f}, {0.52f, 0.2f}, {0.36f, 0.24f}, {0.22f, 0.2f}}, paint(canopy));
        c.capsule(0.52f, 0.04f, 0.36f, 0.22f, 0.008f, solid(withA(mul(canopy, 0.7f), 0.8f)));
        c.capsule(0.52f, 0.04f, 0.68f, 0.22f, 0.008f, solid(withA(mul(canopy, 0.7f), 0.8f)));
    }
    if (frame == PF_SPECIAL && !gran) {  // Sam's time pulse ring
        Col ring = hex(0x9adcf0);
        c.shape(0, 0, 1, 1.5f, [&](Frag& f) {
            float dx = f.x - 0.5f, dy = (f.y - 0.8f) * 0.8f;
            float d = std::sqrt(dx * dx + dy * dy);
            f.nx = (d - 0.45f) / 0.05f; f.ny = 0;
            return d > 0.4f && d < 0.5f;
        }, [ring](const Frag& f) { return withA(mixc(ring, CREAM, 0.3f), (1 - std::fabs(f.nx)) * 0.9f); });
    }

    float limb = 0.075f;
    c.capsule(shoulder.x - 0.06f, shoulder.y + 0.04f, p.handB.x, p.handB.y, 0.06f, paint(topDark));
    c.circle(p.handB.x, p.handB.y, 0.055f, paint(mul(skin, 0.9f)));
    c.capsule(hip.x - 0.05f, hip.y, p.footB.x, p.footB.y - 0.04f, limb, paint(mul(legs, 0.82f)));
    c.ellipse(p.footB.x + 0.04f, p.footB.y - 0.02f, 0.1f, 0.055f, paint(mul(shoes, 0.85f)));

    float tx = 0.32f + p.lean * 0.6f;
    if (gran) {
        c.rect(tx, 0.62f + p.bob, 0.36f, 0.52f, paint(top), 0.1f);
        c.rect(tx + 0.03f, 0.98f + p.bob, 0.3f, 0.18f, paint(legs), 0.04f);   // skirt
        c.capsule(tx + 0.19f, 0.66f + p.bob, tx + 0.19f, 0.96f + p.bob, 0.012f, solid(withA(topDark, 0.8f)));  // cardigan edge
        c.circle(tx + 0.23f, 0.74f + p.bob, 0.018f, paint(CREAM));
        c.circle(tx + 0.23f, 0.86f + p.bob, 0.018f, paint(CREAM));
    } else {
        c.circle(tx + 0.08f, 0.66f + p.bob, 0.1f, paint(topDark));   // hood
        c.rect(tx, 0.62f + p.bob, 0.36f, 0.5f, paint(top), 0.08f);
        c.rect(tx + 0.08f, 0.92f + p.bob, 0.22f, 0.08f, paint(topDark, 0.5f), 0.03f);   // pocket
        c.capsule(tx + 0.22f, 0.68f + p.bob, tx + 0.23f, 0.8f + p.bob, 0.01f, solid(CREAM));  // drawstring
    }

    c.capsule(hip.x + 0.05f, hip.y, p.footF.x, p.footF.y - 0.04f, limb, paint(legs));
    c.ellipse(p.footF.x + 0.04f, p.footF.y - 0.02f, 0.1f, 0.055f, paint(shoes));
    if (!gran) c.rect(p.footF.x - 0.05f, p.footF.y + 0.005f, 0.18f, 0.025f, solid(hex(0xd8d0c8)), 0.01f);  // sneaker sole

    // head
    c.ellipse(hx, hy, 0.21f, 0.2f, paint(skin));
    c.ellipse(hx + 0.13f, hy + 0.07f, 0.04f, 0.025f, solid(withA(hex(0xe87a6a), 0.35f)));   // warm cheek
    if (gran) {
        c.ellipse(hx - 0.05f, hy - 0.09f, 0.2f, 0.12f, paint(hair));
        c.ellipse(hx - 0.13f, hy + 0.0f, 0.09f, 0.15f, paint(hair));
        c.ellipse(hx - 0.09f, hy - 0.23f, 0.09f, 0.075f, paint(hair));   // bun
    } else {
        c.ellipse(hx - 0.04f, hy - 0.08f, 0.21f, 0.13f, paint(hair));
        c.ellipse(hx - 0.14f, hy + 0.0f, 0.08f, 0.13f, paint(hair));
        c.ellipse(hx + 0.08f, hy - 0.16f, 0.1f, 0.05f, paint(hair));   // fringe tuft
        // headphones: band over the head, a cup over the ear
        Col phones = hex(0x24766e);
        c.shape(hx - 0.3f, hy - 0.35f, hx + 0.3f, hy + 0.05f, [&](Frag& f) {
            float dx = (f.x - hx + 0.02f) / 0.23f, dy = (f.y - hy + 0.02f) / 0.24f;
            float d = std::sqrt(dx * dx + dy * dy);
            f.nx = dx; f.ny = dy;
            return d > 0.86f && d < 1.0f && dy < 0.1f;
        }, paint(mul(phones, 0.8f)));
        c.ellipse(hx - 0.09f, hy + 0.02f, 0.045f, 0.07f, paint(phones));
    }
    c.ellipse(hx + 0.095f, hy - 0.005f, 0.022f, 0.03f, solid(INK));     // eye
    c.circle(hx + 0.088f, hy - 0.017f, 0.007f, solid(CREAM));
    if (gran) {   // round glasses
        c.shape(hx, hy - 0.08f, hx + 0.22f, hy + 0.06f, [&](Frag& f) {
            float dx = f.x - (hx + 0.1f), dy = f.y - (hy - 0.005f);
            float d = std::sqrt(dx * dx + dy * dy);
            f.nx = dx; f.ny = dy;
            return d > 0.048f && d < 0.062f;
        }, solid(hex(0x6a4a3a)));
    }
    c.capsule(hx + 0.08f, hy + 0.09f, hx + 0.13f, hy + 0.085f, 0.009f, solid(mul(skin, 0.62f)));   // smile

    c.capsule(shoulder.x + 0.05f, shoulder.y + 0.04f, p.handF.x, p.handF.y, 0.06f, paint(top));
    c.circle(p.handF.x, p.handF.y, 0.055f, paint(skin));

    if (frame == PF_ATTACK) {
        c.glow(0.94f, 0.84f, 0.16f, KEY, 0.8f);
        c.poly({{0.98f, 0.66f}, {1.02f, 0.82f}, {1.0f, 0.86f}, {0.86f, 0.84f}, {0.98f, 0.9f}, {0.92f, 1.02f}},
               solid(withA(hex(0xfff0b8), 0.95f)));
    }
}

// ------------------------------------------------------------------ enemies (the Forgetting)
const Col FORGET = hex(0x7a66a8);       // the Forgetting's violet
const Col FORGET_LIGHT = hex(0xc6b2e6);
const Col FORGET_DEEP = hex(0x2e2550);

// Goo with a soft inner glow and gentle scanline shimmer (the "static", made smooth).
Fill gooFill(Col base, int frame)
{
    return [base, frame](const Frag& f) {
        Col o = litColor(base, f.nx, f.ny, 1.0f);
        float inner = clamp01(1 - std::sqrt((f.nx + 0.15f) * (f.nx + 0.15f) + (f.ny - 0.2f) * (f.ny - 0.2f)));
        o = mixc(o, FORGET_LIGHT, inner * 0.25f);
        float band = 0.5f + 0.5f * std::sin(f.y * 38.0f + frame * 1.6f);
        o = mixc(o, FORGET_LIGHT, smooth(0.8f, 1.0f, band) * 0.12f);
        o.a = base.a;
        return o;
    };
}

void drawBlob(Canvas& c, int frame)
{
    float sq = 0;
    if (frame <= BF_WALK3) sq = 0.08f * std::sin(frame * 1.5707963f);
    Col body = FORGET;
    if (frame == BF_SQUASH) {
        c.ellipse(0.5f, 0.88f, 0.46f, 0.12f, gooFill(body, frame));
        c.capsule(0.32f, 0.86f, 0.4f, 0.86f, 0.015f, solid(INK));
        c.capsule(0.58f, 0.86f, 0.66f, 0.86f, 0.015f, solid(INK));
        return;
    }
    float step = (frame % 2) ? 0.04f : -0.04f;
    c.ellipse(0.3f + step, 0.93f, 0.1f, 0.06f, paint(mul(body, 0.7f)));
    c.ellipse(0.7f - step, 0.93f, 0.1f, 0.06f, paint(mul(body, 0.7f)));
    float ry = 0.36f * (1 - sq), rx = 0.42f * (1 + sq);
    c.ellipse(0.5f, 0.92f - ry, rx, ry, gooFill(body, frame));
    float ey = 0.92f - ry * 1.15f;
    c.ellipse(0.62f, ey, 0.1f, 0.12f, paint(CREAM, 0.5f));
    c.ellipse(0.82f, ey, 0.08f, 0.1f, paint(CREAM, 0.5f));
    c.circle(0.655f, ey + 0.01f, 0.045f, solid(INK));
    c.circle(0.84f, ey + 0.01f, 0.038f, solid(INK));
    c.circle(0.64f, ey - 0.01f, 0.014f, solid(CREAM));
    // antenna with a soft glowing tip
    float ty = 0.92f - 2 * ry - 0.1f;
    c.capsule(0.4f, 0.92f - 2 * ry + 0.05f, 0.32f, ty + 0.02f, 0.022f, paint(mul(body, 0.75f)));
    c.glow(0.31f, ty, 0.09f, FORGET_LIGHT, 0.7f);
    c.circle(0.31f, ty, 0.035f, paint(hex(0xeee0ff), 0.5f));
}

void drawFlyer(Canvas& c, int frame)
{
    Col body = hex(0xe0605a);
    Col wing = hex(0x6fb4cc, 0.88f);
    if (frame == FF_HURT) { body = CREAM; wing = hex(0xf2c25a, 0.9f); }
    float flap = std::sin(frame * 1.5707963f);
    float wy = 0.5f - 0.3f * flap;
    // glitch afterimage: a cool offset ghost of the body
    if (frame != FF_HURT) {
        float gx = (frame % 2 ? -0.07f : 0.06f);
        c.poly({{0.5f + gx, 0.18f}, {0.78f + gx, 0.5f}, {0.5f + gx, 0.84f}, {0.22f + gx, 0.5f}},
               solid(withA(BOUNCE, 0.35f)));
    }
    c.poly({{0.42f, 0.5f}, {0.04f, wy - 0.05f}, {0.16f, wy + 0.12f}}, paint(mul(wing, 0.85f)));
    c.poly({{0.58f, 0.5f}, {0.96f, wy - 0.05f}, {0.84f, wy + 0.12f}}, paint(wing));
    c.poly({{0.5f, 0.18f}, {0.78f, 0.5f}, {0.5f, 0.84f}, {0.22f, 0.5f}}, paint(body));
    c.circle(0.56f, 0.46f, 0.12f, paint(CREAM, 0.4f));
    c.circle(0.6f, 0.47f, 0.055f, solid(INK));
    c.circle(0.585f, 0.45f, 0.016f, solid(CREAM));
}

void drawBrute(Canvas& c, int frame)
{
    Col fog = hex(0x76688e);
    Col fogLight = hex(0xb4a4c6);
    Col eye = hex(0xff7a4a);
    if (frame == RF_HURT) { fog = hex(0xd8cfe8); fogLight = CREAM; }
    float lean = frame == RF_CHARGE ? 0.18f : 0.0f;
    float step = (frame <= RF_WALK3) ? 0.1f * std::sin(frame * 1.5707963f) : 0.0f;
    c.rect(0.55f + step, 1.5f, 0.3f, 0.45f, paint(mul(fog, 0.72f)), 0.1f);
    c.rect(1.15f - step, 1.5f, 0.3f, 0.45f, paint(mul(fog, 0.8f)), 0.1f);
    c.capsule(0.6f + lean, 0.9f, 0.35f + lean - step, 1.45f, 0.16f, paint(mul(fog, 0.78f)));
    c.circle(1.0f + lean, 1.05f, 0.58f, paint(fog));
    c.circle(0.7f + lean, 0.7f, 0.36f, paint(fogLight));
    c.circle(1.25f + lean, 0.62f, 0.4f, paint(fog));
    c.circle(1.0f + lean, 0.45f, 0.34f, paint(fogLight));
    c.circle(1.45f + lean, 1.05f, 0.3f, paint(fog));
    float ey = frame == RF_CHARGE ? 0.72f : 0.68f;
    if (frame != RF_HURT) {
        c.glow(1.28f + lean, ey, 0.2f, eye, 0.55f);
        c.glow(1.58f + lean, ey + 0.02f, 0.17f, eye, 0.55f);
    }
    c.ellipse(1.28f + lean, ey, 0.1f, frame == RF_CHARGE ? 0.1f : 0.07f, paint(eye, 0.4f));
    c.ellipse(1.58f + lean, ey + 0.02f, 0.08f, frame == RF_CHARGE ? 0.09f : 0.06f, paint(eye, 0.4f));
    c.circle(1.25f + lean, ey - 0.02f, 0.025f, solid(hex(0xffe0b0)));
    c.capsule(1.4f + lean, 0.95f, 1.65f + lean + step, 1.45f, 0.17f, paint(fogLight));
}

void drawBoss(Canvas& c, int frame)
{
    Col dark = FORGET_DEEP;
    Col swirl = hex(0x6a4c92);
    Col ringC = hex(0x9a7ac8);
    Col iris = hex(0xf2b84a);
    float wob = frame == XF_FLOAT1 ? 0.06f : 0.0f;
    float slam = frame == XF_SLAM ? 0.25f : 0.0f;
    if (frame == XF_HURT) { dark = hex(0xb8acc8); swirl = CREAM; ringC = CREAM; }
    float cy = 1.4f + wob + slam;
    auto swirlFill = [&](float ox, float oy, float seed) {
        return [=](const Frag& f) {
            float dx = f.x - ox, dy = f.y - oy;
            float ang = std::atan2(dy, dx), r = std::sqrt(dx * dx + dy * dy);
            float s = 0.5f + 0.5f * std::sin(ang * 3 + r * 7 + seed);
            Col base = mixc(dark, swirl, smooth(0.3f, 0.9f, s) * 0.8f);
            Col o = litColor(base, f.nx, f.ny, 0.9f);
            o.a = 1;
            return o;
        };
    };
    for (int i = 0; i < 6; ++i) {
        float x = 0.55f + i * 0.38f;
        float sway = std::sin(i * 1.7f + frame * 1.3f) * 0.18f;
        c.capsule(x, 1.9f + slam, x + sway, 2.85f - slam * 0.3f, 0.12f - i % 2 * 0.03f,
                  paint(mixc(dark, swirl, 0.45f)));
        c.glow(x + sway, 2.85f - slam * 0.3f, 0.1f, FORGET_LIGHT, frame == XF_HURT ? 0 : 0.5f);
    }
    c.glow(1.5f, cy, 1.5f, FORGET, frame == XF_HURT ? 0.2f : 0.45f);
    c.ellipse(1.5f, cy, 1.25f, 1.1f - slam * 0.4f, swirlFill(1.5f, cy, frame * 0.9f));
    c.shape(0, 0, 3, 3, [&](Frag& f) {
        float dx = (f.x - 1.5f) / 1.0f, dy = (f.y - 1.35f - wob - slam) / 0.85f;
        float d = std::sqrt(dx * dx + dy * dy);
        f.nx = dx; f.ny = dy;
        return d > 0.86f && d < 0.98f;
    }, paint(ringC));
    float ey = 1.32f + wob + slam;
    c.ellipse(1.5f, ey, 0.62f, 0.42f - slam * 0.3f, paint(CREAM, 0.6f));
    if (frame != XF_HURT) {
        float ir = 0.28f - slam * 0.2f;
        c.circle(1.62f, ey, ir, [&](const Frag& f) {
            float d = std::sqrt(f.nx * f.nx + f.ny * f.ny);
            Col o = mixc(hex(0xffd88a), hex(0xc0702a), d);
            return litColor(o, f.nx, f.ny, 0.5f);
        });
        c.circle(1.66f, ey, 0.13f - slam * 0.1f, solid(INK));
        c.circle(1.55f, ey - 0.12f, 0.05f, solid(withA(CREAM, 0.95f)));
        c.circle(1.72f, ey + 0.08f, 0.022f, solid(withA(CREAM, 0.7f)));
    } else {
        c.capsule(1.1f, 1.1f, 1.9f, 1.55f, 0.05f, solid(INK));
        c.capsule(1.1f, 1.55f, 1.9f, 1.1f, 0.05f, solid(INK));
    }
}

// ------------------------------------------------------------------ tiles
void drawTile(Canvas& c, int t)
{
    const float TAU = 6.2831853f;
    switch (t) {
    case T_GROUND_TOP:
    case T_GROUND: {
        Col soil = hex(0x8a5a40), soilDeep = hex(0x553846);
        c.rect(0, 0, 1, 1, [&](const Frag& f) {
            float strata = 0.5f + 0.5f * std::sin(f.y * 20.0f + 0.6f * std::sin(f.x * TAU));
            Col d = mixc(soil, soilDeep, f.y * 0.55f);
            return mixc(d, mul(d, 0.86f), smooth(0.75f, 1.0f, strata) * 0.6f);
        });
        const float sxs[3] = {0.22f, 0.66f, 0.42f}, sys[3] = {0.52f, 0.7f, 0.86f};
        for (int i = 0; i < 3; ++i) c.ellipse(sxs[i], sys[i] + (t == T_GROUND ? -0.15f * (i == 0) : 0), 0.085f, 0.055f, paint(hex(0xb4a49c), 0.9f));
        if (t == T_GROUND_TOP) {
            Col grass = hex(0x9cc055), grassDeep = hex(0x4c8a4c);
            c.shape(0, 0, 1, 0.34f, [&](Frag& f) {
                float edge = 0.24f + 0.035f * std::sin(f.x * TAU * 3) + 0.015f * std::sin(f.x * TAU * 7 + 1);
                f.nx = 0; f.ny = f.y / edge;
                return f.y < edge;
            }, [&](const Frag& f) {
                Col g = mixc(grass, grassDeep, smooth(0.0f, 1.0f, f.ny));
                if (f.y < 0.035f) g = mixc(g, hex(0xe0e690), 0.6f);   // sunlit top edge
                return g;
            });
            for (int i = 0; i < 4; ++i) {
                float gx = 0.12f + i * 0.25f;
                c.poly({{gx - 0.05f, 0.24f}, {gx + 0.05f, 0.24f}, {gx + 0.01f, 0.36f + 0.04f * (i % 2)}}, solid(grassDeep));
            }
        }
        break;
    }
    case T_BLOCK:
    case T_CRACKED: {
        Col brick = t == T_BLOCK ? hex(0xc0603e) : hex(0xb0a2a0);
        Col mortar = t == T_BLOCK ? hex(0x5a3a40) : hex(0x5e5262);
        c.rect(0, 0, 1, 1, solid(mortar));
        for (int row = 0; row < 2; ++row)
            for (int col = -1; col < 2; ++col) {
                float off = row ? 0.25f : -0.25f;
                float x = col * 0.5f + 0.25f + off;
                c.rect(x + 0.03f, row * 0.5f + 0.04f, 0.44f, 0.42f, panel(mixc(brick, hex(0xd87a50), 0.12f * (col + 1 + row) / 3)), 0.04f);
            }
        if (t == T_CRACKED) {
            Col crack = INK;
            c.capsule(0.5f, 0.0f, 0.38f, 0.32f, 0.035f, solid(crack));
            c.capsule(0.38f, 0.32f, 0.6f, 0.58f, 0.035f, solid(crack));
            c.capsule(0.6f, 0.58f, 0.46f, 1.0f, 0.035f, solid(crack));
            c.capsule(0.6f, 0.58f, 0.85f, 0.7f, 0.025f, solid(crack));
            c.capsule(0.53f, 0.02f, 0.41f, 0.32f, 0.01f, solid(withA(KEY, 0.6f)));   // lit lip of the crack
        }
        break;
    }
    case T_P1:
    case T_P2: {
        Col base = t == T_P1 ? hex(0xe8843a, 0.92f) : hex(0x3f86c8, 0.92f);
        Col glowC = t == T_P1 ? hex(0xffd27a) : hex(0x9adcf0);
        c.rect(0.0f, 0.0f, 1.0f, 0.55f, panel(base), 0.12f);
        c.rect(0.12f, 0.1f, 0.76f, 0.12f, solid(withA(glowC, 0.95f)), 0.05f);
        for (int i = 0; i < 3; ++i) c.circle(0.2f + i * 0.3f, 0.38f, 0.035f, solid(t == T_P1 ? glowC : INK));
        break;
    }
    case T_WALL: {
        Col stone = hex(0x9a8a96), dark = hex(0x463a52);
        c.rect(0, 0, 1, 1, solid(dark));
        c.rect(0.05f, 0.04f, 0.9f, 0.42f, panel(stone, 0.8f), 0.05f);
        c.rect(0.05f, 0.54f, 0.9f, 0.42f, panel(stone, 0.8f), 0.05f);
        Col ivy = hex(0x4c8a4c);
        c.capsule(0.2f, 0.0f, 0.3f, 0.5f, 0.03f, paint(mul(ivy, 0.8f)));
        c.capsule(0.3f, 0.5f, 0.18f, 1.0f, 0.03f, paint(mul(ivy, 0.8f)));
        c.ellipse(0.36f, 0.3f, 0.07f, 0.05f, paint(hex(0x8ab850)));
        c.ellipse(0.2f, 0.72f, 0.06f, 0.045f, paint(ivy));
        break;
    }
    case T_FRAG0:
    case T_FRAG1: {
        float s = t == T_FRAG0 ? 1.0f : 0.85f;
        Col gold = hex(0xf6c24a);
        c.glow(0.5f, 0.5f, 0.46f, hex(0xffd88a), 0.6f);
        c.poly({{0.5f, 0.5f - 0.36f * s}, {0.5f + 0.22f * s, 0.5f}, {0.5f, 0.5f + 0.36f * s}, {0.5f - 0.22f * s, 0.5f}}, paint(gold));
        c.poly({{0.5f, 0.5f - 0.24f * s}, {0.5f + 0.08f * s, 0.5f}, {0.5f, 0.5f + 0.1f * s}, {0.5f - 0.1f * s, 0.46f}}, solid(withA(CREAM, 0.9f)));
        if (t == T_FRAG1) {
            c.capsule(0.82f, 0.1f, 0.82f, 0.3f, 0.022f, solid(CREAM));
            c.capsule(0.72f, 0.2f, 0.92f, 0.2f, 0.022f, solid(CREAM));
        }
        break;
    }
    case T_PU_SHIELD: {
        Col red = hex(0xd84a5a), chain = hex(0xf2c25a);
        c.capsule(0.3f, 0.08f, 0.5f, 0.26f, 0.022f, paint(chain));
        c.capsule(0.7f, 0.08f, 0.5f, 0.26f, 0.022f, paint(chain));
        c.circle(0.36f, 0.42f, 0.17f, paint(red));
        c.circle(0.64f, 0.42f, 0.17f, paint(red));
        c.poly({{0.2f, 0.48f}, {0.8f, 0.48f}, {0.5f, 0.88f}}, panel(red));
        c.circle(0.34f, 0.36f, 0.04f, solid(withA(CREAM, 0.9f)));
        break;
    }
    case T_PU_SLOW: {
        Col frameC = hex(0xa8703a), glass = hex(0xd8ecf4, 0.8f), sand = hex(0xf2c25a);
        c.poly({{0.26f, 0.16f}, {0.74f, 0.16f}, {0.54f, 0.5f}, {0.74f, 0.84f}, {0.26f, 0.84f}, {0.46f, 0.5f}}, panel(glass, 0.6f));
        c.poly({{0.36f, 0.62f}, {0.64f, 0.62f}, {0.72f, 0.82f}, {0.28f, 0.82f}}, panel(sand));
        c.poly({{0.32f, 0.2f}, {0.68f, 0.2f}, {0.5f, 0.42f}}, panel(sand));
        c.rect(0.18f, 0.06f, 0.64f, 0.1f, paint(frameC), 0.04f);
        c.rect(0.18f, 0.84f, 0.64f, 0.1f, paint(frameC), 0.04f);
        break;
    }
    case T_PU_MAGNET: {
        Col note = hex(0x8a74b4), glowC = hex(0xf0a0b8);
        c.glow(0.55f, 0.5f, 0.45f, glowC, 0.35f);
        c.ellipse(0.34f, 0.74f, 0.17f, 0.13f, paint(note));
        c.ellipse(0.74f, 0.64f, 0.17f, 0.13f, paint(note));
        c.capsule(0.48f, 0.74f, 0.48f, 0.18f, 0.04f, paint(note));
        c.capsule(0.88f, 0.64f, 0.88f, 0.1f, 0.04f, paint(note));
        c.capsule(0.48f, 0.18f, 0.88f, 0.1f, 0.06f, paint(note));
        c.circle(0.29f, 0.7f, 0.03f, solid(CREAM));
        break;
    }
    case T_PU_PHOTO: {
        c.rect(0.12f, 0.12f, 0.76f, 0.8f, panel(hex(0xf6eee0), 0.5f), 0.03f);
        c.rect(0.2f, 0.2f, 0.6f, 0.5f, [&](const Frag& f) { return mixc(hex(0x7aaed8), hex(0xffd29a), (f.ny + 1) * 0.5f); });
        c.rect(0.2f, 0.54f, 0.6f, 0.16f, solid(hex(0x8ab850)));
        c.circle(0.42f, 0.46f, 0.06f, paint(hex(0xe8843a)));
        c.circle(0.6f, 0.48f, 0.05f, paint(hex(0x3f86c8)));
        break;
    }
    case T_PU_SPEED: {
        Col shoe = hex(0xd84a5a);
        c.poly({{0.14f, 0.4f}, {0.0f, 0.3f}, {0.04f, 0.46f}, {-0.0f, 0.52f}, {0.14f, 0.54f}}, solid(CREAM));
        c.poly({{0.38f, 0.28f}, {0.56f, 0.28f}, {0.6f, 0.52f}, {0.9f, 0.62f}, {0.9f, 0.76f}, {0.14f, 0.76f}, {0.16f, 0.5f}}, panel(shoe));
        c.capsule(0.3f, 0.5f, 0.52f, 0.45f, 0.015f, solid(CREAM));
        c.rect(0.12f, 0.74f, 0.8f, 0.1f, panel(CREAM, 0.5f), 0.04f);
        break;
    }
    default: break;
    }
}

// ------------------------------------------------------------------ backgrounds
struct Px {
    Image img;
    int w, h;
    explicit Px(int W, int H) : w(W), h(H) { img = GenImageColor(W, H, BLANK); }
    void set(int x, int y, Col c)
    {
        if (y < 0 || y >= h || c.a <= 0) return;
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
    // Anti-aliased fill: 3x3 coverage per pixel. Coordinates may run past the
    // edges on x; set() wraps them so layers tile.
    template <class In, class Cf>
    void fill(float x0, float y0, float x1, float y1, In in, Cf cf)
    {
        for (int y = (int)std::floor(y0); y <= (int)std::ceil(y1); ++y)
            for (int x = (int)std::floor(x0); x <= (int)std::ceil(x1); ++x) {
                int cov = 0;
                for (int j = 0; j < 3; ++j)
                    for (int i = 0; i < 3; ++i)
                        if (in(x + (i + 0.5f) / 3, y + (j + 0.5f) / 3)) ++cov;
                if (!cov) continue;
                Col c = cf(x + 0.5f, y + 0.5f);
                c.a *= cov / 9.0f;
                set(x, y, c);
            }
    }
    void ell(float cx, float cy, float rx, float ry, const std::function<Col(float, float)>& cf)
    {
        fill(cx - rx, cy - ry, cx + rx, cy + ry, [&](float x, float y) {
            float nx = (x - cx) / rx, ny = (y - cy) / ry;
            return nx * nx + ny * ny <= 1;
        }, [&](float x, float y) { return cf((x - cx) / rx, (y - cy) / ry); });
    }
    void box(float x0, float y0, float bw, float bh, Col c)
    {
        fill(x0, y0, x0 + bw, y0 + bh, [&](float x, float y) { return x >= x0 && x < x0 + bw && y >= y0 && y < y0 + bh; },
             [&](float, float) { return c; });
    }
};

float ridge(float x, int w, int seed, float a1, float a2, float a3)
{
    float t = x / w * 6.2831853f;
    return a1 * std::sin(t * 2 + seed) + a2 * std::sin(t * 5 + seed * 1.7f) + a3 * std::sin(t * 11 + seed * 2.3f);
}

// Golden-hour sky colour at height t (0 top .. 1 bottom).
Col skyAt(float t)
{
    Col c = mixc(hex(0x2f3a6e), hex(0x7c5e92), smooth(0.0f, 0.42f, t));
    c = mixc(c, hex(0xe58a78), smooth(0.35f, 0.66f, t));
    c = mixc(c, hex(0xffcf8e), smooth(0.6f, 0.82f, t));
    return c;
}
// Atmospheric haze: blend toward the sky near the horizon line.
Col haze(Col c, float amount) { return mixc(c, skyAt(0.68f), amount); }

const float SUN_X = 0.22f, SUN_Y = 0.44f;

void bgLayer(Px& p, int layer)
{
    const int W = p.w, H = p.h;
    if (layer == 0) {
        float sx = W * SUN_X, sy = H * SUN_Y, sr = H * 0.065f;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float t = (float)y / H;
                Col c = skyAt(t);
                float dx = std::fabs(x - sx);
                dx = std::min(dx, W - dx);
                float d = std::sqrt(dx * dx + (y - sy) * (y - sy)) / H;
                c = mixc(c, hex(0xffd89a), clamp01(0.5f - d) * 0.9f * clamp01(0.5f - d) * 2.2f);
                float sun = smooth(sr / H + 0.004f, sr / H - 0.004f, d);
                c = mixc(c, hex(0xfff2cc), sun);
                c.a = 1;
                p.set(x, y, c);
            }
        // soft painted clouds, lit from the sun side (left), cool undersides
        for (int i = 0; i < 7; ++i) {
            float cx = (i + 0.35f) * W / 7.0f + rnd(i, 1, 41) * 60, cy = H * (0.14f + 0.08f * (i % 3) + rnd(i, 2, 41) * 0.05f);
            float scale = 0.7f + rnd(i, 3, 41) * 0.6f;
            for (int k = 0; k < 5; ++k) {
                float rx = (34 - std::fabs(k - 2.0f) * 7) * scale, ry = (16 - std::fabs(k - 2.0f) * 3) * scale;
                float ox = cx + (k - 2) * 22 * scale, oy = cy - (k == 2 ? 6 : (k % 2) * 3) * scale;
                p.ell(ox, oy, rx, ry, [&](float nx, float ny) {
                    Col base = skyAt(oy / H);
                    Col lit = mixc(base, hex(0xffe0b8), 0.55f);
                    Col shadow = mixc(base, hex(0x6a5688), 0.35f);
                    float l = smooth(-0.6f, 0.5f, -0.45f * nx - 0.8f * ny);
                    Col o = mixc(shadow, lit, l);
                    o.a = 0.85f * smooth(1.0f, 0.75f, nx * nx + ny * ny) + 0.1f;
                    return o;
                });
            }
        }
        return;
    }
    if (layer == 1) {   // far city skyline in the haze
        float base = H * 0.66f;
        Col far = mixc(hex(0x9a7aa2), skyAt(0.5f), 0.45f);
        for (int x = 0; x < W; ++x) {
            float top = base + ridge((float)x, W, 3, 6, 3, 1);
            for (int y = (int)top; y < H; ++y) {
                Col c = mixc(far, haze(far, 0.5f), clamp01((y - top) / 40.0f) * 0.4f);
                p.set(x, y, c);
            }
        }
        float x = 0;
        int i = 0;
        while (x < W) {
            float bw = 26 + rnd(i, 1, 11) * 46, bh = 30 + rnd(i, 2, 11) * 70;
            if (i % 7 == 3) bh += 40;   // a few towers
            float top = base - bh;
            Col c = mixc(far, haze(far, 0.4f), rnd(i, 3, 11) * 0.5f);
            p.fill(x, top, x + bw, base + 4, [&](float px_, float py) {
                return px_ >= x && px_ < x + bw && py >= top && py < base + 4;
            }, [&](float, float py) { return mixc(c, haze(c, 0.6f), clamp01((py - top) / bh) * 0.6f); });
            p.box(x, top, 2, bh * 0.7f, withA(hex(0xffd2a0), 0.25f));   // sunlit left edge
            if (i % 7 == 3) p.box(x + bw * 0.5f - 1, top - 22, 2, 22, c);   // spire
            for (int r = 0; r < (int)(bh / 16); ++r)
                for (int k = 0; k < (int)(bw / 10); ++k)
                    if (rnd(i * 31 + r, k, 12) < 0.12f) p.box(x + 4 + k * 10, top + 8 + r * 16, 3, 4, withA(hex(0xffd890), 0.45f));
            x += bw + 2 + rnd(i, 4, 11) * 16;
            ++i;
        }
        return;
    }
    if (layer == 2) {   // mid: apartment blocks with lit windows
        float base = H * 0.76f;
        Col mid = haze(hex(0x5e4a72), 0.25f);
        for (int x = 0; x < W; ++x) {
            float top = base + ridge((float)x, W, 7, 4, 2, 1);
            for (int y = (int)top; y < H; ++y) p.set(x, y, mixc(mid, mul(mid, 0.85f), clamp01((y - top) / 60.0f)));
        }
        for (int i = 0; i < 10; ++i) {
            float x = i * W / 10.0f + rnd(i, 3, 21) * 30, bw = 70 + rnd(i, 4, 21) * 50, bh = 60 + rnd(i, 5, 21) * 70;
            float top = base - bh;
            Col wallC = mixc(mid, hex(0x7a5a78), rnd(i, 6, 21) * 0.6f);
            p.fill(x, top, x + bw, base + 6, [&](float px_, float py) {
                return px_ >= x && px_ < x + bw && py >= top && py < base + 6;
            }, [&](float px_, float py) {
                Col o = mixc(wallC, mixc(wallC, SHADOW, 0.25f), clamp01((px_ - x) / bw) * 0.6f);   // shade away from the sun
                return mixc(o, haze(o, 0.4f), clamp01((py - top) / bh) * 0.3f);
            });
            p.box(x, top, 3, bh, withA(hex(0xffc896), 0.55f));   // warm rim, sun side
            p.box(x - 3, top - 4, bw + 6, 5, mul(wallC, 0.8f));   // roof ledge
            if (i % 3 == 0) {   // water tank / antenna
                p.box(x + bw * 0.6f, top - 18, 14, 14, mul(wallC, 0.85f));
                p.box(x + bw * 0.6f + 6, top - 30, 2, 12, mul(wallC, 0.85f));
            } else if (i % 3 == 1) p.box(x + bw * 0.25f, top - 26, 2, 22, mul(wallC, 0.8f));
            int cols = (int)(bw / 18), rows = (int)((bh - 12) / 20);
            for (int r = 0; r < rows; ++r)
                for (int k = 0; k < cols; ++k) {
                    float wx = x + 8 + k * ((bw - 16) / cols), wy = top + 12 + r * 20;
                    bool lit = rnd(i * 17 + r, k, 22) < 0.45f;
                    Col wc = lit ? hex(0xffcf80) : mixc(wallC, SHADOW, 0.4f);
                    if (lit) p.ell(wx + 4, wy + 5, 9, 9, [&](float nx, float ny) { return withA(hex(0xffc070), 0.12f * clamp01(1 - nx * nx - ny * ny)); });
                    p.box(wx, wy, 8, 10, wc);
                }
        }
        return;
    }
    // layer 3: near street: ground, trees and lamp posts, darkest and warmest
    float base = H * 0.86f;
    Col groundC = hex(0x3c2e46);
    for (int x = 0; x < W; ++x) {
        float top = base + ridge((float)x, W, 11, 3, 2, 1.5f);
        for (int y = (int)top; y < H; ++y) {
            Col c = mixc(mixc(groundC, hex(0x6a4a50), 0.35f), groundC, clamp01((y - top) / 10.0f));
            p.set(x, y, c);
        }
    }
    const int n = 12;
    for (int i = 0; i < n; ++i) {
        float x = (i + 0.5f) * W / n + rnd(i, 9, 31) * 20;
        float by = base + ridge(x, W, 11, 3, 2, 1.5f);
        float sc = 0.85f + rnd(i, 10, 31) * 0.35f;
        // trunk
        p.fill(x - 4 * sc, by - 46 * sc, x + 4 * sc, by + 2, [&](float px_, float py) {
            float half = 4 * sc - (by - py) / (46 * sc) * 1.5f;
            return std::fabs(px_ - x) <= half && py >= by - 46 * sc && py <= by + 2;
        }, [&](float px_, float) { return px_ < x - 1 ? hex(0x6a4a44) : hex(0x3a2a34); });
        // canopy: clustered rounds, warm on the upper left, cool on the lower right
        Col leaf = hex(0x58704a);
        const float ox[5] = {0, -16, 15, -6, 8}, oy[5] = {-62, -50, -52, -76, -72}, rr[5] = {26, 18, 19, 16, 15};
        for (int k = 0; k < 5; ++k)
            p.ell(x + ox[k] * sc, by + oy[k] * sc, rr[k] * sc, rr[k] * 0.9f * sc, [&](float nx, float ny) {
                Col o = litColor(leaf, nx * 0.9f, ny * 0.9f, 1.0f);
                o.a = 1;
                return o;
            });
        if (i % 2 == 0) {   // lamp post with a warm glow
            float lx = x + W / (float)n * 0.5f;
            float ly = base + ridge(lx, W, 11, 3, 2, 1.5f);
            p.ell(lx, ly - 54, 26, 26, [&](float nx, float ny) {
                float d = 1 - std::sqrt(nx * nx + ny * ny);
                return withA(hex(0xffc878), 0.45f * d * d);
            });
            p.box(lx - 1.5f, ly - 54, 3, 56, hex(0x2e2438));
            p.box(lx - 1.5f, ly - 56, 9, 2, hex(0x2e2438));
            p.ell(lx + 7, ly - 52, 4, 3.5f, [&](float, float) { return hex(0xffe6b0); });
        } else if (i % 4 == 1) {   // a bench
            float bx = x + 30;
            float bby = base + ridge(bx, W, 11, 3, 2, 1.5f);
            p.box(bx, bby - 12, 28, 3, hex(0x5a3e40));
            p.box(bx, bby - 19, 28, 3, hex(0x6a4a48));
            p.box(bx + 2, bby - 10, 2, 11, hex(0x2e2438));
            p.box(bx + 24, bby - 10, 2, 11, hex(0x2e2438));
        }
    }
}

// ------------------------------------------------------------------ Forgetting Wall texture
Image wallTexture()
{
    int W = (int)(WALL_TEX_W * E4_TILE), H = (int)(WALL_TEX_H * E4_TILE);
    Image img = GenImageColor(W, H, BLANK);
    Color* d = (Color*)img.data;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float fx = (float)x / W;
            int per = 6;
            float fy = (float)y / H;
            // smooth flowing curtain: warped noise, tiles vertically
            float warp = vnoise(fy * per, fx * 2, per, 5) * 1.5f;
            float n = vnoise(fy * per + warp, fx * 3, per, 3) * 0.65f + vnoise(fy * per * 2 + warp, fx * 5, per * 2, 4) * 0.35f;
            float band = smooth(0.45f, 0.75f, n);
            Col c = mixc(FORGET_DEEP, hex(0x8a74b8), band);
            c = mixc(c, FORGET_LIGHT, smooth(0.72f, 0.9f, n) * 0.6f);
            c = mixc(c, hex(0xffc8a0), smooth(0.7f, 1.0f, fx) * 0.25f);   // golden light catching the leading edge
            c.a = clamp01(1.2f - fx * 0.6f);
            d[(size_t)y * W + x] = toColor(c);
        }
    return img;
}

}  // namespace

int generateEra4Art(const std::string& dir)
{
    int n = 0;
    std::string d = dir + "/art/era4/";
    MakeDirectory(d.c_str());
    auto save = [&](Image img, const std::string& path) {
        ExportImage(img, path.c_str());
        UnloadImage(img);
        ++n;
    };
    for (int who = 0; who < 2; ++who)
        save(makeSheet(PF_COUNT, PLAYER_FW, PLAYER_FH, true, [&](Canvas& c, int f) { drawPlayer(c, who, f); }),
             d + (who == 0 ? "player_p1.png" : "player_p2.png"));
    save(makeSheet(BF_COUNT, BLOB_FS, BLOB_FS, true, drawBlob), d + "enemy_blob.png");
    save(makeSheet(FF_COUNT, FLYER_FS, FLYER_FS, true, drawFlyer), d + "enemy_flyer.png");
    save(makeSheet(RF_COUNT, BRUTE_FS, BRUTE_FS, true, drawBrute), d + "enemy_brute.png");
    save(makeSheet(XF_COUNT, BOSS_FS, BOSS_FS, true, drawBoss), d + "enemy_boss.png");
    {
        Image sheet = GenImageColor(E4_TILE * T_COUNT, E4_TILE, BLANK);
        for (int t = 0; t < T_COUNT; ++t) {
            Canvas cv(1, 1, (float)E4_TILE, E4_SS);
            drawTile(cv, t);
            bool outline = t >= T_FRAG0;   // pickups get the soft outline; solid tiles join seamlessly
            blit(cv, sheet, t * E4_TILE, 0, outline);
        }
        save(sheet, d + "tiles.png");
    }
    for (int l = 0; l < BG_LAYERS; ++l) {
        Px p(E4_BG_W, E4_BG_H);
        bgLayer(p, l);
        save(p.img, d + "bg_" + std::to_string(l) + ".png");
    }
    save(wallTexture(), d + "wall.png");
    return n;
}
