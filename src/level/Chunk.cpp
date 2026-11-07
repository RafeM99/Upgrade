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
        } else if (k == "min_era") {
            static const int legacy[4] = {0, 2, 3, 5};   // old era -> graphics stage
            int e = std::atoi(v.c_str());
            c.minEra = e < 0 ? 0 : (e > 3 ? 99 : legacy[e]);
        } else if (k == "min_stage") c.minEra = std::atoi(v.c_str());
        else if (k == "stages") {
            std::stringstream vs(v);
            std::string a;
            while (std::getline(vs, a, ','))
                if (!a.empty()) c.stages |= 1u << std::atoi(a.c_str());
        }
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

// ------------------------------------------------------------------ generator
// Builds a chunk from set pieces laid left to right along a ground line. Every piece keeps to
// the jump limits (a plain jump rises 3.4 tiles and carries about 5.5 tiles), so a generated
// chunk is always passable without abilities it does not have, has no pit you can fall into
// and not climb out of (every pit is bottomless, every dip at most two tiles deep), keeps two
// clear rows over every place you can stand, and puts every fragment where a jump reaches it.
// Enemies are placed last, only on flat floor with room for their whole body.
namespace {

constexpr int G = LEVEL_H - 2;   // the usual ground row (top of a 2-row floor)
constexpr int G_HIGH = G - 4;    // the highest the ground line may climb

struct Gen {
    Chunk& c;
    std::mt19937& rng;
    int tier;
    unsigned mask;
    int diff;
    int x = 0;
    int ground = G;
    struct Flat { int x0, x1, g; };
    std::vector<Flat> flats;   // flat floor stretches, for the enemies
    std::vector<int> top;      // ground top per column (-1 = pit), filled as columns are made

    Gen(Chunk& ch, std::mt19937& r, int t, unsigned m, int d) : c(ch), rng(r), tier(t), mask(m), diff(d) {}

    int rint(int a, int b) { return a >= b ? a : std::uniform_int_distribution<int>(a, b)(rng); }
    bool chance(float p) { return std::uniform_real_distribution<float>(0, 1)(rng) < p; }
    bool has(int ab) const { return (mask >> ab) & 1u; }
    int room() const { return c.w - 3 - x; }   // columns left before the flat exit

    bool inside(int cx, int cy) const { return cx >= 0 && cx < c.w && cy >= 0 && cy < LEVEL_H; }
    char at(int cx, int cy) const { return inside(cx, cy) ? c.rows[cy][cx] : '#'; }
    void set(int cx, int cy, char ch) { if (inside(cx, cy)) c.rows[cy][cx] = ch; }
    // a marker or fragment, only into empty air
    void mark(int cx, int cy, char ch) { if (inside(cx, cy) && c.rows[cy][cx] == '.') c.rows[cy][cx] = ch; }

    void column(int g)
    {
        for (int y = g; y < LEVEL_H; ++y) c.rows[y][x] = '#';
        top.push_back(g);
        ++x;
    }
    void pit() { top.push_back(-1); ++x; }
    void flat(int len, bool forEnemies = true)
    {
        int x0 = x;
        for (int i = 0; i < len && room() > 0; ++i) column(ground);
        if (forEnemies && x - x0 >= 4) flats.push_back({x0, x - 1, ground});
    }
    // a row of fragments one tile over the floor (or a platform) at row g, from x0 to x1
    void coinLine(int x0, int x1, int g)
    {
        for (int cx = x0; cx <= x1; ++cx) mark(cx, g - 1, 'F');
    }
    // fragments along the arc of a jump from column a to column b over a gap (row g = take-off floor)
    void coinArc(int a, int b, int g)
    {
        int n = b - a;
        for (int i = 1; i < n; ++i) {
            float k = (float)i / n;
            int h = (int)std::lround(2.6f * 4 * k * (1 - k));
            mark(a + i, g - 1 - h, 'F');
        }
    }

    // ---- set pieces. Each starts at x with the current ground and leaves at least 2 columns
    // of floor at its end, so the next piece always has somewhere to take off from.
    void pieceRun()
    {
        int len = rint(4, 7);
        int x0 = x;
        flat(len);
        if (chance(0.35f)) coinLine(x0 + 1, std::min(x - 2, x0 + 3), ground);
    }
    void pieceSteps()
    {
        // a short staircase up or down, one or two tiles per step
        int dirUp = ground >= G ? 1 : (ground <= G_HIGH ? -1 : (chance(0.5f) ? 1 : -1));
        int steps = rint(1, 2 + (tier >= 2));
        for (int s = 0; s < steps; ++s) {
            int d = rint(1, 2);
            int ng = std::max(G_HIGH, std::min(G, ground - dirUp * d));
            if (ng == ground) break;
            ground = ng;
            int len = rint(2, 4);
            int x0 = x;
            flat(len, false);
            if (s == steps - 1 && chance(0.5f)) mark(x0 + len / 2, ground - 2, 'F');
        }
        flat(rint(2, 4));
    }
    void pieceGap()
    {
        int maxGap = (tier == 0 ? 3 : 4) + ((has(AB_DOUBLE_JUMP) || has(AB_DASH)) && diff >= 2 ? 1 : 0);
        int gap = rint(2, std::min(maxGap, 2 + diff));
        if (room() < gap + 4) { pieceRun(); return; }
        int a = x - 1, g0 = ground;
        for (int i = 0; i < gap; ++i) pit();
        // land level, one higher or lower; a wide gap lands level or lower
        int dg = gap >= 4 ? rint(0, 1) : rint(-1, 1);
        ground = std::max(G_HIGH, std::min(G, ground + dg));
        coinArc(a, x, std::min(g0, ground));
        flat(rint(3, 5));
    }
    void piecePillars()
    {
        // stepping stones over a long pit: 2-wide pillars, gaps of 2-3, heights varying by a tile
        int n = rint(2, 3);
        if (room() < n * 5 + 3) { pieceGap(); return; }
        int g = ground;
        for (int i = 0; i < n; ++i) {
            int gap = rint(2, tier == 0 ? 2 : 3);
            for (int k = 0; k < gap; ++k) pit();
            g = std::max(G_HIGH, std::min(G, g + rint(-1, 1)));
            ground = g;
            int x0 = x;
            column(ground);
            column(ground);
            if (chance(0.6f)) mark(x0 + rint(0, 1), ground - 2, 'F');
        }
        for (int k = 0; k < 2; ++k) pit();
        ground = std::max(G_HIGH, std::min(G, ground + rint(0, 1)));
        flat(rint(3, 5));
    }
    void piecePlatforms()
    {
        // one or two floating platforms over the floor, reached from the floor (3 tiles up),
        // the second one 3 tiles above the first and moved along so you never hit your head
        int len = rint(3, 5);
        int lead = 2;
        if (room() < len + lead + 3) { pieceRun(); return; }
        int x0 = x;
        flat(lead + len + 2, true);
        int py = ground - 3;
        int px = x0 + lead;
        for (int i = 0; i < len; ++i) set(px + i, py, (tier >= 2 && has(AB_GROUND_POUND) && chance(0.15f)) ? '=' : '#');
        coinLine(px, px + len - 1, py);
        // a second, higher platform, overlapping the first by at most one tile
        if (tier >= 1 && py - 3 >= 2 && chance(0.45f) && room() > 4) {
            int len2 = rint(2, 4);
            int px2 = px + len - 1 + rint(1, 2);
            int x1 = x;
            flat(std::max(0, px2 + len2 + 1 - x1), true);
            for (int i = 0; i < len2; ++i) set(px2 + i, py - 3, '#');
            coinLine(px2, px2 + len2 - 1, py - 3);
        }
    }
    void pieceHill()
    {
        // a mound: up one, up one more, down again; fragments on top
        if (ground <= G_HIGH + 1 || room() < 9) { pieceRun(); return; }
        int base = ground;
        ground = base - 1; flat(2, false);
        ground = base - 2;
        int x0 = x;
        flat(rint(2, 3), false);
        mark(x0, ground - 2, 'F');
        mark(x - 1, ground - 2, 'F');
        ground = base - 1; flat(2, false);
        ground = base;
        flat(2);
    }
    void pieceTunnel()
    {
        // a low roof over the path: two clear rows under it, fragments inside, and a way over the top
        int len = rint(4, 6);
        if (room() < len + 4 || ground - 3 < 3) { pieceRun(); return; }
        int x0 = x;
        flat(1, false);
        int rx = x;
        flat(len, false);
        flats.push_back({rx, x - 1, ground});   // room for a blob (not a clock: the roof is low)
        for (int i = 0; i < len; ++i) { set(rx + i, ground - 3, '#'); set(rx + i, ground - 4, '#'); }
        coinLine(rx + 1, rx + len - 2, ground);
        mark(rx + len / 2, ground - 5, 'F');
        flat(2, false);
        (void)x0;
    }
    void pieceVault()
    {
        // ground pound: a cracked lid over a shallow cache of fragments (one tile deep, so you
        // climb out with a plain jump). Only with the ground pound.
        int len = rint(3, 4);
        if (!has(AB_GROUND_POUND) || room() < len + 4 || ground > G - 1) { pieceRun(); return; }
        flat(2, false);
        int vx = x;
        for (int i = 0; i < len; ++i) {
            for (int y = ground; y < LEVEL_H; ++y) c.rows[y][x] = '#';
            c.rows[ground][x] = '=';
            c.rows[ground + 1][x] = '.';
            mark(x, ground + 1, 'F');
            top.push_back(ground);
            ++x;
        }
        (void)vx;
        flat(2);
    }
    void pieceWallClimb()
    {
        // wall jump: a chimney of two walls hanging over the path (its foot two tiles above the
        // floor, so everyone walks under it), with fragments at the top. Only the fragments need
        // the wall jump; the path never does.
        if (!has(AB_WALL_JUMP) || room() < 8 || ground - 9 < 1) { pieceRun(); return; }
        flat(1, false);
        int wx = x;
        flat(5, false);
        for (int y = ground - 9; y <= ground - 3; ++y) { set(wx, y, '|'); set(wx + 4, y, '|'); }
        for (int y = ground - 8; y <= ground - 6; y += 2) mark(wx + 2, y, 'F');
        flat(2);
    }
    void pieceGlideGap()
    {
        // glide: a high ledge before a long gap, fragments trailing down the glide
        if (!has(AB_GLIDE) || room() < 14) { pieceGap(); return; }
        int g0 = ground;
        ground = std::max(G_HIGH, g0 - 2);
        flat(2, false);
        int a = x - 1;
        int gap = 4;   // passable with a plain jump from the high ledge; glide collects the trail
        for (int i = 0; i < gap; ++i) pit();
        for (int i = 1; i <= gap; ++i) mark(a + i, ground - 2 + i / 2, 'F');
        ground = g0;
        flat(rint(3, 4));
    }
};

}  // namespace

Chunk generateChunk(std::mt19937& rng, int era, unsigned mask, int difficulty)
{
    // `era` is the stage tier (0 1-bit, 1 4-shade/8-bit, 2 16/32-bit, 3 2.5D/3D)
    Chunk c;
    c.generated = true;
    c.name = "generated";
    c.w = std::uniform_int_distribution<int>(26, 36)(rng);
    c.difficulty = difficulty;
    c.rows.assign(LEVEL_H, std::string(c.w, '.'));
    Gen g(c, rng, era, mask, difficulty);

    g.flat(3, false);   // flat entry, joins any chunk
    int last = -1;
    while (g.room() > 3) {
        // pick a set piece, never the same twice running; harder pieces come in later stages
        struct Opt { int id; int weight; };
        const Opt opts[] = {
            {0, 3},                                  // run
            {1, 3},                                  // steps
            {2, 4},                                  // gap
            {3, era >= 1 ? 2 : 0},                   // pillars
            {4, 3},                                  // platforms
            {5, 2},                                  // hill
            {6, era >= 1 ? 2 : 0},                   // tunnel
            {7, g.has(AB_GROUND_POUND) ? 2 : 0},     // vault
            {8, g.has(AB_WALL_JUMP) ? 1 : 0},        // wall climb
            {9, g.has(AB_GLIDE) ? 2 : 0},            // glide gap
        };
        int total = 0;
        for (auto& o : opts) if (o.id != last) total += o.weight;
        int r = g.rint(0, total - 1), pick = 0;
        for (auto& o : opts) {
            if (o.id == last) continue;
            r -= o.weight;
            if (r < 0) { pick = o.id; break; }
        }
        last = pick;
        switch (pick) {
        case 0: g.pieceRun(); break;
        case 1: g.pieceSteps(); break;
        case 2: g.pieceGap(); break;
        case 3: g.piecePillars(); break;
        case 4: g.piecePlatforms(); break;
        case 5: g.pieceHill(); break;
        case 6: g.pieceTunnel(); break;
        case 7: g.pieceVault(); break;
        case 8: g.pieceWallClimb(); break;
        case 9: g.pieceGlideGap(); break;
        }
    }
    // the exit comes back down to the usual floor in steps of at most two tiles
    while (g.x < c.w) {
        if (g.ground < G) g.ground = std::min(G, g.ground + 2);
        g.column(g.ground);
    }
    for (int i = c.w - 2; i < c.w; ++i)
        for (int y = 0; y < LEVEL_H; ++y) c.rows[y][i] = y >= G ? '#' : '.';
    g.top[c.w - 2] = g.top[c.w - 1] = G;

    // ---- enemies, last: only on flat floor, with room for the whole body, away from the edges
    auto clear = [&](int x0, int x1, int y0, int y1) {
        for (int cx = x0; cx <= x1; ++cx)
            for (int cy = y0; cy <= y1; ++cy)
                if (c.rows[std::max(0, cy)][cx] != '.' && c.rows[std::max(0, cy)][cx] != 'F') return false;
        return true;
    };
    auto sameFloor = [&](int x0, int x1, int gy) {
        for (int cx = x0; cx <= x1; ++cx)
            if (cx < 0 || cx >= c.w || g.top[cx] != gy) return false;
        return true;
    };
    int enemies = std::min(3, 1 + era / 2 + (difficulty >= 3 ? 1 : 0));
    std::shuffle(g.flats.begin(), g.flats.end(), rng);
    for (const auto& f : g.flats) {
        if (enemies <= 0) break;
        if (f.x0 < 5) continue;   // never right at the entry, where the last chunk's enemy may stand
        int cx = (f.x0 + f.x1 + 1) / 2;
        bool brute = era >= 1 && f.x1 - f.x0 >= 5 && g.chance(0.2f + 0.08f * difficulty);
        if (brute && sameFloor(cx - 1, cx + 1, f.g) && clear(cx - 1, cx + 1, f.g - 2, f.g - 1)) {
            c.rows[f.g - 1][cx] = 'B';
        } else if (sameFloor(cx - 1, cx + 1, f.g) && clear(cx, cx, f.g - 1, f.g - 1)) {
            c.rows[f.g - 1][cx] = 'S';
        } else continue;
        --enemies;
    }
    // a flyer now and then, in open sky over the path
    if (era >= 1 && g.chance(0.4f)) {
        int cx = g.rint(8, c.w - 6);
        int gy = g.top[cx] < 0 ? G : g.top[cx];
        int fy = gy - 5;
        if (fy >= 2 && clear(cx - 1, cx + 1, fy - 1, fy + 1)) c.rows[fy][cx] = 'G';
    }
    if (g.chance(0.2f)) {   // a power-up over a flat stretch
        for (const auto& f : g.flats)
            if (f.x1 - f.x0 >= 3 && clear(f.x0 + 1, f.x0 + 1, f.g - 3, f.g - 2)) { c.rows[f.g - 3][f.x0 + 1] = 'P'; break; }
    }
    return c;
}
