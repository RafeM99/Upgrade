#include "raylib.h"
#include "level/Level.h"
#include "player/Abilities.h"
#include "render/ArtSpec.h"

#include <algorithm>

void Level::reset()
{
    cols_.clear();
    first_ = 0;
}

char Level::tile(int x, int y) const
{
    if (y < 0 || y >= LEVEL_H) return '.';
    if (x < first_) return '#';   // behind everything is solid, nothing escapes left
    int i = x - first_;
    if (i >= (int)cols_.size()) return '.';
    return cols_[i][y];
}

void Level::setTile(int x, int y, char c)
{
    if (y < 0 || y >= LEVEL_H || x < first_ || x >= endCol()) return;
    cols_[x - first_][y] = c;
}

bool Level::solidFor(int x, int y, int player, bool solo) const
{
    char c = tile(x, y);
    if (isSolidChar(c)) return true;
    if (c == '1') return player == 0 || solo || player < 0;
    if (c == '2') return player == 1 || solo || player < 0;
    return false;
}

void Level::append(const Chunk& c, std::vector<SpawnMarker>& out)
{
    int base = endCol();
    for (int x = 0; x < c.w; ++x) {
        std::array<char, LEVEL_H> col;
        for (int y = 0; y < LEVEL_H; ++y) {
            char ch = c.rows[y][x];
            if (ch == 'S' || ch == 'G' || ch == 'B' || ch == 'F' || ch == 'a' || ch == 'b' || ch == 'P') {
                out.push_back({ch, (float)(base + x), (float)y});
                ch = '.';
            }
            if (ch != '#' && ch != '=' && ch != '|' && ch != '1' && ch != '2') ch = '.';
            col[y] = ch;
        }
        cols_.push_back(col);
    }
}

void Level::dropBefore(int x)
{
    while (!cols_.empty() && first_ < x) {
        cols_.pop_front();
        ++first_;
    }
}

void Level::truncateFrom(int x)
{
    while (!cols_.empty() && endCol() > x) cols_.pop_back();
}

int Level::groundTop(int x) const
{
    for (int y = 0; y < LEVEL_H; ++y)
        if (isSolidChar(tile(x, y))) return y;
    return -1;
}

// ------------------------------------------------------------------ ChunkSpawner
void ChunkSpawner::init(const std::vector<Chunk>& pool, unsigned seed)
{
    pool_ = pool;
    rng_.seed(seed);
}

void ChunkSpawner::reset(Level& level, std::vector<SpawnMarker>& out)
{
    forced_.clear();
    placed_ = 0;
    level.reset();
    level.append(startChunk(), out);
    last_ = "start";
}

void ChunkSpawner::fill(Level& level, float untilX, int era, unsigned abilities, bool two, std::vector<SpawnMarker>& out)
{
    while (level.endCol() < untilX) {
        auto allowed = [&](const Chunk& c) {
            if ((c.requires & abilities) != c.requires) return false;
            if (c.minEra > era) return false;
            if (c.coop && !two) return false;
            if (c.difficulty > art::TIER[std::min(era, art::LAST_ERA)] + 2) return false;
            if (c.stages && !(c.stages >> era & 1u)) return false;
            return c.weight > 0;
        };
        const Chunk* pick = nullptr;
        // An era's intro gate comes first, straight after the upgrade.
        if (!forcedName_.empty()) {
            for (auto& ch : pool_)
                if (ch.name == forcedName_) pick = &ch;
            if (pick) TraceLog(LOG_INFO, "UPGRADE: placed %s at column %d", pick->name.c_str(), level.endCol());
            forcedName_.clear();
        }
        // Right after an ability unlocks, the next chunk uses it.
        while (!forced_.empty() && !pick) {
            int ab = forced_.front();
            forced_.erase(forced_.begin());
            std::vector<const Chunk*> c;
            for (auto& ch : pool_)
                if (allowed(ch) && (ch.requires >> ab & 1u)) c.push_back(&ch);
            if (!c.empty()) pick = c[std::uniform_int_distribution<size_t>(0, c.size() - 1)(rng_)];
        }
        Chunk gen;
        if (!pick) {
            float roll = std::uniform_real_distribution<float>(0, 1)(rng_);
            // most of the time, a chunk built for this stage's look and abilities
            if (roll < stageShare) {
                int total = 0;
                for (auto& ch : pool_)
                    if (ch.stages && allowed(ch) && ch.name != last_) total += ch.weight;
                if (total > 0) {
                    int r = std::uniform_int_distribution<int>(0, total - 1)(rng_);
                    for (auto& ch : pool_) {
                        if (!ch.stages || !allowed(ch) || ch.name == last_) continue;
                        r -= ch.weight;
                        if (r < 0) { pick = &ch; break; }
                    }
                }
            }
            bool useGen = !pick && std::uniform_real_distribution<float>(0, 1)(rng_) < generatedChance;
            if (!pick && !useGen) {
                int total = 0;
                for (auto& ch : pool_)
                    if (allowed(ch) && ch.name != last_) total += ch.weight;
                if (total > 0) {
                    int r = std::uniform_int_distribution<int>(0, total - 1)(rng_);
                    for (auto& ch : pool_) {
                        if (!allowed(ch) || ch.name == last_) continue;
                        r -= ch.weight;
                        if (r < 0) { pick = &ch; break; }
                    }
                }
            }
            if (!pick) {
                gen = generateChunk(rng_, art::TIER[std::min(era, art::LAST_ERA)], abilities, std::min(3, 1 + art::TIER[std::min(era, art::LAST_ERA)]));
                pick = &gen;
            }
        }
        level.append(*pick, out);
        last_ = pick->name;
        ++placed_;
    }
}
