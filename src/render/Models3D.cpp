#include "render/Models3D.h"
#include "core/Game.h"
#include "core/Paths.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <string>

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

const Color kInk = {30, 22, 34, 255};   // outline colour
const float kOutline = 0.0028f;         // outline width per unit of camera distance: about 2 px at 720p

// Gran's cottage stands just behind the start and comes back every COTTAGE_EVERY tiles.
constexpr float COTTAGE_X0 = 10.0f, COTTAGE_EVERY = 72.0f;
inline float nearestCottage(float x) { return COTTAGE_X0 + std::round((x - COTTAGE_X0) / COTTAGE_EVERY) * COTTAGE_EVERY; }

}  // namespace

void Models3D::init()
{
    sh_ = LoadShader(assetPath("shaders/lit3d.vs").c_str(), assetPath("shaders/lit3d.fs").c_str());
    sh_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(sh_, "matModel");
    sh_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(sh_, "matNormal");
    locView_ = GetShaderLocation(sh_, "viewPos");
    locEmissive_ = GetShaderLocation(sh_, "emissive");
    locOutline_ = GetShaderLocation(sh_, "outlineW");
    locHull_ = GetShaderLocation(sh_, "hullMode");
    locLightPos_ = GetShaderLocation(sh_, "lightPos");
    locLightCol_ = GetShaderLocation(sh_, "lightCol");
    locLightRad_ = GetShaderLocation(sh_, "lightRad");
    auto set3 = [&](const char* n, Vector3 v) { SetShaderValue(sh_, GetShaderLocation(sh_, n), &v, SHADER_UNIFORM_VEC3); };
    auto set1 = [&](const char* n, float v) { SetShaderValue(sh_, GetShaderLocation(sh_, n), &v, SHADER_UNIFORM_FLOAT); };
    set3("sunDir", Vector3Normalize({0.45f, -0.8f, -0.4f}));
    set3("sunCol", {1.0f, 0.93f, 0.8f});
    set3("skyCol", {0.95f, 0.97f, 1.0f});
    set3("groundCol", {0.7f, 0.68f, 0.6f});
    (void)set1;

    cube_ = LoadModelFromMesh(GenMeshCube(1, 1, 1));
    sphere_ = LoadModelFromMesh(GenMeshSphere(1, 7, 10));   // low-poly
    cyl_ = LoadModelFromMesh(GenMeshCylinder(1, 1, 8));
    torus_ = LoadModelFromMesh(GenMeshTorus(0.12f, 2.0f, 28, 10));
    cone_ = LoadModelFromMesh(GenMeshCone(1, 1, 8));
    for (Model* m : {&cube_, &sphere_, &cyl_, &torus_, &cone_}) m->materials[0].shader = sh_;
    ready_ = sh_.id > 0;

    static const char* const granNames[G_COUNT] = {"gran_body", "gran_head", "gran_upperarm", "gran_forearm", "gran_thigh", "gran_shin"};
    static const char* const forgetNames[F_COUNT] = {"forget_body", "forget_seg", "forget_tip"};
    loadParts(gran_, granNames, G_COUNT, granReady_);
    loadParts(forget_, forgetNames, F_COUNT, forgetReady_);
}

void Models3D::loadParts(Model* out, const char* const* names, int n, bool& ready)
{
    ready = false;
    for (int i = 0; i < n; ++i) {
        std::string path = assetPath(std::string("models/") + names[i] + ".glb");
        if (!FileExists(path.c_str())) { unloadParts(out, i, ready); return; }
        out[i] = LoadModel(path.c_str());
        if (out[i].meshCount == 0) { UnloadModel(out[i]); unloadParts(out, i, ready); return; }
        for (int k = 0; k < out[i].materialCount; ++k) out[i].materials[k].shader = sh_;
    }
    ready = true;
}

void Models3D::unloadParts(Model* parts, int n, bool& ready)
{
    for (int i = 0; i < n; ++i) {
        for (int k = 0; k < parts[i].materialCount; ++k)
            parts[i].materials[k].shader = Shader{rlGetShaderIdDefault(), rlGetShaderLocsDefault()};
        UnloadModel(parts[i]);
        parts[i] = Model{};
    }
    ready = false;
}

// A Blender part: its own material colours, times the tint (for fading and hit flashes), then
// the dark inverted hull for the toon outline, pushed out along the smooth normals.
void Models3D::modelPart(Model& m, Matrix t, Color tint, bool outline)
{
    float zero = 0.0f, one = 1.0f;
    SetShaderValue(sh_, locEmissive_, &zero, SHADER_UNIFORM_FLOAT);
    m.transform = t;
    DrawModel(m, {0, 0, 0}, 1.0f, tint);
    if (!outline || tint.a < 250) return;
    int hull = 0;
    float w = kOutline;
    SetShaderValue(sh_, locEmissive_, &one, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh_, locOutline_, &w, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh_, locHull_, &hull, SHADER_UNIFORM_INT);
    rlEnableBackfaceCulling();
    rlSetCullFace(RL_CULL_FACE_FRONT);
    // every material drawn in the ink colour
    Color saved[16];
    int nm = std::min(m.materialCount, 16);
    for (int k = 0; k < nm; ++k) { saved[k] = m.materials[k].maps[MATERIAL_MAP_DIFFUSE].color; m.materials[k].maps[MATERIAL_MAP_DIFFUSE].color = kInk; }
    DrawModel(m, {0, 0, 0}, 1.0f, WHITE);
    for (int k = 0; k < nm; ++k) m.materials[k].maps[MATERIAL_MAP_DIFFUSE].color = saved[k];
    rlSetCullFace(RL_CULL_FACE_BACK);
    rlDisableBackfaceCulling();
    SetShaderValue(sh_, locOutline_, &zero, SHADER_UNIFORM_FLOAT);
}

void Models3D::shutdown()
{
    for (Model* m : {&cube_, &sphere_, &cyl_, &torus_, &cone_}) {
        m->materials[0].shader = Shader{rlGetShaderIdDefault(), rlGetShaderLocsDefault()};
        UnloadModel(*m);
    }
    unloadParts(gran_, granReady_ ? G_COUNT : 0, granReady_);
    unloadParts(forget_, forgetReady_ ? F_COUNT : 0, forgetReady_);
    UnloadShader(sh_);
}

void Models3D::part(Model& m, Matrix t, Color c, float emissive, bool outline)
{
    SetShaderValue(sh_, locEmissive_, &emissive, SHADER_UNIFORM_FLOAT);
    m.transform = t;
    DrawModel(m, {0, 0, 0}, 1.0f, c);
    if (!outline || c.a < 250) return;
    // tiny parts (eyes, buttons, glasses) stay as they are: an outline would swallow them
    float sx = Vector3Length({t.m0, t.m1, t.m2}), sy = Vector3Length({t.m4, t.m5, t.m6}), sz = Vector3Length({t.m8, t.m9, t.m10});
    if (std::max(sx, std::max(sy, sz)) < 0.075f) return;
    int hull = &m == &cube_ ? 1 : ((&m == &cyl_ || &m == &cone_) ? 2 : 0);
    float one = 1.0f, w = kOutline;
    SetShaderValue(sh_, locEmissive_, &one, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh_, locOutline_, &w, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh_, locHull_, &hull, SHADER_UNIFORM_INT);
    rlEnableBackfaceCulling();
    rlSetCullFace(RL_CULL_FACE_FRONT);
    DrawModel(m, {0, 0, 0}, 1.0f, kInk);
    rlSetCullFace(RL_CULL_FACE_BACK);
    rlDisableBackfaceCulling();
    float zero = 0.0f;
    SetShaderValue(sh_, locOutline_, &zero, SHADER_UNIFORM_FLOAT);
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
// One tree, picked and sized by its seed: round, tall oval, pine (stacked cones) or a bush.
void Models3D::tree(float x, float z, int seed, int kind)
{
    if (kind < 0) kind = (int)(hashi(seed, 21) % 4);
    float sc = 0.8f + rndf(seed, 22) * 0.5f, rot = rndf(seed, 23) * 2 * PI;
    Matrix root = then(then(S(sc, sc, sc), MatrixRotateY(rot)), T(x, 2, z));
    auto at = [&](Matrix m) { return then(m, root); };
    float v = rndf(seed, 24);
    Color trunk = hexc(0x6a4430);
    if (kind == 0) {   // round: a cluster of three balls
        float h = 1.3f + rndf(seed, 25) * 0.5f;
        Color leaf = mixc(hexc(0x4f9a3e), hexc(0x74b24c), v);
        part(cyl_, at(then(S(0.17f, h + 0.3f, 0.17f), T(0, 0, 0))), trunk);
        part(sphere_, at(then(S(0.95f, 0.85f, 0.95f), T(0, h + 0.55f, 0))), leaf);
        part(sphere_, at(then(S(0.62f, 0.58f, 0.62f), T(0.62f, h + 0.15f, 0.25f))), mixc(leaf, WHITE, 0.08f));
        part(sphere_, at(then(S(0.55f, 0.5f, 0.55f), T(-0.55f, h + 0.25f, -0.2f))), mixc(leaf, BLACK, 0.1f));
    } else if (kind == 1) {   // tall oval, like a poplar
        Color leaf = mixc(hexc(0x5c9c44), hexc(0x7cb456), v);
        part(cyl_, at(then(S(0.14f, 1.0f, 0.14f), T(0, 0, 0))), trunk);
        part(sphere_, at(then(S(0.62f, 1.55f, 0.62f), T(0, 2.3f, 0))), leaf);
    } else if (kind == 2) {   // pine: three stacked cones
        Color leaf = mixc(hexc(0x2e7a4c), hexc(0x449058), v);
        part(cyl_, at(then(S(0.15f, 0.7f, 0.15f), T(0, 0, 0))), hexc(0x5a3a28));
        for (int i = 0; i < 3; ++i) {
            float r = 1.0f - i * 0.25f, y = 0.5f + i * 0.75f;
            part(cone_, at(then(S(r, 1.25f, r), T(0, y, 0))), i % 2 ? mixc(leaf, WHITE, 0.06f) : leaf);
        }
    } else {   // a low bush of two or three balls
        Color leaf = mixc(hexc(0x4a8f3c), hexc(0x66a84a), v);
        part(sphere_, at(then(S(0.6f, 0.45f, 0.55f), T(0, 0.3f, 0))), leaf);
        part(sphere_, at(then(S(0.45f, 0.36f, 0.42f), T(0.55f, 0.22f, 0.1f))), mixc(leaf, WHITE, 0.07f));
        if (hashi(seed, 26) % 2)
            part(sphere_, at(then(S(0.4f, 0.32f, 0.4f), T(-0.5f, 0.2f, -0.05f))), mixc(leaf, BLACK, 0.08f));
    }
}

// A plain street house further back.
void Models3D::house(float x, float z, int seed)
{
    float w = 3.0f, h = 2.4f + rndf(seed, 5) * 0.7f, d = 2.8f;
    Color wall = mixc(hexc(0xe8d4b8), hexc(0xc89a80), rndf(seed, 6));
    Color roof = hashi(seed, 8) % 2 ? hexc(0xa0503e) : hexc(0x6a6a88);
    part(cube_, then(S(w, h, d), T(x, 2 + h / 2, z)), wall);
    float a = d / std::sqrt(2.0f);   // gable: a square turned 45 degrees, its top half above the walls
    part(cube_, then(then(S(w - 0.02f, a, a), MatrixRotateX(PI / 4)), T(x, 2 + h, z)), wall);
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        float L = d / std::sqrt(2.0f) + 0.4f;
        Vector3 c = {x, 2 + h + d / 4 + 0.06f, z + sgn * (d / 4 + 0.06f)};
        part(cube_, then(then(S(w + 0.4f, 0.14f, L), MatrixRotateX(sgn * PI / 4)), T(c.x, c.y, c.z)), roof);
    }
    part(cube_, then(S(0.6f, 1.1f, 0.06f), T(x, 2.55f, z + d / 2 + 0.03f)), hexc(0x6a3a2a));
    for (int wi = 0; wi < 2; ++wi)
        part(cube_, then(S(0.55f, 0.5f, 0.06f), T(x + (wi ? 0.9f : -0.9f), 2 + h * 0.6f, z + d / 2 + 0.03f)), hexc(0xffe2a0), 0.7f);
    part(cube_, then(S(0.35f, 0.9f, 0.35f), T(x + 0.8f, 2 + h + 0.8f, z - 0.4f)), hexc(0x8a5a4a));
}

// Gran's cottage: the hero landmark. Bigger and closer than the street houses.
void Models3D::cottage(float x, float z)
{
    const float w = 4.8f, h = 2.7f, d = 3.6f, fz = z + d / 2;   // fz: the front wall
    const Color wall = hexc(0xf4e6c8), trim = hexc(0xfaf6ee), roof = hexc(0xb4483a), wood = hexc(0x7a4a32);
    part(cube_, then(S(w + 0.1f, 0.3f, d + 0.1f), T(x, 2.15f, z)), hexc(0x9a948a));   // stone footing
    part(cube_, then(S(w, h, d), T(x, 2 + h / 2 + 0.1f, z)), wall);
    const float top = 2 + h + 0.1f;
    float a = d / std::sqrt(2.0f);
    part(cube_, then(then(S(w - 0.02f, a, a), MatrixRotateX(PI / 4)), T(x, top, z)), wall);
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        float L = d / std::sqrt(2.0f) + 0.6f;
        part(cube_, then(then(S(w + 0.6f, 0.18f, L), MatrixRotateX(sgn * PI / 4)), T(x, top + d / 4 + 0.08f, z + sgn * (d / 4 + 0.08f))), roof);
    }
    part(cube_, then(S(w + 0.7f, 0.14f, 0.2f), T(x, top + d / 2 + 0.12f, z)), mixc(roof, BLACK, 0.2f));   // ridge
    // chimney
    part(cube_, then(S(0.55f, 1.5f, 0.55f), T(x + 1.4f, top + 1.2f, z - 0.6f)), hexc(0xa0604a));
    part(cube_, then(S(0.7f, 0.15f, 0.7f), T(x + 1.4f, top + 2.0f, z - 0.6f)), hexc(0x7a4a3a));
    // door with a frame and a knob
    part(cube_, then(S(1.0f, 1.65f, 0.08f), T(x, 2.1f + 0.82f, fz + 0.03f)), trim);
    part(cube_, then(S(0.78f, 1.5f, 0.08f), T(x, 2.1f + 0.75f, fz + 0.07f)), hexc(0x3f7f5a));
    part(sphere_, then(S(0.05f, 0.05f, 0.05f), T(x + 0.26f, 2.9f, fz + 0.13f)), hexc(0xf7d73a));
    // two windows each side of the door: frame, lit glass, a cross of bars, shutters and a sill box
    for (int s = -1; s <= 1; s += 2) {
        float wx = x + s * 1.55f, wy = 2.1f + h * 0.55f;
        part(cube_, then(S(1.0f, 0.9f, 0.08f), T(wx, wy, fz + 0.03f)), trim);
        part(cube_, then(S(0.8f, 0.7f, 0.08f), T(wx, wy, fz + 0.06f)), hexc(0xffe2a0), 0.7f, false);
        part(cube_, then(S(0.06f, 0.7f, 0.08f), T(wx, wy, fz + 0.09f)), trim, 0, false);
        part(cube_, then(S(0.8f, 0.06f, 0.08f), T(wx, wy, fz + 0.09f)), trim, 0, false);
        for (int k = -1; k <= 1; k += 2)
            part(cube_, then(S(0.28f, 0.9f, 0.06f), T(wx + k * 0.66f, wy, fz + 0.04f)), hexc(0x3f7f5a));
        part(cube_, then(S(0.9f, 0.18f, 0.25f), T(wx, wy - 0.55f, fz + 0.14f)), wood);
        for (int f = 0; f < 3; ++f)
            part(sphere_, then(S(0.09f, 0.09f, 0.09f), T(wx - 0.28f + f * 0.28f, wy - 0.4f, fz + 0.14f)), f % 2 ? hexc(0xf28cb4) : hexc(0xe8364f));
    }
    // a little window in the gable
    part(cube_, then(S(0.6f, 0.5f, 0.08f), T(x, top + 0.55f, fz - 0.5f)), trim);
    part(cube_, then(S(0.42f, 0.34f, 0.08f), T(x, top + 0.55f, fz - 0.46f)), hexc(0xffe2a0), 0.7f, false);
    // porch: a deck, two posts and a little roof over the door
    part(cube_, then(S(2.2f, 0.18f, 1.1f), T(x, 2.12f, fz + 0.55f)), hexc(0xb07a50));
    for (int s = -1; s <= 1; s += 2)
        part(cube_, then(S(0.12f, 1.9f, 0.12f), T(x + s * 1.0f, 2.2f + 0.95f, fz + 1.0f)), trim);
    part(cube_, then(then(S(2.5f, 0.12f, 1.35f), MatrixRotateX(0.3f)), T(x, 4.2f, fz + 0.6f)), roof);
    // stepping stones out to the gate
    for (int i = 0; i < 2; ++i)
        part(cube_, then(S(0.6f, 0.06f, 0.4f), T(x + (i % 2 ? 0.1f : -0.1f), 2.02f, fz + 1.35f + i * 0.5f)), hexc(0xb8b0a0));
    // garden fence along both sides of the front yard, up to the street fence
    for (int s = -1; s <= 1; s += 2) {
        float fx = x + s * (w / 2 + 0.5f);
        for (float zz = z - 0.6f; zz < -3.6f; zz += 0.45f)
            part(cube_, then(S(0.08f, 0.6f, 0.1f), T(fx, 2.3f, zz)), trim);
        part(cube_, then(S(0.05f, 0.07f, -3.6f - (z - 0.6f)), T(fx, 2.45f, (z - 0.6f - 3.6f) / 2)), trim, 0, false);
    }
    // mailbox by the gate
    float mx = x + 1.5f, mz = -3.85f;
    part(cube_, then(S(0.1f, 1.0f, 0.1f), T(mx, 2.5f, mz)), wood);
    part(cube_, then(S(0.5f, 0.32f, 0.3f), T(mx, 3.12f, mz)), hexc(0x3c6cb0));
    part(cube_, then(S(0.04f, 0.3f, 0.06f), T(mx + 0.27f, 3.3f, mz + 0.08f)), hexc(0xe8364f));
    // flowers along the front wall
    for (int f = 0; f < 8; ++f) {
        float px = x - w / 2 + 0.3f + f * 0.6f;
        if (std::fabs(px - x) < 0.7f) continue;
        part(sphere_, then(S(0.22f, 0.18f, 0.2f), T(px, 2.12f, fz + 0.3f)), hexc(0x4a8f3c));
        part(sphere_, then(S(0.08f, 0.08f, 0.08f), T(px + 0.05f, 2.32f, fz + 0.36f)), f % 2 ? hexc(0xf7d73a) : hexc(0xf28cb4));
    }
}

void Models3D::drawScenery(Game& g)
{
    float l = g.cam.left() - 16, r = g.cam.right() + 36, mid = (l + r) / 2, wdt = r - l;
    // lawns. In front of the path the lawn lies at the foot of the path (pits are ditches down to
    // it); behind it, at path height, in flat bands that step lighter towards the horizon.
    part(cube_, then(S(wdt + 300, 0.4f, 18), T(mid, -0.2f, 6.0f)), hexc(0x4a9040), 0, false);
    part(cube_, then(S(wdt + 300, 1.9f, 4), T(mid, 0.95f, -5.0f)), hexc(0x6a4a34), 0, false);   // bank behind the ditches
    const float bandZ[5] = {-3.0f, -14.0f, -30.0f, -55.0f, -120.0f};
    const unsigned bandC[4] = {0x5aa04a, 0x68ab54, 0x79b764, 0x8cc378};
    for (int i = 0; i < 4; ++i) {
        float z0 = bandZ[i + 1], z1 = bandZ[i];
        part(cube_, then(S(wdt + 300, 0.1f, z1 - z0), T(mid, 1.95f, (z0 + z1) / 2)), hexc(bandC[i]), 0, false);
    }
    float cot = nearestCottage(mid);
    bool cotOn = cot > l - 8 && cot < r + 8;
    auto nearCot = [&](float x, float m) { return cotOn && std::fabs(x - cot) < m; };

    // a low picket fence along the back of the path, with a gate at the cottage
    for (int x = (int)std::floor(l); x < (int)r; ++x) {
        if (nearCot(x + 0.5f, 0.8f)) continue;
        part(cube_, then(S(0.12f, 0.7f, 0.08f), T(x + 0.5f, 2.35f, -3.5f)), hexc(0xf4f0e6));
    }
    part(cube_, then(S(wdt, 0.08f, 0.05f), T(mid, 2.5f, -3.46f)), hexc(0xe8e2d6), 0, false);
    part(cube_, then(S(wdt, 0.08f, 0.05f), T(mid, 2.2f, -3.46f)), hexc(0xe8e2d6), 0, false);
    if (cotOn) cottage(cot, -7.0f);

    // near row: bushes, flower beds and lamps, just behind the fence
    for (int k = (int)std::floor(l / 3); k <= (int)(r / 3); ++k) {
        float x = k * 3.0f + 1.5f + (rndf(k, 31) - 0.5f) * 1.2f;
        if (nearCot(x, 4.5f)) continue;
        int kind = (int)(hashi(k, 32) % 6);
        if (kind == 0) {   // street lamp
            float z = -4.3f;
            part(cyl_, then(S(0.07f, 2.6f, 0.07f), T(x, 2, z)), hexc(0x2a2a36));
            part(cube_, then(S(0.3f, 0.32f, 0.3f), T(x, 4.75f, z)), hexc(0xfff0c0), 1.0f);
            part(cube_, then(S(0.42f, 0.08f, 0.42f), T(x, 4.95f, z)), hexc(0x2a2a36));
        } else if (kind == 1) {   // flower bed
            for (int f = 0; f < 6; ++f) {
                float fx = x - 1 + f * 0.4f, fz = -4.2f + std::sin(f * 2.1f) * 0.3f;
                part(cyl_, then(S(0.03f, 0.35f, 0.03f), T(fx, 2, fz)), hexc(0x3a7a3a), 0, false);
                part(sphere_, then(S(0.12f, 0.12f, 0.12f), T(fx, 2.4f, fz)), f % 2 ? hexc(0xf28cb4) : hexc(0xf7d73a));
            }
        } else if (kind <= 3) {
            tree(x, -4.4f - rndf(k, 33) * 0.6f, k * 7 + 3, 3);   // a bush
        }
    }
    // middle row: trees of every kind
    for (int k = (int)std::floor(l / 3.5f); k <= (int)(r / 3.5f); ++k) {
        if (hashi(k, 41) % 5 >= 3) continue;
        float x = k * 3.5f + (rndf(k, 42) - 0.5f) * 1.6f;
        if (nearCot(x, 4.0f)) continue;
        tree(x, -6.0f - rndf(k, 43) * 2.5f, k * 13 + 1);
    }
    // houses along the back of the street
    for (int k = (int)std::floor(l / 8); k <= (int)(r / 8); ++k) {
        if (hashi(k, 51) % 3 == 0) continue;
        float x = k * 8.0f + 4.0f + (rndf(k, 52) - 0.5f) * 1.5f;
        if (nearCot(x, 6.0f)) continue;
        house(x, -11.5f - rndf(k, 53) * 1.5f, k);
    }
    // far rows of trees
    for (int k = (int)std::floor((l - 10) / 2.5f); k <= (int)((r + 20) / 2.5f); ++k) {
        if (hashi(k, 61) % 3 == 0) continue;
        float x = k * 2.5f + (rndf(k, 62) - 0.5f) * 1.5f;
        tree(x, -15.0f - rndf(k, 63) * 9.0f, k * 17 + 5);
    }
}

// ------------------------------------------------------------------ the ground
// The solid tiles of the path as blocks: earth under a grass cap with a clear lip. Each row
// of ground is merged into long boxes; floating blocks stay single so they read as blocks.
void Models3D::drawGround(Game& g)
{
    const float zf = 1.0f, zb = -3.0f, zc = (zf + zb) / 2, dz = zf - zb;
    int x0 = (int)std::floor(g.cam.left()) - 6, x1 = (int)std::ceil(g.cam.right()) + 18;
    // left of the level's first column the level reports solid rock; show plain ground there
    auto tileAt = [&](int x, int y) { return x < g.level.firstCol() ? (y >= LEVEL_H - 2 ? '#' : '.') : g.level.tile(x, y); };
    auto solid = [&](int x, int y) { return y >= LEVEL_H || g.level.isSolidChar(tileAt(x, y)); };
    auto groundDepth = [&](int x, int y) {   // rows of solid above this one
        int d = 0;
        while (d < 3 && y - d - 1 >= 0 && solid(x, y - d - 1)) ++d;
        return d;
    };
    for (int ty = 0; ty < LEVEL_H; ++ty) {
        int x = x0;
        while (x <= x1) {
            char ch = tileAt(x, ty);
            if (!modelsTile(ch) && ch != '1' && ch != '2') { ++x; continue; }
            bool floating = !solid(x, ty + 1) && ty < LEVEL_H - 1;
            if (ch != '#' || floating) {   // a single block
                Color c = ch == '=' ? hexc(0xa08a74) : (ch == '|' ? hexc(0x8a8aa4) : hexc(0xb89a7a));
                if (ch == '1' || ch == '2') {   // memory blocks only one of the two can stand on
                    c = mixc(ch == '1' ? hexc(0xf09a40) : hexc(0x4a94f0), WHITE, 0.12f + 0.12f * std::sin(g.time * 3 + x));
                    part(cube_, then(S(0.9f, 0.9f, dz * 0.6f), T(x + 0.5f, Y3(ty + 0.5f), zc)), c, 0.25f);
                    ++x;
                    continue;
                }
                part(cube_, then(S(0.96f, 0.96f, dz), T(x + 0.5f, Y3(ty + 0.5f), zc)), c);
                if (ch == '=') part(cube_, then(S(0.5f, 0.06f, 0.06f), T(x + 0.45f, Y3(ty + 0.45f), zf + 0.02f)), hexc(0x4a3a30), 0, false);
                if (!solid(x, ty - 1) && ch == '#')
                    part(cube_, then(S(1.0f, 0.22f, dz + 0.08f), T(x + 0.5f, Y3(ty + 0.09f), zc)), hexc(0x62c24a));
                ++x;
                continue;
            }
            // a run of ground with the same depth below the surface
            int dep = groundDepth(x, ty), e = x;
            while (e + 1 <= x1 && tileAt(e + 1, ty) == '#' && solid(e + 1, ty + 1) == solid(x, ty + 1) &&
                   groundDepth(e + 1, ty) == dep)
                ++e;
            float w = (float)(e - x + 1), cx = x + w / 2;
            Color dirt = dep == 0 ? hexc(0x8a5a3a) : (dep == 1 ? hexc(0x74482e) : hexc(0x5e3a26));
            if (dep == 0)   // stops under the grass cap, so the two tops never fight
                part(cube_, then(S(w, 0.9f, dz), T(cx, Y3(ty + 0.55f), zc)), dirt);
            else
                part(cube_, then(S(w, 1.0f, dz), T(cx, Y3(ty + 0.5f), zc)), dirt, 0, false);
            if (dep == 0) {   // grass cap with a lip that overhangs the earth a little
                // its top sits flush with the path (y 0) so drop shadows lie on it cleanly
                part(cube_, then(S(w + 0.08f, 0.26f, dz + 0.1f), T(cx, Y3(ty + 0.13f), zc)), hexc(0x62c24a));
            }
            for (int i = x; i <= e; ++i)   // pebbles in the earth face
                for (int k = 0; k < 2; ++k)
                    if (hashi(i * 2 + k, ty + 90) % 2 == 0)
                        part(cube_, then(S(0.16f, 0.09f, 0.04f), T(i + 0.2f + rndf(i * 2 + k, 91) * 0.6f,
                                                                    Y3(ty + (dep == 0 ? 0.4f : 0.15f) + rndf(i * 2 + k, 92) * 0.5f), zf + 0.01f)),
                             mixc(dirt, WHITE, 0.3f), 0, false);
            x = e + 1;
        }
    }
}

// ------------------------------------------------------------------ Gran and Sam
void Models3D::drawPlayer(Game& g, int p)
{
    if (p == 0 && granReady_) { drawGranModel(g); return; }
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
    if (forgetReady_) { drawForgetting(g); return; }
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

// ------------------------------------------------------------------ Gran, modelled in Blender
// A run cycle like the reference: leaning into it, elbows bent, knees lifting.
void Models3D::drawGranModel(Game& g)
{
    const Player& pl = g.players[0];
    float a = 1.0f;
    if (pl.invuln > 0 && !pl.faded && (int)(pl.invuln * 12) % 2 == 0) a = 0.35f;
    if (pl.faded) a = 0.45f;
    Color tint = withA(pl.faded ? hexc(0xb8b8c8) : WHITE, a);

    float yaw = pl.facing > 0 ? -0.52f : PI + 0.52f;
    float ph = g.time * 12.0f;
    bool running = pl.onGround && std::fabs(pl.vx) > 0.6f;
    float run = running ? std::min(1.0f, std::fabs(pl.vx) / 4.0f + 0.4f) : 0.0f;
    float s = std::sin(ph), c = std::cos(ph);

    // joint angles (radians); positive swings a limb forward (+X)
    float thighF = 0.75f * s * run, thighB = -0.75f * s * run;
    float kneeF = -(0.25f + std::max(0.0f, -c) * 1.2f) * run, kneeB = -(0.25f + std::max(0.0f, c) * 1.2f) * run;
    float armF = -0.6f * s * run - 0.05f, armB = 0.6f * s * run - 0.05f;
    float elbow = 0.25f + 1.15f * run;
    float lean = -0.22f * run, bob = std::fabs(s) * 0.05f * run, headNod = 0.12f * run;
    if (!pl.onGround && !pl.gliding) {   // jumping: knees up, arms thrown up
        thighF = 1.0f; kneeF = -1.2f; thighB = -0.2f; kneeB = -0.6f;
        armF = 2.5f; armB = 2.1f; elbow = 0.4f; lean = -0.1f; bob = 0;
    }
    if (pl.gliding || pl.frame == art::PF_SPECIAL) { armF = 3.0f; elbow = 0.1f; armB = 0.3f; }
    if (pl.attackTime > 0) { armF = 1.57f; elbow = 0.0f; }
    float crouch = 0.0f;
    if (pl.pounding) { thighF = thighB = 1.2f; kneeF = kneeB = -1.6f; armF = armB = -0.4f; crouch = 0.18f; lean = 0.1f; }
    if (pl.dashTime > 0) lean = -0.45f;

    Matrix root = then(MatrixRotateY(yaw), T(pl.cx(), Y3(pl.bottom()) + bob - crouch, 0.2f));
    const float hipY = 0.55f;
    Matrix hips = then(then(MatrixRotateZ(lean), T(0, hipY, 0)), root);
    auto R = MatrixRotateZ;

    // legs first (behind), the far one darker by the light anyway
    for (int side = 0; side < 2; ++side) {
        float z = side ? 0.1f : -0.1f;
        float th = side ? thighF : thighB, kn = side ? kneeF : kneeB;
        // legs hang from the hips but do not lean with the body
        Matrix thigh = then(R(th - lean), then(T(0, 0, z), hips));
        modelPart(gran_[G_THIGH], thigh, tint);
        Matrix shin = then(R(kn), then(T(0, -0.26f, 0), thigh));
        modelPart(gran_[G_SHIN], shin, tint);
    }
    // far arm, body, head, near arm
    auto arm = [&](float z, float sw) {
        Matrix up = then(R(sw), then(T(0, 0.4f, z), hips));
        modelPart(gran_[G_UPPERARM], up, tint);
        Matrix fore = then(R(elbow), then(T(0, -0.21f, 0), up));
        modelPart(gran_[G_FOREARM], fore, tint);
    };
    arm(-0.25f, armB);
    modelPart(gran_[G_BODY], hips, tint);
    modelPart(gran_[G_HEAD], then(R(headNod * 0.5f + (pl.onGround ? 0 : 0.1f)), then(T(0, 0.48f, 0), hips)), tint);
    arm(0.25f, armF);

    // her umbrella when she glides
    if (pl.gliding || pl.frame == art::PF_SPECIAL) {
        auto at = [&](Matrix m) { return then(m, root); };
        part(cyl_, at(then(S(0.02f, 0.7f, 0.02f), T(0.1f, 1.25f, 0.25f))), withA(hexc(0x3a2a2a), a));
        part(sphere_, at(then(S(0.62f, 0.22f, 0.62f), T(0.1f, 1.95f, 0.25f))), withA(hexc(0xb03a6a), a));
    }
}

// ------------------------------------------------------------------ the Forgetting, modelled in Blender
// A big tentacle alien that floats and bobs, eye on the players, its eight tentacles curling
// in slow waves; they splay out when it slams down.
void Models3D::drawForgetting(Game& g)
{
    const Boss& b = g.boss;
    float a = b.state == 5 ? std::max(0.0f, 1 - b.stateTime / 1.6f) : 1.0f;
    bool flash = (b.state == 3 && (int)(g.time * 8) % 2 == 0) || b.hurt > 0;
    Color tint = withA(flash ? Color{255, 210, 220, 255} : WHITE, a);

    float t = b.t;
    float bob = std::sin(t * 2.2f) * 0.15f;
    float slam = b.state == 3 ? 1.0f : 0.0f;
    const float k = 1.8f;    // size in the level
    // turned toward the camera and a little toward the nearest player
    float px = g.players[0].cx();
    if (g.players[1].joined && std::fabs(g.players[1].cx() - b.x) < std::fabs(px - b.x)) px = g.players[1].cx();
    float yaw = px < b.x ? -PI / 2 - 0.7f : -PI / 2 + 0.7f;
    float tilt = std::sin(t * 1.3f) * 0.06f;
    Matrix root = then(then(then(S(k, k, k), MatrixRotateX(tilt)), MatrixRotateY(yaw)), T(b.x, Y3(b.y) + 0.4f + bob, 0.4f));

    modelPart(forget_[F_BODY], root, tint);
    // tentacles hang from a ring under the dome; each is 5 segments and a curled tip
    const int N = 8, SEGS = 5;
    for (int i = 0; i < N; ++i) {
        float ang = (i + 0.5f) * 2 * PI / N;
        // ring position (game space, before root): x forward, z sideways
        float rx = std::cos(ang) * 0.55f, rz = std::sin(ang) * 0.52f;
        // turn the chain so its "forward" bend axis points out from the centre
        Matrix m = then(then(MatrixRotateZ(0.35f + 0.6f * slam), MatrixRotateY(-ang)), then(T(rx, -0.5f, rz), root));
        float sc = 1.0f;
        for (int j = 0; j < SEGS; ++j) {
            modelPart(forget_[F_SEG], then(S(sc, sc, sc), m), tint);
            float wave = std::sin(t * 2.6f + i * 0.9f - j * 0.8f) * (0.32f - 0.2f * slam) + 0.06f * j * (1 - slam);
            float sway = std::cos(t * 1.9f + i * 1.7f - j * 0.6f) * 0.15f;
            m = then(then(MatrixRotateZ(wave), MatrixRotateX(sway)), then(T(0, -0.38f * sc, 0), m));
            sc *= 0.86f;
        }
        modelPart(forget_[F_TIP], then(S(sc, sc, sc), m), tint);
    }
}
