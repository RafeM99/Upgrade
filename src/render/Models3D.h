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
    bool ready_ = false;
    // Draws one lit part, then (unless outline is false, or the part is tiny or see-through)
    // the same mesh again as a dark inverted hull: the toon outline.
    void part(Model& m, Matrix t, Color c, float emissive = 0.0f, bool outline = true);
    void tree(float x, float z, int seed, int kind = -1);
    // Gran and the bat boss modelled in Blender (tools/blender/make_models.py), one .glb per
    // moving part, put together and animated here. Falls back to the primitives if missing.
    enum { G_BODY, G_HEAD, G_UPPERARM, G_FOREARM, G_THIGH, G_SHIN, G_COUNT };
    enum { B_BODY, B_WING_IN_L, B_WING_OUT_L, B_WING_IN_R, B_WING_OUT_R, B_COUNT };
    Model gran_[G_COUNT]{}, bat_[B_COUNT]{};
    bool granReady_ = false, batReady_ = false;
    void loadParts(Model* out, const char* const* names, int n, bool& ready);
    void unloadParts(Model* parts, int n, bool& ready);
    void modelPart(Model& m, Matrix t, Color tint, bool outline = true);
    void drawGranModel(Game& g);
    void drawBatBoss(Game& g);   // kind -1: picked by the seed
    void cottage(float x, float z);
    void house(float x, float z, int seed);
};
