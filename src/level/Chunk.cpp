#include "level/Chunk.h"
#include "player/Abilities.h"
#include "raylib.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace {

bool parseHeader(const std::string& line, Chunk& c, std::string& log)
{
    std::istringstream ss(line.substr(1));
    std::string tok;
    while (ss >> tok) {
        size_t eq = tok.find('=');
        if (eq == std::string::npos) continue;
        std::string k = tok.substr(0, eq), v = tok.substr(eq + 1);
        if (k == "abilities" || k == "required_abilities") {
            std::stringstream vs(v);
            std::string a;
            while (std::getline(vs, a, ',')) {
                if (a.empty() || a == "none") continue;
                int id = abilityFromId(a.c_str());
                if (id < 0) log += c.name + ": unknown ability '" + a + "'\n";
                else c.requires |= 1u << id;
            }
        } else if (k == "min_era") c.minEra = std::atoi(v.c_str());
        else if (k == "difficulty") c.difficulty = std::atoi(v.c_str());
        else if (k == "coop" || k == "coop_required") c.coop = (v == "true" || v == "1" || v == "yes");
        else if (k == "weight") c.weight = std::max(0, std::atoi(v.c_str()));
    }
    return true;
}

}  // namespace

int loadChunks(const std::string& dir, std::vector<Chunk>& out, std::string& log)
{
    FilePathList files = LoadDirectoryFilesEx(dir.c_str(), ".txt", false);
    std::vector<std::string> paths;
    for (unsigned i = 0; i < files.count; ++i) paths.push_back(files.paths[i]);
    UnloadDirectoryFiles(files);
    std::sort(paths.begin(), paths.end());
    int n = 0;
    for (const auto& p : paths) {
        std::ifstream in(p);
        if (!in) continue;
        Chunk c;
        c.name = GetFileNameWithoutExt(p.c_str());
        std::string line;
        std::vector<std::string> grid;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            // a comment or tag line is '#' followed by a space; grid rows like "####" are not
            if (!line.empty() && line[0] == '#' && (line.size() == 1 || line[1] == ' ')) { parseHeader(line, c, log); continue; }
            if (line.empty()) continue;
            grid.push_back(line);
        }
        if (grid.empty()) { log += c.name + ": empty grid\n"; continue; }
        if ((int)grid.size() > LEVEL_H) {
            log += c.name + ": taller than 14 rows, top rows cut\n";
            grid.erase(grid.begin(), grid.begin() + (grid.size() - LEVEL_H));
        }
        for (auto& r : grid) c.w = std::max(c.w, (int)r.size());
        c.rows.assign(LEVEL_H, std::string(c.w, '.'));
        int off = LEVEL_H - (int)grid.size();
        for (size_t r = 0; r < grid.size(); ++r)
            for (size_t x = 0; x < grid[r].size(); ++x) c.rows[off + r][x] = grid[r][x];
        // entry/exit check: ground in the bottom row at both ends
        if (c.rows[LEVEL_H - 1][0] != '#' || c.rows[LEVEL_H - 1][c.w - 1] != '#')
            log += c.name + ": no ground at the entry or exit (bottom row), it may not join up\n";
        out.push_back(c);
        ++n;
    }
    return n;
}

Chunk startChunk()
{
    Chunk c;
    c.name = "start";
    c.w = 30;
    c.rows.assign(LEVEL_H, std::string(c.w, '.'));
    for (int x = 0; x < c.w; ++x) { c.rows[LEVEL_H - 2][x] = '#'; c.rows[LEVEL_H - 1][x] = '#'; }
    for (int x = 12; x < 16; ++x) c.rows[LEVEL_H - 6][x] = '#';
    c.rows[LEVEL_H - 7][13] = 'F';
    c.rows[LEVEL_H - 7][14] = 'F';
    c.rows[LEVEL_H - 3][22] = 'S';
    c.weight = 0;
    return c;
}

Chunk generateChunk(std::mt19937& rng, int era, unsigned mask, int difficulty)
{
    auto rint = [&](int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); };
    auto chance = [&](float p) { return std::uniform_real_distribution<float>(0, 1)(rng) < p; };
    Chunk c;
    c.generated = true;
    c.name = "generated";
    c.w = rint(22, 30);
    c.difficulty = difficulty;
    c.rows.assign(LEVEL_H, std::string(c.w, '.'));
    const int G = LEVEL_H - 2;   // ground row (top of the 2-row floor)
    int maxGap = 3 + ((mask >> AB_DOUBLE_JUMP) & 1) + ((mask >> AB_DASH) & 1);
    int ground = G;   // current ground height (row of the top tile)
    int x = 0;
    auto column = [&](int cx, int top) {
        for (int y = top; y < LEVEL_H; ++y) c.rows[y][cx] = '#';
    };
    // flat entry
    for (; x < 3; ++x) column(x, G);
    while (x < c.w - 3) {
        int kind = rint(0, 9);
        if (kind <= 1 && x + 6 < c.w - 3) {   // gap
            int gap = rint(2, std::min(maxGap, 2 + difficulty));
            if (chance(0.5f)) c.rows[G - 3][x + gap / 2] = 'F';
            x += gap;
            ground = G;
            int land = rint(3, 5);
            for (int i = 0; i < land && x < c.w - 3; ++i, ++x) column(x, ground);
        } else if (kind <= 3) {   // step up or down
            int ng = std::max(G - 3, std::min(G, ground + (chance(0.5f) ? -1 : 1) * rint(1, 2)));
            ground = ng;
            int len = rint(3, 6);
            for (int i = 0; i < len && x < c.w - 3; ++i, ++x) column(x, ground);
        } else if (kind <= 5) {   // floating platform with fragments
            int len = rint(3, 5), py = ground - rint(3, 4);
            for (int i = 0; i < len + 2 && x < c.w - 3; ++i, ++x) {
                column(x, ground);
                if (i >= 1 && i <= len && py > 1) {
                    c.rows[py][x] = (era >= 1 && chance(0.15f)) ? '=' : '#';
                    if (chance(0.6f)) c.rows[py - 1][x] = 'F';
                }
            }
        } else {   // flat run with an enemy
            int len = rint(4, 7);
            int at = x + len / 2;
            for (int i = 0; i < len && x < c.w - 3; ++i, ++x) column(x, ground);
            if (at < c.w - 3) {
                char e = 'S';
                int r = rint(0, 99);
                if (era >= 1 && r < 15 + difficulty * 5) e = 'B';
                else if (r < 45) e = 'G';
                if (e == 'G') c.rows[std::max(1, ground - 4)][at] = 'G';
                else c.rows[ground - 1][at] = e;
            }
        }
    }
    for (; x < c.w; ++x) column(x, G);
    if (chance(0.25f)) c.rows[G - 4][c.w / 2] = 'P';
    // bring walkable steps back down at the exit
    for (int i = c.w - 3; i < c.w; ++i)
        for (int y = 0; y < G; ++y)
            if (c.rows[y][i] == '#') c.rows[y][i] = '.';
    return c;
}
