#include "ui/Hud.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include "core/Config.h"
#include "core/Game.h"
#include "core/Strings.h"

namespace {
// Text in the style of the current era.
void text(const std::string& s, int x, int y, int size, int era, Color override = BLANK) {
    const EraLook& L = Renderer::look(era);
    Color c = override.a ? override : L.hudText;
    const char* t = s.c_str();
    switch (era) {
        case 0:   // hard black drop shadow
            DrawText(t, x + 3, y + 3, size, L.hudShadow);
            break;
        case 1:   // full outline, like SNES menus
            for (int dx = -2; dx <= 2; dx += 2)
                for (int dy = -2; dy <= 2; dy += 2)
                    if (dx || dy) DrawText(t, x + dx, y + dy, size, L.hudShadow);
            break;
        case 2:   // chunky offset shadow
            DrawText(t, x + 4, y + 4, size, L.hudShadow);
            break;
        case 3:   // soft shadow
            DrawText(t, x + 2, y + 2, size, Fade(L.hudShadow, 0.7f));
            break;
        default: break;   // Wii: clean text on white panels
    }
    DrawText(t, x, y, size, c);
}

void centred(const std::string& s, int y, int size, int era, Color override = BLANK) {
    int w = MeasureText(s.c_str(), size);
    text(s, (GetScreenWidth() - w) / 2, y, size, era, override);
}

void panel(int x, int y, int w, int h, int era) {
    const EraLook& L = Renderer::look(era);
    switch (era) {
        case 0: DrawRectangle(x, y, w, h, BLACK); DrawRectangleLines(x, y, w, h, WHITE); break;
        case 1:
            DrawRectangle(x, y, w, h, Fade({24, 32, 120, 255}, 0.92f));
            DrawRectangleLinesEx({(float)x, (float)y, (float)w, (float)h}, 4, WHITE);
            break;
        case 2:
            DrawRectangle(x, y, w, h, Fade({20, 20, 80, 255}, 0.85f));
            DrawRectangleLinesEx({(float)x, (float)y, (float)w, (float)h}, 5, L.hudAccent);
            break;
        case 3:
            DrawRectangleRounded({(float)x, (float)y, (float)w, (float)h}, 0.15f, 8, Fade({10, 8, 30, 255}, 0.8f));
            DrawRectangleRoundedLinesEx({(float)x, (float)y, (float)w, (float)h}, 0.15f, 8, 2, L.hudAccent);
            break;
        default:
            DrawRectangleRounded({(float)x + 4, (float)y + 6, (float)w, (float)h}, 0.35f, 12, Fade(BLACK, 0.08f));
            DrawRectangleRounded({(float)x, (float)y, (float)w, (float)h}, 0.35f, 12, Fade(WHITE, 0.95f));
            DrawRectangleRoundedLinesEx({(float)x, (float)y, (float)w, (float)h}, 0.35f, 12, 3, {170, 215, 240, 255});
            break;
    }
}

std::string clock(float seconds) {
    char buf[16];
    int s = (int)seconds;
    std::snprintf(buf, sizeof buf, "%d:%02d", s / 60, s % 60);
    return buf;
}

std::string padded(int value, int digits) {
    std::string s = std::to_string(value);
    while ((int)s.size() < digits) s = "0" + s;
    return s;
}
}

void Hud::init() {}

void Hud::draw(const Game& game) {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const int e = game.era.current();
    const EraLook& L = Renderer::look(e);
    const float t = (float)GetTime();

    if (game.screen == Screen::Title) {
        DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.35f));
        int bounce = (int)(std::sin(t * 2.0f) * 6.0f);
        centred(Strings::get("title"), sh / 4 + bounce, 110, 0, L.hudAccent);
        centred(Strings::get("subtitle"), sh / 4 + 130, 28, 0);
        if (std::fmod(t, 1.0f) < 0.65f) centred(Strings::get("press_start"), sh / 2 + 40, 30, 0);
        centred(Strings::get("controls_p1"), sh - 110, 20, 0);
        centred(Strings::get("controls_p2"), sh - 80, 20, 0);
        if (game.titleNagTimer > 0.0f) centred(game.titleNag, sh / 2 + 100, 30, 0, {255, 90, 90, 255});
        return;
    }

    // Top bar: score, coins, world, deaths, time.
    struct Item { std::string label, value; Color colour; };
    Item items[5] = {
        {Strings::get("score"), padded(game.score, 6), BLANK},
        {Strings::get("coins"), "x" + padded(game.coins, 2), BLANK},
        {Strings::get("world"), std::to_string(game.world + 1) + "  " + Strings::get("era" + std::to_string(e) + "_short"), BLANK},
        {Strings::get("deaths"), std::to_string(game.deaths), {255, 80, 80, 255}},
        {Strings::get("time"), clock(game.playTime), BLANK},
    };
    const int colW = sw / 5;
    if (e == 3) panel(10, 6, sw - 20, 72, e);
    for (int i = 0; i < 5; ++i) {
        int x = colW * i + 24;
        if (e == 4) panel(x - 14, 8, colW - 24, 68, e);
        text(items[i].label, x, 14, 22, e);
        Color vc = items[i].colour.a ? items[i].colour : (e == 4 ? Color{60, 60, 80, 255} : L.hudText);
        if (i == 3 && game.deathTimer > 2.0f) vc = WHITE;   // flash on a fresh death
        text(items[i].value, x, 42, 28, e, vc);
    }

    // Progress to this world's flag.
    float flagX = game.nextFlagX();
    float start = game.worldStartX;
    {
        float end = flagX > 0.0f ? flagX : start + 140.0f;
        float frac = std::clamp((game.leadX() - start) / std::max(1.0f, end - start), 0.0f, 1.0f);
        int bw = sw / 3, bx = (sw - bw) / 2, by = 90;
        if (e == 0) {
            DrawRectangle(bx, by, bw, 6, BLACK);
            DrawRectangle(bx, by, (int)(bw * frac), 6, WHITE);
        } else {
            DrawRectangleRounded({(float)bx, (float)by, (float)bw, 8}, 1.0f, 8, Fade(e == 4 ? Color{170, 215, 240, 255} : BLACK, 0.5f));
            DrawRectangleRounded({(float)bx, (float)by, bw * frac, 8}, 1.0f, 8, L.hudAccent);
        }
        DrawCircle(bx + (int)(bw * frac), by + 4, 8, L.p1Cap);
        DrawRectangle(bx + bw - 2, by - 12, 3, 20, WHITE);
        DrawRectangle(bx + bw - 14, by - 12, 12, 8, L.pipe);
    }

    // P2 join hint.
    if (!game.players[1].active)
        text(Strings::get("p2_join"), 16, sh - 36, 20, e);
    text(Strings::get("debug_keys"), sw - MeasureText(Strings::get("debug_keys").c_str(), 14) - 12, sh - 22, 14, e,
         Fade(L.hudText, 0.6f));

    // Death taunt, with a shake.
    if (game.deathTimer > 0.0f) {
        float shake = game.deathTimer > 2.0f ? std::sin(t * 70.0f) * 8.0f : 0.0f;
        float a = std::min(1.0f, game.deathTimer);
        int y = sh / 3;
        std::string big = Strings::get("deaths") + ": " + std::to_string(game.deaths);
        int w = MeasureText(big.c_str(), 54);
        text(big, (sw - w) / 2 + (int)shake, y, 54, e, Fade({255, 70, 70, 255}, a));
        centred(game.deathLine, y + 70, 28, e, Fade(e == 4 ? Color{60, 60, 80, 255} : L.hudText, a));
    }

    // World clear / era upgrade.
    if (game.freezeTimer > 0.0f) {
        int pw = std::min(sw - 80, 900), ph = 200;
        int px = (sw - pw) / 2, py = sh / 2 - ph / 2;
        panel(px, py, pw, ph, e);
        centred(game.popupTitle, py + 24, 44, e);
        centred(game.popupLine, py + 88, 32, e, L.hudAccent);
        centred(game.popupTroll, py + 140, 22, e, e == 4 ? Color{60, 60, 80, 255} : BLANK);
        // A quick "boot up" flash as the new era starts.
        float flash = game.freezeTimer - (Config::getFloat("era", "transition_seconds", 2.5f) - 0.25f);
        if (flash > 0.0f) DrawRectangle(0, 0, sw, sh, Fade(WHITE, flash * 4.0f));
    }
}
