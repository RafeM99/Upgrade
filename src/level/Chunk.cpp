// Owner: Systems (Camilla's Claude).
#include "level/Chunk.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include "raylib.h"

namespace {
bool isMarker(char c) { return c == 'S' || c == 'G' || c == 'B' || c == 'F'; }

bool isHeader(const std::string& line) {
    if (line.empty() || line[0] != '#') return false;
    for (char c : line)
        if (std::islower((unsigned char)c)) return true;   // tile rows never hold lower-case letters
    return false;
}

void readHeader(const std::string& line, Chunk& chunk) {
    std::istringstream words(line.substr(1));
    std::string word;
    while (words >> word) {
        size_t eq = word.find('=');
        if (eq == std::string::npos) continue;
        std::string key = word.substr(0, eq);
        std::string value = word.substr(eq + 1);
        if (key == "abilities" || key == "required_abilities") {
            std::stringstream list(value);
            std::string item;
            while (std::getline(list, item, ','))
                if (!item.empty() && item != "none") chunk.abilities.push_back(item);
        } else if (key == "min_era") {
            chunk.minEra = std::atoi(value.c_str());
        } else if (key == "difficulty") {
            chunk.difficulty = std::atoi(value.c_str());
        } else if (key == "coop" || key == "coop_required") {
            chunk.coop = value == "true" || value == "1" || value == "yes";
        }
    }
}
}

char Chunk::at(int col, int y) const {
    int row = height - 1 - y;
    if (col < 0 || col >= width || row < 0 || row >= height) return '.';
    return rows[row][col];
}

bool Chunk::uses(const std::string& ability) const {
    return std::find(abilities.begin(), abilities.end(), ability) != abilities.end();
}

bool loadChunkFile(const std::string& path, Chunk& out) {
    std::ifstream file(path);
    if (!file) return false;
    out = Chunk();
    out.name = GetFileNameWithoutExt(path.c_str());

    std::string line;
    bool first = true;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();   // Windows line endings
        if (first && isHeader(line)) {
            readHeader(line, out);
            first = false;
            continue;
        }
        first = false;
        if (line.empty()) continue;
        out.rows.push_back(line);
    }

    out.height = (int)out.rows.size();
    out.width = 0;
    for (const std::string& r : out.rows) out.width = std::max(out.width, (int)r.size());
    for (std::string& r : out.rows) r.resize(out.width, '.');

    for (int row = 0; row < out.height; ++row) {
        for (int col = 0; col < out.width; ++col) {
            char& c = out.rows[row][col];
            if (isMarker(c)) {
                out.markers.push_back({c, (float)col, (float)(out.height - 1 - row)});
                c = '.';
            }
        }
    }
    return out.width > 0 && out.height > 0;
}

std::vector<Chunk> loadChunkFolder(const std::string& folder) {
    std::vector<Chunk> chunks;
    if (!DirectoryExists(folder.c_str())) {
        TraceLog(LOG_WARNING, "UPGRADE: chunk folder missing: %s", folder.c_str());
        return chunks;
    }
    FilePathList files = LoadDirectoryFilesEx(folder.c_str(), ".txt", false);
    std::vector<std::string> paths;
    for (unsigned int i = 0; i < files.count; ++i) paths.push_back(files.paths[i]);
    UnloadDirectoryFiles(files);
    std::sort(paths.begin(), paths.end());

    for (const std::string& p : paths) {
        Chunk c;
        if (loadChunkFile(p, c)) {
            chunks.push_back(c);
            TraceLog(LOG_INFO, "UPGRADE: chunk %s %dx%d min_era=%d abilities=%d markers=%d",
                     c.name.c_str(), c.width, c.height, c.minEra, (int)c.abilities.size(), (int)c.markers.size());
        } else {
            TraceLog(LOG_WARNING, "UPGRADE: could not read chunk %s", p.c_str());
        }
    }
    return chunks;
}
