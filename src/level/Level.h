#pragma once
// Owner: Systems (Camilla's Claude).
// The endless world: chunks placed side by side, plus tile collision.
// World units are tiles. x points right, y points up, tile (x, y) covers [x, x+1] by [y, y+1].
#include <deque>
#include <string>
#include <vector>
#include "level/Chunk.h"
#include "raylib.h"

struct PlacedChunk {
    int poolIndex;   // which chunk in pool()
    int x0;          // world x of its left edge
};

class Level {
public:
    void loadPool(const std::string& folder);
    const std::vector<Chunk>& pool() const { return chunks; }
    int findChunk(const std::string& name) const;   // -1 when missing

    void clear();
    void append(int poolIndex);      // place a chunk at the right end
    void cullBefore(float x);        // drop chunks that end left of x
    int beginX() const;
    int endX() const { return nextX; }
    const std::deque<PlacedChunk>& placedChunks() const { return placed; }

    char tileAt(int x, int y) const;                      // '.' when empty
    bool solidFor(int x, int y, int player) const;        // player 0 or 1, for the '1' and '2' tiles
    bool overlapsSolid(const Rectangle& box, int player) const;   // box.y is the bottom edge
    float spawnHeight(int x) const;                       // y just above the top solid tile in column x

    // Filled by append(), emptied by EnemyManager.
    std::vector<SpawnMarker> newMarkers;

private:
    std::vector<Chunk> chunks;
    std::deque<PlacedChunk> placed;
    int nextX = 0;
};
