#include "render/Renderer.h"
#include "core/Game.h"
#include "core/Paths.h"
#include "render/ArtSpec.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

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
    // flat face shading only: the top is a touch brighter, the sides darker. No glows.
    (void)x; (void)y;
    float r = 0.5f + 0.5f * face, gg = r, b = r;
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
        // a hard-edged flat shadow, like a sprite's drop shadow
        rlColor4ub(0, 0, 0, (unsigned char)(alpha * 90));
        rlVertex3f(cx, Y3(floorY) + 0.02f, -0.3f);
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
    haze_ = LoadShader(nullptr, assetPath("shaders/haze.fs").c_str());
    locHazeCol_ = GetShaderLocation(haze_, "hazeCol");
    locHazeAmt_ = GetShaderLocation(haze_, "hazeAmt");
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
    locShades_ = GetShaderLocation(post_, "shades");
    float sh[12];
    for (int i = 0; i < 4; ++i) {
        sh[i * 3 + 0] = ((SHADES4[i] >> 16) & 255) / 255.0f;
        sh[i * 3 + 1] = ((SHADES4[i] >> 8) & 255) / 255.0f;
        sh[i * 3 + 2] = (SHADES4[i] & 255) / 255.0f;
    }
    SetShaderValueV(post_, locShades_, sh, SHADER_UNIFORM_VEC3, 4);
    models_.init();
    ensureTargets();
}

void Renderer::ensureTargets()
{
    for (int e = 0; e < ERA_COUNT; ++e) {
        int h = lowRes(e) ? (int)(TILE_PX[e] * VIEW_TILES_H) : std::max(360, GetScreenHeight());
        int w = (int)std::lround(h * 16.0 / 9.0);
        if (lowRes(e)) {
            // Framing (ui helper): the pixel stages are scaled by the largest whole number that
            // fits the window height, and the buffer is made just wide enough that the scaled
            // image covers the whole window width. So no stage ever gets side bars; at most a
            // thin, centred band top and bottom where the height does not divide evenly.
            const int sw = std::max(1, GetScreenWidth()), sh = std::max(1, GetScreenHeight());
            const int k = std::max(1, sh / h);
            w = std::max(w / 2, (sw + k - 1) / k);
        }
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
    models_.shutdown();
    UnloadShader(post_);
    UnloadShader(world_);
    UnloadShader(haze_);
}

Rectangle Renderer::screenRect() const
{
    float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    int e = std::max(0, std::min(LAST_ERA, renderEra_));
    if (lowRes(e) && rtW_[e] > 0) {
        // pixel stages: scale the native buffer by a whole number, so every pixel is the same size
        // (ensureTargets sizes the buffer width so this fills the window width; see there)
        int k = std::max(1, (int)(sh / rtH_[e]));
        float w = (float)rtW_[e] * k, h = (float)rtH_[e] * k;
        return Rectangle{std::floor((sw - w) / 2), std::floor((sh - h) / 2), w, h};
    }
    float w = sw, h = sw * 9.0f / 16.0f;
    if (h > sh) { h = sh; w = sh * 16.0f / 9.0f; }
    return Rectangle{(sw - w) / 2, (sh - h) / 2, w, h};
}

// ------------------------------------------------------------------ background
void Renderer::drawBackground(Game& g, int era, float camX, float pxPerTile, int w, int h, float alpha, int layers)
{
    const float parallaxByStyle[4][4] = {{0, 0.15f, 0.15f, 0.15f}, {0, 0.2f, 0.2f, 0.2f}, {0, 0.06f, 0.18f, 0.4f}, {0, 0.06f, 0.18f, 0.4f}};
    const float* parallax = parallaxByStyle[STYLE[era]];
    for (int l = 0; l < std::min(layers, BG_LAYERS); ++l) {
        const Texture2D& t = g.art.era[era].bg[l];
        float scale = (float)h / t.height;
        float tw = t.width * scale;
        float off = std::fmod(camX * pxPerTile * parallax[l], tw);
        if (off < 0) off += tw;
        off = std::floor(off);   // whole pixels only
        // 1-bit: exactly one texel per native pixel (the +1 overlap would stretch 312 texels
        // over 313 pixels and double a column)
        float dw = era == 0 ? tw : tw + 1;
        for (float x = -off; x < w; x += tw)
            DrawTexturePro(t, Rectangle{0, 0, (float)t.width, (float)t.height}, Rectangle{x, 0, dw, (float)h}, {0, 0}, 0,
                           Color{255, 255, 255, (unsigned char)(alpha * 255)});
    }
}

// The 2.5D and 3D stages: only the sky is a flat picture. The other backdrop layers stand in
// the scene as big cut-outs at real depths, on the lawn, so everything shares one perspective
// and parallax comes from the camera itself. Further layers are hazed towards the horizon.
void Renderer::drawBackdrop3D(Game& g, int era, const Camera3D& cam)
{
    struct Layer { int l; float z, height, baseFrac, haze; };
    // baseFrac: how far up the picture its ground line sits (that part goes below the lawn)
    const Layer dusk[3] = {{1, -64.0f, 24.0f, 0.0f, 0.6f}, {2, -38.0f, 11.0f, 0.16f, 0.45f}, {3, -20.0f, 6.5f, 0.14f, 0.3f}};
    const Layer day[1] = {{1, -100.0f, 40.0f, 0.0f, 0.35f}};
    const Layer* L = era == LAST_ERA ? day : dusk;
    int n = era == LAST_ERA ? 1 : 3;
    Vector3 horizon = era == LAST_ERA ? Vector3{0.86f, 0.92f, 0.97f} : Vector3{0.97f, 0.76f, 0.58f};
    rlDrawRenderBatchActive();
    BeginShaderMode(haze_);
    SetShaderValue(haze_, locHazeCol_, &horizon, SHADER_UNIFORM_VEC3);
    const float ground = 2.0f;   // 3D y of the path top
    for (int i = 0; i < n; ++i) {
        const Texture2D& t = g.art.era[era].bg[L[i].l];
        if (t.id == 0) continue;
        rlDrawRenderBatchActive();
        SetShaderValue(haze_, locHazeAmt_, &L[i].haze, SHADER_UNIFORM_FLOAT);
        float h = L[i].height, tw = h * t.width / t.height;
        float half = (cam.position.z - L[i].z) * 1.1f + 12.0f;
        float y0 = ground - 0.2f - L[i].baseFrac * h;   // world-down y of the picture top below
        // quad() takes the top-left in level tiles (y down)
        float top = (float)LEVEL_H - (y0 + h);
        float start = std::floor((cam.target.x - half) / tw) * tw;
        for (float x = start; x < cam.target.x + half; x += tw)
            quad(t, Rectangle{0, 0, (float)t.width, (float)t.height}, x, top, tw + 0.02f, h, L[i].z, WHITE);
    }
    rlDrawRenderBatchActive();
    EndShaderMode();
}

// 2.5D: flat lawn in front of and behind the path, out to the backdrop, in flat bands that step
// towards the dusk sky colour with distance. A ditch floor under the pits and a bank behind them.
void Renderer::drawLawns25D(Game& g)
{
    auto mix = [](unsigned a, unsigned b, float t) {
        auto ch = [&](int s) { return (unsigned char)(((a >> s) & 255) + ((((int)(b >> s) & 255) - (int)((a >> s) & 255)) * t)); };
        return Color{ch(16), ch(8), ch(0), 255};
    };
    const unsigned grass = 0x4f8a42, dusk = 0xf0b48a;
    float l = g.cam.left() - 80, r = g.cam.right() + 80;
    auto flat = [&](float z0, float z1, float y3, Color c) {   // a horizontal quad at 3D height y3
        rlSetTexture(rlGetTextureIdDefault());
        rlBegin(RL_QUADS);
        rlColor4ub(c.r, c.g, c.b, c.a);
        rlTexCoord2f(0, 0); rlVertex3f(l, y3, z0);
        rlTexCoord2f(0, 1); rlVertex3f(l, y3, z1);
        rlTexCoord2f(1, 1); rlVertex3f(r, y3, z1);
        rlTexCoord2f(1, 0); rlVertex3f(r, y3, z0);
        rlEnd();
    };
    flat(40.0f, -1.5f, 0.0f, mix(grass, 0x203018, 0.25f));                 // the low lawn in front, and the ditch floor
    // the bank behind the ditches: a vertical face at the back of the path
    rlSetTexture(rlGetTextureIdDefault());
    rlBegin(RL_QUADS);
    Color bank = hexc(0x5e3a2a);
    rlColor4ub(bank.r, bank.g, bank.b, 255);
    rlTexCoord2f(0, 0); rlVertex3f(l, 2.0f, -1.5f);
    rlTexCoord2f(0, 1); rlVertex3f(l, 0.0f, -1.5f);
    rlTexCoord2f(1, 1); rlVertex3f(r, 0.0f, -1.5f);
    rlTexCoord2f(1, 0); rlVertex3f(r, 2.0f, -1.5f);
    rlEnd();
    const float bandZ[5] = {-1.5f, -8.0f, -20.0f, -36.0f, -70.0f};
    for (int i = 0; i < 4; ++i) flat(bandZ[i], bandZ[i + 1], 1.98f, mix(grass, dusk, 0.12f + 0.16f * i));
    rlSetTexture(0);
}

void Renderer::drawTitleBackground(Game& g, float t)
{
    ensureTargets();
    int e = ((int)(t / 3.0f)) % ERA_COUNT;
    renderEra_ = e;
    oldEra_ = -1;
    BeginTextureMode(rt_[e]);
    ClearBackground(BLACK);
    drawBackground(g, e, t * 3.0f, (float)TILE_PX[e] * (smooth(e) ? rtH_[e] / (VIEW_TILES_H * TILE_PX[e]) : 1.0f), rtW_[e], rtH_[e], 1.0f);
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
    g_lightAmount = smooth(era) ? std::min(1.0f, swing) : 0.0f;
    const bool full3D = era == LAST_ERA;

    // A sprite quad. In 1-bit it lands on whole native pixels and gets a 1 px black halo
    // (dark silhouettes offset left, right and up, drawn first), so it always reads over the line-drawn scenery.
    auto sprite = [&](const Texture2D& tex, Rectangle src, float x, float y, float w, float h, float z, Color tint, bool flip) {
        if (era != 0) { quad(tex, src, x, y, w, h, z, tint, flip); return; }
        const float px = 1.0f / TILE_PX[0];
        x = std::round(x / px) * px;
        y = std::round(y / px) * px;
        Color halo{16, 18, 22, tint.a};
        // left, right and above only: a halo below would cut the ground line under their feet
        const float ox[3] = {px, -px, 0}, oy[3] = {0, 0, -px};
        for (int k = 0; k < 3; ++k) quad(tex, src, x + ox[k], y + oy[k], w, h, z, halo, flip);
        quad(tex, src, x, y, w, h, z, tint, flip);
    };

    // lights: the players glow warm and cool, the boss eye glows
    g_lights.clear();
    g_lights.push_back({g.cam.x, 4, 0.12f, 0.1f, 0.05f, 40});   // soft fill
    for (int p = 0; p < 2; ++p)
        if (g.players[p].joined)
            g_lights.push_back({g.players[p].cx(), g.players[p].cy(), p == 0 ? 0.55f : 0.15f, p == 0 ? 0.32f : 0.3f,
                                p == 0 ? 0.1f : 0.6f, 6});
    if (g.boss.active) g_lights.push_back({g.boss.x, g.boss.y, 0.4f, 0.35f, 0.1f, 7});

    BeginTextureMode(rt);
    ClearBackground(era == 0 ? Color{16, 18, 22, 255} : (era == 1 ? hexc(SHADES4[3]) : BLACK));
    // the 1-bit background is drawn at full strength: its ordered patterns are the shading
    drawBackground(g, era, g.cam.x, pxPerTile, rtW_[era], rtH_[era], 1.0f, smooth(era) ? 1 : BG_LAYERS);

    float nearP, farP;
    float savedSwing = g.cam.swing;
    g.cam.swing = swing;
    Camera3D c3 = g.cam.camera3D(nearP, farP);
    g.cam.swing = savedSwing;
    if (lowRes(era) && c3.projection == CAMERA_ORTHOGRAPHIC) {
        // snap the camera to the native pixel grid, so sprites never land between pixels
        float tp = (float)TILE_PX[era];
        c3.target.x = std::round(c3.target.x * tp) / tp;
        c3.target.y = std::round(c3.target.y * tp) / tp;
        c3.position.x = c3.target.x;
        c3.position.y = c3.target.y;
    }
    rlDrawRenderBatchActive();
    rlSetClipPlanes(nearP, farP);
    BeginMode3D(c3);
    BeginShaderMode(world_);
    if (depth > 0.02f) rlEnableDepthTest(); else rlDisableDepthTest();
    rlDisableBackfaceCulling();

    if (smooth(era)) {
        drawBackdrop3D(g, era, c3);
        BeginShaderMode(world_);
        if (!full3D) {
            drawLawns25D(g);
            // the meadow's modelled props: hay, stooks, hedge, track, flowers, grass (Models3D)
            rlDrawRenderBatchActive();
            models_.begin(g, c3);
            models_.drawMeadow(g);
            rlDrawRenderBatchActive();
            BeginShaderMode(world_);
        }
    }

    // ---- tiles
    // The ortho view is centred on the snapped camera. When the buffer is an odd number of
    // pixels across (313 in 4-shade and 8-bit, 391 in 16-bit) that centre is mid-pixel, so every
    // tile edge lands exactly on a pixel centre and every texel lookup sits on a texel edge.
    // Float error then tips some tiles one texel left: their first column reads the previous
    // atlas frame (or, by wrap, the sheet's transparent last column), the light seams through
    // the terrain. Draw the tile grid half a native pixel to the left: its edges fall between
    // pixels and each pixel reads a texel centre. The tiles cover the same pixels as before.
    const bool tileHalfPx = lowRes(era) && rtW_[era] % 2 == 1;   // every buffer height is even
    if (tileHalfPx) {
        rlPushMatrix();
        rlTranslatef(-0.5f / TILE_PX[era], 0, 0);
    }
    auto tileAt = [&](int x, int y) {
        if (x < g.level.firstCol() && smooth(era)) return y >= LEVEL_H - 2 && y < LEVEL_H ? '#' : '.';
        return g.level.tile(x, y);
    };
    int x0 = (int)std::floor(g.cam.left()) - 3, x1 = (int)std::ceil(g.cam.right()) + 3 + (int)(swing * 6);
    for (int tx = x0; tx <= x1; ++tx) {
        // left of the first column: nothing in the flat stages, plain ground once the camera turns
        if (tx < g.level.firstCol() && !smooth(era)) continue;
        for (int ty = 0; ty < LEVEL_H; ++ty) {
            char ch = tileAt(tx, ty);
            if (ch == '.') continue;
            if (full3D) continue;   // the 3D stage models its blocks (Models3D::drawGround)
            int frame = T_GROUND;
            bool above = g.level.isSolidChar(tileAt(tx, ty - 1));
            bool below = g.level.isSolidChar(tileAt(tx, ty + 1));
            if (ch == '#') frame = !above ? (below || ty >= LEVEL_H - 2 ? T_GROUND_TOP : T_BLOCK) : T_GROUND;
            else if (ch == '=') frame = T_CRACKED;
            else if (ch == '1') frame = T_P1;
            else if (ch == '2') frame = T_P2;
            else if (ch == '|') frame = T_WALL;
            // earth: a variant per tile from a hash of its position (so no stamp repeats on a
            // beat), and a buried keepsake two tiles wide in the bottom row every few screens
            if (frame == T_GROUND || frame == T_GROUND_TOP) {
                auto hashXY = [](int x, int y) {
                    unsigned h = (unsigned)x * 73856093u ^ (unsigned)y * 19349663u;
                    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
                    return h;
                };
                auto deep = [&](int x, int y) {
                    return tileAt(x, y) == '#' && g.level.isSolidChar(tileAt(x, y - 1));
                };
                auto featAt = [&](int x) {   // does a keepsake start at column x (bottom row)?
                    const int every = 44;   // about two and a half screens
                    int bucket = (int)std::floor((float)x / every);
                    int start = bucket * every + 4 + (int)(hashXY(bucket, 977) % (every - 10));
                    return x == start && deep(x, LEVEL_H - 1) && deep(x + 1, LEVEL_H - 1);
                };
                int v = (int)((hashXY(tx, ty) >> 7) % GROUND_VARIANTS);
                if (v) frame = (frame == T_GROUND_TOP ? T_GROUND_TOP1 : T_GROUND1) + v - 1;
                if (frame != T_GROUND_TOP && ty == LEVEL_H - 1) {
                    if (featAt(tx)) frame = T_FEAT_L;
                    else if (featAt(tx - 1)) frame = T_FEAT_R;
                }
            }
            Color tint = WHITE;
            if (ch == '1' || ch == '2') {
                // a gentle pulse so the coloured memories read as "only for one of you"
                unsigned char a = (unsigned char)(200 + 55 * std::sin(g.time * 3 + tx));
                tint = Color{255, 255, 255, a};
            }
            auto open = [&](int dx, int dy) {
                char n = tileAt(tx + dx, ty + dy);
                return !(g.level.isSolidChar(n) || n == '1' || n == '2');
            };
            float d = (ch == '1' || ch == '2') ? depth * 0.6f : depth;
            tileBlock(A.tiles, frameRect(A.tiles, frame, 1, 1, era), tx, ty, d, open(0, -1), open(-1, 0), open(1, 0), tint);
            // pixel stages: a solid tile over empty space gets a dark underside, two native
            // pixels deep (the lower one darkest), so floating platforms read as solid objects
            // (not in 1-bit: there the half-dark row would dither, and the tiles carry their own edges)
            if (lowRes(era) && era != 0 && ch != '1' && ch != '2' && ty + 1 < LEVEL_H && open(0, 1)) {
                float px = 1.0f / TILE_PX[era];
                Color dark = era == 1 ? hexc(SHADES4[0]) : (era == 0 ? Color{16, 18, 22, 255} : hexc(0x1a1424));
                solidQuad((float)tx, ty + 1 - 2 * px, 1.0f, px, 0.02f, Color{dark.r, dark.g, dark.b, 120});
                solidQuad((float)tx, ty + 1 - px, 1.0f, px, 0.02f, dark);
                // and a hard drop shadow down and to the right on the scenery behind (the light
                // comes from the top-left), cool-tinted; the 4-shade and 8-bit post pass snaps it
                Color sh = era == 1 ? hexc(SHADES4[0], 110) : hexc(0x2a1a50, 105);
                solidQuad((float)tx + 2 * px, ty + 1.0f, 1.0f, 2 * px, 0.03f, sh);
                if (open(1, 0)) solidQuad((float)tx + 1, ty + 2 * px, 2 * px, 1.0f - 2 * px, 0.03f, sh);
            }
            // pixel stages: platforms get a 1 px dark outline on every open side, so they stand off
            // the scenery behind them whatever its colour
            if (lowRes(era) && era != 0 && (frame == T_BLOCK || frame == T_CRACKED)) {
                float px = 1.0f / TILE_PX[era];
                Color ol = era == 1 ? hexc(SHADES4[0]) : (era == 2 ? hexc(0x0f0d1a) : (era == 3 ? hexc(0x140f1e) : hexc(0x2a1424)));
                if (open(0, -1)) solidQuad((float)tx, (float)ty, 1.0f, px, 0.02f, ol);
                if (open(-1, 0)) solidQuad((float)tx, (float)ty, px, 1.0f, 0.02f, ol);
                if (open(1, 0)) solidQuad((float)tx + 1 - px, (float)ty, px, 1.0f, 0.02f, ol);
            }
        }
    }
    if (tileHalfPx) rlPopMatrix();

    // ---- the modelled street of the 3D stage
    if (full3D) {
        rlDrawRenderBatchActive();
        models_.begin(g, c3);
        models_.drawScenery(g);
        models_.drawGround(g);
        rlDrawRenderBatchActive();
    }

    // ---- shadows (2.5D and 3D)
    if (smooth(era)) {
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
    if (full3D) {
        for (const auto& f : g.frags) {
            float a = (!f.placed && f.t > 10) ? std::max(0.0f, (12 - f.t) / 2) : 1.0f;
            models_.drawFragment(f.x, f.y, f.t + f.x, a);
        }
    }
    for (const auto& f : g.frags) {
        if (full3D) break;
        int fr = ((int)(f.t * 6 + f.x) % 2) ? T_FRAG1 : T_FRAG0;
        const float s = 1.0f;   // drawn 1:1, never resampled (the art itself sits at 0.7 of the frame)
        unsigned char a = (!f.placed && f.t > 10) ? (unsigned char)(255 * std::max(0.0f, (12 - f.t) / 2)) : 255;
        sprite(A.tiles, frameRect(A.tiles, fr, 1, 1, era), f.x - s / 2, f.y - s / 2, s, s, 0.1f, Color{255, 255, 255, a}, false);
    }
    for (const auto& pu : g.powerups) {
        float bob = std::sin(pu.t * 3) * 0.12f;
        const float ps = 1.0f;   // 1:1 (the art itself sits at 0.9 of the frame)
        sprite(A.tiles, frameRect(A.tiles, T_PU_SHIELD + pu.type, 1, 1, era), pu.x - ps / 2, pu.y - ps / 2 + bob, ps, ps, 0.1f, WHITE, false);
        if ((int)(pu.t * 4) % 2 == 0) ring(pu.x, pu.y + bob, 0.6f, Color{255, 240, 180, 160});
    }

    // ---- enemies
    for (const auto& e : g.enemies) {
        if (full3D) {
            models_.drawEnemy(g, e);
            if (e.owner >= 0 && !e.dead) ring(e.cx(), e.cy(), (e.type == EN_BRUTE ? 2.0f : 1.0f) * 0.6f, e.owner == 0 ? Color{255, 150, 60, 200} : Color{80, 160, 255, 200}, 0.9f);
            continue;
        }
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
        sprite(*t, frameRect(*t, fr, fs, fs, era), dx, dy, fs, fs, 0.0f, tint, e.dir > 0 ? false : true);
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
            if (b.kind == 4 && b.charge) {   // the last train: mark the whole stretch of line it will charge along
                float x0 = std::min(b.fromX, b.toX), x1 = std::max(b.fromX, b.toX);
                solidQuad(x0, b.toY + b.h / 2 - 0.15f, x1 - x0, 0.12f, 0.3f, wc);
            }
        }
        if (g.powers.warning()) {   // a power is coming: the boss pulses like a ticking clock
            float k = std::fmod(g.time * 2.5f, 1.0f);
            Color pc = era == 0 ? Color{235, 235, 225, (unsigned char)(255 * (1 - k))} : Color{200, 150, 255, (unsigned char)(255 * (1 - k))};
            ring(b.x, b.y, 1.6f + k * 2.6f, pc);
            ring(b.x, b.y, 1.6f + std::fmod(k + 0.5f, 1.0f) * 2.6f, pc);
        }
        if (full3D) models_.drawBoss(g);
        int fr = b.state == 5 || b.hurt > 0 ? XF_HURT : (b.state == 3 ? XF_SLAM : XF_FLOAT0 + ((int)(b.t * 4) % 2));
        Color tint = WHITE;
        if (b.state == 5) tint.a = (unsigned char)(255 * std::max(0.0f, 1 - b.stateTime / 1.6f));
        if (b.state == 3 && (int)(g.time * 8) % 2 == 0) tint = Color{255, 255, 200, 255};   // flashing = stomp me
        const Texture2D& bossTex = A.storyBoss.id ? A.storyBoss : A.boss;   // each memory's own boss
        if (!full3D) sprite(bossTex, frameRect(bossTex, fr, BOSS_FS, BOSS_FS, era), b.x - BOSS_FS / 2, b.y - BOSS_FS / 2 - 0.2f, BOSS_FS, BOSS_FS, 0.0f, tint, true);
        // the Last Train's echoes: see-through copies running its last charge again
        for (const auto& e : g.powers.echoes) {
            if (!e.active() || full3D) continue;
            Vector2 c = e.pos();
            sprite(bossTex, frameRect(bossTex, XF_FLOAT0, BOSS_FS, BOSS_FS, era), c.x - BOSS_FS / 2, c.y - BOSS_FS / 2 - 0.2f, BOSS_FS, BOSS_FS,
                   0.01f, Color{200, 220, 255, 110}, true);
        }
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
        const bool held = !pl.faded && (g.powers.freezeT > 0 || g.powers.pullT > 0);   // time stopped / pulled back
        if (held && era > 0) tint = g.powers.pullT > 0 ? Color{215, 185, 255, 255} : Color{185, 215, 255, 255};
        const Texture2D& t = A.player[p];
        if (full3D) models_.drawPlayer(g, p);
        else sprite(t, frameRect(t, fr, PLAYER_FW, PLAYER_FH, era), pl.cx() - PLAYER_FW / 2 + ox,
                    pl.bottom() - PLAYER_FH + (era == 0 ? 0.0f : 0.03f), PLAYER_FW, PLAYER_FH, 0.05f + p * 0.01f, tint, pl.facing < 0);
        // rings sit round the drawn figure (PLAYER_K bigger than the hitbox), not the hitbox
        const float vcy = full3D ? pl.cy() : pl.bottom() - PLAYER_FH * 0.47f, rk = full3D ? 1.0f : 1.25f;
        if (pl.shield) ring(pl.cx(), vcy, 0.95f * rk, Color{255, 120, 160, (unsigned char)(150 + 100 * std::sin(g.time * 5))});
        if (held) ring(pl.cx(), vcy, 1.05f * rk, era == 0 ? Color{235, 235, 225, 255} : Color{170, 200, 255, 230});
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
            float scroll = std::fmod(g.time * (era == 0 ? 8.0f : 2.0f), 1.0f);
            Rectangle src{0, (y / WALL_TEX_H + scroll) * wt.height, (float)wt.width, wt.height * 0.5f / WALL_TEX_H};
            float wq = WALL_TEX_W;
            // the textured leading strip
            quad(wt, src, edge - wq, y, wq, 0.5f, 0.6f, WHITE);
            // the solid body behind it
            Color body = era == 0 ? Color{16, 18, 22, 255} : (era == 1 ? hexc(SHADES4[0]) : (era == 2 ? hexc(0x0f0d1a) : hexc(0x1a1030, smooth(era) ? 235 : 255)));
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
    const Color eraGlow[ERA_COUNT] = {{235, 235, 225, 255}, {232, 226, 210, 255}, {247, 215, 58, 255}, {242, 140, 180, 255},
                                      {126, 240, 192, 255}, {255, 220, 150, 255}, {255, 250, 235, 255}};
    Color glow = eraGlow[std::min(LAST_ERA, std::max(0, g.era))];
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
}
