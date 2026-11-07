// assetgen: generates all the art (PNG sprite sheets) and all the audio (WAV)
// for Upgrade from code. Usage:  assetgen [assets-dir] [--art-only|--audio-only|--sfx-only|--music-only]
#include "Gen.h"
#include "raylib.h"
#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    SetTraceLogLevel(LOG_WARNING);
    std::string dir = "assets";
    bool art = true, sfx = true, music = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--art-only") == 0) sfx = music = false;
        else if (std::strcmp(argv[i], "--audio-only") == 0) art = false;
        else if (std::strcmp(argv[i], "--sfx-only") == 0) art = music = false;
        else if (std::strcmp(argv[i], "--music-only") == 0) art = sfx = false;
        else dir = argv[i];
    }
    int n = 0;
    if (art) n += generateSprites(dir);
    if (sfx) n += generateSounds(dir);
    if (music) n += generateMusic(dir);
    std::printf("assetgen: wrote %d files under %s\n", n, dir.c_str());
    return 0;
}
