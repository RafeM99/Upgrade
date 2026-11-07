#pragma once
// Owner: Systems (Camilla's Claude).
// Reads assets/config.ini. Every tuning number lives there, one [section] per area.
//   float speed = Config::getFloat("player", "run_speed", 7.0f);
// The last argument is the fallback when the key is missing.
#include <map>
#include <string>

namespace Config {
bool load(const std::string& path);
float getFloat(const std::string& section, const std::string& key, float fallback);
int getInt(const std::string& section, const std::string& key, int fallback);
bool getBool(const std::string& section, const std::string& key, bool fallback);
std::string getString(const std::string& section, const std::string& key, const std::string& fallback);
}

// Shared reader for key = value files. Lines starting with ; or # are comments.
// With sections on, keys are stored as "section.key".
bool loadKeyValueFile(const std::string& path, std::map<std::string, std::string>& out, bool sections);
