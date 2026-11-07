#pragma once
// Owner: Systems (Camilla's Claude).
// Keeps chunks placed ahead of the camera. Picks at random from the chunks the players
// are allowed to see (era and abilities). Right after an ability is gained, the next
// chunk uses it, if one exists.
#include <set>
#include <string>

class Level;

class ChunkSpawner {
public:
    void reset(Level& level);   // places the "start" chunk
    void update(Level& level, float viewRightX, int era, const std::set<std::string>& abilities);
    void forceAbility(const std::string& name) { forced = name; }

private:
    std::string forced;
    int lastIndex = -1;
};
