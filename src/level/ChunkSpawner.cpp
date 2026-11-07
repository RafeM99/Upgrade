#include "level/ChunkSpawner.h"
#include <algorithm>
#include <vector>
#include "core/Config.h"
#include "core/EraManager.h"
#include "level/Level.h"
#include "raylib.h"

namespace {
bool isSpecial(const std::string& name) { return name == "start" || name == "flag"; }
}

int ChunkSpawner::chunksInWorld(int world) {
    int base = Config::getInt("world", "chunks_per_world", 5);
    int extra = std::max(0, world - (kEraCount - 1));   // endless worlds grow by one chunk each
    return std::min(base + extra, Config::getInt("world", "max_chunks_per_world", 10));
}

void ChunkSpawner::reset(Level& level) {
    level.clear();
    flags.clear();
    sinceFlag = 0;
    int start = level.findChunk("start");
    lastIndex = start >= 0 ? start : 0;
    level.append(lastIndex);
}

void ChunkSpawner::update(Level& level, float viewRightX, bool twoPlayers) {
    const float ahead = Config::getFloat("spawner", "ahead_tiles", 60.0f);
    while (level.endX() < viewRightX + ahead) placeOne(level, twoPlayers);
}

void ChunkSpawner::ensureFlag(Level& level, int world, bool twoPlayers) {
    int guard = 0;
    while ((int)flags.size() <= world && guard++ < 100) placeOne(level, twoPlayers);
}

void ChunkSpawner::placeOne(Level& level, bool twoPlayers) {
    const std::vector<Chunk>& pool = level.pool();
    const int flagChunk = level.findChunk("flag");
    {
        const int world = (int)flags.size();
        const int era = std::min(world, kEraCount - 1);

        if (flagChunk >= 0 && sinceFlag >= chunksInWorld(world)) {
            float poleX = (float)level.endX() + 6.5f;
            for (const SpawnMarker& m : pool[flagChunk].markers)
                if (m.type == 'P') poleX = (float)level.endX() + m.x + 0.5f;
            flags.push_back(poleX);
            level.append(flagChunk);
            lastIndex = flagChunk;
            sinceFlag = 0;
            return;
        }

        // Endless worlds (after the Wii era) lean on the chunks from the later eras.
        const int freshFrom = world < kEraCount ? era : std::max(0, kEraCount - 3);
        std::vector<int> allowed, fresh;
        for (int i = 0; i < (int)pool.size(); ++i) {
            const Chunk& c = pool[i];
            if (isSpecial(c.name) || c.minEra > era) continue;
            if (c.coop && !twoPlayers) continue;   // needs both players' colour blocks
            allowed.push_back(i);
            if (world < kEraCount ? c.minEra == era : c.minEra >= freshFrom) fresh.push_back(i);
        }
        int freshChance = world < kEraCount ? 50 : 70;
        std::vector<int>& choices = (!fresh.empty() && GetRandomValue(1, 100) <= freshChance) ? fresh : allowed;
        int pick = lastIndex >= 0 ? lastIndex : 0;
        if (!choices.empty()) {
            pick = choices[GetRandomValue(0, (int)choices.size() - 1)];
            if (choices.size() > 1 && pick == lastIndex)   // avoid the same chunk twice in a row
                pick = choices[GetRandomValue(0, (int)choices.size() - 1)];
        }
        level.append(pick);
        lastIndex = pick;
        ++sinceFlag;
    }
}
