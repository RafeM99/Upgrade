#include <cstdlib>
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
    locMetal_ = GetShaderLocation(sh_, "metallic");
    locUseTex_ = GetShaderLocation(sh_, "useTexture");
    locMacro_ = GetShaderLocation(sh_, "macro");
    locFogCol_ = GetShaderLocation(sh_, "fogCol");
    locFogRange_ = GetShaderLocation(sh_, "fogRange");
    sphereLo_ = LoadModelFromMesh(GenMeshSphere(1, 4, 7));   // baked blobs (tree canopies): 5x8, was 4x6, so the facets are smaller
    sphereTiny_ = LoadModelFromMesh(GenMeshSphere(1, 3, 5));
    for (int i = 1; i <= 3; ++i) {
        static const char* nm[4] = {"", "ground_grass_512.jpg", "ground_earth_512.jpg", "ground_rock_512.jpg"};
        std::string path = assetPath(std::string("textures/") + nm[i]);
        if (!FileExists(path.c_str())) continue;
        gtex_[i] = LoadTexture(path.c_str());
        GenTextureMipmaps(&gtex_[i]);
        SetTextureFilter(gtex_[i], TEXTURE_FILTER_TRILINEAR);
        SetTextureWrap(gtex_[i], TEXTURE_WRAP_REPEAT);
    }
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
    loadGroundKit();
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
        for (int k = 0; k < out[i].materialCount; ++k) {
            out[i].materials[k].shader = sh_;
            Texture2D& tex = out[i].materials[k].maps[MATERIAL_MAP_DIFFUSE].texture;
            if (tex.id > 0 && tex.id != rlGetTextureIdDefault()) {   // a baked texture: mipmapped
                GenTextureMipmaps(&tex);
                SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
            }
        }
    }
    ready = true;
}

// The Blender ground kit (tools/blender/make_ground.py): grass-top, earth and deep-earth tiles, several
// variants each. Missing files just leave the box code in charge.
void Models3D::loadGroundKit()
{
    static const char* const kinds[3] = {"grass", "earth", "deep"};
    static const int counts[3] = {6, 5, 4};
    groundKit_ = false;
    if (std::getenv("GAMEJAM_NOKIT")) return;
    for (int k = 0; k < 3; ++k) {
        groundN_[k] = 0;
        for (int v = 0; v < counts[k]; ++v) {
            std::string path = assetPath(std::string("models/ground_") + kinds[k] + "_" + std::to_string(v) + ".glb");
            if (!FileExists(path.c_str())) return;
            Model m = LoadModel(path.c_str());
            if (m.meshCount == 0 || !m.meshes[0].colors) { UnloadModel(m); return; }
            groundMod_[k][v] = m;
            groundN_[k] = v + 1;
        }
    }
    groundKit_ = true;
    liteKit_ = true;
    for (int k = 0; k < 3 && liteKit_; ++k)
        for (int v = 0; v < counts[k]; ++v) {
            std::string path = assetPath(std::string("models/groundlite_") + kinds[k] + "_" + std::to_string(v) + ".glb");
            Model m = FileExists(path.c_str()) ? LoadModel(path.c_str()) : Model{};
            if (m.meshCount == 0 || !m.meshes[0].colors) {
                if (m.meshCount) UnloadModel(m);
                for (int kk = 0; kk <= k; ++kk)
                    for (int vv = 0; vv < counts[kk]; ++vv)
                        if (liteMod_[kk][vv].meshCount && (kk < k || vv < v)) { UnloadModel(liteMod_[kk][vv]); liteMod_[kk][vv] = Model{}; }
                liteKit_ = false;
                break;
            }
            liteMod_[k][v] = m;
        }
}

void Models3D::unloadGroundKit()
{
    for (int k = 0; k < 3; ++k)
        for (int v = 0; v < groundN_[k]; ++v) UnloadModel(groundMod_[k][v]);
    if (liteKit_)
        for (int k = 0; k < 3; ++k)
            for (int v = 0; v < groundN_[k]; ++v) UnloadModel(liteMod_[k][v]);
    groundKit_ = liteKit_ = false;
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
// the lighter rim along its silhouette. Drawn mesh by mesh (one per material) so each material
// sets its own metallic (the toon highlight), emissive (a glTF emissive colour draws unlit) and
// whether it has a baked texture. Afterwards the uniforms are put back for the primitives.
void Models3D::modelPart(Model& m, Matrix t, Color tint, bool rim)
{
    float zero = 0.0f, r = rim && rims_ ? 1.0f : 0.0f;
    SetShaderValue(sh_, locRim_, &r, SHADER_UNIFORM_FLOAT);
    pushLook();
    for (int i = 0; i < m.meshCount; ++i) {
        Material mt = m.materials[m.meshMaterial[i]];
        const Color e = mt.maps[MATERIAL_MAP_EMISSION].color;
        float metal = mt.maps[MATERIAL_MAP_METALNESS].value;
        float emis = (e.r | e.g | e.b) ? 1.0f : 0.0f;
        float useTex = mt.maps[MATERIAL_MAP_DIFFUSE].texture.id != rlGetTextureIdDefault() ? 1.0f : 0.0f;
        SetShaderValue(sh_, locMetal_, &metal, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sh_, locEmissive_, &emis, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sh_, locUseTex_, &useTex, SHADER_UNIFORM_FLOAT);
        Color& c = mt.maps[MATERIAL_MAP_DIFFUSE].color;   // maps are shared: tint, draw, put back
        const Color keep = c;
        c = Color{(unsigned char)(c.r * tint.r / 255), (unsigned char)(c.g * tint.g / 255),
                  (unsigned char)(c.b * tint.b / 255), (unsigned char)(c.a * tint.a / 255)};
        DrawMesh(m.meshes[i], mt, t);
        c = keep;
    }
    SetShaderValue(sh_, locMetal_, &zero, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh_, locEmissive_, &zero, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh_, locUseTex_, &zero, SHADER_UNIFORM_FLOAT);
}

void Models3D::setMacro(float m) { SetShaderValue(sh_, locMacro_, &m, SHADER_UNIFORM_FLOAT); }

// ------------------------------------------------------------------ baked static meshes
// The software renderer pays for every draw call, and the ground, flowers and mid-field are made
// of thousands of small parts. part() calls between bakeBegin and bakeEnd are instead merged (vertex
// colours, world-space planar UVs for the CC0 ground textures) into one mesh per texture and per
// chunk of the world, built once and redrawn with a handful of calls. All of it comes from world
// position hashes, so it never changes as the camera scrolls.
void Models3D::bakeBegin(Chunk& c)
{
    freeChunk(c);
    c.batches.clear();
    rec_ = true;
    recChunk_ = &c;
}

void Models3D::bakeEnd()
{
    rec_ = false;
    if (recChunk_) { recChunk_->built = true; recChunk_->dirty = true; }
    recChunk_ = nullptr;
}

void Models3D::recordTri(Chunk& ch, int tex, int flags, const Vector3* p, const Vector3* n, Color c, float scale, const Color* vc)
{
    BakeBatch& b = ch.batches[tex * 4 + flags];
    for (int k = 0; k < 3; ++k) {
        b.p.insert(b.p.end(), {p[k].x, p[k].y, p[k].z});
        b.n.insert(b.n.end(), {n[k].x, n[k].y, n[k].z});
        float ax = std::fabs(n[k].x), ay = std::fabs(n[k].y), az = std::fabs(n[k].z);
        float u = ay >= ax && ay >= az ? p[k].x : (az >= ax ? p[k].x : p[k].z);
        float v = ay >= ax && ay >= az ? p[k].z : p[k].y;
        b.uv.insert(b.uv.end(), {u * scale, v * scale});
        Color cc = vc ? vc[k] : c;
        b.c.insert(b.c.end(), {cc.r, cc.g, cc.b, 255});
    }
}

void Models3D::recordPart(Model& m, Matrix t, Color c, float emissive, bool rim)
{
    Model& src = &m == &sphere_ ? sphereLo_ : (&m == &sphereTiny_ ? sphereTiny_ : m);
    Matrix nm = MatrixTranspose(MatrixInvert(t));
    nm.m12 = nm.m13 = nm.m14 = 0;
    int flags = (rim && rims_ && &m != &cube_ ? 1 : 0) + (emissive > 0.05f ? 2 : 0);
    static const float scl[4] = {1.0f, 0.45f, 0.5f, 0.6f};
    for (int mi = 0; mi < src.meshCount; ++mi) {
        const Mesh& me = src.meshes[mi];
        int tex = recTex_;
        if (me.colors && recTex_ != 0) {   // a ground tile: its material's red byte names the texture (255 earth, 250 grass, 245 rock)
            int red = src.materials[src.meshMaterial[mi]].maps[MATERIAL_MAP_DIFFUSE].color.r;
            tex = red >= 253 ? 2 : (red >= 248 ? 1 : 3);
        }
        int nTri = me.indices ? me.triangleCount : me.vertexCount / 3;
        for (int tri = 0; tri < nTri; ++tri) {
            Vector3 p[3], n[3];
            Color vc[3];
            for (int k = 0; k < 3; ++k) {
                int i = me.indices ? me.indices[tri * 3 + k] : tri * 3 + k;
                if (me.colors) {   // a Blender tile: rgb is painted colour (times the tint), alpha baked occlusion folded into rgb
                    const unsigned char* q = me.colors + i * 4;
                    float ao = 0.45f + 0.55f * q[3] / 255.0f;
                    vc[k] = Color{(unsigned char)(c.r * q[0] / 255.0f * ao), (unsigned char)(c.g * q[1] / 255.0f * ao),
                                  (unsigned char)(c.b * q[2] / 255.0f * ao), 255};
                }
                p[k] = Vector3Transform({me.vertices[i * 3], me.vertices[i * 3 + 1], me.vertices[i * 3 + 2]}, t);
                n[k] = Vector3Normalize(Vector3Transform({me.normals[i * 3], me.normals[i * 3 + 1], me.normals[i * 3 + 2]}, nm));
            }
            recordTri(*recChunk_, tex, flags, p, n, c, scl[tex], me.colors ? vc : nullptr);
        }
    }
}

void Models3D::freeChunk(Chunk& c)
{
    for (BakedMesh& b : c.meshes) UnloadMesh(b.mesh);
    c.meshes.clear();
}

void Models3D::uploadChunk(Chunk& c)
{
    freeChunk(c);
    for (auto& kv : c.batches) {
        const BakeBatch& b = kv.second;
        if (b.p.empty()) continue;
        BakedMesh bm;
        bm.tex = kv.first / 4;
        bm.rim = (kv.first & 1) ? 1.0f : 0.0f;
        bm.emis = (kv.first & 2) ? 0.15f : 0.0f;
        Mesh m{};
        m.vertexCount = (int)(b.p.size() / 3);
        m.triangleCount = m.vertexCount / 3;
        m.vertices = (float*)RL_MALLOC(b.p.size() * sizeof(float));
        m.normals = (float*)RL_MALLOC(b.n.size() * sizeof(float));
        m.texcoords = (float*)RL_MALLOC(b.uv.size() * sizeof(float));
        m.colors = (unsigned char*)RL_MALLOC(b.c.size());
        std::copy(b.p.begin(), b.p.end(), m.vertices);
        std::copy(b.n.begin(), b.n.end(), m.normals);
        std::copy(b.uv.begin(), b.uv.end(), m.texcoords);
        std::copy(b.c.begin(), b.c.end(), m.colors);
        UploadMesh(&m, false);
        bm.mesh = m;
        c.meshes.push_back(bm);
    }
    c.dirty = false;
}

void Models3D::drawChunk(Chunk& c)
{
    if (c.dirty) uploadChunk(c);
    for (BakedMesh& b : c.meshes) {
        Material mt = cube_.materials[0];
        bool tx = b.tex > 0 && gtex_[b.tex].id > 0;
        if (tx) mt.maps[MATERIAL_MAP_DIFFUSE].texture = gtex_[b.tex];
        mt.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
        float useTex = tx ? 1.0f : 0.0f;
        SetShaderValue(sh_, locUseTex_, &useTex, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sh_, locEmissive_, &b.emis, SHADER_UNIFORM_FLOAT);
        SetShaderValue(sh_, locRim_, &b.rim, SHADER_UNIFORM_FLOAT);
        pushLook();
        DrawMesh(b.mesh, mt, MatrixIdentity());
    }
    float zero = 0.0f;
    SetShaderValue(sh_, locUseTex_, &zero, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh_, locEmissive_, &zero, SHADER_UNIFORM_FLOAT);
}

void Models3D::trimChunks(int kind, int lo, int hi)
{
    for (auto it = chunks_.begin(); it != chunks_.end();) {
        long long k = it->first - kind * 1000000LL - 500000;
        if (k >= -400000 && k < 400000 && it->first / 1000000 == kind && (k < lo || k > hi)) {
            freeChunk(it->second);
            it = chunks_.erase(it);
        } else ++it;
    }
}

void Models3D::drawFlowers()
{
    int lo = (int)std::floor((camPos_.x - 48) / 16), hi = (int)std::floor((camPos_.x + 64) / 16);
    trimChunks(2, lo, hi);
    for (auto& kv : chunks_)
        if (kv.first / 1000000 == 2 && kv.second.built) drawChunk(kv.second);
}

// Contact shadows: each decal is a soft dark ellipse (two rings fading to nothing at the rim)
// just above the surface, drawn blended without writing depth so neighbours never cut into
// each other. Placed from world positions only, so they never move against the scenery.
void Models3D::flushShadows(bool depthTest)
{
    if (decals_.empty()) return;
    rlDrawRenderBatchActive();
    rlSetTexture(rlGetTextureIdDefault());
    if (depthTest) rlDisableDepthMask();
    else rlDisableDepthTest();   // laid down over the lawn before any prop is drawn, so the props cover them (no depth fight)
    rlBegin(RL_TRIANGLES);
    const int N = 14;
    auto vtx = [&](const Decal& d, float k, float a, int i) {
        float an = i * 6.2831853f / N;
        rlColor4ub(12, 14, 28, (unsigned char)(255 * std::min(1.0f, a * d.a * 2.4f)));
        rlVertex3f(d.x + std::cos(an) * d.rx * k, d.y, d.z + std::sin(an) * d.rz * k);
    };
    auto ctr = [&](const Decal& d) { rlColor4ub(12, 14, 28, (unsigned char)(255 * std::min(1.0f, d.a * 2.4f))); rlVertex3f(d.x, d.y, d.z); };
    for (const Decal& d : decals_) {
        for (int i = 0; i < N; ++i) {
            ctr(d); vtx(d, 0.6f, 0.82f, i + 1); vtx(d, 0.6f, 0.82f, i);
            vtx(d, 0.6f, 0.82f, i); vtx(d, 0.6f, 0.82f, i + 1); vtx(d, 1.0f, 0.0f, i);
            vtx(d, 0.6f, 0.82f, i + 1); vtx(d, 1.0f, 0.0f, i + 1); vtx(d, 1.0f, 0.0f, i);
        }
    }
    rlEnd();
    rlDrawRenderBatchActive();   // the depth state applies when the batch is drawn
    rlEnableDepthTest();
    rlEnableDepthMask();
    rlSetTexture(0);
    decals_.clear();
}

void Models3D::characterShadow(float x, float y, float z, float rx, float rz, float a)
{
    // the sun stands up and to the left of the camera (the same side as on the props): the shadow falls a little right and back
    decals_.push_back({x + rx * 0.28f, y + 0.04f, z - 0.02f, rx, rz, a});
}

void Models3D::flushCharacterShadows()
{
    flushShadows(true);
}

void Models3D::pushLook()
{
    SetShaderValue(sh_, locBlend_, &blend_, SHADER_UNIFORM_VEC4);
    SetShaderValue(sh_, locGrey_, &grey_, SHADER_UNIFORM_FLOAT);
}

void Models3D::shutdown()
{
    for (auto& kv : chunks_) freeChunk(kv.second);
    chunks_.clear();
    UnloadModel(sphereLo_);
    UnloadModel(sphereTiny_);
    for (int i = 1; i <= 3; ++i) if (gtex_[i].id > 0) UnloadTexture(gtex_[i]);
    for (Model* m : {&cube_, &sphere_, &cyl_, &torus_, &cone_}) {
        m->materials[0].shader = Shader{rlGetShaderIdDefault(), rlGetShaderLocsDefault()};
        UnloadModel(*m);
    }
    for (int p = 0; p < 2; ++p) unloadParts(person_[p], personReady_[p] ? P_COUNT : 0, personReady_[p]);
    unloadParts(clock_, clockReady_ ? C_COUNT : 0, clockReady_);
    unloadParts(forget_, forgetReady_ ? F_COUNT : 0, forgetReady_);
    unloadGroundKit();
    UnloadShader(sh_);
}

void Models3D::part(Model& m, Matrix t, Color c, float emissive, bool rim)
{
    if (dry_) return;   // the shadow pass only collects decals
    if (rec_) { recordPart(m, t, c, emissive, rim); return; }
    // boxes get no rim: their flat faces would light up whole, and the face bands already
    // separate them. Rounded parts get the thin lighter rim.
    float r = rim && rims_ && &m != &cube_ ? 1.0f : 0.0f;
    SetShaderValue(sh_, locEmissive_, &emissive, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh_, locRim_, &r, SHADER_UNIFORM_FLOAT);
    pushLook();
    m.transform = t;
    DrawModel(m, {0, 0, 0}, 1.0f, c);
}

void Models3D::begin(Game& g, const Camera3D& cam, bool fog)
{
    rlDrawRenderBatchActive();
    camPos_ = cam.position;
    setLook({0, 0, 0, 0}, 0.0f);
    SetShaderValue(sh_, locView_, &cam.position, SHADER_UNIFORM_VEC3);
    // aerial perspective: everything behind the play plane drifts towards the horizon colour (the same one the backdrop is hazed to)
    {
        float dist = std::sqrt((cam.position.x - cam.target.x) * (cam.position.x - cam.target.x) + (cam.position.y - cam.target.y) * (cam.position.y - cam.target.y) +
                               (cam.position.z - cam.target.z) * (cam.position.z - cam.target.z));
        Vector3 fc = {0.80f, 0.88f, 0.95f};
        Vector3 fr = fog ? Vector3{dist + 5.0f, dist + 80.0f, 0.62f} : Vector3{0, 1, 0};   // start, end, strongest mix
        SetShaderValue(sh_, locFogCol_, &fc, SHADER_UNIFORM_VEC3);
        SetShaderValue(sh_, locFogRange_, &fr, SHADER_UNIFORM_VEC3);
    }
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
// Foliage is made of overlapping blobs, dark at the bottom and light at the top (a hand-painted
// vertex-colour gradient), on a trunk that flares at the root; every one casts a soft shadow.
void Models3D::tree(float x, float z, int seed, int kind)
{
    if (kind < 0) kind = (int)(hashi(seed, 21) % 4);
    float sc = 0.62f + rndf(seed, 22) * 0.36f, rot = rndf(seed, 23) * 2 * PI;   // about 25% smaller than before: the canopies stood twice Gran's height
    Matrix root = then(then(S(sc, sc, sc), MatrixRotateY(rot)), T(x, 2, z));
    auto at = [&](Matrix m) { return then(m, root); };
    float v = rndf(seed, 24);
    Color trunk = hexc(0x6a4430);
    float cr = kind == 1 ? 0.55f : (kind == 3 ? 0.75f : 1.1f);
    shadowAt(x + 0.3f * sc, 2.0f, z + 0.12f * sc, cr * sc * 1.15f, cr * sc * 0.85f, 0.42f);
    // a blob: its colour darkens towards the underside of the canopy (hy 0 bottom .. 1 top)
    auto blob = [&](Color leaf, float bx, float by, float bz, float rx, float ry, float rz, float hy) {
        Color c = mixc(mixc(leaf, BLACK, 0.2f * (1 - hy)), mixc(leaf, hexc(0xe8f08c), 0.5f), hy * hy * 0.5f);
        part(sphere_, at(then(S(rx, ry, rz), T(bx, by, bz))), c);
    };
    auto flare = [&](float r, float h) {   // a trunk that widens into its roots
        part(cyl_, at(then(S(r, h, r), T(0, 0, 0))), trunk);
        part(cone_, at(then(S(r * 2.6f, 0.38f, r * 2.6f), T(0, 0, 0))), mixc(trunk, BLACK, 0.12f));
    };
    int var = (int)(hashi(seed, 27) % 4);   // four variants of every kind
    if (kind == 0) {   // round: five to seven blobs
        float h = 1.3f + rndf(seed, 25) * 0.5f;
        Color leaf = mixc(hexc(0x4f9a3e), hexc(0x7cb44c), v);
        flare(0.17f, h + 0.3f);
        int n = 8 + var % 3;   // more, smaller lumps: a softer, less chunky canopy
        blob(leaf, 0, h + 0.5f, 0, 0.85f, 0.7f, 0.85f, 0.55f);
        for (int i = 0; i < n - 1; ++i) {
            float a = i * 2.4f + rndf(seed, 28) * 6, hy = (float)i / (n - 1);
            float rr = 0.38f + 0.16f * rndf(seed + i, 29);
            blob(leaf, std::cos(a) * 0.66f, h + 0.15f + 0.85f * hy + (i % 2) * 0.1f, std::sin(a) * 0.55f, rr, rr * 0.85f, rr, 0.25f + 0.75f * hy);
        }
    } else if (kind == 1) {   // tall oval, like a poplar: stacked overlapping blobs
        Color leaf = mixc(hexc(0x5c9c44), hexc(0x84bc56), v);
        flare(0.14f, 1.0f);
        for (int i = 0; i < 5; ++i) {
            float hy = i / 4.0f, rr = (0.5f - 0.12f * std::fabs(hy - 0.4f)) * (0.9f + 0.2f * rndf(seed + i, 29));
            blob(leaf, (rndf(seed + i, 30) - 0.5f) * 0.15f, 1.3f + 1.9f * hy, (rndf(seed + i, 31) - 0.5f) * 0.15f, rr, 0.6f, rr, hy);
        }
    } else if (kind == 2) {   // pine: stacked cones, darker towards the ground
        Color leaf = mixc(hexc(0x2e7a4c), hexc(0x449058), v);
        flare(0.15f, 0.7f);
        int n = 3 + var % 2;
        for (int i = 0; i < n; ++i) {
            float r = 1.0f - i * (0.75f / n), y = 0.5f + i * (2.25f / n), hy = (float)i / (n - 1);
            part(cone_, at(then(S(r, 1.25f, r), T(0, y, 0))), mixc(mixc(leaf, BLACK, 0.2f * (1 - hy)), WHITE, 0.04f + 0.05f * (i % 2) + 0.05f * hy));
        }
    } else {   // a low bush of four or five balls
        Color leaf = mixc(hexc(0x4a8f3c), hexc(0x6cae4c), v);
        int n = 4 + var % 2;
        for (int i = 0; i < n; ++i) {
            float a = i * 2.1f + rndf(seed, 28) * 6, rr = 0.34f + 0.18f * rndf(seed + i, 29), hy = i == 0 ? 0.8f : 0.3f;
            blob(leaf, i == 0 ? 0.0f : std::cos(a) * 0.45f, (i == 0 ? 0.42f : 0.25f) * 1.0f, i == 0 ? 0.0f : std::sin(a) * 0.35f, rr * 1.2f, rr, rr * 1.1f, hy);
        }
    }
}

// A plain street house further back.
void Models3D::house(float x, float z, int seed)
{
    float w = 3.0f, h = 2.4f + rndf(seed, 5) * 0.7f, d = 2.8f;
    Color wall = mixc(mixc(hexc(0xe8d4b8), hexc(0xc89a80), rndf(seed, 6)), hexc(0x8a9a78), 0.18f * rndf(seed, 11));   // a little weathering green/grey
    Color roof = hashi(seed, 8) % 2 ? hexc(0xa0503e) : hexc(0x6a6a88);
    shadowAt(x + 0.3f, 2.0f, z + 0.1f, w * 0.85f, d * 0.8f, 0.45f);
    part(cube_, then(S(w, h, d), T(x, 2 + h / 2, z)), wall);
    float a = d / std::sqrt(2.0f);   // gable: a square turned 45 degrees, its top half above the walls
    part(cube_, then(then(S(w - 0.02f, a, a), MatrixRotateX(PI / 4)), T(x, 2 + h, z)), wall);
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        float L = d / std::sqrt(2.0f) + 0.4f;
        Vector3 c = {x, 2 + h + d / 4 + 0.06f, z + sgn * (d / 4 + 0.06f)};
        part(cube_, then(then(S(w + 0.4f, 0.14f, L), MatrixRotateX(sgn * PI / 4)), T(c.x, c.y, c.z)), roof);
    }
    // trim and weathering: a stone plinth, pale corner boards, a darker eave edge, door and windows that vary by house
    const unsigned hv = hashi(seed, 9);
    Color trim = hexc(0xf4ecdc), plinth = mixc(wall, hexc(0x7a7468), 0.6f);
    part(cube_, then(S(w + 0.08f, 0.28f, d + 0.08f), T(x, 2.14f, z)), plinth);
    for (int sx = -1; sx <= 1; sx += 2)
        part(cube_, then(S(0.12f, h, 0.12f), T(x + sx * (w / 2 - 0.02f), 2 + h / 2, z + d / 2 + 0.02f)), trim, 0, false);
    part(cube_, then(S(w + 0.5f, 0.1f, 0.12f), T(x, 2 + h + 0.02f, z + d / 2 + 0.18f)), mixc(roof, BLACK, 0.25f), 0, false);   // eave edge
    part(cube_, then(S(w * 0.96f, 0.35f, 0.02f), T(x, 2 + h - 0.18f, z + d / 2 + 0.015f)), mixc(wall, hexc(0x6a5a4a), 0.12f), 0, false);   // weather stain under the eave
    Color door = hv % 3 == 0 ? hexc(0x3f6f8a) : (hv % 3 == 1 ? hexc(0x6a3a2a) : hexc(0x7a3a3a));
    float dx = hv % 2 ? 0.0f : 0.55f;
    part(cube_, then(S(0.74f, 1.2f, 0.07f), T(x + dx, 2.6f, z + d / 2 + 0.035f)), trim, 0, false);
    part(cube_, then(S(0.6f, 1.1f, 0.08f), T(x + dx, 2.55f, z + d / 2 + 0.04f)), door);
    part(cube_, then(S(0.9f, 0.1f, 0.4f), T(x + dx, 2.21f, z + d / 2 + 0.2f)), plinth, 0, false);   // step
    const int nWin = hv % 2 ? 2 : 1;
    for (int wi = 0; wi < nWin; ++wi) {
        float wx = x + (nWin == 1 ? -0.8f : (wi ? 0.9f : -0.9f)), wy = 2 + h * 0.6f;
        part(cube_, then(S(0.7f, 0.64f, 0.06f), T(wx, wy, z + d / 2 + 0.03f)), trim, 0, false);
        part(cube_, then(S(0.54f, 0.48f, 0.07f), T(wx, wy, z + d / 2 + 0.05f)), hexc(0xffe2a0), 0.7f, false);
        part(cube_, then(S(0.05f, 0.48f, 0.07f), T(wx, wy, z + d / 2 + 0.07f)), trim, 0, false);
        part(cube_, then(S(0.8f, 0.07f, 0.16f), T(wx, wy - 0.38f, z + d / 2 + 0.08f)), plinth, 0, false);   // sill
    }
    // a brick chimney with a cap
    Color brick = mixc(hexc(0x9a5a48), hexc(0x8a6a5a), rndf(seed, 10));
    part(cube_, then(S(0.35f, 0.9f, 0.35f), T(x + 0.8f, 2 + h + 0.8f, z - 0.4f)), brick);
    part(cube_, then(S(0.46f, 0.08f, 0.46f), T(x + 0.8f, 2 + h + 1.28f, z - 0.4f)), mixc(brick, BLACK, 0.3f), 0, false);
}

// Gran's cottage: the hero landmark. Bigger and closer than the street houses.
void Models3D::cottage(float x, float z)
{
    const float w = 4.8f, h = 2.7f, d = 3.6f, fz = z + d / 2;   // fz: the front wall
    const Color wall = hexc(0xf4e6c8), trim = hexc(0xfaf6ee), roof = hexc(0xb4483a), wood = hexc(0x7a4a32);
    shadowAt(x + 0.4f, 2.0f, z + 0.2f, w * 0.78f, d * 0.78f, 0.5f);
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
    {
        float sx = x + (i % 2 ? 0.1f : -0.1f), sz = fz + 1.35f + i * 0.5f;
        Color sc = mixc(hexc(0xb8b0a0), hexc(0x9c968a), rndf(i, 311));
        shadowAt(sx + 0.05f, 2.0f, sz + 0.03f, 0.42f, 0.32f, 0.3f);
        part(cube_, then(S(0.6f, 0.06f, 0.4f), T(sx, 2.01f, sz)), sc, 0, false);
        part(cube_, then(S(0.54f, 0.03f, 0.34f), T(sx, 2.045f, sz)), mixc(sc, WHITE, 0.1f), 0, false);   // bevel: a lighter, smaller top
    }
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

// Two passes over the same code: the first draws nothing and only collects the contact shadows,
// which are laid down on the lawn (flushShadows, below) before anything stands on it in the second.
// The land behind the path, from the fence to the horizon: one displaced height field (gentle rolling
// ground, flat beside the path where the houses and fence stand), cut into a patchwork of fields by
// jittered Voronoi cells (hue +-0.04, brightness and saturation per field; some with crop rows, some
// golden), with hedgerows along the field edges and far trees on them. Baked once per 24-column chunk
// from world coordinates only (never the camera). Quads with an alpha below 1 are crop rows: lit3d.fs
// draws the stripes (world z, faded out where they would alias) from that.
namespace {
float vnoise2(float x, float z, int s)
{
    int ix = (int)std::floor(x), iz = (int)std::floor(z);
    float fx = x - ix, fz = z - iz;
    fx = fx * fx * (3 - 2 * fx); fz = fz * fz * (3 - 2 * fz);
    auto h = [&](int a, int b) { return rndf(a * 7919 + b, s); };
    return (h(ix, iz) * (1 - fx) + h(ix + 1, iz) * fx) * (1 - fz) + (h(ix, iz + 1) * (1 - fx) + h(ix + 1, iz + 1) * fx) * fz;
}
float fieldH(float x, float z)
{
    float d = -z;
    float amp = std::max(0.0f, std::min(1.0f, (d - 12.0f) / 40.0f));
    amp = amp * amp * 1.1f + 0.015f * std::min(1.0f, d / 12.0f);
    float n = 0.6f * vnoise2(x * 0.045f, z * 0.06f, 401) + 0.3f * vnoise2(x * 0.13f, z * 0.17f, 402) + 0.1f * vnoise2(x * 0.4f, z * 0.5f, 403);
    return 1.95f + amp * n;
}
}  // namespace

void Models3D::drawField(Game&, float l, float r)
{
    const int W = 24;
    int c0 = (int)std::floor(l / W), c1 = (int)std::floor(r / W);
    for (int c = c0; c <= c1; ++c) {
        Chunk& ch = chunks_[chunkKey(1, c)];
        if (!ch.built) {
            bakeBegin(ch);
            recTex_ = 0;
            static const float zs[] = {-3, -4.5f, -6, -8, -10, -12.5f, -15, -18, -22, -26, -31, -37, -44, -52, -62, -75, -90, -105, -120};
            const int NZ = sizeof(zs) / sizeof(zs[0]);
            const Color near_ = hexc(0x5aa04a), far_ = hexc(0x8cc378);
            struct Fld { Color col; unsigned id; int type; };
            auto fieldAt = [&](float x, float z) {
                float cx = x / 16.0f, cz = z / 22.0f;
                int ix = (int)std::floor(cx), iz = (int)std::floor(cz), bi = 0, bj = 0;
                float best = 1e9f;
                for (int dx = -1; dx <= 1; ++dx)
                    for (int dz = -1; dz <= 1; ++dz) {
                        unsigned hh = hashi(ix + dx, (iz + dz) * 977 + 3);
                        float px = ix + dx + 0.15f + 0.7f * ((hh & 255) / 255.0f), pz = iz + dz + 0.15f + 0.7f * (((hh >> 8) & 255) / 255.0f);
                        float d = (px - cx) * (px - cx) + (pz - cz) * (pz - cz);
                        if (d < best) { best = d; bi = ix + dx; bj = iz + dz; }
                    }
                unsigned id = hashi(bi, bj * 977 + 3), h2 = hashi(bi, bj * 31 + 9);
                float t = std::min(1.0f, std::sqrt(std::max(0.0f, (-z - 3.0f) / 117.0f)));
                Vector3 hsv = ColorToHSV(mixc(near_, far_, t));
                int type = (int)(h2 % 5);
                hsv.x = std::fmod(hsv.x + ((id & 255) / 255.0f - 0.5f) * 2 * 0.04f * 360.0f + 360.0f, 360.0f);
                hsv.y *= 0.85f + 0.3f * (((id >> 8) & 255) / 255.0f);
                hsv.z *= 0.88f + 0.2f * (((id >> 16) & 255) / 255.0f);
                if (type == 4) { hsv.x = 52.0f + 6.0f * ((id & 15) / 15.0f); hsv.y = 0.5f; hsv.z = std::min(1.0f, hsv.z * 1.12f); }   // ripe corn
                Color col = ColorFromHSV(hsv.x, std::min(1.0f, hsv.y), std::min(1.0f, hsv.z));
                if (type == 2 || type == 3) col.a = 205;   // crop rows
                return Fld{col, id, type};
            };
            auto vert = [&](float x, float z) { return Vector3{x, fieldH(x, z), z}; };
            auto nrm = [&](float x, float z) {
                float e = 0.5f;
                return Vector3Normalize({-(fieldH(x + e, z) - fieldH(x - e, z)) / (2 * e), 1.0f, -(fieldH(x, z + e) - fieldH(x, z - e)) / (2 * e)});
            };
            for (int j = 0; j + 1 < NZ; ++j) {
                float za = zs[j], zb = zs[j + 1];
                for (int i = 0; i < W / 2; ++i) {
                    float xa = c * W + i * 2.0f, xb = xa + 2.0f;
                    Fld f = fieldAt((xa + xb) / 2, (za + zb) / 2);
                    Vector3 q[4] = {vert(xa, za), vert(xb, za), vert(xb, zb), vert(xa, zb)};
                    Vector3 nn[4] = {nrm(xa, za), nrm(xb, za), nrm(xb, zb), nrm(xa, zb)};
                    int ord[6] = {0, 3, 2, 0, 2, 1};   // counter-clockwise seen from above
                    for (int t = 0; t < 2; ++t) {
                        Vector3 pp[3], nt[3];
                        for (int k = 0; k < 3; ++k) { pp[k] = q[ord[t * 3 + k]]; nt[k] = nn[ord[t * 3 + k]]; }
                        recordTri(ch, 0, 0, pp, nt, f.col, 1.0f);
                        // a lower-left shade across each quad, so the flat colours are not dead
                    }
                    // hedgerow along the near edge of the quad where the field changes (not near the path or the cottage)
                    if (j > 0 && -za > 16.0f) {
                        float zc0 = (zs[j - 1] + za) / 2;
                        Fld fp = fieldAt((xa + xb) / 2, zc0);
                        float xm = (xa + xb) / 2;
                        bool nearCot = std::fabs(xm - nearestCottage(xm)) < 12.0f && -za < 34.0f;
                        if (fp.id != f.id && !nearCot && hashi((int)xa, j * 13 + 5) % 10 < 7) {
                            float hy = fieldH(xm, za), hh2 = 0.3f + 0.25f * rndf((int)xa, j * 17 + 1), dep = std::min(1.8f, 1.0f + (-za) / 50.0f);
                            Color hcol = mixc(hexc(0x2f6a34), hexc(0x4f8a3c), rndf((int)xa, j * 19 + 2));
                            part(cube_, then(S(2.05f, hh2 * dep, 0.5f * dep), T(xm, hy + hh2 * dep / 2 - 0.02f, za)), mixc(hcol, BLACK, 0.12f));
                            if (-za < 70.0f)
                                for (int bl = 0; bl < 2; ++bl)
                                    part(sphere_, then(S(0.7f * dep, 0.3f * dep, 0.4f * dep), T(xm + (bl - 0.5f) * 1.0f, hy + hh2 * dep, za)), mixc(hcol, hexc(0x8fb85a), 0.25f));
                            if (hashi((int)xa, j * 23 + 7) % 3 == 0) {   // a far tree on the hedge
                                float sc = (0.9f + 0.7f * rndf((int)xa, j * 29 + 3)) * dep * 1.05f, tx = xm + (rndf((int)xa, j * 31) - 0.5f) * 1.2f;
                                Color leaf = mixc(hexc(0x3f8a3c), hexc(0x70aa4c), rndf((int)xa, j * 37));
                                part(cyl_, then(S(0.07f * sc, 0.9f * sc, 0.07f * sc), T(tx, hy, za)), hexc(0x5e4030), 0, false);
                                for (int bl = 0; bl < 3; ++bl) {
                                    float a = bl * 2.1f + rndf((int)xa, j * 41) * 6.0f, hyb = 0.9f + 0.45f * bl;
                                    part(sphere_, then(S(0.7f * sc, 0.6f * sc, 0.7f * sc), T(tx + std::cos(a) * 0.3f * sc * (bl > 0), hy + hyb * sc, za + std::sin(a) * 0.25f * sc * (bl > 0))),
                                         mixc(mixc(leaf, BLACK, 0.18f * (2 - bl)), hexc(0xe8f08c), 0.12f * bl), 0, false);
                                }
                            }
                        }
                    }
                }
            }
            bakeEnd();
        }
        drawChunk(ch);
    }
    trimChunks(1, c0 - 2, c1 + 2);
}

void Models3D::drawScenery(Game& g)
{
    dry_ = true;
    sceneryPass(g);
    dry_ = false;
    sceneryPass(g);
    drawFlowers();
}

void Models3D::sceneryPass(Game& g)
{
    float l = g.cam.left() - 16, r = g.cam.right() + 36, mid = (l + r) / 2, wdt = r - l;
    setMacro(0.6f);   // big soft patches of lighter and darker turf over the lawns, set from world position
    // lawns. In front of the path the lawn lies at the foot of the path (pits are ditches down to
    // it); behind it, at path height, in flat bands that step lighter towards the horizon.
    part(cube_, then(S(wdt + 300, 0.4f, 18), T(mid, -0.2f, 6.0f)), hexc(0x4a9040), 0, false);
    part(cube_, then(S(wdt + 300, 1.9f, 4), T(mid, 0.95f, -5.0f)), hexc(0x6a4a34), 0, false);   // bank behind the ditches
    if (!dry_) drawField(g, l, r);   // the displaced mid-field: patchwork of fields, crop rows, hedges and far trees
    if (!dry_) flushShadows();   // the lawn is down: the shadows go on it, then everything stands over them
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
        shadowAt(px + 0.12f, 2.0f, -3.4f, 0.17f, 0.1f, 0.33f);
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
            shadowAt(x + 0.25f, 2.0f, z + 0.05f, 0.3f, 0.16f, 0.38f);
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
// The solid tiles. Static ground is baked in chunks of 16 columns (rebuilt only when the tiles of a
// chunk change) and drawn in a few calls; the pulsing memory blocks are the only live parts.
void Models3D::drawGround(Game& g, bool meadow)
{
    meadow_ = meadow;
    struct Done { Models3D* m; ~Done() { m->setMacro(0.0f); } } done{this};   // the ground noise off again
    int x0 = (int)std::floor(g.cam.left()) - 6, x1 = (int)std::ceil(g.cam.right()) + 18;
    const int W = meadow ? 8 : 16;   // narrow chunks: less off-screen geometry sent each frame (16 drew about twice the visible columns)
    auto fdiv = [](int a, int b) { return (int)std::floor((float)a / b); };
    int c0 = fdiv(x0, W), c1 = fdiv(x1, W);
    for (int c = c0; c <= c1; ++c) {
        Chunk& ch = chunks_[chunkKey(meadow ? 3 : 0, c)];
        unsigned h = 2166136261u;
        for (int xx = c * W - 1; xx <= c * W + W; ++xx)
            for (int yy = 0; yy < LEVEL_H; ++yy)
                h = (h ^ (unsigned char)(xx < g.level.firstCol() ? (yy >= LEVEL_H - 2 ? '#' : '.') : g.level.tile(xx, yy))) * 16777619u;
        h ^= (unsigned)g.level.firstCol() * 31u;
        h ^= meadow ? 0x9e3779b9u : 0u;
        if (!ch.built || ch.hash != h) {
            bakeBegin(ch);
            bakeGround(g, c * W, c * W + W - 1, 0);
            bakeEnd();
            ch.hash = h;
        }
        drawChunk(ch);
    }
    trimChunks(meadow ? 3 : 0, c0 - 3, c1 + 3);
    if (!meadow) bakeGround(g, x0, x1, 1);
    meadow_ = false;
}

void Models3D::bakeGround(Game& g, int x0, int x1, int mode)
{
    const float zf = 1.0f, zb = -3.0f, zc = (zf + zb) / 2, dz = zf - zb;
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
            if ((mode == 1) != (ch == '1' || ch == '2')) { ++x; continue; }
            bool floating = !solid(x, ty + 1) && ty < LEVEL_H - 1;
            // the 2.5D meadow takes only the ground the kit models (earth with ground under it); hay bales,
            // cracked blocks, the wall and rocky undersides stay 2D tiles (Renderer)
            if (meadow_ && (ch != '#' || floating || !groundKit_)) { ++x; continue; }
            if (ch != '#' || floating) {   // a single block
                Color c = ch == '=' ? hexc(0xa08a74) : (ch == '|' ? hexc(0x8a8aa4) : hexc(0xb89a7a));
                if (ch == '1' || ch == '2') {   // memory blocks only one of the two can stand on
                    c = mixc(ch == '1' ? hexc(0xf09a40) : hexc(0x4a94f0), WHITE, 0.12f + 0.12f * std::sin(g.time * 3 + x));
                    part(cube_, then(S(0.9f, 0.9f, dz * 0.6f), T(x + 0.5f, Y3(ty + 0.5f), zc)), c, 0.25f);
                    ++x;
                    continue;
                }
                recTex_ = ch == '#' ? 2 : 3;
                part(cube_, then(S(0.96f, 0.96f, dz), T(x + 0.5f, Y3(ty + 0.5f), zc)), c);
                if (ch == '=') part(cube_, then(S(0.5f, 0.06f, 0.06f), T(x + 0.45f, Y3(ty + 0.45f), zf + 0.02f)), hexc(0x4a3a30), 0, false);
                recTex_ = 1;
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
            if (groundKit_) {   // the Blender kit: one tile model per column, a variant from a world hash (never the left neighbour's)
                int kind = dep == 0 ? 0 : (dep < 3 ? 1 : 2), n = groundN_[kind];
                Color tint = dep == 2 ? Color{225, 215, 210, 255} : WHITE;
                if (meadow_) {   // golden hour: the kit's greens pulled to warm olive, the earth to a redder brown
                    tint = dep == 0 ? Color{210, 190, 124, 255} : Color{226, 194, 164, 255};
                    if (dep >= 3) tint = Color{206, 184, 166, 255};
                }
                int last = -1;
                recTex_ = 2;
                for (int i = x; i <= e; ++i) {
                    int v = (int)(hashi(i, ty * 131 + 900 + kind) % (unsigned)n);
                    if (v == last) v = (v + 1 + (int)(hashi(i, ty + 77) % (unsigned)(n - 1))) % n;
                    last = v;
                    part((meadow_ && liteKit_ ? liteMod_ : groundMod_)[kind][v], T(i + 0.5f, Y3(ty + 0.5f), zc), tint, 0, false);
                }
                x = e + 1;
                continue;
            }
            recTex_ = 2;
            if (dep == 0)   // stops under the grass cap, so the two tops never fight
                part(cube_, then(S(w, 0.9f, dz), T(cx, Y3(ty + 0.55f), zc)), dirt);
            else
                part(cube_, then(S(w, 1.0f, dz), T(cx, Y3(ty + 0.5f), zc)), dirt, 0, false);
            // The faces below are made tile by tile, every one from its own world-position hash
            // (never the camera), so no two tiles match and the edges are not dead straight.
            // Grass shifts yellow-green where it is lit and blue-green where it shades.
            const Color gLit = hexc(0x86c24a), gMid = hexc(0x5aa84a), gShade = hexc(0x3c9a5c);
            for (int i = x; i <= e; ++i) {
                unsigned th = hashi(i, ty * 131 + 7);
                float r1 = (th & 255) / 255.0f, r2 = ((th >> 8) & 255) / 255.0f, r3 = ((th >> 16) & 255) / 255.0f;
                recTex_ = 1;
                if (dep == 0) {
                    // the cap: its top flush with the path (y 0), the front edge of its lip a different
                    // distance out on every tile and its height a hair different
                    float j = (r2 - 0.5f) * 0.05f, zFront = zf + 0.03f + 0.08f * r1, zBack = zb - 0.05f;
                    Color top = mixc(mixc(gMid, gLit, r3), gShade, 0.25f * (1 - r1) * (1 - r3));
                    part(cube_, then(S(1.04f, 0.26f + j, zFront - zBack), T(i + 0.5f, Y3(ty + 0.13f + j * 0.5f), (zFront + zBack) / 2)), top, 0, false);
                    // the lip's face in shade: bluer, darker, a bit proud of the cap; then the soil under it in the cap's shadow
                    part(cube_, then(S(1.04f, 0.1f, 0.035f), T(i + 0.5f, Y3(ty + 0.2f + j), zFront)), mixc(gShade, gMid, r2 * 0.6f), 0, false);
                    part(cube_, then(S(1.0f, 0.08f, 0.02f), T(i + 0.5f, Y3(ty + 0.3f), zf + 0.003f)), mixc(dirt, BLACK, 0.45f), 0, false);
                    for (int b = 0; b < 2; ++b) {   // a few blades hanging over the lip
                        if (hashi(i * 2 + b, ty * 17 + 211) % 100 > 38) continue;
                        float bx = i + 0.1f + 0.8f * rndf(i * 2 + b, 212 + ty), len = 0.16f + 0.12f * rndf(i * 2 + b, 213 + ty);
                        part(cone_, then(then(then(S(0.03f, len, 0.03f), MatrixRotateX(PI / 2 + 0.5f + rndf(i * 2 + b, 214) * 0.5f)),
                                          MatrixRotateZ((rndf(i * 2 + b, 215) - 0.5f) * 0.5f)), T(bx, Y3(ty + 0.17f), zFront - 0.01f)),
                             mixc(gMid, gLit, rndf(i * 2 + b, 216)), 0, false);
                    }
                }
                if (dep == 0) {
                    // blades standing on the cap: three shapes (needle, wide leaf, curled), random turn and
                    // tilt, scale 0.6 to 1.2, greens from a noise ramp (yellow-green lit, blue-green in the shade)
                    for (int b = 0; b < 5; ++b) {
                        int bs = i * 8 + b;
                        float bx = i + 0.04f + 0.92f * rndf(bs, 231 + ty), bz = zb + 0.3f + (zf - zb - 0.3f) * rndf(bs, 232 + ty * 3);
                        float sc = 0.6f + 0.6f * rndf(bs, 233), tilt = (rndf(bs, 234) - 0.5f) * 0.9f, turn = rndf(bs, 235) * PI;
                        float nz = 0.5f + 0.5f * std::sin(bx * 0.7f + bz * 1.3f) * std::sin(bx * 0.23f - bz * 0.6f);   // a noise ramp across the field
                        Color bc = nz < 0.33f ? mixc(gShade, gMid, nz * 3) : (nz < 0.66f ? mixc(gMid, gLit, (nz - 0.33f) * 3) : mixc(gLit, hexc(0xb6d25a), (nz - 0.66f) * 3));
                        bc = mixc(bc, BLACK, 0.12f * rndf(bs, 236));
                        int shape = (int)(hashi(bs, 237) % 3);
                        Matrix blade = shape == 0 ? S(0.025f * sc, 0.2f * sc, 0.025f * sc)
                                     : (shape == 1 ? S(0.07f * sc, 0.14f * sc, 0.012f * sc) : S(0.035f * sc, 0.17f * sc, 0.035f * sc));
                        Matrix lean = then(MatrixRotateZ(tilt), MatrixRotateY(turn));
                        if (shape == 2) lean = then(then(MatrixRotateZ(tilt), MatrixRotateX(0.5f)), MatrixRotateY(turn));
                        part(cone_, then(then(blade, lean), T(bx, Y3(ty + 0.0f), bz)), bc, 0, false);
                    }
                }
                recTex_ = 2;
                // strata: bands of earth that run on across tiles and wave a little, each its own shade
                const float yS = dep == 0 ? 0.36f : 0.0f;
                const int bands = dep == 0 ? 3 : 4;
                const float bh = (1.0f - yS) / bands;
                for (int bnd = 0; bnd < bands; ++bnd) {
                    unsigned bhsh = hashi(i / 3 + bnd * 57, ty * 7 + 301);
                    float warm = ((bhsh & 255) / 255.0f - 0.5f), lite = (((bhsh >> 8) & 255) / 255.0f - 0.5f);
                    Color sc = mixc(mixc(dirt, hexc(0x9a6a44), warm > 0 ? warm * 0.5f : 0.0f), warm < 0 ? hexc(0x4a3a30) : dirt, std::max(0.0f, -warm) * 0.5f);
                    sc = lite > 0 ? mixc(sc, WHITE, lite * 0.12f) : mixc(sc, BLACK, -lite * 0.3f);
                    float wave = 0.035f * std::sin(i * 0.9f + bnd * 2.4f + ty * 2.0f) + (r3 - 0.5f) * 0.02f;
                    float cy = yS + bh * (bnd + 0.5f) + wave;
                    if (bnd == bands - 1 && dep > 0) continue;   // the last band is the next row's first
                    part(cube_, then(S(1.0f, bh * 0.92f, 0.02f), T(i + 0.5f, Y3(ty + cy), zf + 0.004f + 0.003f * bnd)), sc, 0, false);
                }
                // pebbles, one at most per cell of a 3 x 3 grid and small enough never to touch a
                // neighbour (no overlap), set in the face a third sunk and lying with the surface
                // (a few degrees off), more of them deeper down
                recTex_ = 3;
                static const unsigned stone[5] = {0xc8bca8, 0x9a948c, 0xc4a87a, 0x7a6e64, 0xdcd4c6};
                const float yTop = dep == 0 ? 0.4f : 0.02f, yBot = 0.97f;
                for (int cx = 0; cx < 3; ++cx)
                    for (int cy = 0; cy < 3; ++cy) {
                        int sd = ((i * 3 + cx) * 7 + cy) * 61 + ty * 13;
                        float fy = (cy + 0.5f) / 3.0f;
                        if (rndf(sd, 91) > 0.1f + 0.26f * fy) continue;
                        float px = i + (cx + 0.5f) / 3.0f + (rndf(sd, 92) - 0.5f) * 0.12f;
                        float py = yTop + (yBot - yTop) * (cy + 0.5f + (rndf(sd, 93) - 0.5f) * 0.4f) / 3.0f;
                        float sz = 0.03f + 0.045f * rndf(sd, 94) * rndf(sd, 95) + 0.012f;
                        float sq = 0.6f + 0.35f * rndf(sd, 96), rot = (rndf(sd, 97) - 0.5f) * 0.7f;
                        Color c = mixc(hexc(stone[hashi(sd, 98) % 5]), dirt, 0.15f + 0.3f * rndf(sd, 99));
                        part(sphere_, then(then(S(sz, sz * sq, 0.045f), MatrixRotateZ(rot)), T(px, Y3(ty + py), zf + 0.01f)), c, 0, false);
                    }
                recTex_ = 2;
                // now and then a root coming out under the lip, tapering to a hair
                if (dep == 0 && hashi(i, ty * 31 + 77) % 7 == 0) {
                    float rx = i + 0.2f + 0.6f * r2, len = 0.35f + 0.3f * r3;
                    part(cone_, then(then(S(0.03f, len, 0.03f), MatrixRotateZ(PI + (r1 - 0.5f) * 1.0f)), T(rx, Y3(ty + 0.34f), zf + 0.012f)), hexc(0x3a2618), 0, false);
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

// A clump of wild flowers of one of three species: poppies (cupped red or buttercup yellow), daisies
// (flat white or pink petals round a big yellow eye) or cornflowers (narrow spiky blue or violet
// petals). Stems are scattered through a noise mask, each at scale 0.7 to 1.3 with a hue jitter
// of +-0.03, a stalk bent 5 to 15 degrees in two pieces, two leaves and five squashed-sphere
// petals. The first time a clump is asked for it is baked into the flower chunk of its world
// column; after that the chunk draws it (drawFlowers), so a clump costs no draw calls.
void Models3D::flowerClump(float x, float y, float z, int seed, float size, bool stems)
{
    if (dry_) return;
    if (!rec_) {
        unsigned id = hashi(seed, (int)std::lround(x * 16)) ^ hashi((int)std::lround(z * 16), (int)std::lround(y * 16) + 5) ^ (stems ? 77u : 0u);
        Chunk& ch = chunks_[chunkKey(2, (int)std::floor(x / 16))];
        if (!ch.seeds.insert(id).second) return;   // already in the chunk
        rec_ = true; recChunk_ = &ch; recTex_ = 0;
        flowerClump(x, y, z, seed, size, stems);
        rec_ = false; recChunk_ = nullptr;
        ch.built = true; ch.dirty = true;
        return;
    }
    static const unsigned heads[3][2] = {{0xd8382e, 0xf2cc3a}, {0xf6f2e4, 0xe89ac0}, {0x4a6ad0, 0x7a5ac8}};
    int kind = (int)(hashi(seed, 111) % 3), alt = (int)(hashi(seed, 120) % 2), n = 5 + (int)(hashi(seed, 112) % 5);
    for (int i = 0; i < n; ++i) {
        float rr = rndf(seed + i, 113) * rndf(seed + i, 121), an = rndf(seed + i, 114) * 6.2831853f;   // dense at the middle
        float px = x + std::cos(an) * rr * 0.55f * size, pz = z + std::sin(an) * rr * 0.3f * size;
        float mask = 0.5f + 0.5f * std::sin(px * 2.3f + pz * 1.7f) * std::sin(px * 0.9f - pz * 2.9f + 1.0f);   // noise mask: gaps in the clump
        if (mask < 0.22f) continue;
        float sc = (0.7f + 0.6f * rndf(seed + i, 122)) * size * 0.85f;
        float hh = (0.2f + 0.2f * rndf(seed + i, 115)) * sc, r = 0.06f * sc;
        Color hc = hexc(heads[kind][alt]);
        {   // hue jitter +-0.03 of the colour wheel, and a little brightness
            Vector3 hsv = ColorToHSV(hc);
            hsv.x = std::fmod(hsv.x + (rndf(seed + i, 116) - 0.5f) * 2 * 0.03f * 360.0f + 360.0f, 360.0f);
            hsv.z = std::min(1.0f, hsv.z * (0.88f + 0.2f * rndf(seed + i, 123)));
            hc = ColorFromHSV(hsv.x, hsv.y, hsv.z);
        }
        float dirn = hashi(seed + i, 124) & 1 ? 1.0f : -1.0f;
        float th = dirn * (5.0f + 10.0f * rndf(seed + i, 117)) * PI / 180.0f;   // the stalk bends 5 to 15 degrees
        float mh = hh * 0.5f;
        Color stem = mixc(hexc(0x4a7a34), hexc(0x6a9a3c), rndf(seed + i, 118));
        float tl = th * 0.4f, tu = th * 1.0f;
        float mx = px + std::sin(tl) * mh, my = y + std::cos(tl) * mh;   // top of the lower piece
        part(cyl_, then(then(S(0.012f * sc, mh, 0.012f * sc), MatrixRotateZ(-tl)), T(px, y, pz)), stem, 0, false);
        float uh = hh - mh + 0.02f;
        part(cyl_, then(then(S(0.011f * sc, uh, 0.011f * sc), MatrixRotateZ(-tu)), T(mx, my, pz)), stem, 0, false);
        float hx = mx + std::sin(tu) * uh, hy = my + std::cos(tu) * uh;
        for (int lf = 0; lf < 2; ++lf) {   // two leaves, one each side
            float sd = lf ? -1.0f : 1.0f;
            part(sphereTiny_, then(S(0.075f * sc, 0.012f, 0.026f * sc), then(MatrixRotateZ(sd * 0.55f), T(px + sd * 0.05f * sc, y + mh * (0.35f + 0.5f * lf), pz))), stem, 0, false);
        }
        float rot = rndf(seed + i, 119) * 6.2831853f;
        float pl = r * (kind == 1 ? 1.5f : (kind == 2 ? 1.35f : 1.1f)), pw = r * (kind == 0 ? 1.0f : (kind == 1 ? 0.5f : 0.38f));
        float tilt = kind == 0 ? 0.7f : (kind == 1 ? 0.12f : 0.4f);
        for (int k = 0; k < 5; ++k) {   // five squashed-sphere petals
            float a = rot + k * 6.2831853f / 5;
            Color pc = mixc(hc, kind == 1 ? hexc(0xdad4c0) : BLACK, (k % 2) * (kind == 1 ? 0.2f : 0.08f));
            part(sphereTiny_, then(then(S(pl * 0.55f, pl * 0.14f, pw), then(MatrixRotateZ(tilt), T(pl * 0.6f, 0, 0))), then(MatrixRotateY(-a), T(hx, hy, pz))), pc, 0.12f, false);
        }
        Color cc = kind == 0 ? (alt ? hexc(0xb88a1a) : hexc(0x2a2020)) : (kind == 1 ? hexc(0xf2c030) : hexc(0x2a2a6a));
        float cs = kind == 1 ? 0.55f : 0.4f;
        part(sphereTiny_, then(S(r * cs, r * cs * 0.8f, r * cs), T(hx, hy + 0.012f * sc, pz)), cc, 0.2f, false);
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
    dry_ = true;
    meadowPass(g);
    dry_ = false;
    meadowPass(g);
    drawFlowers();
}

void Models3D::meadowPass(Game& g)
{
    setLight(true);
    rims_ = false;
    if (!dry_) { setMacro(0.45f); flushShadows(); }
    const float l = g.cam.left() - 8, r = g.cam.right() + 14;
    const Color dusk = hexc(0xf0b48a);
    auto hz = [&](Color c, float z) { return mixc(c, dusk, z > -1.5f ? 0.0f : (z > -8 ? 0.1f : (z > -20 ? 0.24f : 0.36f))); };
    const float G = 1.98f;   // the field behind the path
    const Color hayL = hexc(0xf0d890), hayD = hexc(0xa88a50), straw = hexc(0xe6c878);

    // the ground block's contact shadow: the ground it stands on (the field behind, the low lawn in front)
    // is darkened along its back and front foot, so the block sits in the scene instead of floating over it
    if (dry_) {
        for (int ix = (int)std::floor(l); ix <= (int)r; ++ix) {
            char top = ix < g.level.firstCol() ? '#' : g.level.tile(ix, LEVEL_H - 2);
            if (top != '#') continue;
            float j = (rndf(ix, 171) - 0.5f) * 0.2f;
            shadowAt(ix + 0.55f, G, -3.2f + j, 0.95f, 0.5f, 0.26f);   // behind, on the field (the lip's overhang)
            shadowAt(ix + 0.55f, 0.0f, 1.35f + j, 0.95f, 0.45f, 0.3f);   // in front, on the low lawn
        }
    }

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
            shadowAt(x + 0.2f, G, z + 0.05f, 0.5f * sc, 0.36f * sc, 0.32f);
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
            shadowAt(x + 0.15f, G, z + 0.05f, 0.85f * (n == 2 ? 1.4f : 1.0f), 0.5f, 0.34f);
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
                shadowAt(px + 0.1f, G, fz + 0.04f, 0.2f, 0.12f, 0.3f);
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
    if (!dry_) setMacro(0.0f);
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
                // a pond: a wet dark mud ring, shallow water, deeper water in the middle, a glint, reeds
                part(cyl_, then(S(1.5f, 0.008f, 0.74f), T(x, 0.002f, 2.9f)), hexc(0x4e3c2c), 0, false);
                part(cyl_, then(S(1.25f, 0.012f, 0.6f), T(x, 0.004f, 2.9f)), hexc(0x7aa8cc), 0.35f, false);
                part(cyl_, then(S(0.85f, 0.014f, 0.38f), T(x + 0.05f, 0.006f, 2.92f)), hexc(0x4f7fa8), 0.3f, false);
                part(cyl_, then(S(0.45f, 0.016f, 0.13f), T(x - 0.3f, 0.008f, 2.82f)), hexc(0xd8ecf8), 0.7f, false);
                for (int rd = 0; rd < 5; ++rd) {
                    float rx = x + (rd < 3 ? -1.35f : 1.3f) + (rndf(k * 5 + rd, 231) - 0.5f) * 0.4f, rz = 2.75f + rndf(k * 5 + rd, 232) * 0.35f;
                    float rh = 0.5f + 0.35f * rndf(k * 5 + rd, 233);
                    part(cone_, then(then(S(0.035f, rh, 0.035f), MatrixRotateZ((rndf(k * 5 + rd, 234) - 0.5f) * 0.25f)), T(rx, 0, rz)), mixc(hexc(0x5a8a3a), hexc(0x8aa04a), rndf(k * 5 + rd, 235)), 0, false);
                }
            }
            continue;
        }
        float w = 0.72f + 0.12f * rndf(k, 203), d = 0.62f + 0.14f * rndf(k, 204);
        Color c = mixc(hexc(0xc4bcae), hexc(0xa8a094), rndf(k, 205));
        // bevelled: a wider, darker slab with a smaller lighter top, sunk a hair, with its own cool or warm cast
        c = mixc(c, hashi(k, 209) % 2 ? hexc(0xc8b49a) : hexc(0x9aa0a4), 0.25f * rndf(k, 210));
        float yaw = (rndf(k, 206) - 0.5f) * 0.18f;
        shadowAt(x + 0.06f, 0.0f, z + 0.04f, w * 0.68f, d * 0.68f, 0.3f);
        part(cube_, then(then(S(w, 0.07f, d), MatrixRotateY(yaw)), T(x, 0.01f, z)), mixc(c, BLACK, 0.1f), 0, false);
        part(cube_, then(then(S(w - 0.07f, 0.03f, d - 0.07f), MatrixRotateY(yaw)), T(x, 0.05f, z)), mixc(c, WHITE, 0.08f), 0, false);
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
            float kx = 1 + 0.13f * std::sin(ph * 2.4f + 2), ky = 1 + 0.13f * std::sin(ph * 1.1f), kz = 1 + 0.1f * std::cos(ph * 1.3f);
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
        float x = -1.0f + i * 0.4f, sway = std::sin(i * 2.4f + b.t * 3) * 0.35f;
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

    // Anti-sliding: the run cycle advances with the DISTANCE covered (so the planted foot stays put on the
    // ground at any speed, instead of the legs beating at a fixed rate), and while running the body turns
    // from the 3/4 view towards the side view, so the legs swing along the line of travel instead of
    // across it. Idle keeps the 3/4 turn.
    bool running = pl.onGround && std::fabs(pl.vx) > 0.6f;
    float dtp = std::max(0.0f, std::min(0.1f, g.time - lastT_[p]));
    lastT_[p] = g.time;
    if (running) runPhase_[p] += 2.6f * std::fabs(pl.vx) * dtp;
    runBlend_[p] += std::max(-1.0f, std::min(1.0f, ((running ? 1.0f : 0.0f) - runBlend_[p]) * 9.0f * dtp));
    float yawOff = 0.52f - 0.57f * runBlend_[p];
    float yaw = pl.facing > 0 ? -yawOff : PI + yawOff;
    float ph = runPhase_[p] + p * 1.3f;
    float run = running ? std::min(1.0f, std::fabs(pl.vx) / 4.0f + 0.4f) : 0.0f;
    float s = std::sin(ph), c = std::cos(ph);

    // joint angles (radians); positive swings a limb forward (+X)
    float thighF = 1.15f * s * run, thighB = -1.15f * s * run;
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

    // foot contact: the leg parts are 0.26 (thigh) + about 0.27 (shin to the shoe centre) + the shoe's own half height,
    // so the body is set at the height where the lower sole just touches the ground (no sliding or floating through the
    // cycle, no sinking when the leg is straight). The sole is the lowest point of the rotated shoe ellipsoid.
    float footLift = 0.0f;
    if (pl.onGround && !pl.pounding) {
        const float sa = p == 0 ? 0.11f : 0.12f, sb = p == 0 ? 0.065f : 0.07f, sx = p == 0 ? 0.05f : 0.04f;
        float dMax = 0.0f;
        for (int side = 0; side < 2; ++side) {
            float th = side ? thighF : thighB, ph2 = th + (side ? kneeF : kneeB);
            float yc = -0.26f * std::cos(th) + (-0.27f * std::cos(ph2) + sx * std::sin(ph2));   // shoe centre below the hip
            float h = std::sqrt(sa * sa * std::sin(ph2) * std::sin(ph2) + sb * sb * std::cos(ph2) * std::cos(ph2));
            dMax = std::max(dMax, -(yc - h));
        }
        footLift = dMax - 0.55f - 0.01f;   // hips 0.55 above the root; keep 1 cm of sole in the lawn
        bob = 0;
    }
    Matrix root = then(MatrixRotateY(yaw), T(pl.cx(), Y3(pl.bottom()) + bob - crouch + footLift, 0.2f));
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
    const float FOOT_H = 0.1231f, PEND_X = 0.0648f, PEND_Y = 1.2953f, DIAL_X = 0.2306f, DIAL_Y = 1.9301f;
    const float FACE_X = 0.2228f, FACE_Y = 2.215f;
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
            part(sphere_, at(then(S(0.025f, mad ? 0.052f : 0.045f, 0.0375f), T(FACE_X + 0.008f, FACE_Y, s * 0.065f))),
                 mad ? red : ink, mad ? 1.0f : 0.0f, false);
        float tilt = mad ? 0.6f : 0.42f;
        part(cube_, at(then(then(S(0.0375f, 0.0275f, 0.094f), MatrixRotateX(-s * tilt)),
                            T(FACE_X + 0.012f, FACE_Y + 0.06f - (mad ? 0.01f : 0), s * 0.0625f))),
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
            float sway = std::cos(t * 1.9f + i * 2.4f - j * 0.6f) * 0.15f;
            m = then(then(MatrixRotateZ(wave), MatrixRotateX(sway)), then(T(0, -0.4f * sc, 0), m));
            sc *= 0.9f;
        }
        look(1);
        modelPart(forget_[F_TIP], then(S(sc, sc, sc), m), tint);
    }
    setLook({0, 0, 0, 0}, 0.0f);
}
