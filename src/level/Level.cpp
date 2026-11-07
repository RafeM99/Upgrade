#include "level/Level.h"

#include "raylib.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {
// The chunk's bottom row ends here: 23 tiles down, so tile edges sit on multiples of TILE
// and the bottom 16 px of the last row hang below the 720 px screen.
const int GRID_BOTTOM = 23 * TILE;   // 736
const int DEFAULT_FLOOR_TOP = 640;

void parseHeader(const std::string& line, ChunkTags& tags)
{
    std::istringstream in(line.substr(1));   // skip the '#'
    std::string token;
    while (in >> token) {
        const size_t eq = token.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = token.substr(0, eq);
        const std::string value = token.substr(eq + 1);
        if (key == "abilities") {
            std::istringstream list(value);
            std::string name;
            while (std::getline(list, name, ',')) {
                if (!name.empty()) tags.abilities.push_back(name);
            }
        } else if (key == "min_era") {
            tags.minEra = std::atoi(value.c_str());
        } else if (key == "difficulty") {
            tags.difficulty = std::atoi(value.c_str());
        } else if (key == "coop") {
            tags.coop = (value == "true");
        }
    }
}
}

bool Level::load(const std::string& path)
{
    rows.clear();
    spawnMarkers.clear();
    chunkTags = ChunkTags{};
    width = 0;

    std::ifstream file(path);
    if (!file) {
        TraceLog(LOG_WARNING, "LEVEL: chunk file not found: %s (using a flat floor)", path.c_str());
        buildFallback();
        return false;
    }

    std::string line;
    bool first = true;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();   // Windows line endings
        if (first) {
            first = false;
            // Header: starts with '#' and has a key=value or any lower-case letter (tile rows have neither)
            const bool header = !line.empty() && line[0] == '#' &&
                                line.find_first_of("=abcdefghijklmnopqrstuvwxyz") != std::string::npos;
            if (header) {
                parseHeader(line, chunkTags);
                continue;
            }
            TraceLog(LOG_WARNING, "LEVEL: %s has no tag header", path.c_str());
        }
        rows.push_back(line);
        if (static_cast<int>(line.size()) > width) width = static_cast<int>(line.size());
    }

    // Drop trailing empty lines (e.g. a blank line at the end of the file)
    while (!rows.empty() && rows.back().empty()) rows.pop_back();

    if (rows.empty()) {
        TraceLog(LOG_WARNING, "LEVEL: chunk file has no rows: %s (using a flat floor)", path.c_str());
        buildFallback();
        return false;
    }

    // Pad short rows with '.', and take the spawn markers out of the grid
    for (int ty = 0; ty < static_cast<int>(rows.size()); ++ty) {
        rows[ty].resize(width, '.');
        for (int tx = 0; tx < width; ++tx) {
            const char c = rows[ty][tx];
            if (c == 'S' || c == 'G' || c == 'B' || c == 'F') {
                spawnMarkers.push_back(Marker{c, tx, ty});
                rows[ty][tx] = '.';
            }
        }
    }

    placeGrid();
    TraceLog(LOG_INFO, "LEVEL: loaded %s (%d x %d tiles, %d markers)", path.c_str(), width,
             static_cast<int>(rows.size()), static_cast<int>(spawnMarkers.size()));
    return true;
}

void Level::buildFallback()
{
    // Flat floor across the screen: top at 640, bottom at 736
    rows.assign(3, std::string(40, '#'));
    width = 40;
    spawnMarkers.clear();
    chunkTags = ChunkTags{};
    placeGrid();
}

void Level::placeGrid()
{
    originY = GRID_BOTTOM - static_cast<int>(rows.size()) * TILE;
}

char Level::tileAt(int tx, int ty) const
{
    if (ty < 0 || ty >= static_cast<int>(rows.size()) || tx < 0 || tx >= width) return '.';
    return rows[ty][tx];
}

bool Level::isSolidTile(int tx, int ty) const
{
    // tx, ty are world tile coordinates; originY is a multiple of TILE
    const char c = tileAt(tx, ty - originY / TILE);
    // '1' and '2' (platforms solid only for P1 / P2) count as not solid until players are told apart.
    return c == '#' || c == '=' || c == '|';
}

bool Level::isSolidAt(float x, float y) const
{
    return isSolidTile(static_cast<int>(std::floor(x / TILE)), static_cast<int>(std::floor(y / TILE)));
}

// Reads column 0 only, so it is right for a flat start floor; use isSolidAt for real collision.
int Level::floorTop() const
{
    for (int ty = 0; ty < static_cast<int>(rows.size()); ++ty) {
        if (isSolidTile(0, originY / TILE + ty)) return originY + ty * TILE;
    }
    return DEFAULT_FLOOR_TOP;
}

void Level::draw() const
{
    // Placeholder blocks; markers are not drawn yet
    for (int ty = 0; ty < static_cast<int>(rows.size()); ++ty) {
        for (int tx = 0; tx < width; ++tx) {
            if (!isSolidTile(tx, originY / TILE + ty)) continue;
            const int x = tx * TILE;
            const int y = originY + ty * TILE;
            const bool cracked = rows[ty][tx] == '=';
            const Color fill = cracked ? Color{170, 150, 150, 255} : Color{200, 170, 130, 255};
            const Color edge = cracked ? Color{100, 80, 80, 255} : Color{120, 90, 60, 255};
            DrawRectangle(x, y, TILE, TILE, fill);
            DrawRectangleLines(x, y, TILE, TILE, edge);
        }
    }
}
