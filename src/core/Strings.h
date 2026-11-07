#pragma once
// Owner: Systems (Camilla's Claude). The text itself lives in assets/strings.txt (owner: area 4).
// Every player-facing line comes from there:  Strings::get("memory_restored")
// A missing key shows the key itself on screen, so gaps are easy to spot.
#include <string>

namespace Strings {
bool load(const std::string& path);
std::string get(const std::string& key);
}
