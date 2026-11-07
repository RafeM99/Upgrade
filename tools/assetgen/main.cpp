// assetgen: generates all the art (PNG sprite sheets) and all the audio (WAV)
// for Upgrade from code. Usage:  assetgen [assets-dir] [--art-only|--audio-only]
#include "Gen.h"
#include "raylib.h"
#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    SetTraceLogLevel(LOG_WARNING);
    std::string dir = "assets";
    bool art = true, audio = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--art-only") == 0) audio = false;
        else if (std::strcmp(argv[i], "--audio-only") == 0) art = false;
        else dir = argv[i];
    }
    int n = 0;
    if (art) n += generateSprites(dir);
    if (audio) n += generateSounds(dir);
    std::printf("assetgen: wrote %d files under %s\n", n, dir.c_str());
    return 0;
}
