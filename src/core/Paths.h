#pragma once

#include <string>

// Full path to a file under assets/, e.g. assetPath("chunks/start.txt").
// Uses the source assets folder (UPGRADE_ASSETS_DIR) when it exists,
// else the assets folder copied next to the executable.
std::string assetPath(const std::string& relative);
