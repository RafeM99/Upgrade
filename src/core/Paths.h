// Paths.h: finds the assets folder whether you run from the repo, the build folder or a zip.
#pragma once
#include <string>

const std::string& assetsDir();
std::string assetPath(const std::string& relative);
