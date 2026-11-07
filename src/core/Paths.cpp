#include "core/Paths.h"
#include "core/Events.h"
#include "raylib.h"

Events& events()
{
    static Events e;
    return e;
}

const std::string& assetsDir()
{
    static std::string dir;
    if (!dir.empty()) return dir;
    std::string app = GetApplicationDirectory();
    const std::string tries[] = {"assets", app + "assets", app + "../assets", app + "../../assets", "../assets"};
    for (const auto& t : tries)
        if (DirectoryExists(t.c_str()) && FileExists((t + "/config.ini").c_str())) { dir = t; return dir; }
    dir = "assets";
    return dir;
}

std::string assetPath(const std::string& relative) { return assetsDir() + "/" + relative; }
