// Gen.h: shared bits of the asset generator.
#pragma once
#include <string>

// Writes every sprite sheet, background, wall texture and ending picture
// under <assetsDir>/art. Returns the number of files written.
int generateSprites(const std::string& assetsDir);

// Writes every sound effect (WAV) under <assetsDir>/audio/eraN.
int generateSounds(const std::string& assetsDir);

// Writes every music loop (music.qoa per era, music_boss.qoa for eras 1 and 3) under <assetsDir>/audio.
int generateMusic(const std::string& assetsDir);
