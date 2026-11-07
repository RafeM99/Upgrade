// Gen.h: shared bits of the asset generator.
#pragma once
#include <string>
int generateEra4Sounds(const std::string& assetsDir);   // Era4Sound.cpp: era 4 sfx + music under <assetsDir>/audio/era4

// Writes every sprite sheet, background, wall texture and ending picture
// under <assetsDir>/art. Returns the number of files written.
int generateSprites(const std::string& assetsDir);

// Writes every sound effect and music loop under <assetsDir>/audio.
int generateSounds(const std::string& assetsDir);
