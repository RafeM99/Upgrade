#pragma once

#include <string>
#include <vector>

// World units are pixels, y points down.
constexpr int TILE = 32;

// Tags from a chunk's header line, e.g. "# abilities=double_jump min_era=1 difficulty=2 coop=false".
struct ChunkTags {
    std::vector<std::string> abilities;   // required abilities, comma separated in the file
    int minEra = 0;
    int difficulty = 0;
    bool coop = false;
};

// A spawn marker from the chunk grid: S, G, B or F.
struct Marker {
    char type;
    int tx;
    int ty;
};

class Level {
public:
    // Loads one chunk file. Returns false (and builds a flat floor) if the file is missing.
    bool load(const std::string& path);

    // Top edge of the floor, in pixels: top of the highest solid tile in column 0.
    // Reads column 0 only, so it is right for a flat start floor; use isSolidAt for real collision.
    int floorTop() const;

    bool isSolidTile(int tx, int ty) const;      // world tile coordinates: tx = floor(x / TILE), ty = floor(y / TILE)
    bool isSolidAt(float x, float y) const;      // pixel coordinates, calls isSolidTile

    void draw() const;

    const ChunkTags& tags() const { return chunkTags; }
    const std::vector<Marker>& markers() const { return spawnMarkers; }

private:
    void buildFallback();
    void placeGrid();
    char tileAt(int tx, int ty) const;

    std::vector<std::string> rows;   // one string per tile row, all padded to the same width
    int width = 0;                   // in tiles
    int originY = 0;                 // pixel y of the top edge of row 0
    ChunkTags chunkTags;
    std::vector<Marker> spawnMarkers;
};
