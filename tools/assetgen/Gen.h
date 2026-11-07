// Gen.h: shared bits of the asset generator.
#pragma once
#include <string>

// Writes every sprite sheet, background, wall texture and ending picture
// under <assetsDir>/art. Returns the number of files written.
int generateSprites(const std::string& assetsDir);

// Writes every sound effect and music loop under <assetsDir>/audio.
int generateSounds(const std::string& assetsDir);
