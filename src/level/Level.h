// Level.h: the endless tile world. Columns are appended on the right as chunks
// spawn and dropped on the left once they are far behind the Forgetting Wall.
// Units are tiles; x grows right, y grows down; row LEVEL_H-1 is the bottom.
#pragma once
#include "level/Chunk.h"

#include <array>
#include <deque>
#include <random>
#include <vector>

struct SpawnMarker {
    char c;
    float x, y;   // tile coordinates of the marker's cell
};

class Level {
public:
    void reset();
    char tile(int x, int y) const;
    void setTile(int x, int y, char c);
    // player: 0 or 1, -1 for enemies. solo: one player only, so both coloured platforms are solid.
    bool solidFor(int x, int y, int player, bool solo) const;
    bool isSolidChar(char c) const { return c == '#' || c == '=' || c == '|'; }
    void append(const Chunk& c, std::vector<SpawnMarker>& out);
    void dropBefore(int x);
    void truncateFrom(int x);   // forget every column from x on (not yet seen), to place something new there
    int firstCol() const { return first_; }
    int endCol() const { return first_ + (int)cols_.size(); }
    int groundTop(int x) const;   // first solid row from the top, or -1 for a pit

private:
    std::deque<std::array<char, LEVEL_H>> cols_;
    int first_ = 0;
};

// Picks which chunk comes next (brief section 10).
class ChunkSpawner {
public:
    void init(const std::vector<Chunk>& pool, unsigned seed);
    void reset(Level& level, std::vector<SpawnMarker>& out);
    // Keeps chunks spawned up to `untilX`. era, ability mask, two players?
    void fill(Level& level, float untilX, int era, unsigned abilities, bool twoPlayers, std::vector<SpawnMarker>& out);
    void forceAbility(int ability) { forced_.push_back(ability); }
    void forceChunk(const std::string& name) { forcedName_ = name; }   // next chunk, by file name
    float generatedChance = 0.3f;
    float stageShare = 0.6f;   // chance the next chunk is one made for this graphics stage
    const std::string& lastName() const { return last_; }

private:
    std::vector<Chunk> pool_;
    std::mt19937 rng_;
    std::vector<int> forced_;
    std::string forcedName_;
    std::string last_;
    int placed_ = 0;
};
