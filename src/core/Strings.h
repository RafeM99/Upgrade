// Strings.h: every player-facing line lives in assets/strings.txt (key = text).
#pragma once
#include <string>
#include <unordered_map>

class Strings {
public:
    bool load(const std::string& path);
    const std::string& get(const std::string& key) const;   // returns the key itself if missing

private:
    std::unordered_map<std::string, std::string> text_;
    mutable std::unordered_map<std::string, std::string> missing_;
};

Strings& strings();
inline const char* T(const std::string& key) { return strings().get(key).c_str(); }
