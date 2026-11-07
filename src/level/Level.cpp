#include "level/Level.h"
#include <algorithm>
#include <cmath>
#include "core/Config.h"

void Level::loadPool(const std::string& folder) {
    chunks = loadChunkFolder(folder);
    if (chunks.empty()) {
        // Safety net so the game still runs without chunk files: one flat floor.
        Chunk flat;
        flat.name = "start";
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
    overrides.clear();
    crumbling.clear();
    bumps.clear();
    bits.clear();
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
    int keep = beginX();
    while (!overrides.empty() && overrides.begin()->first.first < keep) overrides.erase(overrides.begin());
}

int Level::beginX() const {
    return placed.empty() ? 0 : placed.front().x0;
}

int Level::chunkStartAt(float x) const {
    for (const PlacedChunk& p : placed)
        if (x >= p.x0 && x < p.x0 + chunks[p.poolIndex].width) return p.x0;
    return beginX();
}

char Level::baseTileAt(int x, int y) const {
    for (const PlacedChunk& p : placed) {
        const Chunk& c = chunks[p.poolIndex];
        if (x >= p.x0 && x < p.x0 + c.width) return c.at(x - p.x0, y);
    }
    return '.';
}

char Level::tileAt(int x, int y) const {
    if (!overrides.empty()) {
        auto it = overrides.find({x, y});
        if (it != overrides.end()) return it->second;
    }
    return baseTileAt(x, y);
}

void Level::setTile(int x, int y, char c) {
    overrides[{x, y}] = c;
}

bool Level::solidFor(int x, int y, int player) const {
    switch (tileAt(x, y)) {
        case '#': case '=': case '|': case '%': case '?': case '!': case 'U': case 'X': case '~':
            return true;
        case '1': return player != 1;
        case '2': return player != 0;
        default: return false;
    }
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

bool Level::touchesSpike(const Rectangle& box) const {
    int x0 = (int)std::floor(box.x);
    int x1 = (int)std::floor(box.x + box.width);
    int y0 = (int)std::floor(box.y);
    int y1 = (int)std::floor(box.y + box.height);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (tileAt(x, y) != '^') continue;
            // The deadly part is the lower middle of the tile, so grazing a corner is fair.
            Rectangle s = {x + 0.15f, (float)y, 0.7f, 0.55f};
            if (box.x < s.x + s.width && box.x + box.width > s.x && box.y < s.y + s.height &&
                box.y + box.height > s.y)
                return true;
        }
    }
    return false;
}

float Level::spawnHeight(int x) const {
    for (int y = 40; y >= 0; --y)
        if (solidFor(x, y, 0)) return (float)(y + 1);
    return 2.0f;
}

void Level::stepOn(int x, int y) {
    if (tileAt(x, y) != 'X') return;
    setTile(x, y, '~');
    crumbling.push_back({x, y, Config::getFloat("traps", "fake_floor_delay", 0.12f)});
}

void Level::bump(int x, int y) {
    bumps.push_back({x, y, 0.2f});
}

float Level::bumpOffset(int x, int y) const {
    for (const Bump& b : bumps)
        if (b.x == x && b.y == y) return std::sin((0.2f - b.t) / 0.2f * PI) * 0.3f;
    return 0.0f;
}

void Level::shatter(int x, int y, char kind) {
    for (int i = 0; i < 4; ++i) {
        float sx = (i % 2) ? 1.0f : -1.0f;
        float sy = (i / 2) ? 1.0f : 0.0f;
        bits.push_back({{x + 0.5f + sx * 0.25f, y + 0.3f + sy * 0.4f}, {sx * 3.0f, 8.0f + sy * 4.0f}, 1.2f, kind});
    }
}

void Level::update(float dt) {
    for (Crumble& c : crumbling) {
        c.t -= dt;
        if (c.t <= 0.0f) {
            setTile(c.x, c.y, '.');
            shatter(c.x, c.y, '#');
        }
    }
    crumbling.erase(std::remove_if(crumbling.begin(), crumbling.end(), [](const Crumble& c) { return c.t <= 0.0f; }),
                    crumbling.end());

    for (Bump& b : bumps) b.t -= dt;
    bumps.erase(std::remove_if(bumps.begin(), bumps.end(), [](const Bump& b) { return b.t <= 0.0f; }), bumps.end());

    for (Debris& d : bits) {
        d.vel.y -= 30.0f * dt;
        d.pos.x += d.vel.x * dt;
        d.pos.y += d.vel.y * dt;
        d.life -= dt;
    }
    bits.erase(std::remove_if(bits.begin(), bits.end(), [](const Debris& d) { return d.life <= 0.0f; }), bits.end());
}

void Level::resetTraps(const std::vector<Rectangle>& keepClear) {
    for (auto it = overrides.begin(); it != overrides.end();) {
        int x = it->first.first, y = it->first.second;
        bool fakeFloor = baseTileAt(x, y) == 'X' && it->second != 'X';
        bool blocked = false;
        for (const Rectangle& r : keepClear)
            if (r.x < x + 1 && r.x + r.width > x && r.y < y + 1 && r.y + r.height > y) blocked = true;
        if (fakeFloor && !blocked) {
            crumbling.erase(std::remove_if(crumbling.begin(), crumbling.end(),
                                           [x, y](const Crumble& c) { return c.x == x && c.y == y; }),
                            crumbling.end());
            it = overrides.erase(it);
        } else {
            ++it;
        }
    }
}
