#pragma once
// Owner: Systems (Camilla's Claude).
// Loads the ASCII chunk files in assets/chunks/ (format: brief section 10).
// First line is the tag header, for example:
//   # abilities=double_jump min_era=1 difficulty=2 coop=false
// Then one character per tile. The bottom row of the file sits at y = 0.
// Enemy and fragment letters (S G B F) become spawn markers, and their tile counts as empty.
#include <string>
#include <vector>

struct SpawnMarker {
    char type;    // 'S' Static Blob, 'G' Glitch Flyer, 'B' Fog Brute, 'F' fragment
    float x;      // world (or chunk) position of the tile's bottom-left corner
    float y;
};

struct Chunk {
    std::string name;                     // file name without .txt
    std::vector<std::string> abilities;   // all must be gained before this chunk appears
    int minEra = 0;
    int difficulty = 0;
    bool coop = false;
    int width = 0;
    int height = 0;
    std::vector<std::string> rows;        // rows[0] is the top row
    std::vector<SpawnMarker> markers;     // positions relative to the chunk's bottom-left

    char at(int col, int y) const;        // '.' outside the chunk
    bool uses(const std::string& ability) const;
};

bool loadChunkFile(const std::string& path, Chunk& out);
std::vector<Chunk> loadChunkFolder(const std::string& folder);
