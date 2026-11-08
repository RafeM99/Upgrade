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
    locRim_ = GetShaderLocation(sh_, "rim");
    locLightPos_ = GetShaderLocation(sh_, "lightPos");
    locLightCol_ = GetShaderLocation(sh_, "lightCol");
    locLightRad_ = GetShaderLocation(sh_, "lightRad");
    locSunDir_ = GetShaderLocation(sh_, "sunDir");
    locSunCol_ = GetShaderLocation(sh_, "sunCol");
    locSkyCol_ = GetShaderLocation(sh_, "skyCol");
    locGroundCol_ = GetShaderLocation(sh_, "groundCol");
    locBlend_ = GetShaderLocation(sh_, "blendCol");
    locGrey_ = GetShaderLocation(sh_, "grey");
    setLight(false);

    cube_ = LoadModelFromMesh(GenMeshCube(1, 1, 1));
    sphere_ = LoadModelFromMesh(GenMeshSphere(1, 12, 18));   // smooth enough beside the Blender meshes
    cyl_ = LoadModelFromMesh(GenMeshCylinder(1, 1, 8));
    torus_ = LoadModelFromMesh(GenMeshTorus(0.12f, 2.0f, 28, 10));
    cone_ = LoadModelFromMesh(GenMeshCone(1, 1, 8));
    for (Model* m : {&cube_, &sphere_, &cyl_, &torus_, &cone_}) m->materials[0].shader = sh_;
    ready_ = sh_.id > 0;

    static const char* const personNames[2][P_COUNT] = {
        {"gran_body", "gran_head", "gran_upperarm", "gran_forearm", "gran_thigh", "gran_shin"},
        {"sam_body", "sam_head", "sam_upperarm", "sam_forearm", "sam_thigh", "sam_shin"}};
    static const char* const clockNames[C_COUNT] = {"clock_case", "clock_pendulum", "clock_hour", "clock_minute", "clock_foot_l", "clock_foot_r"};
    static const char* const forgetNames[F_COUNT] = {"forget_body", "forget_seg", "forget_tip", "forget_pupil"};
    for (int p = 0; p < 2; ++p) loadParts(person_[p], personNames[p], P_COUNT, personReady_[p]);
    loadParts(clock_, clockNames, C_COUNT, clockReady_);
    loadParts(forget_, forgetNames, F_COUNT, forgetReady_);
}

// The 3D stage's fresh morning light, or (dusk) the low golden sun of the 2.5D hay meadow,
// coming from the left like the light painted on its backdrop.
void Models3D::setLight(bool dusk)
{
    auto set3 = [&](int loc, Vector3 v) { SetShaderValue(sh_, loc, &v, SHADER_UNIFORM_VEC3); };
    if (dusk) {
        set3(locSunDir_, Vector3Normalize({0.6f, -0.55f, -0.45f}));
        set3(locSunCol_, {1.0f, 0.86f, 0.66f});
        set3(locSkyCol_, {1.0f, 0.88f, 0.8f});
        set3(locGroundCol_, {0.72f, 0.62f, 0.5f});
    } else {
        set3(locSunDir_, Vector3Normalize({0.45f, -0.8f, -0.4f}));
        set3(locSunCol_, {1.0f, 0.93f, 0.8f});
        set3(locSkyCol_, {0.95f, 0.97f, 1.0f});
        set3(locGroundCol_, {0.7f, 0.68f, 0.6f});
    }
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

// A Blender part: its own material colours, times the tint (for fading and hit flashes), with
// the lighter rim along its silhouette.
void Models3D::modelPart(Model& m, Matrix t, Color tint, bool rim)
{
    float zero = 0.0f, r = rim && rims_ ? 1.0f : 0.0f;
    SetShaderValue(sh_, locEmissive_, &zero, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh_, locRim_, &r, SHADER_UNIFORM_FLOAT);
    pushLook();
    m.transform = t;
    DrawModel(m, {0, 0, 0}, 1.0f, tint);
}

void Models3D::pushLook()
{
    SetShaderValue(sh_, locBlend_, &blend_, SHADER_UNIFORM_VEC4);
    SetShaderValue(sh_, locGrey_, &grey_, SHADER_UNIFORM_FLOAT);
}

void Models3D::shutdown()
{
    for (Model* m : {&cube_, &sphere_, &cyl_, &torus_, &cone_}) {
        m->materials[0].shader = Shader{rlGetShaderIdDefault(), rlGetShaderLocsDefault()};
        UnloadModel(*m);
    }
    for (int p = 0; p < 2; ++p) unloadParts(person_[p], personReady_[p] ? P_COUNT : 0, personReady_[p]);
    unloadParts(clock_, clockReady_ ? C_COUNT : 0, clockReady_);
    unloadParts(forget_, forgetReady_ ? F_COUNT : 0, forgetReady_);
    UnloadShader(sh_);
}

void Models3D::part(Model& m, Matrix t, Color c, float emissive, bool rim)
{
    // boxes get no rim: their flat faces would light up whole, and the face bands already
    // separate them. Rounded parts get the thin lighter rim.
    float r = rim && rims_ && &m != &cube_ ? 1.0f : 0.0f;
    SetShaderValue(sh_, locEmissive_, &emissive, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh_, locRim_, &r, SHADER_UNIFORM_FLOAT);
    pushLook();
    m.transform = t;
    DrawModel(m, {0, 0, 0}, 1.0f, c);
}

void Models3D::begin(Game& g, const Camera3D& cam)
{
    rlDrawRenderBatchActive();
    camPos_ = cam.position;
    setLook({0, 0, 0, 0}, 0.0f);
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

    // a low picket fence along the back of the path, with a gate at the cottage. Each post is
    // hand-set: a little off its mark, a touch taller or shorter, now and then leaning.
    for (int x = (int)std::floor(l); x < (int)r; ++x) {
        float px = x + 0.5f + (rndf(x, 301) - 0.5f) * 0.26f;
        if (nearCot(px, 0.8f)) continue;
        float ph = 0.66f + 0.1f * rndf(x, 302);
        float lean = (rndf(x, 303) - 0.5f) * (hashi(x, 304) % 6 == 0 ? 0.16f : 0.04f);
        Color pc = mixc(hexc(0xf4f0e6), hexc(0xe2dccd), rndf(x, 305));
        part(cube_, then(then(then(S(0.12f, ph, 0.08f), T(0, ph / 2, 0)), MatrixRotateZ(lean)), T(px, 2.0f, -3.5f)), pc);
    }
    part(cube_, then(S(wdt, 0.08f, 0.05f), T(mid, 2.5f, -3.46f)), hexc(0xe8e2d6), 0, false);
    part(cube_, then(S(wdt, 0.08f, 0.05f), T(mid, 2.2f, -3.46f)), hexc(0xe8e2d6), 0, false);
    if (cotOn) cottage(cot, -7.0f);
    drawGardenFront(g, g.cam.left() - 4, g.cam.right() + 10);

    // near row: bushes, flower beds and lamps, just behind the fence. Narrow world-space cells,
    // each one taken by chance unless the cell before it is taken, so the gaps run 2.5, 3.75, 5
    // ... units and never repeat in a pattern.
    auto rowTaken = [](int k) { return hashi(k, 30) % 100 < 58; };
    auto slotKind = [&](int k) {   // -1 empty, 0 lamp, 1 flower bed, 2 bush
        if (!rowTaken(k) || rowTaken(k - 1)) return -1;
        unsigned kh = hashi(k, 32) % 8;
        return kh < 2 ? 0 : (kh < 4 ? 1 : 2);
    };
    for (int k = (int)std::floor(l / 1.25f) - 1; k <= (int)(r / 1.25f); ++k) {
        int kind = slotKind(k);
        if (kind < 0) continue;
        float x = k * 1.25f + 0.625f + (rndf(k, 31) - 0.5f) * 0.7f;
        if (nearCot(x, 4.5f)) continue;
        if (kind == 0 && (slotKind(k - 2) == 0 || slotKind(k - 3) == 0 || slotKind(k - 4) == 0)) kind = 2;   // no lamps in a huddle
        // a lamp would poke through a platform: a bush instead. Standing behind the path, the 3/4
        // view lines it up on screen with the path columns where the line of sight from the
        // camera crosses the path (z 1 to -3): check those.
        if (kind == 0) {
            auto across = [&](float zp) { return camPos_.x + (x - camPos_.x) * (camPos_.z - zp) / (camPos_.z + 4.3f); };
            float a = across(1.0f), b = across(-3.0f);
            if (!clearAbove(g, std::min(a, b) - 0.8f, std::max(a, b) + 0.8f)) kind = 2;
        }   // a lamp would poke through a platform: a bush
        if (kind == 0) {   // street lamp (on the line clearAbove checks)
            float z = -4.3f;
            part(cyl_, then(S(0.07f, 2.6f, 0.07f), T(x, 2, z)), hexc(0x2a2a36));
            part(cube_, then(S(0.3f, 0.32f, 0.3f), T(x, 4.75f, z)), hexc(0xfff0c0), 1.0f);
            part(cube_, then(S(0.42f, 0.08f, 0.42f), T(x, 4.95f, z)), hexc(0x2a2a36));
        } else if (kind == 1) {   // flower bed: a loose drift of 4 to 7 flowers
            static const unsigned bed[4] = {0xf28cb4, 0xf7d73a, 0xe8364f, 0xf6f2e4};
            int n = 4 + (int)(hashi(k, 34) % 4);
            float w = 1.2f + 0.8f * rndf(k, 35);
            for (int f = 0; f < n; ++f) {
                int s = k * 8 + f;
                float fx = x - w / 2 + w * (f + 0.15f + 0.7f * rndf(s, 36)) / n, fz = -4.2f + (rndf(s, 37) - 0.5f) * 0.6f;
                float fh = 0.28f + 0.14f * rndf(s, 38), fr = 0.1f + 0.04f * rndf(s, 39);
                part(cyl_, then(S(0.03f, fh, 0.03f), T(fx, 2, fz)), hexc(0x3a7a3a), 0, false);
                part(sphere_, then(S(fr, fr * 0.85f, fr), T(fx, 2 + fh + 0.04f, fz)), hexc(bed[hashi(s, 40) % 4]));
            }
        } else {
            tree(x, -4.4f - rndf(k, 33) * 0.6f, k * 7 + 3, 3);   // a bush
        }
    }
    // middle row: trees of every kind
    for (int k = (int)std::floor(l / 3.5f); k <= (int)(r / 3.5f); ++k) {
        if (hashi(k, 41) % 5 >= 3) continue;
        float x = k * 3.5f + (rndf(k, 42) - 0.5f) * 2.6f;
        if (nearCot(x, 4.0f)) continue;
        tree(x, -6.0f - rndf(k, 43) * 2.5f, k * 13 + 1);
    }
    // houses along the back of the street
    for (int k = (int)std::floor(l / 8); k <= (int)(r / 8); ++k) {
        if (hashi(k, 51) % 3 == 0) continue;
        float x = k * 8.0f + 4.0f + (rndf(k, 52) - 0.5f) * 3.6f;
        if (nearCot(x, 6.0f)) continue;
        house(x, -11.5f - rndf(k, 53) * 1.5f, k);
    }
    // far rows of trees
    for (int k = (int)std::floor((l - 10) / 2.5f); k <= (int)((r + 20) / 2.5f); ++k) {
        if (hashi(k, 61) % 3 == 0) continue;
        float x = k * 2.5f + (rndf(k, 62) - 0.5f) * 2.3f;
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
            // stones in the earth face: none to three a tile, each its own size, shape, tilt,
            // depth and kind of stone, all from the tile's world position
            static const unsigned stone[5] = {0xc8bca8, 0x9a948c, 0xc4a87a, 0x7a6e64, 0xdcd4c6};
            const float yTop = dep == 0 ? 0.34f : 0.06f, yBot = 0.94f;
            for (int i = x; i <= e; ++i) {
                unsigned nh = hashi(i, ty * 131 + 90) % 10;
                int n = nh < 2 ? 0 : (nh < 6 ? 1 : (nh < 9 ? 2 : 3));
                for (int k = 0; k < n; ++k) {
                    int s = (i * 4 + k) * 61 + ty;
                    float px = i + 0.06f + 0.88f * rndf(s, 91), py = yTop + (yBot - yTop) * rndf(s, 92);
                    float sz = 0.055f + 0.09f * rndf(s, 93) * rndf(s, 94);   // mostly small, the odd big one
                    float sq = 0.45f + 0.4f * rndf(s, 95), rot = (rndf(s, 96) - 0.5f) * 1.6f;
                    Color c = mixc(hexc(stone[hashi(s, 97) % 5]), dirt, 0.18f + 0.3f * rndf(s, 98));
                    Matrix face = then(MatrixRotateZ(rot), T(px, Y3(ty + py), zf + 0.005f));
                    if (hashi(s, 99) % 3)   // a rounded stone: a thin disc set into the face
                        part(cyl_, then(then(then(T(0, -0.5f, 0), S(sz, 0.035f, sz * sq)), MatrixRotateX(PI / 2)), face), c, 0, false);
                    else                    // a chip of broken stone
                        part(cube_, then(S(sz * 1.6f, sz * sq * 1.6f, 0.035f), face), c, 0, false);
                }
            }
            x = e + 1;
        }
    }
}

// ------------------------------------------------------------------ small props
// A tuft of tall grass: a fan of thin blades leaning out from one root.
void Models3D::grassTuft(float x, float y, float z, float h, int seed, Color c)
{
    int n = 4 + (int)(hashi(seed, 101) % 3);
    float ry = rndf(seed, 102) * PI;
    for (int i = 0; i < n; ++i) {
        float a = (i - (n - 1) * 0.5f) * 0.24f + (rndf(seed + i, 103) - 0.5f) * 0.2f;
        float hh = h * (0.65f + 0.35f * rndf(seed + i, 104));
        Color bc = i % 2 ? mixc(c, WHITE, 0.12f) : mixc(c, BLACK, 0.08f * (i % 3));
        part(cone_, then(then(then(S(0.05f, hh, 0.05f), MatrixRotateZ(a)), MatrixRotateY(ry)), T(x + a * 0.15f, y, z)), bc, 0, false);
    }
}

// A clump of one kind of wild flower: poppies, daisies, cornflowers, buttercups or clover.
void Models3D::flowerClump(float x, float y, float z, int seed, float size, bool stems)
{
    static const unsigned heads[5] = {0xd8382e, 0xf6f2e4, 0x4a6ad0, 0xf2cc3a, 0xd88ab0};
    int kind = (int)(hashi(seed, 111) % 5), n = 3 + (int)(hashi(seed, 112) % 4);
    for (int i = 0; i < n; ++i) {
        float px = x + (rndf(seed + i, 113) - 0.5f) * 0.9f * size, pz = z + (rndf(seed + i, 114) - 0.5f) * 0.5f * size;
        float hh = (0.2f + 0.22f * rndf(seed + i, 115)) * size, r = 0.065f * size;
        if (stems) part(cyl_, then(S(0.012f, hh, 0.012f), T(px, y, pz)), hexc(0x4a7a34), 0, false);
        part(sphere_, then(S(r, r * 0.7f, r), T(px, y + hh, pz)), hexc(heads[kind]), 0.15f, false);
        if (kind == 1) part(sphere_, then(S(r * 0.4f, r * 0.4f, r * 0.4f), T(px, y + hh + r * 0.45f, pz)), hexc(0xf2c030), 0.2f, false);
    }
}

// True when nothing solid hangs over this stretch of the path (a floating platform, a step):
// tall props (lamps, trees by the path) are only put where they cannot poke through one.
bool Models3D::clearAbove(Game& g, float x0, float x1)
{
    for (int ix = (int)std::floor(x0); ix <= (int)std::floor(x1); ++ix) {
        if (ix < g.level.firstCol()) continue;
        for (int ty = 0; ty < LEVEL_H; ++ty) {
            char ch = g.level.tile(ix, ty);
            bool solid = g.level.isSolidChar(ch) || ch == '1' || ch == '2';
            if (solid && Y3((float)ty + 1) >= 1.99f) return false;
        }
    }
    return true;
}

// ------------------------------------------------------------------ the 2.5D hay meadow
// The 2.5D stage's field between the path and the painted backdrop, and the strip of lawn in
// front of the path: modelled props in the golden light, no rim light (this stage is the paper
// theatre before the full 3D one). Further props step towards the dusk colour in the same flat
// bands as the lawn under them.
void Models3D::drawMeadow(Game& g)
{
    setLight(true);
    rims_ = false;
    const float l = g.cam.left() - 8, r = g.cam.right() + 14;
    const Color dusk = hexc(0xf0b48a);
    auto hz = [&](Color c, float z) { return mixc(c, dusk, z > -1.5f ? 0.0f : (z > -8 ? 0.1f : (z > -20 ? 0.24f : 0.36f))); };
    const float G = 1.98f;   // the field behind the path
    const Color hayL = hexc(0xf0d890), hayD = hexc(0xa88a50), straw = hexc(0xe6c878);

    // a cart track winding through the field: two worn ruts with grass between
    for (float x = std::floor(l * 2) / 2; x < r; x += 0.5f) {
        auto zc = [](float xx) { return -7.0f + 1.3f * std::sin(xx * 0.11f) + 0.6f * std::sin(xx * 0.29f + 1.0f); };
        float z0 = zc(x), z1 = zc(x + 0.5f), ang = std::atan2(z1 - z0, 0.5f);
        for (int s = -1; s <= 1; s += 2)
            part(cube_, then(then(S(0.56f, 0.02f, 0.34f), MatrixRotateY(-ang)), T(x + 0.25f, G + 0.01f, (z0 + z1) / 2 + s * 0.45f)),
                 hz(hexc(0x9a7652), z0), 0, false);
    }

    // the hedge line across the back of the field, with gaps, and trees standing out of it
    // (low, so the painted haystacks behind still show over it)
    for (int k = (int)std::floor(l / 0.7f); k <= (int)(r / 0.7f); ++k) {
        int seg = (int)std::floor(k / 14.0f), in = k - seg * 14;
        if (hashi(seg, 121) % 3 == 0 ? in > 4 : in > 11) continue;   // gaps of different widths
        float x = k * 0.7f + (rndf(k, 122) - 0.5f) * 0.3f, z = -13.0f + (rndf(k, 123) - 0.5f) * 0.5f;
        float sc = 0.75f + 0.4f * rndf(k, 124);
        Color c = mixc(hexc(0x557f38), hexc(0x78a046), rndf(k, 125));
        part(sphere_, then(S(0.6f * sc, 0.42f * sc, 0.5f), T(x, G + 0.25f * sc, z)), hz(c, z), 0, false);
    }
    for (int k = (int)std::floor(l / 9); k <= (int)(r / 9); ++k) {   // hedgerow trees, now and then
        if (hashi(k, 126) % 3 != 0) continue;
        tree(k * 9.0f + rndf(k, 127) * 4, -13.6f - rndf(k, 128), k * 31 + 7, hashi(k, 129) % 2);
    }
    // windrows: long low rows of cut hay lying in the field
    for (int k = (int)std::floor(l / 6); k <= (int)(r / 6); ++k) {
        for (int row = 0; row < 3; ++row) {
            if (hashi(k * 3 + row, 161) % 3 == 0) continue;
            float len = 2.5f + 3.0f * rndf(k * 3 + row, 162), x = k * 6.0f + rndf(k * 3 + row, 163) * 2;
            float z = -5.0f - row * 2.6f - rndf(k * 3 + row, 164) * 0.6f;
            part(sphere_, then(S(len / 2, 0.13f, 0.32f), T(x + len / 2, G, z)), hz(hexc(0xe0cc84), z), 0, false);
        }
    }

    // field props in hashed slots: stooks in rows, bales alone, in pairs or stacked, fence runs
    for (int k = (int)std::floor(l / 2.2f); k <= (int)(r / 2.2f); ++k) {
        unsigned h = hashi(k, 131);
        float x = k * 2.2f + (rndf(k, 132) - 0.5f) * 1.4f;
        float z = -3.2f - rndf(k, 133) * 9.5f;
        float rot = (rndf(k, 134) - 0.5f) * 1.2f, sc = 0.85f + 0.3f * rndf(k, 135);
        switch (h % 7) {
        case 0: case 1: {   // a stook: sheaves leaned together, tied round the waist, ears on top
            float hh = 0.85f * sc;
            part(cone_, then(then(S(0.34f * sc, hh, 0.34f * sc), MatrixRotateY(rot * 3)), T(x, G, z)), hz(straw, z), 0, false);
            part(cyl_, then(S(0.19f * sc, 0.07f, 0.19f * sc), T(x, G + hh * 0.48f, z)), hz(hayD, z), 0, false);
            part(cone_, then(then(S(0.16f * sc, 0.28f * sc, 0.16f * sc), MatrixRotateX(PI)), T(x, G + hh + 0.12f, z)), hz(hayL, z), 0, false);
            if (h % 14 == 1) {   // a second stook beside it
                part(cone_, then(S(0.3f, 0.75f, 0.3f), T(x + 0.8f, G, z - 0.5f)), hz(straw, z), 0, false);
                part(cyl_, then(S(0.17f, 0.07f, 0.17f), T(x + 0.8f, G + 0.36f, z - 0.5f)), hz(hayD, z), 0, false);
            }
            break;
        }
        case 2: case 3: {   // small square bales: one, two side by side, or three stacked
            int n = 1 + (int)(h / 7 % 3);
            Matrix root = then(MatrixRotateY(rot), T(x, G, z));
            const Vector3 at[3] = {{-0.48f, 0.21f, 0}, {0.48f, 0.21f, 0.05f}, {0.0f, 0.63f, 0.02f}};
            for (int i = 0; i < n; ++i) {
                Vector3 p = n == 1 ? Vector3{0, 0.21f, 0} : at[i];
                part(cube_, then(then(S(0.9f, 0.42f, 0.5f), T(p.x, p.y, p.z)), root), hz(hayL, z), 0, false);
                for (int t = -1; t <= 1; t += 2)
                    part(cube_, then(then(S(0.035f, 0.43f, 0.51f), T(p.x + t * 0.22f, p.y, p.z)), root), hz(hayD, z), 0, false);
            }
            break;
        }
        case 4: {   // a few old fence posts, one leaning, with a rail where it still holds
            float fz = -2.4f - rndf(k, 136) * 0.6f;
            int n = 2 + (int)(h / 7 % 3);
            for (int i = 0; i < n; ++i) {
                float px = x + i * 1.1f + (i > 0 && i < n - 1 ? (rndf(k * 5 + i, 139) - 0.5f) * 0.3f : 0.0f), ph = 0.75f + 0.25f * rndf(k * 5 + i, 137);
                float lean = (rndf(k * 5 + i, 138) - 0.5f) * 0.25f;
                part(cube_, then(then(S(0.11f, ph, 0.11f), T(0, ph / 2, 0)), then(MatrixRotateZ(lean), T(px, G, fz))), hz(hexc(0x8a6a4c), fz), 0, false);
            }
            if (h % 2) part(cube_, then(S(1.1f * (n - 1) + 0.1f, 0.07f, 0.05f), T(x + 0.55f * (n - 1), G + 0.62f, fz + 0.07f)), hz(hexc(0x9a7a56), fz), 0, false);
            break;
        }
        default: break;   // open field
        }
    }

    // wild flowers and tall grass along the back of the path and out into the field
    for (int k = (int)std::floor(l / 0.9f); k <= (int)(r / 0.9f); ++k) {
        float x = k * 0.9f + rndf(k, 141) * 0.6f, z = -1.9f - rndf(k, 142) * 3.5f;
        if (hashi(k, 143) % 3 == 0) flowerClump(x, G, z, k * 3 + 1, 1.0f, false);
        else grassTuft(x, G, z, 0.35f + 0.25f * rndf(k, 144), k * 5 + 2, hz(mixc(hexc(0x6a9a3a), hexc(0x94b048), rndf(k, 145)), z));
    }

    // the near foreground, on the low lawn in front of the path: big grass tufts, flowers, stones
    for (int k = (int)std::floor(l / 0.7f); k <= (int)(r / 0.7f); ++k) {
        float x = k * 0.7f + rndf(k, 151) * 0.5f, z = 0.6f + rndf(k, 152) * 2.8f;
        unsigned h = hashi(k, 153) % 6;
        if (h <= 2) grassTuft(x, 0, z, 0.55f + 0.4f * rndf(k, 154), k * 7 + 3, mixc(hexc(0x3e7a30), hexc(0x6a9a3a), rndf(k, 155)));
        else if (h == 3) flowerClump(x, 0, z, k * 11 + 5, 1.5f, true);
        else if (h == 4 && hashi(k, 156) % 3 == 0)
            part(sphere_, then(S(0.28f, 0.14f, 0.22f), T(x, 0.03f, z)), mixc(hexc(0x8a8478), hexc(0xb0a898), rndf(k, 157)), 0, false);
    }
    setLight(false);
    rims_ = true;
}

// ------------------------------------------------------------------ the garden in front
// The 3D stage's lawn in front of the path: a flagstone garden path with the odd puddle, box
// hedges, flower beds and tufts of grass, all on the low lawn (y 0) and low enough never to hide
// the path top.
void Models3D::drawGardenFront(Game& g, float l, float r)
{
    (void)g;
    // the flagstone path
    for (int k = (int)std::floor(l / 0.95f); k <= (int)(r / 0.95f); ++k) {
        float x = k * 0.95f + (rndf(k, 201) - 0.5f) * 0.14f, z = 2.9f + (rndf(k, 207) - 0.5f) * 0.26f;
        if (hashi(k / 5, 202) % 9 == 0 && k % 5 >= 1 && k % 5 <= 3) {   // a puddle across a few stones
            if (k % 5 == 2) {
                part(cyl_, then(S(1.15f, 0.012f, 0.55f), T(x, 0.004f, 2.9f)), hexc(0x8ab4dc), 0.55f, false);
                part(cyl_, then(S(0.5f, 0.014f, 0.16f), T(x - 0.3f, 0.006f, 2.85f)), hexc(0xd8ecf8), 0.8f, false);
            }
            continue;
        }
        float w = 0.72f + 0.12f * rndf(k, 203), d = 0.62f + 0.14f * rndf(k, 204);
        Color c = mixc(hexc(0xc4bcae), hexc(0xa8a094), rndf(k, 205));
        part(cube_, then(then(S(w, 0.07f, d), MatrixRotateY((rndf(k, 206) - 0.5f) * 0.18f)), T(x, 0.02f, z)), c, 0, false);
    }
    // box hedges and flower beds in front of the flagstones, in hashed stretches
    for (int k = (int)std::floor(l / 4.0f); k <= (int)(r / 4.0f); ++k) {
        unsigned h = hashi(k, 211) % 4;
        float x = k * 4.0f + rndf(k, 212) * 1.0f;
        float bz = 4.25f + (rndf(k, 216) - 0.5f) * 0.3f;
        if (h == 0) {   // a low clipped box hedge
            float len = 1.8f + rndf(k, 213) * 1.4f;
            part(cube_, then(S(len, 0.34f, 0.42f), T(x + len / 2, 0.17f, bz)), hexc(0x4f9a40));
            int nb = std::max(2, (int)(len / 0.5f));
            for (int b = 0; b < nb; ++b) {
                float bx = x + 0.25f + (len - 0.5f) * (b + 0.5f + (rndf(k * 16 + b, 217) - 0.5f) * 0.5f) / nb;
                float sw = 0.24f + 0.08f * rndf(k * 16 + b, 218);
                part(sphere_, then(S(sw, 0.14f + 0.05f * rndf(k * 16 + b, 219), 0.22f), T(bx, 0.34f, bz)),
                     mixc(hexc(0x5aa848), hexc(0x6cb854), rndf(k * 16 + b, 215)), 0, false);
            }
        } else if (h <= 2) {   // a flower bed: a soil bed, bushes and flowers
            float bw = 2.3f + rndf(k, 218) * 1.0f;
            part(cube_, then(S(bw, 0.1f, 0.85f), T(x + bw / 2, 0.03f, bz)), hexc(0x6a4430), 0, false);
            int n = 3 + (int)(hashi(k, 219) % 3);
            for (int i = 0; i < n; ++i) {
                float fx = x + bw * (i + 0.5f + (rndf(k * 8 + i, 220) - 0.5f) * 0.45f) / n;
                float fz = bz + (rndf(k * 8 + i, 228) - 0.5f) * 0.3f;
                if ((i + (int)(hashi(k, 229) % 2)) % 2) flowerClump(fx, 0.08f, fz, k * 13 + i, 1.4f, true);
                else {
                    float bs = 0.85f + 0.3f * rndf(k * 8 + i, 230);
                    part(sphere_, then(S(0.3f * bs, 0.26f * bs, 0.28f * bs), T(fx, 0.18f, fz)), mixc(hexc(0x4a8f3c), hexc(0x66a84a), rndf(k * 8 + i, 214)), 0, false);
                }
            }
        }
    }
    // grass tufts and the odd daisy clump between the path base and the flagstones, and in front
    for (int k = (int)std::floor(l / 0.55f); k <= (int)(r / 0.55f); ++k) {
        float x = k * 0.55f + rndf(k, 221) * 0.5f;
        float z = hashi(k, 222) % 2 ? 1.5f + rndf(k, 223) * 0.8f : 5.0f + rndf(k, 224) * 1.5f;
        if (hashi(k, 225) % 5 == 0) flowerClump(x, 0, z, k * 17 + 3, 1.3f, true);
        else grassTuft(x, 0, z, 0.32f + 0.22f * rndf(k, 226), k * 19 + 1, mixc(hexc(0x3f8a34), hexc(0x62a848), rndf(k, 227)));
    }
}

// ------------------------------------------------------------------ Gran and Sam
void Models3D::drawPlayer(Game& g, int p)
{
    if (personReady_[p]) { drawPersonModel(g, p); return; }
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
        // An unidentifiable blob: a lopsided heap of squashed lumps that never settles on a
        // shape. Each lump drifts and swells on its own beat, so the outline keeps changing.
        Matrix root = then(MatrixRotateY(yaw), T(e.cx(), feet, 0.2f));
        auto at = [&](Matrix m) { return then(m, root); };
        float flat = e.dead ? std::min(1.0f, e.deadTime / 0.2f) : 0.0f;   // it slumps as it goes
        // centre x, y, z, radii x, y, z, shade
        const float lumps[6][7] = {{0.00f, 0.31f, 0.00f, 0.36f, 0.30f, 0.33f, 0}, {0.13f, 0.45f, 0.07f, 0.24f, 0.21f, 0.22f, 1},
                                   {-0.19f, 0.22f, -0.11f, 0.23f, 0.20f, 0.24f, 2}, {0.21f, 0.18f, -0.13f, 0.18f, 0.16f, 0.19f, 1},
                                   {-0.11f, 0.51f, -0.07f, 0.17f, 0.15f, 0.17f, 2}, {-0.06f, 0.17f, 0.20f, 0.20f, 0.15f, 0.15f, 0}};
        const unsigned shades[3] = {0x887ca6, 0x9a8fb4, 0x73698f};
        Vector3 eyeC{}, eyeR{};
        for (int i = 0; i < 6; ++i) {
            const float* L = lumps[i];
            float ph = e.t * (2.3f + i * 0.85f) + i * 1.9f;
            float kx = 1 + 0.13f * std::sin(ph * 1.7f + 2), ky = 1 + 0.13f * std::sin(ph * 1.1f), kz = 1 + 0.1f * std::cos(ph * 1.3f);
            Vector3 c = {(L[0] + std::sin(ph) * 0.05f) * (1 + flat * 0.5f), (L[1] + std::sin(ph * 1.3f + 1) * 0.035f) * (1 - flat * 0.7f),
                         L[2] + std::cos(ph * 0.8f) * 0.04f};
            Vector3 r = {L[3] * kx * (1 + flat * 0.4f), L[4] * ky * (1 - flat * 0.6f), L[5] * kz * (1 + flat * 0.2f)};
            part(sphere_, at(then(S(r.x, r.y, r.z), T(c.x, c.y, c.z))), col(shades[(int)L[6]]));
            if (i == 1) { eyeC = c; eyeR = r; }
        }
        if (!e.dead)   // two faint dots on the front lump: maybe eyes, maybe not
            for (int s = -1; s <= 1; s += 2)
                part(sphere_, at(then(S(0.03f, 0.036f, 0.03f), T(eyeC.x + eyeR.x * 0.9f, eyeC.y + eyeR.y * (s > 0 ? 0.12f : 0.02f),
                                                                 eyeC.z + eyeR.z * s * 0.38f))),
                     col(0x4a4458));
    } else if (e.type == EN_FLYER) {
        // A memory book flying on its own pages: spine on top, covers hanging open a little,
        // and a fan of pages out on each side that beats like wings.
        float cy = Y3(e.cy());
        float beat = e.t * 12;
        float flap = e.dead ? -0.8f : 0.3f + std::sin(beat) * 0.6f;   // wing angle above level: mostly up, so they show
        float yawB = e.dir > 0 ? -0.9f : PI + 0.9f;   // turned further to the camera, so the wings spread across the screen
        Matrix root = then(then(MatrixRotateZ(-0.1f), MatrixRotateY(yawB)), T(e.cx(), cy - std::sin(beat) * 0.03f, 0.3f));
        auto at = [&](Matrix m) { return then(m, root); };
        const float L = 0.5f, top = 0.12f;   // length along the spine, height of the spine
        Color cover = col(0x8c3644), spine = col(0x6a2432), gold = col(0xdcb862);
        Color page = col(0xfcf8ee), pageEdge = col(0xeee4ce);
        part(cube_, at(then(S(L - 0.04f, 0.2f, 0.13f), T(0, top - 0.12f, 0))), pageEdge);   // the page block
        for (int s = -1; s <= 1; s += 2) {   // covers, hinged at the spine edges
            Matrix hinge = then(MatrixRotateX(-s * 0.3f), T(0, top, s * 0.065f));
            part(cube_, at(then(then(S(L + 0.02f, 0.3f, 0.025f), T(0, -0.15f, 0)), hinge)), cover);
            part(cube_, at(then(then(S(0.24f, 0.13f, 0.01f), T(0.02f, -0.15f, s * 0.014f)), hinge)), gold, 0, false);   // the label
            part(cube_, at(then(then(S(0.2f, 0.09f, 0.01f), T(0.02f, -0.15f, s * 0.016f)), hinge)), cover, 0, false);
        }
        part(cube_, at(then(S(L + 0.02f, 0.07f, 0.15f), T(0, top + 0.01f, 0))), spine);
        for (int k = -1; k <= 1; k += 2)   // gold bands on the spine
            part(cube_, at(then(S(0.03f, 0.075f, 0.155f), T(k * 0.15f, top + 0.012f, 0))), gold, 0, false);
        // the ribbon bookmark trailing below
        float sway = std::sin(e.t * 7) * 0.3f + 0.7f;
        part(cube_, at(then(then(then(S(0.035f, 0.17f, 0.01f), T(0, -0.085f, 0)), MatrixRotateZ(-sway)), T(-L / 2 + 0.02f, top - 0.18f, 0.0f))), col(0xc8323c));
        // page wings: four pages a side, fanned, the outer ones lag behind the beat
        for (int s = -1; s <= 1; s += 2)
            for (int i = 0; i < 4; ++i) {
                float ang = flap + (i - 1.5f) * 0.22f + (e.dead ? 0 : std::sin(beat - 0.5f - i * 0.25f) * 0.12f);
                float len = 0.4f - std::abs(i - 1.5f) * 0.03f;
                part(cube_, at(then(then(then(S(L - 0.06f - i * 0.02f, 0.012f, len), T(0, 0, s * len / 2)), MatrixRotateX(-s * ang)),
                                    T(0.01f * i, top + 0.03f, s * 0.05f))),
                     i % 2 ? pageEdge : page);
            }
        // one eye at the head of the spine
        part(sphere_, at(then(S(0.09f, 0.09f, 0.08f), T(L / 2 - 0.01f, 0.03f, 0))), col(0xfaf3e6));
        part(sphere_, at(then(S(0.045f, 0.05f, 0.045f), T(L / 2 + 0.065f, 0.03f, 0))), col(0x0f0d1a));
        part(cube_, at(then(then(S(0.03f, 0.04f, 0.2f), MatrixRotateZ(-0.3f)), T(L / 2 + 0.04f, 0.11f, 0))), spine);   // a heavy lid
    } else if (clockReady_) {
        drawClockModel(g, e);
    } else {
        // A grandfather clock on two stubby feet: plinth, trunk with the pendulum behind glass,
        // and a hood with the dial, scowling. It leans in and the pendulum thrashes when it charges.
        float yawC = e.dir > 0 ? -1.0f : PI + 1.0f;   // turned further to the camera, so the dial shows
        Matrix root = then(MatrixRotateY(yawC), T(e.cx(), feet, 0.1f));
        bool mad = e.state >= 1 && !e.dead;
        float wt = e.t * (e.state == 2 ? 18.0f : 7.0f);
        float step = e.state == 1 ? 0.0f : std::sin(wt);
        float lean = e.state == 1 ? -0.16f : (e.state == 2 ? -0.26f : -0.03f);
        float rattle = e.state == 1 ? std::sin(e.t * 70) * 0.025f : 0.0f;
        float rock = step * 0.05f, hop = std::fabs(std::cos(wt)) * (e.state == 1 ? 0 : 0.035f);
        Matrix body = then(then(then(MatrixRotateX(rock), MatrixRotateZ(lean + rattle)), T(0, 0.13f + hop, 0)), root);
        auto at = [&](Matrix m) { return then(m, body); };
        auto atR = [&](Matrix m) { return then(m, root); };
        Color wood = col(0x8e5634), dark = col(0x6c3c22), trim = col(0x50301c), brass = col(0xd4a64a), ink = col(0x1c1420);
        auto disc = [&](float r, float th, float x, float y, float z) {   // a flat round facing forward (+x)
            return at(then(then(S(r, th, r), MatrixRotateZ(-PI / 2)), T(x, y, z)));
        };
        // feet
        for (int s = -1; s <= 1; s += 2) {
            float fx = s * step * 0.12f, lift = std::max(0.0f, s * std::cos(wt)) * (e.state == 1 ? 0 : 0.06f);
            part(cyl_, atR(then(S(0.07f, 0.14f, 0.07f), T(fx, 0.05f + lift, s * 0.21f))), trim);
            part(sphere_, atR(then(S(0.19f, 0.085f, 0.13f), T(fx + 0.12f, 0.07f + lift, s * 0.21f))), trim);
        }
        // the case
        part(cube_, at(then(S(0.52f, 0.28f, 0.8f), T(0, 0.17f, 0))), dark);        // plinth
        part(cube_, at(then(S(0.56f, 0.05f, 0.84f), T(0, 0.32f, 0))), trim);        // moulding
        part(cube_, at(then(S(0.38f, 0.62f, 0.52f), T(0, 0.64f, 0))), wood);        // trunk
        part(cube_, at(then(S(0.56f, 0.05f, 0.84f), T(0, 0.96f, 0))), trim);        // moulding
        part(cube_, at(then(S(0.5f, 0.5f, 0.76f), T(0, 1.22f, 0))), wood);          // hood
        part(cube_, at(then(S(0.58f, 0.06f, 0.86f), T(0, 1.49f, 0))), trim);        // cornice
        part(cyl_, at(then(then(S(0.15f, 0.52f, 0.34f), MatrixRotateZ(-PI / 2)), T(-0.26f, 1.5f, 0))), dark);   // arched top
        part(sphere_, at(then(S(0.06f, 0.06f, 0.06f), T(0, 1.68f, 0))), brass);
        for (int s = -1; s <= 1; s += 2) {
            part(cyl_, at(then(S(0.03f, 0.48f, 0.03f), T(0.25f, 0.98f, s * 0.34f))), brass);   // hood columns
            part(sphere_, at(then(S(0.05f, 0.05f, 0.05f), T(0.22f, 1.55f, s * 0.38f))), brass);
        }
        // the glass door: dark behind, the pendulum, a brass frame and a glint
        float swing = e.dead ? 0.0f : (mad ? std::sin(e.t * 14) * 0.28f : std::sin(e.t * 4.2f) * 0.16f);
        part(cube_, at(then(S(0.02f, 0.48f, 0.34f), T(0.19f, 0.64f, 0))), col(0x2a1a14), 0, false);
        Matrix pend = then(MatrixRotateX(swing), T(0.205f, 0.86f, 0));
        part(cyl_, at(then(then(S(0.012f, 0.36f, 0.012f), T(0, -0.36f, 0)), pend)), brass, 0, false);
        part(cyl_, at(then(then(then(S(0.085f, 0.02f, 0.085f), MatrixRotateZ(-PI / 2)), T(0, -0.36f, 0)), pend)), brass, 0.15f);
        for (int s = -1; s <= 1; s += 2) {
            part(cube_, at(then(S(0.03f, 0.04f, 0.42f), T(0.2f, 0.64f + s * 0.25f, 0))), brass, 0, false);
            part(cube_, at(then(S(0.03f, 0.54f, 0.04f), T(0.2f, 0.64f, s * 0.19f))), brass, 0, false);
        }
        part(cube_, at(then(then(S(0.005f, 0.2f, 0.025f), MatrixRotateX(0.6f)), T(0.215f, 0.72f, 0.09f))), col(0xe8f4ff), 0.6f, false);
        part(cube_, at(then(then(S(0.005f, 0.1f, 0.018f), MatrixRotateX(0.6f)), T(0.215f, 0.62f, 0.12f))), col(0xe8f4ff), 0.6f, false);
        // the dial
        const float dy = 1.21f, dx = 0.25f;
        part(cyl_, disc(0.21f, 0.03f, dx, dy, 0), col(0xf6f0e0), 0.1f, false);   // the bezel outlines it
        part(torus_, at(then(then(S(0.205f, 0.205f, 0.205f), MatrixRotateY(PI / 2)), T(dx + 0.03f, dy, 0))), brass);   // bezel
        for (int k = 0; k < 4; ++k) {   // marks at 12, 3, 6 and 9
            float ang = k * PI / 2;
            part(sphere_, at(then(S(0.016f, 0.016f, 0.016f), T(dx + 0.03f, dy + std::cos(ang) * 0.165f, std::sin(ang) * 0.165f))), ink);
        }
        float spin = mad ? e.t * 9 : e.t * 0.8f;
        const float hand[2][3] = {{0.15f, 0.022f, 1.0f}, {0.1f, 0.03f, 1.0f / 12}};   // length, width, speed
        for (int k = 0; k < 2; ++k)
            part(cube_, at(then(then(then(S(0.012f, hand[k][0], hand[k][1]), T(0, hand[k][0] / 2 - 0.02f, 0)), MatrixRotateX(-spin * hand[k][2] - k * 2.1f)),
                                T(dx + 0.04f, dy, 0))),
                 ink, 0, false);
        part(sphere_, at(then(S(0.022f, 0.022f, 0.022f), T(dx + 0.045f, dy, 0))), brass);
        // eyes and cross brows
        for (int s = -1; s <= 1; s += 2) {
            if (!e.dead)
                part(sphere_, at(then(S(0.03f, mad ? 0.04f : 0.03f, 0.034f), T(dx + 0.035f, dy + 0.055f, s * 0.075f))),
                     mad ? col(0xff4040) : ink, mad ? 1.0f : 0.0f);
            float tilt = mad ? 0.6f : 0.42f;
            part(cube_, at(then(then(S(0.04f, 0.06f, 0.17f), MatrixRotateX(-s * tilt)), T(dx + 0.05f, dy + 0.125f - (mad ? 0.015f : 0), s * 0.08f))), ink);
        }
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

// ------------------------------------------------------------------ Gran and Sam, modelled in Blender
// Both are built on the same skeleton, so one run cycle drives them: leaning into it, elbows
// bent, knees lifting. A faded player is greyed out and see-through (dithered by the shader);
// the flicker after a hit is the same dither, so no inside parts ever show.
void Models3D::drawPersonModel(Game& g, int p)
{
    const Player& pl = g.players[p];
    Model* parts = person_[p];
    float a = 1.0f;
    if (pl.invuln > 0 && !pl.faded && (int)(pl.invuln * 12) % 2 == 0) a = 0.4f;
    if (pl.faded) a = 0.55f;
    Color tint = withA(WHITE, a);
    setLook(pl.faded ? Vector4{0.8f, 0.82f, 0.9f, 0.25f} : Vector4{0, 0, 0, 0}, pl.faded ? 0.85f : 0.0f);

    float yaw = pl.facing > 0 ? -0.52f : PI + 0.52f;
    float ph = g.time * 12.0f + p * 1.3f;
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

    for (int side = 0; side < 2; ++side) {   // legs hang from the hips but do not lean with the body
        float z = side ? 0.1f : -0.1f;
        float th = side ? thighF : thighB, kn = side ? kneeF : kneeB;
        Matrix thigh = then(R(th - lean), then(T(0, 0, z), hips));
        modelPart(parts[P_THIGH], thigh, tint);
        Matrix shin = then(R(kn), then(T(0, -0.26f, 0), thigh));
        modelPart(parts[P_SHIN], shin, tint);
    }
    auto arm = [&](float z, float sw) {
        Matrix up = then(R(sw), then(T(0, 0.4f, z), hips));
        modelPart(parts[P_UPPERARM], up, tint);
        Matrix fore = then(R(elbow), then(T(0, -0.21f, 0), up));
        modelPart(parts[P_FOREARM], fore, tint);
    };
    arm(-0.25f, armB);
    modelPart(parts[P_BODY], hips, tint);
    modelPart(parts[P_HEAD], then(R(headNod * 0.5f + (pl.onGround ? 0 : 0.1f)), then(T(0, 0.48f, 0), hips)), tint);
    arm(0.25f, armF);

    // Gran's umbrella when she glides
    if (p == 0 && (pl.gliding || pl.frame == art::PF_SPECIAL)) {
        auto at = [&](Matrix m) { return then(m, root); };
        part(cyl_, at(then(S(0.02f, 0.7f, 0.02f), T(0.1f, 1.3f, 0.25f))), withA(hexc(0x3a2a2a), a));
        part(sphere_, at(then(S(0.62f, 0.22f, 0.62f), T(0.1f, 2.0f, 0.25f))), withA(hexc(0xb03a6a), a));
    }
    setLook({0, 0, 0, 0}, 0.0f);
}

// ------------------------------------------------------------------ the grandfather clock, modelled in Blender
// A longcase clock waddling on its two bracket feet. Its face sits in the arch above the dial
// (scowling brows, eyes that glow red when it winds up), the hands turn, and the pendulum
// swings inside the trunk: only its weight shows through the round window. The swing is held
// to 0.28 radians even when it charges, so the weight stays inside the case.
void Models3D::drawClockModel(Game& g, const Enemy& e)
{
    (void)g;
    // pivots of the Blender parts (printed by make_models.py)
    const float FOOT_H = 0.0837f, PEND_X = 0.044f, PEND_Y = 0.8808f, DIAL_X = 0.1568f, DIAL_Y = 1.3124f;
    const float FACE_X = 0.1515f, FACE_Y = 1.503f;
    float a = e.dead ? std::max(0.0f, 1 - e.deadTime / 0.45f) : 1.0f;
    Color tint = withA(WHITE, a);
    Vector4 look{0, 0, 0, 0};
    if (e.owner >= 0) {
        Color o = e.owner == 0 ? hexc(0xffaf5f) : hexc(0x78b4ff);
        look = {o.r / 255.0f, o.g / 255.0f, o.b / 255.0f, 0.35f};
    }
    if (e.hurt > 0) look = {1, 1, 1, 0.6f};
    setLook(look, 0.0f);

    float yawC = e.dir > 0 ? -1.0f : PI + 1.0f;   // turned well towards the camera, so the dial shows
    Matrix root = then(MatrixRotateY(yawC), T(e.cx(), Y3(e.y + e.h), 0.1f));
    bool mad = e.state >= 1 && !e.dead;
    float wt = e.t * (e.state == 2 ? 18.0f : 7.0f);
    float step = e.state == 1 ? 0.0f : std::sin(wt);
    float lean = e.state == 1 ? -0.16f : (e.state == 2 ? -0.26f : -0.03f);
    float rattle = e.state == 1 ? std::sin(e.t * 70) * 0.025f : 0.0f;
    float rock = step * 0.05f, hop = std::fabs(std::cos(wt)) * (e.state == 1 ? 0 : 0.035f);
    Matrix body = then(then(then(MatrixRotateX(rock), MatrixRotateZ(lean + rattle)), T(0, FOOT_H + hop, 0)), root);
    auto at = [&](Matrix m) { return then(m, body); };

    for (int s = -1; s <= 1; s += 2) {   // the feet: one steps forward while the other lifts
        float fx = s * step * 0.1f, lift = std::max(0.0f, s * std::cos(wt)) * (e.state == 1 ? 0 : 0.06f);
        modelPart(clock_[s < 0 ? C_FOOT_L : C_FOOT_R], then(T(fx, FOOT_H + lift, -s * 0.15f), root), tint);
    }
    modelPart(clock_[C_CASE], body, tint);
    float swing = e.dead ? 0.0f : (mad ? std::sin(e.t * 14) * 0.28f : std::sin(e.t * 4.2f) * 0.16f);
    modelPart(clock_[C_PENDULUM], at(then(MatrixRotateX(swing), T(PEND_X, PEND_Y, 0))), tint);
    float spin = mad ? e.t * 9 : e.t * 0.8f;
    modelPart(clock_[C_HOUR], at(then(MatrixRotateX(-spin / 12 - 2.1f), T(DIAL_X + 0.004f, DIAL_Y, 0))), tint, false);
    modelPart(clock_[C_MINUTE], at(then(MatrixRotateX(-spin), T(DIAL_X + 0.009f, DIAL_Y, 0))), tint, false);

    // the face in the arch: big dark eyes (red and glowing when it winds up) and cross brows
    Color ink = withA(hexc(0x1c1420), a), red = withA(hexc(0xff4040), a);
    for (int s = -1; s <= 1; s += 2) {
        if (!e.dead)
            part(sphere_, at(then(S(0.02f, mad ? 0.042f : 0.036f, 0.03f), T(FACE_X + 0.006f, FACE_Y, s * 0.052f))),
                 mad ? red : ink, mad ? 1.0f : 0.0f, false);
        float tilt = mad ? 0.6f : 0.42f;
        part(cube_, at(then(then(S(0.03f, 0.022f, 0.075f), MatrixRotateX(-s * tilt)),
                            T(FACE_X + 0.01f, FACE_Y + 0.048f - (mad ? 0.008f : 0), s * 0.05f))),
             ink, 0, false);
    }
    setLook({0, 0, 0, 0}, 0.0f);
}

// ------------------------------------------------------------------ the Forgetting, modelled in Blender
// A ball of Gran's dark knitting wool coming unravelled. It floats and bobs, its one eye
// following the nearest player, and its strands of yarn (3 to 6 pieces each, so the outline
// varies) wave in slow curls, getting paler towards their frayed ends as if fading. They splay
// out when it slams down. A hit flashes it towards white in the shader.
void Models3D::drawForgetting(Game& g)
{
    const Boss& b = g.boss;
    float a = b.state == 5 ? std::max(0.0f, 1 - b.stateTime / 1.6f) : 1.0f;
    bool flash = (b.state == 3 && (int)(g.time * 8) % 2 == 0) || b.hurt > 0;
    Color tint = withA(WHITE, a);
    const Vector3 pale = {0.86f, 0.85f, 0.9f};
    auto look = [&](float fade) {   // fade 0 (the ball) .. 1 (the very tips)
        if (flash) setLook({1.0f, 0.97f, 0.95f, 0.6f}, 0.0f);
        else setLook({pale.x, pale.y, pale.z, 0.55f * fade}, 0.0f);
    };

    float t = b.t;
    float bob = std::sin(t * 2.2f) * 0.15f;
    float slam = b.state == 3 ? 1.0f : 0.0f;
    const float k = 1.8f;    // size in the level
    // the nearest player: the ball turns a little towards them and the eye follows them
    float px = g.players[0].cx(), py = g.players[0].cy();
    if (g.players[1].joined && std::fabs(g.players[1].cx() - b.x) < std::fabs(px - b.x)) { px = g.players[1].cx(); py = g.players[1].cy(); }
    float yaw = px < b.x ? -PI / 2 - 0.3f : -PI / 2 + 0.3f;
    float tilt = std::sin(t * 1.3f) * 0.06f;
    Vector3 centre = {b.x, Y3(b.y) + 0.4f + bob, 0.4f};
    Matrix turn = then(MatrixRotateX(tilt), MatrixRotateY(yaw));
    Matrix root = then(then(S(k, k, k), turn), T(centre.x, centre.y, centre.z));

    look(0);
    modelPart(forget_[F_BODY], root, tint);
    if (b.state != 5) {
        // the pupil slides about inside the hollow eye towards the player
        Vector3 d = Vector3Normalize({px - centre.x, Y3(py) - centre.y, 6.0f});   // towards the player, and the camera
        Vector3 l = Vector3Transform(d, MatrixTranspose(turn));                    // into the ball's own frame
        Vector3 pp = {0.8f, 0.12f + std::max(-1.0f, std::min(1.0f, l.y)) * 0.13f, std::max(-1.0f, std::min(1.0f, l.z)) * 0.13f};
        modelPart(forget_[F_PUPIL], then(T(pp.x, pp.y, pp.z), root), tint, false);
    }
    // strands hang from a ring under the ball
    const int N = 8;
    for (int i = 0; i < N; ++i) {
        float ang = (i + 0.5f) * 2 * PI / N;
        int segs = 3 + (int)(hashi(i, 77) % 4);
        float rx = std::cos(ang) * 0.48f, rz = std::sin(ang) * 0.48f;
        Matrix m = then(then(MatrixRotateZ(0.3f + 0.6f * slam), MatrixRotateY(-ang)), then(T(rx, -0.66f, rz), root));
        float sc = 1.0f;
        for (int j = 0; j < segs; ++j) {
            look((float)j / segs);
            modelPart(forget_[F_SEG], then(S(sc, sc, sc), m), tint);
            float wave = std::sin(t * 2.6f + i * 0.9f - j * 0.8f) * (0.32f - 0.2f * slam) + 0.06f * j * (1 - slam);
            float sway = std::cos(t * 1.9f + i * 1.7f - j * 0.6f) * 0.15f;
            m = then(then(MatrixRotateZ(wave), MatrixRotateX(sway)), then(T(0, -0.4f * sc, 0), m));
            sc *= 0.9f;
        }
        look(1);
        modelPart(forget_[F_TIP], then(S(sc, sc, sc), m), tint);
    }
    setLook({0, 0, 0, 0}, 0.0f);
}
