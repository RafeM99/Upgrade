#pragma once
// Keeps chunks placed ahead of the camera, forever. Each world is some random chunks
// followed by the "flag" chunk. World k uses chunks with min_era <= k and leans toward the
// ones new to that era. After the five console eras, worlds keep coming: longer each time,
// and weighted toward the nastiest chunks. Co-op chunks only appear when P2 has joined.
#include <vector>

class Level;

class ChunkSpawner {
public:
    void reset(Level& level);   // places the "start" chunk
    void update(Level& level, float viewRightX, bool twoPlayers);
    void ensureFlag(Level& level, int world, bool twoPlayers);   // place chunks until world's flag exists
    const std::vector<float>& flagXs() const { return flags; }   // flag x for world 0, 1, 2, ...
    static int chunksInWorld(int world);

private:
    void placeOne(Level& level, bool twoPlayers);
    std::vector<float> flags;
    int sinceFlag = 0;
    int lastIndex = -1;
};
