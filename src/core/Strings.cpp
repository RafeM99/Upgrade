#include "core/Strings.h"
#include <fstream>

Strings& strings()
{
    static Strings s;
    return s;
}

bool Strings::load(const std::string& path)
{
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
        // "\n" in the file means a line break
        for (size_t p = val.find("\\n"); p != std::string::npos; p = val.find("\\n", p)) val.replace(p, 2, "\n");
        text_[key] = val;
    }
    return true;
}

const std::string& Strings::get(const std::string& key) const
{
    auto it = text_.find(key);
    if (it != text_.end()) return it->second;
    return missing_.emplace(key, key).first->second;
}
