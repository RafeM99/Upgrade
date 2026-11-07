// Renderer.h: draws the world into a render texture per graphics stage (low resolution,
// true pixel art for stages 0-4), then upscales it through the stage's post shader:
//   0 1-bit: 2 colours, dither, LCD grid      1 4-shade: four greys, dot-matrix LCD with pixel shadows
//   2 8-bit: 16-colour palette, CRT lines     3 16-bit: 4096 colours, soft scanlines
//   4 32-bit: rich colour, backlit screen glow 5 2.5D: full resolution, the camera swings
//   6 3D: modelled, toon-outlined characters and props (Models3D), a gentle 3/4 perspective
// During an era upgrade both eras are drawn: the old one glitches and goes blocky, then a
// bright scanline sweeps across the screen and the new era appears behind it.
#pragma once
#include "raylib.h"
#include "render/ArtSpec.h"
#include "render/Models3D.h"

class Game;

class Renderer {
public:
    void init();
    void shutdown();
    void drawWorld(Game& g);                    // world into the era render texture(s)
    void present(Game& g, float flash, float desat);   // render texture(s) to the screen
    void drawTitleBackground(Game& g, float t); // animated era-cycling backdrop
    Rectangle screenRect() const;               // where the game image sits in the window (UI space)
    Rectangle imageRect() const;                // the world image itself: screenRect, or whole-number scaled (setting)

    // Transition timeline, shared with Game so the sweep and the era switch line up.
    static constexpr float kSwitchAt = 0.35f;   // seconds into the transition: era changes here
    static constexpr float kSweepTime = 1.1f;   // the scanline crosses the screen in this long
    static float sweepProgress(const Game& g);  // 0..1, or -1 when no sweep is on screen

private:
    RenderTexture2D rt_[art::ERA_COUNT]{};
    int rtW_[art::ERA_COUNT] = {}, rtH_[art::ERA_COUNT] = {};
    Models3D models_;
    int locShades_ = -1;
    Shader post_{}, world_{}, haze_{};
    int locHazeCol_ = -1, locHazeAmt_ = -1;
    int locEra_ = -1, locTime_ = -1, locSize_ = -1, locOut_ = -1, locFlash_ = -1, locDesat_ = -1, locPal_ = -1;
    int locMosaic_ = -1, locGlitch_ = -1, locBeat_ = -1;
    int locRetro_ = -1, locBloom_ = -1, locBright_ = -1;
    bool inWorld_ = false;                       // drawing the game (not the title): integer scaling applies
    int renderEra_ = 0;
    int oldEra_ = -1;                            // also drawn this frame (transition), or -1
    void ensureTargets();
    void drawEra(Game& g, int era, float swing);
    void blitEra(Game& g, int era, Rectangle dst, float flash, float desat, float mosaic, float glitch);
    void drawSweep(Game& g, Rectangle dst, float p);
    void drawBackground(Game& g, int era, float camX, float pxPerTile, int w, int h, float alpha, int layers = art::BG_LAYERS);
    // 2.5D and 3D: the far backdrop layers as cut-outs standing in the scene, and the lawns
    void drawBackdrop3D(Game& g, int era, const Camera3D& cam);
    void drawLawns25D(Game& g);
};
