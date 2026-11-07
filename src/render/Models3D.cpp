#include "render/Models3D.h"
#include "core/Game.h"
#include "core/Paths.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace {

inline float Y3(float y) { return (float)LEVEL_H - y; }

// "a, then b": raymath multiplies so that the left matrix is applied first.
inline Matrix then(Matrix a, Matrix b) { return MatrixMultiply(a, b); }
inline Matrix S(float x, float y, float z) { return MatrixScale(x, y, z); }
inline Matrix T(float x, float y, float z) { return MatrixTranslate(x, y, z); }

Color hexc(unsigned h, unsigned char a = 255) { return Color{(unsigned char)(h >> 16), (unsigned char)(h >> 8), (unsigned char)h, a}; }
Color withA(Color c, float a) { c.a = (unsigned char)(c.a * std::max(0.0f, std::min(1.0f, a))); return c; }
Color mixc(Color a, Color b, float t)
{
    return Color{(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                 (unsigned char)(a.b + (b.b - a.b) * t), a.a};
}

unsigned hashi(int x, int s)
{
    unsigned h = (unsigned)x * 374761393u + (unsigned)s * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float rndf(int x, int s) { return (hashi(x, s) & 0xffff) / 65535.0f; }

}  // namespace

void Models3D::init()
{
    sh_ = LoadShader(assetPath("shaders/lit3d.vs").c_str(), assetPath("shaders/lit3d.fs").c_str());
    sh_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(sh_, "matModel");
    sh_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(sh_, "matNormal");
    locView_ = GetShaderLocation(sh_, "viewPos");
    locEmissive_ = GetShaderLocation(sh_, "emissive");
    locLightPos_ = GetShaderLocation(sh_, "lightPos");
    locLightCol_ = GetShaderLocation(sh_, "lightCol");
    locLightRad_ = GetShaderLocation(sh_, "lightRad");
    auto set3 = [&](const char* n, Vector3 v) { SetShaderValue(sh_, GetShaderLocation(sh_, n), &v, SHADER_UNIFORM_VEC3); };
    auto set1 = [&](const char* n, float v) { SetShaderValue(sh_, GetShaderLocation(sh_, n), &v, SHADER_UNIFORM_FLOAT); };
    set3("sunDir", Vector3Normalize({0.45f, -0.8f, -0.4f}));
    set3("sunCol", {1.0f, 0.93f, 0.8f});
    set3("skyCol", {0.62f, 0.76f, 1.0f});
    set3("groundCol", {0.45f, 0.42f, 0.32f});
    set3("fogCol", {0.93f, 0.9f, 0.86f});
    set1("fogStart", 30.0f);
    set1("fogEnd", 90.0f);

    cube_ = LoadModelFromMesh(GenMeshCube(1, 1, 1));
    sphere_ = LoadModelFromMesh(GenMeshSphere(1, 12, 18));
    cyl_ = LoadModelFromMesh(GenMeshCylinder(1, 1, 14));
    torus_ = LoadModelFromMesh(GenMeshTorus(0.12f, 2.0f, 28, 10));
    for (Model* m : {&cube_, &sphere_, &cyl_, &torus_}) m->materials[0].shader = sh_;
    ready_ = sh_.id > 0;
}

void Models3D::shutdown()
{
    for (Model* m : {&cube_, &sphere_, &cyl_, &torus_}) {
        m->materials[0].shader = Shader{rlGetShaderIdDefault(), rlGetShaderLocsDefault()};
        UnloadModel(*m);
    }
    UnloadShader(sh_);
}

void Models3D::part(Model& m, Matrix t, Color c, float emissive)
{
    SetShaderValue(sh_, locEmissive_, &emissive, SHADER_UNIFORM_FLOAT);
    m.transform = t;
    DrawModel(m, {0, 0, 0}, 1.0f, c);
}

void Models3D::begin(Game& g, const Camera3D& cam)
{
    rlDrawRenderBatchActive();
    SetShaderValue(sh_, locView_, &cam.position, SHADER_UNIFORM_VEC3);
    Vector3 pos[4]{}, col[4]{};
    float rad[4] = {0, 0, 0, 0};
    for (int p = 0; p < 2; ++p) {
        const Player& pl = g.players[p];
        if (!pl.joined) continue;
        pos[p] = {pl.cx(), Y3(pl.cy()), 1.2f};
        col[p] = p == 0 ? Vector3{1.0f, 0.6f, 0.25f} : Vector3{0.3f, 0.6f, 1.0f};
        rad[p] = 5.0f;
    }
    if (g.boss.active) { pos[2] = {g.boss.x, Y3(g.boss.y), 2.0f}; col[2] = {0.8f, 0.5f, 1.0f}; rad[2] = 7.0f; }
    SetShaderValueV(sh_, locLightPos_, pos, SHADER_UNIFORM_VEC3, 4);
    SetShaderValueV(sh_, locLightCol_, col, SHADER_UNIFORM_VEC3, 4);
    SetShaderValueV(sh_, locLightRad_, rad, SHADER_UNIFORM_FLOAT, 4);
}

// ------------------------------------------------------------------ the street
void Models3D::drawScenery(Game& g)
{
    float l = g.cam.left() - 16, r = g.cam.right() + 36, mid = (l + r) / 2, wdt = r - l;
    // lawn behind the path and in front of it
    part(cube_, then(S(wdt, 0.1f, 40), T(mid, 1.95f, -23.0f)), hexc(0x5aa04a));
    part(cube_, then(S(wdt, 0.1f, 10), T(mid, -0.05f, 6.0f)), hexc(0x4a9040));
    // a low picket fence along the back of the path
    for (int x = (int)std::floor(l); x < (int)r; ++x) {
        part(cube_, then(S(0.12f, 0.7f, 0.08f), T(x + 0.5f, 2.35f, -3.5f)), hexc(0xf4f0e6));
    }
    part(cube_, then(S(wdt, 0.08f, 0.05f), T(mid, 2.5f, -3.46f)), hexc(0xe8e2d6));
    part(cube_, then(S(wdt, 0.08f, 0.05f), T(mid, 2.2f, -3.46f)), hexc(0xe8e2d6));

    // trees, houses and lamps on slots four tiles apart, picked by a hash so they stay put
    for (int k = (int)std::floor(l / 4); k <= (int)(r / 4); ++k) {
        float x = k * 4.0f + 2.0f;
        int kind = (int)(hashi(k, 7) % 7);
        if (kind <= 1) {   // tree
            float z = -5.5f - rndf(k, 1) * 3.0f, h = 1.6f + rndf(k, 2) * 0.8f;
            part(cyl_, then(S(0.18f, h, 0.18f), T(x, 2, z)), hexc(0x6a4430));
            Color leaf = mixc(hexc(0x3a8a3a), hexc(0x6ab04a), rndf(k, 3));
            part(sphere_, then(S(1.0f, 0.9f, 1.0f), T(x, 2 + h + 0.5f, z)), leaf);
            part(sphere_, then(S(0.7f, 0.65f, 0.7f), T(x + 0.6f, 2 + h + 0.1f, z + 0.3f)), mixc(leaf, WHITE, 0.08f));
            part(sphere_, then(S(0.6f, 0.55f, 0.6f), T(x - 0.55f, 2 + h + 0.2f, z - 0.2f)), mixc(leaf, BLACK, 0.1f));
        } else if (kind == 2 || kind == 3) {   // house with a gable roof and lit windows
            float z = -9.5f - rndf(k, 4) * 2.0f, w = 3.2f, h = 2.6f + rndf(k, 5) * 0.8f, d = 3.0f;
            Color wall = mixc(hexc(0xe8d4b8), hexc(0xc89a80), rndf(k, 6));
            part(cube_, then(S(w, h, d), T(x, 2 + h / 2, z)), wall);
            // roof: a box turned 45 degrees, half of it sticking out above the walls
            part(cube_, then(then(S(w + 0.4f, d * 0.72f, d * 0.72f), MatrixRotateX(PI / 4)), T(x, 2 + h, z)), hexc(0xa0503e));
            part(cube_, then(S(0.6f, 1.1f, 0.06f), T(x, 2.55f, z + d / 2 + 0.02f)), hexc(0x6a3a2a));
            for (int wi = 0; wi < 2; ++wi)
                part(cube_, then(S(0.6f, 0.55f, 0.06f), T(x + (wi ? 0.95f : -0.95f), 2 + h * 0.62f, z + d / 2 + 0.02f)),
                     hexc(0xffe2a0), 0.9f);
            part(cube_, then(S(0.35f, 0.9f, 0.35f), T(x + 0.9f, 2 + h + 0.9f, z - 0.4f)), hexc(0x8a5a4a));   // chimney
        } else if (kind == 4) {   // street lamp
            float z = -4.4f;
            part(cyl_, then(S(0.07f, 2.6f, 0.07f), T(x, 2, z)), hexc(0x2a2a36));
            part(sphere_, then(S(0.22f, 0.22f, 0.22f), T(x, 4.75f, z)), hexc(0xfff0c0), 1.0f);
        } else if (kind == 5) {   // flower bed
            for (int f = 0; f < 6; ++f) {
                float fx = x - 1 + f * 0.4f, fz = -4.2f + std::sin(f * 2.1f) * 0.3f;
                part(cyl_, then(S(0.03f, 0.35f, 0.03f), T(fx, 2, fz)), hexc(0x3a7a3a));
                part(sphere_, then(S(0.12f, 0.12f, 0.12f), T(fx, 2.4f, fz)), f % 2 ? hexc(0xf28cb4) : hexc(0xf7d73a), 0.2f);
            }
        }
        // a second, further row of trees
        if (hashi(k, 11) % 2 == 0) {
            float z = -16.0f - rndf(k, 12) * 6.0f;
            part(cyl_, then(S(0.25f, 2.2f, 0.25f), T(x + 1.3f, 2, z)), hexc(0x5a3a2a));
            part(sphere_, then(S(1.5f, 1.4f, 1.5f), T(x + 1.3f, 4.8f, z)), hexc(0x3a7a4a));
        }
    }
}

// ------------------------------------------------------------------ Gran and Sam
void Models3D::drawPlayer(Game& g, int p)
{
    const Player& pl = g.players[p];
    const bool gran = p == 0;
    float a = 1.0f;
    if (pl.invuln > 0 && !pl.faded && (int)(pl.invuln * 12) % 2 == 0) a = 0.35f;
    if (pl.faded) a = 0.45f;
    auto col = [&](unsigned h) {
        Color c = hexc(h);
        if (pl.faded) c = mixc(c, hexc(0xa0a0b0), 0.7f);
        return withA(c, a);
    };
    Color skin = col(gran ? 0xf2c29b : 0xe9b58c), top = col(gran ? 0xf08a24 : 0x3c8cf0);
    Color legs = col(gran ? 0x6b4a3a : 0x2b3f6b), shoes = col(gran ? 0x4a2e22 : 0xf4f4f4);
    Color hair = col(gran ? 0xdcd8ea : 0x6b3e22), dark = col(0x1c1420);

    float yaw = pl.facing > 0 ? -0.52f : PI + 0.52f;
    float lean = 0;
    if (pl.dashTime > 0) lean = -0.35f;
    float crouch = pl.pounding ? 0.12f : 0.0f;
    Matrix root = then(then(MatrixRotateZ(lean), MatrixRotateY(yaw)), T(pl.cx(), Y3(pl.bottom()) - crouch * 0.5f, 0.2f));
    auto at = [&](Matrix local) { return then(local, root); };

    // walk cycle
    float ph = g.time * 13.0f;
    bool running = pl.onGround && std::fabs(pl.vx) > 0.6f;
    float legSwing = running ? std::sin(ph) * 0.75f : 0.0f;
    float armSwing = -legSwing * 0.8f;
    float legF = legSwing, legB = -legSwing, armF = armSwing, armB = -armSwing;
    if (!pl.onGround && !pl.gliding) { legF = 0.7f; legB = -0.35f; armF = 2.4f; armB = 1.9f; }
    if (pl.gliding || pl.frame == art::PF_SPECIAL) { armF = 3.0f; armB = 0.4f; }
    if (pl.attackTime > 0) armF = 1.57f;
    if (pl.pounding) { legF = 0.9f; legB = 0.9f; armF = -0.5f; armB = -0.5f; }

    auto limb = [&](float px, float py, float pz, float ang, float len, float r, Color c) {
        part(cyl_, at(then(then(then(S(r, len, r), T(0, -len, 0)), MatrixRotateZ(ang)), T(px, py, pz))), c);
    };
    auto footAt = [&](float hipY, float z, float ang, float len) {
        Vector3 f = {std::sin(ang) * len, hipY - std::cos(ang) * len, z};
        part(cube_, at(then(then(S(0.24f, 0.1f, 0.14f), MatrixRotateZ(ang * 0.4f)), T(f.x + 0.05f, f.y + 0.03f, f.z))), shoes);
    };

    const float hipY = 0.55f - crouch, legLen = 0.5f - crouch * 0.5f;
    limb(0, hipY, -0.09f, legB, legLen, 0.075f, mixc(legs, BLACK, 0.15f));
    footAt(hipY, -0.09f, legB, legLen);
    limb(0, hipY, 0.09f, legF, legLen, 0.075f, legs);
    footAt(hipY, 0.09f, legF, legLen);

    if (gran) {
        part(cyl_, at(then(S(0.22f, 0.26f, 0.2f), T(0, hipY - 0.12f, 0))), legs);       // skirt
        part(cyl_, at(then(S(0.2f, 0.44f, 0.18f), T(0, hipY + 0.08f, 0))), top);        // cardigan
        for (int b = 0; b < 2; ++b)
            part(sphere_, at(then(S(0.03f, 0.03f, 0.03f), T(0.19f, hipY + 0.22f + b * 0.14f, 0))), col(0xfaf3e6));
    } else {
        part(cyl_, at(then(S(0.2f, 0.47f, 0.18f), T(0, hipY - 0.02f, 0))), top);         // hoodie
        part(sphere_, at(then(S(0.16f, 0.1f, 0.15f), T(-0.1f, hipY + 0.45f, 0))), mixc(top, BLACK, 0.2f));   // hood
        part(cube_, at(then(S(0.04f, 0.12f, 0.2f), T(0.19f, hipY + 0.12f, 0))), mixc(top, BLACK, 0.2f));    // pocket
    }
    const float shY = hipY + 0.44f;
    limb(0, shY, -0.23f, armB, 0.38f, 0.06f, mixc(top, BLACK, 0.15f));
    limb(0, shY, 0.23f, armF, 0.38f, 0.06f, top);
    for (int s = 0; s < 2; ++s) {
        float ang = s ? armF : armB;
        part(sphere_, at(then(S(0.07f, 0.07f, 0.07f), T(std::sin(ang) * 0.42f, shY - std::cos(ang) * 0.42f, s ? 0.23f : -0.23f))), skin);
    }

    // head
    const float hy = shY + 0.24f;
    part(sphere_, at(then(S(0.21f, 0.21f, 0.2f), T(0.02f, hy, 0))), skin);
    part(sphere_, at(then(S(0.04f, 0.04f, 0.04f), T(0.21f, hy - 0.02f, 0))), skin);   // nose
    for (int e = -1; e <= 1; e += 2)
        part(sphere_, at(then(S(0.025f, 0.035f, 0.025f), T(0.19f, hy + 0.04f, e * 0.08f))), dark);
    if (gran) {
        part(sphere_, at(then(S(0.22f, 0.17f, 0.22f), T(-0.03f, hy + 0.08f, 0))), hair);
        part(sphere_, at(then(S(0.1f, 0.09f, 0.1f), T(-0.1f, hy + 0.25f, 0))), hair);    // bun
        for (int e = -1; e <= 1; e += 2)   // glasses
            part(torus_, at(then(then(S(0.035f, 0.035f, 0.035f), MatrixRotateZ(PI / 2)), T(0.2f, hy + 0.04f, e * 0.08f))), dark);
    } else {
        part(sphere_, at(then(S(0.22f, 0.15f, 0.22f), T(-0.04f, hy + 0.06f, 0))), hair);
        Color cap = col(0x22c4b0);
        part(cyl_, at(then(S(0.215f, 0.1f, 0.215f), T(0.0f, hy + 0.1f, 0))), cap);
        part(cube_, at(then(S(0.2f, 0.03f, 0.3f), T(0.22f, hy + 0.11f, 0))), mixc(cap, BLACK, 0.2f));   // brim
    }
    // Gran's umbrella when she glides
    if (gran && (pl.gliding || pl.frame == art::PF_SPECIAL)) {
        part(cyl_, at(then(S(0.02f, 0.7f, 0.02f), T(0.1f, shY + 0.3f, 0.23f))), col(0x3a2a2a));
        part(sphere_, at(then(S(0.62f, 0.22f, 0.62f), T(0.1f, shY + 1.0f, 0.23f))), col(0xb03a6a));
    }
}

// ------------------------------------------------------------------ the Forgetting's creatures
void Models3D::drawEnemy(Game& g, const Enemy& e)
{
    float a = e.dead ? std::max(0.0f, 1 - e.deadTime / 0.45f) : 1.0f;
    Color tintOwner = e.owner == 0 ? hexc(0xffaf5f) : (e.owner == 1 ? hexc(0x78b4ff) : WHITE);
    auto col = [&](unsigned h) {
        Color c = hexc(h);
        if (e.owner >= 0) c = mixc(c, tintOwner, 0.55f);
        if (e.hurt > 0) c = mixc(c, WHITE, 0.6f);
        return withA(c, a);
    };
    float yaw = e.dir > 0 ? -0.52f : PI + 0.52f;
    float feet = Y3(e.y + e.h);
    if (e.type == EN_BLOB) {
        float sq = e.dead ? 0.7f : 0.1f * std::sin(e.t * 9);
        Matrix root = then(MatrixRotateY(yaw), T(e.cx(), feet, 0.2f));
        auto at = [&](Matrix m) { return then(m, root); };
        float ry = 0.36f * (1 - sq);
        part(sphere_, at(then(S(0.42f * (1 + sq), ry, 0.38f), T(0, ry, 0))), col(0x8a7fb4));
        if (!e.dead) {
            for (int s = -1; s <= 1; s += 2) {
                part(sphere_, at(then(S(0.1f, 0.12f, 0.1f), T(0.3f, ry * 1.4f, s * 0.13f))), col(0xfaf3e6));
                part(sphere_, at(then(S(0.05f, 0.06f, 0.05f), T(0.39f, ry * 1.4f, s * 0.13f))), col(0x0f0d1a));
            }
            part(cyl_, at(then(then(S(0.02f, 0.3f, 0.02f), MatrixRotateZ(0.4f)), T(-0.05f, ry * 1.9f, 0))), col(0x26305e));
            part(sphere_, at(then(S(0.06f, 0.06f, 0.06f), T(-0.17f, ry * 1.9f + 0.28f, 0))), col(0xfaf3e6), 0.6f);
        }
    } else if (e.type == EN_FLYER) {
        float cy = Y3(e.cy());
        Matrix root = then(MatrixRotateY(yaw), T(e.cx(), cy, 0.3f));
        auto at = [&](Matrix m) { return then(m, root); };
        float flap = std::sin(e.t * 18) * 0.7f;
        part(cube_, at(then(then(S(0.38f, 0.38f, 0.3f), MatrixRotateZ(PI / 4)), T(0, 0, 0))), col(0xe8364f));
        for (int s = -1; s <= 1; s += 2)
            part(cube_, at(then(then(then(S(0.32f, 0.03f, 0.42f), T(0, 0, s * 0.21f)), MatrixRotateX(s * flap)), T(-0.05f, 0.05f, s * 0.12f))), col(0x3c9cf0));
        part(sphere_, at(then(S(0.11f, 0.11f, 0.11f), T(0.2f, 0.04f, 0))), col(0xfaf3e6));
        part(sphere_, at(then(S(0.05f, 0.05f, 0.05f), T(0.29f, 0.04f, 0))), col(0x0f0d1a));
    } else {   // Fog Brute
        Matrix root = then(MatrixRotateY(yaw), T(e.cx(), feet, 0.1f));
        auto at = [&](Matrix m) { return then(m, root); };
        float step = std::sin(e.t * 7) * 0.12f;
        float lean = e.state >= 1 ? 0.25f : 0.0f;
        part(cyl_, at(then(S(0.15f, 0.5f, 0.15f), T(-0.2f + step, 0, -0.25f))), col(0x5a567a));
        part(cyl_, at(then(S(0.15f, 0.5f, 0.15f), T(-0.2f - step, 0, 0.25f))), col(0x5a567a));
        const float puffs[5][4] = {{0, 1.05f, 0, 0.58f}, {-0.3f, 1.4f, -0.2f, 0.38f}, {0.25f, 1.45f, 0.15f, 0.4f},
                                   {0.0f, 1.62f, 0, 0.34f}, {0.35f, 1.0f, -0.3f, 0.3f}};
        for (int i = 0; i < 5; ++i)
            part(sphere_, at(then(S(puffs[i][3], puffs[i][3], puffs[i][3]), T(puffs[i][0] + lean, puffs[i][1], puffs[i][2]))),
                 col(i % 2 ? 0xa8a4c8 : 0x6e6a8e));
        for (int s = -1; s <= 1; s += 2)
            part(sphere_, at(then(S(0.08f, e.state >= 1 ? 0.08f : 0.05f, 0.08f), T(0.5f + lean, 1.35f, s * 0.15f))), col(0xff4040), 1.0f);
        part(cyl_, at(then(then(S(0.15f, 0.6f, 0.15f), MatrixRotateZ(0.3f + step)), T(0.35f + lean, 1.25f, 0.45f))), col(0xa8a4c8));
    }
}

void Models3D::drawBoss(Game& g)
{
    const Boss& b = g.boss;
    float a = b.state == 5 ? std::max(0.0f, 1 - b.stateTime / 1.6f) : 1.0f;
    bool flash = (b.state == 3 && (int)(g.time * 8) % 2 == 0) || b.hurt > 0;
    auto col = [&](unsigned h) {
        Color c = hexc(h);
        if (flash) c = mixc(c, hexc(0xfff8e0), 0.5f);
        return withA(c, a);
    };
    float cx = b.x, cy = Y3(b.y) + 0.2f;
    float slam = b.state == 3 ? 0.25f : 0.0f, wob = std::sin(b.t * 3) * 0.08f;
    Matrix root = T(cx, cy + wob, 0.4f);
    auto at = [&](Matrix m) { return then(m, root); };
    for (int i = 0; i < 6; ++i) {   // tendrils
        float x = -1.0f + i * 0.4f, sway = std::sin(i * 1.7f + b.t * 3) * 0.35f;
        part(cyl_, at(then(then(then(S(0.1f, 1.0f, 0.1f), T(0, -1.0f, 0)), MatrixRotateZ(sway)), T(x, -0.6f + slam, std::cos(i * 2.0f) * 0.4f))),
             col(i % 2 ? 0x6a2c6e : 0x26305e));
    }
    part(sphere_, at(then(S(1.25f, 1.1f - slam * 0.4f, 1.0f), T(0, 0, 0))), col(0x26305e));
    part(torus_, at(then(then(S(0.55f, 0.55f, 0.55f), MatrixRotateX(PI / 2)), T(0, 0, 0.35f))), col(0x8a3a9e), 0.3f);
    part(sphere_, at(then(S(0.62f, 0.42f - slam * 0.3f, 0.3f), T(0, 0.02f, 0.85f))), col(0xfaf3e6), 0.2f);
    if (b.state != 5 && b.hurt <= 0) {
        part(sphere_, at(then(S(0.26f, 0.26f, 0.12f), T(0.1f, 0.02f, 1.08f))), col(0xf7d73a), 0.6f);
        part(sphere_, at(then(S(0.12f, 0.12f, 0.08f), T(0.12f, 0.02f, 1.16f))), col(0x0f0d1a));
    }
}

void Models3D::drawFragment(float x, float y, float t, float alpha)
{
    Matrix m = then(then(then(S(0.2f, 0.2f, 0.2f), MatrixRotateZ(PI / 4)), then(S(1, 1.6f, 1), MatrixRotateY(t * 3))),
                    T(x, Y3(y) + std::sin(t * 4) * 0.08f, 0.3f));
    part(cube_, m, withA(hexc(0xf7d73a), alpha), 0.75f);
}
