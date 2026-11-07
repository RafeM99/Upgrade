#include "core/Paths.h"

#include "raylib.h"

std::string assetPath(const std::string& relative)
{
#ifdef UPGRADE_ASSETS_DIR
    if (DirectoryExists(UPGRADE_ASSETS_DIR)) return std::string(UPGRADE_ASSETS_DIR) + relative;
#endif
    return std::string(GetApplicationDirectory()) + "assets/" + relative;
}
