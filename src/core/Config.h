// Config.h: every tuning number lives in assets/config.ini (one place for designers).
// Keys are "section.key". Missing keys fall back to the default passed in code.
#pragma once
#include <string>
#include <unordered_map>

class Config {
public:
    bool load(const std::string& path);
    float f(const std::string& key, float def) const;
    int i(const std::string& key, int def) const;
    bool b(const std::string& key, bool def) const;
    std::string s(const std::string& key, const std::string& def) const;
    void set(const std::string& key, const std::string& value) { values_[key] = value; }

private:
    std::unordered_map<std::string, std::string> values_;
};

Config& config();   // the one shared config
