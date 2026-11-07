#include "ui/Hud.h"
#include "core/Config.h"
#include "core/Game.h"
#include "core/Strings.h"
#include "player/Abilities.h"
#include "raylib.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

float S = 1;          // UI scale (window height / 720)
float OX = 0, OY = 0; // game image offset in the window

Color hexc(unsigned h, unsigned char a = 255) { return Color{(unsigned char)(h >> 16), (unsigned char)(h >> 8), (unsigned char)h, a}; }
Color fade(Color c, float a) { c.a = (unsigned char)(c.a * std::max(0.0f, std::min(1.0f, a))); return c; }

const Color P_COL[2] = {{242, 138, 30, 255}, {60, 156, 240, 255}};

void setup(Game& g)
{
    Rectangle r = g.renderer.screenRect();
    S = r.height / 720.0f;
    OX = r.x;
    OY = r.y;
}

int fs(float size) { return std::max(8, (int)std::lround(size * S)); }

// Text in UI units (1280 x 720 space), centred or left aligned, with a drop shadow.
void text(const std::string& s, float x, float y, float size, Color c, bool centre = false, bool shadow = true)
{
    int f = fs(size);
    float spacing = f / 10.0f;
    Vector2 m = MeasureTextEx(GetFontDefault(), s.c_str(), (float)f, spacing);
    float px = OX + x * S - (centre ? m.x / 2 : 0), py = OY + y * S;
    if (shadow) DrawTextEx(GetFontDefault(), s.c_str(), {px + 2 * S, py + 2 * S}, (float)f, spacing, fade(BLACK, c.a / 255.0f * 0.7f));
    DrawTextEx(GetFontDefault(), s.c_str(), {px, py}, (float)f, spacing, c);
}

Rectangle R(float x, float y, float w, float h) { return Rectangle{OX + x * S, OY + y * S, w * S, h * S}; }

// ------------------------------------------------------------------ Memory Bar, styled per era
void memoryBar(Game& g)
{
    const int era = g.era;
    float k = std::max(0.0f, std::min(1.0f, g.bar / 100.0f));
    float x = 390, y = 18, w = 500, h = era == 3 ? 26 : 24;
    if (era == 0) {
        DrawRectangleRec(R(x - 4, y - 4, w + 8, h + 8), hexc(0xebebe1));
        DrawRectangleRec(R(x - 2, y - 2, w + 4, h + 4), hexc(0x101216));
        int segs = 20;
        for (int i = 0; i < segs; ++i)
            if ((i + 0.5f) / segs <= k) DrawRectangleRec(R(x + i * w / segs + 2, y + 2, w / segs - 4, h - 4), hexc(0xebebe1));
    } else if (era == 1) {
        DrawRectangleRec(R(x - 4, y - 4, w + 8, h + 8), hexc(0x0f0d1a));
        DrawRectangleRec(R(x, y, w, h), hexc(0x26305e));
        DrawRectangleRec(R(x, y, w * k, h), hexc(0xf28a1e));
        DrawRectangleRec(R(x, y, w * k, 6), hexc(0xf7d73a));
        for (int i = 1; i < 25; ++i) DrawRectangleRec(R(x + i * w / 25 - 1, y, 2, h), hexc(0x0f0d1a, 120));
    } else if (era == 2) {
        DrawRectangleRounded(R(x - 5, y - 5, w + 10, h + 10), 0.4f, 6, hexc(0x140f1e));
        DrawRectangleRounded(R(x, y, w, h), 0.3f, 6, hexc(0x2a2a4a));
        if (k > 0) {
            DrawRectangleGradientH((int)(OX + x * S), (int)(OY + y * S), (int)(w * k * S), (int)(h * S), hexc(0xe8506a), hexc(0xf7d73a));
            DrawRectangleRec(R(x, y + 3, w * k, 5), fade(WHITE, 0.35f));
        }
    } else {
        DrawRectangleRounded(R(x - 8, y - 8, w + 16, h + 16), 1.0f, 12, fade(hexc(0x100818), 0.65f));
        DrawRectangleRounded(R(x, y, w, h), 1.0f, 12, fade(hexc(0x3a2a4a), 0.9f));
        if (k > 0.02f) {
            DrawRectangleRounded(R(x, y, w * k, h), 1.0f, 12, hexc(0xffb050));
            DrawRectangleRounded(R(x + 4, y + 3, std::max(0.0f, w * k - 8), h * 0.38f), 1.0f, 12, fade(WHITE, 0.45f));
            float shine = std::fmod(g.time * 0.4f, 1.0f) * w * k;
            DrawRectangleRec(R(x + shine, y + 2, 10, h - 4), fade(WHITE, 0.25f));
        }
        DrawRectangleRoundedLinesEx(R(x, y, w, h), 1.0f, 12, 2 * S, fade(hexc(0xfff0d0), 0.6f + 0.4f * std::sin(g.time * 2)));
    }
    // tick marks at 25 / 50 / 75
    Color tick = era == 0 ? hexc(0xebebe1) : hexc(0xfaf3e6, 200);
    for (int i = 1; i <= 3; ++i) DrawRectangleRec(R(x + w * i / 4.0f - 1, y + h, 2, 7), tick);
    std::string label = std::string(T("bar_label")) + "  " + std::to_string((int)std::floor(g.bar)) + "%";
    text(label, x + w / 2, y + h + 9, 16, era == 0 ? hexc(0xebebe1) : WHITE, true);
    text(T("era_name_" + std::to_string(era)), x + w / 2, y + h + 29, 13, fade(WHITE, 0.75f), true);
}

// ------------------------------------------------------------------ player panels
void playerPanel(Game& g, int p)
{
    const Player& pl = g.players[p];
    float x = p == 0 ? 20 : 1280 - 290, y = 14, w = 270, h = 74;
    Color col = g.era == 0 ? hexc(0xebebe1) : P_COL[p];
    Color bg = fade(hexc(0x0f0d1a), g.era == 3 ? 0.55f : 0.8f);
    if (g.era >= 2) DrawRectangleRounded(R(x, y, w, h), 0.25f, 6, bg);
    else DrawRectangleRec(R(x, y, w, h), bg);
    DrawRectangleRec(R(x, y, 8, h), col);
    if (!pl.joined) {
        text(T("p2_join_1"), x + 20, y + 14, 18, fade(WHITE, 0.6f + 0.4f * std::sin(g.time * 4)));
        text(T("p2_join_2"), x + 20, y + 42, 14, fade(WHITE, 0.7f));
        return;
    }
    text(std::string(T(p == 0 ? "p1_name" : "p2_name")), x + 20, y + 8, 20, col);
    if (pl.faded) {
        text(T("faded"), x + 150, y + 10, 16, fade(WHITE, 0.6f + 0.4f * std::sin(g.time * 6)));
        text(g.solo() ? T("faded_hint_solo") : T("faded_hint"), x + 20, y + 40, 13, fade(WHITE, 0.8f));
        return;
    }
    // ability chips
    float cx = x + 20;
    for (int a = 0; a < AB_COUNT; ++a) {
        if (!pl.has[a]) continue;
        std::string n = T(std::string("ability_short_") + abilityId(a));
        float tw = MeasureText(n.c_str(), fs(12)) / S + 12;
        if (g.era >= 2) DrawRectangleRounded(R(cx, y + 38, tw, 22), 0.4f, 4, fade(col, 0.35f));
        else DrawRectangleRec(R(cx, y + 38, tw, 22), fade(col, 0.35f));
        text(n, cx + 6, y + 43, 12, WHITE, false, false);
        cx += tw + 6;
    }
    if (cx == x + 20) text(T("no_abilities"), x + 20, y + 42, 12, fade(WHITE, 0.6f));
    // status icons
    float sx = x + 150;
    if (pl.shield) { text(T("status_shield"), sx, y + 12, 13, hexc(0xff7aa0)); sx += 50; }
    if (pl.speedTime > 0) { text(T("status_speed"), sx, y + 12, 13, hexc(0xf7d73a)); sx += 50; }
    if (pl.magnetTime > 0) { text(T("status_magnet"), sx, y + 12, 13, hexc(0xc8a8ff)); sx += 50; }
    if (pl.has[AB_TIME_SLOW] && pl.slowCooldown > 0) {
        float k = 1 - pl.slowCooldown / config().f("abilities.time_slow_cooldown", 10.0f);
        DrawRectangleRec(R(x + 20, y + 64, (w - 40) * k, 4), hexc(0x7fe8ff));
    }
}

}  // namespace

void Hud::drawPlaying(Game& g)
{
    setup(g);
    memoryBar(g);
    playerPanel(g, 0);
    playerPanel(g, 1);
    text(std::string(T("score")) + " " + std::to_string(g.score), 640, 690, 16, fade(WHITE, 0.8f), true);

    // boss health
    if (g.boss.active && g.boss.state != 5) {
        float w = 360, x = 640 - w / 2, y = 98;
        text(T("boss_name_" + std::to_string(g.boss.kind)), 640, y - 2, 14, hexc(0xe0b0ff), true);
        DrawRectangleRec(R(x, y + 16, w, 10), fade(BLACK, 0.7f));
        DrawRectangleRec(R(x, y + 16, w * g.boss.hp / (float)g.boss.maxHp, 10), hexc(0xb050d0));
        if (g.boss.state == 3) text(T("boss_hint"), 640, y + 32, 14, fade(WHITE, 0.6f + 0.4f * std::sin(g.time * 8)), true);
    }

    // banner (era transitions, memory lost, boss, power-ups)
    if (g.bannerT > 0) {
        float a = std::min(1.0f, std::min(g.bannerT * 3, (g.bannerLen - g.bannerT) * 4 + 0.0f));
        a = std::min(1.0f, g.bannerT * 3);
        float big = g.era == 0 ? 40 : 46;
        Color c = g.era == 0 ? hexc(0xebebe1) : hexc(0xfff4d8);
        DrawRectangleRec(R(0, 250, 1280, 150), fade(BLACK, 0.45f * a));
        text(g.banner, 640, 275, big, fade(c, a), true);
        text(g.bannerSub, 640, 340, 22, fade(WHITE, a), true);
    }

    // help line for the first seconds
    if (g.time < 12 && !g.transActive)
        text(T("controls_hint"), 640, 660, 14, fade(WHITE, std::min(1.0f, (12 - g.time) / 2)), true);
}

void Hud::drawTitle(Game& g)
{
    setup(g);
    int e = ((int)(g.time / 3.0f)) % 4;
    Color title = e == 0 ? hexc(0xebebe1) : (e == 1 ? hexc(0xf7d73a) : (e == 2 ? hexc(0xffb0d0) : hexc(0xfff0d0)));
    float bob = std::sin(g.time * 2) * 6;
    // the title "upgrades" every few seconds, like the game does
    float size = 110 + e * 8;
    text(T("title"), 640, 150 + bob, size, title, true);
    text(T("title_sub"), 640, 290, 22, fade(WHITE, 0.9f), true);
    text(T("title_start"), 640, 400, 28, fade(WHITE, 0.55f + 0.45f * std::sin(g.time * 4)), true);
    std::string p2 = g.players[1].joined ? T("title_p2_ready") : T("title_join");
    text(p2, 640, 450, 20, g.players[1].joined ? P_COL[1] : fade(WHITE, 0.8f), true);
    text(T("title_controls_p1"), 640, 560, 15, fade(P_COL[0], 0.95f), true);
    text(T("title_controls_p2"), 640, 586, 15, fade(P_COL[1], 0.95f), true);
    text(T("title_controls_pad"), 640, 612, 14, fade(WHITE, 0.7f), true);
    text(T("title_credit"), 640, 680, 13, fade(WHITE, 0.5f), true);
}

void Hud::drawPause(Game& g)
{
    setup(g);
    DrawRectangleRec(R(0, 0, 1280, 720), fade(BLACK, 0.6f));
    text(T("pause"), 640, 220, 56, WHITE, true);
    const char* items[3] = {"resume", "restart", "quit"};
    for (int i = 0; i < 3; ++i) {
        bool sel = g.pauseItem == i;
        std::string s = std::string(sel ? "> " : "  ") + T(items[i]) + (sel ? " <" : "  ");
        text(s, 640, 330 + i * 50, 30, sel ? hexc(0xf7d73a) : fade(WHITE, 0.8f), true);
    }
    text(T("pause_hint"), 640, 520, 16, fade(WHITE, 0.6f), true);
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
        text(T("ending_sub_" + std::to_string(i)), 640, 668, 18, fade(hexc(0xffe0b0), a), true);
        return;
    }
    float rt = t - n * slide;
    // the reveal
    if (rt < 12) {
        for (int i = 0; i < 4; ++i) {
            float a = std::min(1.0f, std::max(0.0f, (rt - i * 2.2f) / 1.0f));
            text(T("ending_reveal_" + std::to_string(i + 1)), 640, 220 + i * 70, i == 3 ? 40 : 28,
                 fade(i == 3 ? hexc(0xffd890) : WHITE, a), true);
        }
        return;
    }
    // credits roll
    float ct = rt - 12;
    std::string credits = T("credits");
    float y = 720 - ct * 50;
    text(T("credits_title"), 640, y, 44, hexc(0xffd890), true);
    y += 80;
    size_t start = 0;
    while (start <= credits.size()) {
        size_t nl = credits.find('\n', start);
        std::string line = credits.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        text(line, 640, y, 22, WHITE, true);
        y += 40;
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    if (y < 360) text(T("thanks"), 640, 360, 34, hexc(0xfff0d0), true);
    if (ct > 3) text(T("ending_continue"), 640, 660, 16, fade(WHITE, 0.5f + 0.5f * std::sin(g.time * 3)), true);
}
