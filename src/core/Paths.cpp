// Owner: Systems (Camilla's Claude).
#include "core/Paths.h"
#include "raylib.h"

namespace {
std::string base;
}

namespace Paths {
void init() {
#ifdef UPGRADE_SOURCE_ASSETS
    if (DirectoryExists(UPGRADE_SOURCE_ASSETS)) {
        base = UPGRADE_SOURCE_ASSETS;
        return;
    }
#endif
    base = std::string(GetApplicationDirectory()) + "assets/";
}

std::string asset(const std::string& relative) {
    return base + relative;
}
}
