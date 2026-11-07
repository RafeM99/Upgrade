// Owner: Systems (Camilla's Claude).
#include "level/ChunkSpawner.h"
#include <vector>
#include "core/Config.h"
#include "level/Level.h"
#include "raylib.h"

void ChunkSpawner::reset(Level& level) {
    level.clear();
    forced.clear();
    int start = level.findChunk("start");
    lastIndex = start >= 0 ? start : 0;
    level.append(lastIndex);
}

void ChunkSpawner::update(Level& level, float viewRightX, int era, const std::set<std::string>& abilities) {
    const float ahead = Config::getFloat("spawner", "ahead_tiles", 60.0f);
    const std::vector<Chunk>& pool = level.pool();

    while (level.endX() < viewRightX + ahead) {
        std::vector<int> allowed;
        std::vector<int> usesForced;
        for (int i = 0; i < (int)pool.size(); ++i) {
            const Chunk& c = pool[i];
            if (c.name == "start" || c.minEra > era) continue;
            bool ok = true;
            for (const std::string& a : c.abilities)
                if (!abilities.count(a)) ok = false;
            if (!ok) continue;
            allowed.push_back(i);
            if (!forced.empty() && c.uses(forced)) usesForced.push_back(i);
        }

        std::vector<int>& choices = usesForced.empty() ? allowed : usesForced;
        int pick = lastIndex >= 0 ? lastIndex : 0;
        if (!choices.empty()) {
            pick = choices[GetRandomValue(0, (int)choices.size() - 1)];
            if (choices.size() > 1 && pick == lastIndex)   // avoid the same chunk twice in a row
                pick = choices[GetRandomValue(0, (int)choices.size() - 1)];
        }
        if (!usesForced.empty()) forced.clear();

        level.append(pick);
        lastIndex = pick;
    }
}
