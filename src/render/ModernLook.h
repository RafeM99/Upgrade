// ModernLook.h: the extra passes of era 4, "modern day" (after 2.5D).
// The world is the same 3D scene as the 2.5D era; on top of it this adds the look of a
// current indie game:
//   - one warm key light from the upper left (late-afternoon sun) and a cool sky ambient,
//     lit per pixel with face normals, plus soft point lights (players, boss, fragments)
//   - a warm rim light on every sprite edge that faces the sun, so silhouettes read clearly
//   - depth of field: the far background layers are blurred, the middle one a little,
//     with aerial haze taken from the sky's own colour so it always matches the art
//   - soft light shafts, drifting dust and out-of-focus bokeh in front of the camera
//   - modern_post.fs: wide bloom, filmic tone mapping, gentle split-tone grading
// Everything era 4 needs lives here and in assets/shaders/modern_*.{vs,fs}, so the
// other eras' code stays as it was.
#pragma once
#include "raylib.h"

class Game;
struct EraArt;

namespace modern {

void init();
void shutdown();

// Before the world target is bound: renders the blurred background layers.
void prepareBackground(Game& g, const EraArt& A, float camX, float pxPerTile, int w, int h);
// Inside the world target: the background with depth of field, haze and light shafts.
void drawBackground(Game& g, const EraArt& A, float camX, float pxPerTile, int w, int h);

// Around the 3D world pass: per-pixel lighting and rim light.
void beginWorld(Game& g, const Camera3D& cam, float amount);
void endWorld();

// After the 3D pass, still inside the world target: dust motes and bokeh.
void drawForeground(Game& g, int w, int h, float amount);

// Draws the world target to the screen through modern_post.fs.
void blit(Game& g, const Texture2D& tex, Rectangle src, Rectangle dst, float flash, float desat, float mosaic, float glitch);

}  // namespace modern
