#pragma once
// One look per console era, all from the same 3D scene:
//   NES       256x144 render, point filtered, flat colours, scanlines
//   SNES      384x216 render, point filtered, grass tops, parallax hills
//   N64       640x360 render, blurry bilinear filter, perspective, low-poly mountains, haze
//   GameCube  full resolution, angled camera, dusk sky, shaded block tops, vignette
//   Wii       full resolution, bright pastel world, soft glow and sparkles, swaying camera
#include "raylib.h"

class Game;

struct EraLook {
    Color skyTop, skyBottom;
    Color ground, groundTop, brick, block, used, pipe, coin;
    Color enemy, flyer, brute, spike;
    Color p1Cap, p2Cap, overalls, skin;
    Color hill, hillFar, cloud;
    Color hudText, hudShadow, hudAccent;
};

class Renderer {
public:
    void init();
    void draw(const Game& game);
    void shutdown();
    static const EraLook& look(int era);

private:
    void drawScene(const Game& game, int w, int h);
    void drawBackdrop2D(const Game& game, int w, int h);
    void drawBackdrop3D(const Game& game);
    void drawWorld(const Game& game);
    void drawOverlayText(const Game& game, int w, int h);
    void postEffects(const Game& game, int w, int h);

    RenderTexture2D target{};
    int targetW = 0;
    int targetH = 0;
};
