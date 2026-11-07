// Owner: Art and rendering (area 3). Starter code from the skeleton: replace freely.
#include "render/Renderer.h"
#include <cmath>
#include "core/Game.h"

namespace {
// Placeholder palettes. Era 0 is black and white only.
const EraPalette kPalettes[4] = {
    // background      solid               cracked             wall                p1                  p2                  enemy               fragment            forgetting
    {{12, 12, 12, 255}, {235, 235, 235, 255}, {150, 150, 150, 255}, {235, 235, 235, 255}, {255, 255, 255, 255}, {190, 190, 190, 255}, {235, 235, 235, 255}, {255, 255, 255, 255}, {90, 90, 90, 255}},
    {{32, 24, 72, 255}, {196, 120, 60, 255}, {140, 80, 40, 255}, {120, 120, 140, 255}, {255, 150, 40, 255}, {60, 160, 255, 255}, {200, 40, 90, 255}, {255, 240, 120, 255}, {80, 70, 110, 255}},
    {{58, 110, 165, 255}, {92, 160, 72, 255}, {120, 90, 60, 255}, {110, 110, 120, 255}, {255, 140, 40, 255}, {50, 150, 255, 255}, {150, 40, 160, 255}, {255, 245, 160, 255}, {70, 60, 100, 255}},
    {{140, 190, 230, 255}, {110, 170, 90, 255}, {130, 100, 70, 255}, {120, 120, 130, 255}, {255, 130, 30, 255}, {40, 140, 255, 255}, {110, 30, 140, 255}, {255, 250, 190, 255}, {60, 50, 90, 255}},
};

Color darker(Color c) { return {(unsigned char)(c.r * 0.6f), (unsigned char)(c.g * 0.6f), (unsigned char)(c.b * 0.6f), c.a}; }
}

const EraPalette& Renderer::palette(int era) {
    if (era < 0) era = 0;
    if (era > 3) era = 3;
    return kPalettes[era];
}

void Renderer::init() {}
void Renderer::shutdown() {}

void Renderer::draw(const Game& game) {
    const EraPalette& pal = palette(game.era.current());
    const GameCamera& camera = game.camera;
    ClearBackground(pal.background);

    BeginMode3D(camera.cam);

    // Tiles in view.
    int x0 = (int)std::floor(camera.leftEdge()) - 1;
    int x1 = (int)std::ceil(camera.rightEdge()) + 1;
    int y0 = (int)std::floor(camera.bottomEdge()) - 1;
    int y1 = (int)std::ceil(camera.topEdge()) + 1;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            char t = game.level.tileAt(x, y);
            if (t == '.') continue;
            Color c = pal.solid;
            if (t == '=') c = pal.cracked;
            else if (t == '|') c = pal.wall;
            else if (t == '1') c = Fade(pal.p1, 0.7f);
            else if (t == '2') c = Fade(pal.p2, 0.7f);
            Vector3 at = {x + 0.5f, y + 0.5f, 0.0f};
            DrawCube(at, 1.0f, 1.0f, 1.0f, c);
            DrawCubeWires(at, 1.0f, 1.0f, 1.0f, darker(c));
        }
    }

    // Fragments and enemies.
    for (const Fragment& f : game.enemies.fragments)
        DrawCube({f.pos.x, f.pos.y, 0.0f}, 0.35f, 0.35f, 0.35f, pal.fragment);
    for (const Enemy& e : game.enemies.enemies) {
        float s = e.type == 'B' ? 1.6f : (e.type == 'G' ? 0.7f : 0.9f);
        DrawCube({e.pos.x, e.pos.y + (s - 1.0f) * 0.5f, 0.0f}, s, s, s, pal.enemy);
    }

    // Players.
    for (int p = 0; p < 2; ++p) {
        const Player& pl = game.players[p];
        if (!pl.active) continue;
        Color c = p == 0 ? pal.p1 : pal.p2;
        if (pl.faded) c = Fade(GRAY, 0.5f);
        Vector2 ctr = pl.centre();
        DrawCube({ctr.x, ctr.y, 0.0f}, Player::W, Player::H, 0.8f, c);
        DrawCubeWires({ctr.x, ctr.y, 0.0f}, Player::W, Player::H, 0.8f, darker(c));
    }

    // Forgetting Wall.
    if (game.wall.enabled) {
        float h = camera.viewHeight + 4.0f;
        DrawCube({game.wall.x - 10.0f, camera.cam.target.y, 0.0f}, 20.0f, h, 2.0f, Fade(pal.forgetting, 0.85f));
    }

    EndMode3D();
}
