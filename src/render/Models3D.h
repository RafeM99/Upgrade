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
    int locView_ = -1, locEmissive_ = -1, locRim_ = -1;
    int locLightPos_ = -1, locLightCol_ = -1, locLightRad_ = -1;
    int locSunDir_ = -1, locSunCol_ = -1, locSkyCol_ = -1, locGroundCol_ = -1;
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
    void drawGardenFront(Game& g, float l, float r);   // 3D stage: beds, low hedge, path, puddle
    bool ready_ = false;
    Vector3 camPos_{0, 0, 50};   // this frame's camera, set by begin()
    bool rims_ = true;   // the 2.5D meadow draws its props without the rim light
    // Draws one lit part. There are no outlines: unless rim is false (flat slabs, tiny details)
    // a rounded part gets a thin lighter rim along its silhouette so it reads against the next.
    void part(Model& m, Matrix t, Color c, float emissive = 0.0f, bool rim = true);
    void tree(float x, float z, int seed, int kind = -1);
    // Gran, Sam, the grandfather clock and the Forgetting (the boss) are modelled in Blender
    // (tools/blender/make_models.py), one .glb per moving part, put together and animated here.
    // Each falls back to the primitives if its files are missing.
    enum { P_BODY, P_HEAD, P_UPPERARM, P_FOREARM, P_THIGH, P_SHIN, P_COUNT };
    enum { C_CASE, C_PENDULUM, C_HOUR, C_MINUTE, C_FOOT_L, C_FOOT_R, C_COUNT };
    enum { F_BODY, F_SEG, F_TIP, F_PUPIL, F_COUNT };
    Model person_[2][P_COUNT]{}, clock_[C_COUNT]{}, forget_[F_COUNT]{};
    bool personReady_[2] = {false, false}, clockReady_ = false, forgetReady_ = false;
    void loadParts(Model* out, const char* const* names, int n, bool& ready);
    void unloadParts(Model* parts, int n, bool& ready);
    void modelPart(Model& m, Matrix t, Color tint, bool rim = true);
    void drawPersonModel(Game& g, int p);   // Gran (0) or Sam (1)
    void drawClockModel(Game& g, const Enemy& e);
    void drawForgetting(Game& g);
    void cottage(float x, float z);
    void house(float x, float z, int seed);
};
