// Owner: Systems (Camilla's Claude).
#include "level/Level.h"
#include <cmath>

void Level::loadPool(const std::string& folder) {
    chunks = loadChunkFolder(folder);
    if (chunks.empty()) {
        // Safety net so the game still runs without chunk files: one flat floor.
        Chunk flat;
        flat.name = "fallback_flat";
        flat.width = 24;
        flat.height = 2;
        flat.rows = {std::string(24, '#'), std::string(24, '#')};
        chunks.push_back(flat);
    }
}

int Level::findChunk(const std::string& name) const {
    for (size_t i = 0; i < chunks.size(); ++i)
        if (chunks[i].name == name) return (int)i;
    return -1;
}

void Level::clear() {
    placed.clear();
    newMarkers.clear();
    nextX = 0;
}

void Level::append(int poolIndex) {
    if (poolIndex < 0 || poolIndex >= (int)chunks.size()) return;
    const Chunk& c = chunks[poolIndex];
    placed.push_back({poolIndex, nextX});
    for (const SpawnMarker& m : c.markers) newMarkers.push_back({m.type, m.x + nextX, m.y});
    nextX += c.width;
}

void Level::cullBefore(float x) {
    while (placed.size() > 1) {
        const PlacedChunk& first = placed.front();
        if (first.x0 + chunks[first.poolIndex].width < x) placed.pop_front();
        else break;
    }
}

int Level::beginX() const {
    return placed.empty() ? 0 : placed.front().x0;
}

char Level::tileAt(int x, int y) const {
    for (const PlacedChunk& p : placed) {
        const Chunk& c = chunks[p.poolIndex];
        if (x >= p.x0 && x < p.x0 + c.width) return c.at(x - p.x0, y);
    }
    return '.';
}

bool Level::solidFor(int x, int y, int player) const {
    char t = tileAt(x, y);
    if (t == '#' || t == '=' || t == '|') return true;
    if (t == '1') return player == 0;
    if (t == '2') return player == 1;
    return false;
}

bool Level::overlapsSolid(const Rectangle& box, int player) const {
    const float eps = 0.0001f;
    int x0 = (int)std::floor(box.x);
    int x1 = (int)std::floor(box.x + box.width - eps);
    int y0 = (int)std::floor(box.y);
    int y1 = (int)std::floor(box.y + box.height - eps);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (solidFor(x, y, player)) return true;
    return false;
}

float Level::spawnHeight(int x) const {
    for (int y = 40; y >= 0; --y)
        if (solidFor(x, y, 0)) return (float)(y + 1);
    return 2.0f;
}
