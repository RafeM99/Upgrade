// Models3D.h: the full 3D stage (stage 6). Gran, Sam, the creatures of the Forgetting,
// fragments and the street around them are modelled from lit primitives (boxes, spheres,
// cylinders, a torus) instead of drawn as flat sprites. Everything is made in code.
#pragma once
#include "raylib.h"
#include <map>
#include <set>
#include <vector>

class Game;
struct Player;
struct Enemy;
struct Boss;

class Models3D {
public:
    void init();
    void shutdown();
    // Sets the lighting for this frame. Call inside BeginMode3D.
    void begin(Game& g, const Camera3D& cam, bool fog = false);   // fog: era 6 aerial perspective (lit3d.fs fogCol/fogRange)
    void drawScenery(Game& g);                      // lawns, fence, trees, houses, lamps, Gran's cottage
    bool hasGroundKit() const { return groundKit_; }
    void drawGround(Game& g, bool meadow = false);                       // the solid tiles as grass-topped blocks
    void drawMeadow(Game& g);                       // 2.5D stage: hay, stooks, hedge, track, flowers, grass
    static bool modelsTile(char c) { return c == '#' || c == '=' || c == '|'; }   // drawn by drawGround
    void drawPlayer(Game& g, int p);
    void drawEnemy(Game& g, const Enemy& e);
    void drawBoss(Game& g);
    void drawFragment(float x, float y, float t, float alpha);
    // Soft contact shadow under a player, enemy or the boss (same decal as the props'; lies on the
    // surface at floor height y, nudged along the sun's side). Call between begin() and the models.
    void characterShadow(float x, float y, float z, float rx, float rz, float a);
    void flushCharacterShadows();

private:
    Shader sh_{};
    Model cube_{}, sphere_{}, cyl_{}, torus_{}, cone_{};
    int locView_ = -1, locEmissive_ = -1, locRim_ = -1;
    int locLightPos_ = -1, locLightCol_ = -1, locLightRad_ = -1;
    int locSunDir_ = -1, locSunCol_ = -1, locSkyCol_ = -1, locGroundCol_ = -1;
    int locMacro_ = -1, locFogCol_ = -1, locFogRange_ = -1;
    int locBlend_ = -1, locGrey_ = -1, locMetal_ = -1, locUseTex_ = -1;
    // Applied to every part drawn until changed: blend the lit colour towards blend_.xyz by
    // blend_.w (hit flash, the wool's pale tips) and take out grey_ of the colour (faded).
    Vector4 blend_{0, 0, 0, 0};
    float grey_ = 0.0f;
    void setLook(Vector4 blend, float grey) { blend_ = blend; grey_ = grey; }
    void pushLook();
    void setLight(bool dusk);   // the 3D stage's morning light, or the 2.5D meadow's golden hour
    // small props shared by the meadow and the garden (x, ground height y, z)
    void grassTuft(float x, float y, float z, float h, int seed, Color c);
    void flowerClump(float x, float y, float z, int seed, float size, bool stems);
    bool clearAbove(Game& g, float x0, float x1);   // nothing solid stands above the path in columns x0..x1
    void drawGardenFront(Game& g, float l, float r);
    void bakeGround(Game& g, int x0, int x1, int mode);   // ground tiles x0..x1: mode 0 static (baked), 1 pulsing memory blocks (live)
    void drawField(Game& g, float l, float r);        // the displaced mid-field behind the path (baked per chunk)
    // Baked static meshes: part() calls made between bakeBegin/bakeEnd are merged (per texture) into
    // one mesh per chunk of the world, built once and drawn with a few calls per frame.
    struct BakeBatch { std::vector<float> p, n, uv; std::vector<unsigned char> c; };
    struct BakedMesh { Mesh mesh{}; int tex = 0; float rim = 0, emis = 0; };
    struct Chunk {
        unsigned hash = 0; bool built = false, dirty = false;
        std::vector<BakedMesh> meshes;
        std::map<int, BakeBatch> batches;   // key: tex * 4 + rim + 2 * emissive
        std::set<unsigned> seeds;           // flower clumps already in the chunk
    };
    std::map<long long, Chunk> chunks_;
    bool rec_ = false;
    int recTex_ = 0;                // 0 none, 1 grass, 2 earth, 3 rock (world-space planar UVs)
    Chunk* recChunk_ = nullptr;
    Model sphereLo_{}, sphereTiny_{};
    Texture2D gtex_[4]{};
    static long long chunkKey(int kind, int idx) { return kind * 1000000LL + idx + 500000; }
    void bakeBegin(Chunk& c);
    void bakeEnd();
    void recordPart(Model& m, Matrix t, Color c, float emissive, bool rim);
    void recordTri(Chunk& ch, int tex, int flags, const Vector3* p, const Vector3* n, Color c, float scale, const Color* vc = nullptr);
    void uploadChunk(Chunk& c);
    void drawChunk(Chunk& c);
    void freeChunk(Chunk& c);
    void trimChunks(int kind, int lo, int hi);
    void drawFlowers();             // baked flower chunks near the camera   // 3D stage: beds, low hedge, path, puddle
    bool ready_ = false;
    Vector3 camPos_{0, 0, 50};   // this frame's camera, set by begin()
    bool rims_ = true;   // the 2.5D meadow draws its props without the rim light
    // Draws one lit part. There are no outlines: unless rim is false (flat slabs, tiny details)
    // a rounded part gets a thin lighter rim along its silhouette so it reads against the next.
    void part(Model& m, Matrix t, Color c, float emissive = 0.0f, bool rim = true);
    void tree(float x, float z, int seed, int kind = -1);
    // Soft dark decals lying on the ground under props, collected while the scene is drawn and
    // laid down together by flushShadows (after the lit parts, blended, no depth writes).
    struct Decal { float x, y, z, rx, rz, a; };
    std::vector<Decal> decals_;
    void shadowAt(float x, float y, float z, float rx, float rz, float a = 0.4f) { if (dry_) decals_.push_back({x, y, z, rx, rz, a}); }
    bool dry_ = false;   // collecting shadows only: part() draws nothing
    void sceneryPass(Game& g);
    void meadowPass(Game& g);
    void flushShadows(bool depthTest = false);
    void setMacro(float m);   // the world-space ground noise on or off (lit3d.fs)
    // Gran, Sam, the grandfather clock and the Forgetting (the boss) are modelled in Blender
    // (tools/blender/make_models.py), one .glb per moving part, put together and animated here.
    // Each falls back to the primitives if its files are missing.
    enum { P_BODY, P_HEAD, P_UPPERARM, P_FOREARM, P_THIGH, P_SHIN, P_COUNT };
    enum { C_CASE, C_PENDULUM, C_HOUR, C_MINUTE, C_FOOT_L, C_FOOT_R, C_COUNT };
    enum { F_BODY, F_SEG, F_TIP, F_PUPIL, F_COUNT };
    Model person_[2][P_COUNT]{}, clock_[C_COUNT]{}, forget_[F_COUNT]{};
    bool personReady_[2] = {false, false}, clockReady_ = false, forgetReady_ = false;
    Model groundMod_[3][6]{};   // Blender ground kit: grass top, earth, deep earth
    int groundN_[3] = {0, 0, 0};
    Model liteMod_[3][6]{};     // the same kit at about half the triangles (groundlite_*.glb): the 2.5D meadow draws these
    bool liteKit_ = false;
    bool groundKit_ = false;
    bool meadow_ = false;   // baking the 2.5D meadow's ground (warm tint, only the kit's tiles)
    void loadGroundKit();
    void unloadGroundKit();
    void loadParts(Model* out, const char* const* names, int n, bool& ready);
    void unloadParts(Model* parts, int n, bool& ready);
    void modelPart(Model& m, Matrix t, Color tint, bool rim = true);
    void drawPersonModel(Game& g, int p);   // Gran (0) or Sam (1)
    float runPhase_[2] = {0, 0}, runBlend_[2] = {0, 0}, lastT_[2] = {0, 0};   // run cycle by distance, turn towards the side view
    void drawClockModel(Game& g, const Enemy& e);
    void drawForgetting(Game& g);
    void cottage(float x, float z);
    void house(float x, float z, int seed);
};
