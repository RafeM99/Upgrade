// Owner: Systems (Camilla's Claude).
#include "core/Strings.h"
#include <map>
#include "core/Config.h"
#include "raylib.h"

namespace {
std::map<std::string, std::string> texts;
}

namespace Strings {
bool load(const std::string& path) {
    texts.clear();
    bool ok = loadKeyValueFile(path, texts, false);
    TraceLog(LOG_INFO, "UPGRADE: strings loaded %d lines from %s", (int)texts.size(), path.c_str());
    return ok;
}

std::string get(const std::string& key) {
    auto it = texts.find(key);
    return it == texts.end() ? key : it->second;
}
}
