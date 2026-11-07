#pragma once
// Owner: Art and rendering (area 3). Starter code from the skeleton: replace freely.
// Placeholder look: coloured cubes, one palette per era.
// To do (art and rendering): low-res RenderTexture2D per era, palette shader
// (assets/shaders), parallax backgrounds, the 2.5D camera swing and lighting.
#include "raylib.h"

class Game;

struct EraPalette {
    Color background, solid, cracked, wall, p1, p2, enemy, fragment, forgetting;
};

class Renderer {
public:
    void init();
    void draw(const Game& game);
    void shutdown();
    static const EraPalette& palette(int era);
};
