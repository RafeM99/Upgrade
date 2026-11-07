// ArtSpec.h: the layout of every generated sprite sheet.
// Shared by the asset generator (tools/assetgen) and the game, so both agree
// on frame sizes and frame order. Change a sheet here and re-run assetgen.
#pragma once

namespace art {

constexpr int ERA_COUNT = 5;
constexpr int LAST_ERA = ERA_COUNT - 1;   // the ending comes after this era's bar is full
constexpr int ERA_25D = 3;                // first era drawn at window resolution with the 3D camera
constexpr int ERA_MODERN = 4;             // "modern day": HD painted look, soft light, bloom, depth of field

// Eras from 2.5D on render at window size with smooth (mipmapped) art.
inline bool isHiRes(int era) { return era >= ERA_25D; }

// Pixels per tile in each era: 1-bit, 8-bit, 16-bit, 2.5D, modern day.
// Era 4 sheets use the same sizes and layouts as era 3.
constexpr int TILE_PX[ERA_COUNT] = {8, 16, 32, 64, 64};

// The world view is VIEW_TILES_H tiles tall. The low-res render target for
// eras 0-2 is TILE_PX * VIEW_TILES_H pixels tall, so one sprite pixel is one
// screen pixel of the render target (true pixel art). Eras 3 and 4 render at window size.
constexpr float VIEW_TILES_H = 14.0f;

// Background layers: each is two screens wide and tiles horizontally.
constexpr int BG_H[ERA_COUNT] = {112, 224, 448, 360, 360};
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

// ---- Ending scene pictures: ending_0..3.png (in assets/art/), 640 x 360.
constexpr int ENDING_W = 640, ENDING_H = 360, ENDING_COUNT = 4;

}  // namespace art
