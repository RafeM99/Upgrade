#include "ui/Hud.h"
#include "core/Config.h"
#include "core/Events.h"
#include "core/Game.h"
#include "core/Settings.h"
#include "core/Strings.h"
#include "player/Abilities.h"
#include "raylib.h"
#include "render/ArtSpec.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// The HUD and the menus (akshat, UI pass). Everything is laid out on the 1280 x 720 page of
// ui/Theme and drawn through its fonts, colours and panels.
//
// In play the layout is the same on every stage - Gran's card top left, Sam's top right, the
// Memory Bar between them, the boss bar under it, power-ups in the bottom corners - and each
// graphics stage skins it its own way (ui::skin): an ink-and-cream 1-bit box, the handheld's
// four greys, a white-bordered 8-bit box, the blue 16-bit RPG window, a glossy 32-bit bevel,
// a solid warm plate in 2.5D and paper cards in full 3D. The HUD is set in Silkscreen.
//
// The title, pause, settings and credits screens share one look (ui::menuSkin) on every stage.

using namespace ui;

namespace {

// ------------------------------------------------------------------ shared bits
const Skin* K = nullptr;    // the current stage's skin
float W = 1280, H = 720;    // page size (always 1280 x 720)

void gameFrame(Game& g) { setFrame(g.renderer.screenRect()); }
void menuFrame() { setFrame(menuRect()); }

TextStyle ts(float size, Color c, int align = LEFT, bool bold = false, float track = 0)
{
    TextStyle st;
    st.size = size;
    st.color = c;
    st.align = align;
    st.bold = bold;   // Silkscreen Bold
    (void)track;      // Silkscreen is already spaced for reading
    st.font = K ? K->font : BODY;
    return st;
}
TextStyle body(float size, Color c, int align = LEFT, bool bold = false, float track = 0)
{
    TextStyle st;
    st.size = size;
    st.color = c;
    st.align = align;
    st.bold = bold;
    st.track = track;
    return st;
}
TextStyle serif(float size, Color c, int align = LEFT, bool italic = false)
{
    TextStyle st;
    st.size = size;
    st.color = c;
    st.align = align;
    st.font = italic ? SERIF_ITALIC : SERIF;
    return st;
}
TextStyle pix(float size, Color c, int align = LEFT, float track = 0.04f)
{
    TextStyle st;
    st.size = size;
    st.color = c;
    st.align = align;
    st.font = PIXEL;
    st.track = track;
    return st;
}

float pulse(float speed, float lo = 0.55f) { return lo + (1 - lo) * (0.5f + 0.5f * std::sin(now() * speed)); }
float blinkOn(float speed) { return std::sin(now() * speed) > -0.3f ? 1.0f : 0.0f; }

// ------------------------------------------------------------------ ability icons
// A tiny line drawing for each ability, in an s x s box.
void line(float x0, float y0, float x1, float y1, float t, Color c) { DrawLineEx({X(x0), Y(y0)}, {X(x1), Y(y1)}, std::max(1.0f, L(t)), c); }

void abilityIcon(int a, float x, float y, float s, Color c)
{
    const float t = std::max(1.4f, s * 0.13f);
    const float m = s * 0.5f;
    switch (a) {
    case AB_DOUBLE_JUMP:
        for (int i = 0; i < 2; ++i) {
            float cy = y + s * (0.42f + i * 0.34f);
            line(x + s * 0.18f, cy, x + m, cy - s * 0.28f, t, c);
            line(x + m, cy - s * 0.28f, x + s * 0.82f, cy, t, c);
        }
        break;
    case AB_DASH:
        line(x + s * 0.3f, y + m, x + s * 0.9f, y + m, t, c);
        line(x + s * 0.62f, y + s * 0.24f, x + s * 0.9f, y + m, t, c);
        line(x + s * 0.62f, y + s * 0.76f, x + s * 0.9f, y + m, t, c);
        line(x + s * 0.05f, y + s * 0.3f, x + s * 0.32f, y + s * 0.3f, t * 0.8f, fade(c, 0.6f));
        line(x + s * 0.05f, y + s * 0.7f, x + s * 0.32f, y + s * 0.7f, t * 0.8f, fade(c, 0.6f));
        break;
    case AB_GROUND_POUND:
        line(x + m, y + s * 0.05f, x + m, y + s * 0.62f, t, c);
        line(x + s * 0.26f, y + s * 0.4f, x + m, y + s * 0.66f, t, c);
        line(x + s * 0.74f, y + s * 0.4f, x + m, y + s * 0.66f, t, c);
        line(x + s * 0.08f, y + s * 0.9f, x + s * 0.92f, y + s * 0.9f, t, c);
        break;
    case AB_WALL_JUMP:
        line(x + s * 0.14f, y + s * 0.05f, x + s * 0.14f, y + s * 0.95f, t, c);
        line(x + s * 0.3f, y + s * 0.85f, x + s * 0.86f, y + s * 0.2f, t, c);
        line(x + s * 0.52f, y + s * 0.2f, x + s * 0.86f, y + s * 0.2f, t, c);
        line(x + s * 0.86f, y + s * 0.2f, x + s * 0.86f, y + s * 0.54f, t, c);
        break;
    case AB_GLIDE:
        DrawRing({X(x + m), Y(y + s * 0.56f)}, L(s * 0.42f - t / 2), L(s * 0.42f + t / 2), 180, 360, 16, c);
        line(x + s * 0.08f, y + s * 0.56f, x + s * 0.92f, y + s * 0.56f, t, c);
        line(x + m, y + s * 0.56f, x + m, y + s * 0.95f, t, c);
        break;
    case AB_TIME_SLOW:
        DrawRing({X(x + m), Y(y + m)}, L(s * 0.44f - t / 2), L(s * 0.44f + t / 2), 0, 360, 24, c);
        line(x + m, y + m, x + m, y + s * 0.24f, t, c);
        line(x + m, y + m, x + s * 0.72f, y + s * 0.62f, t, c);
        break;
    }
}

// ------------------------------------------------------------------ player cards
constexpr float M = 16;           // screen margin
constexpr float CARD_H = 70;
constexpr float CHIP_H = 26;
constexpr float PX = type::pixel;       // Silkscreen, one step: every HUD label
constexpr float PX2 = 2 * type::pixel;  // two steps: banners

struct Ab { int id; std::string label; float k; };   // k: recharge 0..1 (1 = ready)

std::vector<Ab> abilitiesOf(Game& g, int p)
{
    std::vector<Ab> v;
    const Player& pl = g.players[p];
    for (int a = 0; a < AB_COUNT; ++a) {
        if (!pl.has[a]) continue;
        float k = 1;
        if (a == AB_TIME_SLOW && pl.slowCooldown > 0)
            k = 1 - pl.slowCooldown / std::max(0.1f, config().f("abilities.time_slow_cooldown", 10.0f));
        v.push_back({a, T(std::string("ability_short_") + abilityId(a)), clamp01(k)});
    }
    return v;
}

float chipLabelSize() { return PX; }
float abilityChipW(const Ab& a, bool iconOnly) { return iconOnly ? CHIP_H + 2 : CHIP_H - 6 + 6 + textW(a.label, ts(chipLabelSize(), WHITE, LEFT, true)) + 10; }

void abilityChip(const Ab& a, float x, float y, bool iconOnly, Color pc)
{
    float w = abilityChipW(a, iconOnly);
    Color ink = K->era == 0 ? K->bg : (K->era == 1 ? hex(art::SHADES4[3]) : (K->era == 2 ? hex(0x000000) : (K->era == 6 ? pc : K->text)));
    if (K->era == 0) ink = hex(0x0e0f13);
    chip({x, y, w, CHIP_H}, *K, pc);
    if (a.k < 1) {   // recharging: the chip fills up from the left
        Color dim = K->era <= 2 ? fade(BLACK, 0.55f) : fade(BLACK, 0.35f);
        if (K->radius > 0) rrect({x + w * a.k, y, w * (1 - a.k), CHIP_H}, 0, dim);
        else rect({x + w * a.k, y, w * (1 - a.k), CHIP_H}, dim);
    }
    float is = CHIP_H - 8;
    abilityIcon(a.id, x + 5, y + 4, is, fade(ink, a.k < 1 ? 0.6f : 1));
    if (!iconOnly) textMid(a.label, x + 5 + is + 5, y + CHIP_H / 2, ts(chipLabelSize(), fade(ink, a.k < 1 ? 0.7f : 1), LEFT, true));
}

float playerCard(Game& g, int p, float alphaBase)
{
    const Player& pl = g.players[p];
    const bool right = p == 1;
    const float pad = 10;
    Color pc = K->p[p];
    std::string name = T(p == 0 ? "p1_name" : "p2_name");
    TextStyle nameSt = ts(PX, pc, LEFT, true, 0.06f);
    if (!pl.joined) {
        // a quiet invitation instead of a card
        std::string a = name, b = T("p2_join_2");
        TextStyle sa = ts(PX, fade(K->text, 0.9f), LEFT, true, 0.06f), sb = ts(PX, K->dim);
        float w = pad * 2 + textW(a, sa) + 10 + textW(b, sb) + 4;
        float h = 34, x = W - M - w, y = M;
        float al = alphaBase * (0.75f + 0.25f * pulse(2.2f, 0));
        panel({x, y, w, h}, *K, al * 0.85f);
        text(a, x + pad, y + h / 2 - sa.size * 0.48f, sa);
        textMid(b, x + pad + textW(a, sa) + 10, y + h / 2, sb);
        return h;
    }
    auto abs = abilitiesOf(g, p);
    bool iconOnly = abs.size() > 3;
    float rowW = 0;
    for (auto& a : abs) rowW += abilityChipW(a, iconOnly) + 5;
    if (!abs.empty()) rowW -= 5;
    std::string sub;
    if (pl.faded) sub = g.solo() ? T("faded_hint_solo") : T("faded_hint");
    else if (abs.empty()) sub = T("no_abilities");
    TextStyle subSt = ts(PX, K->dim);
    float contentW = std::max(textW(name, nameSt) + 34, pl.faded ? 64 + textW(sub, subSt) : (abs.empty() ? textW(sub, subSt) : rowW));
    float w = std::max(150.0f, contentW + 2 * pad + (K->era >= 5 ? 6 : 0));
    float x = right ? W - M - w : M, y = M;
    panel({x, y, w, CARD_H}, *K, alphaBase);
    float ix = x + pad;
    if (K->era == 5) { rrect({x + 7, y + 12, 3, CARD_H - 24}, 1.5f, fade(pc, alphaBase)); ix += 6; }
    if (K->era == 6) { rrect({x + 7, y + 12, 3, CARD_H - 24}, 1.5f, fade(pc, alphaBase)); ix += 6; }
    // row 1: the name, and P1 / P2 in the far corner
    textMid(name, ix, y + 21, nameSt);
    std::string tag = p == 0 ? "P1" : "P2";
    TextStyle tagSt = ts(PX, fade(K->dim, 0.8f), RIGHT, true, 0.08f);
    textMid(tag, x + w - pad, y + 21, tagSt);
    // row 2: abilities, or what is going on
    float ry = y + CARD_H - CHIP_H - 10;
    if (pl.faded) {
        float a = 0.6f + 0.4f * pulse(7, 0);
        Ab f{-1, T("faded"), 1};
        float fw = textW(f.label, ts(chipLabelSize(), WHITE, LEFT, true)) + 14;
        chip({ix, ry, fw, CHIP_H}, *K, K->danger, a);
        textMid(f.label, ix + fw / 2, ry + CHIP_H / 2, ts(chipLabelSize(), K->era <= 1 ? hex(0x0e0f13) : (K->era == 6 ? K->danger : WHITE), CENTER, true));
        textMid(sub, ix + fw + 8, ry + CHIP_H / 2, subSt);
    } else if (abs.empty()) {
        textMid(sub, ix, ry + CHIP_H / 2, subSt);
    } else {
        float cx = ix;
        for (auto& a : abs) { abilityChip(a, cx, ry, iconOnly, pc); cx += abilityChipW(a, iconOnly) + 5; }
    }
    return CARD_H;
}

// ------------------------------------------------------------------ the Memory Bar
float shownBar = 0, barFlash = 0, lastBar = 0;
float shownScore = 0;

void meter(float x, float y, float w, float h, float k, float flash)
{
    k = clamp01(k);
    const float u = 2;
    switch (K->era) {
    case 0: case 1: {   // blocks on the LCD / 1-bit strip
        rect({x, y, w, h}, K->era == 0 ? K->text : K->line);
        rect({x + u, y + u, w - 2 * u, h - 2 * u}, K->era == 0 ? hex(0x0e0f13) : K->bg);
        int segs = 20;
        float iw = w - 4 * u, sw = (iw + u) / segs;
        for (int i = 0; i < segs; ++i) {
            bool on = (i + 0.5f) / segs <= k;
            if (on) rect({x + 2 * u + i * sw, y + 2 * u, sw - u, h - 4 * u}, K->accent);
            else if (K->era == 1) rect({x + 2 * u + i * sw, y + 2 * u, sw - u, h - 4 * u}, K->trough);
        }
        break;
    }
    case 2: {   // a row of console-coloured blocks
        int segs = 20;
        float sw = (w + u) / segs;
        for (int i = 0; i < segs; ++i) {
            bool on = (i + 0.5f) / segs <= k;
            float bx = x + i * sw;
            rect({bx, y, sw - u, h}, on ? K->accent : K->trough);
            rect({bx, y + h - u, sw - u, u}, on ? K->accent2 : hex(0x1c1c1c));
        }
        break;
    }
    case 3: case 4: {   // sunken trough, gradient fill, tick marks
        rect({x, y, w, h}, hex(0x05061a));
        rect({x + u, y + u, w - 2 * u, h - 2 * u}, K->trough);
        float fw = (w - 2 * u) * k;
        gradV({x + u, y + u, fw, h - 2 * u}, K->accent, K->accent2);
        rect({x + u, y + u, fw, u}, fade(WHITE, 0.45f));
        for (int i = 1; i < 10; ++i) rect({x + u + (w - 2 * u) * i / 10.0f, y + u, u * 0.75f, h - 2 * u}, fade(hex(0x05061a), 0.6f));
        break;
    }
    default: {   // smooth stages: a rounded track with a warm gradient and a soft glow
        float r = h / 2;
        rrect({x, y, w, h}, r, K->trough);
        float fw = std::max(h, w * k);
        if (k > 0.001f) {
            rrect({x, y, fw, h}, r, K->accent2);
            if (fw > 2 * r) gradH({x + r, y, fw - 2 * r, h}, K->accent2, K->accent);
            DrawCircleV({X(x + fw - r), Y(y + r)}, L(r), K->accent);
            rrect({x + 3, y + 1.5f, fw - 6, h * 0.32f}, h * 0.16f, fade(WHITE, 0.3f));
        }
        for (int i = 1; i < 4; ++i) rect({x + w * i / 4.0f - 0.75f, y + 2, 1.5f, h - 4}, fade(K->era == 6 ? WHITE : BLACK, 0.25f));
        break;
    }
    }
    if (flash > 0) {
        float fx = x + w * k;
        if (K->radius > 0) rrect({x, y, w * k, h}, h / 2, fade(WHITE, 0.35f * flash));
        else rect({x, y, w * k, h}, fade(WHITE, 0.35f * flash));
        (void)fx;
    }
}

void memoryPanel(Game& g)
{
    const float dt = std::min(0.1f, GetFrameTime());
    if (g.bar > lastBar + 0.01f) barFlash = 1;
    if (g.bar < lastBar - 5) shownBar = g.bar;   // memory lost / new stage: no long slide down
    lastBar = g.bar;
    shownBar = approach(shownBar, g.bar, 10, dt);
    barFlash = std::max(0.0f, barFlash - dt * 3);
    shownScore = std::fabs(shownScore - g.score) < 0.5f ? (float)g.score : approach(shownScore, (float)g.score, 12, dt);
    if (g.score < shownScore - 1000) shownScore = (float)g.score;

    const float w = 500, x = (W - w) / 2, y = M, h = CARD_H, pad = 12;
    panel({x, y, w, h}, *K);
    // row 1: the stage name, the score
    std::string stage = upper(T("era_name_" + std::to_string(g.era)));
    for (size_t i = 0; (i = stage.find(" . ", i)) != std::string::npos;) stage.replace(i, 3, "  \xC2\xB7  ");
    textMid(stage, x + pad, y + 21, ts(PX, K->dim, LEFT, true, 0.08f));
    std::string sc = std::to_string((int)std::lround(shownScore));
    TextStyle scSt = ts(PX, K->text, RIGHT, true, 0.02f);
    textMid(sc, x + w - pad, y + 21, scSt);
    textMid(T("score"), x + w - pad - textW(sc, scSt) - 8, y + 21, ts(PX, K->dim, RIGHT, true, 0.1f));
    // row 2: MEMORY [=====     ] 42%
    float rowY = y + h - 22;
    TextStyle lab = ts(PX, K->text, LEFT, true, 0.1f);
    std::string label = T("bar_label");
    float labW = textW(label, lab);
    textMid(label, x + pad, rowY, lab);
    std::string pct = std::to_string((int)std::floor(g.bar)) + "%";
    TextStyle pctSt = ts(PX, K->era >= 2 && K->era != 6 ? K->accent : K->text, RIGHT, true);
    float pctW = textW("100%", pctSt);
    textMid(pct, x + w - pad, rowY, pctSt);
    float bx = x + pad + labW + 10, bw = w - 2 * pad - labW - pctW - 20;
    float bh = 12;
    meter(bx, rowY - bh / 2, bw, bh, shownBar / 100.0f, barFlash);
}

// ------------------------------------------------------------------ boss bar
float bossShown = 1, bossGhost = 1, bossHitFlash = 0;
int bossLastHp = -1;

float bossBar(Game& g, float top)
{
    const Boss& b = g.boss;
    const float dt = std::min(0.1f, GetFrameTime());
    const int maxHp = std::max(1, b.maxHp);
    float k = clamp01(b.hp / (float)maxHp);
    if (bossLastHp < 0 || b.hp > bossLastHp) { bossShown = bossGhost = k; }
    else if (b.hp < bossLastHp) bossHitFlash = 1;
    bossLastHp = b.hp;
    bossShown = approach(bossShown, k, 18, dt);
    if (bossHitFlash <= 0.3f) bossGhost = approach(bossGhost, k, 4, dt);
    bossHitFlash = std::max(0.0f, bossHitFlash - dt * 2.5f);

    const float w = 600, x = (W - w) / 2, y = top + 8, h = 62, pad = 12;
    panel({x, y, w, h}, *K);
    std::string name = upper(T("boss_name_" + std::to_string(b.kind)));
    Color bc = K->era <= 1 ? K->text : K->boss;
    TextStyle nameSt = ts(PX, K->era == 6 ? bc : (K->era <= 1 ? K->text : mix(bc, WHITE, 0.45f)), LEFT, true, 0.1f);
    textMid(name, x + pad, y + 19, nameSt);
    if (b.phase >= 2) {   // down to half its health: it gets angrier
        std::string ph = upper(T("ui_phase2"));
        TextStyle pst = ts(PX, K->era <= 1 ? (K->era == 0 ? hex(0x0e0f13) : hex(art::SHADES4[3])) : (K->era == 6 ? bc : WHITE), CENTER, true, 0.1f);
        float pw = textW(ph, pst) + 12, px = x + pad + textW(name, nameSt) + 10;
        chip({px, y + 7, pw, 24}, *K, K->era <= 1 ? K->text : bc);
        textMid(ph, px + pw / 2, y + 19, pst);
    }
    // what it is doing: open to a stomp, or just its hit count
    bool open = b.vulnerable() || b.state == 3;
    if (open) {
        std::string hint = upper(T("ui_stunned"));   // the full hint goes under the bar
        TextStyle hs = ts(PX, K->era <= 1 ? K->bg : WHITE, RIGHT, true, 0.06f);
        if (K->era <= 1) hs.color = K->era == 0 ? hex(0x0e0f13) : hex(art::SHADES4[3]);
        if (K->era == 6) hs.color = K->accent2;
        float hw = textW(hint, hs) + 16;
        float a = 0.65f + 0.35f * pulse(9, 0);
        chip({x + w - pad - hw, y + 6, hw, 26}, *K, K->era <= 1 ? K->text : hex(0x3ec46d), a);
        textMid(hint, x + w - pad - 8, y + 19, hs);
    } else {
        std::string hp = std::to_string(std::max(0, b.hp)) + " / " + std::to_string(maxHp);
        textMid(hp, x + w - pad, y + 19, ts(PX, K->dim, RIGHT, true));
    }
    // the HP track: one segment per hit
    float tx = x + pad, tw = w - 2 * pad, ty = y + h - 20, th = 10;
    const float gap = 3;
    float segW = (tw - gap * (maxHp - 1)) / maxHp;
    for (int i = 0; i < maxHp; ++i) {
        float sx = tx + i * (segW + gap);
        float s0 = i / (float)maxHp, s1 = (i + 1) / (float)maxHp;
        auto part = [&](float upto) { return clamp01((upto - s0) / (s1 - s0)); };
        float r = K->radius > 0 ? th / 2 : 0;
        Color trough = K->era <= 1 ? (K->era == 0 ? hex(0x2a2b30) : K->trough) : (K->era == 6 ? fade(BLACK, 0.12f) : fade(BLACK, 0.45f));
        rrect({sx, ty, segW, th}, r, trough);
        float gk = part(bossGhost), fk = part(bossShown);
        if (gk > fk) rrect({sx, ty, segW * gk, th}, r, fade(WHITE, 0.8f));
        if (fk > 0) {
            Color fc = K->era <= 1 ? K->text : bc;
            if (open) fc = mix(fc, WHITE, 0.25f * pulse(9, 0));
            rrect({sx, ty, segW * fk, th}, r, fc);
            if (!K->pixel || K->era >= 3) rrect({sx + 1, ty + 1, std::max(0.0f, segW * fk - 2), th * 0.35f}, r * 0.35f, fade(WHITE, 0.3f));
        }
    }
    // stunned: how long the weak point stays open
    if (b.state == 3 && b.stunLen > 0) {
        float sk = clamp01(b.stunT / b.stunLen);
        Color sc = K->era <= 1 ? K->text : hex(0x3ec46d);
        rrect({tx, y + h - 6, tw * sk, 2}, K->radius > 0 ? 1 : 0, sc);
    }
    float bottom = y + h;
    if (open) {   // what to do now, in a strip under the bar
        std::string hint = upper(T("boss_hint"));
        TextStyle hs = ts(PX, K->era <= 1 ? K->text : (K->era == 6 ? K->text : hex(0xd8ffd8)), CENTER, true);
        float hw = std::min(W - 80, textW(hint, hs) + 36), hy = bottom + 8;
        float a = 0.75f + 0.25f * pulse(9, 0);
        panel({(W - hw) / 2, hy, hw, 36}, *K);
        hs.color = fade(hs.color, a);
        textMid(hint, W / 2, hy + 18, hs);
        bottom = hy + 36;
    }
    // the boss's time power: its warning, then its name
    if (g.powers.labelT > 0 && !g.powers.label.empty()) {
        float a = std::min(1.0f, g.powers.labelT * 4);
        TextStyle ps = ts(PX, K->era <= 1 ? K->text : (K->era == 6 ? K->boss : hex(0xffe2ec)), CENTER, true, 0.02f);
        float pw = textW(g.powers.label, ps) + 32;
        float py = bottom + 8;
        panel({(W - pw) / 2, py, pw, 38}, *K, a);
        if (K->era >= 2) rect({(W - pw) / 2 + 4, py + 32, pw - 8, 2}, fade(K->boss, a));
        textMid(g.powers.label, W / 2, py + 18, [&] { TextStyle t = ps; t.color = fade(ps.color, a); return t; }());
        bottom = py + 38;
    }
    return bottom;
}

// ------------------------------------------------------------------ banner (a toast under the HUD)
void banner(Game& g, float top)
{
    float in = g.bannerLen - g.bannerT;
    float a = std::min(clamp01(in / 0.25f), clamp01(g.bannerT / 0.35f));
    float slide = (1 - easeOutCubic(clamp01(in / 0.35f))) * -14;
    std::string sub = g.bannerSub, title = g.banner;
    for (char& c : sub) if (c == '\n') c = ' ';
    for (size_t i = 0; (i = title.find(" . ", i)) != std::string::npos;) title.replace(i, 3, "  \xC2\xB7  ");
    TextStyle tSt = ts(PX2, K->era <= 1 ? K->text : (K->era == 6 ? K->text : hex(0xfff4d8)), CENTER, true, 0.04f);
    TextStyle sSt = serif(26, K->text, CENTER);
    auto lines = sub.empty() ? std::vector<std::string>{} : wrap(sub, sSt.size, W * 0.5f, sSt.font);
    float w = textW(title, tSt);
    for (auto& l : lines) w = std::max(w, textW(l, sSt));
    w = std::min(W - 80, w + 56);
    float lh = sSt.size * 1.35f;
    float h = 20 + tSt.size + (lines.empty() ? 0 : 6 + lines.size() * lh) + 14;
    float x = (W - w) / 2, y = top + 12 + slide;
    panel({x, y, w, h}, *K, a);
    if (K->era == 2) rect({W / 2 - 18, y + 4, 36, 2}, fade(K->accent, a));
    tSt.color = fade(tSt.color, a);
    text(title, W / 2, y + 16, tSt);
    float ly = y + 16 + tSt.size + 6;
    sSt.color = fade(sSt.color, a);
    for (auto& l : lines) { text(l, W / 2, ly, sSt); ly += lh; }
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

void powerUpIcon(Game& g, int type, float x, float y, float s, float a = 1)
{
    const int e = std::max(0, std::min(art::LAST_ERA, g.era));
    const Texture2D& tex = g.art.era[e].tiles;
    if (!tex.id) return;
    Rectangle src = frameRect(tex, art::T_PU_SHIELD + type, 1, 1, e);
    // pixel stages: a whole number of window pixels per art pixel
    float px = L(s);
    if (art::lowRes(e)) px = std::max(src.width, std::floor(px / src.width) * src.width);
    Rectangle dst{X(x) + (L(s) - px) / 2, Y(y) + (L(s) - px) / 2, px, px};
    dst.x = std::round(dst.x); dst.y = std::round(dst.y);
    DrawTexturePro(tex, src, dst, {0, 0}, 0, fade(WHITE, a));
}

void powerUpCorners(Game& g)
{
    const float box = 52, pad = 6, gap = 8, m = M;
    const float base = H - m - box - 6;
    for (int p = 0; p < 2; ++p) {
        auto icons = activePowerUps(g, p);
        const bool right = p == 1;
        for (size_t i = 0; i < icons.size(); ++i) {
            float x = right ? W - m - box - i * (box + gap) : m + i * (box + gap);
            panel({x, base, box, box}, *K);
            powerUpIcon(g, icons[i].type, x + pad, base + pad, box - 2 * pad);
            if (icons[i].left >= 0) {   // time left
                Color bc = K->era <= 1 ? K->text : K->p[p];
                float r = K->radius > 0 ? 2 : 0;
                rrect({x + 4, base + box + 3, box - 8, 4}, r, K->era <= 1 ? K->trough : fade(BLACK, 0.45f));
                rrect({x + 4, base + box + 3, (box - 8) * std::min(1.0f, icons[i].left), 4}, r, bc);
            }
        }
        // the pop-up: what it is and what it does, above this player's corner
        if (g.puPopT[p] > 0 && g.players[p].joined) {
            float a = std::min(1.0f, g.puPopT[p] * 3);
            float rise = (1 - a) * 10;
            int type = g.puPopType[p];
            std::string name = upper(T("powerup_" + std::to_string(type))), desc = T("powerup_desc_" + std::to_string(type));
            TextStyle nSt = ts(PX, K->era <= 1 ? K->text : K->p[p], LEFT, true, 0.06f);
            TextStyle dSt = serif(24, K->text);
            auto dl = wrap(desc, dSt.size, 300, dSt.font);
            float tw = textW(name, nSt);
            for (auto& l : dl) tw = std::max(tw, textW(l, dSt));
            const float ip = 36, lh = dSt.size * 1.3f;
            float w = 12 + ip + 10 + tw + 14;
            float h = std::max(ip + 20, 12 + nSt.size + 4 + dl.size() * lh + 10);
            float x = right ? W - m - w : m;
            float y = base - 12 - h + rise;
            panel({x, y, w, h}, *K, a);
            powerUpIcon(g, type, x + 12, y + (h - ip) / 2, ip, a);
            float tx = x + 12 + ip + 10;
            nSt.color = fade(nSt.color, a);
            text(name, tx, y + 10, nSt);
            float ly = y + 10 + nSt.size + 3;
            dSt.color = fade(dSt.color, a);
            for (auto& l : dl) { text(l, tx, ly, dSt); ly += lh; }
        }
    }
}

// ------------------------------------------------------------------ menus: shared pieces
// The menus look like Edie's scrapbook: cream sheets with grain, tape and a shadow, ink-dark
// Lora type, handwritten Caveat notes in brick red, a yellow highlighter on the chosen row.
void dimBackdrop(float a)
{
    // the whole window, letterbox included
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), fade(hex(0x120d0a), a));
}

std::vector<Prompt> navPrompts(bool change, bool back)
{
    std::vector<Prompt> ps;
    ps.push_back({{"\xE2\x86\x91", "\xE2\x86\x93"}, {PAD_DPAD}, T("ui_navigate")});
    if (change) ps.push_back({{"\xE2\x86\x90", "\xE2\x86\x92"}, {PAD_DPAD}, T("ui_change")});
    ps.push_back({{"Enter"}, {PAD_A}, T("ui_select")});
    if (back) ps.push_back({{"Esc"}, {PAD_B}, T("ui_back")});
    return ps;
}

TextStyle hand(float size, Color c, int align = LEFT)
{
    TextStyle st;
    st.size = size;
    st.color = c;
    st.align = align;
    st.font = HAND;
    return st;
}

// A dark strip along the bottom of the screen for the button prompts, so they read over anything.
void promptStrip(const std::vector<Prompt>& ps, float a, bool left = false, Game* g = nullptr);

// The chosen row: a hand-drawn arrow in the margin.
void pointer(float x, float midY, float a, Color c)
{
    float k = std::sin(now() * 5) * 2.5f;
    inkLine(x - 30 + k, midY + 1, x - 8 + k, midY, 3, fade(c, a));
    inkLine(x - 17 + k, midY - 7, x - 8 + k, midY, 3, fade(c, a));
    inkLine(x - 17 + k, midY + 7, x - 8 + k, midY, 3, fade(c, a));
}

// < value >, as two small ink chevrons
void handArrows(float cx, float midY, float halfW, float a, Color c)
{
    float k = std::sin(now() * 6) * 2;
    inkLine(cx - halfW - k, midY, cx - halfW + 8 - k, midY - 8, 2.5f, fade(c, a));
    inkLine(cx - halfW - k, midY, cx - halfW + 8 - k, midY + 8, 2.5f, fade(c, a));
    inkLine(cx + halfW + k, midY, cx + halfW - 8 + k, midY - 8, 2.5f, fade(c, a));
    inkLine(cx + halfW + k, midY, cx + halfW - 8 + k, midY + 8, 2.5f, fade(c, a));
}

// ------------------------------------------------------------------ the logo
// "UPGRADE" upgrades with the backdrop every three seconds: 1-bit cream, the handheld's greys,
// an 8-bit drop shadow, a 16-bit two-tone, a 32-bit chrome glow, a soft 2.5D shadow and a full
// 3D extrusion. A quick glitch covers each switch.
void twoTone(const std::string& s, float cx, float y, TextStyle st, Color top, Color bottom, float split)
{
    st.color = bottom;
    text(s, cx, y, st);
    Rectangle clip = R(0, y - 50, 1280, 50 + st.size * split);
    BeginScissorMode((int)clip.x, (int)clip.y, (int)clip.width, (int)clip.height);
    TextStyle t2 = st;
    t2.shadow = BLANK;
    t2.outline = BLANK;
    t2.color = top;
    text(s, cx, y, t2);
    EndScissorMode();
}

void logo(int e, float cx, float y, float size, float a, float t)
{
    const std::string s = T("title");
    TextStyle st;
    st.size = size;
    st.align = CENTER;
    st.track = 0.04f;
    st.font = art::lowRes(e) ? PIXEL : BODY;
    st.bold = !art::lowRes(e);
    // glitch on the switch
    float since = std::fmod(t, 3.0f);
    float gl = since < 0.22f ? 1 - since / 0.22f : 0;
    if (gl > 0) {
        float o = gl * 10;
        TextStyle r = st, b = st;
        r.color = fade(hex(0xff3060), 0.6f * a * gl);
        b.color = fade(hex(0x30d0ff), 0.6f * a * gl);
        text(s, cx - o, y, r);
        text(s, cx + o, y, b);
    }
    switch (e) {
    case 0:
        st.color = fade(hex(0xebebe1), a);
        st.shadow = fade(hex(0x000000), a);
        st.shadowOff = size * 0.06f;
        text(s, cx, y, st);
        break;
    case 1: {
        const unsigned* G = art::SHADES4;
        st.color = fade(hex(G[3]), a);
        st.outline = fade(hex(G[0]), a);
        st.outlineW = size * 0.035f;
        st.shadow = fade(hex(G[1]), a);
        st.shadowOff = size * 0.08f;
        text(s, cx, y, st);
        break;
    }
    case 2:
        st.outline = fade(BLACK, a);
        st.outlineW = size * 0.04f;
        st.shadow = fade(hex(0xd83c00), a);
        st.shadowOff = size * 0.09f;
        st.color = fade(hex(0xfcfcfc), a);
        text(s, cx, y, st);
        break;
    case 3:
        st.outline = fade(hex(0x1a1450), a);
        st.outlineW = size * 0.04f;
        st.shadow = fade(hex(0x0a0820), 0.8f * a);
        st.shadowOff = size * 0.08f;
        twoTone(s, cx, y, st, fade(hex(0xffd0e4), a), fade(hex(0xe85898), a), 0.52f);
        break;
    case 4: {
        TextStyle glow = st;   // a soft teal glow
        for (int i = 3; i >= 1; --i) {
            glow.color = fade(hex(0x5ef0dc), 0.10f * a);
            glow.outline = fade(hex(0x5ef0dc), 0.10f * a);
            glow.outlineW = size * 0.03f * i;
            text(s, cx, y, glow);
        }
        st.outline = fade(hex(0x150f2c), a);
        st.outlineW = size * 0.03f;
        twoTone(s, cx, y, st, fade(hex(0xffffff), a), fade(hex(0x7fd8ff), a), 0.5f);
        break;
    }
    case 5: {
        TextStyle sh = st;
        for (int i = 1; i <= 5; ++i) {
            sh.color = fade(BLACK, 0.08f * a);
            text(s, cx + i * 0.8f, y + i * 1.6f, sh);
        }
        st.color = fade(hex(0xfff3dc), a);
        text(s, cx, y, st);
        break;
    }
    default: {   // full 3D: an extruded block
        TextStyle ex = st;
        const int n = 12;
        for (int i = n; i >= 1; --i) {
            ex.color = fade(mix(hex(0x6a2c10), hex(0xd2680c), 1 - i / (float)n), a);
            text(s, cx + i * 0.55f, y + i * 0.7f, ex);
        }
        twoTone(s, cx, y, st, fade(hex(0xffffff), a), fade(hex(0xffe0b0), a), 0.55f);
        break;
    }
    }
}

// ------------------------------------------------------------------ title: the controls card
// An index card taped to the page: who plays with which keys (or pad buttons).
void controlsCard(Game& g, float x, float y, float w, float h, float a)
{
    const float pad = 26;
    paper({x, y, w, h}, col::paper, a);
    tape(x + w / 2, y + 2, 110, -3, a);
    text(T("ui_controls"), x + pad, y + 16, hand(42, fade(col::brick, a)));
    float cy = y + 70;
    const float kh = 32;
    auto row = [&](const std::vector<Prompt>& ps, float yy, float al) {
        float px = x + pad;
        for (auto& p : ps) px += prompt(p, px, yy, kh, fade(col::paperInk, al), al) + 22;
    };
    const bool pads = anyPad();
    for (int p = 0; p < 2; ++p) {
        bool joined = p == 0 || g.players[1].joined;
        float al = joined ? a : a * 0.8f;
        Color pc = p == 0 ? col::granInk : col::samInk;
        std::string nm = T(p == 0 ? "p1_name" : "p2_name");
        TextStyle ns = serif(28, fade(pc, a));
        ns.bold = true;
        text(nm, x + pad, cy, ns);
        float nx = x + pad + textW(nm, ns) + 12;
        text(T(p == 0 ? "ui_player1" : "ui_player2"), nx, cy + 3, serif(24, fade(col::paperInk2, a), LEFT, true));
        if (p == 1) {   // joined, or how to join
            std::string st = joined ? T("ui_ready") : T("ui_press_to_join");
            TextStyle hs = hand(34, fade(joined ? hex(0x2e7d3a) : col::brick, joined ? a : a * pulse(3, 0.6f)), RIGHT);
            text(st, x + w - pad, cy - 4, hs);
        }
        cy += 40;
        if (pads) {
            row({{{}, {PAD_STICK}, T("ctl_move")}, {{}, {PAD_A}, T("ctl_jump")}, {{}, {PAD_X}, T("ctl_attack")}, {{}, {PAD_B}, T("ctl_ability")}}, cy, al);
            cy += 54;
        } else if (p == 0) {
            row({{{"A", "D"}, {}, T("ctl_move")}, {{"Space"}, {}, T("ctl_jump")}, {{"F"}, {}, T("ctl_attack")}}, cy, al);
            row({{{"G"}, {}, T("ctl_ability")}, {{"S", "G"}, {}, T("ctl_ability2")}}, cy + 40, al);
            cy += 88;
        } else {
            row({{{"\xE2\x86\x90", "\xE2\x86\x92"}, {}, T("ctl_move")}, {{"R-Ctrl"}, {}, T("ctl_jump")}}, cy, al);
            row({{{"R-Shift"}, {}, T("ctl_attack")}, {{"/"}, {}, T("ctl_ability")}}, cy + 40, al);
            cy += 88;
        }
    }
}

// Which controllers the game can see, so a player can tell at once if theirs works.
std::string padLine(Game& g, Color* c)
{
    auto pads = g.input.pads();
    std::string line;
    *c = col::cream;
    if (pads.empty()) line = T("pads_none");
    for (auto& pd : pads) {
        std::string one = pd.name + ": ";
        if (!pd.usable) { one += T("pads_unknown"); *c = hex(0xffc890); }
        else if (pd.player >= 0) { one += T(pd.player == 0 ? "p1_name" : "p2_name"); *c = hex(0xb8f0c0); }
        else one += T("pads_spare");
        line += (line.empty() ? "" : "   ") + one;
    }
    return line;
}

void promptStrip(const std::vector<Prompt>& ps, float a, bool left, Game* g)
{
    const float h = 58, y = H - h;
    rect({-400, y, W + 800, h}, fade(hex(0x14100d), 0.9f * a));
    rect({-400, y, W + 800, 2}, fade(col::paperDark, 0.5f * a));
    promptRow(ps, W - 36, y + (h - 32) / 2, 32, fade(col::cream, a), RIGHT, a);
    if (left && g) {
        Color c;
        std::string line = padLine(*g, &c);
        padButton(PAD_DPAD, 36, y + (h - 24) / 2, 24, a);
        auto lines = wrap(line, 21, 720);
        if (!lines.empty()) textMid(lines[0], 70, y + h / 2, serif(21, fade(c, a)));
    }
}

enum TitleRow { TR_START, TR_PLAYERS, TR_DIFFICULTY, TR_SETTINGS, TR_CREDITS, TR_QUIT };

std::string rowLabel(int i)
{
    const char* k[TITLE_ITEMS] = {"menu_start", "menu_players", "menu_difficulty", "menu_settings", "menu_credits", "menu_quit"};
    return T(k[i]);
}
std::string rowValue(Game& g, int i)
{
    if (i == TR_PLAYERS) return g.players[1].joined ? T("menu_players_2") : T("menu_players_1");
    if (i == TR_DIFFICULTY) return T("menu_diff_" + std::to_string(g.difficulty));
    return "";
}
std::string rowDesc(Game& g, int i)
{
    switch (i) {
    case TR_START: return T("menu_start_desc");
    case TR_PLAYERS: return g.players[1].joined ? T("menu_players_desc_2") : T("menu_players_desc_1");
    case TR_DIFFICULTY: return T("difficulty_desc_" + std::to_string(g.difficulty));
    case TR_SETTINGS: return T("menu_settings_desc");
    case TR_CREDITS: return T("menu_credits_desc");
    default: return T("menu_quit_desc");
    }
}

// The credits: one sheet, names in Edie's hand.
void credits(float a)
{
    const float w = 720, x = (W - w) / 2, y = 186, h = 420;
    beginRotate(W / 2, y + h / 2, -0.6f);
    paper({x, y, w, h}, col::paper, a);
    tape(x + 40, y + 6, 100, -32, a);
    tape(x + w - 40, y + 6, 100, 32, a);
    TextStyle tt = serif(50, fade(col::paperInk, a), CENTER);
    tt.bold = true;
    text(T("menu_credits"), W / 2, y + 26, tt);
    text(T("credits_team"), W / 2, y + 96, hand(52, fade(col::brick, a), CENTER));
    float yy = y + 176;
    auto line = [&](const char* head, const char* body) {
        text(T(head), x + 60, yy, serif(24, fade(col::paperInk2, a), LEFT, true));
        auto ls = wrap(T(body), 26, w - 260);
        for (auto& l : ls) { text(l, x + 220, yy - 2, serif(26, fade(col::paperInk, a))); yy += 34; }
        yy += 14;
    };
    line("credits_jam_head", "credits_jam");
    line("credits_tech_head", "credits_tech");
    line("credits_fonts_head", "credits_fonts");
    endRotate();
}

}  // namespace

// ================================================================== Hud
void Hud::drawPlaying(Game& g)
{
    gameFrame(g);
    K = &skin(g.era);
    float a = 1;
    // stay out of the way while a boss scene or the cinematic bars have the screen
    playerCard(g, 0, a);
    playerCard(g, 1, a);
    memoryPanel(g);
    float below = M + CARD_H;
    if (g.boss.active && g.boss.state != 5) below = bossBar(g, below);
    else { bossLastHp = -1; }
    powerUpCorners(g);
    if (g.bannerT > 0) banner(g, below);
}

void Hud::drawTitle(Game& g)
{
    menuFrame();
    K = &menuSkin();
    const float t = g.time;
    const int e = ((int)(t / 3.0f)) % art::ERA_COUNT;
    const float intro = easeOutCubic(t / 0.9f);
    // the backdrop stays, but quieter: a dark band behind the logo, a warm wash over the rest
    gradV({0, 0, W, 300}, fade(hex(0x120d0a), 0.7f), fade(hex(0x120d0a), 0.15f));
    rect({0, 300, W, H - 300}, fade(hex(0x120d0a), 0.25f));
    vignette({0, 0, W, H}, 0.4f);

    const bool creditsMode = g.titleMode == 1;
    const float logoX = creditsMode ? W / 2 : 860;
    const float logoY = creditsMode ? 30 : 28;
    const float logoSize = creditsMode ? 84 : 96;
    logo(e, logoX, logoY + (1 - intro) * -20 + std::round(std::sin(t * 1.6f) * 3), logoSize, intro, t);
    // which stage the logo and backdrop are on: dots and the name on a dark tag
    {
        float dy = logoY + logoSize * 1.12f + 10;
        std::string era = upper(T("era_name_" + std::to_string(e)));
        for (size_t i = 0; (i = era.find(" . ", i)) != std::string::npos;) era.replace(i, 3, "  \xC2\xB7  ");
        TextStyle es = pix(PX, fade(col::cream, intro), LEFT, 0);
        float ew = textW(era, es), step = 16, dotsW = step * art::ERA_COUNT;
        float tw = dotsW + 14 + ew + 28, tx = logoX - tw / 2;
        rect({tx, dy, tw, 34}, fade(hex(0x14100d), 0.85f * intro));
        for (int i = 0; i < art::ERA_COUNT; ++i) {
            bool on = i == e;
            float s = on ? 8 : 6;
            rect({tx + 14 + i * step + (8 - s) / 2, dy + 17 - s / 2, s, s}, fade(on ? col::highlight : fade(col::cream, 0.45f), intro));
        }
        textMid(era, tx + 14 + dotsW + 6, dy + 17, es);
    }
    if (creditsMode) {
        credits(intro);
        promptStrip({{{"Esc"}, {PAD_B}, T("ui_back")}}, intro);
        return;
    }
    // the tagline, in cream italic on a dark band under the logo
    {
        float a = clamp01((t - 0.3f) / 0.5f);
        auto ls = wrap(T("title_sub"), 26, 680, SERIF_ITALIC);
        float sy = 186, sh = 16 + ls.size() * 32, bw = 0;
        for (auto& l : ls) bw = std::max(bw, textW(l, serif(26, WHITE, LEFT, true)));
        rect({logoX - bw / 2 - 24, sy, bw + 48, sh}, fade(hex(0x14100d), 0.72f * a));
        float ly = sy + 8;
        for (auto& l : ls) { text(l, logoX, ly, serif(26, fade(col::cream, a), CENTER, true)); ly += 32; }
    }

    // ---- the menu: a page from the album, taped on the left
    const float cx = 64, cy = 112, cw = 440, ch = 512;
    float ma = clamp01((t - 0.1f) / 0.4f);
    beginRotate(cx + cw / 2, cy + ch / 2, -0.8f);
    paper({cx, cy, cw, ch}, col::paper, ma);
    tape(cx + cw / 2, cy + 4, 120, 2, ma);
    text(T("menu_note"), cx + 44, cy + 26, hand(44, fade(col::brick, ma)));
    inkLine(cx + 44, cy + 84, cx + cw - 44, cy + 86, 1.5f, fade(col::paperInk2, 0.5f * ma));
    const float mx = cx + 70, my = cy + 104, rowH = 54;
    for (int i = 0; i < TITLE_ITEMS; ++i) {
        float ry = my + i * rowH, mid = ry + rowH / 2;
        float ia = clamp01((t - 0.2f - i * 0.05f) / 0.35f) * ma;
        bool sel = g.titleItem == i;
        TextStyle st = serif(36, fade(sel ? col::paperInk : col::paperInk2, ia));
        st.bold = sel;
        float lw = textW(rowLabel(i), st);
        if (sel) {
            selectionBar(1001, mx - 8, ry + 8, lw + 16, rowH - 16, col::highlight, ia);
            pointer(mx - 8, mid, ia, col::brick);
        }
        textMid(rowLabel(i), mx, mid, st);
        std::string v = rowValue(g, i);
        if (!v.empty()) {
            TextStyle vs = hand(38, fade(col::brick, ia), CENTER);
            float vcx = cx + cw - 100;
            textMid(v, vcx, mid, vs);
            if (sel) handArrows(vcx, mid, textW(v, vs) / 2 + 18, ia, col::brick);
        }
    }
    // what the chosen row does, in small italic under the list
    {
        float dy = my + TITLE_ITEMS * rowH + 6;
        for (auto& l : wrap(rowDesc(g, g.titleItem), 23, cw - 100, SERIF_ITALIC)) {
            text(l, mx, dy, serif(23, fade(col::paperInk2, ma), LEFT, true));
            dy += 29;
        }
    }
    endRotate();

    // ---- how to play, on an index card
    float ca = clamp01((t - 0.35f) / 0.5f);
    beginRotate(870, 450, 1.0f);
    controlsCard(g, 580, 286, 580, 330, ca);
    endRotate();

    promptStrip(navPrompts(g.titleItem == TR_PLAYERS || g.titleItem == TR_DIFFICULTY, false), clamp01((t - 0.5f) / 0.5f), true, &g);
}

void Hud::drawPause(Game& g)
{
    gameFrame(g);
    dimBackdrop(0.66f);
    menuFrame();
    K = &menuSkin();
    static double openedAt = 0;
    static int lastFrameSeen = -1;
    int fr = (int)(GetTime() * 60);
    if (lastFrameSeen < 0 || fr - lastFrameSeen > 3) openedAt = GetTime();
    lastFrameSeen = fr;
    float in = easeOutCubic((float)(GetTime() - openedAt) / 0.25f);

    const float w = 470, h = 430, x = (W - w) / 2, y = 112 + (1 - in) * 16;
    beginRotate(W / 2, y + h / 2, -0.7f);
    paper({x, y, w, h}, col::paper, in);
    tape(x + 50, y + 4, 96, -28, in);
    tape(x + w - 50, y + 4, 96, 28, in);
    TextStyle hs = serif(54, fade(col::paperInk, in), CENTER);
    hs.bold = true;
    text(T("pause"), W / 2, y + 30, hs);
    // where you are, as a pencilled note
    std::string where = T("era_name_" + std::to_string(g.era));
    size_t dot = where.find(" . ");
    if (dot != std::string::npos) where = where.substr(dot + 3);
    std::string note = where + "  \xC2\xB7  " + T("pause_memory") + " " + std::to_string((int)g.bar) + "%";
    text(note, W / 2, y + 100, hand(36, fade(col::brick, in), CENTER));
    const char* items[4] = {"resume", "settings_item", "restart", "quit"};
    const float rowH = 56, rx = x + 110, ry0 = y + 160;
    for (int i = 0; i < 4; ++i) {
        float ry = ry0 + i * rowH, mid = ry + rowH / 2;
        bool sel = g.pauseItem == i;
        TextStyle st = serif(36, fade(sel ? col::paperInk : col::paperInk2, in));
        st.bold = sel;
        if (sel) {
            selectionBar(2001, rx - 8, ry + 9, textW(T(items[i]), st) + 16, rowH - 18, col::highlight, in);
            pointer(rx - 8, mid, in, col::brick);
        }
        textMid(T(items[i]), rx, mid, st);
    }
    endRotate();
    promptStrip(navPrompts(false, true), in);
}

// Settings: a page of a notebook - blue rules, a red margin, the values pencilled in by hand,
// on / off as ticked boxes, the many-step values as a row of boxes.
void Hud::drawSettings(Game& g)
{
    bool overGame = g.settingsFrom != Screen::Title;
    dimBackdrop(overGame ? 0.66f : 0.6f);
    menuFrame();
    K = &menuSkin();
    const float w = 860, x = (W - w) / 2, y = 14, h = 628;
    paper({x, y, w, h}, col::paper);
    tape(x + w / 2, y + 2, 130, -2);
    const float margin = x + 92;
    // the notebook: blue rules, a red margin line
    for (float ry = y + 96; ry < y + h - 10; ry += 31) rect({x + 2, ry, w - 4, 1.2f}, fade(col::paperRule, 0.35f));
    rect({margin - 18, y + 2, 1.6f, h - 4}, fade(col::brick, 0.45f));
    TextStyle hs = serif(46, col::paperInk);
    hs.bold = true;
    text(T("settings"), margin, y + 26, hs);
    text(T("settings_note"), x + w - 48, y + 38, hand(34, col::brick, RIGHT));

    const char* headers[3] = {"settings_display", "settings_graphics", "settings_audio"};
    const float lx = margin, rx = x + w - 48, rowH = 31;
    float yy = y + 96;
    int lastSection = -1;
    float selY = 0, selW = 0;
    std::vector<std::pair<int, float>> rows;
    for (int i = 0; i <= SET_COUNT; ++i) {
        bool back = i == SET_COUNT;
        int section = back ? 3 : Settings::sectionOf(i);
        if (section != lastSection) {
            lastSection = section;
            if (!back) {
                textMid(T(headers[section]), lx - 4, yy + rowH / 2 + 2, serif(26, col::brick, LEFT, true));
                yy += rowH;
            }
        }
        rows.push_back({i, yy});
        std::string lab = back ? T("settings_back") : T("set_" + std::string(Settings::info(i).key));
        if (g.settingsItem == i) { selY = yy; selW = textW(lab, serif(25, WHITE, LEFT)) + 16; }
        yy += rowH;
    }
    selectionBar(3001, lx + 18 - 8, selY + 4, selW, rowH - 7, col::highlight);
    for (auto& [i, ry] : rows) {
        bool sel = g.settingsItem == i;
        float mid = ry + rowH / 2 + 1;
        TextStyle ls = serif(25, sel ? col::paperInk : col::paperInk2);
        ls.bold = sel;
        if (sel) pointer(lx + 10, mid, 1, col::brick);
        if (i == SET_COUNT) { textMid(T("settings_back"), lx + 18, mid, ls); continue; }
        textMid(T("set_" + std::string(Settings::info(i).key)), lx + 18, mid, ls);
        const int n = Settings::info(i).count, v = settings().get(i);
        const bool isToggle = i == SET_FULLSCREEN || i == SET_VSYNC || i == SET_SHOW_FPS || i == SET_BLOOM;
        TextStyle vs = hand(32, col::brick, RIGHT);
        if (isToggle) {
            checkbox(rx - 22, mid, 20, v != 0, col::paperInk);
            textMid(settings().valueText(i), rx - 34, mid, vs);
        } else if (n >= 5) {   // a row of boxes, filled up to the value
            const float bs = 13, gap = 4, bw = n * (bs + gap) - gap, bx = rx - bw;
            for (int s = 0; s < n; ++s) {
                Rectangle b{bx + s * (bs + gap), mid - bs / 2, bs, bs};
                if (s <= v) rect(b, fade(col::brick, sel ? 1 : 0.85f));
                else roundLines(b, 0, 1.5f, fade(col::paperInk2, 0.6f));
            }
            textMid(settings().valueText(i), bx - 14, mid, vs);
        } else {
            float vw = textW(settings().valueText(i), vs), vcx = rx - 20 - vw / 2;
            textMid(settings().valueText(i), rx - 20, mid, vs);
            if (sel) handArrows(vcx, mid, vw / 2 + 16, 1, col::brick);
        }
    }
    // what the chosen option does
    std::string desc = g.settingsItem < SET_COUNT ? std::string("set_") + Settings::info(g.settingsItem).key + "_desc" : "settings_back_desc";
    text(T(desc), lx - 4, y + h - 44, serif(24, col::paperInk2, LEFT, true));
    promptStrip(navPrompts(true, true), 1);
}

void Hud::drawEnding(Game& g)
{
    // The ending is told by ui/Story (Story::drawEnding); this older slideshow is kept for reference.
    menuFrame();
    ClearBackground(BLACK);
    text(T("thanks"), W / 2, H / 2 - 16, serif(32, col::cream, CENTER));
    (void)g;
}
