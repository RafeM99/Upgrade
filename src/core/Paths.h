#pragma once
// Owner: Systems (Camilla's Claude).
// Finds the assets folder: the repo's assets/ while developing, or assets/ next to the
// executable in a shipped build.  Paths::asset("chunks")  ->  ".../assets/chunks"
#include <string>

namespace Paths {
void init();
std::string asset(const std::string& relative);
}
