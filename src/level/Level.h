#pragma once
// The endless world: chunks placed side by side, plus tile collision.
// World units are tiles. x points right, y points up, tile (x, y) covers [x, x+1] by [y, y+1].
//
// Tiles:
//   .  empty            #  ground            %  brick (breaks when hit from below)
//   ?  coin block       !  troll block (spits out an enemy)
//   H  hidden block (invisible until you hit it from below, usually mid-jump over a pit)
//   X  fake floor (looks like ground, crumbles when stood on)
//   ^  spikes           V  hidden spikes (pop up when you get close)
//   |  pipe             =  cracked ground     1 / 2  solid for P1 / P2 only
//   U  used block (what ? ! H become)
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "level/Chunk.h"
#include "raylib.h"

struct PlacedChunk {
    int poolIndex;   // which chunk in pool()
    int x0;          // world x of its left edge
};

struct Debris {
    Vector2 pos;
    Vector2 vel;
    float life;
    char kind;       // tile it came from, for colour
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
    int chunkStartAt(float x) const; // left edge of the chunk holding x
    const std::deque<PlacedChunk>& placedChunks() const { return placed; }

    char tileAt(int x, int y) const;                      // '.' when empty
    void setTile(int x, int y, char c);                   // runtime change (used blocks, crumbled floor)
    bool solidFor(int x, int y, int player) const;        // player 0 or 1; -1 for enemies
    bool overlapsSolid(const Rectangle& box, int player) const;   // box.y is the bottom edge
    bool touchesSpike(const Rectangle& box) const;
    float spawnHeight(int x) const;                       // y just above the top solid tile in column x

    // Traps and juice.
    void update(float dt);
    void stepOn(int x, int y);       // a player is standing on this tile
    void bump(int x, int y);         // block wobble after a head hit
    void shatter(int x, int y, char kind);
    float bumpOffset(int x, int y) const;
    void resetTraps(const std::vector<Rectangle>& keepClear);   // fake floors come back after a death
    const std::vector<Debris>& debris() const { return bits; }

    // Filled by append(), emptied by EnemyManager.
    std::vector<SpawnMarker> newMarkers;

private:
    char baseTileAt(int x, int y) const;

    struct Crumble { int x, y; float t; };
    struct Bump { int x, y; float t; };

    std::vector<Chunk> chunks;
    std::deque<PlacedChunk> placed;
    std::map<std::pair<int, int>, char> overrides;
    std::vector<Crumble> crumbling;
    std::vector<Bump> bumps;
    std::vector<Debris> bits;
    int nextX = 0;
};
