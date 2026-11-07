// Owner: Systems (Camilla's Claude).
#include "core/Config.h"
#include <cstdlib>
#include <fstream>
#include "raylib.h"

namespace {
std::map<std::string, std::string> values;

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

const std::string* find(const std::string& section, const std::string& key) {
    auto it = values.find(section + "." + key);
    return it == values.end() ? nullptr : &it->second;
}
}

bool loadKeyValueFile(const std::string& path, std::map<std::string, std::string>& out, bool sections) {
    std::ifstream file(path);
    if (!file) {
        TraceLog(LOG_WARNING, "UPGRADE: could not open %s", path.c_str());
        return false;
    }
    std::string line, section;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (sections && line[0] == '[') {
            size_t close = line.find(']');
            section = trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        out[sections ? section + "." + key : key] = value;
    }
    return true;
}

namespace Config {
bool load(const std::string& path) {
    values.clear();
    bool ok = loadKeyValueFile(path, values, true);
    TraceLog(LOG_INFO, "UPGRADE: config loaded %d values from %s", (int)values.size(), path.c_str());
    return ok;
}

float getFloat(const std::string& section, const std::string& key, float fallback) {
    const std::string* v = find(section, key);
    return v ? (float)std::atof(v->c_str()) : fallback;
}

int getInt(const std::string& section, const std::string& key, int fallback) {
    const std::string* v = find(section, key);
    return v ? std::atoi(v->c_str()) : fallback;
}

bool getBool(const std::string& section, const std::string& key, bool fallback) {
    const std::string* v = find(section, key);
    if (!v) return fallback;
    return *v == "1" || *v == "true" || *v == "yes" || *v == "on";
}

std::string getString(const std::string& section, const std::string& key, const std::string& fallback) {
    const std::string* v = find(section, key);
    return v ? *v : fallback;
}
}
