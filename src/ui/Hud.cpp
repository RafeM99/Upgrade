#include "ui/Hud.h"
#include "core/Config.h"
#include "core/Events.h"
#include "core/Game.h"
#include "core/Strings.h"
#include "player/Abilities.h"
#include "raylib.h"
#include "render/ArtSpec.h"
#include "ui/PixelFont.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// Every stage has its own HUD, drawn the way a game of that era drew it:
//   0 1-bit    one black strip at the top, 1 px white lines, big blocky text
//   1 4-shade  a handheld status bar in the four LCD greys
// Every HUD sits along the top of the screen (one place in every stage) and stays slim.
//   2 8-bit    chunky white-bordered boxes in the corners, a block meter
//   3 16-bit   separate bevelled windows (light top-left edge, dark bottom-right)
//   4 32-bit   one bevelled header bar across the top, split in three
//   5 2.5D     flat dark modern panels with a colour accent
//   6 3D       flat light modern cards
// All of it is flat colour. In the pixel stages every box and line is drawn on a whole
// pixel grid (U window pixels per HUD pixel) and the default font is drawn at whole
// multiples of its 10 px size, so text is never blurry.

namespace {

// ------------------------------------------------------------------ screen space
float S = 1;          // legacy UI scale (window height / 720), for the title, pause and ending screens
float OX = 0, OY = 0; // game image offset in the window
int X0 = 0, Y0 = 0, W = 1280, H = 720;   // the game image, in whole window pixels
int hudBottom = 720;  // where the HUD ends at the bottom (the 4-shade status bar), set each frame

Color hexc(unsigned h, unsigned char a = 255) { return Color{(unsigned char)(h >> 16), (unsigned char)(h >> 8), (unsigned char)h, a}; }
Color fade(Color c, float a) { c.a = (unsigned char)(c.a * std::max(0.0f, std::min(1.0f, a))); return c; }

const Color P_COL[2] = {{242, 138, 30, 255}, {60, 156, 240, 255}};

// The look of the current stage.
struct Theme {
    int era = 0;
    int U = 1;        // window pixels per HUD pixel
    int tb = 2;       // big text scale (window pixels per font pixel)
    int ts = 2;       // small text scale
    int bt = 4;       // banner text scale
    int tn = 2;       // player name scale (kept small so the player panels stay out of the way)
    Color fg, dim, bg, p[2], chipText;
    bool pixel = true;
};
Theme th;

void setup(Game& g)
{
    Rectangle r = g.renderer.screenRect();
    S = r.height / 720.0f;
    OX = r.x;
    OY = r.y;
    X0 = (int)std::lround(r.x);
    Y0 = (int)std::lround(r.y);
    W = (int)std::lround(r.width);
    H = (int)std::lround(r.height);

    const int e = std::max(0, std::min(art::LAST_ERA, g.era));
    th.era = e;
    th.pixel = art::lowRes(e);
    if (th.pixel) {
        const int k = std::max(1, (int)std::lround(r.height / art::BG_H[e]));   // window px per world pixel
        auto atLeast1 = [](int v) { return std::max(1, v); };
        switch (e) {
        case 0: th.U = atLeast1(k / 2); th.tb = atLeast1(k / 2); th.ts = atLeast1(k / 4); th.bt = atLeast1(k / 2); break;
        case 1: th.U = atLeast1(k / 2); th.tb = atLeast1(k / 2); th.ts = atLeast1(k / 2); th.bt = atLeast1(k); break;
        case 2: th.U = k; th.tb = atLeast1(k * 3 / 4); th.ts = atLeast1(k / 2); th.bt = k; break;
        case 3: th.U = k; th.tb = k; th.ts = atLeast1(k * 2 / 3); th.bt = atLeast1(k * 4 / 3); break;
        default: th.U = k; th.tb = atLeast1(k * 3 / 2); th.ts = k; th.bt = 2 * k; break;
        }
    } else {
        const int s = std::max(1, (int)std::lround(r.height / 720.0f));
        th.U = 2 * s;   // modern stages: 2 px lines
        th.tb = 3 * s;
        th.ts = 2 * s;
        th.bt = 4 * s;
    }
    // Player names use the small text size on every stage, so each player panel is just a line
    // of name and a line of chips.
    th.tn = th.ts;

    const unsigned* G = art::SHADES4;
    switch (e) {
    case 0:
        th.fg = hexc(0xebebe1); th.dim = th.fg; th.bg = hexc(0x101216);
        th.p[0] = th.p[1] = th.fg; th.chipText = th.bg;
        break;
    case 1:
        th.fg = hexc(G[0]); th.dim = hexc(G[0]); th.bg = hexc(G[3]);   // darkest grey: readable on the LCD
        th.p[0] = hexc(G[0]); th.p[1] = hexc(G[1]); th.chipText = hexc(G[3]);
        break;
    case 2:   // a handful of colours from an 8-bit console palette
        th.fg = hexc(0xfcfcfc); th.dim = hexc(0xdcdcdc); th.bg = hexc(0x000000);
        th.p[0] = hexc(0xfc9838); th.p[1] = hexc(0x3cbcfc); th.chipText = hexc(0x000000);
        break;
    case 3:
        th.fg = hexc(0xf8f8f8); th.dim = hexc(0xe0e4fc); th.bg = hexc(0x30389c);
        th.p[0] = hexc(0xf8a838); th.p[1] = hexc(0x68c0f8); th.chipText = hexc(0xf8f8f8);
        break;
    case 4:
        th.fg = hexc(0xf4ecff); th.dim = hexc(0xddd4f4); th.bg = hexc(0x2e2650);
        th.p[0] = hexc(0xff9a3c); th.p[1] = hexc(0x52b8ff); th.chipText = hexc(0xf4ecff);
        break;
    case 5:
        th.fg = hexc(0xf2f2f2); th.dim = hexc(0xd2d6de); th.bg = hexc(0x12161f);   // opaque: no scenery through it
        th.p[0] = P_COL[0]; th.p[1] = P_COL[1]; th.chipText = hexc(0xf2f2f2);
        break;
    default:
        th.fg = hexc(0x23262d); th.dim = hexc(0x454952); th.bg = hexc(0xf6f3ed);
        th.p[0] = hexc(0xd2680c); th.p[1] = hexc(0x2474c4); th.chipText = hexc(0x23262d);
        break;
    }
}

// ------------------------------------------------------------------ crisp primitives (window px relative to the game image)
void fill(int x, int y, int w, int h, Color c)
{
    if (w > 0 && h > 0) DrawRectangle(X0 + x, Y0 + y, w, h, c);
}

// The default font is 10 px; drawn at 10 * t with spacing t, every font pixel is t x t window pixels.
// HUD text uses the plain pixel font (ui/PixelFont.h): same 10 px metrics, easier to read small.
int textW(const std::string& s, int t) { return (int)std::lround(MeasureTextEx(ui::smallFont(), s.c_str(), 10.0f * t, (float)t).x); }
int capH(int t) { return 7 * t; }   // the visible height of a capital letter
void txt(const std::string& s, int x, int y, int t, Color c)
{
    DrawTextEx(ui::smallFont(), s.c_str(), Vector2{(float)(X0 + x), (float)(Y0 + y)}, 10.0f * t, (float)t, c);
}
int snap(int v) { return v / th.U * th.U; }

// Blinks on and off in the pixel stages; pulses in the modern ones.
float blink(Game& g, float speed)
{
    float s = std::sin(g.time * speed);
    return th.pixel ? (s > -0.3f ? 1.0f : 0.0f) : 0.6f + 0.4f * s;
}

// ------------------------------------------------------------------ the stage's box, meter and chip
void panel(int x, int y, int w, int h)
{
    const int U = th.U;
    switch (th.era) {
    case 0:   // black box, 1 px white line
        fill(x, y, w, h, th.fg);
        fill(x + U, y + U, w - 2 * U, h - 2 * U, th.bg);
        break;
    case 1:   // the LCD: lightest grey, darkest grey line
        fill(x, y, w, h, hexc(art::SHADES4[0]));
        fill(x + U, y + U, w - 2 * U, h - 2 * U, hexc(art::SHADES4[3]));
        break;
    case 2:   // chunky: black edge, white border with clipped corners, black inside
        fill(x, y, w, h, th.bg);
        fill(x + 2 * U, y + U, w - 4 * U, U, th.fg);
        fill(x + 2 * U, y + h - 2 * U, w - 4 * U, U, th.fg);
        fill(x + U, y + 2 * U, U, h - 4 * U, th.fg);
        fill(x + w - 2 * U, y + 2 * U, U, h - 4 * U, th.fg);
        break;
    case 3:   // bevelled window
        fill(x, y, w, h, hexc(0x080818));
        fill(x + U, y + U, w - 2 * U, h - 2 * U, hexc(0x9098e8));
        fill(x + 2 * U, y + 2 * U, w - 3 * U, h - 3 * U, hexc(0x141850));
        fill(x + 2 * U, y + 2 * U, w - 4 * U, h - 4 * U, th.bg);
        break;
    case 4:   // richer bevel: outline, light and dark edge, a second inner light line on top
        fill(x, y, w, h, hexc(0x0a0814));
        fill(x + U, y + U, w - 2 * U, h - 2 * U, hexc(0x8676c0));
        fill(x + 2 * U, y + 2 * U, w - 3 * U, h - 3 * U, hexc(0x15102a));
        fill(x + 2 * U, y + 2 * U, w - 4 * U, h - 4 * U, th.bg);
        fill(x + 2 * U, y + 2 * U, w - 4 * U, U, hexc(0x40366c));
        break;
    case 5:   // flat dark panel
        fill(x, y, w, h, th.bg);
        break;
    default:  // flat light card, hairline border
        fill(x, y, w, h, hexc(0xd8d2c6));
        fill(x + U, y + U, w - 2 * U, h - 2 * U, th.bg);
        break;
    }
}
int panelPad() { return th.era == 2 ? 3 * th.U : (th.era >= 5 ? 4 * th.U : (th.era >= 3 ? 3 * th.U : 3 * th.U + (th.era == 0 ? th.U : 0))); }

void meter(int x, int y, int w, int h, float k)
{
    const int U = th.U;
    k = std::max(0.0f, std::min(1.0f, k));
    switch (th.era) {
    case 0: {
        fill(x, y, w, h, th.fg);
        fill(x + U, y + U, w - 2 * U, h - 2 * U, th.bg);
        int segs = 20, ix = x + 2 * U, iw = w - 4 * U, sw = (iw + U) / segs;
        for (int i = 0; i < segs; ++i)
            if ((i + 0.5f) / segs <= k) fill(ix + i * sw, y + 2 * U, sw - U, h - 4 * U, th.fg);
        break;
    }
    case 1: {
        const unsigned* G = art::SHADES4;
        fill(x, y, w, h, hexc(G[0]));
        fill(x + U, y + U, w - 2 * U, h - 2 * U, hexc(G[3]));
        int segs = 25, ix = x + 2 * U, iw = w - 4 * U, sw = (iw + U) / segs;
        for (int i = 0; i < segs; ++i)
            fill(ix + i * sw, y + 2 * U, sw - U, h - 4 * U, hexc((i + 0.5f) / segs <= k ? G[0] : G[2]));
        break;
    }
    case 2: {   // a row of blocks, like hearts
        int segs = 16, sw = (w + U) / segs;
        for (int i = 0; i < segs; ++i) {
            bool on = (i + 0.5f) / segs <= k;
            int bx = x + i * sw;
            fill(bx, y, sw - U, h, hexc(on ? 0xf8b800 : 0x383838));
            fill(bx, y + h - U, sw - U, U, hexc(on ? 0xc84c0c : 0x1c1c1c));
        }
        break;
    }
    case 3: {   // sunken trough
        fill(x, y, w, h, hexc(0x9098e8));
        fill(x, y, w - U, h - U, hexc(0x0c0e34));
        fill(x + U, y + U, w - 2 * U, h - 2 * U, hexc(0x1c2060));
        int fw = (int)((w - 2 * U) * k);
        fill(x + U, y + U, fw, h - 2 * U, hexc(0xf06898));
        fill(x + U, y + h - 2 * U, fw, U, hexc(0xa83070));
        break;
    }
    case 4: {
        fill(x, y, w, h, hexc(0x0a0814));
        fill(x + U, y + U, w - 2 * U, h - 2 * U, hexc(0x15102a));
        int iw = w - 2 * U, fw = (int)(iw * k), ih = h - 2 * U;
        fill(x + U, y + U, fw, ih, hexc(0x46dcc8));
        fill(x + U, y + U + ih / 2, fw, ih - ih / 2, hexc(0x2196a8));
        for (int i = 1; i < 10; ++i) fill(x + U + iw * i / 10, y + U, U, ih, hexc(0x0a0814));
        break;
    }
    case 5:
        fill(x, y, w, h, hexc(0xffffff, 46));
        fill(x, y, (int)(w * k), h, hexc(0xffb050));
        break;
    default:
        fill(x, y, w, h, hexc(0xdcd6ca));
        fill(x, y, (int)(w * k), h, hexc(0xee8a2c));
        break;
    }
}

// A label for an ability or status. Returns its width.
int chipW(const std::string& s) { return textW(s, th.ts) + (th.era >= 5 ? 4 * th.U : (th.era == 2 ? 2 * th.U : 3 * th.U) + (th.era >= 3 ? th.U : 0)); }
int chipH() { return capH(th.ts) + (th.era >= 5 ? 3 * th.U : (th.era == 2 ? 2 * th.U : 3 * th.U) + (th.era >= 3 ? th.U : 0)); }
void chip(int x, int y, const std::string& s, Color col)
{
    const int U = th.U, w = chipW(s), h = chipH();
    int tx = x + (w - textW(s, th.ts)) / 2, ty = y + (h - capH(th.ts)) / 2;
    Color tc = th.chipText;
    switch (th.era) {
    case 0: fill(x, y, w, h, th.fg); tc = th.bg; break;
    case 1: fill(x, y, w, h, hexc(art::SHADES4[1])); tc = hexc(art::SHADES4[3]); break;
    case 2: fill(x, y, w, h, col); tc = th.chipText; break;
    case 3:
        fill(x, y, w, h, hexc(0x0c0e34));
        fill(x, y, w - U, h - U, col);
        fill(x + U, y + U, w - 2 * U, h - 2 * U, ColorBrightness(col, -0.45f));
        break;
    case 4:
        fill(x, y, w, h, hexc(0x0a0814));
        fill(x + U, y + U, w - 2 * U, h - 2 * U, ColorBrightness(col, -0.5f));
        fill(x + U, y + U, w - 2 * U, U, ColorBrightness(col, -0.15f));
        break;
    case 5: fill(x, y, w, h, ColorLerp(th.bg, col, 0.32f)); break;   // solid: the panel stays opaque
    default:
        fill(x, y, w, h, col);
        fill(x + U, y + U, w - 2 * U, h - 2 * U, hexc(0xf6f3ed));
        tc = col;
        break;
    }
    txt(s, tx, ty, th.ts, tc);
}

// ------------------------------------------------------------------ what a player panel shows
struct PView {
    std::string name;
    Color col;
    bool joined, faded;
    std::vector<std::string> abilities;
    std::vector<std::pair<std::string, Color>> status;
    float slowK = -1;   // time-slow recharge 0..1, or -1
};

PView view(Game& g, int p)
{
    const Player& pl = g.players[p];
    PView v;
    v.name = T(p == 0 ? "p1_name" : "p2_name");
    v.col = th.p[p];
    v.joined = pl.joined;
    v.faded = pl.faded;
    for (int a = 0; a < AB_COUNT; ++a)
        if (pl.has[a]) v.abilities.push_back(T(std::string("ability_short_") + abilityId(a)));
    // (power-ups are no longer listed here: each has an icon in the player's bottom corner)
    if (pl.has[AB_TIME_SLOW] && pl.slowCooldown > 0)
        v.slowK = 1 - pl.slowCooldown / config().f("abilities.time_slow_cooldown", 10.0f);
    return v;
}

// ------------------------------------------------------------------ word wrap
std::vector<std::string> wrap(const std::string& s, int t, int maxW)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {   // hard breaks first
        size_t nl = s.find('\n', start);
        std::string para = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string line, word;
        auto flushWord = [&]() {
            if (word.empty()) return;
            std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && textW(trial, t) > maxW) { out.push_back(line); line = word; }
            else line = trial;
            word.clear();
        };
        for (char ch : para) {
            if (ch == ' ') flushWord();
            else word += ch;
        }
        flushWord();
        out.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

// ------------------------------------------------------------------ a player's block
// Row 1: the name (big) and status words. Below it, one or more lines of ability chips or a
// message, wrapped to fit maxW.
struct Item { std::string s; bool isChip; };
using Line = std::vector<Item>;

int rowGap() { return th.pixel ? 2 * th.U : 3 * th.U; }
int chipGap() { return th.pixel ? 2 * th.U : 3 * th.U; }
int lineGap() { return th.pixel ? th.U : 2 * th.U; }
int itemW(const Item& it) { return it.isChip ? chipW(it.s) : textW(it.s, th.ts); }
int lineW(const Line& l)
{
    int w = 0;
    for (auto& it : l) w += itemW(it) + chipGap();
    return l.empty() ? 0 : w - chipGap();
}

std::vector<Line> detailLines(Game& g, const PView& v, int maxW)
{
    std::vector<Line> out;
    if (!v.joined) maxW = std::max(maxW, textW(T("p2_join_2"), th.ts));   // keep the join prompt on one line
    auto textLines = [&](const std::string& s) { for (auto& l : wrap(s, th.ts, maxW)) out.push_back({{l, false}}); };
    if (!v.joined) { textLines(T("p2_join_2")); return out; }
    if (v.faded) {
        std::string h = g.solo() ? T("faded_hint_solo") : T("faded_hint");
        Line first{{T("faded"), true}, {h, false}};
        if (lineW(first) <= maxW) out.push_back(first);
        else { out.push_back({{T("faded"), true}}); textLines(h); }
        return out;
    }
    if (v.abilities.empty()) { textLines(T("no_abilities")); return out; }
    Line cur;
    for (auto& a : v.abilities) {
        Line trial = cur;
        trial.push_back({a, true});
        if (!cur.empty() && lineW(trial) > maxW) { out.push_back(cur); cur = {{a, true}}; }
        else cur = trial;
    }
    out.push_back(cur);
    return out;
}

int row1W(const PView& v)
{
    if (!v.joined) return textW(T("p2_join_1"), th.tn);
    int w = textW(v.name, th.tn);
    for (auto& s : v.status) w += 3 * th.U + textW(s.first, th.ts) + 2 * th.ts;
    return w;
}
// A player's block is slim: the name, then the chips on the same row (more rows only when the
// chips do not fit). The name column is nameCol() wide; the chips start after it.
int nameGap() { return th.pixel ? 3 * th.U : 4 * th.U; }
int nameCol(const PView& v) { return row1W(v) + nameGap(); }
int playerRowsH(Game& g, const PView& v, int maxW)
{
    int n = (int)detailLines(g, v, maxW - nameCol(v)).size();
    return std::max(capH(th.tn), n * chipH() + (n - 1) * lineGap()) + (v.slowK >= 0 ? 2 * th.U : 0);
}
int playerRowsW(Game& g, const PView& v, int maxW)
{
    int w = 0;
    for (auto& l : detailLines(g, v, maxW - nameCol(v))) w = std::max(w, lineW(l));
    return nameCol(v) + w;
}

// `right` lines everything up against x instead of starting at x.
void playerRows(Game& g, const PView& v, int x, int y, bool right, int maxW)
{
    const int U = th.U;
    const int blockW = playerRowsW(g, v, maxW);
    const int x0 = right ? x - blockW : x;
    auto put = [&](int w) { (void)w; return x0; };
    const int ny = y + (chipH() - capH(th.tn)) / 2;   // the name sits level with the first chip row
    // the name
    if (!v.joined) {
        std::string a = T("p2_join_1");
        if (blink(g, 4) > 0) txt(a, put(textW(a, th.tn)), ny, th.tn, fade(th.fg, blink(g, 4)));
    } else {
        int rx = put(row1W(v));
        y = ny;
        txt(v.name, rx, y, th.tn, v.col);
        rx += textW(v.name, th.tn);
        for (auto& s : v.status) {   // a small square marker then the word, bottom aligned with the name
            rx += 3 * U;
            int sy = y + capH(th.tn) - capH(th.ts);
            fill(rx, sy + capH(th.ts) / 2 - th.ts, 2 * th.ts, 2 * th.ts, s.second);
            txt(s.first, rx + 3 * th.ts, sy, th.ts, s.second);
            rx += textW(s.first, th.ts) + 2 * th.ts;
        }
    }
    // the detail lines, beside the name
    int ly = ny - (chipH() - capH(th.tn)) / 2;
    for (auto& l : detailLines(g, v, maxW - nameCol(v))) {
        int lx = x0 + nameCol(v);
        for (auto& it : l) {
            if (it.isChip) {
                bool isFaded = v.faded && it.s == T("faded");
                if (!isFaded || blink(g, 6) > 0) chip(lx, ly, it.s, v.col);
            } else {
                Color c = (!v.joined || v.abilities.empty()) && !v.faded ? th.dim : th.fg;
                txt(it.s, lx, ly + (chipH() - capH(th.ts)) / 2, th.ts, c);
            }
            lx += itemW(it) + chipGap();
        }
        ly += chipH() + lineGap();
    }
    if (v.slowK >= 0) {   // time-slow recharging: a thin line under the chips
        int lw = std::min(maxW - nameCol(v), 60 * th.ts);
        int lx = x0 + nameCol(v);
        ly += U - lineGap();
        fill(lx, ly, lw, U, th.era == 0 ? th.bg : (th.era == 1 ? hexc(art::SHADES4[2]) : fade(v.col, 0.35f)));
        if (th.era == 0) { fill(lx, ly, lw, U, th.fg); fill(lx + U, ly, (int)((lw - 2 * U) * (1 - v.slowK)), U, th.bg); }
        else fill(lx, ly, (int)(lw * v.slowK), U, th.era == 1 ? hexc(art::SHADES4[0]) : (th.era == 6 ? v.col : hexc(0x7fe8ff)));
    }
}

std::string pctText(Game& g) { return std::to_string((int)std::floor(g.bar)) + "%"; }
std::string stageText(Game& g) { return T("era_name_" + std::to_string(g.era)); }
std::string scoreText(Game& g) { return std::string(T("score")) + " " + std::to_string(g.score); }

// ------------------------------------------------------------------ layouts
// Every stage's HUD is along the top and slim (about 8% of the screen height at most): each
// player is one row (name, then chips), and the centre is one row when it fits:
//   MEMORY [bar] 42%    8-BIT . 2006    SCORE 120
// Each layout returns where the free space under the HUD starts.

int centreSep() { return 14 * th.ts; }
int centreLg() { return 3 * th.ts; }
bool centreOneRow(Game& g, int w)
{
    int need = textW(T("bar_label"), th.ts) + 2 * centreLg() + W * 10 / 100 + textW("100%", th.ts)
             + 2 * centreSep() + textW(stageText(g), th.ts) + textW(scoreText(g), th.ts);
    return need <= w;
}
int centreRowGap() { return std::max(th.U, 2 * th.ts); }
int centreH(Game& g, int w, int barH)
{
    int rh = std::max(barH, capH(th.ts));
    return centreOneRow(g, w) ? rh : rh + centreRowGap() + capH(th.ts);
}
void centreBlock(Game& g, int x, int y, int w, int barH, Color pctCol)
{
    const int rh = std::max(barH, capH(th.ts)), ty = y + (rh - capH(th.ts)) / 2, lg = centreLg();
    std::string lab = T("bar_label"), pct = pctText(g), st = stageText(g), sc = scoreText(g);
    const int labW = textW(lab, th.ts), pctW = textW("100%", th.ts);
    txt(lab, x, ty, th.ts, th.fg);
    int right = x + w;   // where the bar's percentage ends
    if (centreOneRow(g, w)) {
        int scX = x + w - textW(sc, th.ts), stX = scX - centreSep() - textW(st, th.ts);
        txt(sc, scX, ty, th.ts, th.fg);
        txt(st, stX, ty, th.ts, th.dim);
        right = stX - centreSep();
    } else {
        int y2 = y + rh + centreRowGap();
        txt(st, x, y2, th.ts, th.dim);
        txt(sc, x + w - textW(sc, th.ts), y2, th.ts, th.fg);
    }
    txt(pct, right - textW(pct, th.ts), ty, th.ts, pctCol);
    const int barX = x + labW + lg;
    meter(barX, y + (rh - barH) / 2, right - pctW - lg - barX, barH, g.bar / 100.0f);
}

// 1-bit and 4-shade: one full-width strip along the top.
int stripLayout(Game& g, bool bottom)
{
    const int U = th.U, pad = 2 * U, side = 4 * U;
    PView a = view(g, 0), b = view(g, 1);
    std::string line2 = stageText(g) + "   " + scoreText(g);
    const int labW = textW(T("bar_label"), th.ts), pctW = textW("100%", th.ts);
    const int centreW = snap(std::max(textW(line2, th.ts), labW + pctW + 6 * U + W * 22 / 100));
    const int maxW = (W - centreW) / 2 - side - 6 * U;
    const int rowsH = std::max(playerRowsH(g, a, maxW), playerRowsH(g, b, maxW));
    const int bh = std::max(capH(th.ts), 5 * U);   // the meter draws a 2-pixel frame inside
    const int ch = centreH(g, centreW, bh);
    const int inner = std::max(rowsH, ch);
    const int h = U + pad + inner + pad + U;
    const int y = bottom ? H - h + U : -U;
    if (bottom) hudBottom = y;
    panel(-U, y, W + 2 * U, h);   // only the inner edge's line shows
    const int top = y + U + pad;
    playerRows(g, a, side, top + (inner - rowsH) / 2, false, maxW);
    playerRows(g, b, W - side, top + (inner - rowsH) / 2, true, maxW);
    centreBlock(g, snap((W - centreW) / 2), top + (inner - ch) / 2, centreW, bh, th.fg);
    return bottom ? 0 : y + h;
}

// 8-bit, 16-bit, 2.5D, 3D: a box for each player in the top corners, the Memory Bar box between them.
int cornerLayout(Game& g)
{
    const int U = th.U, pad = panelPad(), m = th.pixel ? (th.era == 2 ? U : 2 * U) : 6 * U;
    const int gap = th.pixel ? 2 * U : 6 * U;
    const int accent = th.era == 5 ? U : 0;
    PView v[2] = {view(g, 0), view(g, 1)};
    const int maxW = W * 30 / 100 - 2 * pad - accent;
    int pw[2], rowsH = 0;
    for (int p = 0; p < 2; ++p) {
        pw[p] = snap(std::max(W * 10 / 100, playerRowsW(g, v[p], maxW) + 2 * pad + accent) + U - 1);
        rowsH = std::max(rowsH, playerRowsH(g, v[p], maxW));
    }
    const int bx = m + pw[0] + gap, bw = W - m - pw[1] - gap - bx;
    const int barH = th.era == 2 ? 3 * U : (th.era == 3 ? 4 * U : (th.pixel ? capH(th.ts) : 4 * U));
    const int ch = centreH(g, bw - 2 * pad, barH);
    const int extra = (th.era == 2 ? U : 0) + (th.era == 6 ? U : 0);   // 8-bit: bottom border; 3D: top accent
    const int inner = std::max(rowsH, ch);
    const int ph = snap(2 * pad + inner + extra + U - 1);
    const int iy = m + pad + (th.era == 6 ? U : 0);
    for (int p = 0; p < 2; ++p) {
        int x = p == 0 ? m : W - m - pw[p];
        panel(x, m, pw[p], ph);
        if (th.era == 5) fill(x, m, 2 * U, ph, th.p[p]);   // modern colour accent
        if (th.era == 6) fill(x, m, pw[p], 2 * U, th.p[p]);
        playerRows(g, v[p], x + pad + accent, iy + (inner - rowsH) / 2, false, maxW);
    }
    panel(bx, m, bw, ph);
    centreBlock(g, bx + pad, iy + (inner - ch) / 2, bw - 2 * pad, barH, th.fg);
    return m + ph;
}

// 32-bit: one bevelled header bar across the top, split into three sections by grooves.
int headerLayout(Game& g)
{
    const int U = th.U, pad = 4 * U;
    PView a = view(g, 0), b = view(g, 1);
    const int maxW = W * 30 / 100 - 2 * pad;
    const int sw = snap(std::max(W * 16 / 100, std::max(playerRowsW(g, a, maxW), playerRowsW(g, b, maxW)) + 2 * pad));
    const int rowsH = std::max(playerRowsH(g, a, maxW), playerRowsH(g, b, maxW));
    const int cx = sw + 2 * U + pad, cw = W - 2 * sw - 2 * U - 2 * pad;
    const int barH = capH(th.ts) + 2 * U;
    const int ch = centreH(g, cw, barH);
    const int inner = std::max(rowsH, ch);
    const int h = snap(pad + inner + pad + 2 * U);
    const int y = 0;
    panel(-2 * U, y - 2 * U, W + 4 * U, h + 2 * U);
    for (int i = 0; i < 2; ++i) {   // grooves: dark line with a light line beside it
        int gx = i == 0 ? sw : W - sw;
        fill(gx, y + 2 * U, U, h - 5 * U, hexc(0x15102a));
        fill(gx + U, y + 2 * U, U, h - 5 * U, hexc(0x8676c0));
    }
    playerRows(g, a, pad, y + pad + (inner - rowsH) / 2, false, maxW);
    playerRows(g, b, W - sw + pad, y + pad + (inner - rowsH) / 2, false, maxW);
    centreBlock(g, cx, y + pad + (inner - ch) / 2, cw, barH, hexc(0x46dcc8));
    return y + h;
}

// ------------------------------------------------------------------ the one-time hint, as Gran speaking
bool hintDone = false;
bool gotFragment = false;
bool listening = false;
constexpr float kHintSeconds = 8.0f;

void hintBox(Game& g, int top)
{
    const int U = th.U, pad = panelPad() + U;
    const int t = th.ts;
    const int w = snap(std::min(W * 62 / 100, std::max(W / 2, 64 * t * 6)));
    const int x = snap((W - w) / 2);
    std::string who = "Gran:";
    std::string msg = T("controls_hint");
    int lh = capH(t) + 4 * t;
    std::vector<std::string> lines = wrap(msg, t, w - 2 * pad);
    const int h = snap(pad + capH(th.tb) + 3 * U + (int)lines.size() * lh - 2 * t + pad + U);
    const int y = snap(top + (th.pixel ? std::max(3 * U, 8) : 6 * U));
    panel(x, y, w, h);
    if (th.era == 5) fill(x, y, 2 * U, h, th.p[0]);
    if (th.era == 6) fill(x, y, w, 2 * U, th.p[0]);
    txt(who, x + pad, y + pad, th.tb, th.p[0]);
    // the words type in, like an old text box
    int shown = (int)(g.time * 40.0f);
    int used = 0, ly = y + pad + capH(th.tb) + 3 * U + U;
    for (auto& l : lines) {
        if (used >= shown) break;
        std::string part = l.substr(0, (size_t)std::max(0, std::min((int)l.size(), shown - used)));
        txt(part, x + pad, ly, t, th.fg);
        used += (int)l.size() + 1;
        ly += lh;
    }
    // a "more" marker in the corner once the text is out, blinking
    if (used <= shown && blink(g, 5) > 0 && th.era <= 4) {
        int mx = x + w - pad - 3 * U, my = y + h - pad - 2 * U;
        fill(mx, my, 3 * U, U, th.era == 1 ? th.fg : th.p[0]);
        fill(mx + U, my + U, U, U, th.era == 1 ? th.fg : th.p[0]);
    }
}

int bossBar(Game& g, int top)
{
    const int U = th.U, pad = panelPad();
    const int w = snap(W * 36 / 100), x = snap((W - w) / 2), y = snap(top + 3 * U);
    const int barH = th.pixel ? std::max(2 * U, capH(th.ts)) : 8 * U;
    const int h = snap(pad + capH(th.ts) + 3 * U + barH + pad);
    panel(x, y, w, h);
    std::string n = T("boss_name_" + std::to_string(g.era));   // each memory has its own boss
    Color nc = th.era <= 1 ? th.fg : (th.era == 6 ? hexc(0x8a3ab0) : hexc(0xe0b0ff));
    txt(n, x + (w - textW(n, th.ts)) / 2, y + pad, th.ts, nc);
    float k = g.boss.hp / (float)std::max(1, g.boss.maxHp);
    int bx = x + pad, bw = w - 2 * pad, by = y + pad + capH(th.ts) + 3 * U;
    Color fc = th.era == 0 ? th.fg : (th.era == 1 ? hexc(art::SHADES4[0]) : hexc(0xb050d0));
    Color tc = th.era == 0 ? th.bg : (th.era == 1 ? hexc(art::SHADES4[2]) : (th.era == 6 ? hexc(0xdcd6ca) : hexc(0x000000, 200)));
    fill(bx, by, bw, barH, tc);
    fill(bx, by, (int)(bw * k), barH, fc);
    if (th.era == 0) { fill(bx, by, bw, U, th.fg); fill(bx, by + barH - U, bw, U, th.fg); fill(bx, by, U, barH, th.fg); fill(bx + bw - U, by, U, barH, th.fg); }
    if (g.boss.state == 3 && blink(g, 8) > 0) {
        std::string hnt = T("boss_hint");
        txt(hnt, (W - textW(hnt, th.ts)) / 2, y + h + 2 * U, th.ts, fade(th.era == 6 ? th.fg : (th.era == 1 ? th.fg : WHITE), blink(g, 8)));
    }
    // the boss's time power: its warning, then its name
    if (g.powers.labelT > 0 && !g.powers.label.empty()) {
        const std::string& pl = g.powers.label;
        int py = y + h + 2 * U + capH(th.ts) + 3 * U;
        Color pc = th.era <= 1 ? th.fg : (th.era == 6 ? hexc(0x8a3ab0) : hexc(0xe0b0ff));
        txt(pl, (W - textW(pl, th.ts)) / 2, py, th.ts, pc);
    }
    return y + h + 2 * U + 2 * (capH(th.ts) + 3 * U);   // under the hint and power lines
}

// A short message (stage name, memory slips, player 2 joins): a slim notice just under the top
// HUD, never a big box across the middle of the screen.
void banner(Game& g, int top)
{
    float a = std::min(1.0f, std::min(g.bannerT * 3, (g.bannerLen - g.bannerT) * 6 + 0.2f));
    if (th.pixel) a = 1;   // pixel stages: it just appears and goes
    const int U = th.U, pad = panelPad();
    const int bt = th.ts, st = th.ts;
    std::string sub = g.bannerSub;
    for (char& c : sub) if (c == '\n') c = ' ';
    std::vector<std::string> lines = wrap(sub, st, W * 60 / 100);
    int w = textW(g.banner, bt);
    for (auto& l : lines) w = std::max(w, textW(l, st));
    w = snap(std::min(W - 8 * U, w + 2 * pad));
    int lh = capH(st) + 3 * U;
    int h = snap(pad + capH(bt) + (lines.empty() || sub.empty() ? 0 : 3 * U + (int)lines.size() * lh - 3 * U) + pad + U);
    int x = snap((W - w) / 2), y = snap(top + 3 * U);
    if (a < 1) fill(x, y, w, h, fade(th.bg, a));
    else panel(x, y, w, h);
    Color tc = th.era == 0 ? th.fg : (th.era == 1 ? hexc(art::SHADES4[0]) : (th.era == 6 ? th.fg : hexc(0xfff4d8)));
    txt(g.banner, (W - textW(g.banner, bt)) / 2, y + pad, bt, fade(tc, a));
    if (sub.empty()) return;
    int ly = y + pad + capH(bt) + 3 * U;
    for (auto& l : lines) { txt(l, (W - textW(l, st)) / 2, ly, st, fade(th.era <= 1 ? th.fg : th.dim, a)); ly += lh; }
}

// ------------------------------------------------------------------ power-ups in the bottom corners
// Each active power-up shows as its own icon (the same picture as in the world, in this stage's
// style) in its player's bottom corner: Gran's on the left, Sam's on the right. A thin bar under a
// timed one shows how long it has left. When one is picked up, a short note pops up above it.
struct PuIcon { int type; float left; };   // left: 0..1 of its time, or -1 for "until used"

std::vector<PuIcon> activePowerUps(Game& g, int p)
{
    std::vector<PuIcon> out;
    const Player& pl = g.players[p];
    if (!pl.joined) return out;
    if (pl.shield) out.push_back({PU_SHIELD, -1});
    if (pl.speedTime > 0) out.push_back({PU_SPEED, pl.speedTime / std::max(0.1f, config().f("powerups.speed_seconds", 8.0f))});
    if (pl.magnetTime > 0) out.push_back({PU_MAGNET, pl.magnetTime / std::max(0.1f, config().f("powerups.magnet_seconds", 10.0f))});
    if (g.wallSlowTime > 0 && (g.hourglassBy == p || g.solo()))
        out.push_back({PU_SLOW, g.wallSlowTime / std::max(0.1f, config().f("powerups.hourglass_seconds", 6.0f))});
    return out;
}

void powerUpIcon(Game& g, int type, int x, int y, int s)
{
    const Texture2D& tex = g.art.era[th.era].tiles;
    Rectangle src = frameRect(tex, art::T_PU_SHIELD + type, 1, 1, th.era);
    DrawTexturePro(tex, src, Rectangle{(float)(X0 + x), (float)(Y0 + y), (float)s, (float)s}, {0, 0}, 0, WHITE);
}

void powerUpCorners(Game& g)
{
    const int U = th.U, pad = th.pixel ? 2 * U : 3 * U;
    // icon size: about 44 px at 720p, a whole multiple of the stage's tile pixels on pixel stages
    const float want = 44.0f * H / 720.0f;
    const int tp = art::TILE_PX[th.era];
    const int s = th.pixel ? std::max(1, (int)std::lround(want / tp)) * tp : (int)std::lround(want);
    const int box = s + 2 * pad, m = th.pixel ? 2 * U : 6 * U;
    const int base = hudBottom - m - box - 3 * U;   // leave room for the timer bar
    for (int p = 0; p < 2; ++p) {
        auto icons = activePowerUps(g, p);
        const bool right = p == 1;
        for (size_t i = 0; i < icons.size(); ++i) {
            int x = right ? W - m - box - (int)i * (box + 2 * U) : m + (int)i * (box + 2 * U);
            x = snap(x);
            panel(x, base, box, box);
            powerUpIcon(g, icons[i].type, x + pad, base + pad, s);
            if (icons[i].left >= 0) {   // time left
                Color bc = th.era == 0 ? th.fg : (th.era == 1 ? hexc(art::SHADES4[0]) : P_COL[p]);
                fill(x, base + box + U, box, U, fade(th.era <= 1 ? th.dim : hexc(0x000000), 0.5f));
                fill(x, base + box + U, (int)(box * std::min(1.0f, icons[i].left)), U, bc);
            }
        }
        // the pop-up: what it is and what it does, above this player's corner
        if (g.puPopT[p] > 0 && g.players[p].joined) {
            float a = th.pixel ? 1.0f : std::min(1.0f, g.puPopT[p] * 3);
            int type = g.puPopType[p];
            std::string name = T("powerup_" + std::to_string(type)), desc = T("powerup_desc_" + std::to_string(type));
            const int t = th.ts, ip = th.pixel ? std::max(1, (int)std::lround(want * 0.7f / tp)) * tp : (int)std::lround(want * 0.7f);
            std::vector<std::string> dl = wrap(desc, t, W * 30 / 100);
            int tw = textW(name, t);
            for (auto& l : dl) tw = std::max(tw, textW(l, t));
            const int lh = capH(t) + 3 * U;
            const int w = snap(pad + ip + 3 * U + tw + pad + U);
            const int h = snap(pad + std::max(ip, capH(t) + 3 * U + (int)dl.size() * lh - 3 * U) + pad + U);
            int x = snap(right ? W - m - w : m);
            int y = snap(base - 3 * U - h);
            if (a < 1) fill(x, y, w, h, fade(th.bg, a)); else panel(x, y, w, h);
            powerUpIcon(g, type, x + pad, y + pad, ip);
            int tx = x + pad + ip + 3 * U;
            Color nc = th.era <= 1 ? th.fg : P_COL[p];
            txt(name, tx, y + pad, t, fade(nc, a));
            int ly = y + pad + capH(t) + 3 * U;
            for (auto& l : dl) { txt(l, tx, ly, t, fade(th.era <= 1 ? th.fg : (th.era == 6 ? th.fg : th.dim), a)); ly += lh; }
        }
    }
}

// ------------------------------------------------------------------ legacy text for the title, pause and ending screens
// Snapped to whole multiples of the font's 10 px and whole pixels, with a hard 1-font-pixel shadow.
// display: raylib's default font (titles); otherwise the plain pixel font.
void text(const std::string& s, float x, float y, float size, Color c, bool centre = false, bool shadow = true, bool display = false)
{
    const Font& f = display ? GetFontDefault() : ui::smallFont();
    int t = std::max(1, (int)std::lround(size * S / 10.0f));
    Vector2 m = MeasureTextEx(f, s.c_str(), 10.0f * t, (float)t);
    float px = std::round(OX + x * S - (centre ? std::round(m.x / 2) : 0)), py = std::round(OY + y * S);
    if (shadow) DrawTextEx(f, s.c_str(), {px + t, py + t}, 10.0f * t, (float)t, Color{0, 0, 0, c.a});
    DrawTextEx(f, s.c_str(), {px, py}, 10.0f * t, (float)t, c);
}

Rectangle R(float x, float y, float w, float h) { return Rectangle{OX + x * S, OY + y * S, w * S, h * S}; }

}  // namespace

void Hud::drawPlaying(Game& g)
{
    setup(g);
    if (!listening) {
        events().fragmentCollected.listen([](int, float) { gotFragment = true; });
        listening = true;
    }

    int top = 0;   // where the free space under the HUD starts
    hudBottom = H;
    if (th.era == 0) top = stripLayout(g, false);
    else if (th.era == 1) top = stripLayout(g, false);
    else if (th.era == 4) top = headerLayout(g);
    else top = cornerLayout(g);

    int below = top;   // messages go under the HUD (and under the boss bar in a boss fight)
    if (g.boss.active && g.boss.state != 5) below = bossBar(g, top);

    // The old "Gran speaking" hint box is no longer drawn: the story has its own UI (ui/Story.*)
    // and the controls hint rides on the level banner at the start of a run.
    (void)hintBox;

    powerUpCorners(g);
    if (g.bannerT > 0) banner(g, below);
}

void Hud::drawTitle(Game& g)
{
    setup(g);
    int e = ((int)(g.time / 3.0f)) % art::ERA_COUNT;
    const unsigned cols[art::ERA_COUNT] = {0xebebe1, art::SHADES4[3], 0xf7d73a, 0xffb0d0, 0x7ef0c0, 0xfff0d0, 0xffffff};
    Color title = hexc(cols[e]);
    float bob = std::round(std::sin(g.time * 2) * 6);
    // the title "upgrades" every few seconds, like the game does
    float size = 100 + e * 10;
    text(T("title"), 640, 128 + bob, size, title, true, true, true);

    // Everything else sits on one solid panel, so the scenery never runs behind the words.
    {
        const int u = std::max(1, (int)std::lround(2 * S));   // border width, whole pixels
        const int px = (int)std::lround(OX + 96 * S), py = (int)std::lround(OY + 268 * S);
        const int pw = (int)std::lround(1088 * S), ph = (int)std::lround(424 * S);
        DrawRectangle(px, py, pw, ph, hexc(0x0b0c10));
        DrawRectangle(px, py, pw, u, hexc(0x4a4e5c));
        DrawRectangle(px, py + ph - u, pw, u, hexc(0x4a4e5c));
        DrawRectangle(px, py, u, ph, hexc(0x4a4e5c));
        DrawRectangle(px + pw - u, py, u, ph, hexc(0x4a4e5c));
        DrawRectangle(px + 3 * u, py + 3 * u + (int)std::lround(130 * S), pw - 6 * u, u, hexc(0x262833));   // divider
        DrawRectangle(px + 3 * u, py + 3 * u + (int)std::lround(206 * S), pw - 6 * u, u, hexc(0x262833));
    }
    text(T("title_sub"), 640, 290, 20, hexc(0xf2efe6), true, false);
    // steady text that blinks off briefly, rather than a dim pulse
    if (std::fmod(g.time, 1.2f) < 0.9f) text(T("title_start"), 640, 326, 30, WHITE, true, false);
    std::string p2 = g.players[1].joined ? T("title_p2_ready") : T("title_join");
    text(p2, 640, 372, 20, g.players[1].joined ? hexc(0x8cccff) : hexc(0xd6d3cc), true, false);
    // difficulty: how strong the bosses' time powers are
    std::string diff = std::string(T("difficulty_label")) + "   < " + T("difficulty_" + std::to_string(g.difficulty)) + " >";
    text(diff, 640, 418, 20, hexc(0xffd890), true, false);
    text(T("difficulty_desc_" + std::to_string(g.difficulty)), 640, 448, 20, hexc(0xd6d3cc), true, false);
    // controls: light tints of the player colours, bright enough to read on the dark panel
    text(T("title_controls_p1"), 640, 498, 20, hexc(0xffc98f), true, false);
    text(T("title_controls_p2"), 640, 528, 20, hexc(0xa6d6ff), true, false);
    {   // the gamepad line can hold a "\n"
        std::string pad = T("title_controls_pad");
        size_t nl = pad.find('\n');
        text(pad.substr(0, nl), 640, 558, 20, hexc(0xe8e6e0), true, false);
        if (nl != std::string::npos) text(pad.substr(nl + 1), 640, 588, 20, hexc(0xe8e6e0), true, false);
    }
    // which controllers the game can see, so a player can tell at once if theirs works
    {
        auto pads = g.input.pads();
        std::string line;
        Color c = hexc(0xb8b6b0);
        if (pads.empty()) line = T("pads_none");
        for (auto& pd : pads) {
            std::string one = pd.name + ": ";
            if (!pd.usable) { one += T("pads_unknown"); c = hexc(0xffb070); }
            else if (pd.player >= 0) { one += T(pd.player == 0 ? "p1_name" : "p2_name"); c = hexc(0x9fe8a8); }
            else one += T("pads_spare");
            line += (line.empty() ? "" : "   ") + one;
        }
        text(line, 640, 648, 20, c, true, false);
    }
}

void Hud::drawPause(Game& g)
{
    setup(g);
    DrawRectangleRec(R(0, 0, 1280, 720), fade(BLACK, 0.6f));
    text(T("pause"), 640, 220, 60, WHITE, true, true, true);
    const char* items[3] = {"resume", "restart", "quit"};
    for (int i = 0; i < 3; ++i) {
        bool sel = g.pauseItem == i;
        std::string s = std::string(sel ? "> " : "  ") + T(items[i]) + (sel ? " <" : "  ");
        text(s, 640, 330 + i * 50, 30, sel ? hexc(0xf7d73a) : fade(WHITE, 0.8f), true);
    }
    text(T("pause_hint"), 640, 520, 20, fade(WHITE, 0.6f), true);
}

void Hud::drawEnding(Game& g)
{
    setup(g);
    float t = g.endingT;
    const float slide = config().f("ending.slide_seconds", 5.0f);
    const int n = art::ENDING_COUNT;
    ClearBackground(BLACK);
    if (t < n * slide) {
        int i = (int)(t / slide);
        float lt = t - i * slide;
        float a = std::min(1.0f, std::min(lt / 0.8f, (slide - lt) / 0.8f));
        float zoom = 1.0f + lt / slide * 0.08f;
        Texture2D tex = g.art.ending[i];
        Rectangle scr = g.renderer.screenRect();
        float w = scr.width * zoom, h = scr.height * zoom;
        DrawTexturePro(tex, Rectangle{0, 0, (float)tex.width, (float)tex.height},
                       Rectangle{scr.x - (w - scr.width) / 2, scr.y - (h - scr.height) / 2, w, h}, {0, 0}, 0, fade(WHITE, a));
        DrawRectangleRec(R(0, 600, 1280, 120), fade(BLACK, 0.5f * a));
        text(T("ending_caption_" + std::to_string(i)), 640, 625, 30, fade(WHITE, a), true);
        text(T("ending_sub_" + std::to_string(i)), 640, 668, 20, fade(hexc(0xffe0b0), a), true);
        return;
    }
    float rt = t - n * slide;
    // the reveal
    if (rt < 12) {
        for (int i = 0; i < 4; ++i) {
            float a = std::min(1.0f, std::max(0.0f, (rt - i * 2.2f) / 1.0f));
            text(T("ending_reveal_" + std::to_string(i + 1)), 640, 220 + i * 70, i == 3 ? 40 : 30,
                 fade(i == 3 ? hexc(0xffd890) : WHITE, a), true);
        }
        return;
    }
    // credits roll
    float ct = rt - 12;
    std::string credits = T("credits");
    float y = std::round(720 - ct * 50);
    text(T("credits_title"), 640, y, 40, hexc(0xffd890), true, true, true);
    y += 80;
    size_t start = 0;
    while (start <= credits.size()) {
        size_t nl = credits.find('\n', start);
        std::string line = credits.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        text(line, 640, y, 20, WHITE, true);
        y += 40;
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    if (y < 360) text(T("thanks"), 640, 360, 30, hexc(0xfff0d0), true);
    if (ct > 3) text(T("ending_continue"), 640, 660, 20, fade(WHITE, 0.5f + 0.5f * std::sin(g.time * 3)), true);
}
