#include "render/Renderer.h"
#include <algorithm>
#include <cmath>
#include <string>
#include "core/Game.h"

namespace {
// clang-format off
const EraLook kLooks[kEraCount] = {
    // NES: the classic blue sky and orange-brown ground.
    {{92, 148, 252, 255}, {92, 148, 252, 255},
     {200, 76, 12, 255}, {200, 76, 12, 255}, {176, 56, 8, 255}, {252, 152, 56, 255}, {136, 72, 32, 255}, {0, 168, 0, 255}, {252, 188, 60, 255},
     {172, 80, 36, 255}, {228, 0, 88, 255}, {0, 120, 0, 255}, {188, 188, 188, 255},
     {228, 0, 16, 255}, {0, 168, 0, 255}, {0, 88, 248, 255}, {252, 188, 140, 255},
     {0, 168, 0, 255}, {0, 120, 0, 255}, {252, 252, 252, 255},
     {252, 252, 252, 255}, {0, 0, 0, 255}, {252, 188, 60, 255}},
    // SNES: softer gradient sky, grass tops, rounder everything.
    {{80, 128, 248, 255}, {176, 216, 248, 255},
     {168, 104, 48, 255}, {72, 200, 72, 255}, {200, 96, 40, 255}, {248, 200, 40, 255}, {140, 96, 60, 255}, {40, 184, 72, 255}, {248, 216, 56, 255},
     {176, 104, 48, 255}, {232, 56, 88, 255}, {56, 144, 72, 255}, {210, 210, 220, 255},
     {232, 40, 40, 255}, {40, 176, 72, 255}, {48, 80, 232, 255}, {248, 200, 152, 255},
     {64, 176, 96, 255}, {120, 200, 160, 255}, {255, 255, 255, 255},
     {255, 232, 72, 255}, {40, 24, 8, 255}, {255, 255, 255, 255}},
    // N64: saturated primary colours, slightly muddy.
    {{40, 104, 224, 255}, {150, 200, 255, 255},
     {150, 96, 56, 255}, {60, 170, 60, 255}, {190, 90, 50, 255}, {240, 200, 30, 255}, {120, 80, 50, 255}, {30, 160, 60, 255}, {250, 210, 40, 255},
     {160, 96, 48, 255}, {220, 50, 60, 255}, {40, 120, 60, 255}, {200, 200, 210, 255},
     {220, 30, 30, 255}, {30, 160, 60, 255}, {40, 60, 200, 255}, {250, 200, 160, 255},
     {70, 150, 70, 255}, {110, 170, 200, 255}, {255, 255, 255, 255},
     {255, 255, 255, 255}, {20, 20, 80, 255}, {255, 210, 0, 255}},
    // GameCube: dusk, indigo, moodier.
    {{30, 20, 70, 255}, {230, 130, 90, 255},
     {110, 80, 70, 255}, {90, 150, 90, 255}, {170, 90, 70, 255}, {230, 180, 50, 255}, {100, 70, 60, 255}, {40, 140, 90, 255}, {255, 210, 60, 255},
     {130, 80, 60, 255}, {200, 40, 90, 255}, {60, 100, 80, 255}, {180, 180, 200, 255},
     {200, 30, 40, 255}, {40, 150, 70, 255}, {50, 60, 170, 255}, {240, 190, 160, 255},
     {60, 50, 90, 255}, {90, 70, 120, 255}, {255, 190, 170, 255},
     {240, 240, 255, 255}, {10, 10, 30, 255}, {150, 120, 255, 255}},
    // Wii: bright, clean, pastel.
    {{200, 235, 255, 255}, {250, 252, 255, 255},
     {200, 160, 120, 255}, {120, 220, 120, 255}, {230, 140, 90, 255}, {255, 215, 80, 255}, {190, 160, 130, 255}, {90, 210, 130, 255}, {255, 220, 80, 255},
     {190, 130, 90, 255}, {240, 100, 130, 255}, {100, 180, 120, 255}, {220, 225, 240, 255},
     {240, 70, 70, 255}, {80, 200, 110, 255}, {80, 130, 240, 255}, {255, 215, 185, 255},
     {150, 225, 150, 255}, {190, 230, 240, 255}, {255, 255, 255, 255},
     {40, 150, 220, 255}, {255, 255, 255, 255}, {40, 150, 220, 255}},
};
// clang-format on

Color shade(Color c, float k) {
    auto ch = [k](unsigned char v) { return (unsigned char)std::min(255.0f, v * k); };
    return {ch(c.r), ch(c.g), ch(c.b), c.a};
}

float hash01(int i) {
    unsigned int x = (unsigned int)i * 2654435761u;
    x ^= x >> 13;
    x *= 0x5bd1e995u;
    x ^= x >> 15;
    return (x & 0xFFFF) / 65535.0f;
}

bool isGroundTile(char t) { return t == '#' || t == 'X' || t == '~' || t == '='; }
bool isSolidLook(char t) {
    return isGroundTile(t) || t == '%' || t == '?' || t == '!' || t == 'U' || t == '|' || t == '1' || t == '2';
}

// A block with an outline. From the GameCube era on, also a lighter top face for fake lighting.
void block(Vector3 at, float w, float h, float d, Color c, int era, bool lightTop = true) {
    DrawCube(at, w, h, d, c);
    if (era <= 1) DrawCubeWires(at, w, h, d, shade(c, 0.55f));
    else if (era == 2) DrawCubeWires(at, w, h, d, shade(c, 0.75f));
    else DrawCubeWires(at, w, h, d, Fade(shade(c, 0.6f), 0.5f));
    if (era >= 3 && lightTop) DrawCube({at.x, at.y + h * 0.5f - 0.03f, at.z}, w + 0.01f, 0.06f, d + 0.01f, shade(c, 1.25f));
}

void drawHero(const Player& p, Color cap, const EraLook& L, int era) {
    if (p.invuln > 0.0f && std::fmod((float)GetTime(), 0.16f) < 0.08f) return;   // respawn blink
    float x = p.centre().x;
    float y = p.pos.y;
    float f = (float)p.facing;
    // Little run cycle: legs bob.
    float bob = (p.onGround && std::fabs(p.vel.x) > 0.5f) ? std::fabs(std::sin((float)GetTime() * 18.0f)) * 0.06f : 0.0f;
    block({x, y + 0.25f + bob, 0.0f}, 0.66f, 0.5f, 0.55f, L.overalls, era);
    block({x, y + 0.66f + bob, 0.0f}, 0.56f, 0.34f, 0.5f, L.skin, era);
    block({x - f * 0.02f, y + 0.88f + bob, 0.0f}, 0.62f, 0.16f, 0.56f, cap, era);
    DrawCube({x + f * 0.34f, y + 0.82f + bob, 0.0f}, 0.2f, 0.06f, 0.46f, cap);   // brim
    DrawCube({x + f * 0.16f, y + 0.7f + bob, 0.26f}, 0.07f, 0.12f, 0.02f, BLACK);    // eye
    DrawCube({x + f * 0.26f, y + 0.58f + bob, 0.2f}, 0.22f, 0.05f, 0.1f, {60, 30, 10, 255});   // moustache
}

void drawEyes(Vector3 c, float spread, float size, bool angry) {
    for (int s = -1; s <= 1; s += 2) {
        Vector3 e = {c.x + s * spread, c.y, c.z};
        DrawCube(e, size, size * 1.3f, 0.02f, WHITE);
        DrawCube({e.x - 0.03f, e.y - 0.02f, e.z + 0.01f}, size * 0.5f, size * 0.7f, 0.02f, angry ? RED : BLACK);
    }
}

void drawCoin(Vector2 at, float spin, Color c, int sides) {
    Vector3 axis = {std::sin(spin) * 0.06f, 0.0f, std::cos(spin) * 0.06f};
    Vector3 a = {at.x - axis.x, at.y, -axis.z};
    Vector3 b = {at.x + axis.x, at.y, axis.z};
    DrawCylinderEx(a, b, 0.3f, 0.3f, sides, c);
}
}

const EraLook& Renderer::look(int era) {
    return kLooks[std::clamp(era, 0, kEraCount - 1)];
}

void Renderer::init() {}

void Renderer::shutdown() {
    if (target.id != 0) UnloadRenderTexture(target);
    target = RenderTexture2D{};
}

void Renderer::draw(const Game& game) {
    const int e = game.era.current();
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const int lowH = e == 0 ? 144 : e == 1 ? 216 : e == 2 ? 360 : 0;

    if (lowH > 0) {
        int lowW = std::max(1, lowH * sw / std::max(1, sh));
        if (target.id == 0 || targetW != lowW || targetH != lowH) {
            if (target.id != 0) UnloadRenderTexture(target);
            target = LoadRenderTexture(lowW, lowH);
            targetW = lowW;
            targetH = lowH;
        }
        // The N64 blur is the bilinear filter. The 8 and 16-bit crunch is point filtering.
        SetTextureFilter(target.texture, e == 2 ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
        BeginTextureMode(target);
        drawScene(game, lowW, lowH);
        EndTextureMode();
        ClearBackground(BLACK);
        DrawTexturePro(target.texture, {0, 0, (float)lowW, -(float)lowH}, {0, 0, (float)sw, (float)sh}, {0, 0}, 0.0f,
                       WHITE);
    } else {
        drawScene(game, sw, sh);
    }
    postEffects(game, sw, sh);
}

void Renderer::drawScene(const Game& game, int w, int h) {
    const int e = game.era.current();
    const EraLook& L = look(e);
    ClearBackground(L.skyBottom);
    DrawRectangleGradientV(0, 0, w, h, L.skyTop, L.skyBottom);
    drawBackdrop2D(game, w, h);

    BeginMode3D(game.camera.cam);
    if (e >= 2) drawBackdrop3D(game);
    drawWorld(game);
    EndMode3D();

    if (e == 2) DrawRectangle(0, 0, w, h, Fade(L.hillFar, 0.12f));   // N64 distance haze
    drawOverlayText(game, w, h);
}

void Renderer::drawBackdrop2D(const Game& game, int w, int h) {
    const int e = game.era.current();
    const EraLook& L = look(e);
    const GameCamera& cam = game.camera;
    const float scale = h / cam.viewHeight;           // pixels per tile
    const float groundY = h * 0.5f + (cam.focusY() - 2.0f) * scale;   // screen y of the ground top
    const float t = (float)GetTime();

    if (e == 0 || e == 1) {
        // Far hills (SNES only), then near hills, then clouds. Each layer scrolls slower than the world.
        for (int layer = (e == 1 ? 0 : 1); layer < 2; ++layer) {
            float par = layer == 0 ? 0.25f : 0.5f;
            float span = layer == 0 ? 20.0f : 24.0f;
            Color c = layer == 0 ? L.hillFar : L.hill;
            float camX = cam.focusX() * par;
            int k0 = (int)std::floor((camX - w / scale) / span) - 1;
            for (int k = k0; k < k0 + (int)(2 * w / scale / span) + 4; ++k) {
                float hx = (k * span + hash01(k + layer * 100) * 6.0f - camX) * scale + w * 0.5f;
                float r = (layer == 0 ? 5.0f : 3.0f + hash01(k) * 2.0f) * scale;
                if (e == 0) {
                    DrawCircle((int)hx, (int)groundY, r, c);
                    DrawCircle((int)(hx - r * 0.3f), (int)(groundY - r * 0.5f), r * 0.08f, BLACK);
                    DrawCircle((int)(hx + r * 0.25f), (int)(groundY - r * 0.6f), r * 0.08f, BLACK);
                } else {
                    DrawCircle((int)hx, (int)groundY, r, c);
                    for (int s = 1; s < 4; ++s)
                        DrawCircleLines((int)hx, (int)groundY, r * s / 4.0f, shade(c, 0.85f));
                }
            }
        }
        float camX = cam.focusX() * 0.3f;
        for (int k = (int)std::floor(camX / 14.0f) - 3; k < (int)std::floor(camX / 14.0f) + 6; ++k) {
            float cx = (k * 14.0f - camX) * scale + w * 0.5f;
            float cy = h * (0.12f + hash01(k * 7) * 0.18f);
            if (e == 0) {
                DrawRectangle((int)cx, (int)cy, (int)(3 * scale), (int)(scale), L.cloud);
                DrawRectangle((int)(cx + scale * 0.5f), (int)(cy - scale * 0.5f), (int)(2 * scale), (int)(scale), L.cloud);
            } else {
                DrawEllipse((int)cx, (int)cy, 1.8f * scale, 0.7f * scale, L.cloud);
                DrawEllipse((int)(cx + scale), (int)(cy - scale * 0.3f), 1.2f * scale, 0.7f * scale, L.cloud);
            }
        }
    } else if (e == 3) {
        // Stars in the dusk.
        for (int i = 0; i < 70; ++i) {
            float sx = std::fmod(hash01(i) * w * 1.5f - cam.focusX() * 2.0f, (float)w);
            if (sx < 0) sx += w;
            float sy = hash01(i + 500) * h * 0.45f;
            float tw = 0.5f + 0.5f * std::sin(t * 2.0f + i);
            DrawCircle((int)sx, (int)sy, 1.0f + hash01(i + 9) * 1.5f, Fade(WHITE, 0.3f + 0.6f * tw));
        }
    } else if (e == 4) {
        // Soft floating bubbles, Wii-menu style.
        for (int i = 0; i < 18; ++i) {
            float sx = std::fmod(hash01(i) * w * 1.3f - cam.focusX() * 4.0f + w * 10.0f, (float)w);
            float sy = std::fmod(hash01(i + 77) * h - t * (12.0f + 10.0f * hash01(i + 3)) + h * 10.0f, (float)h);
            float r = 12.0f + hash01(i + 31) * 40.0f;
            DrawCircle((int)sx, (int)sy, r, Fade(WHITE, 0.35f));
            DrawCircleLines((int)sx, (int)sy, r, Fade(L.hudAccent, 0.25f));
        }
    }
}

void Renderer::drawBackdrop3D(const Game& game) {
    const int e = game.era.current();
    const EraLook& L = look(e);
    const float cx = game.camera.focusX();

    // A floor stretching back to the horizon, behind the playfield, and a deep floor under
    // everything so the sky never shows through below the level.
    DrawPlane({cx, -6.0f, 0.0f}, {400.0f, 120.0f}, shade(L.groundTop, e == 3 ? 0.35f : 0.6f));
    DrawPlane({cx, 1.9f, -22.0f}, {400.0f, 40.0f}, shade(L.groundTop, e == 3 ? 0.55f : 0.85f));

    int k0 = (int)std::floor((cx - 90.0f) / 16.0f);
    for (int k = k0; k < k0 + 14; ++k) {
        float x = k * 16.0f + hash01(k) * 8.0f;
        float ht = 9.0f + hash01(k + 40) * 9.0f;
        Vector3 base = {x, 1.9f, -30.0f - hash01(k + 80) * 10.0f};
        if (e == 2) {
            // Low-poly N64 mountains: five sides, flat colours.
            DrawCylinder(base, 0.0f, ht * 0.7f, ht, 5, L.hill);
            DrawCylinderWires(base, 0.0f, ht * 0.7f, ht, 5, shade(L.hill, 0.7f));
            DrawCylinder({base.x, base.y + ht * 0.72f, base.z}, 0.0f, ht * 0.2f, ht * 0.28f, 5, WHITE);   // snowcap
        } else if (e == 3) {
            DrawCylinder(base, 0.0f, ht * 0.7f, ht * 1.2f, 12, L.hill);
            DrawCylinder({base.x + 6.0f, base.y, base.z + 6.0f}, 0.0f, 0.9f, 3.5f, 8, shade(L.pipe, 0.6f));   // pine
        } else {
            DrawSphereEx({base.x, base.y - ht * 0.3f, base.z + 8.0f}, ht * 0.7f, 10, 16, L.hill);
            DrawSphereEx({base.x + 7.0f, base.y + 6.0f + hash01(k) * 3.0f, base.z - 6.0f}, 1.6f, 8, 12, Fade(WHITE, 0.9f));
        }
    }
}

void Renderer::drawWorld(const Game& game) {
    const int e = game.era.current();
    const EraLook& L = look(e);
    const GameCamera& cam = game.camera;
    const Level& level = game.level;
    const float t = (float)GetTime();
    const float margin = e >= 2 ? 8.0f : 2.0f;
    const float groundDepth = e >= 2 ? 3.0f : 1.0f;

    int x0 = (int)std::floor(cam.leftEdge() - margin);
    int x1 = (int)std::ceil(cam.rightEdge() + margin);
    for (int y = -1; y <= 14; ++y) {
        for (int x = x0; x <= x1; ++x) {
            char tile = level.tileAt(x, y);
            if (tile == '.' || tile == 'H' || tile == 'V') continue;   // hidden things stay hidden
            float off = level.bumpOffset(x, y);
            Vector3 c = {x + 0.5f, y + 0.5f + off, 0.0f};
            bool topOpen = !isSolidLook(level.tileAt(x, y + 1));
            switch (tile) {
                case '#': case 'X': case '~': case '=': {
                    Color col = tile == '=' ? shade(L.ground, 0.8f) : L.ground;
                    if (tile == '~') c.x += std::sin(t * 90.0f) * 0.06f;   // about to go
                    bool grass = e >= 1 && topOpen;
                    block(c, 1.0f, 1.0f, groundDepth, col, e, !grass);
                    if (grass) DrawCube({c.x, y + 0.95f, 0.0f}, 1.0f, 0.22f, groundDepth + 0.06f, L.groundTop);
                    break;
                }
                case '%':
                    block(c, 1.0f, 1.0f, 1.0f, L.brick, e);
                    DrawCube({c.x, c.y, 0.505f}, 1.0f, 0.05f, 0.01f, shade(L.brick, 0.5f));
                    DrawCube({c.x, c.y + 0.25f, 0.505f}, 0.05f, 0.5f, 0.01f, shade(L.brick, 0.5f));
                    DrawCube({c.x - 0.25f, c.y - 0.25f, 0.505f}, 0.05f, 0.5f, 0.01f, shade(L.brick, 0.5f));
                    break;
                case '?': case '!': {
                    // Both look identical. That is the point.
                    float pulse = e >= 1 ? 0.9f + 0.1f * std::sin(t * 5.0f) : 1.0f;
                    block(c, 1.0f, 1.0f, 1.0f, shade(L.block, pulse), e);
                    break;
                }
                case 'U': block(c, 1.0f, 1.0f, 1.0f, L.used, e); break;
                case '|': {
                    bool capTile = level.tileAt(x, y + 1) != '|';
                    if (e >= 2) DrawCylinder({c.x, (float)y, 0.0f}, 0.48f, 0.48f, 1.0f, e == 2 ? 6 : 16, L.pipe);
                    else block(c, 1.0f, 1.0f, 1.0f, L.pipe, e);
                    if (capTile) block({c.x, y + 0.82f, 0.0f}, 1.12f, 0.36f, 1.12f, shade(L.pipe, 1.15f), e);
                    break;
                }
                case '1': case '2': {
                    Color col = tile == '1' ? L.p1Cap : L.p2Cap;
                    block(c, 1.0f, 1.0f, 1.0f, Fade(col, 0.75f), e);
                    break;
                }
                case '^':
                    for (int i = 0; i < 3; ++i)
                        DrawCylinder({x + 0.17f + i * 0.33f, (float)y, 0.0f}, 0.0f, 0.16f, 0.65f, e == 2 ? 4 : 8, L.spike);
                    break;
                default: break;
            }
        }
    }

    // Coins.
    const int coinSides = e == 0 ? 6 : e == 2 ? 8 : 16;
    for (const Coin& c : game.enemies.coins)
        drawCoin({c.pos.x, c.pos.y + std::sin(t * 3.0f + c.pos.x) * 0.05f}, t * 3.0f + c.pos.x, L.coin, coinSides);

    // Enemies.
    for (const Enemy& en : game.enemies.enemies) {
        if (!en.alive) continue;
        Vector2 hs = EnemyManager::halfSize(en);
        float squash = en.stomped ? 0.3f : 1.0f;
        Vector3 c = {en.pos.x, en.pos.y - hs.y + hs.y * squash, 0.0f};
        switch (en.type) {
            case 'S': {
                float step = en.awake ? std::sin(en.t * 12.0f) * 0.08f : 0.0f;
                block(c, hs.x * 2.0f, hs.y * 2.0f * squash, 0.8f, L.enemy, e);
                DrawCube({c.x - 0.22f + step, en.pos.y - hs.y + 0.06f, 0.0f}, 0.3f, 0.12f, 0.6f, BLACK);
                DrawCube({c.x + 0.22f - step, en.pos.y - hs.y + 0.06f, 0.0f}, 0.3f, 0.12f, 0.6f, BLACK);
                if (!en.stomped) drawEyes({c.x - 0.05f, c.y + 0.1f, 0.41f}, 0.15f, 0.12f, false);
                break;
            }
            case 'G': {
                block(c, hs.x * 2.0f, hs.y * 2.0f * squash, 0.7f, L.flyer, e);
                float flap = std::sin(en.t * 20.0f) * 0.15f;
                DrawCube({c.x + 0.2f, c.y + 0.35f + flap, -0.1f}, 0.4f, 0.12f, 0.3f, WHITE);
                DrawCube({c.x + 0.25f, c.y + 0.25f - flap, 0.2f}, 0.4f, 0.12f, 0.3f, WHITE);
                if (!en.stomped) drawEyes({c.x - 0.1f, c.y + 0.05f, 0.36f}, 0.12f, 0.1f, false);
                break;
            }
            case 'B': {
                Color col = en.flash > 0.0f ? WHITE : L.brute;
                block(c, hs.x * 2.0f, hs.y * 2.0f * squash, 1.2f, col, e);
                if (!en.stomped) {
                    for (int i = -1; i <= 1; ++i)
                        DrawCylinder({c.x + i * 0.45f, c.y + hs.y, 0.0f}, 0.0f, 0.18f, 0.4f, 6, L.spike);
                    drawEyes({c.x - 0.15f, c.y + 0.25f, 0.61f}, 0.25f, 0.18f, true);
                }
                break;
            }
            case 'Q':
                if (!en.awake) {
                    // Exactly the same as a real coin. Trust nothing.
                    drawCoin({en.pos.x, en.pos.y + std::sin(t * 3.0f + en.pos.x) * 0.05f}, t * 3.0f + en.pos.x, L.coin, coinSides);
                } else {
                    block(c, hs.x * 2.0f, hs.y * 2.0f * squash, 0.5f, L.coin, e);
                    if (!en.stomped) {
                        drawEyes({c.x, c.y + 0.12f, 0.26f}, 0.13f, 0.1f, true);
                        DrawCube({c.x, c.y - 0.18f, 0.26f}, 0.45f, 0.1f, 0.02f, WHITE);   // teeth
                    }
                }
                break;
            default: break;
        }
    }

    // Flagpoles.
    for (const Flag& f : game.enemies.flags) {
        float base = f.baseY;
        block({f.x, base + 0.5f, 0.0f}, 1.0f, 1.0f, 1.0f, L.used, e);
        DrawCube({f.x, base + 4.75f, 0.0f}, 0.12f, 7.5f, 0.12f, e == 0 ? L.pipe : LIGHTGRAY);
        DrawSphereEx({f.x, base + 8.6f, 0.0f}, 0.22f, 6, 8, L.coin);
        float fy = base + 8.0f - f.dropT * 6.5f;
        DrawCube({f.x - 0.5f, fy, 0.0f}, 0.85f, 0.6f, 0.05f, e == 3 ? L.hudAccent : WHITE);
        DrawCube({f.x - 0.5f, fy, 0.03f}, 0.3f, 0.3f, 0.02f, L.pipe);
    }

    // Heroes.
    for (int p = 0; p < 2; ++p) {
        const Player& pl = game.players[p];
        if (!pl.active) continue;
        drawHero(pl, p == 0 ? L.p1Cap : L.p2Cap, L, e);
    }

    // Broken bits and coins popping out of blocks.
    for (const Debris& d : level.debris()) {
        Color col = d.kind == '%' ? L.brick : L.ground;
        DrawCube({d.pos.x, d.pos.y, 0.0f}, 0.3f, 0.3f, 0.3f, col);
    }
    for (const Popup& p : game.enemies.popups) {
        if (!p.text.empty() || p.t > 0.5f) continue;
        float rise = std::sin(p.t / 0.5f * PI) * 2.0f;
        drawCoin({p.pos.x, p.pos.y + rise}, p.t * 25.0f, L.coin, coinSides);
    }
}

void Renderer::drawOverlayText(const Game& game, int w, int h) {
    const int e = game.era.current();
    const EraLook& L = look(e);
    const Camera3D& cam = game.camera.cam;
    const float scale = h / game.camera.viewHeight;
    const float margin = e >= 2 ? 8.0f : 2.0f;

    // "?" marks on coin blocks (and on troll blocks, which is the joke).
    int x0 = (int)std::floor(game.camera.leftEdge() - margin);
    int x1 = (int)std::ceil(game.camera.rightEdge() + margin);
    int size = std::max(8, (int)(scale * 0.8f));
    for (int y = 0; y <= 14; ++y) {
        for (int x = x0; x <= x1; ++x) {
            char t = game.level.tileAt(x, y);
            if (t != '?' && t != '!') continue;
            Vector3 front = {x + 0.5f, y + 0.5f + game.level.bumpOffset(x, y), 0.5f};
            Vector2 s = GetWorldToScreenEx(front, cam, w, h);
            int tw = MeasureText("?", size);
            DrawText("?", (int)s.x - tw / 2 + 1, (int)s.y - size / 2 + 1, size, Fade(BLACK, 0.4f));
            DrawText("?", (int)s.x - tw / 2, (int)s.y - size / 2, size, shade(L.used, 0.8f));
        }
    }

    // Score and taunt popups.
    int psize = std::max(8, (int)(scale * 0.5f));
    for (const Popup& p : game.enemies.popups) {
        if (p.text.empty()) continue;
        Vector2 s = GetWorldToScreenEx({p.pos.x, p.pos.y + p.t * 1.5f, 0.0f}, cam, w, h);
        int tw = MeasureText(p.text.c_str(), psize);
        float a = std::min(1.0f, (0.9f - p.t) * 3.0f);
        DrawText(p.text.c_str(), (int)s.x - tw / 2 + 1, (int)s.y + 1, psize, Fade(L.hudShadow, a));
        DrawText(p.text.c_str(), (int)s.x - tw / 2, (int)s.y, psize, Fade(L.hudText, a));
    }
}

void Renderer::postEffects(const Game& game, int w, int h) {
    const int e = game.era.current();
    const float t = (float)GetTime();
    if (e == 0) {
        for (int y = 0; y < h; y += 3) DrawRectangle(0, y, w, 1, Fade(BLACK, 0.18f));   // CRT scanlines
    } else if (e == 3) {
        int vw = w / 6, vh = h / 5;
        DrawRectangleGradientH(0, 0, vw, h, Fade(BLACK, 0.45f), BLANK);
        DrawRectangleGradientH(w - vw, 0, vw, h, BLANK, Fade(BLACK, 0.45f));
        DrawRectangleGradientV(0, 0, w, vh, Fade(BLACK, 0.35f), BLANK);
        DrawRectangleGradientV(0, h - vh, w, vh, BLANK, Fade(BLACK, 0.45f));
    } else if (e == 4) {
        DrawRectangleGradientV(0, 0, w, h / 3, Fade(WHITE, 0.35f), BLANK);   // soft bloom
        for (int i = 0; i < 40; ++i) {
            float sx = hash01(i * 3) * w;
            float sy = hash01(i * 3 + 1) * h;
            float tw = std::sin(t * 3.0f + i * 1.7f);
            if (tw <= 0.3f) continue;
            float r = 2.0f + tw * 3.0f;
            DrawCircle((int)sx, (int)sy, r, Fade(WHITE, tw * 0.8f));
            DrawRectangle((int)(sx - r * 2), (int)sy, (int)(r * 4), 1, Fade(WHITE, tw * 0.6f));
            DrawRectangle((int)sx, (int)(sy - r * 2), 1, (int)(r * 4), Fade(WHITE, tw * 0.6f));
        }
    }
}
