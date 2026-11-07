#include "core/Config.h"
#include <cstdlib>
#include <fstream>

namespace {
std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
}  // namespace

Config& config()
{
    static Config c;
    return c;
}

bool Config::load(const std::string& path)
{
    std::ifstream in(path);
    if (!in) return false;
    std::string line, section;
    while (std::getline(in, line)) {
        size_t hash = line.find_first_of(";#");
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = trim(line);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') { section = trim(line.substr(1, line.size() - 2)); continue; }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        values_[section.empty() ? key : section + "." + key] = trim(line.substr(eq + 1));
    }
    return true;
}

float Config::f(const std::string& key, float def) const
{
    auto it = values_.find(key);
    return it == values_.end() ? def : (float)std::atof(it->second.c_str());
}
int Config::i(const std::string& key, int def) const
{
    auto it = values_.find(key);
    return it == values_.end() ? def : std::atoi(it->second.c_str());
}
bool Config::b(const std::string& key, bool def) const
{
    auto it = values_.find(key);
    if (it == values_.end()) return def;
    const std::string& v = it->second;
    return v == "1" || v == "true" || v == "yes" || v == "on";
}
std::string Config::s(const std::string& key, const std::string& def) const
{
    auto it = values_.find(key);
    return it == values_.end() ? def : it->second;
}
