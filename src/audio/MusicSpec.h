// MusicSpec.h (akshat): tempos and lengths shared by the music generator (tools/assetgen/MusicGen.cpp)
// and the game, so the game can pulse the screen in time with the music. Every loop is an exact
// whole number of 4/4 bars from time 0, so beat = timePlayed * BPM / 60 stays on the grid.
// Indices are sound sets (eraN folders); stages map to them via art::AUDIO_SET.
#pragma once

namespace musicspec {
// Main loops. Set 0: intro 4, verse 8, build 4, chorus 8, breakdown 4 = 28 bars.
// Sets 1-3: intro 4, verse 8, pre-chorus 4, drop 8, drop B 8, breakdown 4 = 36 bars.
constexpr float BPM[4] = {110.0f, 140.0f, 150.0f, 128.0f};
constexpr int BARS[4] = {28, 36, 36, 36};
// Boss loops, 24 bars each: set 1 The School Bell (stage 2), set 2 The Last Train (stage 4),
// set 3 The Hourglass (stage 6, final). Set 0 has no boss track; its entries mirror the main loop.
constexpr bool HAS_BOSS[4] = {false, true, true, true};
constexpr float BOSS_BPM[4] = {110.0f, 160.0f, 158.0f, 140.0f};
constexpr int BOSS_BARS[4] = {0, 24, 24, 24};
}  // namespace musicspec
