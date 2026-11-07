// ArtSpec.h: the layout of every generated sprite sheet.
// Shared by the asset generator (tools/assetgen) and the game, so both agree
// on frame sizes and frame order. Change a sheet here and re-run assetgen.
#pragma once

namespace art {

// The graphics stages ("eras" in the code), oldest to newest. Each one has its own sprites,
// tiles, palette, post shader look and level chunks:
//   0 1-bit        two colours, 8 px tiles
//   1 4-shade      a handheld LCD: exactly four greys, 16 px tiles
//   2 8-bit        first colour: a 16-colour palette, 3 colours per sprite part, 16 px tiles
//   3 16-bit       shaded sprites, a 32-colour palette, 20 px tiles
//   4 32-bit       late, detailed 2D pixel art: hue-shifted shading, coloured outlines, 32 px tiles
//   5 2.5D         flat sprites in a lit 3D world, the camera swings (64 px textures)
//   6 3D           full 3D: modelled characters and props, deep perspective, bloom
constexpr int ERA_COUNT = 7;
constexpr int LAST_ERA = ERA_COUNT - 1;

// Pixels per tile in each stage.
// The pixel-art stages render into a native buffer TILE_PX * VIEW_TILES_H pixels tall
// (88, 176, 176, 220, 352) that is scaled up by a whole number (8x, 4x, 4x, 3x, 2x at 720p).
constexpr int TILE_PX[ERA_COUNT] = {8, 16, 16, 20, 32, 64, 64};

// Which older drawing style the sprite generator starts from for each stage
// (0 = 1-bit, 1 = 8-bit, 2 = 16-bit, 3 = smooth), before the stage's own rules.
constexpr int STYLE[ERA_COUNT] = {0, 1, 1, 2, 2, 3, 3};

// Pixel-art stages render into a small target (one sprite pixel = one target pixel);
// the 2.5D and 3D stages render at window size.
constexpr bool lowRes(int era) { return era <= 4; }
constexpr bool smooth(int era) { return era >= 5; }

// Camera swing at each stage: 0 = flat side view, 1 = 2.5D, 2 = full 3D.
constexpr float SWING[ERA_COUNT] = {0, 0, 0, 0, 0, 1, 2};

// Sound set (assets/audio/eraN) used by each stage.
constexpr int AUDIO_SET[ERA_COUNT] = {0, 0, 1, 2, 2, 3, 3};

// Difficulty tier (the old four-era scale) that enemies and the boss use at each stage.
constexpr int TIER[ERA_COUNT] = {0, 1, 1, 2, 2, 3, 3};

// The world view is VIEW_TILES_H tiles tall. The low-res render target is
// TILE_PX * VIEW_TILES_H pixels tall, so one sprite pixel is one target pixel.
constexpr float VIEW_TILES_H = 11.0f;   // Gran and Sam (1.5 tiles) are about 1/7 of the screen

// Background layers: each is two screens wide and tiles horizontally.
constexpr int BG_H[ERA_COUNT] = {88, 176, 176, 220, 352, 360, 360};   // = the native buffer height
inline int bgW(int era) { return BG_H[era] * 32 / 9; }
constexpr int BG_LAYERS = 4;   // 0 sky (opaque), 1 far, 2 mid, 3 near

// ---- Player sheet: player_p1.png / player_p2.png, frames 1 x 1.5 tiles in one row
enum PlayerFrame {
    PF_IDLE0, PF_IDLE1,
    PF_RUN0, PF_RUN1, PF_RUN2, PF_RUN3,
    PF_JUMP, PF_FALL,
    PF_DASH,
    PF_SPECIAL,   // P1: glide with umbrella. P2: time-slow pulse pose.
    PF_POUND,
    PF_WALL,
    PF_ATTACK,
    PF_COUNT
};
constexpr float PLAYER_FW = 1.0f, PLAYER_FH = 1.5f;   // frame size in tiles

// ---- Enemy sheets (frame sizes in tiles)
enum BlobFrame  { BF_WALK0, BF_WALK1, BF_WALK2, BF_WALK3, BF_SQUASH, BF_COUNT };
enum FlyerFrame { FF_FLAP0, FF_FLAP1, FF_FLAP2, FF_FLAP3, FF_HURT, FF_COUNT };
enum BruteFrame { RF_WALK0, RF_WALK1, RF_WALK2, RF_WALK3, RF_HURT, RF_CHARGE, RF_COUNT };
enum BossFrame  { XF_FLOAT0, XF_FLOAT1, XF_HURT, XF_SLAM, XF_COUNT };
constexpr float BLOB_FS = 1.0f, FLYER_FS = 1.0f, BRUTE_FS = 2.0f, BOSS_FS = 3.0f;

// ---- Tiles and pickups: tiles.png, 1 x 1 tile frames in one row
enum TileFrame {
    T_GROUND_TOP, T_GROUND, T_BLOCK, T_CRACKED, T_P1, T_P2, T_WALL,
    T_FRAG0, T_FRAG1,
    T_PU_SHIELD, T_PU_SLOW, T_PU_MAGNET, T_PU_PHOTO, T_PU_SPEED,
    T_COUNT
};

// ---- Forgetting Wall texture: wall.png, 2 x 4 tiles, tiles vertically.
constexpr float WALL_TEX_W = 2.0f, WALL_TEX_H = 4.0f;

// ---- Four warm greys of the 4-shade stage (dark to light). Our own; the shader uses them too.
constexpr unsigned SHADES4[4] = {0x1f1d24, 0x5b5662, 0xa8a297, 0xe8e2d2};

// ---- Ending scene pictures: ending_0..3.png (in assets/art/), 640 x 360.
constexpr int ENDING_W = 640, ENDING_H = 360, ENDING_COUNT = 4;

}  // namespace art
