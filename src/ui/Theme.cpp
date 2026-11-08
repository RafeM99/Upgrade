// Theme.cpp (akshat): see Theme.h.
#include "ui/Theme.h"
#include "core/Paths.h"
#include "render/ArtSpec.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace ui {

// ------------------------------------------------------------------ colours
Color hex(unsigned h, unsigned char a) { return Color{(unsigned char)(h >> 16), (unsigned char)(h >> 8), (unsigned char)h, a}; }
Color fade(Color c, float a) { c.a = (unsigned char)std::lround(c.a * clamp01(a)); return c; }
Color mix(Color a, Color b, float k)
{
    k = clamp01(k);
    auto m = [&](unsigned char x, unsigned char y) { return (unsigned char)std::lround(x + (y - x) * k); };
    return Color{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}

namespace col {
const Color ink = {14, 13, 22, 255};
const Color ink2 = {26, 24, 38, 255};
const Color cream = {246, 239, 226, 255};
const Color creamDim = {196, 186, 170, 255};
const Color mute = {128, 120, 112, 255};
const Color amber = {242, 168, 64, 255};
const Color gold = {255, 214, 120, 255};
const Color gran = {242, 138, 30, 255};
const Color sam = {60, 156, 240, 255};
const Color good = {120, 220, 150, 255};
const Color danger = {236, 84, 92, 255};
const Color lilac = {205, 176, 255, 255};
const Color paper = {243, 234, 214, 255};
const Color paperDark = {214, 198, 168, 255};
const Color paperInk = {40, 31, 25, 255};
const Color paperInk2 = {88, 72, 60, 255};
const Color paperRule = {120, 150, 190, 255};
const Color brick = {160, 52, 34, 255};
const Color highlight = {246, 200, 76, 255};
const Color tape = {232, 222, 192, 255};
const Color granInk = {176, 78, 10, 255};
const Color samInk = {30, 92, 158, 255};
}  // namespace col

// ------------------------------------------------------------------ frame
namespace {
float gS = 1, gOX = 0, gOY = 0;
}

void setFrame(Rectangle r)
{
    gS = r.height / 720.0f;
    gOX = r.x;
    gOY = r.y;
}

Rectangle menuRect()
{
    float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    float w = sw, h = sw * 9.0f / 16.0f;
    if (h > sh) { h = sh; w = sh * 16.0f / 9.0f; }
    return Rectangle{std::floor((sw - w) / 2), std::floor((sh - h) / 2), w, h};
}

float S() { return gS; }
float dpi()
{
    float k = GetScreenWidth() > 0 ? (float)GetRenderWidth() / (float)GetScreenWidth() : 1.0f;
    return k > 0.5f ? k : 1.0f;
}
static float snapPx(float v) { float d = dpi(); return std::round(v * d) / d; }
float X(float x) { return snapPx(gOX + x * gS); }
float Y(float y) { return snapPx(gOY + y * gS); }
float L(float len) { return len * gS; }
Rectangle R(float x, float y, float w, float h)
{
    float x0 = X(x), y0 = Y(y), x1 = X(x + w), y1 = Y(y + h);
    return Rectangle{x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}
float hair() { return 1.0f / (gS * dpi()); }
float now() { return (float)GetTime(); }

// ------------------------------------------------------------------ fonts
namespace {
struct FontFile { unsigned char* data = nullptr; int size = 0; bool tried = false; };
FontFile files[FONT_COUNT];
const char* fileNames[FONT_COUNT] = {"fonts/Lora.ttf", "fonts/Lora-Italic.ttf", "fonts/Caveat.ttf",
                                     "fonts/Silkscreen-Regular.ttf", "fonts/Silkscreen-Bold.ttf"};

// Silkscreen is drawn from one big atlas: at a 256 px line its 8 px em is 200 px, so every font
// pixel is exactly 25 texels; drawn with point sampling at a whole number of screen pixels per
// font pixel it stays perfectly square.
constexpr int PIXEL_ATLAS = 256;
constexpr float PIXEL_LINE = 10.24f;   // line box of one font-pixel step (8 px em * 1.28)

struct Entry { int font, px; Font f; double used; };
std::vector<Entry> cache;

std::vector<int> codepoints()
{
    std::vector<int> cp;
    for (int c = 32; c < 127; ++c) cp.push_back(c);
    const int extra[] = {0x00B7, 0x00D7, 0x00E9, 0x00E8, 0x00E0, 0x00F6, 0x00FC, 0x2013, 0x2014, 0x2018, 0x2019,
                         0x201C, 0x201D, 0x2022, 0x2026, 0x2190, 0x2191, 0x2192, 0x2193, 0x00A9};
    for (int c : extra) cp.push_back(c);
    return cp;
}

// Smooth fonts: sizes up to 64 px are rasterised exactly; above that in small steps (animated
// titles would otherwise make a new atlas every frame).
int quantize(int px)
{
    if (px <= 64) return std::max(6, px);
    if (px <= 140) return (px + 2) / 4 * 4;
    return (px + 8) / 16 * 16;
}

const Font& getFont(int font, int px)
{
    if (font < 0 || font >= FONT_COUNT) font = SERIF;
    const bool pixel = isPixelFont(font);
    int q = pixel ? PIXEL_ATLAS : quantize(px);
    double t = GetTime();
    for (auto& e : cache)
        if (e.font == font && e.px == q) { e.used = t; return e.f; }
    // drop atlases nobody has drawn with for a while (never one used this frame)
    if (cache.size() > 24) {
        for (size_t i = 0; i < cache.size();) {
            if (t - cache[i].used > 3.0) { UnloadFont(cache[i].f); cache.erase(cache.begin() + i); }
            else ++i;
        }
    }
    FontFile& ff = files[font];
    if (!ff.tried) {
        ff.tried = true;
        ff.data = LoadFileData(assetPath(fileNames[font]).c_str(), &ff.size);
        if (!ff.data) TraceLog(LOG_WARNING, "UI: font %s missing, using the default font", fileNames[font]);
    }
    Font f = GetFontDefault();
    if (ff.data) {
        static std::vector<int> cps = codepoints();
        f = LoadFontFromMemory(".ttf", ff.data, ff.size, q, cps.data(), (int)cps.size());
        if (f.texture.id == 0) f = GetFontDefault();
        else {
            if (!pixel) GenTextureMipmaps(&f.texture);
            SetTextureFilter(f.texture, pixel ? TEXTURE_FILTER_POINT : TEXTURE_FILTER_TRILINEAR);
        }
    }
    cache.push_back({font, q, f, t});
    return cache.back().f;
}

struct Resolved { const Font* f; float size; float spacing; float bold; };
Resolved resolve(float sizeUI, int font, float track, bool bold)
{
    const float d = dpi();
    Resolved r;
    if (isPixelFont(font)) {
        if (bold) font = PIXEL_BOLD;
        int n = std::max(1, (int)std::lround(sizeUI * gS * d / PIXEL_LINE));   // screen px per font px
        r.f = &getFont(font, 0);
        r.size = PIXEL_LINE * n / d;
        r.spacing = std::round(track * 8) * n / d;
        r.bold = 0;
        if (r.f->texture.id == GetFontDefault().texture.id) r.size = std::max(10.0f, std::round(r.size / 10) * 10);
        return r;
    }
    int px = std::max(6, (int)std::lround(sizeUI * gS * d));
    r.f = &getFont(font, px);
    r.size = px / d;                       // drawn size in window pixels
    if (r.f->texture.id == GetFontDefault().texture.id) r.size = std::max(10.0f, std::round(r.size / 10) * 10);
    r.spacing = std::round(track * px) / d;   // whole physical pixels
    r.bold = bold ? std::max(1.0f, std::round(px * 0.03f)) / d : 0;
    return r;
}
}  // namespace

void unload()
{
    for (auto& e : cache)
        if (e.f.texture.id != GetFontDefault().texture.id) UnloadFont(e.f);
    cache.clear();
    for (auto& ff : files) { if (ff.data) UnloadFileData(ff.data); ff = FontFile{}; }
}

std::string upper(std::string s)
{
    for (char& c : s) if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    return s;
}

const Font& fontAt(float sizeUI, int font, float* drawSize)
{
    Resolved r = resolve(sizeUI, font, 0, false);
    if (drawSize) *drawSize = r.size;
    return *r.f;
}

float textW(const std::string& s, float size, int font, float track, bool bold)
{
    if (s.empty()) return 0;
    Resolved r = resolve(size, font, track, bold);
    return (MeasureTextEx(*r.f, s.c_str(), r.size, r.spacing).x + r.bold) / gS;
}
float textW(const std::string& s, const TextStyle& st) { return textW(s, st.size, st.font, st.track, st.bold); }

void text(const std::string& s, float x, float y, const TextStyle& st)
{
    if (s.empty() || st.color.a == 0) return;
    Resolved r = resolve(st.size, st.font, st.track, st.bold);
    float w = MeasureTextEx(*r.f, s.c_str(), r.size, r.spacing).x + r.bold;
    float px = gOX + x * gS - (st.align == CENTER ? w / 2 : st.align == RIGHT ? w : 0);
    float py = gOY + y * gS;
    px = snapPx(px);
    py = snapPx(py);
    auto draw = [&](float ox, float oy, Color c) {
        DrawTextEx(*r.f, s.c_str(), {px + ox, py + oy}, r.size, r.spacing, c);
        if (r.bold > 0) DrawTextEx(*r.f, s.c_str(), {px + ox + r.bold, py + oy}, r.size, r.spacing, c);
    };
    if (st.shadow.a > 0) {
        float o = snapPx(std::max(1.0f / dpi(), st.shadowOff * gS));
        draw(o, o, st.shadow);
    }
    if (st.outline.a > 0) {
        float o = snapPx(std::max(1.0f / dpi(), st.outlineW * gS));
        for (int i = 0; i < 8; ++i) {
            float a = i * PI / 4;
            draw(std::round(std::cos(a) * o * dpi()) / dpi(), std::round(std::sin(a) * o * dpi()) / dpi(), st.outline);
        }
    }
    draw(0, 0, st.color);
}

void text(const std::string& s, float x, float y, float size, Color c, int align, int font, bool bold)
{
    TextStyle st;
    st.size = size;
    st.color = c;
    st.align = align;
    st.font = font;
    st.bold = bold;
    text(s, x, y, st);
}

void textMid(const std::string& s, float x, float midY, const TextStyle& st)
{
    // where the middle of a capital sits in the line box: Lora 0.51, Caveat 0.52, Silkscreen 0.53
    float k = isPixelFont(st.font) ? 0.53f : (st.font == HAND ? 0.53f : 0.51f);
    text(s, x, midY - st.size * k, st);
}

std::vector<std::string> wrap(const std::string& s, float size, float maxW, int font, float track)
{
    std::vector<std::string> out;
    std::string line, word;
    auto flush = [&]() {
        if (word.empty()) return;
        std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && textW(trial, size, font, track) > maxW) { out.push_back(line); line = word; }
        else line = trial;
        word.clear();
    };
    for (char ch : s) {
        if (ch == ' ') flush();
        else if (ch == '\n') { flush(); out.push_back(line); line.clear(); }
        else word += ch;
    }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}

// ------------------------------------------------------------------ easing
float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
float easeOutCubic(float k) { k = clamp01(k); return 1 - (1 - k) * (1 - k) * (1 - k); }
float easeInOutCubic(float k) { k = clamp01(k); return k < 0.5f ? 4 * k * k * k : 1 - std::pow(-2 * k + 2, 3.0f) / 2; }
float easeOutBack(float k)
{
    k = clamp01(k);
    const float c1 = 1.70158f, c3 = c1 + 1;
    return 1 + c3 * std::pow(k - 1, 3.0f) + c1 * std::pow(k - 1, 2.0f);
}
float approach(float cur, float target, float rate, float dt) { return cur + (target - cur) * (1 - std::exp(-rate * dt)); }

namespace {
struct AnimVal { float v; double at; };
std::unordered_map<int, AnimVal> anims;
}
float anim(int key, float target, float rate)
{
    double t = GetTime();
    auto it = anims.find(key);
    if (it == anims.end() || t - it->second.at > 0.5) {   // first use, or not drawn for a while: jump there
        anims[key] = {target, t};
        return target;
    }
    float dt = (float)std::min(0.1, t - it->second.at);
    it->second.v = approach(it->second.v, target, rate, dt);
    it->second.at = t;
    return it->second.v;
}
void animSet(int key, float value) { anims[key] = {value, GetTime()}; }

// ------------------------------------------------------------------ shapes
void rect(Rectangle r, Color c)
{
    if (c.a == 0) return;
    DrawRectangleRec(R(r.x, r.y, r.width, r.height), c);
}
void rrect(Rectangle r, float radius, Color c)
{
    if (c.a == 0 || r.width <= 0 || r.height <= 0) return;
    Rectangle w = R(r.x, r.y, r.width, r.height);
    float m = std::min(r.width, r.height);
    if (radius <= 0.01f) { DrawRectangleRec(w, c); return; }
    DrawRectangleRounded(w, std::min(1.0f, 2 * radius / m), 10, c);
}
void roundLines(Rectangle r, float radius, float thick, Color c)
{
    if (c.a == 0) return;
    Rectangle w = R(r.x, r.y, r.width, r.height);
    float th = std::max(1.0f / dpi(), thick * gS);
    float m = std::min(r.width, r.height);
    if (radius <= 0.01f) { DrawRectangleLinesEx(w, th, c); return; }
    DrawRectangleRoundedLinesEx(w, std::min(1.0f, 2 * radius / m), 10, th, c);
}
void gradV(Rectangle r, Color top, Color bottom) { DrawRectangleGradientEx(R(r.x, r.y, r.width, r.height), top, bottom, bottom, top); }
void gradH(Rectangle r, Color left, Color right) { DrawRectangleGradientEx(R(r.x, r.y, r.width, r.height), left, left, right, right); }
void softShadow(Rectangle r, float radius, float spread, float alpha)
{
    const int n = 6;
    for (int i = n; i >= 1; --i) {
        float e = spread * i / n;
        rrect(Rectangle{r.x - e * 0.6f, r.y - e * 0.3f + spread * 0.35f, r.width + e * 1.2f, r.height + e * 1.2f}, radius + e,
              fade(BLACK, alpha / n * 1.4f));
    }
}
void vignette(Rectangle r, float s)
{
    Color d = fade(BLACK, s), z = fade(BLACK, 0);
    gradV({r.x, r.y, r.width, r.height * 0.32f}, d, z);
    gradV({r.x, r.y + r.height * 0.6f, r.width, r.height * 0.4f}, z, fade(BLACK, s * 1.1f));
    gradH({r.x, r.y, r.width * 0.22f, r.height}, fade(BLACK, s * 0.7f), z);
    gradH({r.x + r.width * 0.78f, r.y, r.width * 0.22f, r.height}, z, fade(BLACK, s * 0.7f));
}
void tri(Vector2 a, Vector2 b, Vector2 c, Color col)
{
    Vector2 A{X(a.x), Y(a.y)}, B{X(b.x), Y(b.y)}, C{X(c.x), Y(c.y)};
    DrawTriangle(A, B, C, col);
    DrawTriangle(A, C, B, col);
}

// ------------------------------------------------------------------ skins
namespace {
Skin makeSkin(int e)
{
    Skin k;
    k.era = e;
    k.pixel = art::lowRes(e);
    k.font = PIXEL;   // the HUD is Silkscreen on every stage
    k.danger = col::danger;
    switch (e) {
    case 0:   // 1-bit: ink and cream, nothing else
        k.bg = k.bg2 = hex(0x0e0f13, 236);
        k.line = hex(0xebebe1); k.lineHi = hex(0xebebe1);
        k.text = hex(0xebebe1); k.dim = hex(0xa9a99f);
        k.accent = k.accent2 = hex(0xebebe1);
        k.trough = hex(0x2a2b30);
        k.p[0] = k.p[1] = hex(0xebebe1);
        k.danger = k.boss = hex(0xebebe1);
        break;
    case 1: {  // 4-shade: the handheld's four greys, a light LCD panel with dark ink
        const unsigned* G = art::SHADES4;
        k.bg = k.bg2 = hex(G[3], 240);
        k.line = hex(G[0]); k.lineHi = hex(G[2]);
        k.text = hex(G[0]); k.dim = hex(G[1]);
        k.accent = k.accent2 = hex(G[0]);
        k.trough = hex(G[2]);
        k.p[0] = hex(G[0]); k.p[1] = hex(G[1]);
        k.danger = k.boss = hex(G[0]);
        break;
    }
    case 2:   // 8-bit: black boxes with a white border, console colours
        k.bg = k.bg2 = hex(0x000000, 235);
        k.line = hex(0xfcfcfc); k.lineHi = hex(0xfcfcfc);
        k.text = hex(0xfcfcfc); k.dim = hex(0xbcbcbc);
        k.accent = hex(0xf8b800); k.accent2 = hex(0xe45c10);
        k.trough = hex(0x383838);
        k.p[0] = hex(0xfc9838); k.p[1] = hex(0x3cbcfc);
        k.boss = hex(0xd82800);
        break;
    case 3:   // 16-bit: the blue RPG window with a silver frame
        k.bg = hex(0x3a56c0, 240); k.bg2 = hex(0x111a58, 240);
        k.line = hex(0xd8e0f0); k.lineHi = hex(0xffffff);
        k.text = hex(0xf8f8f8); k.dim = hex(0xb8c4f0);
        k.accent = hex(0xf890b8); k.accent2 = hex(0xc04880);
        k.trough = hex(0x0c1240);
        k.p[0] = hex(0xf8a838); k.p[1] = hex(0x68d0f8);
        k.boss = hex(0xf04060);
        break;
    case 4:   // 32-bit: glossy violet with a teal meter
        k.bg = hex(0x3a2f6a, 238); k.bg2 = hex(0x1a1434, 238);
        k.line = hex(0x8676c0); k.lineHi = hex(0xc8bcff);
        k.text = hex(0xf4ecff); k.dim = hex(0xa89cc8);
        k.accent = hex(0x5ef0dc); k.accent2 = hex(0x2196a8);
        k.trough = hex(0x0e0a1e);
        k.p[0] = hex(0xff9a3c); k.p[1] = hex(0x52b8ff);
        k.boss = hex(0xff4f8a);
        break;
    case 5:   // 2.5D: a solid warm-dark plate with a cream rule
        k.radius = 0;
        k.bg = hex(0x231d18, 244); k.bg2 = hex(0x1a1512, 244);
        k.line = hex(0x0d0a08); k.lineHi = hex(0xe8d9bc, 90);
        k.text = hex(0xf6eedf); k.dim = hex(0xd2c2a6);
        k.accent = hex(0xffbe5c); k.accent2 = hex(0xff8a3c);
        k.trough = hex(0xffffff, 40);
        k.p[0] = col::gran; k.p[1] = col::sam;
        k.boss = hex(0xe0507a);
        break;
    default:  // 3D: paper cards, ink text
        k.radius = 0;
        k.bg = col::paper; k.bg2 = col::paper;
        k.line = col::paperDark; k.lineHi = hex(0xffffff, 160);
        k.text = col::paperInk; k.dim = col::paperInk2;
        k.accent = hex(0xe08a2c); k.accent2 = hex(0xb8561a);
        k.trough = hex(0x281f19, 40);
        k.p[0] = col::granInk; k.p[1] = col::samInk;
        k.boss = hex(0xa8243c);
        break;
    }
    return k;
}
}  // namespace

const Skin& skin(int era)
{
    static Skin s[art::ERA_COUNT];
    static bool made = false;
    if (!made) { for (int e = 0; e < art::ERA_COUNT; ++e) s[e] = makeSkin(e); made = true; }
    return s[std::max(0, std::min(art::LAST_ERA, era))];
}

const Skin& menuSkin()
{
    static Skin k = [] {
        Skin m = makeSkin(6);
        m.era = -1;
        m.font = SERIF;
        m.pixel = false;
        m.accent = col::brick; m.accent2 = col::highlight;
        return m;
    }();
    return k;
}

void panel(Rectangle r, const Skin& k, float a)
{
    const float u = 2;   // a border, in UI units
    switch (k.era) {
    case 0:
        rect(r, fade(k.bg, a));
        roundLines(r, 0, u, fade(k.line, a));
        break;
    case 1:
        rect(r, fade(k.bg, a));
        roundLines(r, 0, u, fade(k.line, a));
        rect({r.x + u, r.y + r.height - 2 * u, r.width - 2 * u, u}, fade(k.lineHi, a));   // the LCD's shadow line
        break;
    case 2: {   // a black box with a white border and clipped corners
        rect({r.x + u, r.y, r.width - 2 * u, r.height}, fade(BLACK, a * k.bg.a / 255.0f));
        rect({r.x, r.y + u, r.width, r.height - 2 * u}, fade(BLACK, a * k.bg.a / 255.0f));
        float b = 2 * u;
        rect({r.x + b, r.y + u, r.width - 2 * b, u}, fade(k.line, a));
        rect({r.x + b, r.y + r.height - 2 * u, r.width - 2 * b, u}, fade(k.line, a));
        rect({r.x + u, r.y + b, u, r.height - 2 * b}, fade(k.line, a));
        rect({r.x + r.width - 2 * u, r.y + b, u, r.height - 2 * b}, fade(k.line, a));
        break;
    }
    case 3:
    case 4:   // bevelled window: dark outline, light frame, gradient inside
        rect(r, fade(hex(0x05061a), a * 0.9f));
        rect({r.x + u, r.y + u, r.width - 2 * u, r.height - 2 * u}, fade(k.line, a));
        gradV({r.x + 2 * u, r.y + 2 * u, r.width - 4 * u, r.height - 4 * u}, fade(k.bg, a), fade(k.bg2, a));
        rect({r.x + 2 * u, r.y + 2 * u, r.width - 4 * u, u}, fade(k.lineHi, a * 0.35f));
        break;
    case 5:   // a solid plate: dark edge, a thin cream rule inside the top
        rect({r.x + 2, r.y + 3, r.width, r.height}, fade(BLACK, a * 0.35f));
        rect(r, fade(k.line, a));
        gradV({r.x + u, r.y + u, r.width - 2 * u, r.height - 2 * u}, fade(k.bg, a), fade(k.bg2, a));
        rect({r.x + u, r.y + u, r.width - 2 * u, 1}, fade(k.lineHi, a));
        break;
    default:   // full 3D and the menus: a sheet of paper
        paper(r, k.bg, a, k.era == 6 ? 0.6f : 1.0f);
        break;
    }
}

void chip(Rectangle r, const Skin& k, Color c, float a)
{
    switch (k.era) {
    case 0: rect(r, fade(k.text, a)); break;
    case 1: rect(r, fade(hex(art::SHADES4[1]), a)); break;
    case 2: rect(r, fade(c, a)); break;
    case 3:
    case 4:
        rect(r, fade(hex(0x05061a), a * 0.85f));
        rect({r.x + 1.5f, r.y + 1.5f, r.width - 3, r.height - 3}, fade(ColorBrightness(c, -0.45f), a));
        rect({r.x + 1.5f, r.y + 1.5f, r.width - 3, 1.5f}, fade(ColorBrightness(c, -0.1f), a));
        break;
    case 5: rect(r, fade(ColorBrightness(c, -0.35f), a)); rect({r.x, r.y + r.height - 2, r.width, 2}, fade(c, a)); break;
    default: rect(r, fade(c, a * 0.18f)); rect({r.x, r.y + r.height - 2, r.width, 2}, fade(c, a * 0.8f)); break;
    }
}

// ------------------------------------------------------------------ glyphs
bool anyPad()
{
    for (int p = 0; p < 4; ++p) if (IsGamepadAvailable(p)) return true;
    return false;
}

namespace {
// arrows are drawn, not typed (not every font has them)
int arrowDir(const std::string& l)
{
    if (l == "\xE2\x86\x91") return 0;
    if (l == "\xE2\x86\x92") return 1;
    if (l == "\xE2\x86\x93") return 2;
    if (l == "\xE2\x86\x90") return 3;
    return -1;
}
TextStyle capStyle(float h)
{
    TextStyle st;
    st.size = h >= 28 ? std::max(type::pixel, h * 0.58f) : h * 0.62f;
    st.font = h >= 28 ? PIXEL : SERIF;
    st.bold = h < 28;
    st.align = CENTER;
    return st;
}
}  // namespace

float keycapW(const std::string& label, float h)
{
    if (arrowDir(label) >= 0) return h;
    return std::max(h, textW(label, capStyle(h)) + h * 0.6f);
}

// A paper-coloured key with an ink outline and a deeper bottom edge.
float keycap(const std::string& label, float x, float y, float h, float a, bool dark)
{
    float w = keycapW(label, h);
    float lip = std::max(3.0f, h * 0.12f);
    Color face = dark ? hex(0x3a302a) : hex(0xfbf6ea), ink = dark ? col::paper : col::paperInk;
    rect({x, y, w, h}, fade(col::paperInk, a));
    rect({x + 2, y + 2, w - 4, h - 4}, fade(dark ? hex(0x241d18) : col::paperDark, a));
    rect({x + 2, y + 2, w - 4, h - 2 - lip}, fade(face, a));
    float cx = x + w / 2, cy = y + (h - lip) / 2 + 1;
    int dir = arrowDir(label);
    if (dir >= 0) {
        float s = h * 0.22f;
        Color c = fade(ink, a);
        if (dir == 0) tri({cx, cy - s}, {cx - s, cy + s * 0.7f}, {cx + s, cy + s * 0.7f}, c);
        if (dir == 2) tri({cx, cy + s}, {cx - s, cy - s * 0.7f}, {cx + s, cy - s * 0.7f}, c);
        if (dir == 1) tri({cx + s, cy}, {cx - s * 0.7f, cy - s}, {cx - s * 0.7f, cy + s}, c);
        if (dir == 3) tri({cx - s, cy}, {cx + s * 0.7f, cy - s}, {cx + s * 0.7f, cy + s}, c);
        return w;
    }
    TextStyle st = capStyle(h);
    st.color = fade(ink, a);
    textMid(label, cx, cy, st);
    return w;
}

float padButton(int g, float x, float y, float h, float a)
{
    const float cx = x + h / 2, cy = y + h / 2, r = h / 2;
    auto circle = [&](Color c, const char* l) {
        DrawCircleV({X(cx), Y(cy)}, L(r), fade(hex(0x14121c), a));
        DrawCircleV({X(cx), Y(cy)}, L(r * 0.86f), fade(c, a));
        TextStyle st = capStyle(h);
        st.color = fade(WHITE, a);
        textMid(l, cx, cy, st);
        return h;
    };
    switch (g) {
    case PAD_A: return circle(hex(0x3aa856), "A");
    case PAD_B: return circle(hex(0xd8443c), "B");
    case PAD_X: return circle(hex(0x3c78d8), "X");
    case PAD_Y: return circle(hex(0xe0a822), "Y");
    case PAD_LB: case PAD_RB: {
        float w = h * 1.5f;
        rrect({x, y + h * 0.12f, w, h * 0.76f}, h * 0.3f, fade(hex(0x2a2838), a));
        TextStyle st; st.size = h * 0.5f; st.bold = true; st.align = CENTER; st.color = fade(col::cream, a);
        textMid(g == PAD_LB ? "LB" : "RB", x + w / 2, cy, st);
        return w;
    }
    case PAD_START: case PAD_SELECT: {
        float w = h * 1.2f;
        rrect({x, y + h * 0.18f, w, h * 0.64f}, h * 0.32f, fade(hex(0x2a2838), a));
        for (int i = 0; i < 3; ++i) {
            float ly = cy - h * 0.13f + i * h * 0.13f;
            if (g == PAD_START) rect({x + w * 0.3f, ly - h * 0.03f, w * 0.4f, h * 0.06f}, fade(col::cream, a));
            else rect({x + w * 0.32f + i * w * 0.12f, cy - h * 0.12f, w * 0.06f, h * 0.24f}, fade(col::cream, a));
        }
        return w;
    }
    case PAD_DPAD: {
        float t = h * 0.34f;
        rrect({x + (h - t) / 2, y, t, h}, t * 0.2f, fade(hex(0x2a2838), a));
        rrect({x, y + (h - t) / 2, h, t}, t * 0.2f, fade(hex(0x2a2838), a));
        DrawCircleV({X(cx), Y(cy)}, L(t * 0.22f), fade(hex(0x5a5868), a));
        return h;
    }
    default: {   // stick
        DrawCircleV({X(cx), Y(cy)}, L(r), fade(hex(0x14121c), a));
        DrawCircleV({X(cx), Y(cy)}, L(r * 0.78f), fade(hex(0x3a3848), a));
        DrawCircleV({X(cx), Y(cy - r * 0.08f)}, L(r * 0.5f), fade(hex(0x55536a), a));
        return h;
    }
    }
}

namespace {
float padW(int g, float h) { return g == PAD_LB || g == PAD_RB ? h * 1.5f : (g == PAD_START || g == PAD_SELECT) ? h * 1.2f : h; }
}

float promptW(const Prompt& p, float h)
{
    float w = 0, gap = h * 0.22f;
    bool pads = anyPad() && !p.pads.empty();
    if (pads) for (int g : p.pads) w += padW(g, h) + gap;
    else for (auto& k : p.keys) w += keycapW(k, h) + gap;
    if (!p.label.empty()) w += h * 0.3f + textW(p.label, h * 0.82f, SERIF);
    else if (w > 0) w -= gap;
    return w;
}

float prompt(const Prompt& p, float x, float y, float h, Color lc, float a)
{
    float x0 = x, gap = h * 0.22f;
    bool pads = anyPad() && !p.pads.empty();
    if (pads) for (int g : p.pads) x += padButton(g, x, y, h, a) + gap;
    else for (auto& k : p.keys) x += keycap(k, x, y, h, a) + gap;
    if (!p.label.empty()) {
        x += h * 0.18f;
        TextStyle st;
        st.size = h * 0.68f;
        st.color = fade(lc, a * lc.a / 255.0f);
        textMid(p.label, x, y + h * 0.46f, st);
        x += textW(p.label, st);
    }
    return x - x0;
}

void promptRow(const std::vector<Prompt>& ps, float x, float y, float h, Color lc, int align, float a)
{
    const float gap = h * 1.1f;
    float total = 0;
    for (auto& p : ps) total += promptW(p, h) + gap;
    total -= gap;
    float px = align == RIGHT ? x - total : align == CENTER ? x - total / 2 : x;
    for (auto& p : ps) px += prompt(p, px, y, h, lc, a) + gap;
}

// ------------------------------------------------------------------ menu pieces
void selectionBar(int key, float x, float targetY, float w, float h, Color c, float a)
{
    float y = anim(key, targetY, 22);
    rrect({x, y, w, h}, h * 0.5f, fade(c, a * 0.16f));
    rrect({x, y + h * 0.18f, 4, h * 0.64f}, 2, fade(c, a));
}

void arrows(float cx, float midY, float halfW, float s, Color c, float pulse)
{
    float o = pulse * 3;
    tri({cx - halfW - o, midY}, {cx - halfW + s * 0.6f - o, midY - s * 0.5f}, {cx - halfW + s * 0.6f - o, midY + s * 0.5f}, c);
    tri({cx + halfW + o, midY}, {cx + halfW - s * 0.6f + o, midY - s * 0.5f}, {cx + halfW - s * 0.6f + o, midY + s * 0.5f}, c);
}

void toggle(float x, float midY, bool on, Color onCol, Color offCol, float a)
{
    const float w = 44, h = 24;
    rrect({x, midY - h / 2, w, h}, h / 2, fade(on ? onCol : offCol, a));
    float k = on ? 1.0f : 0.0f;
    float kx = x + 3 + k * (w - h);
    DrawCircleV({X(kx + (h - 6) / 2), Y(midY)}, L((h - 6) / 2), fade(WHITE, a));
}

}  // namespace ui
