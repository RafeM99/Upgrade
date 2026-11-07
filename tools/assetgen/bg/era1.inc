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

// A canopy built from blocky clusters. Later blobs sit in front; a front cluster casts a one-block
// shadow on the cluster behind it, which is what makes the clumps read. tone[0..3] = lit, mid,
// shadow, deepest (deepest may be -1 for none). rim: lit blocks on the outer edge drop one tone so
// the crown keeps its silhouette against a light sky.
static void e1_canopy(Scene& sc, const std::vector<E1Blob>& bl, int b, const int tone[4], int seed, bool rim)
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
    float ccx = (minx + maxx) * 0.5f, ccy = (miny + maxy) * 0.5f, rh = std::max(1.0f, (maxy - miny) * 0.5f);
    float rw = std::max(1.0f, (maxx - minx) * 0.5f);
    std::vector<int> who((size_t)gw * gh, -1);
    for (int gy = 0; gy < gh; ++gy)
        for (int gx = 0; gx < gw; ++gx) {
            float mx = (gx + gx0 + 0.5f) * b, my = (gy + gy0 + 0.5f) * b;
            for (int k = (int)bl.size() - 1; k >= 0; --k) {
                float j = 1.0f + 0.08f * (rnd(gx + gx0, gy + gy0, seed + k * 7) - 0.5f);
                float dx = mx - bl[k].x, dy = (my - bl[k].y) * 1.08f, r = bl[k].r * j;
                if (dx * dx + dy * dy < r * r) { who[(size_t)gy * gw + gx] = k; break; }
            }
        }
    auto at = [&](int gx, int gy) { return (gx < 0 || gy < 0 || gx >= gw || gy >= gh) ? -1 : who[(size_t)gy * gw + gx]; };
    for (int gy = 0; gy < gh; ++gy)
        for (int gx = 0; gx < gw; ++gx) {
            int k = at(gx, gy);
            if (k < 0) continue;
            float mx = (gx + gx0 + 0.5f) * b, my = (gy + gy0 + 0.5f) * b;
            float nx = (mx - bl[k].x) / bl[k].r, ny = (my - bl[k].y) / bl[k].r;
            // tone = the crown's light direction, bent by the clump each block belongs to, so the
            // lit / mid / shadow boundaries are scalloped along the clumps
            float gx_ = (mx - ccx) / rw, gy_ = (my - ccy) / rh;
            float t = 0.5f * gx_ + 0.85f * gy_ + 0.5f * (0.6f * nx + 0.8f * ny);
            int lv = t < 0.2f ? 1 : (t < 0.8f ? 2 : 3);
            float lt = 0.6f * nx + 0.8f * ny;
            if (lt < -0.45f && t < -0.1f) lv = 0;   // each clump's lit cap on the sunny side
            bool edge = at(gx - 1, gy) < 0 || at(gx + 1, gy) < 0 || at(gx, gy - 1) < 0 || at(gx, gy + 1) < 0;
            if (rim && edge && lv == 0) lv = 1;
            if (tone[lv] < 0) lv = 2;
            e1_rect(sc, (float)(gx + gx0) * b, (float)(gy + gy0) * b, (float)b, (float)b, tone[lv]);
        }
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
    auto crown = [&](float cx, float cy, float rx, float ry) {   // a ring of clumps round a core
        std::vector<E1Blob> c;
        c.push_back({cx, cy, std::min(rx, ry) * 0.7f});
        int n = 7 + (ry > rx * 1.2f ? 3 : 0);
        for (int i = 0; i < n; ++i) {
            float a = i * 6.2831853f / n + rnd(i, seed, 61) * 0.5f;
            float rr = std::min(rx, ry) * (0.38f + 0.14f * rnd(i, seed, 62));
            c.push_back({cx + std::cos(a) * (rx - rr * 0.95f), cy + std::sin(a) * (ry - rr * 0.95f), rr});
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

// A cloud of blocks with a flat, shaded underside.
static void e1_cloud(Scene& sc, float cx, float cy, float size, int b)
{
    int gx0 = (int)std::floor((cx - size * 2.6f) / b), gx1 = (int)std::ceil((cx + size * 2.6f) / b);
    int gy0 = (int)std::floor((cy - size * 1.5f) / b), gy1 = (int)std::ceil((cy + size) / b);
    auto in = [&](int gx, int gy) {
        float mx = (gx + 0.5f) * b, my = (gy + 0.5f) * b;
        if (my > cy + size * 0.5f) return false;
        auto e = [&](float ox, float oy, float r) { return (mx - ox) * (mx - ox) + (my - oy) * (my - oy) * 2.2f < r * r; };
        return e(cx, cy - size * 0.25f, size) || e(cx - size * 1.15f, cy + size * 0.1f, size * 0.7f) ||
               e(cx + size * 1.25f, cy + size * 0.15f, size * 0.62f) || e(cx + size * 0.5f, cy - size * 0.55f, size * 0.55f);
    };
    for (int gy = gy0; gy <= gy1; ++gy)
        for (int gx = gx0; gx <= gx1; ++gx) {
            if (!in(gx, gy)) continue;
            float my = (gy + 0.5f) * b;
            bool under = my > cy + size * 0.18f || !in(gx, gy + 2);
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
        float sx = W * 0.8f, sy = H * 0.15f, sr = 10 * s * 0.5f + 4;
        for (int y = (int)(sy - sr - 2); y <= (int)(sy + sr + 2); ++y)
            for (int x = (int)(sx - sr - 2); x <= (int)(sx + sr + 2); ++x) {
                float d = std::hypot((std::floor(x / (float)b) + 0.5f) * b - sx, (std::floor(y / (float)b) + 0.5f) * b - sy);
                if (d < sr && d > sr - b) e1_put(sc, x, y, 2);
            }
        const float cl[6][3] = {{0.06f, 0.13f, 7}, {0.27f, 0.22f, 5}, {0.45f, 0.1f, 8}, {0.63f, 0.25f, 4.5f}, {0.93f, 0.3f, 4}, {0.74f, 0.08f, 3.5f}};
        for (auto& c : cl) e1_cloud(sc, W * c[0], H * c[1], c[2] * s, b * 2);
        // thin streaks low in the sky
        for (int i = 0; i < 5; ++i) {
            float y = std::floor(H * (0.36f + 0.03f * (i % 3)) / b) * b, x = W * (0.1f + i * 0.21f);
            e1_rect(sc, x, y, (14 + 6 * (i % 2)) * s, (float)b, 2);
            e1_rect(sc, x + 6 * s, y + b, 8 * s, (float)b, 2);
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
            e1_canopy(sc, hb, 2 * b, tone, 70 + h, true);
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
            e1_rect(sc, cx + cw * 0.5f - half + b, std::floor(y / b) * b, half - b, 1, 1);
            e1_rect(sc, cx + cw * 0.5f, std::floor(y / b) * b, half - b, 1, 0);
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
        e1_tree(sc, W * 0.13f, g - 2 * b, 0, 15 * s, 2 * b, tone, 2, 1, 11, false);
        e1_tree(sc, W * 0.34f, g - 2 * b, 3, 12 * s, 2 * b, tone, 2, 1, 12, false);
        e1_tree(sc, W * 0.42f, g - 2 * b, 2, 12 * s, 2 * b, tone, 2, 1, 13, false);
        e1_tree(sc, W * 0.78f, g - 2 * b, 1, 11 * s, 2 * b, tone, 2, 1, 14, false);
        e1_tree(sc, W * 0.96f, g - 2 * b, 5, 10 * s, 2 * b, tone, 2, 1, 15, false);
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
    // the washing line: two posts and a sagging line
    float wx0 = std::floor(W * 0.2f / b) * b, wx1 = std::floor(W * 0.47f / b) * b, top = g - 42 * s;
    for (float px : {wx0, wx1}) {
        e1_rect(sc, px, top - 2 * b, (float)b, g - top + 2 * b, 2);
        e1_rect(sc, px + b, top - 2 * b, (float)b, g - top + 2 * b, 1);
        e1_rect(sc, px - 2 * b, top, 6.0f * b, (float)b, 1);
    }
    auto lineY = [&](float x) { float f = (x - wx0) / (wx1 - wx0); return std::floor((top + 1 + 7 * s * 4 * f * (1 - f)) / b) * b; };
    for (float x = wx0 + 2 * b; x < wx1; x += b) e1_rect(sc, x, lineY(x), (float)b, 1, 1);
    // shirts: collar, sleeves, body, a fold in shadow on the right; pegs on the line
    const float shirtAt[3] = {0.19f, 0.5f, 0.81f};
    for (int i = 0; i < 3; ++i) {
        float sx = std::floor((wx0 + (wx1 - wx0) * shirtAt[i]) / b) * b, sy = lineY(sx);
        int body = (i % 2) ? 2 : 3, shade = body - 1;
        int hh = i == 1 ? 7 : 6, ln = i == 1 ? 18 : 16, sway = (i % 2) ? 1 : -1;
        auto inside = [&](int u, int v) {
            int au = std::abs(u);
            if (v < 1 && !(v == 0 && au < 3)) return false;
            if (au < hh) return v < ln - 1 || (v == ln - 1 && std::abs(u - sway) < hh);
            return au < hh + 4 && v < 8 - (au - hh);   // sleeves, drooping at the cuff
        };
        auto toneAt = [&](int u, int v) {
            int au = std::abs(u);
            if (v == 0) return shade;                                   // collar
            if (au >= hh) return (u > 0 || !inside(u, v + 1)) ? shade : body;   // sleeve shadow, cuffs
            if (u == 0) return (v % 4 == 1) ? 1 : shade;                // placket and buttons
            if (u >= hh - 3 && u < hh - 1 && v >= 3) return shade;     // fold
            if (u <= -hh + 3 && u >= -hh + 1 && v >= 4 && v < 7) return shade;   // pocket
            return body;
        };
        for (int v = -1; v <= ln; ++v)
            for (int u = -hh - 5; u <= hh + 5; ++u) {
                int t = -1;
                if (inside(u, v)) t = toneAt(u, v);
                else if (inside(u - 1, v) || inside(u + 1, v) || inside(u, v + 1) || inside(u, v - 1)) t = 1;
                if (t >= 0) e1_rect(sc, sx + u * b, sy + v * b, (float)b, (float)b, t);
            }
        e1_rect(sc, sx - (hh - 1) * b, sy - b, (float)b, 2.0f * b, 0);   // pegs
        e1_rect(sc, sx + (hh - 2) * b, sy - b, (float)b, 2.0f * b, 0);
    }
    // bushes along the fence foot
    const float bushAt[5] = {0.06f, 0.19f, 0.48f, 0.68f, 0.84f};
    for (int i = 0; i < 5; ++i) {
        float x = W * bushAt[i];
        std::vector<E1Blob> bb = {{x + 6 * s, g - 3 * s, 5 * s}, {x - 5 * s, g - 3 * s, 5 * s}, {x, g - 6 * s, 6 * s}, {x - 3 * s, g - 8 * s, 4 * s}};
        const int soft[4] = {3, 2, 1, -1};   // no darkest grey down by the players' feet
        e1_canopy(sc, bb, 2 * b, soft, 50 + i, true);
    }
    // two big trees, near enough to show every cluster
    e1_tree(sc, W * 0.1f, g, 4, 20 * s, 2 * b, tone, 1, 0, 21, true);
    e1_tree(sc, W * 0.9f, g, 1, 17 * s, 2 * b, tone, 1, 0, 22, true);
    return true;
}
