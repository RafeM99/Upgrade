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
    void drawScenery(Game& g);                      // ground, fence, trees, houses, lamps
    void drawPlayer(Game& g, int p);
    void drawEnemy(Game& g, const Enemy& e);
    void drawBoss(Game& g);
    void drawFragment(float x, float y, float t, float alpha);

private:
    Shader sh_{};
    Model cube_{}, sphere_{}, cyl_{}, torus_{};
    int locView_ = -1, locEmissive_ = -1;
    int locLightPos_ = -1, locLightCol_ = -1, locLightRad_ = -1;
    bool ready_ = false;
    void part(Model& m, Matrix t, Color c, float emissive = 0.0f);
};
