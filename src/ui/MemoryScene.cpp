// MemoryScene.cpp (akshat): the memory cutscenes. See MemoryScene.h.
//
// 2D scenes are laid out in "units": the view is 100 units tall (W ~ 178 wide), the ground of
// most scenes is at y = 84 and a grown-up is 20 units tall, the same share of the screen as the
// player sprites in the game. Each pixel stage draws into a canvas exactly as many pixels tall as
// makes the stage's player sprite 20 units (120, 120, 150, 240 px; the 1-bit canvas is 72 px
// tall, so its 12 px sprites are 10x on a 720p screen), so the sheets draw 1:1;
// the canvas is then quantised by a small shader and scaled up with hard pixels.
#include "ui/MemoryScene.h"

#include "core/Game.h"
#include "render/Art.h"
#include "render/ArtSpec.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace scene {
namespace {

constexpr float PI_F = 3.14159265f;

// ---------------------------------------------------------------- canvas per style
const int CANVAS_H[art::ERA_COUNT] = {72, 120, 120, 150, 240, 1440, 1440};
RenderTexture2D g_rt[art::ERA_COUNT]{};
Shader g_post{};
int g_locMode = -1, g_locSize = -1;
bool g_postTried = false;

const char* POST_FS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform int mode;
uniform vec2 size;
out vec4 finalColor;
const vec3 PAL[16] = vec3[16](
    vec3(0.059,0.051,0.102), vec3(0.149,0.188,0.369), vec3(0.416,0.173,0.431), vec3(0.118,0.541,0.298),
    vec3(0.549,0.290,0.184), vec3(0.353,0.329,0.408), vec3(0.722,0.714,0.784), vec3(0.980,0.953,0.902),
    vec3(0.910,0.212,0.310), vec3(0.949,0.541,0.118), vec3(0.969,0.843,0.227), vec3(0.435,0.863,0.290),
    vec3(0.235,0.612,0.941), vec3(0.541,0.498,0.706), vec3(0.949,0.549,0.706), vec3(0.961,0.788,0.627));
const vec3 SH4[4] = vec3[4](vec3(0.122,0.114,0.141), vec3(0.357,0.337,0.384), vec3(0.659,0.635,0.592), vec3(0.910,0.886,0.824));
float bayer(vec2 p)
{
    int x = int(mod(p.x, 4.0)), y = int(mod(p.y, 4.0));
    int m[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
    return (float(m[y * 4 + x]) + 0.5) / 16.0;
}
void main()
{
    vec3 c = texture(texture0, fragTexCoord).rgb;
    vec2 p = floor(fragTexCoord * size);
    float b = bayer(p);
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    vec3 o = c;
    if (mode == 0) {
        // flat black and white, an ordered dither only for the tones in between
        o = l + (b - 0.5) * 0.32 > 0.5 ? vec3(0.922, 0.922, 0.882) : vec3(0.063, 0.071, 0.086);
    } else if (mode == 1) {
        float v = clamp(l, 0.0, 1.0) * 3.0;
        float f = floor(v);
        float k = clamp((v - f) > 0.5 + (b - 0.5) * 0.45 ? f + 1.0 : f, 0.0, 3.0);
        o = SH4[int(k)];
    } else if (mode == 2) {
        vec3 q = c + (b - 0.5) * 0.06;
        float best = 1e9;
        for (int i = 0; i < 16; ++i) {
            vec3 d = (q - PAL[i]) * vec3(0.9, 1.2, 0.7);
            float e = dot(d, d);
            if (e < best) { best = e; o = PAL[i]; }
        }
    } else if (mode == 3) {
        o = floor(c * 7.0 + 0.5) / 7.0;      // 16-bit: few shades per channel, banded gradients
    } else if (mode == 4) {
        o = floor(c * 15.0 + 0.5) / 15.0;
    }
    finalColor = vec4(o, 1.0) * colDiffuse;
}
)";

// ---------------------------------------------------------------- the frame being drawn
struct Ctx {
    Game* g = nullptr;
    int id = 0, st = 0;
    float t = 0, end = 0;
    const std::vector<float>* ls = nullptr;
    float z = 1, px = 1, W = 177.8f;   // canvas pixels per unit, one pixel in units, view width
    int cw = 1, ch = 1;
    float camX = 88.9f, camY = 50, camZ = 1;
    bool outline = false;
    Color ink{20, 16, 30, 255};
    float L(int i) const
    {
        if (!ls || ls->empty()) return 2.5f + i * 6.0f;
        if (i < 0) return 0;
        if (i < (int)ls->size()) return (*ls)[i];
        return end;
    }
};
Ctx C;

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
float lerp(float a, float b, float k) { return a + (b - a) * k; }
float smooth(float k) { k = clamp01(k); return k * k * (3 - 2 * k); }
float ramp(float a, float b) { return b <= a ? (C.t >= a ? 1.0f : 0.0f) : clamp01((C.t - a) / (b - a)); }
float sm(float a, float b) { return smooth(ramp(a, b)); }
float hf(int i)
{
    unsigned h = (unsigned)i * 2654435761u + 0x9e3779b9u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
    return (h & 0xffffff) / 16777215.0f;
}
Color H(unsigned rgb, float a = 1) { return Color{(unsigned char)(rgb >> 16), (unsigned char)(rgb >> 8), (unsigned char)rgb, (unsigned char)(255 * clamp01(a))}; }
Color mixc(Color a, Color b, float k)
{
    k = clamp01(k);
    return Color{(unsigned char)lerp(a.r, b.r, k), (unsigned char)lerp(a.g, b.g, k), (unsigned char)lerp(a.b, b.b, k), (unsigned char)lerp(a.a, b.a, k)};
}
Color alpha(Color c, float a) { c.a = (unsigned char)(c.a * clamp01(a)); return c; }
Color shade(Color c, float k) { return Color{(unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), c.a}; }
bool pixel() { return C.st <= 4; }
float snap(float v) { return pixel() ? std::round(v * C.z) / C.z : v; }

// ---------------------------------------------------------------- primitives (units)
// In outline mode every shape is drawn one pixel bigger in the ink colour: drawing a figure
// twice (outline, then fill) gives it the dark outline the game's sprites have.
void R(float x, float y, float w, float h, Color c)
{
    if (C.outline) { float g = C.px; DrawRectangleRec({x - g, y - g, w + 2 * g, h + 2 * g}, C.ink); return; }
    DrawRectangleRec({x, y, w, h}, c);
}
void fan(float cx, float cy, float rx, float ry, Color in, Color out, float a0 = 0, float a1 = 2 * PI_F)
{
    int segs = (int)std::clamp(std::max(rx, ry) * C.z * 0.9f, 8.0f, 72.0f);
    rlCheckRenderBatchLimit(segs * 3);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < segs; ++i) {
        float u0 = a0 + (a1 - a0) * i / segs, u1 = a0 + (a1 - a0) * (i + 1) / segs;
        rlColor4ub(in.r, in.g, in.b, in.a);
        rlVertex2f(cx, cy);
        rlColor4ub(out.r, out.g, out.b, out.a);
        rlVertex2f(cx + std::cos(u1) * rx, cy + std::sin(u1) * ry);
        rlVertex2f(cx + std::cos(u0) * rx, cy + std::sin(u0) * ry);
    }
    rlEnd();
}
void E(float cx, float cy, float rx, float ry, Color c)
{
    if (C.outline) { fan(cx, cy, rx + C.px, ry + C.px, C.ink, C.ink); return; }
    fan(cx, cy, rx, ry, c, c);
}
void Cir(float cx, float cy, float r, Color c) { E(cx, cy, r, r, c); }
void Ln(float x1, float y1, float x2, float y2, float th, Color c)
{
    if (pixel()) th = std::max(th, C.px);
    if (C.outline) { th += 2 * C.px; c = C.ink; }
    DrawLineEx({x1, y1}, {x2, y2}, th, c);
    if (!pixel()) { fan(x1, y1, th / 2, th / 2, c, c); fan(x2, y2, th / 2, th / 2, c, c); }
}
void Quad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col)
{
    if (C.outline) {
        Vector2 m{(a.x + b.x + c.x + d.x) / 4, (a.y + b.y + c.y + d.y) / 4};
        auto grow = [&](Vector2 p) {
            Vector2 v{p.x - m.x, p.y - m.y};
            float l = std::sqrt(v.x * v.x + v.y * v.y) + 1e-4f;
            return Vector2{p.x + v.x / l * C.px * 1.4f, p.y + v.y / l * C.px * 1.4f};
        };
        a = grow(a); b = grow(b); c = grow(c); d = grow(d); col = C.ink;
    }
    // back-face culling is off while a canvas is drawn, so either winding shows (once)
    DrawTriangle(a, b, c, col);
    DrawTriangle(a, c, d, col);
}
void Tri(Vector2 a, Vector2 b, Vector2 c, Color col) { DrawTriangle(a, b, c, col); }
void GradV(float x, float y, float w, float h, Color top, Color bot) { DrawRectangleGradientV((int)std::floor(x), (int)std::floor(y), (int)std::ceil(w) + 1, (int)std::ceil(h) + 1, top, bot); }
// smooth vertical gradient that does not snap to whole units (for the high-res canvases)
void GradVf(float x, float y, float w, float h, Color top, Color bot)
{
    rlBegin(RL_QUADS);
    rlColor4ub(top.r, top.g, top.b, top.a); rlVertex2f(x, y);
    rlColor4ub(bot.r, bot.g, bot.b, bot.a); rlVertex2f(x, y + h);
    rlColor4ub(bot.r, bot.g, bot.b, bot.a); rlVertex2f(x + w, y + h);
    rlColor4ub(top.r, top.g, top.b, top.a); rlVertex2f(x + w, y);
    rlEnd();
}
void glow(float x, float y, float r, Color c)
{
    BeginBlendMode(BLEND_ADDITIVE);
    fan(x, y, r, r, c, alpha(c, 0));
    EndBlendMode();
}
// a shape drawn with its outline in the pixel stages
void outlined(const std::function<void()>& f)
{
    if (pixel()) { C.outline = true; f(); C.outline = false; }
    f();
}
// the whole view, in screen space of the canvas (after the camera)
void screenFill(Color c, int blend = -1)
{
    // raylib's multiply blend is dst * src + dst * (1 - src alpha): give it an opaque colour
    // faded toward white instead, or a translucent tint would brighten the scene
    if (blend == BLEND_MULTIPLIED) { Color o = c; o.a = 255; c = mixc(WHITE, o, c.a / 255.0f); c.a = 255; }
    if (blend >= 0) BeginBlendMode(blend);
    DrawRectangle(0, 0, C.cw, C.ch, c);
    if (blend >= 0) EndBlendMode();
}

// ---------------------------------------------------------------- the camera
void beginCam(float x, float y, float zoom = 1)
{
    C.camX = x; C.camY = y; C.camZ = pixel() ? 1 : zoom;
    Camera2D cam{};
    cam.offset = {C.cw / 2.0f, C.ch / 2.0f};
    cam.target = {snap(x), snap(y + 4)};   // a little low: the bars cover the top and bottom
    cam.zoom = C.z * C.camZ;
    BeginMode2D(cam);
}
void endCam() { EndMode2D(); }

// ---------------------------------------------------------------- sprites from the stage's sheets
// who 0 Gran, 1 Sam. Feet at (x, y). face +1 right, -1 left.
void sprite(int who, int frame, float x, float y, int face, Color tint = WHITE)
{
    const Texture2D& tex = C.g->art.era[C.st].player[who];
    if (!tex.id) return;
    Rectangle src = frameRect(tex, frame, art::PLAYER_FW, art::PLAYER_FH, C.st);
    float w = src.width / C.z, h = src.height / C.z;
    if (face < 0) src.width = -src.width;
    DrawTexturePro(tex, src, {snap(x - w / 2), snap(y - h), w, h}, {0, 0}, 0, tint);
}
int walkFrame(float speed = 7) { return art::PF_RUN0 + ((int)(C.t * speed)) % 4; }
int idleFrame() { return ((int)(C.t * 1.6f)) % 2 ? art::PF_IDLE1 : art::PF_IDLE0; }

// ---------------------------------------------------------------- people drawn from primitives
enum HairStyle { HAIR_SHORT, HAIR_BUN, HAIR_BOB, HAIR_PLAITS, HAIR_CAP, HAIR_HAT };
struct Look {
    Color skin{241, 196, 160, 255}, hair{90, 60, 40, 255}, top{200, 80, 60, 255}, bottom{60, 60, 90, 255};
    Color legs{60, 60, 90, 255}, shoes{40, 30, 30, 255};
    int hair_ = HAIR_SHORT;
    bool dress = false;
    Color extra = BLANK;   // cap / hat / bow colour
    Color apron = BLANK;
};
struct Pose {
    float walk = 0, move = 0;   // walk cycle phase (radians) and how much it shows (0 standing)
    int face = 1;
    float armF = 0.12f, armB = -0.1f;   // arm angles from hanging down, + is forward
    float sit = 0, kneel = 0, bob = 0;
    float sitH = 0.27f;                 // seat height when sitting, as a share of height
};
Pose walking(float speed, int face, float amount = 1)
{
    Pose p;
    p.walk = C.t * speed; p.move = amount; p.face = face;
    p.armF = std::sin(p.walk) * 0.5f * amount; p.armB = -std::sin(p.walk) * 0.5f * amount;
    p.bob = -std::fabs(std::sin(p.walk)) * 0.4f * amount;
    return p;
}

struct Joints { Vector2 shoulderF, handF, head; float headR; };
Joints person(float x, float y, float h, const Look& L, const Pose& P)
{
    Joints J{};
    auto body = [&]() {
        const float f = (float)P.face;
        const float r = h * 0.12f;
        const float drop = P.kneel * h * 0.2f + P.sit * (h * 0.47f - h * P.sitH);
        const float hipY = y - h * 0.47f + drop + P.bob * 0.5f;
        const float headY = y - h + r + drop + P.bob;
        const float hx = x + f * h * 0.02f;
        const float shY = headY + r + h * 0.04f;
        const float tw = h * 0.25f;
        const float legLen = h * 0.47f;
        const bool shadeOn = C.st >= 3;
        auto arm = [&](float a, bool front) {
            Vector2 s{x - f * h * 0.02f + (front ? f * h * 0.03f : -f * h * 0.03f), shY + h * 0.04f};
            Vector2 e{s.x + f * std::sin(a) * h * 0.33f, s.y + std::cos(a) * h * 0.33f};
            Color c = front ? L.top : shade(L.top, 0.78f);
            Ln(s.x, s.y, e.x, e.y, h * 0.085f, c);
            Cir(e.x, e.y, h * 0.045f, L.skin);
            if (front) { J.shoulderF = s; J.handF = e; }
        };
        arm(P.armB, false);
        // legs: hip -> knee -> foot, the knee bending forward (simple two-bone IK)
        for (int s = -1; s <= 1; s += 2) {
            Vector2 hip{x + s * h * 0.045f, hipY};
            float ph = P.walk + (s < 0 ? PI_F : 0);
            Vector2 foot{x + std::sin(ph) * h * 0.16f * P.move, y - std::max(0.0f, std::cos(ph)) * h * 0.05f * P.move};
            if (P.sit > 0) foot.x = lerp(foot.x, x + f * h * (0.2f + s * 0.03f), P.sit);
            if (P.kneel > 0) {
                if (s > 0) foot.x = lerp(foot.x, x + f * h * 0.16f, P.kneel);
                else foot.x = lerp(foot.x, x - f * h * 0.14f, P.kneel);
            }
            float dx = foot.x - hip.x, dy = foot.y - hip.y, d = std::sqrt(dx * dx + dy * dy) + 1e-4f;
            float bend = std::sqrt(std::max(0.0f, legLen * legLen / 4 - d * d / 4));
            Vector2 perp{-dy / d, dx / d};
            if (perp.x * f < 0) { perp.x = -perp.x; perp.y = -perp.y; }
            if (s < 0 && P.kneel > 0) { perp.x = -perp.x; perp.y = -perp.y; }   // back knee down
            Vector2 knee{(hip.x + foot.x) / 2 + perp.x * bend, (hip.y + foot.y) / 2 + perp.y * bend};
            Color lc = s < 0 && shadeOn ? shade(L.legs, 0.8f) : L.legs;
            Ln(hip.x, hip.y, knee.x, knee.y, h * 0.1f, lc);
            Ln(knee.x, knee.y, foot.x, foot.y, h * 0.09f, lc);
            R(foot.x - h * 0.05f + f * h * 0.025f, foot.y - h * 0.05f, h * 0.1f, h * 0.05f, L.shoes);
        }
        // skirt / dress, or trousers up to the waist
        if (L.dress) {
            float hem = P.sit > 0 ? hipY + h * 0.12f : y - h * 0.2f + drop * 0.6f;
            Quad({x - tw * 0.5f, hipY - h * 0.08f}, {x + tw * 0.5f, hipY - h * 0.08f},
                 {x + tw * 0.78f + (P.sit > 0 ? f * h * 0.15f : 0), hem}, {x - tw * 0.78f + (P.sit > 0 ? f * h * 0.15f : 0), hem}, L.bottom);
        } else {
            R(x - tw * 0.48f, hipY - h * 0.08f, tw * 0.96f, h * 0.12f, L.bottom);
        }
        // torso
        R(x - tw / 2, shY, tw, hipY - shY + h * 0.02f, L.top);
        if (!C.outline && shadeOn) R(f > 0 ? x - tw / 2 : x + tw / 2 - tw * 0.32f, shY, tw * 0.32f, hipY - shY + h * 0.02f, shade(L.top, 0.8f));
        if (!C.outline && L.apron.a) R(x - tw * 0.3f + f * tw * 0.12f, hipY - h * 0.1f, tw * 0.6f, h * 0.24f, L.apron);
        // head
        const float cx = hx, cy = headY;
        if (L.hair_ == HAIR_PLAITS) {
            Ln(cx - f * r * 0.7f, cy + r * 0.1f, cx - f * r * 0.95f, cy + r * 1.7f, r * 0.42f, L.hair);
            if (!C.outline && L.extra.a) Cir(cx - f * r * 0.95f, cy + r * 1.7f, r * 0.3f, L.extra);
        }
        if (L.hair_ == HAIR_BOB) Cir(cx - f * r * 0.5f, cy + r * 0.35f, r * 0.62f, L.hair);
        Cir(cx - f * r * 0.12f, cy - r * 0.12f, r * 1.04f, L.hair);
        if (L.hair_ == HAIR_BUN) Cir(cx - f * r * 0.8f, cy - r * 0.6f, r * 0.5f, L.hair);
        Cir(cx + f * r * 0.12f, cy + r * 0.1f, r * 0.88f, L.skin);
        if (L.hair_ != HAIR_CAP && L.hair_ != HAIR_HAT) {
            // the fringe over the forehead
            if (!C.outline) E(cx - f * r * 0.05f, cy - r * 0.62f, r * 0.85f, r * 0.42f, L.hair);
        }
        if (L.hair_ == HAIR_CAP) {
            R(cx - r * 0.95f, cy - r * 1.08f, r * 1.9f, r * 0.62f, L.extra);
            R(f > 0 ? cx + r * 0.4f : cx - r * 1.45f, cy - r * 0.58f, r * 1.05f, r * 0.24f, L.extra);
        }
        if (L.hair_ == HAIR_HAT) {
            R(cx - r * 1.35f, cy - r * 0.75f, r * 2.7f, r * 0.26f, L.extra);
            R(cx - r * 0.8f, cy - r * 1.4f, r * 1.6f, r * 0.7f, L.extra);
        }
        if (!C.outline) {
            float ew = std::max(C.px, r * 0.18f), eh = std::max(C.px, r * 0.24f);
            R(cx + f * r * 0.5f - ew / 2, cy - r * 0.08f, ew, eh, H(0x1c1420));
        }
        J.head = {cx, cy}; J.headR = r;
        arm(P.armF, true);
    };
    if (pixel()) { C.outline = true; body(); C.outline = false; }
    body();
    if (!pixel()) {
        // a soft contact shadow under the feet
        fan(x, y + 0.2f, h * 0.22f, h * 0.035f, Color{0, 0, 0, 60}, Color{0, 0, 0, 0});
    }
    return J;
}

// ---------------------------------------------------------------- shared props
void snowfall(int n, float speed, float wind, float size, Color c, int seed)
{
    for (int i = 0; i < n; ++i) {
        float sx = hf(i * 7 + seed) * 220 - 20, sy = hf(i * 13 + seed + 1) * 110;
        float v = speed * (0.7f + 0.6f * hf(i * 3 + seed));
        float y = std::fmod(sy + C.t * v, 110.0f) - 5;
        float x = std::fmod(sx + C.t * wind + std::sin(C.t * 1.3f + i) * 2.0f + 400, 220.0f) - 20 + C.camX - 88.9f;
        float s = std::max(C.px, size);
        R(snap(x), snap(y), s, s, c);
    }
}
void rain(int n, float speed, float slant, Color c, int seed, float amount)
{
    int m = (int)(n * clamp01(amount));
    for (int i = 0; i < m; ++i) {
        float sx = hf(i * 7 + seed) * 240 - 40, sy = hf(i * 13 + seed + 1) * 110;
        float v = speed * (0.8f + 0.4f * hf(i * 3 + seed));
        float y = std::fmod(sy + C.t * v, 110.0f) - 5;
        float x = sx + y * slant + C.camX - 88.9f;
        DrawLineEx({x, y}, {x - slant * 4, y - 4}, std::max(C.px, 0.35f), c);
    }
}
// a run of rolling hills / a horizon silhouette
void hills(float base, float amp, float freq, float phase, Color c, float x0 = -60, float x1 = 260)
{
    float step = std::max(C.px * 2, 1.5f);
    for (float x = x0; x < x1; x += step) {
        float h = amp * (0.6f * std::sin(x * freq + phase) + 0.4f * std::sin(x * freq * 2.3f + phase * 1.7f));
        R(x, base - h, step + C.px, 120, c);
    }
}
void pine(float x, float base, float h, Color c, float sway = 0)
{
    R(x - h * 0.04f, base - h * 0.2f, h * 0.08f, h * 0.2f, shade(c, 0.7f));
    for (int k = 0; k < 4; ++k) {
        float y0 = base - h * 0.15f - k * h * 0.2f, w = h * (0.34f - k * 0.07f);
        float s = sway * (k + 1) * 0.3f;
        Tri({x - w + s * 0.5f, y0}, {x + w + s * 0.5f, y0}, {x + s, y0 - h * 0.34f}, c);
    }
}
void cloud(float x, float y, float s, Color c)
{
    E(x, y, s * 1.2f, s * 0.5f, c);
    E(x - s * 0.6f, y + s * 0.1f, s * 0.7f, s * 0.4f, c);
    E(x + s * 0.4f, y - s * 0.25f, s * 0.7f, s * 0.5f, c);
}
void bird(float x, float y, float s, float flap, Color c)
{
    float k = std::sin(flap) * 0.6f;
    float th = std::max(C.px, s * 0.22f);
    DrawLineEx({x - s, y - s * k}, {x, y}, th, c);
    DrawLineEx({x, y}, {x + s, y - s * k}, th, c);
}
// iris at the start: a circle of the scene opens out of black
void iris(float k, float cx, float cy)
{
    if (k >= 1) return;
    float r = smooth(k) * 120;
    float big = 400;
    BeginBlendMode(BLEND_ALPHA);
    DrawRing({cx, cy}, std::max(0.0f, r), big, 0, 360, 64, BLACK);
    EndBlendMode();
}

// The looks of the people in the memories (natural colours: each stage quantises them)
Look granLook(bool coat = false)
{
    Look l;
    l.skin = H(0xf2c29b); l.hair = H(0xd8d4e0); l.hair_ = HAIR_BUN; l.dress = true;
    l.top = coat ? H(0x6a4a5a) : H(0xf08a24); l.bottom = coat ? H(0x5a3a4a) : H(0x7a4a3a);
    l.legs = H(0x5a4048); l.shoes = H(0x3a2422);
    return l;
}
Look edieLook(int age)
{
    Look l;
    l.skin = H(0xf4cba6); l.hair = H(0x7a4424); l.dress = true; l.legs = H(0xf4cba6); l.shoes = H(0x5a2a1a);
    if (age <= 5) { l.hair_ = HAIR_BOB; l.top = H(0xe8504a); l.bottom = H(0xe8504a); l.extra = H(0xffffff); l.shoes = H(0x8a3a2a); }
    else if (age <= 12) { l.hair_ = HAIR_PLAITS; l.top = H(0xf2e2a0); l.bottom = H(0x5aa0d8); l.extra = H(0xe04a3a); }
    else if (age <= 18) { l.hair_ = HAIR_BOB; l.top = H(0x2f8a5a); l.bottom = H(0x2f8a5a); l.legs = H(0xd0a888); }
    else { l.hair_ = HAIR_BOB; l.top = H(0xd8a040); l.bottom = H(0x4a5a8a); }
    return l;
}
Look arthurLook(bool young)
{
    Look l;
    l.skin = H(0xeec09a); l.hair = young ? H(0x3a2618) : H(0x5a4030);
    l.top = young ? H(0x6a6258) : H(0x9a5a3a); l.bottom = young ? H(0x5a5248) : H(0x3a4a5a);
    l.legs = l.bottom; l.shoes = H(0x2a1a14);
    l.hair_ = HAIR_SHORT;
    return l;
}
Look childLook(int seed)
{
    const unsigned tops[6] = {0x3c9cf0, 0xe8364f, 0x6fdc4a, 0xf7d73a, 0x8a7fb4, 0xf28cb4};
    const unsigned hairs[4] = {0x3a2418, 0x8c4a2f, 0xe0b060, 0x1a1414};
    Look l;
    l.top = H(tops[seed % 6]); l.hair = H(hairs[(seed / 2) % 4]);
    l.bottom = H(seed % 3 == 0 ? 0x26305e : 0x5a5468); l.legs = l.bottom;
    l.dress = seed % 2 == 1; if (l.dress) { l.bottom = l.top; l.legs = H(0xf5c9a0); }
    l.hair_ = l.dress ? HAIR_PLAITS : HAIR_SHORT;
    l.extra = H(0xfaf3e6);
    return l;
}
Look littleSam()
{
    Look l;
    l.skin = H(0xe9b58c); l.hair = H(0x8c4a2f); l.top = H(0x3c9cf0); l.bottom = H(0x26305e); l.legs = l.bottom; l.shoes = H(0x0f0d1a);
    return l;
}

// ================================================================ the scenes
// ---------------------------------------------------------------- prologue: the present day (1-bit)
void sPrologue()
{
    const float G = 84;
    beginCam(88.9f, 50);
    // the room: papered wall, skirting, floorboards
    R(-20, 0, 220, G, H(0xe4e0d4));
    for (int i = 0; i < 40; ++i) for (int j = 0; j < 12; ++j) if ((i + j) % 2 == 0) R(-20 + i * 6.0f, 4 + j * 6.0f, C.px, C.px, H(0x403c38));
    R(-20, G - 4, 220, 4, H(0x2a2620));
    R(-20, G, 220, 30, H(0x6a6058));
    for (int i = 0; i < 14; ++i) R(-20 + i * 17.0f + (i % 2) * 5, G + 4 + (i % 3) * 4, 10, C.px, H(0x201c18));
    // the window, a grey morning outside, a branch moving in the wind
    const float wx = 18, wy = 22, ww = 44, wh = 38;
    R(wx - 2, wy - 2, ww + 4, wh + 4, H(0x2a2420));
    GradV(wx, wy, ww, wh, H(0xe8e8e0), H(0xa0a098));
    float sway = std::sin(C.t * 0.9f) * 2;
    Ln(wx - 2, wy + 10, wx + 22 + sway, wy + 6 + sway * 0.4f, 1.6f, H(0x403a36));
    Ln(wx + 12, wy + 8, wx + 18 + sway, wy + 15, 1.0f, H(0x403a36));
    for (int i = 0; i < 6; ++i) Cir(wx + 10 + i * 3.4f + sway * (0.5f + i * 0.1f), wy + 6 + std::sin(i * 2.0f) * 3, 1.6f, H(0x58524c));
    R(wx + ww / 2 - 1, wy, 2, wh, H(0x2a2420));
    R(wx, wy + wh / 2 - 1, ww, 2, H(0x2a2420));
    R(wx - 4, wy + wh + 1, ww + 8, 3, H(0xf0f0e8));
    // the clock on the wall: the hands move, the pendulum swings
    const float cx = 112, cy = 30;
    R(cx - 7, cy - 8, 14, 34, H(0x3a2a20));
    Cir(cx, cy, 6.2f, H(0xf0ece0));
    float sec = std::floor(C.t);
    float a = sec / 60.0f * 2 * PI_F - PI_F / 2, m = (sec / 60.0f + 17) / 60.0f * 2 * PI_F - PI_F / 2;
    Ln(cx, cy, cx + std::cos(m) * 4.6f, cy + std::sin(m) * 4.6f, 1.0f, H(0x1a1410));
    Ln(cx, cy, cx + std::cos(a * 0 + 1.9f) * 3.2f, cy + std::sin(1.9f) * 3.2f, 1.2f, H(0x1a1410));
    Ln(cx, cy, cx + std::cos(a) * 5.4f, cy + std::sin(a) * 5.4f, 0.6f, H(0x1a1410));
    R(cx - 5, cy + 8, 10, 16, H(0x20160f));
    float pa = std::sin(C.t * PI_F) * 0.35f;
    Ln(cx, cy + 8, cx + std::sin(pa) * 12, cy + 8 + std::cos(pa) * 12, 0.7f, H(0xc8b070));
    Cir(cx + std::sin(pa) * 12, cy + 8 + std::cos(pa) * 12, 2.2f, H(0xe0c880));
    // a shelf of photographs, fading while she forgets
    R(132, 46, 38, 2, H(0x3a2a20));
    float forget = sm(C.L(1), C.L(2) + 2);
    for (int i = 0; i < 3; ++i) {
        float x = 135 + i * 12;
        R(x, 35, 9, 11, H(0x2a2420));
        R(x + 1.5f, 36.5f, 6, 8, H(0xe8e4d8));
        float k = clamp01(forget * 1.6f - i * 0.3f);
        Color img = mixc(H(0x484440), H(0xe8e4d8), k);
        Cir(x + 4.5f, 39, 1.6f, img);
        R(x + 2.5f, 41, 4, 3, img);
    }
    // the armchair
    R(64, 60, 24, 24, H(0x5a3a40));
    R(62, 70, 6, 14, H(0x4a2a30));
    R(84, 70, 6, 14, H(0x4a2a30));
    R(66, 72, 20, 5, H(0x7a5a60));
    // Gran at the window; Sam comes in through the door on the right
    R(150, 44, 22, 40, H(0x2a1c14));
    float open = sm(C.L(3) - 2.5f, C.L(3) - 1.8f);
    R(150, 44, 22 * (1 - open * 0.8f), 40, H(0x6a4a34));
    R(150 + 18 * (1 - open * 0.8f), 64, 2, 2, H(0xe0c060));
    float granFace = C.t > C.L(3) + 0.3f ? 1 : -1;
    sprite(0, idleFrame(), 54, G, (int)granFace);
    float in0 = C.L(3) - 1.8f, in1 = C.L(3) + 0.4f;
    if (C.t > in0) {
        float k = ramp(in0, in1);
        float x = lerp(162, 72, k);
        sprite(1, k < 1 ? walkFrame(6) : idleFrame(), x, G, -1);
    }
    // her breath of a sigh at the window: the glass mists
    endCam();
    // the fog of forgetting, lifting on the last line
    float fog = 0.3f * (1 - sm(C.L(4), C.L(4) + 3.0f));
    screenFill(alpha(H(0x9a9a9a), fog));
}

// ---------------------------------------------------------------- 0: the night she got lost (1-bit)
void sNight()
{
    const float G = 84;
    // Sam brings her home: the two of them walk off left at the end
    float home0 = C.L(3) + 2.2f;
    float home = std::max(0.0f, C.t - home0);
    float pairX = 104 - home * 5.0f;
    float camX = 88.9f - std::max(0.0f, 70 - pairX) * 0.6f;
    float camY = lerp(38, 50, sm(0.0f, 4.5f));
    beginCam(camX, camY);
    GradV(-80, -20, 360, G + 20, H(0x05060c), H(0x1c2236));
    // stars and a thin moon
    for (int i = 0; i < 26; ++i) {
        float tw = hf(i * 5 + (int)(C.t * 2) * 31) > 0.86f ? 0.3f : 1;
        R(hf(i) * 260 - 50, hf(i + 99) * 48 - 8, C.px, C.px, alpha(H(0xffffff), tw));
    }
    Cir(32, 20, 3.4f, H(0xf0f0e0));
    Cir(33.6f, 19.2f, 2.6f, H(0x05060c));
    // the houses across the road, asleep, roofs white with snow; one window on
    for (int i = 0; i < 7; ++i) {
        float x = -40 + i * 38, w = 30, h = 20 + (i % 3) * 4, base = G - 12;
        R(x, base - h, w, h, H(0x1a1c26));
        Tri({x - 2, base - h}, {x + w + 2, base - h}, {x + w / 2, base - h - 10}, H(0x1a1c26));
        Ln(x - 2, base - h, x + w / 2, base - h - 10, 1.4f, H(0xe8e8f0));
        Ln(x + w / 2, base - h - 10, x + w + 2, base - h, 1.4f, H(0xe8e8f0));
        bool lit = i == 2;
        if (lit) R(x + 6, base - h + 6, 5, 5, H(0xe8d890));
        else { R(x + 5.6f, base - h + 5.6f, 5.8f, 5.8f, H(0x9a9aa0)); R(x + 7, base - h + 7, 3, 3, H(0x10121a)); }
        R(x + w - 11.4f, base - h + 5.6f, 5.8f, 5.8f, H(0x9a9aa0)); R(x + w - 10, base - h + 7, 3, 3, H(0x10121a));
    }
    // the pavement and kerb, deep in snow
    R(-80, G - 12, 360, 12, H(0x14161e));
    R(-80, G, 360, 30, H(0xdadae4));
    R(-80, G + 6, 360, 1.5f, H(0x8a8a98));
    // footprints: where Sam came running
    float samIn0 = lerp(C.L(0), C.L(1), 0.5f), samIn1 = C.L(1) - 0.2f;
    float samX = C.t < samIn0 ? -20 : lerp(-20, 90, sm(samIn0, samIn1));
    samX = lerp(samX, 96, sm(C.L(2), C.L(2) + 1.2f));
    for (float x = -16; x < samX - 4; x += 4.5f) R(x, G + 1.5f + ((int)(x / 4.5f) % 2) * 1.5f, 1.4f, C.px, H(0x30303a));
    // the bus stop: a pole, a round sign and a timetable
    const float bx = 116;
    R(bx - 0.6f, 58, 1.4f, G - 58, H(0xa0a0a8));
    outlined([&] { Cir(bx, 58, 4.4f, H(0xe8e8e8)); });
    R(bx - 3, 57.4f, 6, 1.4f, H(0x101216));
    R(bx - 1.6f, 66, 3.2f, 6, H(0xd0d0d0));
    // the street lamp, flickering
    const float lx = 146;
    float fl = 1;
    int tick = (int)(C.t * 14);
    if (hf(tick * 3 + 5) < 0.07f || (C.t > 1.0f && C.t < 1.25f)) fl = 0.25f;
    R(lx - 0.8f, 30, 1.8f, G - 30, H(0x707078));
    Ln(lx, 30, lx - 7, 30, 1.2f, H(0x707078));
    R(lx - 10, 29, 6, 2.4f, H(0xa0a0a8));
    Tri({lx - 9, 31.4f}, {lx - 18, G}, {lx + 2, G}, alpha(H(0xfff0c0), 0.45f * fl));
    E(lx - 8, G + 1.5f, 16, 3, alpha(H(0xffffff), 0.5f * fl));
    glow(lx - 7, 31.5f, 7, alpha(H(0xfff4c8), 0.9f * fl));
    // Gran waiting, in her coat and slippers; Sam's coat round her shoulders later
    int granFace = C.t > C.L(1) - 0.6f ? -1 : (std::fmod(C.t, 6.0f) < 3 ? -1 : 1);
    float gx = home > 0 ? pairX : 106;
    sprite(0, home > 0 ? walkFrame(4) : idleFrame(), gx, G, home > 0 ? -1 : granFace);
    float coatK = sm(C.L(2) + 1.6f, C.L(2) + 2.6f);
    if (coatK > 0) {
        float cy = lerp(G - 22, G - 11.5f, coatK);
        // Sam's coat, round her shoulders: dark, edged in light so it reads on the night
        R(gx - 4.1f, cy - 0.7f, 8.2f, 8.4f, H(0xe8e8f0));
        R(gx - 3.4f, cy, 6.8f, 7, H(0x30364a));
    }
    // her breath in the cold
    for (int i = 0; i < 3; ++i) {
        float ph = std::fmod(C.t * 0.5f + i * 0.33f, 1.0f);
        Cir(gx - 4 - ph * 4, G - 16 - ph * 6, 0.8f + ph * 1.4f, alpha(H(0xffffff), 0.9f * (1 - ph)));
    }
    if (C.t > samIn0) {
        bool moving = (C.t < samIn1) || (C.t > C.L(2) && C.t < C.L(2) + 1.2f) || home > 0;
        float sx = home > 0 ? pairX + 8 : samX;
        sprite(1, moving ? walkFrame(home > 0 ? 4 : 9) : idleFrame(), sx, G, home > 0 ? -1 : 1);
    }
    // snow: far flakes small and slow, near ones bigger
    snowfall(70, 9, -3, C.px, H(0xffffff), 3);
    snowfall(16, 16, -5, 2 * C.px, H(0xffffff), 77);
    endCam();
}

// ---------------------------------------------------------------- 1: Arthur's shirts (4 shades)
void sShirts()
{
    const float G = 84;
    beginCam(lerp(80, 97, sm(0, C.end)), 50);
    GradV(-40, -20, 280, 90, H(0xdcdcd4), H(0xf4f4ee));
    // clouds drifting
    for (int i = 0; i < 5; ++i) {
        float x = std::fmod(hf(i) * 260 + C.t * (3 + i), 300.0f) - 60;
        cloud(x, 16 + hf(i + 9) * 18, 6 + hf(i + 3) * 6, H(0xffffff));
    }
    // the back of the house, the garden fence, a tree
    R(-40, 30, 70, G - 30, H(0x8a8478));
    for (int r = 0; r < 12; ++r) R(-40, 30 + r * 4.5f, 70, 0.6f, H(0x6a6458));
    R(4, 52, 12, G - 52, H(0x3a3634));
    R(-20, 40, 14, 12, H(0x3a3634)); R(-18.5f, 41.5f, 11, 9, H(0xb8b8b0));
    for (int i = 0; i < 26; ++i) {
        float x = 30 + i * 7.2f;
        R(x, 56, 6.4f, G - 56, H(0xa09a8c));
        Tri({x, 56}, {x + 6.4f, 56}, {x + 3.2f, 53}, H(0xa09a8c));
    }
    R(30, 62, 190, 1.4f, H(0x7a7468));
    R(30, 76, 190, 1.4f, H(0x7a7468));
    float sway = std::sin(C.t * 1.4f) * 1.5f;
    R(184, 40, 4, G - 40, H(0x4a443e));
    for (int i = 0; i < 9; ++i) Cir(186 + std::cos(i * 1.7f) * 10 + sway * (i % 3) * 0.4f, 34 + std::sin(i * 2.3f) * 8, 7, H(0x5a5650));
    // the lawn
    R(-40, G, 280, 30, H(0x7a7a70));
    for (int i = 0; i < 60; ++i) {
        float x = -30 + i * 4.1f, k = std::sin(C.t * 3 + x * 0.2f) * 0.8f;
        Ln(x, G + 1, x + k, G - 1.5f, 0.6f, H(0x5a5a50));
    }
    // the washing line between two posts, sagging
    const float p0 = 44, p1 = 168, top = 40;
    R(p0 - 1, top - 2, 2, G - top + 2, H(0x3a3634));
    R(p1 - 1, top - 2, 2, G - top + 2, H(0x3a3634));
    auto lineY = [&](float x) { float u = (x - p0) / (p1 - p0); return top + 6 * 4 * u * (1 - u); };
    for (float x = p0; x < p1; x += 2) Ln(x, lineY(x), x + 2, lineY(x + 2), 0.5f, H(0x2a2628));
    // the shirts, one of them blue; the wind lifts their tails
    float wind = 0.6f + 0.4f * std::sin(C.t * 0.7f);
    auto shirt = [&](float x, float y, Color c, int i, float lift, float sc = 1) {
        float fl = std::sin(C.t * 3.1f + i * 1.3f) * 2.2f * wind + 2.4f * wind + lift;
        float w = 11 * sc, h = 13 * sc;
        outlined([&] {
            Quad({x - w / 2, y}, {x + w / 2, y}, {x + w / 2 + fl, y + h}, {x - w / 2 + fl, y + h - 0.6f}, c);
            Quad({x - w / 2, y}, {x - w / 2 - 4, y + 1}, {x - w / 2 - 4 + fl * 0.6f, y + 6}, {x - w / 2 + fl * 0.3f, y + 5}, c);
            Quad({x + w / 2, y}, {x + w / 2 + 4, y + 1}, {x + w / 2 + 4 + fl * 0.6f, y + 6}, {x + w / 2 + fl * 0.3f, y + 5}, c);
        });
        R(x - 1.5f, y - 0.5f, 3, 1.5f, shade(c, 0.6f));
        R(x - w / 2 + 0.5f, y - 1.6f, 1, 2.4f, H(0x2a2628));
        R(x + w / 2 - 1.5f, y - 1.6f, 1, 2.4f, H(0x2a2628));
    };
    const float sx[5] = {62, 82, 102, 124, 146};
    const unsigned scol[5] = {0xf0f0f0, 0xe0dcd0, 0x3c6ab8, 0xf0f0f0, 0xc8c4b8};
    // the shirt pegged during the first line, Gran's hands at the line
    float pegged = sm(C.L(0) + 0.5f, C.L(0) + 2.5f);
    float give = sm(C.L(2) + 0.4f, C.L(2) + 2.0f);   // she unpegs the blue one and gives it to Sam
    for (int i = 0; i < 5; ++i) {
        if (i == 4 && pegged < 1) continue;
        if (i == 2 && give > 0) continue;
        shirt(sx[i], lineY(sx[i]), H(scol[i]), i, 0);
    }
    // Gran at the line, Sam beside her with the peg bag
    const float gx = 132, samX0 = 116;
    bool reaching = (C.t < C.L(0) + 2.5f && C.t > C.L(0)) || (C.t > C.L(2) && C.t < C.L(2) + 1.4f);
    int gf = reaching ? art::PF_ATTACK : idleFrame();
    float gxx = lerp(gx, 120, sm(C.L(2), C.L(2) + 1.0f));
    sprite(0, gf, gxx, G, (C.t > C.L(2) + 1.0f) ? -1 : 1);
    if (pegged < 1 && C.t > C.L(0)) {
        // the last shirt, in her arms, going up onto the line
        float k = pegged;
        shirt(lerp(gx + 4, sx[4], k), lerp(G - 14, lineY(sx[4]), k), H(scol[4]), 4, 0);
    }
    float samX = lerp(samX0 - 30, samX0 - 16, sm(C.L(1) - 1.5f, C.L(1)));
    int samF = C.t > C.L(1) - 1.5f && C.t < C.L(1) ? walkFrame(6) : idleFrame();
    sprite(1, samF, samX, G, 1);
    R(samX + 2, G - 9, 4, 3.5f, H(0x7a6a5a));   // the peg bag
    if (give > 0) {
        // the blue shirt travels from the line to Sam's arms
        float x = lerp(sx[2], samX + 5, give), y = lerp(lineY(sx[2]), G - 14, give);
        shirt(x, y, H(scol[2]), 2, -2 * give, lerp(1, 0.75f, give));
    }
    // a few leaves on the wind
    for (int i = 0; i < 8; ++i) {
        float x = std::fmod(hf(i * 3) * 300 + C.t * (18 + i * 2), 300.0f) - 50;
        float y = 30 + hf(i * 5) * 40 + std::sin(C.t * 2 + i) * 4;
        R(snap(x), snap(y), 1.2f, 0.8f, H(0x4a4640));
    }
    endCam();
}

// ---------------------------------------------------------------- 2: Sam's first day at school (8-bit)
void sSchool()
{
    const float G = 84;
    beginCam(88.9f, 50);
    // the clock goes round to three o'clock, and the sky with it
    float a = C.L(1), b = C.L(2);
    float spin = sm(lerp(a, b, 0.62f), b - 0.2f);
    float afternoon = spin;
    GradV(-40, -20, 280, 100, mixc(H(0x3c9cf0), H(0xf28a1e), afternoon * 0.35f), mixc(H(0xb8d8f0), H(0xf5c9a0), afternoon));
    for (int i = 0; i < 3; ++i) cloud(std::fmod(30 + i * 70 + C.t * 2, 260.0f) - 40, 18 + i * 5, 7, H(0xfaf3e6));
    // the school: brick, windows, a bell tower with the clock
    const float sx = 110, sw = 90;
    R(sx, 36, sw, G - 36, H(0x8c4a2f));
    for (int r = 0; r < 12; ++r) for (int c = 0; c < 18; ++c) if ((r + c) % 3 == 0) R(sx + c * 5 + (r % 2) * 2.5f, 37 + r * 4, 3, 1, H(0x6a2c2a));
    R(sx - 3, 34, sw + 6, 3, H(0x5a5468));
    for (int i = 0; i < 4; ++i) { R(sx + 8 + i * 20, 44, 10, 10, H(0xfaf3e6)); R(sx + 9 + i * 20, 45, 8, 8, H(0x26305e)); R(sx + 12.5f + i * 20, 45, 1, 8, H(0xfaf3e6)); }
    R(sx + 36, 62, 14, 22, H(0x26305e));
    R(sx + 37, 63, 12, 21, H(0x5a3a2a));
    R(sx + 32, 24, 22, 12, H(0x8c4a2f));
    Tri({sx + 30, 24}, {sx + 56, 24}, {sx + 43, 12}, H(0x5a5468));
    const float ccx = sx + 43, ccy = 29.5f;
    Cir(ccx, ccy, 4.6f, H(0xfaf3e6));
    // 8:50 in the morning to 3:00 in the afternoon
    float minutes = lerp(8 * 60 + 50, 15 * 60, spin);
    float ma = minutes / 60.0f * 2 * PI_F - PI_F / 2, ha = minutes / 720.0f * 2 * PI_F - PI_F / 2;
    Ln(ccx, ccy, ccx + std::cos(ma) * 3.8f, ccy + std::sin(ma) * 3.8f, 0.8f, H(0x0f0d1a));
    Ln(ccx, ccy, ccx + std::cos(ha) * 2.6f, ccy + std::sin(ha) * 2.6f, 1.0f, H(0x0f0d1a));
    // a tree dropping autumn leaves
    R(18, 46, 4, G - 46, H(0x5a3a2a));
    for (int i = 0; i < 7; ++i) Cir(20 + std::cos(i * 2.1f) * 9, 40 + std::sin(i * 1.7f) * 7, 7, i % 2 ? H(0xf28a1e) : H(0xe8364f));
    for (int i = 0; i < 6; ++i) {
        float ph = std::fmod(C.t * 0.25f + hf(i), 1.0f);
        R(12 + hf(i + 4) * 20 + std::sin(C.t * 2 + i) * 3, 42 + ph * 42, 1.2f, 1, H(0xf28a1e));
    }
    // the pavement and the path in
    R(-40, G, 280, 30, H(0x5a5468));
    R(-40, G, 280, 1.2f, H(0xb8b6c8));
    // the railings and the open gate
    const float gateX = 96;
    for (float x = -40; x < 240; x += 3.2f) {
        if (x > gateX - 1 && x < gateX + 14) continue;
        R(x, 66, 1, G - 66, H(0x0f0d1a));
        R(x - 0.2f, 65, 1.4f, 1.4f, H(0x0f0d1a));
    }
    R(-40, 68, gateX + 39, 1, H(0x0f0d1a));
    R(gateX + 14, 68, 200, 1, H(0x0f0d1a));
    R(gateX - 1.5f, 60, 2, G - 60, H(0x26305e));
    R(gateX + 13.5f, 60, 2, G - 60, H(0x26305e));
    R(gateX - 4, 58, 22, 3, H(0x26305e));
    // other children going in, early in the scene
    for (int i = 0; i < 4; ++i) {
        float s0 = 0.4f + i * 1.3f, s1 = s0 + 5.5f;
        if (C.t < s0 || C.t > s1 + 0.4f) continue;
        float k = ramp(s0, s1);
        float x = k < 0.5f ? lerp(-10 - i * 6, gateX + 7, k * 2) : lerp(gateX + 7, sx + 43, (k - 0.5f) * 2);
        Look l = childLook(i + 1);
        person(x, G, 12.5f, l, walking(9, 1));
        R(x - 3.8f, G - 10.5f, 2.6f, 4, H(i % 2 ? 0xe8364f : 0xf7d73a));
    }
    // Gran and little Sam: walk to the gate, she kneels, he lets go, walks in, waves
    float arrive = sm(0.3f, a - 0.3f);
    float gx = lerp(-12, gateX - 8, arrive);
    bool gWalk = C.t > 0.3f && C.t < a - 0.3f;
    float kneel = sm(a + 0.2f, a + 0.9f) * (1 - sm(lerp(a, b, 0.62f), lerp(a, b, 0.7f)));
    float letGo = lerp(a, b, 0.32f);
    float samIn = sm(letGo, lerp(a, b, 0.6f));
    float samX = C.t < letGo ? gx + 7 : lerp(gx + 7, sx + 43, samIn);
    bool samWalk = (gWalk) || (C.t > letGo && samIn < 1);
    float wave = C.t > letGo + 0.4f * (lerp(a, b, 0.6f) - letGo) && samIn < 0.98f ? 1 : 0;
    // after three o'clock: he comes running out to her
    float out = sm(b + 0.2f, b + 1.8f);
    if (C.t > b) samX = lerp(sx + 43, gx + 7, out);
    bool samVisible = samIn < 0.98f || C.t > b;
    sprite(0, gWalk ? walkFrame(6) : (kneel > 0.5f ? art::PF_POUND : idleFrame()), gx, G, 1);
    if (samVisible) {
        Pose p = samWalk || (C.t > b && out < 1) ? walking(C.t > b ? 14 : 9, C.t > b ? -1 : 1) : Pose{};
        if (C.t > b && out >= 1) p.face = -1;
        if (!samWalk && C.t < letGo) { p.armF = 1.1f; p.face = -1; }   // holding her hand, not letting go
        if (wave > 0) { p.face = -1; p.armF = 2.6f + std::sin(C.t * 12) * 0.4f; p.move = 0; }
        Look l = littleSam();
        person(samX, G, 12, l, p);
        R(samX - (p.face > 0 ? 4.2f : -1.6f), G - 10, 2.6f, 4.2f, H(0xe8364f));   // the new school bag
    }
    endCam();
}

// ---------------------------------------------------------------- 3: the camping trip (16-bit)
void sCamping()
{
    const float G = 84;
    float morning = sm(C.L(2) - 1.5f, C.L(2) + 2.5f);
    float storm = 1 - morning;
    float camX = lerp(88.9f, 96, sm(C.L(1), C.L(1) + 3));
    beginCam(camX, 50);
    Color skyT = mixc(H(0x0a0e22), H(0x6a8ad0), morning), skyB = mixc(H(0x1e2648), H(0xf8c890), morning);
    GradV(-40, -20, 280, 110, skyT, skyB);
    // lightning: a flash of the whole sky, the pines black against it
    float flash = 0;
    const float strikes[5] = {0.9f, 3.4f, 6.8f, 9.6f, 13.5f};
    for (float s : strikes) {
        float st = C.L(0) + s;
        if (st > C.L(2) - 1.5f) continue;
        float d = C.t - st;
        if (d > 0 && d < 0.5f) flash = std::max(flash, (d < 0.08f || (d > 0.16f && d < 0.24f)) ? 1.0f : 0.25f * (1 - d * 2));
    }
    if (flash > 0.5f) {
        GradV(-40, -20, 280, 110, H(0xc8d0f0), H(0x8a90c0));
        float bx = 40 + std::fmod(C.L(0) * 7, 80.0f);
        Ln(bx, 10, bx + 4, 24, 0.8f, H(0xffffff)); Ln(bx + 4, 24, bx - 2, 36, 0.8f, H(0xffffff)); Ln(bx - 2, 36, bx + 3, 52, 0.8f, H(0xffffff));
    }
    if (morning > 0) glow(150, 74, 40 * morning, alpha(H(0xffd890), 0.8f * morning));
    // pines in two rows; they bend in the wind of the storm
    float wind = std::sin(C.t * 1.7f) * 2.5f * storm;
    Color farC = flash > 0.5f ? H(0x10142a) : mixc(H(0x141a30), H(0x5a6a7a), morning);
    Color nearC = flash > 0.5f ? H(0x060810) : mixc(H(0x0c1220), H(0x2a4a3a), morning);
    for (int i = 0; i < 12; ++i) pine(-30 + i * 22 + hf(i) * 8, G - 6, 36 + hf(i + 5) * 14, farC, wind * 0.6f);
    R(-40, G - 6, 280, 10, farC);
    for (int i = 0; i < 6; ++i) { float x = -20 + i * 46 + hf(i + 20) * 10; if (x > 60 && x < 130) continue; pine(x, G + 2, 58 + hf(i + 30) * 10, nearC, wind); }
    // the ground: pine needles, puddles
    R(-40, G, 280, 30, mixc(H(0x1a1a20), H(0x4a5a30), morning));
    E(70, G + 5, 10, 1.5f, mixc(H(0x2a3050), H(0xf0c8a0), morning));
    E(140, G + 8, 14, 1.8f, mixc(H(0x2a3050), H(0xf0c8a0), morning));
    // the tent: shaking in the storm, glowing from the lantern inside; Arthur's shadow on the canvas
    const float tx = 100, tw = 40, th = 30;
    float shake = std::sin(C.t * 23) * 0.7f * storm * (0.5f + 0.5f * std::sin(C.t * 3));
    Vector2 apex{tx + shake, G - th};
    Color canvas = mixc(H(0xa04a2a), H(0xe0703a), morning);
    outlined([&] { Tri({tx - tw / 2, G}, {tx + tw / 2, G}, apex, canvas); });
    Tri({tx - tw / 2, G}, {tx, G}, apex, shade(canvas, 0.82f));
    float lamp = storm * (0.75f + 0.25f * std::sin(C.t * 9) * std::sin(C.t * 5.3f));
    if (lamp > 0.05f) {
        Tri({tx - tw / 2 + 2, G - 1}, {tx + tw / 2 - 2, G - 1}, {apex.x, apex.y + 3}, alpha(H(0xf8d070), 0.75f * lamp));
        // Arthur, standing in the middle, both hands on the pole; Edie and Rose asleep
        Color sh = alpha(H(0x2a1008), 0.95f * lamp);
        R(apex.x - 0.7f, apex.y + 2, 1.4f, th - 3, sh);
        Cir(tx - 4, G - 19, 2.8f, sh);
        R(tx - 6.2f, G - 16.4f, 4.6f, 9, sh);
        R(tx - 5.8f, G - 8, 1.6f, 8, sh); R(tx - 3.4f, G - 8, 1.6f, 8, sh);
        Ln(tx - 3, G - 15, apex.x, G - 20 + shake * 0.5f, 1.3f, sh);
        Ln(tx - 3, G - 13, apex.x, G - 16 + shake * 0.5f, 1.3f, sh);
        E(tx + 9, G - 2, 6, 2.2f, sh);
        E(tx - 13, G - 1.8f, 3.6f, 1.8f, sh);
    }
    Ln(tx - tw / 2 - 4, G, apex.x, apex.y, 0.4f, H(0xb0a080));
    Ln(tx + tw / 2 + 4, G, apex.x, apex.y, 0.4f, H(0xb0a080));
    // the morning: the flap opens, Arthur steps out and stretches, Rose runs round him
    if (morning > 0.4f) {
        float k = sm(C.L(2) + 0.3f, C.L(2) + 2.2f);
        if (k > 0) {
            Pose p = k < 1 ? walking(7, 1) : Pose{};
            if (k >= 1) { p.armF = 2.9f + std::sin(C.t * 2) * 0.1f; p.armB = -2.9f; }
            person(lerp(tx + 2, tx + 22, k), G, 20, arthurLook(false), p);
        }
        float r = sm(C.L(2) + 1.2f, C.L(2) + 3.5f);
        if (r > 0) {
            float x = tx + 6 + std::sin(r * 5) * 14 * r;
            Look l = edieLook(6); l.top = l.bottom = H(0xe04a6a); l.hair = H(0x8a4a24);
            person(x, G, 11, l, walking(14, std::cos(r * 5) > 0 ? 1 : -1));
        }
        // birds out of the trees
        for (int i = 0; i < 4; ++i) bird(fmodf(C.t * 12 + i * 30, 260) - 40, 26 + i * 4 + std::sin(C.t * 2 + i) * 2, 1.6f, C.t * 10 + i, H(0x2a2140));
    }
    // the rain
    rain(150, 90, 0.35f, alpha(H(0x9ab0e0), 0.7f), 5, storm * 1.2f - 0.1f);
    endCam();
    if (flash > 0 && flash <= 0.5f) screenFill(alpha(H(0xc0c8f0), flash), BLEND_ADDITIVE);
}

// ---------------------------------------------------------------- 4: the last train (32-bit)
void sTrain()
{
    const float G = 86;
    float sway = std::sin(C.t * 5.0f) * 0.25f + (std::fmod(C.t, 1.1f) < 0.08f ? 0.4f : 0);
    float camX = lerp(80, 100, sm(0, C.L(1)));
    beginCam(camX, 50 + sway * 0);
    // outside the windows: sky, far hills, fields, telegraph poles rushing past
    float evening = sm(C.L(1), C.end);
    GradV(-40, 0, 280, 60, mixc(H(0x6aa0e0), H(0xe08a6a), evening), mixc(H(0xd8e8f0), H(0xf8d090), evening));
    float run = C.t * 30;
    for (int i = -1; i < 6; ++i) {
        float x = std::fmod(i * 60 - run * 0.15f, 360.0f) + (i * 60 - run * 0.15f < 0 ? 0 : 0);
        (void)x;
    }
    for (float x = -40; x < 240; x += 2) {
        float u = x + run * 0.12f;
        float hh = 5 * std::sin(u * 0.05f) + 3 * std::sin(u * 0.13f);
        R(x, 36 - hh, 2.1f, 30, mixc(H(0x6a8aa0), H(0x9a7a8a), evening));
    }
    for (float x = -40; x < 240; x += 2) {
        float u = x + run * 0.5f;
        float hh = 3 * std::sin(u * 0.08f) + 2 * std::sin(u * 0.21f);
        R(x, 42 - hh, 2.1f, 30, mixc(H(0x5a9a4a), H(0x7a7a3a), evening));
    }
    for (int i = 0; i < 6; ++i) {
        float x = std::fmod(i * 50 - run * 2.4f, 300.0f);
        if (x < 0) x += 300;
        x -= 60;
        R(x, 22, 1.2f, 30, H(0x3a2a20));
        R(x - 3, 24, 7, 0.8f, H(0x3a2a20));
    }
    // the carriage: wall panels around the windows, luggage racks, lamps
    const Color wall = H(0x6a2a2a), wallD = H(0x4a1a1e), wood = H(0x8a5a30);
    R(-40, 0, 280, 18, wall);
    R(-40, 52, 280, 40, wall);
    for (int i = 0; i < 7; ++i) {
        float x = -30 + i * 38;
        R(x, 18, 8, 34, wall);
        R(x + 1, 18, 1, 34, wallD);
        R(x + 6, 18, 1, 34, H(0x8a3a3a));
    }
    R(-40, 17, 280, 1.5f, wood);
    R(-40, 51, 280, 2, wood);
    R(-40, 10, 280, 1.2f, H(0xc8a060));   // the brass luggage rack
    for (int i = 0; i < 12; ++i) R(-36 + i * 22, 10, 0.8f, 4, H(0xc8a060));
    const float bags[6] = {-10, 24, 58, 120, 150, 190};
    for (int i = 0; i < 6; ++i) outlined([&] { R(bags[i], 3, 12 + (i % 2) * 4, 7, i % 2 ? H(0x5a3a2a) : H(0x3a4a5a)); });
    // the seats: high backs, passengers in hats and headscarves
    const float seatX[6] = {-6, 30, 66, 102, 138, 174};
    for (int i = 0; i < 6; ++i) {
        float x = seatX[i];
        bool arthurSeat = i == 3;
        // a plush bench: high back behind, cushion, an iron leg
        outlined([&] {
            R(x - 13, G - 34, 5, 22, H(0x2f5a48));
            R(x - 12, G - 14.5f, 20, 4.5f, H(0x2f5a48));
        });
        R(x - 12.2f, G - 33.6f, 1.2f, 21, H(0x4a8068));
        R(x - 11, G - 14.2f, 18, 1, H(0x4a8068));
        R(x - 4, G - 10, 1.4f, 10, H(0x2a2420));
        if (arthurSeat) continue;
        Look l = arthurLook(true);
        const unsigned coats[5] = {0x3a4a6a, 0x6a5a3a, 0x5a3a4a, 0x3a5a4a, 0x7a6a5a};
        l.top = H(coats[i % 5]); l.bottom = shade(l.top, 0.8f); l.legs = l.bottom;
        l.hair_ = i % 2 ? HAIR_HAT : HAIR_BUN;
        l.extra = H(i % 2 ? 0x2a2a30 : 0x8a3a3a);
        l.hair = H(i % 2 ? 0x2a1a10 : 0x9a8a80);
        Pose p; p.sit = 1; p.face = 1; p.armF = 0.9f; p.bob = std::sin(C.t * 5 + i) * 0.15f;
        person(x, G - 8, 20, l, p);
    }
    // Arthur: sitting, then standing up and offering his seat; Edie sits down
    float in = sm(C.L(0) + 0.2f, lerp(C.L(0), C.L(1), 0.55f));
    float stand = sm(lerp(C.L(0), C.L(1), 0.6f), lerp(C.L(0), C.L(1), 0.75f));
    float sit = sm(lerp(C.L(0), C.L(1), 0.85f), C.L(1));
    const float ax = seatX[3];
    {
        Pose p; p.face = -1;
        p.sit = 1 - stand;
        float x = lerp(ax, ax + 15, stand);
        if (stand > 0 && sit < 1) p.armF = 1.3f;                // after you
        if (sit >= 1) { p.face = -1; p.armF = 3.0f; p.armB = 0.2f; }   // holding the rack strap
        if (C.t > C.L(1) && C.t < C.L(1) + 1.2f) p.armF = 3.0f;
        Look l = arthurLook(true);
        l.top = H(0x5a5a62); l.bottom = H(0x4a4a52); l.legs = l.bottom;
        person(x, p.sit > 0.5f ? G - 8 : G, 20, l, p);
        if (sit >= 1) Ln(x - 2, 10, x - 2, 22, 0.6f, H(0xc8a060));
    }
    {
        Look l = edieLook(18);
        float x = lerp(-30, ax - 13, in);
        Pose p = in < 1 ? walking(7, 1) : Pose{};
        if (sit > 0) { x = lerp(ax - 13, ax, sit); p.sit = sit; p.face = 1; p.move = 0; p.armF = 0.6f; }
        person(x, p.sit > 0.5f ? lerp(G, G - 8, p.sit) : G, 18, l, p);
        // her suitcase: carried, then set down by the seat
        float cx = sit > 0 ? ax - 9 : x + 2.5f, cy = sit > 0 ? G - 6 : G - 9 + (in < 1 ? std::sin(C.t * 14) * 0.3f : 0);
        outlined([&] { R(cx, cy, 7, 6, H(0x8a5a3a)); });
        R(cx + 2.5f, cy - 1.4f, 2, 1.4f, H(0x3a2a20));
    }
    // the floor of the aisle
    R(-40, G, 280, 20, H(0x3a2a24));
    for (int i = 0; i < 20; ++i) R(-40 + i * 16, G + 2, 10, 0.6f, H(0x2a1a14));
    // sunlight through the windows sweeping across the carriage
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 6; ++i) {
        float x = -22 + i * 38 + std::sin(C.t * 0.5f) * 4;
        Quad({x, 18}, {x + 30, 18}, {x + 40, G}, {x + 10, G}, alpha(mixc(H(0xfff0c0), H(0xffa060), evening), 0.06f));
    }
    EndBlendMode();
    endCam();
    // into a tunnel and out again, once
    float tun = C.L(1) - 0.9f;
    if (C.t > tun && C.t < tun + 0.7f) screenFill(alpha(BLACK, 0.85f * std::sin((C.t - tun) / 0.7f * PI_F)));
}

// ---------------------------------------------------------------- 5: the hay meadow (2.5D, smooth)
void sHay()
{
    const float G = 84;
    // the race: Edie runs the long field; the camera runs with her (parallax layers in depth)
    float race0 = C.L(1) - 0.2f, race1 = C.L(2) + 0.6f;
    float run = sm(race0, race1);
    float ex = lerp(60, 330, run);
    float camX = std::max(88.9f, std::min(ex + 10, 300.0f));
    float zoom = lerp(1.08f, 1.0f, sm(0, 4)) + 0.04f * sm(C.L(2) + 1, C.end);
    beginCam(camX, 50, zoom);
    float par = camX - 88.9f;
    // sky: gold to rose, a low sun
    GradVf(-200, -40, 900, 60, H(0x4a3a6e), H(0xd8705a));
    GradVf(-200, 20, 900, 50, H(0xd8705a), H(0xf4b45a));
    GradVf(-200, 70, 900, 40, H(0xf4b45a), H(0xf8cc70));
    float sunX = 120 + par * 0.95f;
    glow(sunX, 56, 34, alpha(H(0xffd890), 0.38f));
    Cir(sunX, 56, 7, H(0xfff2c8));
    for (int i = 0; i < 5; ++i) {
        float x = par * 0.9f + std::fmod(hf(i) * 400 + C.t * 1.5f, 400.0f) - 100;
        E(x, 20 + hf(i + 3) * 18, 26, 2.6f, alpha(H(0xf09878), 0.75f));
    }
    // far hills, misty; a village with a church spire on the far hill
    auto layer = [&](float depth, float base, float amp, float freq, Color c) {
        float off = par * (1 - depth);
        float step = 1.2f;
        for (float x = -120 + off; x < 520 + off; x += step) {
            float u = (x - off);
            float h = amp * (0.6f * std::sin(u * freq) + 0.4f * std::sin(u * freq * 2.7f + 1));
            DrawRectangleRec({x, base - h, step + 0.05f, 90}, c);
        }
        return off;
    };
    layer(0.9f, 62, 5, 0.03f, H(0x9a6a86));
    float off2 = par * (1 - 0.75f);
    R(130 + off2, 52, 4, 10, H(0x7a5a72)); Tri({129 + off2, 52}, {135 + off2, 52}, {132 + off2, 42}, H(0x7a5a72));
    for (int i = 0; i < 5; ++i) { R(110 + off2 + i * 6, 56, 5, 6, H(0x86607a)); Tri({109.5f + off2 + i * 6, 56}, {115.5f + off2 + i * 6, 56}, {112.5f + off2 + i * 6, 53}, H(0x6a4a62)); }
    layer(0.75f, 68, 4, 0.045f, H(0xa8705e));
    float off3 = layer(0.55f, 74, 3, 0.06f, H(0xa47a3c));
    // hedgerow trees on the middle distance
    for (int i = 0; i < 10; ++i) { float x = off3 + i * 52 + hf(i) * 20; E(x, 70, 7, 6, H(0x4e5428)); E(x + 4, 67, 5, 5, H(0x6a6a30)); E(x - 2, 66, 3, 3, H(0x8a7a3a)); }
    // the field: gold stripes of cut hay running into the distance
    GradVf(-200, 74, 900, 40, H(0xe6a640), H(0xb87224));
    for (int i = 0; i < 9; ++i) {
        float y = 76 + i * i * 0.35f;
        float off = par * (1 - (0.4f - i * 0.04f));
        for (float x = -120 + std::fmod(off, 12.0f); x < 520; x += 12) E(x, y, 4 + i * 0.6f, 0.35f + i * 0.08f, H(0xf6c860));
    }
    // haycocks and a cart with villagers pitching hay up onto it
    auto haycock = [&](float x, float y, float s) {
        fan(x, y, s * 1.1f, s * 0.2f, Color{90, 50, 20, 70}, Color{90, 50, 20, 0});
        E(x, y - s * 0.45f, s * 0.8f, s * 0.6f, H(0xc8862a));
        E(x - s * 0.2f, y - s * 0.6f, s * 0.5f, s * 0.38f, H(0xf0c050));
        E(x - s * 0.35f, y - s * 0.75f, s * 0.22f, s * 0.15f, H(0xffe08a));
        E(x + s * 0.35f, y - s * 0.3f, s * 0.42f, s * 0.3f, H(0x9a6220));
    };
    for (int i = 0; i < 8; ++i) haycock(20 + i * 48 + hf(i) * 10, 79 + (i % 2) * 2, 5 + (i % 3));
    const float cartX = 40;
    R(cartX - 14, 66, 28, 8, H(0x7a4a2a));
    E(cartX, 62, 16, 8, H(0xe8c060));
    E(cartX - 4, 57, 10, 5, H(0xf8d878));
    Cir(cartX - 9, 76, 4, H(0x5a3a1a)); Cir(cartX - 9, 76, 1.5f, H(0x8a6a4a));
    Cir(cartX + 9, 76, 4, H(0x5a3a1a)); Cir(cartX + 9, 76, 1.5f, H(0x8a6a4a));
    for (int i = 0; i < 3; ++i) {
        float x = cartX + 20 + i * 12, pitch = std::sin(C.t * 2.2f + i * 1.9f);
        Look l = arthurLook(true);
        const unsigned shirts[3] = {0xf0f0e8, 0xa8c8e8, 0xd8b890};
        l.top = H(shirts[i]); l.bottom = H(0x5a4a3a); l.legs = l.bottom; l.hair_ = i == 1 ? HAIR_HAT : HAIR_SHORT; l.extra = H(0xe8d090);
        Pose p; p.face = -1; p.armF = 1.6f + pitch * 0.8f; p.armB = 1.2f + pitch * 0.6f;
        Joints j = person(x, G - 2, 19, l, p);
        DrawLineEx({j.handF.x + 3, j.handF.y + 4}, {j.handF.x - 6, j.handF.y - 6 - pitch * 3}, 0.5f, H(0x6a4a2a));
        if (pitch > 0.6f) E(j.handF.x - 6, j.handF.y - 8 - pitch * 3, 2.5f, 1.4f, H(0xf0c860));
    }
    // Edie: by the cart, then off down the long field after the swallows, arms up at the end
    {
        Look l = edieLook(12);
        Pose p;
        if (C.t < race0) { p.face = 1; p.armF = 0.3f + std::sin(C.t * 2) * 0.1f; }
        else if (run < 1) { p = walking(16, 1); p.armF *= 2.4f; p.armB *= 2.4f; }
        else { p.face = -1; p.armF = 2.7f + std::sin(C.t * 6) * 0.15f; p.armB = -2.7f; p.bob = -std::fabs(std::sin(C.t * 6)) * 1.2f; }
        person(ex, G, 15, l, p);
    }
    // the swallows: swooping alongside her, then up into the sunset
    for (int i = 0; i < 6; ++i) {
        float fx, fy;
        float ph = C.t * 3 + i * 1.1f;
        if (C.t < race0) { fx = 160 + i * 18 + std::sin(ph * 0.7f) * 20; fy = 30 + std::sin(ph) * 8 + i * 3; }
        else {
            float lead = 6 + i * 7 + std::sin(ph) * 5;
            fx = ex + lead; fy = 66 - i * 4 + std::sin(ph * 1.3f) * 5;
            float up = sm(race1 - 0.3f, race1 + 3.5f);
            fy -= up * (40 + i * 6); fx += up * 20;
        }
        bird(fx, fy, 2.2f, C.t * 14 + i, H(0x2a2440));
    }
    // dust and seeds in the low sun
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 60; ++i) {
        float x = par * 0.8f + std::fmod(hf(i) * 260 + C.t * (2 + hf(i + 1) * 4), 260.0f) - 40;
        float y = 40 + hf(i + 7) * 45 + std::sin(C.t + i) * 2;
        float a = 0.15f + 0.15f * std::sin(C.t * 2 + i);
        fan(x, y, 0.5f, 0.5f, alpha(H(0xfff0c0), a), alpha(H(0xfff0c0), 0));
    }
    EndBlendMode();
    endCam();
    // warm grade and a soft vignette
    BeginBlendMode(BLEND_ALPHA);
    DrawRectangleGradientV(0, C.ch * 4 / 5, C.cw, C.ch / 5, Color{60, 20, 20, 0}, Color{60, 20, 20, 90});
    EndBlendMode();
}

// ================================================================ the 3D dioramas (stage 6 and the ending)
Shader g_sh3{};
bool g_3dOk = false, g_3dTried = false;
Mesh g_cube{}, g_sph{}, g_cyl{}, g_cone{};
Material g_mat{};
int l_sun = -1, l_sunCol = -1, l_sky = -1, l_ground = -1, l_emis = -1, l_grow = -1, l_out = -1, l_outCol = -1, l_view = -1, l_fog = -1, l_fogCol = -1;

const char* VS3 = R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform float grow;
out vec3 fragPosition;
out vec3 fragNormal;
void main()
{
    vec3 n = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    vec4 wp = matModel * vec4(vertexPosition, 1.0);
    wp.xyz += n * grow;
    fragPosition = wp.xyz;
    fragNormal = n;
    gl_Position = mvp * inverse(matModel) * wp;
}
)";
const char* FS3 = R"(#version 330
in vec3 fragPosition;
in vec3 fragNormal;
uniform vec4 colDiffuse;
uniform vec3 sunDir;
uniform vec3 sunCol;
uniform vec3 skyCol;
uniform vec3 groundCol;
uniform float emissive;
uniform float outline;
uniform vec3 outCol;
uniform vec3 viewPos;
uniform vec2 fog;      // start, end distance
uniform vec3 fogCol;
out vec4 finalColor;
void main()
{
    if (outline > 0.5) { finalColor = vec4(outCol, 1.0); return; }
    vec3 n = normalize(fragNormal);
    vec3 base = colDiffuse.rgb;
    float d = dot(n, -sunDir);
    float band = mix(0.6, 1.0, smoothstep(0.12, 0.3, d));
    vec3 amb = mix(groundCol, skyCol, n.y * 0.5 + 0.5);
    vec3 c = base * (0.42 * amb + 0.7 * band * sunCol);
    vec3 v = normalize(viewPos - fragPosition);
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    c += sunCol * rim * 0.12;
    c = mix(c, base, clamp(emissive, 0.0, 1.0));
    float f = clamp((length(viewPos - fragPosition) - fog.x) / (fog.y - fog.x), 0.0, 1.0);
    c = mix(c, fogCol, f * 0.85);
    finalColor = vec4(c, colDiffuse.a);
}
)";

bool init3D()
{
    if (g_3dTried) return g_3dOk;
    g_3dTried = true;
    g_sh3 = LoadShaderFromMemory(VS3, FS3);
    if (!g_sh3.id || g_sh3.id == rlGetShaderIdDefault()) return false;
    l_sun = GetShaderLocation(g_sh3, "sunDir");
    l_sunCol = GetShaderLocation(g_sh3, "sunCol");
    l_sky = GetShaderLocation(g_sh3, "skyCol");
    l_ground = GetShaderLocation(g_sh3, "groundCol");
    l_emis = GetShaderLocation(g_sh3, "emissive");
    l_grow = GetShaderLocation(g_sh3, "grow");
    l_out = GetShaderLocation(g_sh3, "outline");
    l_outCol = GetShaderLocation(g_sh3, "outCol");
    l_view = GetShaderLocation(g_sh3, "viewPos");
    l_fog = GetShaderLocation(g_sh3, "fog");
    l_fogCol = GetShaderLocation(g_sh3, "fogCol");
    g_cube = GenMeshCube(1, 1, 1);
    g_sph = GenMeshSphere(0.5f, 14, 18);
    g_cyl = GenMeshCylinder(0.5f, 1, 18);
    g_cone = GenMeshCone(0.5f, 1, 18);
    g_mat = LoadMaterialDefault();
    g_mat.shader = g_sh3;
    g_3dOk = true;
    return true;
}
void unload3D()
{
    if (!g_3dOk) return;
    UnloadMesh(g_cube); UnloadMesh(g_sph); UnloadMesh(g_cyl); UnloadMesh(g_cone);
    g_mat.shader = Shader{rlGetShaderIdDefault(), rlGetShaderLocsDefault()};
    UnloadMaterial(g_mat);
    UnloadShader(g_sh3);
    g_3dOk = false; g_3dTried = false;
}
void set1(int loc, float v) { SetShaderValue(g_sh3, loc, &v, SHADER_UNIFORM_FLOAT); }
void set3(int loc, Vector3 v) { SetShaderValue(g_sh3, loc, &v, SHADER_UNIFORM_VEC3); }
Vector3 cv(Color c) { return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f}; }

// One lit part (and its dark outline hull): mesh m placed by M (unit mesh -> world).
void part(Mesh& m, Matrix M, Color c, float emissive = 0, bool outline = true)
{
    g_mat.maps[MATERIAL_MAP_DIFFUSE].color = c;
    set1(l_emis, emissive);
    set1(l_out, 0);
    set1(l_grow, 0);
    if (c.a < 255) rlDisableDepthMask();
    DrawMesh(m, g_mat, M);
    if (c.a < 255) rlEnableDepthMask();
    if (outline && c.a == 255) {
        set1(l_out, 1);
        set1(l_grow, 0.022f);
        rlSetCullFace(RL_CULL_FACE_FRONT);
        DrawMesh(m, g_mat, M);
        rlSetCullFace(RL_CULL_FACE_BACK);
        set1(l_out, 0);
        set1(l_grow, 0);
    }
}
Matrix TRS(Vector3 p, Vector3 s, Vector3 rotDeg = {0, 0, 0})
{
    Matrix r = MatrixRotateXYZ({rotDeg.x * DEG2RAD, rotDeg.y * DEG2RAD, rotDeg.z * DEG2RAD});
    return MatrixMultiply(MatrixMultiply(MatrixScale(s.x, s.y, s.z), r), MatrixTranslate(p.x, p.y, p.z));
}
void box(Vector3 p, Vector3 s, Color c, Vector3 rot = {0, 0, 0}, float em = 0, bool ol = true) { part(g_cube, TRS(p, s, rot), c, em, ol); }
void ball(Vector3 p, float d, Color c, float em = 0, bool ol = true) { part(g_sph, TRS(p, {d, d, d}), c, em, ol); }
void ballS(Vector3 p, Vector3 s, Color c, float em = 0, bool ol = true) { part(g_sph, TRS(p, s), c, em, ol); }
// a cylinder from a to b, diameter d
void rod(Vector3 a, Vector3 b, float d, Color c, Mesh* mesh = nullptr, bool ol = true)
{
    Vector3 v = Vector3Subtract(b, a);
    float len = Vector3Length(v);
    if (len < 1e-4f) return;
    Vector3 dir = Vector3Scale(v, 1 / len);
    Quaternion q = QuaternionFromVector3ToVector3({0, 1, 0}, dir);
    // straight down has no unique shortest rotation: turn it over about x instead
    Matrix rot = dir.y < -0.9999f ? MatrixRotateX(PI_F) : QuaternionToMatrix(q);
    Matrix M = MatrixMultiply(MatrixMultiply(MatrixScale(d, len, d), rot), MatrixTranslate(a.x, a.y, a.z));
    part(mesh ? *mesh : g_cyl, M, c, 0, ol);
}

// A person in 3D: feet at p, facing yaw (degrees, 0 = +z), h tall.
struct Pose3 {
    float walk = 0, move = 0;
    float armF = 0.1f, armB = 0.1f;   // the right / left arm, forward swing in radians
    float armOutR = 0, armOutL = 0;   // raised to the side
    float sit = 0, headYaw = 0, bob = 0;
};
void person3(Vector3 p, float yaw, float h, const Look& L, const Pose3& P)
{
    Matrix base = MatrixMultiply(MatrixRotateY(yaw * DEG2RAD), MatrixTranslate(p.x, p.y, p.z));
    auto W = [&](Vector3 v) { return Vector3Transform(v, base); };
    const float hipY = h * 0.47f, legLen = h * 0.47f;
    float seat = h * 0.28f;
    float hy = lerp(hipY, seat, P.sit) + P.bob;
    for (int s = -1; s <= 1; s += 2) {
        Vector3 hip{s * h * 0.06f, hy, 0};
        float ph = P.walk + (s < 0 ? PI_F : 0);
        Vector3 foot{s * h * 0.06f, std::max(0.0f, std::cos(ph)) * h * 0.05f * P.move, std::sin(ph) * h * 0.17f * P.move};
        foot.z = lerp(foot.z, h * 0.24f, P.sit);
        float dz = foot.z - hip.z, dy = foot.y - hip.y, d = std::sqrt(dz * dz + dy * dy) + 1e-4f;
        float bend = std::sqrt(std::max(0.0f, legLen * legLen / 4 - d * d / 4));
        Vector3 knee{hip.x, (hip.y + foot.y) / 2 + (dz / d) * bend * 0.0f, (hip.z + foot.z) / 2};
        // perpendicular in the y-z plane, toward +z (forward)
        Vector2 perp{-dy / d, dz / d};   // (z, y)
        if (perp.x < 0) { perp.x = -perp.x; perp.y = -perp.y; }
        knee.z += perp.x * bend; knee.y += perp.y * bend;
        rod(W(hip), W(knee), h * 0.085f, L.legs);
        rod(W(knee), W(foot), h * 0.075f, L.legs);
        box(W({foot.x, foot.y + h * 0.025f, foot.z + h * 0.03f}), {h * 0.08f, h * 0.05f, h * 0.15f}, L.shoes, {0, yaw, 0});
    }
    // body
    const float shY = h * 0.8f + (hy - hipY);
    if (L.dress) {
        float hem = P.sit > 0.5f ? hy - h * 0.02f : h * 0.2f;
        Matrix M = MatrixMultiply(MatrixMultiply(MatrixScale(h * 0.36f, (hy + h * 0.12f) - hem, h * 0.3f), MatrixTranslate(0, hem, P.sit * h * 0.06f)), base);
        part(g_cone, M, L.bottom);
    } else {
        rod(W({0, hy - h * 0.04f, 0}), W({0, hy + h * 0.08f, 0}), h * 0.24f, L.bottom);
    }
    Matrix T = MatrixMultiply(MatrixMultiply(MatrixScale(h * 0.25f, shY - hy + h * 0.02f, h * 0.18f), MatrixTranslate(0, hy, 0)), base);
    part(g_cyl, T, L.top);
    ballS(W({0, shY, 0}), {h * 0.27f, h * 0.1f, h * 0.19f}, L.top);
    if (L.apron.a) box(W({0, hy + h * 0.02f, h * 0.1f}), {h * 0.2f, h * 0.3f, h * 0.02f}, L.apron, {0, yaw, 0}, 0, false);
    // arms
    for (int s = -1; s <= 1; s += 2) {
        float fwd = s > 0 ? P.armF : P.armB, outA = s > 0 ? P.armOutR : P.armOutL;
        Vector3 sh{s * h * 0.15f, shY - h * 0.02f, 0};
        float len = h * 0.36f;
        Vector3 hand{sh.x + s * std::sin(outA) * len, sh.y - std::cos(fwd) * std::cos(outA) * len, std::sin(fwd) * std::cos(outA) * len};
        rod(W(sh), W(hand), h * 0.07f, L.top);
        ball(W(hand), h * 0.075f, L.skin);
    }
    // head
    Matrix hb = MatrixMultiply(MatrixRotateY(P.headYaw * DEG2RAD), base);
    auto WH = [&](Vector3 v) { return Vector3Transform(v, hb); };
    float hr = h * 0.13f;
    Vector3 hc{0, shY + h * 0.05f + hr, 0};
    rod(W({0, shY - h * 0.02f, 0}), W({0, shY + h * 0.06f, 0}), h * 0.07f, L.skin, nullptr, false);
    ball(WH(hc), hr * 2, L.skin);
    ballS(WH({0, hc.y + hr * 0.18f, -hr * 0.18f}), {hr * 2.1f, hr * 1.9f, hr * 1.95f}, L.hair);
    if (L.hair_ == HAIR_BUN) ball(WH({0, hc.y + hr * 0.6f, -hr * 0.85f}), hr * 1.0f, L.hair);
    if (L.hair_ == HAIR_PLAITS || L.hair_ == HAIR_BOB) {
        for (int s = -1; s <= 1; s += 2) {
            if (L.hair_ == HAIR_PLAITS) rod(WH({s * hr * 0.8f, hc.y, -hr * 0.3f}), WH({s * hr * 0.9f, hc.y - hr * 1.6f, -hr * 0.2f}), hr * 0.45f, L.hair);
            else ballS(WH({s * hr * 0.55f, hc.y - hr * 0.35f, -hr * 0.25f}), {hr * 0.9f, hr * 1.3f, hr * 1.2f}, L.hair);
        }
        if (L.extra.a) ball(WH({0, hc.y + hr * 0.9f, -hr * 0.3f}), hr * 0.6f, L.extra);
    }
    if (L.hair_ == HAIR_CAP) {
        ballS(WH({0, hc.y + hr * 0.45f, -hr * 0.05f}), {hr * 2.2f, hr * 1.2f, hr * 2.2f}, L.extra);
        box(WH({0, hc.y + hr * 0.35f, hr * 1.0f}), {hr * 1.5f, hr * 0.12f, hr * 0.9f}, L.extra, {0, yaw + P.headYaw, 0});
    }
    for (int s = -1; s <= 1; s += 2) ball(WH({s * hr * 0.38f, hc.y + hr * 0.05f, hr * 0.9f}), hr * 0.22f, H(0x1c1420), 1, false);
}

struct Light3 { Vector3 sunDir; Color sun, sky, ground, fog; float fogNear, fogFar; };
void begin3D(const Camera3D& cam, const Light3& L)
{
    BeginMode3D(cam);
    Vector3 sd = Vector3Normalize(L.sunDir);
    set3(l_sun, sd);
    set3(l_sunCol, cv(L.sun));
    set3(l_sky, cv(L.sky));
    set3(l_ground, cv(L.ground));
    set3(l_outCol, {0.10f, 0.07f, 0.09f});
    set3(l_view, cam.position);
    Vector2 f{L.fogNear, L.fogFar};
    SetShaderValue(g_sh3, l_fog, &f, SHADER_UNIFORM_VEC2);
    set3(l_fogCol, cv(L.fog));
    rlEnableBackfaceCulling();
}
Camera3D cam3(Vector3 pos, Vector3 target, float fovy = 38)
{
    Camera3D c{};
    c.position = pos; c.target = target; c.up = {0, 1, 0}; c.fovy = fovy; c.projection = CAMERA_PERSPECTIVE;
    return c;
}
Vector3 lerp3(Vector3 a, Vector3 b, float k) { return {lerp(a.x, b.x, k), lerp(a.y, b.y, k), lerp(a.z, b.z, k)}; }

void tree3(float x, float z, float s, int seed)
{
    rod({x, 0, z}, {x, 1.6f * s, z}, 0.28f * s, H(0x6a4a30));
    const unsigned greens[3] = {0x4a8a3a, 0x5a9a40, 0x3a7a34};
    for (int i = 0; i < 4; ++i) {
        float a = i * 1.7f + seed;
        ball({x + std::cos(a) * 0.5f * s, (2.0f + (i % 2) * 0.5f) * s, z + std::sin(a) * 0.5f * s}, (1.3f + hf(seed + i) * 0.4f) * s, H(greens[(i + seed) % 3]));
    }
}

// The cottage at the end of the lane: white walls, a thatched roof, a chimney, warm windows.
void cottage3(float lamps, float doorOpen)
{
    const float cz = -5, w = 6.4f, d = 3.4f, wallH = 2.5f;
    box({0, wallH / 2, cz}, {w, wallH, d}, H(0xf0ead8));
    // the thatch: a square prism turned 45 degrees, its lower half hidden in the walls
    box({0, wallH, cz}, {w + 0.6f, 2.6f, 2.6f}, H(0xc8a050), {45, 0, 0});
    box({0, wallH + 1.85f, cz}, {w + 0.62f, 0.25f, 0.25f}, H(0xa88038), {45, 0, 0}, 0, false);
    box({2.0f, wallH + 1.9f, cz - 0.4f}, {0.6f, 1.3f, 0.6f}, H(0xa86a50));
    box({2.0f, wallH + 2.58f, cz - 0.4f}, {0.72f, 0.12f, 0.72f}, H(0x8a5a40));
    // the door, opening inward, and the windows glowing
    const float fz = cz + d / 2 + 0.02f;
    box({0, 0.95f, fz}, {1.0f, 1.9f, 0.06f}, H(0x2a1a14), {0, 0, 0}, 1, false);
    Matrix door = MatrixMultiply(MatrixMultiply(MatrixTranslate(0.5f, 0, 0), MatrixRotateY(-doorOpen * 1.6f)), MatrixTranslate(-0.5f, 0.95f, fz + 0.02f));
    part(g_cube, MatrixMultiply(MatrixScale(1.0f, 1.9f, 0.08f), MatrixMultiply(MatrixTranslate(0.5f, 0, 0), door)), H(0x3a6a4a));
    Color win = mixc(H(0xf0e0b0), H(0xffc860), lamps);
    for (int s = -1; s <= 1; s += 2) {
        float x = s * 1.9f;
        box({x, 1.4f, fz}, {1.1f, 0.9f, 0.05f}, win, {0, 0, 0}, 0.6f + 0.4f * lamps, false);
        box({x, 1.4f, fz + 0.03f}, {0.07f, 0.9f, 0.05f}, H(0xf8f4ea), {0, 0, 0}, 0, false);
        box({x, 1.4f, fz + 0.03f}, {1.1f, 0.07f, 0.05f}, H(0xf8f4ea), {0, 0, 0}, 0, false);
        box({x, 0.9f, fz + 0.12f}, {1.3f, 0.12f, 0.25f}, H(0xf8f4ea));
        for (int i = 0; i < 4; ++i) ball({x - 0.45f + i * 0.3f, 1.02f, fz + 0.15f}, 0.2f, i % 2 ? H(0xe84a6a) : H(0xf8d040), 0, false);
    }
    // roses up the wall by the door
    for (int i = 0; i < 9; ++i) ball({-0.8f - hf(i) * 0.3f, 0.3f + i * 0.22f, fz + 0.05f}, 0.26f, i % 3 ? H(0x3a7a34) : H(0xe8506a), 0, false);
}

void lane3(float gateOpen)
{
    // the green, the lane, hedges, a picket fence with its gate
    box({0, -0.05f, 0}, {60, 0.1f, 60}, H(0x7ab84a), {0, 0, 0}, 0, false);
    box({0, -0.02f, 8}, {1.8f, 0.06f, 20}, H(0xc8a878), {0, 0, 0}, 0, false);
    box({0, -0.02f, -1.6f}, {1.1f, 0.06f, 3.4f}, H(0xb89868), {0, 0, 0}, 0, false);
    for (int s = -1; s <= 1; s += 2) {
        box({s * 3.4f, 0.4f, 10}, {1.2f, 0.8f, 18}, H(0x4a8a3a));
        for (int i = 0; i < 9; ++i) ball({s * 3.4f, 0.85f, 2.2f + i * 2}, 1.1f, H(i % 2 ? 0x55963f : 0x4a8a3a), 0, false);
        for (int i = 0; i < 7; ++i) {
            float x = s * (0.75f + i * 0.5f);
            box({x, 0.45f, 0}, {0.12f, 0.9f, 0.08f}, H(0xf8f4ea));
        }
        box({s * 2.2f, 0.62f, 0}, {3.0f, 0.08f, 0.06f}, H(0xf8f4ea), {0, 0, 0}, 0, false);
        box({s * 2.2f, 0.3f, 0}, {3.0f, 0.08f, 0.06f}, H(0xf8f4ea), {0, 0, 0}, 0, false);
    }
    // the gate swings in on its hinge at x = -0.6
    Matrix hinge = MatrixMultiply(MatrixRotateY(gateOpen * 1.7f), MatrixTranslate(-0.6f, 0, 0));
    for (int i = 0; i < 3; ++i) part(g_cube, MatrixMultiply(TRS({0.2f + i * 0.4f, 0.45f, 0}, {0.12f, 0.85f, 0.08f}), hinge), H(0xf8f4ea));
    part(g_cube, MatrixMultiply(TRS({0.6f, 0.6f, 0}, {1.2f, 0.08f, 0.06f}), hinge), H(0xf8f4ea), 0, false);
    // flower beds in the front garden
    for (int i = 0; i < 26; ++i) {
        float x = (hf(i) - 0.5f) * 7, z = -0.6f - hf(i + 50) * 2.2f;
        if (std::fabs(x) < 0.8f) continue;
        const unsigned fc[4] = {0xe84a6a, 0xf8d040, 0xa070e0, 0xf8f4ea};
        ball({x, 0.18f, z}, 0.24f, H(fc[i % 4]), 0, false);
    }
    tree3(-6.5f, -6, 1.3f, 1);
    tree3(6.0f, -8, 1.5f, 2);
    tree3(-6.8f, 9, 1.0f, 3);
    tree3(5.5f, 12, 1.2f, 4);
    // the far hills
    for (int i = 0; i < 5; ++i) ballS({-24 + i * 12.0f, -2, -30.0f - (i % 2) * 6.0f}, {22, 8.0f + (i % 3) * 2, 12}, H(i % 2 ? 0x8ab860 : 0x7aa858), 0, false);
}

void sky2D(Color top, Color bottom, Vector2 sun, Color sunC)
{
    DrawRectangleGradientV(0, 0, C.cw, C.ch, top, bottom);
    BeginBlendMode(BLEND_ADDITIVE);
    float r = C.ch * 0.35f;
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < 48; ++i) {
        float a0 = i * 2 * PI_F / 48, a1 = (i + 1) * 2 * PI_F / 48;
        rlColor4ub(sunC.r, sunC.g, sunC.b, sunC.a); rlVertex2f(sun.x, sun.y);
        rlColor4ub(sunC.r, sunC.g, sunC.b, 0);
        rlVertex2f(sun.x + std::cos(a1) * r, sun.y + std::sin(a1) * r);
        rlVertex2f(sun.x + std::cos(a0) * r, sun.y + std::sin(a0) * r);
    }
    rlEnd();
    EndBlendMode();
}

void smoke3(Vector3 from, float t, int n)
{
    for (int i = 0; i < n; ++i) {
        float ph = std::fmod(t * 0.18f + i / (float)n, 1.0f);
        Vector3 p{from.x + ph * 1.6f + std::sin(t + i) * 0.15f, from.y + ph * 3.2f, from.z - ph * 0.4f};
        ball(p, 0.35f + ph * 0.9f, Color{235, 230, 225, (unsigned char)(170 * (1 - ph))}, 0.6f, false);
    }
}

// ---------------------------------------------------------------- 6: the cottage (3D)
void sCottage()
{
    float a = C.L(1), b = C.L(2);
    // Edie plays in the lane; her mother calls from the gate; she runs up the lane; they go in
    float call = sm(a, a + 0.6f);
    float runIn = sm(lerp(a, b, 0.35f), lerp(a, b, 0.85f));
    float goIn = sm(b + 0.6f, b + 4.0f);
    float doorOpen = sm(b + 0.2f, b + 1.0f) * (1 - sm(b + 4.6f, b + 5.4f));
    float gateOpen = sm(lerp(a, b, 0.7f), lerp(a, b, 0.9f));
    float lamps = sm(b + 4.6f, b + 6.5f);
    // camera: down the lane, closer while she runs, then back up to see the cottage whole
    Vector3 p0{0.3f, 1.3f, 16}, p1{1.1f, 1.45f, 7.5f}, p2{-1.4f, 3.4f, 14.5f};
    float k1 = sm(0, a + 2), k2 = sm(b, C.end + 1.5f);
    Vector3 cp = lerp3(lerp3(p0, p1, k1), p2, k2);
    Vector3 ct = lerp3(lerp3({0, 1.2f, 2}, {0, 1.1f, -1}, k1), {0, 1.8f, -4}, k2);
    ct.x += std::sin(C.t * 0.3f) * 0.15f;
    Camera3D cam = cam3(cp, ct, 40);
    float dusk = sm(b + 2, C.end + 2);
    sky2D(mixc(H(0x6ab0e8), H(0x7a90d0), dusk), mixc(H(0xf8e8c0), H(0xf8c890), dusk), {C.cw * 0.78f, C.ch * 0.25f}, Color{255, 236, 190, 140});
    Light3 L{{-0.5f, -0.75f, -0.45f}, mixc(H(0xfff0d0), H(0xffd0a0), dusk), H(0xb0d0f0), H(0x8a9a60), mixc(H(0xe8ecf0), H(0xf0d0b0), dusk), 18, 46};
    begin3D(cam, L);
    lane3(gateOpen);
    cottage3(lamps, doorOpen);
    // Mum at the gate in her apron
    Look mum;
    mum.skin = H(0xf2c8a2); mum.hair = H(0x6a3a22); mum.hair_ = HAIR_BUN; mum.dress = true;
    mum.top = H(0x5a7ab0); mum.bottom = H(0x5a7ab0); mum.legs = H(0xe8c0a0); mum.shoes = H(0x3a2a20); mum.apron = H(0xf8f4ea);
    Look edie = edieLook(5);
    Vector3 gate{0.0f, 0, -0.9f}, door{0, 0, -2.9f};
    Vector3 mumP = lerp3({0.9f, 0, -0.7f}, door, goIn);
    Vector3 edieStart{1.1f + std::sin(C.t * 0.8f) * 0.6f, 0, 7.5f + std::cos(C.t * 0.6f) * 0.4f};
    Vector3 edieP = lerp3(edieStart, {0.25f, 0, 0.6f}, runIn);
    if (goIn > 0) edieP = lerp3({0.25f, 0, 0.6f}, {-0.35f, 0, -2.8f}, goIn);
    {
        Pose3 p;
        float yaw = 180 + 160;   // looking down the lane, a little toward Edie
        if (call > 0 && runIn < 0.9f) { p.armOutR = 0.5f + 0.9f * call + std::sin(C.t * 7) * 0.25f * call; p.armF = 1.8f * call; }
        if (goIn > 0) { yaw = 180; p = Pose3{}; p.walk = C.t * 7; p.move = goIn < 1 ? 1 : 0; p.armB = 0.6f; }
        if (goIn >= 1 && C.t > b + 4.0f) { yaw = 180; }
        float vis = 1 - sm(b + 3.6f, b + 4.2f);
        if (vis > 0) person3(mumP, goIn > 0 ? 180 : 0, 1.75f, mum, p);
        (void)yaw;
    }
    {
        Pose3 p;
        float yaw = 0;
        if (runIn <= 0) {
            // chasing a butterfly round the lane
            p.walk = C.t * 9; p.move = 0.7f; yaw = std::fmod(C.t * 40, 360.0f);
            yaw = 90 + std::sin(C.t * 0.8f) * 80;
        } else if (runIn < 1) { p.walk = C.t * 13; p.move = 1; yaw = 180; p.armF = 0.6f * std::sin(C.t * 13); p.armB = -p.armF; }
        else if (goIn <= 0) { yaw = 180; p.armF = 1.2f; }
        else { yaw = 180; p.walk = C.t * 9; p.move = goIn < 1 ? 1 : 0; p.armB = 0.9f; }
        float vis = 1 - sm(b + 3.8f, b + 4.3f);
        if (vis > 0) person3(edieP, yaw, 1.0f, edie, p);
    }
    // the butterfly
    if (runIn < 1) {
        Vector3 bp{edieStart.x + std::sin(C.t * 1.3f) * 0.9f, 1.1f + std::sin(C.t * 3.1f) * 0.25f, edieStart.z + std::cos(C.t * 1.1f) * 0.9f};
        float f = std::fabs(std::sin(C.t * 18));
        box({bp.x, bp.y, bp.z}, {0.22f, 0.02f + 0.0f * f, 0.14f}, H(0xf8f0a0), {0, 0, (f - 0.5f) * 80}, 0.5f, false);
    }
    smoke3({2.0f, 5.1f, -5.4f}, C.t, 9);
    EndMode3D();
    // warm grade
    screenFill(alpha(H(0xfff0d8), 0.10f), BLEND_MULTIPLIED);
}

// ---------------------------------------------------------------- the ending: tea by the window (3D)
void sEnding()
{
    // lines: 0 narration (Sam makes the tea), 1 "Sam?", 2 "Hi, Gran.", 3 "Look at you...", 4 "A while ago. Sit down..."
    float walk = sm(C.L(1) - 0.2f, C.L(2) + 0.2f);
    float give = sm(C.L(2) + 0.3f, C.L(2) + 1.3f);
    float sitK = sm(C.L(4) + 0.6f, C.L(4) + 1.8f);
    Vector3 cp = lerp3({3.4f, 1.7f, 5.8f}, {1.0f, 1.3f, 3.0f}, sm(0, C.end + 2));
    Camera3D cam = cam3(cp, lerp3({-0.4f, 1.0f, -0.6f}, {-0.9f, 0.9f, -0.8f}, sm(0, C.end + 2)), 40);
    sky2D(H(0x2a2026), H(0x3a2c30), {C.cw * 0.2f, C.ch * 0.3f}, Color{0, 0, 0, 0});
    Light3 L{{0.8f, -0.55f, 0.45f}, H(0xfff0d8), H(0xd8c8b8), H(0x8a6a50), H(0x3a2c30), 30, 60};
    begin3D(cam, L);
    // the room: floorboards, a rug, the window wall with the garden outside
    box({0, -0.05f, 0}, {14, 0.1f, 12}, H(0xa8784a), {0, 0, 0}, 0, false);
    for (int i = 0; i < 14; ++i) box({-7.0f + i, 0.001f, 0}, {0.03f, 0.01f, 12}, H(0x8a5a34), {0, 0, 0}, 0, false);
    ballS({-0.3f, 0.0f, 0.2f}, {3.6f, 0.04f, 2.6f}, H(0xb04a3a), 0, false);
    const float wz = -2.6f;
    box({-6, 1.6f, wz}, {6, 3.2f, 0.2f}, H(0xe8dcc0));
    box({4.2f, 1.6f, wz}, {6.4f, 3.2f, 0.2f}, H(0xe8dcc0));
    box({-1.5f, 0.45f, wz}, {3.0f, 0.9f, 0.2f}, H(0xe8dcc0));
    box({-1.5f, 2.85f, wz}, {3.0f, 0.7f, 0.2f}, H(0xe8dcc0));
    // outside: the garden in afternoon sun
    box({-1.5f, 1.7f, wz - 0.4f}, {3.2f, 1.6f, 0.05f}, H(0xa8d0f0), {0, 0, 0}, 1, false);
    for (int i = 0; i < 6; ++i) ball({-2.8f + i * 0.55f, 1.05f + (i % 2) * 0.15f, wz - 0.35f}, 0.7f, H(i % 2 ? 0x6aa848 : 0x5a9840), 0.75f, false);
    for (int i = 0; i < 5; ++i) ball({-2.6f + i * 0.6f, 1.0f, wz - 0.3f}, 0.18f, H(i % 2 ? 0xe84a6a : 0xf8d040), 0.85f, false);
    box({-1.5f, 1.7f, wz + 0.06f}, {0.06f, 1.6f, 0.08f}, H(0xf8f4ea), {0, 0, 0}, 0, false);
    box({-1.5f, 1.7f, wz + 0.06f}, {3.0f, 0.06f, 0.08f}, H(0xf8f4ea), {0, 0, 0}, 0, false);
    box({-1.5f, 0.92f, wz + 0.15f}, {3.3f, 0.08f, 0.35f}, H(0xf8f4ea));
    ball({-0.4f, 1.1f, wz + 0.2f}, 0.3f, H(0xe84a6a));
    rod({-0.4f, 0.95f, wz + 0.2f}, {-0.4f, 1.05f, wz + 0.2f}, 0.22f, H(0xc86a3a));
    // the sunbeam through the window onto the floor
    rlDisableDepthMask();
    {
        Color beam{255, 236, 190, 46};
        ballS({-0.8f, 0.02f, -0.6f}, {2.8f, 0.02f, 1.8f}, beam, 1, false);
    }
    rlEnableDepthMask();
    // the kitchen corner on the right: counter, kettle steaming
    box({3.4f, 0.5f, -1.6f}, {2.4f, 1.0f, 1.0f}, H(0x6a8a7a));
    box({3.4f, 1.02f, -1.6f}, {2.5f, 0.06f, 1.1f}, H(0xd8cfc0));
    rod({3.0f, 1.05f, -1.6f}, {3.0f, 1.35f, -1.6f}, 0.32f, H(0xd04a3a));
    float steamOn = 1 - sm(C.L(1), C.L(2));
    for (int i = 0; i < 5; ++i) {
        float ph = std::fmod(C.t * 0.4f + i * 0.2f, 1.0f);
        ball({3.0f + std::sin(C.t * 2 + i) * 0.05f, 1.45f + ph * 0.7f, -1.6f}, 0.1f + ph * 0.18f, Color{250, 250, 250, (unsigned char)(120 * (1 - ph) * steamOn)}, 1, false);
    }
    // her chair, by the window
    const Vector3 ch{-1.6f, 0, -0.9f};
    box({ch.x, 0.42f, ch.z}, {1.0f, 0.2f, 0.9f}, H(0x7a5aa0));
    box({ch.x, 0.9f, ch.z - 0.42f}, {1.0f, 1.0f, 0.2f}, H(0x7a5aa0));
    box({ch.x - 0.55f, 0.6f, ch.z}, {0.18f, 0.55f, 0.9f}, H(0x6a4a90));
    box({ch.x + 0.55f, 0.6f, ch.z}, {0.18f, 0.55f, 0.9f}, H(0x6a4a90));
    for (int s = -1; s <= 1; s += 2) for (int f = -1; f <= 1; f += 2) box({ch.x + s * 0.42f, 0.15f, ch.z + f * 0.35f}, {0.08f, 0.3f, 0.08f}, H(0x4a3020));
    // the footstool Sam sits on
    const Vector3 st{-0.45f, 0, -0.2f};
    box({st.x, 0.18f, st.z}, {0.6f, 0.3f, 0.5f}, H(0xb07a4a));
    // Gran in her chair
    Look gran = granLook();
    gran.top = H(0xf08a24);
    {
        Pose3 p; p.sit = 1; p.armF = 0.9f; p.armB = 0.9f;
        p.headYaw = C.t > C.L(1) - 0.3f ? 45 : -20;
        if (give > 0.5f) { p.armF = 1.2f; p.armB = 1.2f; }
        person3({ch.x, 0.1f, ch.z + 0.05f}, 20, 1.6f, gran, p);
    }
    // Sam: from the kettle to her chair with the tea; sits down at the end
    Look sam;
    sam.skin = H(0xe9b58c); sam.hair = H(0x6b3e22); sam.top = H(0x3c8cf0); sam.bottom = H(0x2b3f6b); sam.legs = sam.bottom; sam.shoes = H(0xf4f4f4);
    sam.hair_ = HAIR_CAP; sam.extra = H(0x4a9a3a);
    Vector3 samP = lerp3({2.8f, 0, -0.8f}, {-0.25f, 0, -0.6f}, walk);
    float yaw = walk > 0 && walk < 1 ? -95 : (walk >= 1 ? -105 : 180);
    Pose3 p;
    if (walk > 0 && walk < 1) { p.walk = C.t * 8; p.move = 1; }
    p.armF = 1.1f;
    if (give > 0.5f) p.armF = 0.4f;
    if (sitK > 0) { samP = lerp3(samP, {st.x, 0.0f, st.z + 0.05f}, sitK); p.sit = sitK; yaw = lerp(-105, -100, sitK); p.headYaw = 10 * sitK; }
    if (C.t < C.L(1) - 0.2f) { p.armF = 1.3f + std::sin(C.t * 3) * 0.1f; }
    person3(samP, yaw, 1.8f, sam, p);
    // the cup of tea: in Sam's hand, then in hers
    Matrix sb = MatrixMultiply(MatrixRotateY(yaw * DEG2RAD), MatrixTranslate(samP.x, samP.y, samP.z));
    Vector3 inSam = Vector3Transform({0.27f, 1.25f - (1 - std::cos(p.armF)) * 0.0f, 0.55f}, sb);
    Vector3 inGran{ch.x + 0.2f, 0.88f, ch.z + 0.55f};
    Vector3 cup = C.t < C.L(1) - 0.2f ? Vector3{2.6f, 1.1f, -1.4f} : lerp3(inSam, inGran, give);
    rod(cup, {cup.x, cup.y + 0.14f, cup.z}, 0.14f, H(0xf8f4ea));
    for (int i = 0; i < 3; ++i) {
        float ph = std::fmod(C.t * 0.5f + i * 0.33f, 1.0f);
        ball({cup.x + std::sin(C.t * 3 + i) * 0.03f, cup.y + 0.2f + ph * 0.35f, cup.z}, 0.05f + ph * 0.08f, Color{255, 255, 255, (unsigned char)(110 * (1 - ph))}, 1, false);
    }
    // a lamp on the side table
    box({-2.6f, 0.35f, 0.1f}, {0.6f, 0.7f, 0.6f}, H(0x6a4a30));
    rod({-2.6f, 0.7f, 0.1f}, {-2.6f, 1.1f, 0.1f}, 0.06f, H(0xc8a060));
    part(g_cone, TRS({-2.6f, 1.05f, 0.1f}, {0.5f, 0.35f, 0.5f}), H(0xf8e0b0), 0.8f);
    EndMode3D();
    // the lamp's glow and a warm grade
    glow(C.cw * 0.12f, C.ch * 0.45f, C.ch * 0.3f, Color{255, 200, 120, 40});
    screenFill(alpha(H(0xfff0dc), 0.08f), BLEND_MULTIPLIED);
}

// ---------------------------------------------------------------- dispatch
void drawScene()
{
    switch (C.id) {
    case PROLOGUE: sPrologue(); break;
    case 0: sNight(); break;
    case 1: sShirts(); break;
    case 2: sSchool(); break;
    case 3: sCamping(); break;
    case 4: sTrain(); break;
    case 5: sHay(); break;
    case 6: sCottage(); break;
    default: sEnding(); break;
    }
}

}  // namespace

int styleOf(int id)
{
    if (id == PROLOGUE) return 0;
    if (id >= ENDING) return art::LAST_ERA;
    return std::max(0, std::min(art::LAST_ERA, id));
}

void draw(Game& g, int id, float t, const std::vector<float>& lineStart, float end, Rectangle dst)
{
    const int st = styleOf(id);
    const bool is3D = id == 6 || id >= ENDING;
    if (is3D && !init3D()) return;
    const int hpx = CANVAS_H[st];
    const int wpx = (int)std::round(hpx * 16.0f / 9.0f);
    if (!g_rt[st].id) {
        g_rt[st] = LoadRenderTexture(wpx, hpx);
        SetTextureFilter(g_rt[st].texture, art::lowRes(st) ? TEXTURE_FILTER_POINT : TEXTURE_FILTER_BILINEAR);
    }
    if (!g_postTried) {
        g_postTried = true;
        g_post = LoadShaderFromMemory(nullptr, POST_FS);
        g_locMode = GetShaderLocation(g_post, "mode");
        g_locSize = GetShaderLocation(g_post, "size");
    }
    C = Ctx();
    C.g = &g; C.id = id; C.st = st; C.t = t; C.end = end; C.ls = &lineStart;
    C.cw = wpx; C.ch = hpx;
    C.z = hpx / 100.0f; C.px = 1 / C.z; C.W = wpx / C.z;
    C.ink = st == 0 ? H(0x101216) : (st == 1 ? H(art::SHADES4[0]) : H(0x140f1e));

    BeginTextureMode(g_rt[st]);
    ClearBackground(BLACK);
    rlDisableBackfaceCulling();
    drawScene();
    rlDisableBackfaceCulling();
    // the scene opens: an iris in the pixel stages, a fade in the smooth ones
    float open = clamp01(t / 1.4f);
    if (art::lowRes(st)) iris(open, wpx / 2.0f, hpx * 0.55f);
    else if (open < 1) DrawRectangle(0, 0, wpx, hpx, Color{0, 0, 0, (unsigned char)(255 * (1 - smooth(open)))});
    EndTextureMode();
    rlEnableBackfaceCulling();

    int mode = st <= 4 ? st : 5;
    Vector2 size{(float)wpx, (float)hpx};
    if (g_post.id) {
        SetShaderValue(g_post, g_locMode, &mode, SHADER_UNIFORM_INT);
        SetShaderValue(g_post, g_locSize, &size, SHADER_UNIFORM_VEC2);
        BeginShaderMode(g_post);
    }
    DrawTexturePro(g_rt[st].texture, {0, 0, (float)wpx, (float)-hpx}, dst, {0, 0}, 0, WHITE);
    if (g_post.id) EndShaderMode();
}

void unload()
{
    for (auto& r : g_rt) if (r.id) { UnloadRenderTexture(r); r = RenderTexture2D{}; }
    if (g_post.id) UnloadShader(g_post);
    g_post = Shader{};
    g_postTried = false;
    unload3D();
}

}  // namespace scene
