// Renderer.h: draws the world into a low-resolution render texture per era
// (true pixel art for eras 0-2), then upscales it through the era's palette shader:
//   era 0 = 2 colours with dither and an LCD grid, era 1 = 16-colour palette with CRT
//   scanlines, era 2 = wider palette with soft scanlines, era 3 = full resolution with bloom,
//   era 4 = "modern day": the 2.5D scene with per-pixel light, rim light, depth of field and
//   filmic bloom (all of it in render/ModernLook and shaders/modern_*).
// During an era upgrade both eras are drawn: the old one glitches and goes blocky, then a
// bright scanline sweeps across the screen and the new era appears behind it.
#pragma once
#include "raylib.h"
#include "render/ArtSpec.h"

class Game;

class Renderer {
public:
    void init();
    void shutdown();
    void drawWorld(Game& g);                    // world into the era render texture(s)
    void present(Game& g, float flash, float desat);   // render texture(s) to the screen
    void drawTitleBackground(Game& g, float t); // animated era-cycling backdrop
    Rectangle screenRect() const;               // where the game image sits in the window

    // Transition timeline, shared with Game so the sweep and the era switch line up.
    static constexpr float kSwitchAt = 0.8f;    // seconds into the transition: era changes here
    static constexpr float kSweepTime = 1.5f;   // the scanline crosses the screen in this long
    static float sweepProgress(const Game& g);  // 0..1, or -1 when no sweep is on screen

private:
    RenderTexture2D rt_[art::ERA_COUNT]{};
    int rtW_[art::ERA_COUNT] = {}, rtH_[art::ERA_COUNT] = {};
    Shader post_{}, world_{};
    int locEra_ = -1, locTime_ = -1, locSize_ = -1, locOut_ = -1, locFlash_ = -1, locDesat_ = -1, locPal_ = -1;
    int locMosaic_ = -1, locGlitch_ = -1;
    int renderEra_ = 0;
    int oldEra_ = -1;                            // also drawn this frame (transition), or -1
    void ensureTargets();
    void drawEra(Game& g, int era, float swing);
    void blitEra(Game& g, int era, Rectangle dst, float flash, float desat, float mosaic, float glitch);
    void drawSweep(Game& g, Rectangle dst, float p);
    void drawBackground(Game& g, int era, float camX, float pxPerTile, int w, int h, float alpha);
};
