// MusicSpec.h (akshat): tempos shared by the sound generator (tools/assetgen) and the game,
// so the game can pulse the screen in time with the music.
#pragma once

namespace musicspec {
// Main loops: 16 bars. Bars 0-7 build up, bars 8-15 are the drop.
constexpr float BPM[4] = {110.0f, 140.0f, 150.0f, 128.0f};
constexpr int BARS = 16;
// Boss loops: 8 bars, always at full intensity.
constexpr float BOSS_BPM[4] = {120.0f, 150.0f, 160.0f, 140.0f};
constexpr int BOSS_BARS = 8;
}  // namespace musicspec
