#include "render/Renderer.h"
#include "core/Game.h"
#include "core/Paths.h"
#include "render/ArtSpec.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace art;

namespace {

const unsigned PALETTE16[16] = {0x0f0d1a, 0x26305e, 0x6a2c6e, 0x1e8a4c, 0x8c4a2f, 0x5a5468, 0xb8b6c8, 0xfaf3e6,
                                0xe8364f, 0xf28a1e, 0xf7d73a, 0x6fdc4a, 0x3c9cf0, 0x8a7fb4, 0xf28cb4, 0xf5c9a0};

// The world's y axis points down in tiles; 3D y points up with the floor at 0.
inline float Y3(float y) { return (float)LEVEL_H - y; }

struct Light { float x, y, r, g, b, radius; };
std::vector<Light> g_lights;
float g_lightAmount = 0;   // 0 in flat eras, 1 in 2.5D

Color lit(Color base, float face, float x, float y)
{
    if (g_lightAmount <= 0) return base;
    float r = 0.5f + 0.5f * face, gg = r, b = r;
    for (const auto& l : g_lights) {
        float d = std::hypot(l.x - x, l.y - y);
        if (d >= l.radius) continue;
        float k = (1 - d / l.radius);
        k *= k;
        r += l.r * k; gg += l.g * k; b += l.b * k;
    }
    auto mixk = [&](float k) { return 1.0f + (k - 1.0f) * g_lightAmount; };
    return Color{(unsigned char)std::min(255.0f, base.r * mixk(r)), (unsigned char)std::min(255.0f, base.g * mixk(gg)),
                 (unsigned char)std::min(255.0f, base.b * mixk(b)), base.a};
}

// A textured quad facing the camera (the XY plane) at depth z. (x, y) is the top-left in tiles.
void quad(const Texture2D& tex, Rectangle src, float x, float y, float w, float h, float z, Color tint, bool flip = false)
{
    float u0 = src.x / tex.width, u1 = (src.x + src.width) / tex.width;
    float v0 = src.y / tex.height, v1 = (src.y + src.height) / tex.height;
    if (flip) std::swap(u0, u1);
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    rlNormal3f(0, 0, 1);
    Color c0 = lit(tint, 1.0f, x, y), c1 = lit(tint, 1.0f, x, y + h), c2 = lit(tint, 1.0f, x + w, y + h), c3 = lit(tint, 1.0f, x + w, y);
    rlColor4ub(c0.r, c0.g, c0.b, c0.a); rlTexCoord2f(u0, v0); rlVertex3f(x, Y3(y), z);
    rlColor4ub(c1.r, c1.g, c1.b, c1.a); rlTexCoord2f(u0, v1); rlVertex3f(x, Y3(y + h), z);
    rlColor4ub(c2.r, c2.g, c2.b, c2.a); rlTexCoord2f(u1, v1); rlVertex3f(x + w, Y3(y + h), z);
    rlColor4ub(c3.r, c3.g, c3.b, c3.a); rlTexCoord2f(u1, v0); rlVertex3f(x + w, Y3(y), z);
    rlEnd();
    rlSetTexture(0);
}

void solidQuad(float x, float y, float w, float h, float z, Color c)
{
    rlSetTexture(rlGetTextureIdDefault());
    rlBegin(RL_QUADS);
    rlColor4ub(c.r, c.g, c.b, c.a);
    rlTexCoord2f(0, 0); rlVertex3f(x, Y3(y), z);
    rlTexCoord2f(0, 1); rlVertex3f(x, Y3(y + h), z);
    rlTexCoord2f(1, 1); rlVertex3f(x + w, Y3(y + h), z);
    rlTexCoord2f(1, 0); rlVertex3f(x + w, Y3(y), z);
    rlEnd();
    rlSetTexture(0);
}

// A tile as an extruded block. depth 0 draws just the front face (flat eras).
void tileBlock(const Texture2D& tex, Rectangle src, int tx, int ty, float depth, bool openTop, bool openLeft,
               bool openRight, Color tint)
{
    float x = (float)tx, y = (float)ty;
    float zf = depth * 0.5f, zb = -depth * 1.5f;
    quad(tex, src, x, y, 1, 1, zf, tint);
    if (depth <= 0.01f) return;
    float u0 = src.x / tex.width, u1 = (src.x + src.width) / tex.width;
    float v0 = src.y / tex.height, vTop = (src.y + src.height * 0.25f) / tex.height, v1 = (src.y + src.height) / tex.height;
    rlSetTexture(tex.id);
    rlBegin(RL_QUADS);
    if (openTop) {
        Color c = lit(tint, 1.15f, x + 0.5f, y);
        rlColor4ub(c.r, c.g, c.b, c.a);
        rlTexCoord2f(u0, v0); rlVertex3f(x, Y3(y), zb);
        rlTexCoord2f(u0, vTop); rlVertex3f(x, Y3(y), zf);
        rlTexCoord2f(u1, vTop); rlVertex3f(x + 1, Y3(y), zf);
        rlTexCoord2f(u1, v0); rlVertex3f(x + 1, Y3(y), zb);
    }
    if (openLeft) {
        Color c = lit(tint, 0.55f, x, y + 0.5f);
        rlColor4ub(c.r, c.g, c.b, c.a);
        rlTexCoord2f(u0, v0); rlVertex3f(x, Y3(y), zb);
        rlTexCoord2f(u0, v1); rlVertex3f(x, Y3(y + 1), zb);
        rlTexCoord2f(u1, v1); rlVertex3f(x, Y3(y + 1), zf);
        rlTexCoord2f(u1, v0); rlVertex3f(x, Y3(y), zf);
    }
    if (openRight) {
        Color c = lit(tint, 0.45f, x + 1, y + 0.5f);
        rlColor4ub(c.r, c.g, c.b, c.a);
        rlTexCoord2f(u0, v0); rlVertex3f(x + 1, Y3(y), zf);
        rlTexCoord2f(u0, v1); rlVertex3f(x + 1, Y3(y + 1), zf);
        rlTexCoord2f(u1, v1); rlVertex3f(x + 1, Y3(y + 1), zb);
        rlTexCoord2f(u1, v0); rlVertex3f(x + 1, Y3(y), zb);
    }
    rlEnd();
    rlSetTexture(0);
}

// Soft round shadow lying on the floor (2.5D only).
void shadow(float cx, float floorY, float r, float alpha)
{
    if (alpha <= 0.01f) return;
    rlSetTexture(rlGetTextureIdDefault());
    rlBegin(RL_TRIANGLES);
    const int N = 16;
    for (int i = 0; i < N; ++i) {
        float a0 = i * 6.2831853f / N, a1 = (i + 1) * 6.2831853f / N;
        rlColor4ub(0, 0, 0, (unsigned char)(alpha * 120));
        rlVertex3f(cx, Y3(floorY) + 0.02f, -0.3f);
        rlColor4ub(0, 0, 0, 0);
        rlVertex3f(cx + std::cos(a1) * r, Y3(floorY) + 0.02f, -0.3f + std::sin(a1) * r * 0.6f);
        rlVertex3f(cx + std::cos(a0) * r, Y3(floorY) + 0.02f, -0.3f + std::sin(a0) * r * 0.6f);
    }
    rlEnd();
    rlSetTexture(0);
}

void ring(float cx, float cy, float r, Color c, float z = 0.05f)
{
    rlBegin(RL_LINES);
    rlColor4ub(c.r, c.g, c.b, c.a);
    const int N = 24;
    for (int i = 0; i < N; ++i) {
        float a0 = i * 6.2831853f / N, a1 = (i + 1) * 6.2831853f / N;
        rlVertex3f(cx + std::cos(a0) * r, Y3(cy + std::sin(a0) * r), z);
        rlVertex3f(cx + std::cos(a1) * r, Y3(cy + std::sin(a1) * r), z);
    }
    rlEnd();
}

Color hexc(unsigned h, unsigned char a = 255) { return Color{(unsigned char)(h >> 16), (unsigned char)(h >> 8), (unsigned char)h, a}; }

}  // namespace

// ------------------------------------------------------------------ setup
void Renderer::init()
{
    post_ = LoadShader(nullptr, assetPath("shaders/post.fs").c_str());
    world_ = LoadShader(nullptr, assetPath("shaders/world.fs").c_str());
    locEra_ = GetShaderLocation(post_, "era");
    locTime_ = GetShaderLocation(post_, "time");
    locSize_ = GetShaderLocation(post_, "texSize");
    locFlash_ = GetShaderLocation(post_, "flash");
    locOut_ = GetShaderLocation(post_, "outSize");
    locMosaic_ = GetShaderLocation(post_, "mosaic");
    locGlitch_ = GetShaderLocation(post_, "glitch");
    locDesat_ = GetShaderLocation(post_, "desat");
    locPal_ = GetShaderLocation(post_, "palette");
    float pal[48];
    for (int i = 0; i < 16; ++i) {
        pal[i * 3 + 0] = ((PALETTE16[i] >> 16) & 255) / 255.0f;
        pal[i * 3 + 1] = ((PALETTE16[i] >> 8) & 255) / 255.0f;
        pal[i * 3 + 2] = (PALETTE16[i] & 255) / 255.0f;
    }
    SetShaderValueV(post_, locPal_, pal, SHADER_UNIFORM_VEC3, 16);
    ensureTargets();
}

void Renderer::ensureTargets()
{
    for (int e = 0; e < 4; ++e) {
        int h = e < 3 ? (int)(TILE_PX[e] * VIEW_TILES_H) : std::max(360, GetScreenHeight());
        int w = (int)std::lround(h * 16.0 / 9.0);
        if (rt_[e].id != 0 && rtW_[e] == w && rtH_[e] == h) continue;
        if (rt_[e].id != 0) UnloadRenderTexture(rt_[e]);
        rt_[e] = LoadRenderTexture(w, h);
        rtW_[e] = w;
        rtH_[e] = h;
        // Bilinear on every era: post.fs does "sharp" sampling, so pixel art stays crisp and even
        // at any scale, and the mosaic blocks of the transition sample cleanly.
        SetTextureFilter(rt_[e].texture, TEXTURE_FILTER_BILINEAR);
    }
}

void Renderer::shutdown()
{
    for (auto& r : rt_) if (r.id) UnloadRenderTexture(r);
    UnloadShader(post_);
    UnloadShader(world_);
}

Rectangle Renderer::screenRect() const
{
    float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    float w = sw, h = sw * 9.0f / 16.0f;
    if (h > sh) { h = sh; w = sh * 16.0f / 9.0f; }
    return Rectangle{(sw - w) / 2, (sh - h) / 2, w, h};
}

// ------------------------------------------------------------------ background
void Renderer::drawBackground(Game& g, int era, float camX, float pxPerTile, int w, int h, float alpha)
{
    const float parallax[4][4] = {{0, 0.15f, 0.15f, 0.15f}, {0, 0.2f, 0.2f, 0.2f}, {0, 0.06f, 0.18f, 0.4f}, {0, 0.06f, 0.18f, 0.4f}};
    for (int l = 0; l < BG_LAYERS; ++l) {
        const Texture2D& t = g.art.era[era].bg[l];
        float scale = (float)h / t.height;
        float tw = t.width * scale;
        float off = std::fmod(camX * pxPerTile * parallax[era][l], tw);
        if (off < 0) off += tw;
        for (float x = -off; x < w; x += tw)
            DrawTexturePro(t, Rectangle{0, 0, (float)t.width, (float)t.height}, Rectangle{x, 0, tw + 1, (float)h}, {0, 0}, 0,
                           Color{255, 255, 255, (unsigned char)(alpha * 255)});
    }
}

void Renderer::drawTitleBackground(Game& g, float t)
{
    ensureTargets();
    int e = ((int)(t / 3.0f)) % 4;
    renderEra_ = e;
    oldEra_ = -1;
    BeginTextureMode(rt_[e]);
    ClearBackground(BLACK);
    drawBackground(g, e, t * 3.0f, (float)TILE_PX[e] * (e == 3 ? rtH_[3] / (VIEW_TILES_H * TILE_PX[3]) : 1.0f), rtW_[e], rtH_[e], 1.0f);
    EndTextureMode();
}

// ------------------------------------------------------------------ world
void Renderer::drawWorld(Game& g)
{
    ensureTargets();
    oldEra_ = -1;
    float p = sweepProgress(g);
    if (p >= 0.0f && p < 1.0f && g.transFromEra != g.era) {
        oldEra_ = g.transFromEra;
        drawEra(g, oldEra_, 0.0f);   // the old world, still flat, behind the sweep
    }
    drawEra(g, g.era, g.cam.swing);
    renderEra_ = g.era;
}

float Renderer::sweepProgress(const Game& g)
{
    if (!g.transActive || !g.transSwitched) return -1.0f;
    return std::min(1.0f, std::max(0.0f, (g.transT - kSwitchAt) / kSweepTime));
}

void Renderer::drawEra(Game& g, int era, float swing)
{
    const EraArt& A = g.art.era[era];
    RenderTexture2D& rt = rt_[era];
    g.cam.aspect = (float)rtW_[era] / rtH_[era];
    float pxPerTile = rtH_[era] / g.cam.viewH;
    const float depth = swing;   // blocks extrude as the camera swings
    g_lightAmount = era == 3 ? swing : 0.0f;

    // lights: the players glow warm and cool, the boss eye glows
    g_lights.clear();
    g_lights.push_back({g.cam.x, 4, 0.12f, 0.1f, 0.05f, 40});   // soft fill
    for (int p = 0; p < 2; ++p)
        if (g.players[p].joined)
            g_lights.push_back({g.players[p].cx(), g.players[p].cy(), p == 0 ? 0.55f : 0.15f, p == 0 ? 0.32f : 0.3f,
                                p == 0 ? 0.1f : 0.6f, 6});
    if (g.boss.active) g_lights.push_back({g.boss.x, g.boss.y, 0.4f, 0.35f, 0.1f, 7});

    BeginTextureMode(rt);
    ClearBackground(era == 0 ? Color{16, 18, 22, 255} : BLACK);
    // the 1-bit background is drawn at full strength: its ordered patterns are the shading
    drawBackground(g, era, g.cam.x, pxPerTile, rtW_[era], rtH_[era], 1.0f);

    float nearP, farP;
    float savedSwing = g.cam.swing;
    g.cam.swing = swing;
    Camera3D c3 = g.cam.camera3D(nearP, farP);
    g.cam.swing = savedSwing;
    rlDrawRenderBatchActive();
    rlSetClipPlanes(nearP, farP);
    BeginMode3D(c3);
    BeginShaderMode(world_);
    if (depth > 0.02f) rlEnableDepthTest(); else rlDisableDepthTest();
    rlDisableBackfaceCulling();

    // ---- tiles
    int x0 = (int)std::floor(g.cam.left()) - 3, x1 = (int)std::ceil(g.cam.right()) + 3 + (int)(swing * 6);
    for (int tx = x0; tx <= x1; ++tx) {
        if (tx < g.level.firstCol()) continue;
        for (int ty = 0; ty < LEVEL_H; ++ty) {
            char ch = g.level.tile(tx, ty);
            if (ch == '.') continue;
            int frame = T_GROUND;
            bool above = g.level.isSolidChar(g.level.tile(tx, ty - 1));
            bool below = g.level.isSolidChar(g.level.tile(tx, ty + 1));
            if (ch == '#') frame = !above ? (below || ty >= LEVEL_H - 2 ? T_GROUND_TOP : T_BLOCK) : T_GROUND;
            else if (ch == '=') frame = T_CRACKED;
            else if (ch == '1') frame = T_P1;
            else if (ch == '2') frame = T_P2;
            else if (ch == '|') frame = T_WALL;
            Color tint = WHITE;
            if ((ch == '1' || ch == '2') && era != 0) {   // in 1-bit a pulse would only make the dither shimmer
                // a gentle pulse so the coloured memories read as "only for one of you"
                unsigned char a = (unsigned char)(200 + 55 * std::sin(g.time * 3 + tx));
                tint = Color{255, 255, 255, a};
            }
            auto open = [&](int dx, int dy) {
                char n = g.level.tile(tx + dx, ty + dy);
                return !(g.level.isSolidChar(n) || n == '1' || n == '2');
            };
            float d = (ch == '1' || ch == '2') ? depth * 0.6f : depth;
            tileBlock(A.tiles, frameRect(A.tiles, frame, 1, 1, era), tx, ty, d, open(0, -1), open(-1, 0), open(1, 0), tint);
        }
    }

    // ---- shadows (2.5D)
    if (era == 3) {
        auto floorBelow = [&](float x, float y) {
            for (int ty = (int)std::floor(y); ty < LEVEL_H; ++ty)
                if (g.level.isSolidChar(g.level.tile((int)std::floor(x), ty))) return (float)ty;
            return -1.0f;
        };
        for (int p = 0; p < 2; ++p) {
            const Player& pl = g.players[p];
            if (!pl.joined) continue;
            float fy = floorBelow(pl.cx(), pl.bottom());
            if (fy >= 0) shadow(pl.cx(), fy, 0.45f, swing * std::max(0.0f, 1 - (fy - pl.bottom()) / 6));
        }
        for (const auto& e : g.enemies) {
            float fy = floorBelow(e.cx(), e.y + e.h);
            if (fy >= 0 && !e.dead) shadow(e.cx(), fy, e.w * 0.6f, swing * std::max(0.0f, 1 - (fy - e.y - e.h) / 6));
        }
        if (g.boss.active) {
            float fy = floorBelow(g.boss.x, g.boss.y + g.boss.h / 2);
            if (fy >= 0) shadow(g.boss.x, fy, 1.6f, swing * 0.8f);
        }
    }

    // ---- pickups
    for (const auto& f : g.frags) {
        int fr = ((int)(f.t * 6 + f.x) % 2) ? T_FRAG1 : T_FRAG0;
        float s = 0.7f;
        unsigned char a = (!f.placed && f.t > 10) ? (unsigned char)(255 * std::max(0.0f, (12 - f.t) / 2)) : 255;
        quad(A.tiles, frameRect(A.tiles, fr, 1, 1, era), f.x - s / 2, f.y - s / 2, s, s, 0.1f, Color{255, 255, 255, a});
    }
    for (const auto& pu : g.powerups) {
        float bob = std::sin(pu.t * 3) * 0.12f;
        quad(A.tiles, frameRect(A.tiles, T_PU_SHIELD + pu.type, 1, 1, era), pu.x - 0.45f, pu.y - 0.45f + bob, 0.9f, 0.9f, 0.1f, WHITE);
        if ((int)(pu.t * 4) % 2 == 0) ring(pu.x, pu.y + bob, 0.6f, Color{255, 240, 180, 160});
    }

    // ---- enemies
    for (const auto& e : g.enemies) {
        const Texture2D* t = &A.blob;
        float fs = BLOB_FS;
        int fr = 0;
        if (e.type == EN_BLOB) {
            fr = e.dead ? BF_SQUASH : BF_WALK0 + ((int)(e.t * 6) % 4);
        } else if (e.type == EN_FLYER) {
            t = &A.flyer; fs = FLYER_FS;
            fr = (e.dead || e.hurt > 0) ? FF_HURT : FF_FLAP0 + ((int)(e.t * 10) % 4);
        } else {
            t = &A.brute; fs = BRUTE_FS;
            fr = (e.hurt > 0 || e.dead) ? RF_HURT : (e.state >= 1 ? RF_CHARGE : RF_WALK0 + ((int)(e.t * 5) % 4));
        }
        Color tint = WHITE;
        if (e.owner == 0) tint = Color{255, 175, 95, 255};
        if (e.owner == 1) tint = Color{120, 180, 255, 255};
        if (e.dead) tint.a = (unsigned char)(255 * std::max(0.0f, 1 - e.deadTime / 0.45f));
        float dx = e.cx() - fs / 2, dy = e.y + e.h - fs;
        if (e.type == EN_FLYER) dy = e.cy() - fs / 2;
        quad(*t, frameRect(*t, fr, fs, fs, era), dx, dy, fs, fs, 0.0f, tint, e.dir > 0 ? false : true);
        if (e.owner >= 0 && !e.dead) ring(e.cx(), e.cy(), fs * 0.6f, e.owner == 0 ? Color{255, 150, 60, 200} : Color{80, 160, 255, 200});
    }

    // ---- boss
    if (g.boss.active) {
        const Boss& b = g.boss;
        if (b.aiming()) {   // warning marker where it will land
            float k = b.state == 6 ? 0.5f + 0.5f * std::sin(g.time * 20) : 1.0f;
            Color wc = era == 0 ? Color{235, 235, 225, (unsigned char)(255 * k)} : Color{255, 70, 90, (unsigned char)(255 * k)};
            ring(b.toX, b.toY + b.h / 2 - 0.2f, 1.3f, wc);
            ring(b.toX, b.toY + b.h / 2 - 0.2f, 0.7f, wc);
            solidQuad(b.toX - 1.3f, b.toY + b.h / 2 - 0.15f, 2.6f, 0.12f, 0.3f, wc);
        }
        int fr = b.state == 5 || b.hurt > 0 ? XF_HURT : (b.state == 3 ? XF_SLAM : XF_FLOAT0 + ((int)(b.t * 4) % 2));
        Color tint = WHITE;
        if (b.state == 5) tint.a = (unsigned char)(255 * std::max(0.0f, 1 - b.stateTime / 1.6f));
        if (b.state == 3 && (int)(g.time * 8) % 2 == 0) tint = Color{255, 255, 200, 255};   // flashing = stomp me
        quad(A.boss, frameRect(A.boss, fr, BOSS_FS, BOSS_FS, era), b.x - BOSS_FS / 2, b.y - BOSS_FS / 2 - 0.2f, BOSS_FS, BOSS_FS, 0.0f, tint, true);
    }

    // ---- players
    for (int p = 0; p < 2; ++p) {
        const Player& pl = g.players[p];
        if (!pl.joined) continue;
        if (pl.invuln > 0 && !pl.faded && (int)(pl.invuln * 12) % 2 == 0) continue;   // blink
        int fr = pl.frame;
        if (p == 1 && g.slowTime > 2.6f) fr = PF_SPECIAL;
        Color tint = WHITE;
        float ox = 0;
        if (pl.faded) {
            tint = era == 0 ? Color{255, 255, 255, 120} : Color{150, 150, 165, 130};
            ox = std::sin(g.time * 6 + p) * 0.05f;
        }
        const Texture2D& t = A.player[p];
        quad(t, frameRect(t, fr, PLAYER_FW, PLAYER_FH, era), pl.cx() - PLAYER_FW / 2 + ox, pl.bottom() - PLAYER_FH + 0.03f,
             PLAYER_FW, PLAYER_FH, 0.05f + p * 0.01f, tint, pl.facing < 0);
        if (pl.shield) ring(pl.cx(), pl.cy(), 0.95f, Color{255, 120, 160, (unsigned char)(150 + 100 * std::sin(g.time * 5))});
        if (pl.faded && pl.reviveProgress > 0) ring(pl.cx(), pl.cy(), 1.1f * (1 - pl.reviveProgress / 0.6f) + 0.3f, Color{255, 255, 255, 230});
        if (pl.attackTime > 0) {
            float ax = pl.cx() + pl.facing * 1.0f;
            ring(ax, pl.cy(), 0.6f * (1 - pl.attackTime / 0.18f) + 0.2f, p == 0 ? Color{255, 200, 120, 255} : Color{140, 210, 255, 255});
        }
    }
    // time-slow pulse ring
    if (g.slowTime > 2.4f) {
        float k = (3.0f - g.slowTime) / 0.6f;
        ring(g.players[1].joined ? g.players[1].cx() : g.players[0].cx(), g.players[1].joined ? g.players[1].cy() : g.players[0].cy(),
             k * 14, Color{130, 230, 255, (unsigned char)(255 * (1 - k))});
    }

    // ---- particles
    rlDisableDepthMask();
    for (const auto& pa : g.particles.all()) {
        Color c = pa.color;
        c.a = (unsigned char)(c.a * std::min(1.0f, pa.life / (pa.maxLife * 0.5f)));
        solidQuad(pa.x - pa.size / 2, pa.y - pa.size / 2, pa.size, pa.size, 0.2f, c);
    }

    // ---- the Forgetting Wall: rows of static/fog with a wavy leading edge
    {
        const Texture2D& wt = A.wall;
        float left = g.cam.left() - 20;
        for (float y = -1.0f; y < LEVEL_H + 1; y += 0.5f) {
            float edge = g.wallX + std::sin(y * 1.7f + g.time * 3) * 0.35f + std::sin(y * 4.1f - g.time * 5) * 0.15f;
            if (edge < left) continue;
            // 1-bit static crawls slowly: fast full-screen static was the harshest flicker in era 0
            float scroll = std::fmod(g.time * (era == 0 ? 1.0f : 2.0f), 1.0f);
            Rectangle src{0, (y / WALL_TEX_H + scroll) * wt.height, (float)wt.width, wt.height * 0.5f / WALL_TEX_H};
            float wq = WALL_TEX_W;
            // the textured leading strip
            quad(wt, src, edge - wq, y, wq, 0.5f, 0.6f, WHITE);
            // the solid body behind it
            Color body = era == 0 ? Color{16, 18, 22, 255} : (era == 1 ? hexc(0x0f0d1a) : hexc(0x1a1030, era == 3 ? 235 : 255));
            if (edge - wq > left) solidQuad(left, y, edge - wq - left, 0.5f, 0.6f, body);
        }
        // glitch sparks at the edge
        if (era >= 1) {
            for (int i = 0; i < 6; ++i) {
                float yy = std::fmod(g.time * (3 + i) * 1.7f + i * 2.3f, (float)LEVEL_H);
                solidQuad(g.wallX + 0.2f + std::sin(g.time * 9 + i) * 0.4f, yy, 0.18f, 0.18f, 0.7f, hexc(i % 2 ? 0xe8364f : 0x8a7fb4, 200));
            }
        }
    }
    rlEnableDepthMask();
    EndShaderMode();
    EndMode3D();
    rlDisableDepthTest();
    rlSetClipPlanes(0.01, 1000.0);
    EndTextureMode();
}

// ------------------------------------------------------------------ to the screen
void Renderer::blitEra(Game& g, int e, Rectangle dst, float flash, float desat, float mosaic, float glitch)
{
    float t = g.time;
    float size[2] = {(float)rtW_[e], (float)rtH_[e]};
    float out[2] = {dst.width, dst.height};
    SetShaderValue(post_, locEra_, &e, SHADER_UNIFORM_INT);
    SetShaderValue(post_, locTime_, &t, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post_, locSize_, size, SHADER_UNIFORM_VEC2);
    SetShaderValue(post_, locOut_, out, SHADER_UNIFORM_VEC2);
    SetShaderValue(post_, locFlash_, &flash, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post_, locDesat_, &desat, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post_, locMosaic_, &mosaic, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post_, locGlitch_, &glitch, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(post_);
    DrawTexturePro(rt_[e].texture, Rectangle{0, 0, (float)rtW_[e], -(float)rtH_[e]}, dst, {0, 0}, 0, WHITE);
    EndShaderMode();
}

// The upgrade scanline: a hot white edge with a glow trailing behind it, and loose pixels
// the size of the old era's pixels breaking off and flying ahead of it.
void Renderer::drawSweep(Game& g, Rectangle dst, float p)
{
    const Color eraGlow[4] = {{235, 235, 225, 255}, {247, 215, 58, 255}, {242, 140, 180, 255}, {255, 220, 150, 255}};
    Color glow = eraGlow[std::min(3, std::max(0, g.era))];
    float x = dst.x + dst.width * p;
    int y0 = (int)dst.y, h = (int)dst.height;
    DrawRectangleGradientH((int)(x - 90), y0, 90, h, Color{glow.r, glow.g, glow.b, 0}, Color{glow.r, glow.g, glow.b, 110});
    DrawRectangleGradientH((int)x, y0, 28, h, Color{255, 255, 255, 170}, Color{255, 255, 255, 0});
    DrawRectangle((int)x - 2, y0, 4, h, WHITE);

    float px = oldEra_ >= 0 ? std::max(3.0f, dst.height / rtH_[oldEra_]) : 6.0f;   // one old-era pixel on screen
    for (int i = 0; i < 70; ++i) {
        float hy = std::fmod(std::sin(i * 91.7f) * 43758.5f, 1.0f);
        float hx = std::fmod(std::sin(i * 37.3f + 1.7f) * 24634.6f, 1.0f);
        hy = std::fabs(hy);
        hx = std::fabs(hx);
        float life = std::fmod(g.time * (1.5f + hx) + hx * 3.0f, 1.0f);   // each pixel loops along
        float sx = x + life * (40 + 160 * hx);
        float sy = dst.y + hy * dst.height + std::sin(g.time * 6 + i) * 6;
        if (sx > dst.x + dst.width) continue;
        unsigned char a = (unsigned char)(255 * (1 - life));
        Color c = (i % 3 == 0) ? Color{255, 255, 255, a} : Color{glow.r, glow.g, glow.b, a};
        DrawRectangle((int)sx, (int)sy, (int)px, (int)px, c);
    }
}

void Renderer::present(Game& g, float flash, float desat)
{
    Rectangle dst = screenRect();

    // Before the switch: the old era powers down, going blocky and torn.
    if (g.transActive && !g.transSwitched) {
        float k = std::min(1.0f, g.transT / kSwitchAt);
        blitEra(g, renderEra_, dst, flash, desat, 1.0f + k * k * 7.0f, k * 0.9f);
        return;
    }

    float p = sweepProgress(g);
    if (oldEra_ >= 0 && p >= 0.0f && p < 1.0f) {
        float e = p * p * (3 - 2 * p);   // ease in and out
        blitEra(g, oldEra_, dst, 0.0f, desat, 8.0f, 0.5f);
        float edge = dst.x + dst.width * e;
        BeginScissorMode((int)dst.x, (int)dst.y, std::max(0, (int)(edge - dst.x)), (int)dst.height);
        blitEra(g, renderEra_, dst, flash, desat, 1.0f + 3.0f * (1.0f - e), 0.0f);   // sharpens as it goes
        EndScissorMode();
        drawSweep(g, dst, e);
        return;
    }
    blitEra(g, renderEra_, dst, flash, desat, 1.0f, 0.0f);
    if (renderEra_ == 0 && g.screen != Screen::Title) drawPlayerTags(g, dst);
}

// 1-bit has no colours to tell the players apart, so each gets a small "P1" / "P2" tag above
// the head. Drawn on the screen, not in the world, so the 3 x 5 letters stay whole at any
// camera zoom; snapped to the LCD pixel grid and in the shader's two colours, so it still
// reads as part of the 1-bit screen. A player off the screen keeps the tag at the edge.
void Renderer::drawPlayerTags(Game& g, Rectangle dst)
{
    const Color light{198, 203, 183, 255}, dark{30, 35, 33, 255};   // post.fs era 0 pair
    static const char* P = "111" "101" "111" "100" "100";
    static const char* D[2] = {"010" "110" "010" "010" "111", "111" "001" "111" "100" "111"};
    float nearP, farP;
    float savedSwing = g.cam.swing;
    g.cam.swing = 0;
    g.cam.aspect = (float)rtW_[0] / rtH_[0];
    Camera3D c3 = g.cam.camera3D(nearP, farP);
    g.cam.swing = savedSwing;
    const float B = dst.height / rtH_[0];   // one 1-bit pixel on the screen
    int tx[2] = {0, 0}, ty[2] = {0, 0};
    for (int p = 0; p < 2; ++p) {
        const Player& pl = g.players[p];
        Vector2 v = GetWorldToScreenEx(Vector3{pl.cx(), Y3(pl.bottom() - PLAYER_FH), 0.05f}, c3, rtW_[0], rtH_[0]);
        tx[p] = (int)std::lround(v.x - 4.5f);   // box 9 x 7, pointer 2 below it
        ty[p] = (int)std::lround(v.y) - 10;
    }
    // players standing together: put the two tags side by side, P1 on the left
    if (g.players[0].joined && g.players[1].joined && std::abs(tx[0] - tx[1]) < 12 && std::abs(ty[0] - ty[1]) < 10) {
        int mid = (tx[0] + tx[1]) / 2;
        tx[0] = mid - 6;
        tx[1] = mid + 6;
        ty[0] = ty[1] = std::min(ty[0], ty[1]);
    }
    for (int p = 0; p < 2; ++p) {
        if (!g.players[p].joined) continue;
        int bx = std::max(1, std::min(rtW_[0] - 10, tx[p]));
        int by = std::max(1, std::min(rtH_[0] - 10, ty[p]));
        auto px = [&](int x, int y, int w, int h, Color c) {
            int x0 = (int)std::lround(dst.x + (bx + x) * B), y0 = (int)std::lround(dst.y + (by + y) * B);
            int x1 = (int)std::lround(dst.x + (bx + x + w) * B), y1 = (int)std::lround(dst.y + (by + y + h) * B);
            DrawRectangle(x0, y0, x1 - x0, y1 - y0, c);
        };
        px(-1, -1, 11, 9, dark);   // rim
        px(0, 0, 9, 7, light);
        px(3, 7, 3, 1, light);     // pointer
        px(4, 8, 1, 1, light);
        for (int r = 0; r < 5; ++r)
            for (int c = 0; c < 3; ++c) {
                if (P[r * 3 + c] == '1') px(1 + c, 1 + r, 1, 1, dark);
                if (D[p][r * 3 + c] == '1') px(5 + c, 1 + r, 1, 1, dark);
            }
    }
}
