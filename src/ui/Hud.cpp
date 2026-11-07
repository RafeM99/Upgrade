// Owner: Audio, level design and UI (area 4). Starter code from the skeleton: replace freely.
#include "ui/Hud.h"
#include <algorithm>
#include <string>
#include "core/Game.h"
#include "core/Strings.h"

namespace {
void centredText(const std::string& text, int y, int size, Color c) {
    int w = MeasureText(text.c_str(), size);
    DrawText(text.c_str(), (GetScreenWidth() - w) / 2, y, size, c);
}
}

void Hud::init() {}

void Hud::draw(const Game& game) {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const EraPalette& pal = Renderer::palette(game.era.current());

    // Memory Bar, top centre, with ticks at the era thresholds.
    int barW = std::min(480, sw / 2);
    int barH = 20;
    int barX = (sw - barW) / 2;
    int barY = 16;
    float fill = game.bar.value / game.bar.maxValue;
    DrawRectangle(barX - 2, barY - 2, barW + 4, barH + 4, BLACK);
    DrawRectangle(barX, barY, (int)(barW * fill), barH, game.era.current() == 0 ? WHITE : pal.fragment);
    for (int e = 1; e <= 3; ++e) {
        int tx = barX + (int)(barW * game.era.thresholdFor(e) / game.bar.maxValue);
        DrawRectangle(tx - 1, barY - 4, 2, barH + 8, GRAY);
    }
    DrawRectangleLines(barX, barY, barW, barH, WHITE);
    std::string label = Strings::get("memory_bar") + "  " + std::to_string((int)game.bar.value) + "%   " +
                        Strings::get("era" + std::to_string(game.era.current()) + "_name");
    centredText(label, barY + barH + 6, 20, WHITE);

    // Player indicators, bottom left.
    for (int p = 0; p < 2; ++p) {
        const Player& pl = game.players[p];
        int x = 16 + p * 260;
        int y = sh - 36;
        Color c = p == 0 ? pal.p1 : pal.p2;
        if (!pl.active) {
            DrawText(Strings::get("p2_join").c_str(), x, y, 20, LIGHTGRAY);
            continue;
        }
        DrawRectangle(x, y, 20, 20, pl.faded ? GRAY : c);
        std::string name = "P" + std::to_string(p + 1) + (pl.faded ? " " + Strings::get("faded") : "");
        DrawText(name.c_str(), x + 28, y, 20, WHITE);
    }

    // Era transition popup.
    if (game.freezeTimer > 0.0f) {
        DrawRectangle(0, sh / 2 - 60, sw, 120, Fade(BLACK, 0.75f));
        centredText(game.popupTitle, sh / 2 - 40, 32, WHITE);
        centredText(game.popupLine, sh / 2 + 10, 22, WHITE);
    }

    // Placeholder ending.
    if (game.complete) {
        DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.8f));
        centredText(game.popupTitle, sh / 2 - 50, 32, WHITE);
        centredText(Strings::get("ending_placeholder"), sh / 2 + 10, 22, LIGHTGRAY);
    }

    DrawText(Strings::get("debug_keys").c_str(), 10, 10, 10, GRAY);
    DrawFPS(sw - 90, 10);
}
