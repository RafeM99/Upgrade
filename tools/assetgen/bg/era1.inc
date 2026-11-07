// Background scene for stage 1, included into SpriteGen.cpp just before bgLayer().
// 2014, "Arthur's shirts": Gran's back garden on a pale 4-shade handheld screen.
// Exactly the four SHADES4 greys, written straight into the pixels (no quantize, so no checker
// dither). Trees use cluster-built canopies in flat tones: lit top left lightest, shadow darkest.
// Distance is told by value: far things use only the two light greys.

static Col e1_g(int i) { return hex(SHADES4[std::max(0, std::min(3, i))]); }
static void e1_put(Scene& sc, int x, int y, int i)
{
    if (y < 0 || y >= sc.H) return;
    sc.p.set(x, y, e1_g(i));   // Px::set wraps x, so everything tiles
}
static void e1_rect(Scene& sc, float x0, float y0, float w, float h, int i)
{
    for (int y = (int)std::floor(y0); y < (int)std::floor(y0 + h); ++y)
        for (int x = (int)std::floor(x0); x < (int)std::floor(x0 + w); ++x) e1_put(sc, x, y, i);
}
static void e1_tri(Scene& sc, float ax, float ay, float bx, float by, float cx, float cy, int i)
{
    int x0 = (int)std::floor(std::min({ax, bx, cx})), x1 = (int)std::ceil(std::max({ax, bx, cx}));
    int y0 = (int)std::floor(std::min({ay, by, cy})), y1 = (int)std::ceil(std::max({ay, by, cy}));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            float d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by);
            float d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy);
            float d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay);
            bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
            if (!(neg && pos)) e1_put(sc, x, y, i);
        }
}

struct E1Blob { float x, y, r; };

// Shared by era1.inc and era2.inc: shade a canopy made of clumps, clean and hand-drawn looking.
// Each block belongs to the front-most clump that covers it (no jitter, so the edges step
// cleanly). Every clump gets one flat body level from where it sits in the crown (upper left =
// lighter, lower right = darker), a lit cap on its own upper left, a darker underside on its
// lower right, and a one-block shadow line where a clump in front overlaps it. A last pass
// removes single blocks that differ from all their neighbours, so nothing reads as noise.
// out(x, y, level, edge) gets block coordinates (in pixels, top left) and a level 0 lit .. 3 deepest.
template <class Out>
static void clumpCanopy(const std::vector<E1Blob>& bl, int b, int maxLevel, Out out)
{
    if (bl.empty()) return;
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (auto& q : bl) {
        minx = std::min(minx, q.x - q.r); maxx = std::max(maxx, q.x + q.r);
        miny = std::min(miny, q.y - q.r); maxy = std::max(maxy, q.y + q.r);
    }
    int gx0 = (int)std::floor(minx / b) - 1, gx1 = (int)std::ceil(maxx / b) + 1;
    int gy0 = (int)std::floor(miny / b) - 1, gy1 = (int)std::ceil(maxy / b) + 1;
    int gw = gx1 - gx0 + 1, gh = gy1 - gy0 + 1;
    float ccx = (minx + maxx) * 0.5f, ccy = (miny + maxy) * 0.5f;
    float rw = std::max(1.0f, (maxx - minx) * 0.5f), rh = std::max(1.0f, (maxy - miny) * 0.5f);
    std::vector<int> who((size_t)gw * gh, -1), lvl((size_t)gw * gh, -1);
    for (int gy = 0; gy < gh; ++gy)
        for (int gx = 0; gx < gw; ++gx) {
            float mx = (gx + gx0 + 0.5f) * b, my = (gy + gy0 + 0.5f) * b;
            for (int k = (int)bl.size() - 1; k >= 0; --k) {
                float dx = mx - bl[k].x, dy = (my - bl[k].y) * 1.1f;
                if (dx * dx + dy * dy < bl[k].r * bl[k].r) { who[(size_t)gy * gw + gx] = k; break; }
            }
        }
    auto at = [&](const std::vector<int>& v, int gx, int gy) {
        return (gx < 0 || gy < 0 || gx >= gw || gy >= gh) ? -1 : v[(size_t)gy * gw + gx];
    };
    for (int gy = 0; gy < gh; ++gy)
        for (int gx = 0; gx < gw; ++gx) {
            int k = at(who, gx, gy);
            if (k < 0) continue;
            float mx = (gx + gx0 + 0.5f) * b, my = (gy + gy0 + 0.5f) * b;
            float gxn = (mx - ccx) / rw, gyn = (my - ccy) / rh;
            float gpos = 0.5f * gxn + 0.85f * gyn;                 // -1 upper left .. +1 lower right
            float nx = (mx - bl[k].x) / bl[k].r, ny = (my - bl[k].y) / bl[k].r;
            float lt = 0.6f * nx + 0.8f * ny;                     // the clump's own light
            int l = 1;
            if (lt < -0.3f && gpos < 0.35f) l = 0;                // lit cap of each clump
            else if (lt > 0.3f || gpos > 0.55f) l = 2;            // its underside, and the shaded side
            if (l == 2 && gpos > 0.45f && lt > 0.0f) l = 3;       // deepest, lower right only
            // the top row of a clump that sits in front of another: always its lit cap
            int up = at(who, gx, gy - 1);
            if (up >= 0 && up < k && lt < 0 && gpos < 0.6f) l = std::min(l, gpos < 0.1f ? 0 : 1);
            lvl[(size_t)gy * gw + gx] = std::max(0, std::min(maxLevel, l));
        }
    // clean-up: a block unlike all four neighbours takes their most common level
    std::vector<int> fin = lvl;
    for (int gy = 0; gy < gh; ++gy)
        for (int gx = 0; gx < gw; ++gx) {
            int l = at(lvl, gx, gy);
            if (l < 0) continue;
            int n[4] = {at(lvl, gx - 1, gy), at(lvl, gx + 1, gy), at(lvl, gx, gy - 1), at(lvl, gx, gy + 1)};
            int cnt[4] = {0, 0, 0, 0}, same = 0, inside = 0;
            for (int v : n)
                if (v >= 0) { ++cnt[v]; ++inside; if (v == l) ++same; }
            if (inside >= 3 && same == 0) {
                int best = l;
                for (int v = 0; v < 4; ++v) if (cnt[v] > (best == l ? 0 : cnt[best])) best = v;
                fin[(size_t)gy * gw + gx] = best;
            }
        }
    for (int gy = 0; gy < gh; ++gy)
        for (int gx = 0; gx < gw; ++gx) {
            int l = at(fin, gx, gy);
            if (l < 0) continue;
            bool edge = at(who, gx - 1, gy) < 0 || at(who, gx + 1, gy) < 0 || at(who, gx, gy - 1) < 0 || at(who, gx, gy + 1) < 0;
            out((gx + gx0) * b, (gy + gy0) * b, l, edge);
        }
}

// A canopy in the four greys. tone[0..3] = lit, mid, shadow, deepest (deepest may be -1: then
// the canopy uses three levels). rim is kept for the callers; the clean shading keeps the
// silhouette on its own.
static void e1_canopy(Scene& sc, const std::vector<E1Blob>& bl, int b, const int tone[4], int seed, bool rim)
{
    (void)seed;
    int maxL = tone[3] < 0 ? 2 : 3;
    clumpCanopy(bl, b, maxL, [&](int x, int y, int l, bool edge) {
        if (rim && edge && l == 0) l = 1;   // keep the silhouette against the pale sky
        e1_rect(sc, (float)x, (float)y, (float)b, (float)b, tone[l]);
    });
}

// Tree kinds: 0 round, 1 tall oval, 2 pine (stepped tiers), 3 forked (two crowns), 4 wide round,
// 5 tall narrow. R is the crown radius in pixels. trunkL/trunkD are the lit and dark trunk tones.
static void e1_tree(Scene& sc, float x, float base, int kind, float R, int b, const int tone[4], int trunkL, int trunkD,
                    int seed, bool tuft)
{
    float tw = std::max(2.0f, std::round(R * 0.26f / b) * b);
    auto trunk = [&](float tx, float top) {
        e1_rect(sc, tx - tw * 0.5f, top, tw * 0.5f, base - top, trunkL);
        e1_rect(sc, tx, top, tw * 0.5f, base - top, trunkD);
    };
    std::vector<E1Blob> bl;
    auto crown = [&](float cx, float cy, float rx, float ry) {   // many small clumps filling the crown
        std::vector<E1Blob> c;
        float m = std::min(rx, ry);
        c.push_back({cx, cy, m * 0.6f});
        // two rings of clumps, the outer one making the scalloped silhouette
        for (int ring = 0; ring < 2; ++ring) {
            int n = ring == 0 ? 6 : 9 + (ry > rx * 1.2f ? 3 : 0);
            float d = ring == 0 ? 0.42f : 0.74f;
            for (int i = 0; i < n; ++i) {
                float a = (i + 0.5f * ring) * 6.2831853f / n + (rnd(i, seed + ring, 61) - 0.5f) * 0.5f;
                float rr = m * (0.26f + 0.1f * rnd(i, seed + ring, 62));
                c.push_back({cx + std::cos(a) * rx * d, cy + std::sin(a) * ry * d, rr});
            }
        }
        // upper clumps first, lower ones in front: each clump shows its lit top like a bump
        std::sort(c.begin() + 1, c.end(), [&](const E1Blob& p, const E1Blob& q) { return p.y < q.y; });
        for (auto& q : c) bl.push_back(q);
    };
    if (kind == 2) {   // pine
        float th = R * 0.55f;
        trunk(x, base - th - b);
        int tiers = 4;
        float top = base - th - R * 2.6f, tierH = R * 2.6f / tiers;
        for (int t = 0; t < tiers; ++t) {
            float ty0 = top + t * tierH * 0.8f, ty1 = ty0 + tierH * 1.35f;
            float halfMax = R * (0.45f + 0.55f * (t + 1) / tiers);
            for (float y = std::floor(ty0 / b) * b; y < ty1; y += b) {
                float f = (y - ty0) / (ty1 - ty0);
                float half = std::max((float)b, std::round(halfMax * f / b) * b);
                if (rnd((int)y, t, seed) < 0.5f) half += b;   // jagged needles
                for (float xx = x - half; xx < x + half; xx += b) {
                    float u = (xx + b * 0.5f - x) / half;
                    float tt = u * 0.9f + f * 0.7f;
                    int lv = tt < -0.15f ? 0 : (tt < 0.55f ? 1 : 2);
                    if (y + b >= ty1) lv = std::min(3, lv + 1);
                    if (tone[lv] < 0) lv = 2;
                    e1_rect(sc, std::floor(xx / b) * b, y, (float)b, (float)b, tone[lv]);
                }
            }
        }
        e1_rect(sc, std::floor(x / b) * b - b * 0.5f, top - 2 * b, (float)b, 2.0f * b, tone[1]);
    } else if (kind == 3) {   // forked: two crowns on a split trunk
        float fy = base - R * 0.9f;
        trunk(x, fy);
        float ax = x - R * 0.85f, bx2 = x + R * 0.95f, ay = fy - R * 0.9f, by2 = fy - R * 0.55f;
        for (int i = 0; i <= 20; ++i) {
            float f = i / 20.0f;
            e1_rect(sc, std::floor((x + (ax - x) * f) / b) * b - b * 0.5f, std::floor((fy + (ay - fy) * f) / b) * b, (float)b, (float)b, trunkL);
            e1_rect(sc, std::floor((x + (bx2 - x) * f) / b) * b, std::floor((fy + (by2 - fy) * f) / b) * b, (float)b, (float)b, trunkD);
        }
        crown(bx2, by2 - R * 0.55f, R * 0.75f, R * 0.7f);
        crown(ax, ay - R * 0.6f, R * 0.9f, R * 0.85f);
        e1_canopy(sc, bl, b, tone, seed, true);
    } else {
        float rx = R, ry = R;
        if (kind == 1) ry = R * 1.45f;
        if (kind == 4) { rx = R * 1.25f; ry = R * 0.95f; }
        if (kind == 5) { rx = R * 0.72f; ry = R * 1.7f; }
        float cy = base - R * 0.75f - ry;
        trunk(x, cy + ry * 0.5f);
        crown(x, cy, rx, ry);
        e1_canopy(sc, bl, b, tone, seed, true);
    }
    if (tuft) {   // a little grass tuft at the foot
        for (int i = -3; i <= 3; ++i) {
            float h = (1 + (int)(rnd(i, seed, 41) * 2.5f)) * (float)b;
            e1_rect(sc, std::floor(x / b) * b + i * b, base - h, (float)b, h, (i + seed) % 2 ? tone[1] : tone[2]);
        }
    }
}

// A cloud of blocks with a flat, shaded underside. Every cloud is its own shape: the number,
// size and spacing of its puffs come from `seed`, and `stretch` widens it into a long bank.
static void e1_cloud(Scene& sc, float cx, float cy, float size, int b, int seed, float stretch)
{
    struct P { float x, y, r; };
    std::vector<P> puffs;
    int n = 2 + (int)(rnd(seed, 1, 90) * 4);
    float span = size * (1.2f + stretch);
    int big = (int)(rnd(seed, 2, 90) * n);
    for (int i = 0; i < n; ++i) {
        float f = n == 1 ? 0 : (float)i / (n - 1) * 2 - 1;
        float r = size * (0.45f + 0.35f * rnd(seed, 10 + i, 90)) * (i == big ? 1.45f : 1.0f);
        puffs.push_back({cx + f * span + (rnd(seed, 20 + i, 90) - 0.5f) * size * 0.5f, cy + size * 0.35f - r * 0.75f, r});
    }
    float bottom = cy + size * 0.4f;
    int gx0 = (int)std::floor((cx - span - size * 2) / b), gx1 = (int)std::ceil((cx + span + size * 2) / b);
    int gy0 = (int)std::floor((cy - size * 2.5f) / b), gy1 = (int)std::ceil(bottom / b) + 1;
    auto in = [&](int gx, int gy) {
        float mx = (gx + 0.5f) * b, my = (gy + 0.5f) * b;
        if (my > bottom) return false;
        for (auto& p : puffs)
            if ((mx - p.x) * (mx - p.x) + (my - p.y) * (my - p.y) * 1.6f < p.r * p.r) return true;
        return false;
    };
    for (int gy = gy0; gy <= gy1; ++gy)
        for (int gx = gx0; gx <= gx1; ++gx) {
            if (!in(gx, gy)) continue;
            bool under = !in(gx, gy + 1) || (!in(gx, gy + 2) && ((gx + seed) % 3 != 0));
            bool edgeTop = !in(gx, gy - 1) || !in(gx - 1, gy) || !in(gx + 1, gy);
            e1_rect(sc, (float)gx * b, (float)gy * b, (float)b, (float)b, under || edgeTop ? 2 : 3);
        }
}

bool bgEra1(Scene& sc, int layer)
{
    const int W = sc.W, H = sc.H;
    const float s = sc.s;                 // 2 px per 1-bit pixel
    const int b = std::max(1, (int)std::lround(s));
    sc.fade = 0;

    if (layer == 0) {   // sky: palest grey, stepped clouds, a pale sun
        e1_rect(sc, 0, 0, (float)W, (float)H, 3);
        float sx = W * 0.8f, sy = H * 0.21f, sr = 10 * s * 0.5f + 4;
        for (int y = (int)(sy - sr - 2); y <= (int)(sy + sr + 2); ++y)
            for (int x = (int)(sx - sr - 2); x <= (int)(sx + sr + 2); ++x) {
                float d = std::hypot((std::floor(x / (float)b) + 0.5f) * b - sx, (std::floor(y / (float)b) + 0.5f) * b - sy);
                if (d < sr && d > sr - b) e1_put(sc, x, y, 2);
            }
        // clouds kept below the top tenth (where the HUD can sit), each a different shape
        const float cl[6][4] = {{0.06f, 0.2f, 6, 0.6f}, {0.25f, 0.27f, 4, 1.8f}, {0.44f, 0.17f, 7, 0.2f},
                                {0.6f, 0.3f, 3.5f, 0.9f}, {0.9f, 0.24f, 5, 2.6f}, {0.72f, 0.16f, 3, 0.4f}};
        int ci = 0;
        for (auto& c : cl) e1_cloud(sc, W * c[0], H * c[1], c[2] * s, b, 31 + 7 * ci++, c[3]);
        // thin streaks low in the sky, each its own length
        for (int i = 0; i < 5; ++i) {
            float y = std::floor(H * (0.37f + 0.03f * (i % 3)) / b) * b, x = W * (0.1f + i * 0.21f + 0.04f * rnd(i, 1, 33));
            float l1 = (8 + 14 * rnd(i, 2, 33)) * s, l2 = (4 + 8 * rnd(i, 3, 33)) * s;
            e1_rect(sc, std::floor(x / b) * b, y, std::floor(l1 / b) * b, (float)b, 2);
            e1_rect(sc, std::floor((x + l1 * 0.3f) / b) * b, y + b, std::floor(l2 / b) * b, (float)b, 2);
        }
        return true;
    }

    if (layer == 1) {   // far: mountains, a light hill with a pine band, nearer rolling fields
        // mountains: pale-mid body, lit left faces in the palest grey
        auto mtop = [&](int x) {
            int sx = (x / (2 * b)) * (2 * b);
            float t = H * 0.44f + ridge((float)sx, W, 4, 9 * s, 6 * s, 3 * s);
            return (int)(std::floor(t / b) * b);
        };
        for (int x = 0; x < W; ++x) {
            int top = mtop(x);
            int prev = mtop((x - 6 * b + W) % W), next = mtop((x + 6 * b) % W);
            bool shadeFace = next > prev + b;   // falling to the right: the face turned from the sun
            for (int y = top; y < H; ++y) {
                int depth = y - top;
                bool crest = depth < b;
                bool shd = shadeFace && depth < (int)((5 + 5 * rnd(x / (4 * b), 0, 3)) * s);
                e1_put(sc, x, y, crest || shd ? 2 : 3);
            }
        }
        // first hill: palest, with a dense band of small pines along its crest
        auto htop = [&](int x, float base, int seed, float a) {
            int sx = (x / (2 * b)) * (2 * b);
            return (int)(std::floor((H * base + ridge((float)sx, W, seed, a, a * 0.4f, a * 0.15f)) / b) * b);
        };
        for (int x = 0; x < W; ++x)
            for (int y = htop(x, 0.58f, 7, 4 * s); y < H; ++y) e1_put(sc, x, y, 3);
        for (int x = 0; x < W; x += 3 * b) {
            if (rnd(x, 1, 8) < 0.25f) continue;
            int base = htop(x, 0.58f, 7, 4 * s) + 2 * b;
            int h = (int)((6 + rnd(x, 2, 8) * 5) * s);
            for (int y = 0; y < h; y += b) {
                int half = (int)(std::floor((y * 0.45f + b) / b) * b);
                if (((y / b) % 3) == 0) half = std::max(b, half - b);   // stepped tiers
                for (int xx = -half; xx < half; xx += b)
                    e1_rect(sc, (float)(x + xx), (float)(base - h + y), (float)b, (float)b, (xx == -half && y > 2 * b) ? 3 : 2);
            }
        }
        // nearer fields: mid grey with a palest crest and a few hedge lines
        for (int x = 0; x < W; ++x) {
            int top = htop(x, 0.66f, 11, 3 * s);
            for (int y = top; y < H; ++y) e1_put(sc, x, y, y < top + b ? 3 : 2);
        }
        for (int i = 0; i < 7; ++i) {
            float x0 = W * (0.03f + i * 0.142f), y = std::floor(H * (0.72f + 0.02f * (i % 3)) / b) * b;
            for (float x = x0; x < x0 + 30 * s; x += b)
                e1_rect(sc, x, y + ((int)(x / (6 * b)) % 2) * b, (float)b, (float)b, 3);
        }
        return true;
    }

    if (layer == 2) {   // mid: hedge, Gran's cottage, trees (no darkest grey here)
        const int tone[4] = {3, 2, 1, -1};
        float g = std::floor(H * 0.8f / b) * b;
        e1_rect(sc, 0, g, (float)W, H - g, 2);
        e1_rect(sc, 0, g, (float)W, (float)b, 3);
        // the hedge: a long row of small clusters, broken around the cottage and for gaps
        const float hedge[4][2] = {{-0.02f, 0.24f}, {0.28f, 0.47f}, {0.67f, 0.9f}, {0.93f, 0.98f}};
        for (int h = 0; h < 4; ++h) {
            std::vector<E1Blob> hb;
            for (float x = W * hedge[h][0]; x < W * hedge[h][1]; x += 7 * s)
                hb.push_back({x, g - (3.5f + 2.0f * rnd((int)x, h, 77)) * s, (4.5f + 1.5f * rnd((int)x, h, 78)) * s});
            for (float x = W * hedge[h][0] + 3 * s; x < W * hedge[h][1]; x += 9 * s) hb.push_back({x, g - 1.5f * s, 4.0f * s});
            const int hedgeTone[4] = {2, 2, 1, -1};   // the hedge: calm, one step darker than the bushes
            e1_canopy(sc, hb, b, hedgeTone, 70 + h, true);
        }
        // Gran's cottage
        float cx = std::floor(W * 0.5f / b) * b, cw = 66 * b, ch = 34 * b;
        float roofY = g - ch;
        e1_rect(sc, cx, roofY, cw, ch, 3);
        e1_rect(sc, cx + cw * 0.82f, roofY, cw * 0.18f, ch, 2);
        for (int r = 0; r < 6; ++r)   // stone courses
            for (int k = 0; k < 7; ++k)
                if (rnd(r, k, 31) < 0.45f) e1_rect(sc, cx + (k * 9 + (r % 2) * 4 + 2) * b, roofY + (r * 5 + 3) * b, 4.0f * b, (float)b, 2);
        float peak = roofY - 26 * b;
        e1_tri(sc, cx - 5 * b, roofY + b, cx + cw * 0.5f, peak, cx + cw * 0.5f, roofY + b, 2);
        e1_tri(sc, cx + cw * 0.5f, peak, cx + cw + 5 * b, roofY + b, cx + cw * 0.5f, roofY + b, 1);
        for (int r = 1; r < 6; ++r) {   // tile rows
            float y = peak + (roofY - peak) * r / 6.0f;
            float half = (cw * 0.5f + 5 * b) * r / 6.0f;
            e1_rect(sc, cx + cw * 0.5f - half + b, std::floor(y / b) * b, half - b, (float)b, 1);
            e1_rect(sc, cx + cw * 0.5f, std::floor(y / b) * b, half - b, (float)b, 0);
        }
        e1_rect(sc, cx - 5 * b, roofY, cw + 10 * b, (float)b, 1);   // eaves
        // chimney and a curl of smoke
        e1_rect(sc, cx + cw * 0.68f, peak + 6 * b, 6.0f * b, 12.0f * b, 2);
        e1_rect(sc, cx + cw * 0.68f + 4 * b, peak + 6 * b, 2.0f * b, 12.0f * b, 1);
        e1_rect(sc, cx + cw * 0.68f - b, peak + 5 * b, 8.0f * b, (float)b, 1);
        for (int i = 0; i < 4; ++i) {
            float px = cx + cw * 0.68f + (3 + i * 3) * b, py = peak + (1 - i * 4) * b, r = (2 + i * 0.6f) * b;
            for (float yy = py - r; yy < py + r; yy += b)
                for (float xx = px - r; xx < px + r; xx += b)
                    if ((xx - px) * (xx - px) + (yy - py) * (yy - py) < r * r)
                        e1_rect(sc, std::floor(xx / b) * b, std::floor(yy / b) * b, (float)b, (float)b, yy > py ? 2 : 3);
        }
        // windows with a cross frame and flower boxes, a door with a step
        for (int wdw = 0; wdw < 2; ++wdw) {
            float wx = cx + (wdw == 0 ? 8 : 44) * b, wy = roofY + 8 * b;
            e1_rect(sc, wx, wy, 12.0f * b, 10.0f * b, 1);
            e1_rect(sc, wx + 5 * b, wy, 2.0f * b, 10.0f * b, 3);
            e1_rect(sc, wx, wy + 4 * b, 12.0f * b, (float)b, 3);
            e1_rect(sc, wx + b, wy + b, 2.0f * b, 2.0f * b, 2);   // glint
            e1_rect(sc, wx - b, wy + 10 * b, 14.0f * b, 2.0f * b, 1);
            for (int k = 0; k < 6; ++k) e1_rect(sc, wx + k * 2 * b, wy + 9 * b - (k % 2) * b, (float)b, (float)b, 2);
        }
        e1_rect(sc, cx + 26 * b, roofY + 14 * b, 12.0f * b, ch - 14 * b, 1);
        e1_rect(sc, cx + 26 * b, roofY + 14 * b, 12.0f * b, (float)b, 0);
        e1_rect(sc, cx + 35 * b, roofY + 24 * b, (float)b, (float)b, 3);   // handle
        e1_rect(sc, cx + 24 * b, g - b, 16.0f * b, (float)b, 2);
        // trees behind the hedge
        e1_tree(sc, W * 0.13f, g - 2 * b, 0, 15 * s, b, tone, 2, 1, 11, false);
        e1_tree(sc, W * 0.34f, g - 2 * b, 3, 12 * s, b, tone, 2, 1, 12, false);
        e1_tree(sc, W * 0.42f, g - 2 * b, 2, 12 * s, b, tone, 2, 1, 13, false);
        e1_tree(sc, W * 0.78f, g - 2 * b, 1, 11 * s, b, tone, 2, 1, 14, false);
        e1_tree(sc, W * 0.96f, g - 2 * b, 5, 10 * s, b, tone, 2, 1, 15, false);
        return true;
    }

    // near: fence, the washing line with Arthur's shirts, two big trees, bushes
    const int tone[4] = {3, 2, 1, 0};
    float g = std::floor(H * 0.86f / b) * b;
    // picket fence sections
    const float fenceAt[3] = {0.0f, 0.53f, 0.75f};
    for (int seg = 0; seg < 3; ++seg) {
        float fx = std::floor(W * fenceAt[seg] / b) * b;
        float fw = (seg == 0 ? 60 : 44) * s;
        e1_rect(sc, fx, g - 9 * s, fw, (float)b, 2);
        e1_rect(sc, fx, g - 4 * s, fw, (float)b, 2);
        for (float k = 0; k < fw; k += 4 * s) {
            float h = 12 * s;
            e1_rect(sc, fx + k, g - h + b, 2.0f * b, h - b, 3);
            e1_rect(sc, fx + k + b, g - h + b, (float)b, h - b, 2);
            e1_rect(sc, fx + k + b * 0.5f, g - h, (float)b, (float)b, 3);
        }
    }
    // the washing line: two tall posts and a sagging line, high above the players' heads, so
    // the small light washing hangs well above where Gran and Sam run and the platforms sit apart
    float wx0 = std::floor(W * 0.2f / b) * b, wx1 = std::floor(W * 0.47f / b) * b, top = std::floor((g - 46 * s) / b) * b;
    for (float px : {wx0, wx1}) {
        e1_rect(sc, px, top - 2 * b, (float)b, g - top + 2 * b, 2);
        e1_rect(sc, px + b, top - 2 * b, (float)b, g - top + 2 * b, 1);
        e1_rect(sc, px - 2 * b, top, 6.0f * b, (float)b, 1);
    }
    auto lineY = [&](float x) { float f = (x - wx0) / (wx1 - wx0); return std::floor((top + b + 5 * s * 4 * f * (1 - f)) / b) * b; };
    for (float x = wx0 + 2 * b; x < wx1; x += b) e1_rect(sc, x, lineY(x), (float)b, (float)b, 2);
    // the washing, each piece different: shirts of two sizes (one mirrored), socks, a towel.
    // Light greys only, outlined in the light-mid grey, so it stays behind the characters.
    auto px_ = [&](float sx, float sy, int u, int v, int t) { e1_rect(sc, sx + u * b, sy + v * b, (float)b, (float)b, t); };
    auto peg = [&](float sx, float sy, int u) { e1_rect(sc, sx + u * b, sy - b, (float)b, 2.0f * b, 1); };
    struct Item { float at; int kind, hh, ln; bool flip; };
    const Item items[5] = {{0.12f, 0, 3, 7, false}, {0.3f, 1, 0, 3, false}, {0.47f, 0, 4, 8, true}, {0.68f, 2, 3, 6, false}, {0.88f, 0, 3, 6, true}};
    for (int i = 0; i < 5; ++i) {
        const Item& it = items[i];
        float sx = std::floor((wx0 + (wx1 - wx0) * it.at) / b) * b, sy = lineY(sx) + b;
        int f = it.flip ? -1 : 1;
        if (it.kind == 0) {   // a shirt: collar, short sleeves, placket, a fold on one side
            int hh = it.hh, ln = it.ln;
            auto inside = [&](int u, int v) {
                int au = std::abs(u);
                if (v < 0) return false;
                if (au < hh) return v < ln;
                return au < hh + 2 && v < 3 - (au - hh);
            };
            for (int v = -1; v <= ln; ++v)
                for (int u = -hh - 3; u <= hh + 3; ++u) {
                    int t = -1;
                    if (inside(u, v)) {
                        t = 3;
                        if (v == 0 && std::abs(u) < 2) t = 2;                     // collar
                        else if (u == 0 && v > 0) t = (v % 2) ? 3 : 2;             // placket and buttons
                        else if (u * f == hh - 1 && v >= 2) t = 2;                 // fold in shadow
                    } else if (inside(u - 1, v) || inside(u + 1, v) || inside(u, v + 1) || inside(u, v - 1)) t = 2;
                    if (t >= 0) px_(sx, sy, u, v, t);
                }
            peg(sx, sy, -hh + 1); peg(sx, sy, hh - 1);
        } else if (it.kind == 1) {   // a pair of socks, one a step lower
            for (int k = 0; k < 2; ++k) {
                int ox = k * 3, ln = it.ln + k;
                for (int v = 0; v <= ln; ++v) { px_(sx, sy, ox - 1, v, 2); px_(sx, sy, ox, v, v == ln ? 2 : 3); }
                px_(sx, sy, ox + 1, ln, 2); px_(sx, sy, ox + 1, ln - 1, 2);
                peg(sx, sy, ox);
            }
        } else {   // a striped tea towel
            for (int v = 0; v <= it.ln; ++v)
                for (int u = -it.hh; u <= it.hh; ++u) {
                    bool edge = std::abs(u) == it.hh || v == it.ln;
                    px_(sx, sy, u, v, edge ? 2 : (v == 2 || v == it.ln - 2) ? 2 : 3);
                }
            peg(sx, sy, -it.hh + 1); peg(sx, sy, it.hh - 1);
        }
    }
    // bushes along the fence foot
    const float bushAt[5] = {0.06f, 0.19f, 0.48f, 0.68f, 0.84f};
    for (int i = 0; i < 5; ++i) {
        float x = W * bushAt[i];
        float k = (0.7f + 0.55f * rnd(i, 1, 51)) * s, f = (i % 2) ? -1.0f : 1.0f;   // per-bush size and mirror
        std::vector<E1Blob> bb = {{x + 6 * k * f, g - 3 * k, 5 * k}, {x - 5 * k * f, g - 3 * k, 5 * k}, {x, g - 6 * k, 6 * k},
                                  {x - 3 * k * f, g - 8 * k, 4 * k}};
        if (i % 3 == 1) bb.push_back({x + 10 * k * f, g - 2.5f * k, 3.5f * k});
        const int soft[4] = {3, 2, 2, -1};   // calm and light down by the players' feet
        e1_canopy(sc, bb, b, soft, 50 + i, true);
    }
    // two big trees, near enough to show every cluster
    e1_tree(sc, W * 0.1f, g, 4, 20 * s, b, tone, 1, 0, 21, true);
    e1_tree(sc, W * 0.9f, g, 1, 17 * s, b, tone, 1, 0, 22, true);
    return true;
}
