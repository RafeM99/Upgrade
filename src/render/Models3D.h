// Models3D.h: the full 3D stage (stage 6). Gran, Sam, the creatures of the Forgetting,
// fragments and the street around them are modelled from lit primitives (boxes, spheres,
// cylinders, a torus) instead of drawn as flat sprites. Everything is made in code.
#pragma once
#include "raylib.h"

class Game;
struct Player;
struct Enemy;
struct Boss;

class Models3D {
public:
    void init();
    void shutdown();
    // Sets the lighting for this frame. Call inside BeginMode3D.
    void begin(Game& g, const Camera3D& cam);
    void drawScenery(Game& g);                      // lawns, fence, trees, houses, lamps, Gran's cottage
    void drawGround(Game& g);                       // the solid tiles as grass-topped blocks
    void drawMeadow(Game& g);                       // 2.5D stage: hay, stooks, hedge, track, flowers, grass
    static bool modelsTile(char c) { return c == '#' || c == '=' || c == '|'; }   // drawn by drawGround
    void drawPlayer(Game& g, int p);
    void drawEnemy(Game& g, const Enemy& e);
    void drawBoss(Game& g);
    void drawFragment(float x, float y, float t, float alpha);

private:
    Shader sh_{};
    Model cube_{}, sphere_{}, cyl_{}, torus_{}, cone_{};
    int locView_ = -1, locEmissive_ = -1, locOutline_ = -1, locHull_ = -1;
    int locLightPos_ = -1, locLightCol_ = -1, locLightRad_ = -1;
    int locSunDir_ = -1, locSunCol_ = -1, locSkyCol_ = -1, locGroundCol_ = -1;
    void setLight(bool dusk);   // the 3D stage's morning light, or the 2.5D meadow's golden hour
    // small props shared by the meadow and the garden (x, ground height y, z)
    void grassTuft(float x, float y, float z, float h, int seed, Color c);
    void flowerClump(float x, float y, float z, int seed, float size, bool stems);
    bool clearAbove(Game& g, float x0, float x1);   // nothing solid stands above the path in columns x0..x1
    void drawGardenFront(Game& g, float l, float r);   // 3D stage: beds, low hedge, path, puddle
    bool ready_ = false;
    Vector3 camPos_{0, 0, 50};   // this frame's camera, set by begin()
    bool outlines_ = true;   // the 2.5D meadow draws its props without the toon outline
    // Draws one lit part, then (unless outline is false, or the part is tiny or see-through)
    // the same mesh again as a dark inverted hull: the toon outline.
    void part(Model& m, Matrix t, Color c, float emissive = 0.0f, bool outline = true);
    void tree(float x, float z, int seed, int kind = -1);
    // Gran and the Forgetting (the boss) modelled in Blender (tools/blender/make_models.py), one .glb per
    // moving part, put together and animated here. Falls back to the primitives if missing.
    enum { G_BODY, G_HEAD, G_UPPERARM, G_FOREARM, G_THIGH, G_SHIN, G_COUNT };
    enum { F_BODY, F_SEG, F_TIP, F_COUNT };
    Model gran_[G_COUNT]{}, forget_[F_COUNT]{};
    bool granReady_ = false, forgetReady_ = false;
    void loadParts(Model* out, const char* const* names, int n, bool& ready);
    void unloadParts(Model* parts, int n, bool& ready);
    void modelPart(Model& m, Matrix t, Color tint, bool outline = true);
    void drawGranModel(Game& g);
    void drawForgetting(Game& g);   // kind -1: picked by the seed
    void cottage(float x, float z);
    void house(float x, float z, int seed);
};
