// Renderer.h: draws the world into a low-resolution render texture per era
// (true pixel art for eras 0-2), then upscales it through the era's palette shader:
//   era 0 = 2 colours with dither, era 1 = 16-colour palette, era 2 = wider palette
//   with scanlines, era 3 = full resolution with bloom and vignette.
#pragma once
#include "raylib.h"

class Game;

class Renderer {
public:
    void init();
    void shutdown();
    void drawWorld(Game& g);                    // world into the era render texture
    void present(Game& g, float flash, float desat);   // render texture to the screen
    void drawTitleBackground(Game& g, float t); // animated era-cycling backdrop
    Rectangle screenRect() const;               // where the game image sits in the window

private:
    RenderTexture2D rt_[4]{};
    int rtW_[4] = {}, rtH_[4] = {};
    Shader post_{}, world_{};
    int locEra_ = -1, locTime_ = -1, locSize_ = -1, locFlash_ = -1, locDesat_ = -1, locPal_ = -1;
    int renderEra_ = 0;
    void ensureTargets();
    void drawBackground(Game& g, int era, float camX, float pxPerTile, int w, int h, float alpha);
};
