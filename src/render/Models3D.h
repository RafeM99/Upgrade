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
    void tree(float x, float z, int seed, int kind = -1);   // kind -1: picked by the seed
    void cottage(float x, float z);
    void house(float x, float z, int seed);
};
