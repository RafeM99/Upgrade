#include "render/ModernLook.h"
#include "core/Game.h"
#include "core/Paths.h"
#include "render/Art.h"
#include "render/ArtSpec.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace modern {
namespace {

// ---- the one light setup of the modern era. Everything is lit from here.
// Sun: warm, low, from the upper left and a little in front of the scene.
const Vector3 KEY_DIR = {-0.50f, 0.70f, 0.50f};   // towards the light, normalised in the shader
const Vector3 KEY_COL = {1.00f, 0.84f, 0.62f};
const Vector3 AMBIENT = {0.58f, 0.62f, 0.78f};    // cool sky fill
const Vector3 RIM_KEY = {1.00f, 0.86f, 0.62f};    // sun-side edge light
const Vector3 RIM_BACK = {0.45f, 0.62f, 1.00f};   // cool bounce on the shadow side
const Color SHAFT = {255, 214, 160, 255};

// background parallax per layer (sky, far, mid, near)
const float PARALLAX[art::BG_LAYERS] = {0.0f, 0.04f, 0.15f, 0.42f};

Shader worldSh{}, blurSh{}, postSh{};
bool ready = false;
int wlViewPos = -1, wlCount = -1, wlPos = -1, wlCol = -1, wlAmb = -1, wlKeyDir = -1, wlKeyCol = -1, wlRimKey = -1,
    wlRimBack = -1, wlAmount = -1;
int blDir = -1, blMode = -1;
int pTime = -1, pSize = -1, pOut = -1, pFlash = -1, pDesat = -1, pMosaic = -1, pGlitch = -1;

RenderTexture2D farA{}, farB{}, midA{}, midB{};
int bw = 0, bh = 0;

unsigned hazeTex = 0;
Color haze = {236, 196, 170, 255};

inline float Y3(float y) { return (float)LEVEL_H - y; }

void freeTargets()
{
    for (RenderTexture2D* r : {&farA, &farB, &midA, &midB})
        if (r->id) { UnloadRenderTexture(*r); *r = RenderTexture2D{}; }
    bw = bh = 0;
}

void ensureTargets(int w, int h)
{
    int nw = std::max(16, w / 2), nh = std::max(16, h / 2);
    if (nw == bw && nh == bh && farA.id) return;
    freeTargets();
    for (RenderTexture2D* r : {&farA, &farB, &midA, &midB}) {
        *r = LoadRenderTexture(nw, nh);
        SetTextureFilter(r->texture, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(r->texture, TEXTURE_WRAP_CLAMP);
    }
    bw = nw;
    bh = nh;
}

// The average colour of the lower sky: the haze and the depth fog take this colour,
// so they always match whatever the era 4 background art is painted with.
void updateHaze(const Texture2D& sky)
{
    if (sky.id == hazeTex) return;
    hazeTex = sky.id;
    Image img = LoadImageFromTexture(sky);
    if (!img.data) return;
    Color* px = LoadImageColors(img);
    double r = 0, g = 0, b = 0;
    int n = 0;
    for (int y = img.height / 2; y < img.height; y += 2)
        for (int x = 0; x < img.width; x += 4) {
            Color c = px[y * img.width + x];
            r += c.r; g += c.g; b += c.b;
            ++n;
        }
    if (n > 0) {
        // lift it towards the sun colour a little so the haze reads as lit air, not grey fog
        haze = Color{(unsigned char)std::min(255.0, r / n * 0.8 + 255 * 0.2), (unsigned char)std::min(255.0, g / n * 0.8 + 214 * 0.2),
                     (unsigned char)std::min(255.0, b / n * 0.8 + 160 * 0.2), 255};
    }
    UnloadImageColors(px);
    UnloadImage(img);
}

// One tiling background layer, scrolled with parallax. `scale` maps full-size pixels to the target.
void drawLayer(const Texture2D& t, float camX, float pxPerTile, float parallax, int w, int h, float scale, Color tint)
{
    float s = (float)h / t.height;
    float tw = t.width * s;
    float off = std::fmod(camX * pxPerTile * scale * parallax, tw);
    if (off < 0) off += tw;
    for (float x = -off; x < w; x += tw)
        DrawTexturePro(t, Rectangle{0, 0, (float)t.width, (float)t.height}, Rectangle{x, 0, tw + 1, (float)h}, {0, 0}, 0, tint);
}

// Separable gaussian: src -> dst along one axis. mode 0 opaque, 1 straight -> premultiplied, 2 premultiplied.
void blurPass(const RenderTexture2D& src, RenderTexture2D& dst, float dx, float dy, int mode)
{
    BeginTextureMode(dst);
    ClearBackground(BLANK);
    rlDrawRenderBatchActive();
    rlDisableColorBlend();
    BeginShaderMode(blurSh);
    float dir[2] = {dx / bw, dy / bh};
    SetShaderValue(blurSh, blDir, dir, SHADER_UNIFORM_VEC2);
    SetShaderValue(blurSh, blMode, &mode, SHADER_UNIFORM_INT);
    DrawTexturePro(src.texture, Rectangle{0, 0, (float)bw, -(float)bh}, Rectangle{0, 0, (float)bw, (float)bh}, {0, 0}, 0, WHITE);
    EndShaderMode();
    rlDrawRenderBatchActive();
    rlEnableColorBlend();
    EndTextureMode();
}

// A soft beam of sunlight, slanting away from the sun (upper left) towards the lower right.
void shaft(float x, float topW, float len, float h, float a)
{
    const float sx = 0.42f;   // horizontal travel per unit of height
    float x0 = x, x1 = x + topW;
    float y1 = len * h;
    float bx0 = x0 + sx * y1, bx1 = x1 + sx * y1 + topW * 0.8f;
    unsigned char ca = (unsigned char)std::max(0.0f, std::min(255.0f, a * 255));
    rlSetTexture(rlGetTextureIdDefault());
    rlBegin(RL_QUADS);
    rlColor4ub(SHAFT.r, SHAFT.g, SHAFT.b, ca); rlTexCoord2f(0, 0); rlVertex2f(x0, 0);
    rlColor4ub(SHAFT.r, SHAFT.g, SHAFT.b, 0);  rlTexCoord2f(0, 1); rlVertex2f(bx0, y1);
    rlColor4ub(SHAFT.r, SHAFT.g, SHAFT.b, 0);  rlTexCoord2f(1, 1); rlVertex2f(bx1, y1);
    rlColor4ub(SHAFT.r, SHAFT.g, SHAFT.b, ca); rlTexCoord2f(1, 0); rlVertex2f(x1, 0);
    rlEnd();
    rlSetTexture(0);
}

float hash1(float n) { float s = std::sin(n * 12.9898f) * 43758.5453f; return s - std::floor(s); }

}  // namespace

void init()
{
    worldSh = LoadShader(assetPath("shaders/modern_world.vs").c_str(), assetPath("shaders/modern_world.fs").c_str());
    blurSh = LoadShader(nullptr, assetPath("shaders/modern_blur.fs").c_str());
    postSh = LoadShader(nullptr, assetPath("shaders/modern_post.fs").c_str());
    wlViewPos = GetShaderLocation(worldSh, "viewPos");
    wlCount = GetShaderLocation(worldSh, "lightCount");
    wlPos = GetShaderLocation(worldSh, "lightPos");
    wlCol = GetShaderLocation(worldSh, "lightCol");
    wlAmb = GetShaderLocation(worldSh, "ambient");
    wlKeyDir = GetShaderLocation(worldSh, "keyDir");
    wlKeyCol = GetShaderLocation(worldSh, "keyCol");
    wlRimKey = GetShaderLocation(worldSh, "rimKey");
    wlRimBack = GetShaderLocation(worldSh, "rimBack");
    wlAmount = GetShaderLocation(worldSh, "amount");
    blDir = GetShaderLocation(blurSh, "dir");
    blMode = GetShaderLocation(blurSh, "mode");
    pTime = GetShaderLocation(postSh, "time");
    pSize = GetShaderLocation(postSh, "texSize");
    pOut = GetShaderLocation(postSh, "outSize");
    pFlash = GetShaderLocation(postSh, "flash");
    pDesat = GetShaderLocation(postSh, "desat");
    pMosaic = GetShaderLocation(postSh, "mosaic");
    pGlitch = GetShaderLocation(postSh, "glitch");
    ready = true;
}

void shutdown()
{
    if (!ready) return;
    freeTargets();
    UnloadShader(worldSh);
    UnloadShader(blurSh);
    UnloadShader(postSh);
    ready = false;
}

void prepareBackground(Game& g, const EraArt& A, float camX, float pxPerTile, int w, int h)
{
    (void)g;
    ensureTargets(w, h);
    updateHaze(A.bg[0]);
    const float scale = (float)bh / h;

    // far: sky and far layer, strongly out of focus
    BeginTextureMode(farA);
    ClearBackground(haze);
    drawLayer(A.bg[0], camX, pxPerTile, PARALLAX[0], bw, bh, scale, WHITE);
    drawLayer(A.bg[1], camX, pxPerTile, PARALLAX[1], bw, bh, scale, WHITE);
    EndTextureMode();
    blurPass(farA, farB, 2.2f, 0, 0);
    blurPass(farB, farA, 0, 2.2f, 0);

    // middle: a lighter blur, keeping its transparency
    BeginTextureMode(midA);
    ClearBackground(BLANK);
    rlDrawRenderBatchActive();
    rlDisableColorBlend();
    drawLayer(A.bg[2], camX, pxPerTile, PARALLAX[2], bw, bh, scale, WHITE);
    rlDrawRenderBatchActive();
    rlEnableColorBlend();
    EndTextureMode();
    blurPass(midA, midB, 0.9f, 0, 1);
    blurPass(midB, midA, 0, 0.9f, 2);
}

void drawBackground(Game& g, const EraArt& A, float camX, float pxPerTile, int w, int h)
{
    Rectangle src{0, 0, (float)bw, -(float)bh}, dst{0, 0, (float)w, (float)h};
    DrawTexturePro(farA.texture, src, dst, {0, 0}, 0, WHITE);
    // aerial perspective: the distance sinks into lit haze
    DrawRectangleGradientV(0, (int)(h * 0.30f), w, (int)(h * 0.70f) + 1, Color{haze.r, haze.g, haze.b, 0},
                           Color{haze.r, haze.g, haze.b, 120});

    BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
    DrawTexturePro(midA.texture, src, dst, {0, 0}, 0, WHITE);
    EndBlendMode();
    DrawRectangleGradientV(0, (int)(h * 0.45f), w, (int)(h * 0.55f) + 1, Color{haze.r, haze.g, haze.b, 0},
                           Color{haze.r, haze.g, haze.b, 55});

    // the near layer sits close to the plane of play: in focus
    drawLayer(A.bg[3], camX, pxPerTile, PARALLAX[3], w, h, 1.0f, Color{235, 232, 240, 255});

    // sunlight: a glow in the upper left and slow soft shafts
    BeginBlendMode(BLEND_ADDITIVE);
    float u = h / 720.0f;
    DrawCircleGradient((int)(w * 0.06f), (int)(-h * 0.08f), h * 0.75f, Color{SHAFT.r, SHAFT.g, SHAFT.b, 70},
                       Color{SHAFT.r, SHAFT.g, SHAFT.b, 0});
    float drift = std::fmod(camX * pxPerTile * 0.08f, (float)w * 1.5f);
    for (int i = 0; i < 6; ++i) {
        float base = hash1(i * 3.1f) * w * 1.5f - drift;
        while (base < -w * 0.7f) base += w * 1.5f;
        float sway = std::sin(g.time * 0.21f + i * 1.7f) * 18 * u;
        float a = 0.05f + 0.04f * std::sin(g.time * 0.33f + i * 2.3f);
        shaft(base + sway - w * 0.15f, (40 + 90 * hash1(i * 7.7f)) * u, 0.75f + 0.3f * hash1(i * 5.3f), (float)h, a);
    }
    EndBlendMode();
}

void beginWorld(Game& g, const Camera3D& cam, float amount)
{
    // point lights: players, boss, fragments and power-ups nearest the middle of the view
    std::vector<float> pos, col;
    auto add = [&](float x, float y, float r, float cr, float cg, float cb) {
        if (pos.size() / 4 >= 16) return;
        pos.insert(pos.end(), {x, Y3(y), 0.3f, r});
        col.insert(col.end(), {cr, cg, cb});
    };
    for (int p = 0; p < 2; ++p)
        if (g.players[p].joined)
            add(g.players[p].cx(), g.players[p].cy(), 4.5f, p == 0 ? 0.38f : 0.10f, p == 0 ? 0.22f : 0.22f, p == 0 ? 0.06f : 0.42f);
    if (g.boss.active) add(g.boss.x, g.boss.y, 6.5f, 0.45f, 0.22f, 0.55f);
    for (const auto& pu : g.powerups)
        if (pu.x > g.cam.left() - 2 && pu.x < g.cam.right() + 2) add(pu.x, pu.y, 2.6f, 0.40f, 0.34f, 0.20f);
    for (const auto& f : g.frags)
        if (f.x > g.cam.left() - 2 && f.x < g.cam.right() + 2) add(f.x, f.y, 2.0f, 0.42f, 0.33f, 0.10f);
    int n = (int)(pos.size() / 4);

    BeginShaderMode(worldSh);
    float vp[3] = {cam.position.x, cam.position.y, cam.position.z};
    float kd[3] = {KEY_DIR.x, KEY_DIR.y, KEY_DIR.z}, kc[3] = {KEY_COL.x, KEY_COL.y, KEY_COL.z};
    float am[3] = {AMBIENT.x, AMBIENT.y, AMBIENT.z};
    float rk[3] = {RIM_KEY.x, RIM_KEY.y, RIM_KEY.z}, rb[3] = {RIM_BACK.x, RIM_BACK.y, RIM_BACK.z};
    SetShaderValue(worldSh, wlViewPos, vp, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldSh, wlKeyDir, kd, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldSh, wlKeyCol, kc, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldSh, wlAmb, am, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldSh, wlRimKey, rk, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldSh, wlRimBack, rb, SHADER_UNIFORM_VEC3);
    SetShaderValue(worldSh, wlAmount, &amount, SHADER_UNIFORM_FLOAT);
    SetShaderValue(worldSh, wlCount, &n, SHADER_UNIFORM_INT);
    if (n > 0) {
        SetShaderValueV(worldSh, wlPos, pos.data(), SHADER_UNIFORM_VEC4, n);
        SetShaderValueV(worldSh, wlCol, col.data(), SHADER_UNIFORM_VEC3, n);
    }
}

void endWorld() { EndShaderMode(); }

void drawForeground(Game& g, int w, int h, float amount)
{
    if (amount <= 0.01f) return;
    float u = h / 720.0f;
    float pxPerTile = h / g.cam.viewH;
    float camPx = g.cam.x * pxPerTile;
    BeginBlendMode(BLEND_ADDITIVE);
    // dust and pollen catching the sun, just in front of the play plane
    for (int i = 0; i < 46; ++i) {
        float span = w + 200 * u;
        float x = std::fmod(hash1(i * 1.37f) * span * 3 - camPx * 1.08f + std::sin(g.time * 0.4f + i) * 30 * u, span);
        if (x < 0) x += span;
        x -= 100 * u;
        float y = std::fmod(hash1(i * 2.71f) * h + g.time * (6 + 10 * hash1(i * 4.1f)) * u, (float)h);
        float r = (1.2f + 2.4f * hash1(i * 9.3f)) * u;
        float tw = 0.5f + 0.5f * std::sin(g.time * (1.2f + hash1(i)) + i);
        unsigned char a = (unsigned char)(amount * (40 + 90 * tw));
        DrawCircleGradient((int)x, (int)y, r * 2.5f, Color{255, 226, 180, a}, Color{255, 226, 180, 0});
    }
    // big out-of-focus bokeh discs very close to the lens
    for (int i = 0; i < 7; ++i) {
        float span = w + 400 * u;
        float x = std::fmod(hash1(i * 6.13f + 2) * span * 4 - camPx * 1.7f, span);
        if (x < 0) x += span;
        x -= 200 * u;
        float y = h * (0.12f + 0.8f * hash1(i * 3.33f + 1)) + std::sin(g.time * 0.3f + i) * 12 * u;
        float r = (34 + 46 * hash1(i * 8.8f)) * u;
        unsigned char a = (unsigned char)(amount * (9 + 8 * hash1(i * 1.9f)));
        Color c = i % 3 == 0 ? Color{170, 200, 255, a} : Color{255, 214, 160, a};
        DrawCircleV({x, y}, r, c);
        DrawRing({x, y}, r - 2.5f * u, r, 0, 360, 48, Color{c.r, c.g, c.b, (unsigned char)(a * 1.6f)});
    }
    EndBlendMode();
}

void blit(Game& g, const Texture2D& tex, Rectangle src, Rectangle dst, float flash, float desat, float mosaic, float glitch)
{
    float size[2] = {(float)tex.width, (float)tex.height};
    float out[2] = {dst.width, dst.height};
    float t = g.time;
    SetShaderValue(postSh, pTime, &t, SHADER_UNIFORM_FLOAT);
    SetShaderValue(postSh, pSize, size, SHADER_UNIFORM_VEC2);
    SetShaderValue(postSh, pOut, out, SHADER_UNIFORM_VEC2);
    SetShaderValue(postSh, pFlash, &flash, SHADER_UNIFORM_FLOAT);
    SetShaderValue(postSh, pDesat, &desat, SHADER_UNIFORM_FLOAT);
    SetShaderValue(postSh, pMosaic, &mosaic, SHADER_UNIFORM_FLOAT);
    SetShaderValue(postSh, pGlitch, &glitch, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(postSh);
    DrawTexturePro(tex, src, dst, {0, 0}, 0, WHITE);
    EndShaderMode();
}

}  // namespace modern
