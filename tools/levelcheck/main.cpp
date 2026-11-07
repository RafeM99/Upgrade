// levelcheck: plays every level chunk with the real player physics and reports layouts that
// can trap the player, fragments that cannot be reached and enemies placed in the terrain.
//
// Build:  cmake --build build --target levelcheck
// Run:    ./build/levelcheck                 every chunk in assets/chunks
//         ./build/levelcheck --chunk c06_double [-v]
//         ./build/levelcheck --generated 200 every stage tier 0..3 of generateChunk(), 200 seeds each
//         ./build/levelcheck --two           also try each player alone in two-player mode (warnings)
// Exit code: 0 when everything passes, 1 when anything fails.
//
// How it works. Each chunk is placed in a test level: 8 columns of flat ground, the chunk, then
// 12 columns of flat ground. From a standing start on the left the tool searches over the states
// where the player stands on the ground (x quantised to 0.1 tile, row, running or not, which
// cracked tiles are broken). From each such state it runs input macros through the real
// Player::update at 1/60 s: hold left/right/none (with a change of direction at the top of the
// jump), jump held for 0/4/8/14/24 frames, and with the abilities the stage gives: double jump,
// dash, glide, wall jumps (climb one wall or zig-zag between two), ground pound (which breaks
// '=' tiles). Each macro ends when the player lands (a new state) or falls out of the level.
// Every box position on the way is checked against the fragments and power-ups.
//
// Checks per chunk:
//   exit      the right-hand ground can be reached from the left.
//   soft-lock a state you can reach from which the exit can no longer be reached (a pit, a cage).
//   F         every fragment: the player box can get within 1 tile of it on a path that can still
//             reach the exit.
//   P         every power-up: the player box can touch it on such a path.
//   enemies   box inside solid tiles; walkers (S B a b) floating with no floor, or falling out of
//             the level; walkers with no room to walk; brutes (B, 1.5 x 1.8) too big for the space.
//   ends      bottom row at the first and last column is ground, so chunks join up.
// Abilities come from the chunk header: the earliest stage it can be placed in (min_era,
// min_stage, stages=, difficulty, requires) and everything unlocked by then; alone, Player 1 has
// both players' abilities. Co-op chunks are checked for each player on their own (solo=false).
#include "core/Config.h"
#include "core/Paths.h"
#include "level/Chunk.h"
#include "level/Level.h"
#include "player/Abilities.h"
#include "player/Player.h"
#include "render/ArtSpec.h"
#include "raylib.h"

#include <algorithm>
#include <atomic>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef LEVELCHECK_ASSETS
#define LEVELCHECK_ASSETS ""
#endif

namespace {

constexpr float DT = 1.0f / 60.0f;
constexpr int LEAD_IN = 8;
constexpr int LEAD_OUT = 12;
constexpr int STAGES = art::ERA_COUNT;   // graphics stages 0..6
constexpr float REACH = 1.0f;            // a fragment counts if the box gets this close to its centre

PlayerTuning g_tune;
float g_quant = 0.1f;
bool g_stats = false;

// ------------------------------------------------------------------ abilities per stage
unsigned stageMask(int stage, int player /* -1 = solo: both sets */)
{
    unsigned m = 0;
    for (int e = 1; e <= stage; ++e)
        for (int p = 0; p < 2; ++p) {
            if (player >= 0 && p != player) continue;
            int a = abilityForEra(p, e);
            if (a >= 0) m |= 1u << a;
        }
    return m;
}

std::string maskNames(unsigned m)
{
    std::string s;
    for (int a = 0; a < AB_COUNT; ++a)
        if (m >> a & 1u) s += std::string(s.empty() ? "" : ",") + abilityId(a);
    return s.empty() ? "none" : s;
}

// The same rule as ChunkSpawner::fill's allowed(), for the solo ability set of each stage.
bool allowedIn(const Chunk& c, int stage)
{
    unsigned ab = stageMask(stage, -1);
    if ((c.requires & ab) != c.requires) return false;
    if (c.minEra > stage) return false;
    if (c.difficulty > art::TIER[std::min(stage, art::LAST_ERA)] + 2) return false;
    if (c.stages && !(c.stages >> stage & 1u)) return false;
    return c.weight > 0;
}

// ------------------------------------------------------------------ test level
struct TestLevel {
    Level level;
    int x0 = LEAD_IN;          // first column of the chunk
    int w = 0;
    int gridOff = 0;           // level row of the first grid row in the file
    float exitX = 0;           // a box left edge at or past this, standing, is the exit
    std::vector<SpawnMarker> markers;
    std::vector<std::pair<int, int>> cracked;   // '=' cells, index = bit in the broken mask
};

Chunk flatChunk(int w, bool endWall)
{
    Chunk c;
    c.name = "flat";
    c.w = w;
    c.rows.assign(LEVEL_H, std::string(w, '.'));
    for (int x = 0; x < w; ++x) c.rows[LEVEL_H - 2][x] = c.rows[LEVEL_H - 1][x] = '#';
    if (endWall)
        for (int y = 0; y < LEVEL_H; ++y) c.rows[y][w - 1] = '#';
    return c;
}

TestLevel buildLevel(const Chunk& c, int gridRows)
{
    TestLevel t;
    std::vector<SpawnMarker> dummy;
    t.level.reset();
    t.level.append(flatChunk(LEAD_IN, false), dummy);
    t.level.append(c, t.markers);
    t.level.append(flatChunk(LEAD_OUT, true), dummy);
    t.w = c.w;
    t.gridOff = LEVEL_H - gridRows;
    t.exitX = (float)(t.x0 + c.w + 1);
    for (int x = t.x0; x < t.x0 + c.w; ++x)
        for (int y = 0; y < LEVEL_H; ++y)
            if (t.level.tile(x, y) == '=') t.cracked.push_back({x, y});
    return t;
}

// ------------------------------------------------------------------ search
struct NodeKey {
    int xq, row, vb;
    uint64_t mask;
    bool operator==(const NodeKey& o) const { return xq == o.xq && row == o.row && vb == o.vb && mask == o.mask; }
};
struct NodeKeyHash {
    size_t operator()(const NodeKey& k) const
    {
        uint64_t h = (uint64_t)(uint32_t)k.xq * 0x9E3779B97F4A7C15ull ^ (uint64_t)(k.row * 131 + k.vb + 7) * 0xC2B2AE3D27D4EB4Full ^ k.mask * 0x165667B19E3779F9ull;
        return (size_t)(h ^ (h >> 29));
    }
};
struct Node {
    float x, y, vx;
    int row;
    uint64_t mask;
};

enum Act { A_NONE, A_DJ_LONG, A_DJ_SHORT, A_DASH, A_POUND, A_GLIDE };
enum Trig { T_APEX, T_APEX12, T_FRAME0 };
enum Wj { WJ_OFF, WJ_CLIMB, WJ_ZIGZAG };

struct Macro {
    int d1 = 0, d2 = 0, jump = 0;
    int a1 = A_NONE, t1 = T_APEX, a2 = A_NONE;
    int wj = WJ_OFF;
};

constexpr int DEATH = -1;

struct Search {
    const TestLevel* T = nullptr;
    int pid = 0;
    bool solo = true;
    unsigned abilities = 0;

    std::vector<Node> nodes;
    std::unordered_map<NodeKey, int, NodeKeyHash> index;
    std::deque<int> queue;
    std::unordered_set<uint64_t> edges;   // from << 32 | to
    std::map<uint64_t, Level> levels;     // per broken mask
    std::unordered_set<NodeKey, NodeKeyHash> coarse;

    // what each touched item led to: node id, or DEATH
    struct Touch { std::set<int> ends; };
    std::vector<Touch> fragTouch, pupTouch;
    std::vector<std::vector<int>> fragsByCol, pupsByCol;   // marker index per level column
    std::vector<const SpawnMarker*> frags, pups;

    std::vector<char> coreach;
    long long sims = 0, frames = 0;
    bool exitReached = false;
    float maxX = 0;

    const Level& levelFor(uint64_t mask)
    {
        auto it = levels.find(mask);
        if (it != levels.end()) return it->second;
        Level L = T->level;
        for (size_t i = 0; i < T->cracked.size(); ++i)
            if (mask >> i & 1u) L.setTile(T->cracked[i].first, T->cracked[i].second, '.');
        return levels.emplace(mask, L).first->second;
    }

    uint64_t maskOf(const Level& L) const
    {
        uint64_t m = 0;
        for (size_t i = 0; i < T->cracked.size(); ++i)
            if (L.tile(T->cracked[i].first, T->cracked[i].second) != '=') m |= 1ull << i;
        return m;
    }

    int addNode(const Player& p, uint64_t mask)
    {
        int vb = p.vx >= g_tune.runSpeed - 0.01f ? 1 : (p.vx <= -g_tune.runSpeed + 0.01f ? -1 : 0);
        int row = (int)std::lround(p.y + p.h);
        NodeKey k{(int)std::lround(p.x / g_quant), row, vb, mask};
        auto it = index.find(k);
        if (it != index.end()) return it->second;
        int id = (int)nodes.size();
        nodes.push_back({p.x, p.y, vb * g_tune.runSpeed, row, mask});
        index.emplace(k, id);
        queue.push_back(id);
        maxX = std::max(maxX, p.x);
        return id;
    }

    void setup()
    {
        int cols = T->level.endCol() + 2;
        fragsByCol.assign(cols, {});
        pupsByCol.assign(cols, {});
        for (const auto& m : T->markers) {
            if (m.c == 'F') { fragsByCol[(int)m.x].push_back((int)frags.size()); frags.push_back(&m); }
            if (m.c == 'P') { pupsByCol[(int)m.x].push_back((int)pups.size()); pups.push_back(&m); }
        }
        fragTouch.assign(frags.size(), {});
        pupTouch.assign(pups.size(), {});
    }

    Player makePlayer(const Node& n) const
    {
        Player p;
        p.index = pid;
        p.joined = true;
        for (int a = 0; a < AB_COUNT; ++a) p.has[a] = abilities >> a & 1u;
        p.x = n.x;
        p.y = n.y;
        p.vx = n.vx;
        p.vy = 0;
        p.facing = n.vx < 0 ? -1 : 1;
        p.onGround = true;
        p.coyote = g_tune.coyote;
        return p;
    }

    // Box positions: note fragments and power-ups the box touches.
    void touch(const Player& p, std::vector<int>& fr, std::vector<int>& pu) const
    {
        int c0 = std::max(0, (int)std::floor(p.x) - 2), c1 = std::min((int)fragsByCol.size() - 1, (int)std::floor(p.x + p.w) + 2);
        for (int c = c0; c <= c1; ++c) {
            for (int i : fragsByCol[c]) {
                float fx = frags[i]->x + 0.5f, fy = frags[i]->y + 0.5f;
                float dx = std::max({p.x - fx, 0.0f, fx - (p.x + p.w)});
                float dy = std::max({p.y - fy, 0.0f, fy - (p.y + p.h)});
                if (dx * dx + dy * dy <= REACH * REACH) fr.push_back(i);
            }
            for (int i : pupsByCol[c]) {
                float px = pups[i]->x + 0.05f, py = pups[i]->y + 0.05f;
                if (p.x < px + 0.9f && px < p.x + p.w && p.y < py + 0.9f && py < p.y + p.h) pu.push_back(i);
            }
        }
    }

    struct Result {
        bool airApex = false;     // reached the top of the jump (or left the ground) in the air
        bool airAfterA1 = false;  // a1 fired and a later apex came
        bool wall = false;        // touched a wall in the air
        bool a1Fired = false;
    };

    // Runs one macro from node `from`. Records new nodes and edges.
    Result run(int from, const Macro& m)
    {
        Result r;
        const Node n = nodes[from];
        bool pound = m.a1 == A_POUND || m.a2 == A_POUND;
        Level* own = nullptr;
        Level copy;
        const Level& base = levelFor(n.mask);
        if (pound && !T->cracked.empty()) { copy = base; own = &copy; }
        Level& L = own ? *own : const_cast<Level&>(base);   // only a pound writes, and then to the copy

        Player p = makePlayer(n);
        ++sims;
        const bool walk = m.jump == 0 && m.a1 == A_NONE && m.wj == WJ_OFF;
        int hold = 0, frame = 0, still = 0, apexAt = -1, a1At = -1, sinceWj = 99, wjCount = 0, dirOverride = 0;
        int stage = m.a1 == A_NONE ? 2 : 0;   // 0 waiting for a1, 1 waiting for a2, 2 done
        if (stage == 0 && m.a2 == A_NONE) {}
        bool glide = false, airborne = false, switched = false;
        int last = -2;
        std::vector<int> fr, pu;
        auto settle = [&](int to) {
            if (to != last) edges.insert((uint64_t)from << 32 | (uint32_t)to);
            for (int i : fr) fragTouch[i].ends.insert(to);
            for (int i : pu) pupTouch[i].ends.insert(to);
            fr.clear();
            pu.clear();
            last = to;
        };
        touch(p, fr, pu);
        for (frame = 0; frame < 480; ++frame) {
            PlayerInput in;
            // direction: d1 until the first action (or the top of the jump), then d2
            int dir = switched ? m.d2 : m.d1;
            if (dirOverride) dir = dirOverride;
            if (m.jump > 0 && frame == 0) { in.jumpPressed = true; hold = m.jump; }
            // actions
            auto fire = [&](int a) {
                switch (a) {
                case A_DJ_LONG: in.jumpPressed = true; hold = 24; break;
                case A_DJ_SHORT: in.jumpPressed = true; hold = 8; break;
                case A_DASH: in.abilityPressed = true; break;
                case A_POUND: in.abilityPressed = true; in.down = true; break;
                case A_GLIDE: glide = true; break;
                }
            };
            bool air = !p.onGround && frame > 0;
            if (air && apexAt < 0 && p.vy >= 0 && p.dashTime <= 0) { apexAt = frame; r.airApex = true; }
            if (stage == 0) {
                bool go = (m.t1 == T_FRAME0 && frame == 0) || (m.t1 == T_APEX && apexAt >= 0) || (m.t1 == T_APEX12 && apexAt >= 0 && frame >= apexAt + 12);
                if (go) {
                    switched = true;
                    if (!dirOverride) dir = m.d2;
                    fire(m.a1);
                    r.a1Fired = true;
                    a1At = frame;
                    stage = m.a2 == A_NONE ? 2 : 1;
                }
            } else if (stage == 1) {
                if (air && frame >= a1At + 2 && p.vy >= 0 && p.dashTime <= 0 && !p.pounding) {
                    r.airAfterA1 = true;
                    fire(m.a2);
                    stage = 2;
                }
            }
            if (!switched && m.a1 == A_NONE && apexAt >= 0) { switched = true; if (!dirOverride) dir = m.d2; }
            // wall jumps
            bool wjNow = false;
            int wallBefore = p.wallDir;
            if (air && p.wallDir != 0) r.wall = true;
            if (m.wj != WJ_OFF && air && p.wallDir != 0 && sinceWj >= 10 && p.vy > -3 && wjCount < 12 && !in.jumpPressed) {
                in.jumpPressed = true;
                hold = 12;
                wjNow = true;
            }
            in.left = dir < 0;
            in.right = dir > 0;
            in.jumpHeld = hold > 0 || glide;
            if (hold > 0) --hold;
            p.update(in, L, solo, false, DT, g_tune);
            ++frames;
            ++sinceWj;
            if (wjNow && p.evWallJump) {
                ++wjCount;
                sinceWj = 0;
                dirOverride = m.wj == WJ_CLIMB ? wallBefore : -wallBefore;
            }
            touch(p, fr, pu);
            if (p.y > LEVEL_H + 1) { settle(DEATH); return r; }
            if (!p.onGround) airborne = true;
            if (p.onGround) {
                if (airborne) { settle(addNode(p, own ? maskOf(L) : n.mask)); return r; }
                if (walk) {
                    int id = addNode(p, n.mask);
                    if (id != last) settle(id);
                    if (std::fabs(p.vx) < 0.001f && frame > 2) { if (++still > 3) return r; }
                    else still = 0;
                    if (frame > 300) return r;
                } else if (frame >= 8 && m.jump == 0) {
                    return r;   // walking ability macro that never left the ground: the walk covers it
                } else if (frame >= 8) {
                    settle(addNode(p, own ? maskOf(L) : n.mask));   // jump blocked
                    return r;
                }
            }
        }
        return r;   // timed out in the air
    }

    void expand(int id)
    {
        const bool dj = abilities >> AB_DOUBLE_JUMP & 1u, dash = abilities >> AB_DASH & 1u;
        const bool gp = abilities >> AB_GROUND_POUND & 1u, wj = abilities >> AB_WALL_JUMP & 1u;
        const bool gl = abilities >> AB_GLIDE & 1u;
        std::vector<int> a1s;
        if (dj) { a1s.push_back(A_DJ_LONG); a1s.push_back(A_DJ_SHORT); }
        if (dash) a1s.push_back(A_DASH);
        if (gp) a1s.push_back(A_POUND);
        if (gl) a1s.push_back(A_GLIDE);
        auto seconds = [&](int a1) {
            std::vector<int> v{A_NONE};
            if (a1 == A_DJ_LONG || a1 == A_DJ_SHORT) {
                if (dash) v.push_back(A_DASH);
                if (gl) v.push_back(A_GLIDE);
                if (gp) v.push_back(A_POUND);
            } else if (a1 == A_DASH) {
                if (dj) v.push_back(A_DJ_LONG);
                if (gl) v.push_back(A_GLIDE);
                if (gp) v.push_back(A_POUND);
            }
            return v;
        };
        auto withWalls = [&](Macro m) {
            Result r = run(id, m);
            if (wj && r.wall) {
                m.wj = WJ_CLIMB; run(id, m);
                m.wj = WJ_ZIGZAG; run(id, m);
            }
            return r;
        };
        // Ability macros run from one state per 0.3 tile (per row, speed and broken tiles): they
        // fan out widely, and neighbouring states 0.1 apart give the same landings.
        const Node& nd = nodes[id];
        NodeKey ck{(int)std::lround(nd.x / (3 * g_quant)), nd.row, nd.vx > 0 ? 1 : (nd.vx < 0 ? -1 : 0), nd.mask};
        const bool rich = coarse.insert(ck).second;
        static const int jumps[] = {0, 4, 8, 14, 24};
        for (int d1 = -1; d1 <= 1; ++d1)
            for (int d2 = -1; d2 <= 1; ++d2)
                for (int J : jumps) {
                    Macro m;
                    m.d1 = d1; m.d2 = d2; m.jump = J;
                    if (J == 0 && d1 != d2 && d2 != 0) {}   // walk then turn in the air: kept
                    Result base = withWalls(m);
                    if (!rich || a1s.empty() || (J != 0 && J != 8 && J != 24)) continue;
                    if (!base.airApex && J > 0) continue;
                    for (int a1 : a1s)
                        for (int t1 : {T_APEX, T_APEX12}) {
                            // second actions only after a full double jump or a dash at the top
                            std::vector<int> a2s = (t1 == T_APEX && (a1 == A_DJ_LONG || a1 == A_DASH)) ? seconds(a1) : std::vector<int>{A_NONE};
                            for (int a2 : a2s) {
                                Macro ma = m;
                                ma.a1 = a1; ma.t1 = t1; ma.a2 = a2;
                                Result r = withWalls(ma);
                                if (!r.a1Fired) break;          // landed before the action: the rest are the same
                                if (a2 == A_NONE && !r.airApex) break;
                            }
                            if (a1 == A_GLIDE || a1 == A_POUND) break;   // one timing is enough
                        }
                    if (rich && dash && J == 0 && d1 == d2) {
                        for (int a2 : seconds(A_DASH)) {
                            Macro ma = m;
                            ma.a1 = A_DASH; ma.t1 = T_FRAME0; ma.a2 = a2;
                            withWalls(ma);
                        }
                    }
                }
    }

    void solve(float startX)
    {
        setup();
        Player p;
        p.index = pid;
        p.spawnAt(startX, (float)(LEVEL_H - 2));
        p.y = (float)(LEVEL_H - 2) - p.h - 0.001f;
        p.vx = 0;
        addNode(p, 0);
        while (!queue.empty()) {
            int id = queue.front();
            queue.pop_front();
            expand(id);
        }
        // backwards from the exit
        std::vector<std::vector<int>> rev(nodes.size());
        for (uint64_t e : edges) {
            int a = (int)(e >> 32), b = (int)(uint32_t)e;
            if (b >= 0) rev[b].push_back(a);
        }
        coreach.assign(nodes.size(), 0);
        std::vector<int> st;
        for (size_t i = 0; i < nodes.size(); ++i)
            if (nodes[i].x >= T->exitX) { coreach[i] = 1; st.push_back((int)i); exitReached = true; }
        while (!st.empty()) {
            int b = st.back();
            st.pop_back();
            for (int a : rev[b])
                if (!coreach[a]) { coreach[a] = 1; st.push_back(a); }
        }
    }

    // 0 never touched, 1 touched only on paths that die or get stuck, 2 safely
    int itemState(const Touch& t) const
    {
        if (t.ends.empty()) return 0;
        for (int e : t.ends)
            if (e >= 0 && coreach[e]) return 2;
        return 1;
    }
};

// ------------------------------------------------------------------ enemies
struct EnemyBox { float w, h; const char* name; };
const EnemyBox BLOB{0.8f, 0.72f, "blob"}, FLYER{0.7f, 0.6f, "flyer"}, BRUTE{1.5f, 1.8f, "brute"};

bool solidE(const Level& L, float x, float y, float w, float h) { return boxHitsSolid(L, x, y, w, h, -1, false); }

// The walker movement of updateEnemy (src/enemies/Enemy.cpp), copied so the tool does not depend
// on the enemy code: fall, then walk, turning at walls and ledges.
void checkWalker(const Level& L, float x, float y, const EnemyBox& b, float speed, std::vector<std::string>& out, const std::string& where)
{
    float vy = 0;
    bool onG = false;
    const float y0 = y;
    for (int i = 0; i < 600 && !onG; ++i) {
        vy = std::min(vy + 50 * DT, 20.0f);
        y += vy * DT;
        if (solidE(L, x, y, b.w, b.h)) {
            if (vy > 0) { y = std::floor(y + b.h) - b.h - 0.001f; onG = true; }
            else y = std::floor(y) + 1.001f;
            vy = 0;
        }
        if (y > LEVEL_H + 2) { out.push_back(where + ": " + b.name + " has no floor and falls out of the level"); return; }
    }
    if (y - y0 > 0.05f) {
        char buf[96];
        std::snprintf(buf, sizeof buf, ": %s floats, it drops %.1f tiles before it lands", b.name, y - y0);
        out.push_back(where + buf);
    }
    float minX = x, maxX = x;
    int dir = 1;
    for (int i = 0; i < 900; ++i) {
        float vx = dir * speed;
        vy = std::min(vy + 50 * DT, 20.0f);
        x += vx * DT;
        if (solidE(L, x, y, b.w, b.h)) { x -= vx * DT; dir = -dir; }
        y += vy * DT;
        bool g = false;
        if (solidE(L, x, y, b.w, b.h)) {
            if (vy > 0) { y = std::floor(y + b.h) - b.h - 0.001f; g = true; }
            else y = std::floor(y) + 1.001f;
            vy = 0;
        }
        if (g) {
            float ahead = dir > 0 ? x + b.w + 0.05f : x - 0.05f;
            if (!L.solidFor((int)std::floor(ahead), (int)std::floor(y + b.h + 0.1f), -1, false)) dir = -dir;
        }
        if (y > LEVEL_H + 2) { out.push_back(where + ": " + b.name + " walks off into a pit"); return; }
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
    }
    if (maxX - minX < 0.5f) out.push_back(where + ": " + b.name + " has no room to walk (stuck)");
}

inline int yy_cap(int h) { return std::min(h, 3); }

// ------------------------------------------------------------------ one chunk
struct Report {
    std::string name;
    std::string head;
    std::vector<std::string> fails, warns;
    // for the generated summary
    bool crawlFail = false, exitFail = false, softFail = false, fragFail = false, enemyFail = false, pupFail = false, endFail = false;
    double seconds = 0;
};

struct CheckOpts {
    bool two = false;
    bool verbose = false;
};

std::string at(const TestLevel& T, int lx, int ly)
{
    char b[64];
    std::snprintf(b, sizeof b, "(x=%d, y=%d)", lx - T.x0, ly - T.gridOff);
    return b;
}

struct RunOut {
    bool exit = false;
    float maxX = 0;
    std::vector<std::string> soft;
    std::vector<int> fragState, pupState;
};

RunOut runPlayer(const TestLevel& T, int pid, bool solo, unsigned ab)
{
    Search s;
    s.T = &T;
    s.pid = pid;
    s.solo = solo;
    s.abilities = ab;
    s.solve(2.5f);
    RunOut o;
    if (g_stats) std::fprintf(stderr, "  [P%d] %zu states, %lld macros, %lld frames\n", pid + 1, s.nodes.size(), s.sims, s.frames);
    o.exit = s.exitReached;
    o.maxX = s.maxX - T.x0;
    for (auto& t : s.fragTouch) o.fragState.push_back(s.itemState(t));
    for (auto& t : s.pupTouch) o.pupState.push_back(s.itemState(t));
    if (o.exit) {
        // group soft-lock states by row and column runs
        std::map<int, std::set<int>> byRow;
        for (size_t i = 0; i < s.nodes.size(); ++i)
            if (!s.coreach[i]) byRow[s.nodes[i].row].insert((int)std::floor(s.nodes[i].x + 0.35f));
        for (auto& [row, cols] : byRow) {
            int a = -1000, b = -1000;
            auto flush = [&]() {
                if (a < -999) return;
                char buf[160];
                std::snprintf(buf, sizeof buf, "standing on row y=%d, x=%d..%d: the exit can no longer be reached from there", row - T.gridOff, a - T.x0, b - T.x0);
                o.soft.push_back(buf);
            };
            for (int c : cols) {
                if (c == b + 1) b = c;
                else { flush(); a = b = c; }
            }
            flush();
        }
    }
    return o;
}

Report checkChunk(const Chunk& c, int gridRows, int stage, unsigned ab, const CheckOpts& opt)
{
    Report R;
    R.name = c.name;
    TestLevel T = buildLevel(c, gridRows);
    char hb[200];
    std::snprintf(hb, sizeof hb, "stage %d, abilities %s%s", stage, maskNames(ab).c_str(), c.coop ? ", co-op (each player alone)" : "");
    R.head = hb;

    // ends join up
    if (c.rows[LEVEL_H - 1][0] != '#' || c.rows[LEVEL_H - 1][c.w - 1] != '#') {
        R.fails.push_back("ends: the bottom row needs ground ('#') in the first and last column so chunks join up");
        R.endFail = true;
    }

    std::vector<int> fragBest(0), pupBest(0);
    auto merge = [](std::vector<int>& best, const std::vector<int>& s) {
        if (best.empty()) best = s;
        else for (size_t i = 0; i < s.size(); ++i) best[i] = std::max(best[i], s[i]);
    };
    auto runAndReport = [&](int pid, bool solo, unsigned mask, const std::string& who, bool asWarning) {
        RunOut o = runPlayer(T, pid, solo, mask);
        auto& sink = asWarning ? R.warns : R.fails;
        if (!o.exit) {
            char b[200];
            std::snprintf(b, sizeof b, "%sexit: cannot get through (furthest standing point x=%.1f of %d)", who.c_str(), o.maxX, c.w);
            sink.push_back(b);
            if (!asWarning) R.exitFail = true;
        }
        for (auto& s : o.soft) {
            sink.push_back(who + "soft-lock: " + s);
            if (!asWarning) R.softFail = true;
        }
        if (!asWarning) { merge(fragBest, o.fragState); merge(pupBest, o.pupState); }
    };
    if (!c.coop) {
        runAndReport(0, true, ab, "", false);
        if (opt.two)
            for (int p = 0; p < 2; ++p) {
                unsigned m = stageMask(stage, p);
                runAndReport(p, false, m, p == 0 ? "two players, P1 alone: " : "two players, P2 alone: ", true);
            }
    } else {
        for (int p = 0; p < 2; ++p)
            runAndReport(p, false, stageMask(stage, p), p == 0 ? "P1: " : "P2: ", false);
    }
    // fragments and power-ups
    int fi = 0, pi = 0;
    for (const auto& m : T.markers) {
        if (m.c == 'F') {
            int s = fragBest.empty() ? 0 : fragBest[fi];
            if (s < 2) {
                R.fails.push_back("F " + at(T, (int)m.x, (int)m.y) + (s == 0 ? ": fragment cannot be reached" : ": fragment only reachable by falling out or getting stuck"));
                R.fragFail = true;
            }
            ++fi;
        } else if (m.c == 'P') {
            int s = pupBest.empty() ? 0 : pupBest[pi];
            if (s < 2) {
                R.fails.push_back("P " + at(T, (int)m.x, (int)m.y) + (s == 0 ? ": power-up cannot be reached" : ": power-up only reachable by falling out or getting stuck"));
                R.pupFail = true;
            }
            if (T.level.isSolidChar(c.rows[(int)m.y][(int)m.x - T.x0])) R.fails.push_back("P " + at(T, (int)m.x, (int)m.y) + ": inside solid");
            ++pi;
        }
    }
    // enemies
    bool brute = false, blobB = false;
    for (int s = 0; s < STAGES; ++s)
        if (c.generated ? s == stage : allowedIn(c, s)) { if (s >= 1) brute = true; else blobB = true; }
    if (c.generated) { brute = stage >= 1; blobB = stage == 0; }
    if (c.name.rfind("intro_stage", 0) == 0) brute = true;
    std::vector<std::string> en;
    for (const auto& m : T.markers) {
        std::vector<EnemyBox> boxes;
        bool walker = true;
        if (m.c == 'S' || m.c == 'a' || m.c == 'b') boxes.push_back(BLOB);
        else if (m.c == 'G') { boxes.push_back(FLYER); walker = false; }
        else if (m.c == 'B') { if (brute) boxes.push_back(BRUTE); if (blobB) boxes.push_back(BLOB); }
        else continue;
        for (const auto& b : boxes) {
            std::string where = std::string(1, m.c) + " " + at(T, (int)m.x, (int)m.y);
            float x = m.x + 0.5f - b.w / 2, y = m.y + 1.0f - b.h;
            if (solidE(T.level, x, y, b.w, b.h)) {
                en.push_back(where + ": " + b.name + " box (" + (b.w == BRUTE.w ? "1.5 x 1.8" : b.w == BLOB.w ? "0.8 x 0.72" : "0.7 x 0.6") + ") is inside solid tiles");
                continue;
            }
            if (walker) checkWalker(T.level, x, y, b, b.w == BRUTE.w ? 1.2f : 1.7f, en, where);
        }
    }
    for (auto& e : en) R.fails.push_back(e);
    if (!en.empty()) R.enemyFail = true;

    // one-row crawl spaces: an empty cell with solid floor under it and a solid tile right above.
    // The player (1.3 tall) cannot get in, enemies (0.72 tall) can, so blobs hide where nobody can
    // stomp them and fragments there are out of reach. Cells under a "#.#" desk count as one space:
    // headroom-1 cells joined by at most one cell of headroom 2 make a run; a run longer than one
    // tile is flagged.
    for (int y = 1; y < LEVEL_H - 1; ++y) {
        auto solid = [&](int x, int yy) { return T.level.solidFor(x, yy, -1, false); };
        auto head = [&](int x) {   // empty cells from y upwards, 0 if (x, y) is not a floor cell
            if (solid(x, y) || !solid(x, y + 1)) return 0;
            int h = 0;
            for (int yy = y; yy >= 0 && !solid(x, yy); --yy) ++h;
            return yy_cap(h);
        };
        int first = -1, lastLow = -1;
        auto flush = [&]() {
            if (first >= 0 && lastLow > first) {
                char b[200];
                std::snprintf(b, sizeof b, "crawl space %s..x=%d: one empty row between the floor and a solid tile above, the player cannot get in but enemies can",
                              at(T, first, y).c_str(), lastLow - T.x0);
                R.fails.push_back(b);
                R.crawlFail = true;
            }
            first = lastLow = -1;
        };
        for (int x = T.x0; x < T.x0 + T.w; ++x) {
            int h = head(x);
            if (h == 1) {
                if (first < 0 || x - lastLow > 2) { flush(); first = x; }
                lastLow = x;
            } else if (h == 2 && first >= 0 && x - lastLow == 1) {
                // a one-cell gap between two low cells, e.g. the middle of a desk
            } else if (first >= 0 && (h == 0 || h > 2 || x - lastLow >= 2)) flush();
        }
        flush();
    }
    return R;
}

// ------------------------------------------------------------------ files
int gridRowsOf(const std::string& path)
{
    std::ifstream in(path);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty() && line[0] == '#' && (line.size() == 1 || line[1] == ' ')) continue;
        if (line.empty()) continue;
        ++n;
    }
    return std::min(n, LEVEL_H);
}

double now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

template <class F>
void parallel(int n, int threads, F fn)
{
    std::atomic<int> next{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; ++t)
        ts.emplace_back([&]() {
            for (int i; (i = next++) < n;) fn(i);
        });
    for (auto& t : ts) t.join();
}

void printReport(const Report& r, bool verbose)
{
    bool ok = r.fails.empty();
    std::printf("%-4s %-22s %s  (%.1fs)\n", ok ? "ok" : "FAIL", r.name.c_str(), r.head.c_str(), r.seconds);
    for (auto& f : r.fails) std::printf("       - %s\n", f.c_str());
    if (verbose || !r.warns.empty())
        for (auto& w : r.warns) std::printf("       ~ %s\n", w.c_str());
}

}  // namespace

int main(int argc, char** argv)
{
    SetTraceLogLevel(LOG_WARNING);
    std::string dir, only, assets = LEVELCHECK_ASSETS;
    int generated = 0, threads = (int)std::max(1u, std::thread::hardware_concurrency());
    CheckOpts opt;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--dir") dir = next();
        else if (a == "--assets") assets = next();
        else if (a == "--chunk") only = next();
        else if (a == "--generated") generated = std::atoi(next().c_str());
        else if (a == "--quant") g_quant = (float)std::atof(next().c_str());
        else if (a == "--threads") threads = std::max(1, std::atoi(next().c_str()));
        else if (a == "--two") opt.two = true;
        else if (a == "--stats") g_stats = true;
        else if (a == "-v" || a == "--verbose") opt.verbose = true;
        else {
            std::printf("usage: levelcheck [--chunk NAME] [--generated N] [--two] [--quant 0.1] [--threads N] [--assets DIR] [--dir DIR] [-v]\n");
            return a == "-h" || a == "--help" ? 0 : 2;
        }
    }
    if (assets.empty() || !DirectoryExists(assets.c_str())) assets = assetsDir();
    if (dir.empty()) dir = assets + "/chunks";
    if (!config().load(assets + "/config.ini")) std::printf("warning: no %s/config.ini, using the defaults in Player.cpp\n", assets.c_str());
    g_tune.load();

    int failed = 0;
    if (generated > 0) {
        struct Job { int tier, seed; };
        std::vector<Job> jobs;
        for (int tier = 0; tier <= 3; ++tier)
            for (int s = 1; s <= generated; ++s) jobs.push_back({tier, s});
        std::vector<Report> out(jobs.size());
        std::vector<int> stageOf(4, 0);
        for (int t = 0; t <= 3; ++t)
            for (int s = STAGES - 1; s >= 0; --s)
                if (art::TIER[s] == t) stageOf[t] = s;   // the first stage of the tier: fewest abilities
        std::printf("generateChunk: %d seeds for each stage tier 0..3 (abilities of the first stage of each tier, solo)\n", generated);
        parallel((int)jobs.size(), threads, [&](int i) {
            const Job& j = jobs[i];
            int stage = stageOf[j.tier];
            unsigned mask = stageMask(stage, -1);
            std::mt19937 rng((unsigned)j.seed);
            Chunk c = generateChunk(rng, j.tier, mask, std::min(3, 1 + j.tier));
            c.generated = true;
            double t0 = now();
            out[i] = checkChunk(c, LEVEL_H, stage, mask, opt);
            out[i].seconds = now() - t0;
            char nb[64];
            std::snprintf(nb, sizeof nb, "tier%d_seed%d", j.tier, j.seed);
            out[i].name = nb;
        });
        std::printf("\n%-6s %-6s %-8s %-8s %-10s %-10s %-9s %-9s %-6s %-6s\n", "tier", "chunks", "failed", "exit", "soft-lock", "fragment", "power-up", "enemy", "ends", "crawl");
        for (int t = 0; t <= 3; ++t) {
            int n = 0, f = 0, ex = 0, so = 0, fr = 0, pu = 0, en = 0, ends = 0, cr = 0;
            for (size_t i = 0; i < jobs.size(); ++i) {
                if (jobs[i].tier != t) continue;
                const Report& r = out[i];
                ++n;
                if (!r.fails.empty()) ++f;
                ex += r.exitFail; so += r.softFail; fr += r.fragFail; pu += r.pupFail; en += r.enemyFail; ends += r.endFail; cr += r.crawlFail;
            }
            auto pc = [&](int k) { static thread_local char b[12][32]; static thread_local int slot = 0; char* s = b[slot++ % 12]; std::snprintf(s, 32, "%d%%", n ? (int)std::lround(100.0 * k / n) : 0); return s; };
            std::printf("%-6d %-6d %-8s %-8s %-10s %-10s %-9s %-9s %-6s %-6s\n", t, n, pc(f), pc(ex), pc(so), pc(fr), pc(pu), pc(en), pc(ends), pc(cr));
            failed += f;
        }
        std::printf("\nFirst failures per tier (seed = std::mt19937 seed passed to generateChunk):\n");
        for (int t = 0; t <= 3; ++t) {
            int shown = 0;
            for (size_t i = 0; i < jobs.size() && shown < (opt.verbose ? 1000 : 3); ++i)
                if (jobs[i].tier == t && !out[i].fails.empty()) { printReport(out[i], opt.verbose); ++shown; }
        }
    } else {
        std::vector<Chunk> pool;
        std::string log;
        int n = loadChunks(dir, pool, log);
        if (n == 0) { std::printf("no chunks found in %s\n", dir.c_str()); return 2; }
        if (!only.empty())
            pool.erase(std::remove_if(pool.begin(), pool.end(), [&](const Chunk& c) { return c.name != only; }), pool.end());
        std::vector<Report> out(pool.size());
        parallel((int)pool.size(), threads, [&](int i) {
            const Chunk& c = pool[i];
            int stage = -1;
            if (c.name.rfind("intro_stage", 0) == 0) stage = std::atoi(c.name.c_str() + 11);
            else
                for (int s = 0; s < STAGES && stage < 0; ++s)
                    if (allowedIn(c, s)) stage = s;
            double t0 = now();
            if (stage < 0) {
                out[i].name = c.name;
                out[i].head = "never placed (no stage allows it: check min_era, stages, difficulty, abilities, weight)";
                out[i].fails.push_back("not checked: the spawner never places this chunk");
            } else {
                out[i] = checkChunk(c, gridRowsOf(dir + "/" + c.name + ".txt"), stage, stageMask(stage, -1) | c.requires, opt);
            }
            out[i].seconds = now() - t0;
        });
        std::printf("levelcheck: %zu chunks from %s\n", pool.size(), dir.c_str());
        std::printf("Positions are (x, y) inside the chunk file: x = column from the left, y = grid line from the top (0-based).\n\n");
        for (auto& r : out) {
            printReport(r, opt.verbose);
            if (!r.fails.empty()) ++failed;
        }
        std::printf("\n%d of %zu chunks fail.\n", failed, out.size());
    }
    return failed ? 1 : 0;
}
