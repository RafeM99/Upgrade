// Background scene for stage 4 (32-bit late 2D), 1964, "The train to the city".
// A small railway town on a bright summer day: blue sky with stepped clouds and a pale sun,
// layered blue mountains, a pine forest band, a stone viaduct with a steam train, and the
// town street with shops, the station canopy, cluster-built trees, hedges, lamps and benches.
// Every shape is flat colour bands with crisp pixel edges; all x positions wrap so the
// parallax layers tile.

static int e4_wrap(int x, int W) { return ((x % W) + W) % W; }
static void e4_px(Scene& sc, int x, int y, Col c)
{
    if (y < 0 || y >= sc.H) return;
    sc.put(e4_wrap(x, sc.W), y, c);
}
static void e4_rect(Scene& sc, int x, int y, int w, int h, Col c)
{
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) e4_px(sc, xx, yy, c);
}
// wrapped horizontal distance
static float e4_dx(float a, float b, int W)
{
    float d = a - b;
    while (d > W * 0.5f) d -= W;
    while (d < -W * 0.5f) d += W;
    return d;
}

// ---------------------------------------------------------------- a tiny 3x5 pixel font for signs
static int e4_glyph(char ch)
{
    switch (ch) {
    case 'A': return 0x2BED; case 'B': return 0x6BAE; case 'C': return 0x3923; case 'D': return 0x6B6E;
    case 'E': return 0x79A7; case 'F': return 0x79A4; case 'G': return 0x396B; case 'H': return 0x5BED;
    case 'I': return 0x7497; case 'K': return 0x5BAD; case 'L': return 0x4927; case 'M': return 0x5FED;
    case 'N': return 0x6B6D; case 'O': return 0x2B6A; case 'P': return 0x6BA4; case 'R': return 0x6BAD;
    case 'S': return 0x388E; case 'T': return 0x7492; case 'U': return 0x5B6F; case 'Y': return 0x5A92;
    case 'W': return 0x5BFD; default: return 0;
    }
}
static int e4_textW(const char* t) { int n = 0; while (t[n]) ++n; return n * 4 - 1; }
static void e4_text(Scene& sc, int x, int y, const char* t, Col c)
{
    for (int i = 0; t[i]; ++i) {
        int g = e4_glyph(t[i]);
        for (int r = 0; r < 5; ++r)
            for (int k = 0; k < 3; ++k)
                if (g & (1 << (14 - (r * 3 + k)))) e4_px(sc, x + i * 4 + k, y + r, c);
    }
}

// ---------------------------------------------------------------- foliage
// Leaf palette: 0 highlight (yellow-green), 1 lit, 2 mid, 3 shadow (blue-teal).
struct E4Leaf { Col c[4]; };
static E4Leaf e4_leaf(float cool)   // cool 0 = warm near greens, 1 = bluish far greens
{
    E4Leaf L;
    const uint32_t warm[4] = {0xa8d848, 0x6cb83c, 0x3e8c3a, 0x1f5a4a};
    const uint32_t far[4]  = {0x8cc070, 0x5e9e62, 0x3e7a62, 0x285a5a};
    for (int i = 0; i < 4; ++i) L.c[i] = mixc(hex(warm[i]), hex(far[i]), cool);
    return L;
}

// A canopy built from blocky clusters (lumps). Lower lumps sit in front of upper ones, each
// lump lit on its top-left, so the cluster edges read like the reference trees.
static void e4_canopy(Scene& sc, float cx, float cy, float rx, float ry, int seed, const E4Leaf& L, int b, int nl = 7)
{
    float lx[16], ly[16], lr[16];
    int n = std::min(nl, 16);
    lx[0] = cx; ly[0] = cy + ry * 0.1f; lr[0] = std::min(rx, ry) * 0.62f;
    for (int i = 1; i < n; ++i) {
        float a = -3.14159f + (i - 1) * (6.2832f / (n - 1)) + (rnd(seed, i, 401) - 0.5f) * 0.6f;
        float d = 0.5f + 0.15f * rnd(seed, i, 402);
        lx[i] = cx + std::cos(a) * rx * d;
        ly[i] = cy + std::sin(a) * ry * d * 0.95f;
        lr[i] = std::min(rx, ry) * (0.42f + 0.14f * rnd(seed, i, 403));
    }
    int x0 = (int)std::floor((cx - rx * 1.2f) / b) * b, x1 = (int)(cx + rx * 1.2f);
    int y0 = (int)std::floor((cy - ry * 1.2f) / b) * b, y1 = (int)(cy + ry * 1.2f);
    for (int by = y0; by <= y1; by += b)
        for (int bx = x0; bx <= x1; bx += b) {
            float px = bx + b * 0.5f, py = by + b * 0.5f;
            int best = -1;
            float bestKey = -1e9f, bnx = 0, bny = 0;
            for (int i = 0; i < n; ++i) {
                float nx = (px - lx[i]) / lr[i], ny = (py - ly[i]) / (lr[i] * 0.9f);
                float d = nx * nx + ny * ny;
                float ang = std::atan2(ny, nx);
                float jit = 0.16f * std::sin(ang * 5 + seed * 1.7f + i * 2.3f) + 0.08f * std::sin(ang * 9 + i * 4.1f + seed);
                if (d > 1.0f + jit) continue;
                float key = ly[i] + lr[i] * 0.3f;
                if (key > bestKey) { bestKey = key; best = i; bnx = nx; bny = ny; }
            }
            if (best < 0) continue;
            float gx = (px - cx) / rx, gy = (py - cy) / ry;
            float l = -(0.55f * bnx + 0.8f * bny) * 0.75f - (0.35f * gx + 0.55f * gy) * 0.8f;
            int t = l > 0.62f ? 0 : (l > 0.05f ? 1 : (l > -0.55f ? 2 : 3));
            e4_rect(sc, bx, by, b, b, L.c[t]);
        }
}
static void e4_trunk(Scene& sc, int x, int top, int base, int w, Col lit, Col dark)
{
    e4_rect(sc, x - w / 2, top, w, base - top, lit);
    e4_rect(sc, x - w / 2 + w - std::max(1, w / 2), top, std::max(1, w / 2), base - top, dark);
}
static void e4_tuft(Scene& sc, int x, int base, int seed, const E4Leaf& L)
{
    for (int i = -4; i <= 4; ++i) {
        int h = 1 + (int)(rnd(seed, i, 410) * 4);
        if ((i & 1) == 0) e4_rect(sc, x + i * 2, base - h, 2, h, L.c[i < 0 ? 1 : 2]);
    }
}
// kind 0 round, 1 tall oval, 2 forked two-canopy, 3 wide round
static void e4_tree(Scene& sc, int x, int base, float size, int kind, int seed, const E4Leaf& L, Col tl, Col td, int b)
{
    int th = (int)(size * 0.55f);
    if (kind == 2) {
        int fy = base - th;
        for (int i = 0; i < th; ++i) {   // two forked limbs
            int k = i * 10 / th;
            e4_rect(sc, x - 2 - k, base - th - i, 3, 1, tl);
            e4_rect(sc, x + 1 + k, base - th - i, 3, 1, td);
        }
        e4_trunk(sc, x, fy, base, 5, tl, td);
        e4_canopy(sc, x - size * 0.45f, base - th - size * 0.85f, size * 0.55f, size * 0.55f, seed, L, b, 6);
        e4_canopy(sc, x + size * 0.5f, base - th - size * 0.55f, size * 0.45f, size * 0.45f, seed + 9, L, b, 6);
    } else {
        float rx = size * (kind == 1 ? 0.55f : kind == 3 ? 1.05f : 0.8f);
        float ry = size * (kind == 1 ? 1.05f : kind == 3 ? 0.7f : 0.75f);
        int tw = std::max(4, (int)(size * 0.14f));
        e4_trunk(sc, x, base - th - (int)(ry * 0.6f), base, tw, tl, td);
        e4_canopy(sc, (float)x, base - th - ry * 0.75f, rx, ry, seed, L, b, kind == 3 ? 9 : 7);
    }
    e4_tuft(sc, x, base, seed, L);
}
// A pine: stepped tiers, lit on the left, teal shadow on the right, jagged hems.
static void e4_pine(Scene& sc, int x, int base, int h, int halfW, int seed, const E4Leaf& L, Col trunk, int b)
{
    int trunkH = std::max(2, h / 8);
    e4_rect(sc, x - b / 2 - 1, base - trunkH, b + 1, trunkH, trunk);
    int tiers = std::max(2, h / 14);
    int top = base - h, foot = base - trunkH;
    for (int t = tiers - 1; t >= 0; --t) {   // draw from the lowest tier up
        float t0 = (float)t / tiers, t1 = (float)(t + 1) / tiers;
        int ty0 = top + (int)((foot - top) * t0 * 0.85f);
        int ty1 = top + (int)((foot - top) * t1);
        float hw = halfW * (0.35f + 0.65f * t1);
        for (int y = ty0; y < ty1; y += b) {
            float u = (float)(y - ty0) / std::max(1, ty1 - ty0);
            int w = std::max(b, (int)(hw * (0.2f + 0.8f * u)));
            w = (w / b) * b;
            for (int dx = -w; dx < w; dx += b) {
                bool hem = (y + 2 * b >= ty1);
                int tooth = (dx + 300) / 3;
                if (hem && (tooth & 1) && rnd(tooth, t, seed) < 0.7f) continue;   // jagged hem, 3px teeth
                float r = (dx + b * 0.5f) / std::max(1, w);
                int tone = r < -0.35f ? 1 : (r < 0.25f ? 2 : 3);
                if (r < -0.6f && u < 0.6f) tone = 0;
                if (hem && tone < 3) tone++;
                e4_rect(sc, x + dx, y, b, b, L.c[tone]);
            }
        }
    }
    e4_rect(sc, x, top - 2, 1, 2, L.c[1]);
}
// A low hedge: a long row of small cluster lumps with a flat bottom.
static void e4_hedge(Scene& sc, int x, int base, int len, int h, int seed, const E4Leaf& L, int b)
{
    for (int i = 0; i * h * 0.7f < len; ++i) {
        float cx = x + h * 0.5f + i * h * 0.7f;
        float ry = h * (0.45f + 0.12f * rnd(seed, i, 420));
        e4_canopy(sc, cx, base - ry, h * 0.55f, ry, seed * 31 + i, L, b, 4);
    }
    e4_rect(sc, x + h / 4, base - b, len, b, L.c[3]);
}

// ---------------------------------------------------------------- sky pieces
static void e4_cloud(Scene& sc, float cx, float base, float size, int seed)
{
    const Col c0 = hex(0xffffff), c1 = hex(0xeaf2fa), c2 = hex(0xc4d4ec), c3 = hex(0xa0b6dc);
    int n = 4 + (int)(rnd(seed, 0, 430) * 3);
    float px[8], py[8], pr[8];
    for (int i = 0; i < n; ++i) {
        float u = (i + 0.5f) / n - 0.5f;
        px[i] = cx + u * size * 3.2f + (rnd(seed, i, 431) - 0.5f) * size * 0.4f;
        float bump = 1 - std::fabs(u) * 1.6f;
        pr[i] = size * (0.45f + 0.5f * bump) * (0.8f + 0.4f * rnd(seed, i, 432));
        py[i] = base - pr[i] * 0.55f;
    }
    const int b = 1;
    for (int y = (int)(base - size * 1.6f); y < base; y += b)
        for (int x = (int)(cx - size * 2.6f); x < cx + size * 2.6f; x += b) {
            float mx = x + 0.5f, my = y + 0.5f;
            int best = -1; float key = -1e9f, ny = 0, nx = 0;
            for (int i = 0; i < n; ++i) {
                float dx = (mx - px[i]) / pr[i], dy = (my - py[i]) / (pr[i] * 0.85f);
                if (dx * dx + dy * dy > 1) continue;
                if (-pr[i] > key) { key = -pr[i]; best = i; nx = dx; ny = dy; }
            }
            if (best < 0) continue;
            float l = -(0.4f * nx + 0.9f * ny);
            float under = (my - (base - size * 0.45f)) / (size * 0.45f);
            int t = l > 0.35f ? 0 : 1;
            if (under > 0) t = under > 0.55f ? 3 : 2;
            else if (l < -0.35f) t = 2;
            Col cc = t == 0 ? c0 : t == 1 ? c1 : t == 2 ? c2 : c3;
            e4_rect(sc, x, y, b, b, cc);
        }
}

// Mountains: triangular peaks with a lit left face and shaded right face, optional snow.
static void e4_range(Scene& sc, int seed, int count, float baseY, float hMin, float hMax, Col lit, Col shade, bool snow, Col snowL, Col snowD)
{
    const int W = sc.W, b = 1;
    std::vector<float> pkx(count), pkh(count), pkw(count);
    for (int i = 0; i < count; ++i) {
        pkx[i] = (i + 0.2f + 0.6f * rnd(seed, i, 440)) * W / count;
        pkh[i] = hMin + (hMax - hMin) * rnd(seed, i, 441);
        pkw[i] = pkh[i] * (1.3f + 0.7f * rnd(seed, i, 442));
    }
    for (int x = 0; x < W; x += b) {
        float best = -1e9f; int bi = 0; float bdx = 0;
        for (int i = 0; i < count; ++i) {
            float dx = e4_dx(x + 1.0f, pkx[i], W);
            float h = pkh[i] * (1 - std::fabs(dx) / pkw[i]);
            if (h > best) { best = h; bi = i; bdx = dx; }
        }
        float hj = best + 1.2f * std::sin(x * 6.2832f * 23 / W + seed) + 0.8f * std::sin(x * 6.2832f * 57 / W + seed * 3);
        int top = (int)std::floor((baseY - hj) / b) * b;
        float snowLine = baseY - pkh[bi] * 0.85f + std::fabs(bdx) * 0.25f + 3.0f * std::sin(x * 6.2832f * 41 / W + seed) + 2.0f * std::sin(x * 6.2832f * 97 / W);
        snowLine = std::floor(snowLine / 2) * 2;
        for (int y = top; y < sc.H; y += b) {
            bool left = bdx < 0;
            // gullies: a few diagonal streaks on each face
            float along = std::fabs(bdx) + (y - (baseY - pkh[bi])) * 0.5f;
            bool gully = std::fmod(along + seed * 7.0f, 31.0f) < 2.0f && y > top + 6 && y < baseY - pkh[bi] * 0.55f;
            Col c = left ? (gully ? shade : lit) : (gully ? mixc(shade, lit, 0.5f) : shade);
            if (snow && pkh[bi] > hMin + (hMax - hMin) * 0.4f && y < snowLine) c = left ? snowL : snowD;
            e4_rect(sc, x, y, b, b, c);
        }
    }
}

// ---------------------------------------------------------------- the layers
static void e4_sky(Scene& sc)
{
    const int W = sc.W, H = sc.H;
    const uint32_t bands[9] = {0x3c78cc, 0x4282d2, 0x4a8cd8, 0x5496de, 0x60a2e4, 0x6eaee8, 0x82bcec, 0x9ccaf0, 0xb6d8f2};
    const float edges[10] = {0, 0.1f, 0.19f, 0.27f, 0.34f, 0.40f, 0.46f, 0.52f, 0.58f, 1.0f};
    for (int i = 0; i < 9; ++i) {
        int y0 = (int)(H * edges[i]), y1 = (int)(H * edges[i + 1]);
        e4_rect(sc, 0, y0, W, y1 - y0, hex(bands[i]));
    }
    // the sun: a pale stepped disc with a soft ring, blocks of 2
    float sx = W * 0.79f, sy = H * 0.22f;
    for (int y = (int)sy - 32; y < sy + 32; ++y)
        for (int x = (int)sx - 32; x < sx + 32; ++x) {
            float d = std::sqrt((x + 0.5f - sx) * (x + 0.5f - sx) + (y + 0.5f - sy) * (y + 0.5f - sy));
            if (d < 19) e4_px(sc, x, y, d < 13 && (x - sx) + (y - sy) < 4 ? hex(0xfff6d2) : hex(0xfbe7a2));
            else if (d < 25) e4_px(sc, x, y, mixc(hex(0x5496de), hex(0xfff0c0), 0.28f));
        }
    struct C { float x, y, s; } cl[9] = {{0.06f, 0.24f, 20}, {0.20f, 0.20f, 12}, {0.31f, 0.32f, 24}, {0.47f, 0.21f, 15},
                                         {0.57f, 0.37f, 10}, {0.66f, 0.29f, 19}, {0.92f, 0.33f, 17}, {0.985f, 0.19f, 11}, {0.40f, 0.42f, 8}};
    for (int i = 0; i < 9; ++i) e4_cloud(sc, W * cl[i].x, H * cl[i].y, cl[i].s, 50 + i);
    // a few tiny wisps
    for (int i = 0; i < 6; ++i) {
        int x = (int)(W * (0.1f + i * 0.17f + 0.04f * rnd(i, 0, 436))), y = (int)(H * (0.15f + 0.04f * (i % 3)));
        e4_rect(sc, x, y, 14 + i * 2, 2, hex(0xd8e8f8));
        e4_rect(sc, x + 4, y - 2, 8, 2, hex(0xf0f6fc));
    }
}

static void e4_far(Scene& sc)
{
    const int W = sc.W, H = sc.H;
    // farthest range: pale, blue, snow-capped
    e4_range(sc, 3, 6, H * 0.70f, H * 0.28f, H * 0.50f, hex(0xa6c2e2), hex(0x8eaad6), true, hex(0xeef4fa), hex(0xc8d8ee));
    // nearer range: deeper blue
    e4_range(sc, 11, 9, H * 0.72f, H * 0.14f, H * 0.28f, hex(0x82a4cc), hex(0x6a8cbc), false, Col{}, Col{});
    // blue-green rolling hills
    for (int x = 0; x < W; ++x) {
        int top = (int)std::lround(H * 0.645f + ridge((float)x, W, 5, 5, 3, 1.5f));
        for (int y = top; y < H; ++y) sc.put(x, y, y < top + 2 ? hex(0x7ab0a4) : hex(0x5e9496));
    }
    // dense band of far pines, teal and hazy
    E4Leaf F;
    F.c[0] = hex(0x6aa496); F.c[1] = hex(0x52907e); F.c[2] = hex(0x3e7a70); F.c[3] = hex(0x2e6264);
    for (int i = 0; i < 150; ++i) {
        float u = rnd(i, 1, 450);
        int x = (int)(i * W / 150.0f + u * 6);
        int h = 20 + (int)(rnd(i, 2, 450) * 22) + (int)(ridge((float)x, W, 8, 4, 2, 0));
        int base = (int)(H * 0.76f) + (int)(rnd(i, 3, 450) * 8);
        e4_pine(sc, x, base, h, 6 + (int)(h * 0.2f), i, F, hex(0x2e5250), 1);
    }
    e4_rect(sc, 0, (int)(H * 0.76f), W, H, hex(0x2e6264));
}

// A stone block wall, courses of 6 px with staggered joints that tile across the width.
static Col e4_stone(int x, int y, int W, int y0, Col base, int salt)
{
    int course = (y - y0) / 6;
    int nBlocks = std::max(1, (int)std::lround(W / 15.0f));
    float fx = e4_wrap(x + (course & 1) * 7, W) * (float)nBlocks / W;
    int blk = (int)fx;
    bool vJoint = (fx - blk) * W / nBlocks < 1.0f;
    bool hJoint = ((y - y0) % 6) == 5;
    float v = rnd(blk, course, 460 + salt);
    Col c = v < 0.3f ? mul(base, 0.92f) : (v > 0.8f ? mixc(base, hex(0xfff0dc), 0.18f) : base);
    if (hJoint || vJoint) c = mixc(mul(base, 0.72f), hex(0x40406a), 0.18f);
    else if (((y - y0) % 6) == 0) c = mixc(c, hex(0xfff4e0), 0.12f);   // lit top edge of each stone
    return c;
}

static void e4_mid(Scene& sc)
{
    const int W = sc.W, H = sc.H;
    const E4Leaf L = e4_leaf(0.55f);
    // green hillside under the viaduct
    for (int x = 0; x < W; ++x) {
        int top = (int)std::lround(H * 0.80f + ridge((float)x, W, 2, 4, 2.5f, 1));
        for (int y = top; y < H; ++y) sc.put(x, y, y < top + 2 ? hex(0x8ec05a) : (y < top + 6 ? hex(0x6aa24c) : hex(0x58904a)));
    }
    // ---- the viaduct
    const int rail = (int)(H * 0.47f);           // rail level (top of deck)
    const int parTop = rail - 5, deckBot = rail + 12;
    const int nArch = 13;
    const float period = (float)W / nArch, pier = 18;
    const int spring = rail + 52;                 // where the arches start to curve
    const float ry = 32;
    const Col stone = hex(0xc29a78), stoneD = hex(0x9a7a6a), soffit = hex(0x6a5462);
    auto archOpen = [&](int x, int y, float* nxOut) {
        float fx = std::fmod((float)e4_wrap(x, W), period);
        float half = (period - pier) * 0.5f, cx = pier * 0.5f + half;
        float nx = (fx + 0.5f - cx) / half;
        if (nxOut) *nxOut = nx;
        if (std::fabs(nx) >= 1) return false;
        if (y >= spring) return true;
        float ny = (spring - (y + 0.5f)) / ry;
        return nx * nx + ny * ny < 1;
    };
    // ---- the train first (the parapet hides its wheels)
    {
        const int tx = (int)(W * 0.30f);
        const Col black = hex(0x2a2a36), blackL = hex(0x4a4a5c), green = hex(0x2f7a46), greenL = hex(0x4c9a5a), greenD = hex(0x1e5238);
        const Col red = hex(0xc0403a), brass = hex(0xe0b050), maroon = hex(0x8a2c34), maroonL = hex(0xa8443e), maroonD = hex(0x5e1e2a);
        const Col roof = hex(0x5a5866), roofL = hex(0x7a7888), cream = hex(0xeadcb8), glass = hex(0x9cc4dc), glassL = hex(0xd8ecf6);
        // locomotive, facing left
        int ly = rail;
        e4_rect(sc, tx - 4, ly - 9, 4, 4, red);                       // buffer beam
        e4_rect(sc, tx - 6, ly - 8, 2, 2, black);                    // buffers
        e4_rect(sc, tx, ly - 26, 10, 19, black);                     // smokebox
        e4_rect(sc, tx, ly - 26, 10, 2, blackL);
        e4_rect(sc, tx + 2, ly - 34, 5, 8, black);                   // chimney
        e4_rect(sc, tx + 1, ly - 35, 7, 2, blackL);
        e4_rect(sc, tx + 10, ly - 27, 34, 17, green);                // boiler
        e4_rect(sc, tx + 10, ly - 27, 34, 3, greenL);
        e4_rect(sc, tx + 10, ly - 14, 34, 4, greenD);
        for (int k = 0; k < 3; ++k) e4_rect(sc, tx + 18 + k * 10, ly - 27, 1, 17, brass);   // boiler bands
        e4_rect(sc, tx + 24, ly - 32, 6, 5, green);                  // dome
        e4_rect(sc, tx + 24, ly - 32, 6, 1, greenL);
        e4_rect(sc, tx + 36, ly - 30, 3, 3, brass);                  // safety valve
        e4_rect(sc, tx + 44, ly - 36, 18, 28, green);                // cab
        e4_rect(sc, tx + 43, ly - 38, 21, 3, black);                 // cab roof
        e4_rect(sc, tx + 47, ly - 32, 7, 8, glass);                  // cab window
        e4_rect(sc, tx + 47, ly - 32, 2, 3, glassL);
        e4_rect(sc, tx + 56, ly - 34, 2, 26, greenD);
        e4_rect(sc, tx + 6, ly - 10, 56, 3, black);                  // running plate
        for (int k = 0; k < 3; ++k) {                                // driving wheels
            int wx = tx + 16 + k * 13;
            sc.disc((float)wx, ly - 5.f, 6, 6, black, black, false);
            sc.disc((float)wx, ly - 5.f, 4, 4, red, red, false);
            e4_rect(sc, wx - 1, ly - 6, 2, 2, black);
        }
        e4_rect(sc, tx + 14, ly - 6, 30, 2, hex(0xb8b8c4));          // coupling rod
        // tender
        e4_rect(sc, tx + 64, ly - 28, 28, 20, green);
        e4_rect(sc, tx + 64, ly - 28, 28, 2, greenL);
        e4_rect(sc, tx + 64, ly - 32, 28, 4, black);                 // coal
        e4_rect(sc, tx + 66, ly - 34, 20, 2, blackL);
        e4_rect(sc, tx + 70, ly - 22, 14, 6, greenL);                // lining panel
        e4_rect(sc, tx + 71, ly - 21, 12, 4, green);
        // carriages
        for (int c = 0; c < 5; ++c) {
            int cx = tx + 96 + c * 66;
            e4_rect(sc, cx - 3, ly - 28, 3, 18, black);                // gangway
            e4_rect(sc, cx, ly - 34, 62, 26, maroon);
            e4_rect(sc, cx, ly - 34, 62, 2, maroonL);
            e4_rect(sc, cx, ly - 12, 62, 4, maroonD);
            e4_rect(sc, cx + 1, ly - 38, 60, 4, roof);                 // curved roof
            e4_rect(sc, cx + 3, ly - 39, 56, 1, roofL);
            e4_rect(sc, cx + 2, ly - 30, 58, 1, cream);                // lining
            e4_rect(sc, cx + 2, ly - 16, 58, 1, hex(0xd8a860));
            for (int w2 = 0; w2 < 6; ++w2) {
                int wx = cx + 4 + w2 * 9;
                e4_rect(sc, wx, ly - 27, 6, 8, glass);
                e4_rect(sc, wx, ly - 27, 6, 2, glassL);
                if (rnd(c, w2, 470) < 0.4f) e4_rect(sc, wx + 2, ly - 23, 3, 4, hex(0x4a4058));   // a passenger
                if (rnd(c, w2, 471) < 0.25f) e4_rect(sc, wx + 1, ly - 24, 2, 3, hex(0x806070)); // a passenger
            }
            if (c == 2) { e4_rect(sc, cx + 26, ly - 27, 1, 8, hex(0xe8c868)); e4_rect(sc, cx + 23, ly - 21, 3, 1, hex(0xe8c868)); }
        }
        // smoke trailing back and up from the chimney, stepped puffs fading lighter
        for (int k = 0; k < 7; ++k) {
            float px = tx + 6 + k * 22 + k * k * 1.5f, py = ly - 42 - k * 7 - (k > 2 ? (k - 2) * 2 : 0);
            float r = 6 + k * 1.6f;
            const int b = 1;
            for (int y = (int)(py - r); y < py + r; y += b)
                for (int x = (int)(px - r * 1.3f); x < px + r * 1.3f; x += b) {
                    float nx = (x + 0.5f - px) / (r * 1.3f), ny = (y + 0.5f - py) / r;
                    float d = nx * nx + ny * ny;
                    if (d > 1) continue;
                    float l = -(0.5f * nx + 0.85f * ny);
                    Col c = l > 0.3f ? hex(0xfcfcfc) : (l > -0.3f ? hex(0xdfe4ee) : hex(0xb4bccc));
                    if (k == 0 && l < 0.3f) c = mixc(c, hex(0x707080), 0.35f);
                    e4_rect(sc, x, y, b, b, c);
                }
        }
    }
    // ---- parapet, deck, spandrels and piers, stone blocks
    for (int y = parTop; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float nx = 0;
            if (y >= deckBot && archOpen(x, y, &nx)) continue;
            Col c = e4_stone(x, y, W, parTop, stone, 0);
            if (y >= deckBot) {
                // right face of each pier a touch darker; arch ring (voussoirs) around each opening
                float fx = std::fmod((float)x, period);
                if (fx < 2 || (y > spring && fx > pier - 3 && fx < pier)) c = mixc(c, stoneD, 0.6f);
                bool ring = false;
                for (int d = 1; d <= 5 && !ring; ++d) if (archOpen(x, y + d, nullptr) || archOpen(x - d, y, nullptr) || archOpen(x + d, y, nullptr)) ring = y < spring + 4;
                if (ring) {
                    int seg = (int)(std::atan2((float)(spring - y), nx) * 9);
                    c = (seg & 1) ? mixc(stone, hex(0xfff0dc), 0.15f) : mul(stone, 0.9f);
                }
            }
            sc.put(x, y, c);
        }
    // the arch soffit: a dark band of barrel vault seen inside each arch (shadow side right)
    for (int y = deckBot; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float nx = 0;
            if (!archOpen(x, y, &nx)) continue;
            if (!archOpen(x, y - 4, nullptr) && y < spring) sc.put(x, y, soffit);
            else if (nx > 0.86f) sc.put(x, y, mixc(soffit, stoneD, 0.3f));
        }
    // coping on the parapet and the deck string course
    e4_rect(sc, 0, parTop - 2, W, 2, hex(0xe4c8a8));
    e4_rect(sc, 0, parTop, W, 1, hex(0x8a6e66));
    e4_rect(sc, 0, deckBot - 3, W, 3, hex(0xd8b896));
    e4_rect(sc, 0, deckBot, W, 1, hex(0x7a6060));
    // ---- trees and hedges along the foot of the viaduct (they hide the pier bases)
    const Col tl = hex(0x8a6448), td = hex(0x5a3e3e);
    for (int i = 0; i < 16; ++i) {
        float u = rnd(i, 0, 490);
        int x = (int)((i + 0.15f + 0.7f * u) * W / 16);
        int base = (int)(H * 0.86f) + (int)(rnd(i, 1, 490) * 6);
        float size = 20 + 12 * rnd(i, 2, 490);
        if (i % 5 == 3) e4_pine(sc, x, base, (int)(size * 2.4f), (int)(size * 0.6f), i, L, td, 1);
        else e4_tree(sc, x, base, size, (i * 7) % 4, 600 + i, L, tl, td, 1);
    }
    for (int i = 0; i < 26; ++i) {
        int x = (int)(i * W / 26.0f);
        e4_hedge(sc, x, (int)(H * 0.88f), 40, 12 + (int)(rnd(i, 3, 491) * 6), 700 + i, L, 1);
    }
    // a distant church spire and rooftops peeking between the trees
    {
        int cx = (int)(W * 0.82f), base = (int)(H * 0.84f);
        e4_rect(sc, cx, base - 40, 14, 40, hex(0xc8b8a0));
        e4_rect(sc, cx + 10, base - 40, 4, 40, hex(0xa8988c));
        sc.tri((float)cx - 1, base - 40.f, cx + 15.f, base - 40.f, cx + 7.f, base - 74.f, hex(0x6a7088));
        sc.tri((float)cx + 7, base - 74.f, cx + 15.f, base - 40.f, cx + 7.f, base - 40.f, hex(0x545a74));
        e4_rect(sc, cx + 5, base - 32, 4, 6, hex(0x3a3a50));
    }
}

// ---------------------------------------------------------------- the near layer: the town
struct E4Wall { Col c, d, trim; bool brick; };
static void e4_window(Scene& sc, int x, int y, int w, int h, bool lit, int seed, Col trim)
{
    e4_rect(sc, x - 1, y - 1, w + 2, h + 2, trim);
    if (lit) {
        e4_rect(sc, x, y, w, h, hex(0xf6d68a));
        e4_rect(sc, x, y, w, h / 2, hex(0xffecb4));
        if (rnd(seed, 1, 500) < 0.5f) { e4_rect(sc, x, y, 2, h, hex(0xd86a5a)); e4_rect(sc, x + w - 2, y, 2, h, hex(0xd86a5a)); }
    } else {
        e4_rect(sc, x, y, w, h, hex(0x4a6488));
        e4_rect(sc, x, y + h / 2, w, h - h / 2, hex(0x3e5476));
        for (int k = 0; k < std::min(w, h) / 2; ++k) e4_px(sc, x + 1 + k, y + h / 2 - 1 - k + 1, hex(0x8cb0d0));   // sky reflection
        if (rnd(seed, 2, 500) < 0.4f) e4_rect(sc, x, y, w, 3, hex(0xe8e0d0));   // net curtain top
    }
    e4_rect(sc, x, y + h / 2, w, 1, trim);    // sash bar
    e4_rect(sc, x + w / 2, y, 1, h, trim);
    e4_rect(sc, x - 2, y + h + 1, w + 4, 2, mixc(trim, hex(0xb0a090), 0.3f));   // sill
}
static void e4_awning(Scene& sc, int x, int y, int w, Col a, Col b)
{
    const int h = 9, sw = 6;
    for (int k = 0; k * sw < w; ++k) {
        Col c = (k & 1) ? b : a;
        int sx = x + k * sw, ww = std::min(sw, w - k * sw);
        for (int r = 0; r < h; ++r) e4_rect(sc, sx - (h - r) / 3, y + r, ww, 1, c);   // slopes out
        e4_rect(sc, sx + 1, y + h, ww - 2, 2, c);                                     // scallop
        e4_rect(sc, sx - 2, y, ww, 2, mixc(c, hex(0xffffff), 0.25f));                 // top lit
    }
    e4_rect(sc, x, y + h + 2, w, 3, hex(0x4a3a50));   // shadow under the awning
}
static void e4_building(Scene& sc, int x, int base, int w, int h, int seed, const E4Wall& wl, const char* sign, int kind)
{
    // roof: a front-facing slate slope with chimneys
    int rh = 16 + (int)(rnd(seed, 0, 510) * 10);
    const Col slate = hex(0x5e6680), slateL = hex(0x7a849c), slateD = hex(0x464c66);
    for (int r = 0; r < rh; ++r) {
        int inset = (rh - r) * 2 / 3;
        e4_rect(sc, x + inset - 2, base - h - rh + r, w + 4 - inset * 2, 1, (r % 4 == 0) ? slateL : (r > rh - 3 ? slateD : slate));
    }
    for (int c = 0; c < 2; ++c) {
        int cx = x + (c == 0 ? 8 : w - 20) + (int)(rnd(seed, c, 511) * 4), cy = base - h - rh - 10;
        e4_rect(sc, cx, cy, 12, rh / 2 + 10, wl.brick ? hex(0xa25640) : hex(0xb08a70));
        e4_rect(sc, cx + 8, cy, 4, rh / 2 + 10, hex(0x7a4038));
        e4_rect(sc, cx - 1, cy, 14, 2, hex(0xd0c0b0));
        e4_rect(sc, cx + 2, cy - 4, 3, 4, hex(0xc8683a));
        e4_rect(sc, cx + 7, cy - 3, 3, 3, hex(0xb0583a));
    }
    // walls
    for (int y = base - h; y < base; ++y)
        for (int xx = x; xx < x + w; ++xx) {
            Col c = wl.c;
            if (wl.brick) {
                int row = (y - (base - h)) / 3;
                bool joint = ((y - (base - h)) % 3 == 2) || (((xx - x) + (row & 1) * 4) % 8 == 0);
                if (joint) c = mixc(wl.c, hex(0xe0d0c0), 0.25f);
                else if (rnd((xx - x) / 8, row, seed) < 0.2f) c = mul(wl.c, 0.92f);
            }
            if (xx >= x + w - 4) c = wl.d;
            e4_px(sc, xx, y, c);
        }
    e4_rect(sc, x - 2, base - h, w + 4, 3, wl.trim);                    // cornice
    e4_rect(sc, x, base - h + 3, w, 2, mixc(wl.d, hex(0x40406a), 0.2f)); // its shadow
    // upper windows
    // signs sit high on the facade, right under the cornice, well above the players' heads
    const int top0 = (sign && kind != 1) ? 22 : 10;
    if (sign && kind != 1) {
        Col board = kind == 2 ? hex(0x2a4a3a) : (rnd(seed, 3, 513) < 0.5f ? hex(0x2a5a4a) : hex(0x2a3a6a));
        int bw = std::min(w - 6, e4_textW(sign) + 10);
        int bx = x + (w - bw) / 2, by = base - h + 7;
        e4_rect(sc, bx - 1, by - 1, bw + 2, 13, wl.trim);
        e4_rect(sc, bx, by, bw, 11, board);
        e4_rect(sc, bx, by, bw, 1, mixc(board, hex(0xffffff), 0.3f));
        e4_text(sc, x + (w - e4_textW(sign)) / 2, by + 3, sign, kind == 2 ? hex(0xe8c868) : hex(0xf4e4b0));
    }
    int floors = std::max(1, (h - 34 - top0) / 26);
    int cols = std::max(2, (w - 8) / 22);
    for (int f = 0; f < floors; ++f)
        for (int c = 0; c < cols; ++c) {
            int wx = x + 6 + c * ((w - 12) / cols) + ((w - 12) / cols - 10) / 2;
            int wy = base - h + top0 + f * 26;
            e4_window(sc, wx, wy, 10, 14, rnd(seed, f * 9 + c, 512) < 0.3f, seed + f * 9 + c, wl.trim);
        }
    // ground floor
    int gf = base - 40;
    if (kind == 0) {   // shop: sign, awning, window, door
        e4_rect(sc, x + 2, gf - 2, w - 4, 3, wl.trim);   // fascia moulding
        int ww = w - 28;
        e4_rect(sc, x + 4, gf + 18, ww, 18, hex(0x3e4c6a));
        e4_rect(sc, x + 4, gf + 18, ww, 6, hex(0xf6d68a));                   // lit inside, under the awning shade
        for (int k = 0; k < ww / 8; ++k) e4_rect(sc, x + 6 + k * 8, gf + 28, 5, 4, k % 2 ? hex(0xe8a050) : hex(0xd8d0b0)); // goods
        e4_rect(sc, x + 4, gf + 36, ww, 4, wl.trim);
        e4_rect(sc, x + w - 20, gf + 12, 12, 28, hex(0x6a3a3a));               // door
        e4_rect(sc, x + w - 18, gf + 15, 8, 8, hex(0xf6d68a));
        e4_awning(sc, x + 3, gf + 8, w - 6, hex(0xe8e0d0), (seed & 1) ? hex(0xc8443a) : hex(0x3a8a6a));
    } else if (kind == 1) {   // house: front door with a step and two windows
        e4_window(sc, x + 8, gf + 10, 12, 16, rnd(seed, 4, 514) < 0.5f, seed + 40, wl.trim);
        e4_window(sc, x + w - 24, gf + 10, 12, 16, rnd(seed, 5, 514) < 0.5f, seed + 41, wl.trim);
        int dx = x + w / 2 - 6;
        e4_rect(sc, dx - 2, gf + 8, 16, 2, wl.trim);
        e4_rect(sc, dx, gf + 10, 12, 30, (seed & 1) ? hex(0x2a5a9a) : hex(0xb83a3a));
        e4_rect(sc, dx + 2, gf + 12, 8, 3, hex(0xf6d68a));
        e4_rect(sc, dx - 2, base - 2, 16, 2, hex(0xd0c8c0));
    } else {   // pub: hanging sign on a bracket, dark green front
        e4_rect(sc, x + 2, gf, w - 4, 40, hex(0x2a4a3a));
        e4_rect(sc, x + 2, gf, w - 4, 2, hex(0x4a7a5a));
        e4_window(sc, x + 8, gf + 14, 16, 18, true, seed + 50, hex(0x1e3a2a));
        e4_window(sc, x + w - 26, gf + 14, 16, 18, true, seed + 51, hex(0x1e3a2a));
        e4_rect(sc, x + w / 2 - 6, gf + 12, 12, 28, hex(0x5a3a2a));
        int sy = base - h + 30;                                 // hanging sign, high on a bracket
        e4_rect(sc, x - 12, sy, 14, 2, hex(0x2a2a36));
        e4_rect(sc, x - 10, sy + 2, 12, 14, hex(0x8a3a2a));
        e4_rect(sc, x - 9, sy + 3, 10, 12, hex(0xe8c868));
        e4_rect(sc, x - 6, sy + 6, 4, 6, hex(0x8a3a2a));
    }
}
// Street lamps differ per instance: post height, paint, single lantern or a two-arm bracket.
static void e4_lamp(Scene& sc, int x, int base, int seed)
{
    const bool green = rnd(seed, 0, 520) < 0.6f;
    const Col iron = green ? hex(0x2e4a46) : hex(0x34364a), ironL = green ? hex(0x4a6a62) : hex(0x50546a);
    const int ph = 44 + (int)(rnd(seed, 1, 520) * 14);       // post height
    const bool twin = rnd(seed, 2, 520) < 0.35f;
    e4_rect(sc, x - 3, base - 6, 7, 6, iron);
    e4_rect(sc, x - 1, base - ph, 3, ph - 6, iron);
    e4_rect(sc, x - 1, base - ph, 1, ph - 6, ironL);
    if (rnd(seed, 3, 520) < 0.5f) e4_rect(sc, x - 6, base - ph + 6, 13, 1, iron);   // ladder bar
    auto lantern = [&](int lx, int ly) {
        e4_rect(sc, lx - 4, ly, 9, 2, iron);
        e4_rect(sc, lx - 3, ly - 10, 7, 10, hex(0xf8e0a0));
        e4_rect(sc, lx - 3, ly - 10, 2, 10, hex(0xfff4d0));
        e4_rect(sc, lx - 4, ly - 12, 9, 2, iron);
        e4_rect(sc, lx - 2, ly - 14, 5, 2, iron);
        e4_rect(sc, lx, ly - 16, 1, 2, iron);
    };
    if (twin) {
        e4_rect(sc, x - 9, base - ph - 1, 19, 2, iron);
        e4_rect(sc, x - 9, base - ph - 1, 19, 1, ironL);
        lantern(x - 9, base - ph + 9);
        lantern(x + 9, base - ph + 9);
        e4_rect(sc, x - 9, base - ph + 1, 1, 0 + 8, iron);
        e4_rect(sc, x + 9, base - ph + 1, 1, 8, iron);
        e4_rect(sc, x - 1, base - ph - 6, 3, 5, iron);
    } else {
        lantern(x, base - ph);
    }
}
static void e4_bench(Scene& sc, int x, int base, int seed = 0)
{
    const bool green = rnd(seed, 0, 525) < 0.4f;
    const Col wood = green ? hex(0x3a7a52) : hex(0xa86a3a), woodL = green ? hex(0x5a9a6a) : hex(0xc88a50), iron = hex(0x2e3440);
    const int L = 22 + (int)(rnd(seed, 1, 525) * 10);
    e4_rect(sc, x, base - 16, L, 2, woodL);
    e4_rect(sc, x, base - 13, L, 2, wood);
    e4_rect(sc, x - 1, base - 8, L + 2, 2, woodL);
    e4_rect(sc, x - 1, base - 6, L + 2, 1, mul(wood, 0.6f));
    e4_rect(sc, x + 2, base - 16, 2, 16, iron);
    e4_rect(sc, x + L - 4, base - 16, 2, 16, iron);
}
static void e4_station(Scene& sc, int x, int base, int w)
{
    const Col sand = hex(0xd8bc8c), sandD = hex(0xb09878), trim = hex(0xf0e6d0);
    int h = 90;
    // hipped slate roof
    for (int r = 0; r < 18; ++r) e4_rect(sc, x + 18 - r, base - h - 18 + r, w - 36 + 2 * r, 1, r % 4 == 0 ? hex(0x7a849c) : hex(0x5e6680));
    e4_rect(sc, x + w / 2 - 8, base - h - 34, 16, 20, sand);   // clock tower
    e4_rect(sc, x + w / 2 + 4, base - h - 34, 4, 18, sandD);
    sc.tri(x + w / 2 - 10.f, base - h - 34.f, x + w / 2 + 10.f, base - h - 34.f, x + w / 2.f, base - h - 48.f, hex(0x5e6680));
    sc.disc(x + w / 2.f, base - h - 26.f, 5, 5, trim, trim, false);
    e4_rect(sc, x + w / 2, base - h - 30, 1, 4, hex(0x2a2a36));
    e4_rect(sc, x + w / 2, base - h - 26, 3, 1, hex(0x2a2a36));
    e4_rect(sc, x, base - h, w, h, sand);
    for (int y = base - h; y < base; y += 6) e4_rect(sc, x, y, w, 1, mixc(sand, sandD, 0.5f));   // ashlar courses
    e4_rect(sc, x + w - 5, base - h, 5, h, sandD);
    e4_rect(sc, x - 2, base - h, w + 4, 3, trim);
    // round-headed windows on the upper storey
    for (int k = 0; k < 5; ++k) {
        int wx = x + 14 + k * (w - 28) / 5 + 2, wy = base - h + 10;
        sc.disc(wx + 6.f, (float)wy + 5, 7, 6, trim, trim, false);
        e4_window(sc, wx, wy + 4, 12, 14, k % 2 == 0, 900 + k, trim);
        sc.disc(wx + 6.f, (float)wy + 5, 5, 4, k % 2 == 0 ? hex(0xffecb4) : hex(0x4a6488), hex(0x4a6488), false);
    }
    // the sign board
    const char* nm = "STATION";
    int bw = e4_textW(nm) + 10, sby = base - h - 15;
    e4_rect(sc, x + (w - bw) / 2 - 1, sby - 1, bw + 2, 12, trim);
    e4_rect(sc, x + (w - bw) / 2, sby, bw, 10, hex(0x2a3a6a));
    e4_rect(sc, x + (w - bw) / 2, sby, bw, 1, hex(0x5a6a9a));
    e4_text(sc, x + (w - e4_textW(nm)) / 2, sby + 3, nm, hex(0xf4ecd8));
    // under the canopy: shade, doors and a timetable board
    int cy = base - 34;
    e4_rect(sc, x - 20, cy, w + 40, 34, mixc(hex(0x6a5a5a), hex(0x3a3a6a), 0.25f));
    e4_rect(sc, x + 4, cy + 4, w - 8, 30, mixc(sandD, hex(0x3a3a6a), 0.35f));
    for (int k = 0; k < 3; ++k) {
        int dx = x + 20 + k * (w - 40) / 3 + 10;
        e4_rect(sc, dx, cy + 8, 14, 26, hex(0x2a4a3a));
        e4_rect(sc, dx + 2, cy + 10, 10, 10, hex(0xe8c878));
    }
    e4_rect(sc, x + w / 2 - 10, cy + 10, 20, 12, hex(0xf0ecd8));
    for (int r = 0; r < 4; ++r) e4_rect(sc, x + w / 2 - 8, cy + 12 + r * 3, 12 + (r % 2) * 4, 1, hex(0x5a5a6a));
    // the canopy: roof edge, sawtooth valance in cream and green, iron columns
    e4_rect(sc, x - 24, cy - 6, w + 48, 3, hex(0x6e7890));
    for (int k = 0; k < (w + 48) / 4; ++k) {
        int vx = x - 24 + k * 4;
        Col c = (k & 1) ? hex(0x2f7a56) : hex(0xeee2c4);
        e4_rect(sc, vx, cy - 3, 4, 4, c);
        e4_rect(sc, vx + 1, cy + 1, 2, 2, c);
    }
    for (int k = 0; k < 6; ++k) {
        int px = x - 16 + k * (w + 32) / 5;
        e4_rect(sc, px - 1, cy + 3, 3, 31, hex(0x2f7a56));
        e4_rect(sc, px - 1, cy + 3, 1, 31, hex(0x4c9a6c));
        e4_rect(sc, px - 3, cy + 3, 7, 2, hex(0x2f7a56));
        e4_rect(sc, px - 2, base - 3, 5, 3, hex(0x2f7a56));
    }
    e4_bench(sc, x + 8, base, 1);
    e4_bench(sc, x + w - 36, base, 2);
}

static void e4_near(Scene& sc)
{
    const int W = sc.W, H = sc.H;
    const int base = (int)(H * 0.87f);
    const E4Leaf L = e4_leaf(0.0f);
    const Col tl = hex(0x9a6a40), td = hex(0x5e3a32);
    const E4Wall brick{hex(0xb46048), hex(0x8a4a44), hex(0xece2cc), true};
    const E4Wall cream{hex(0xeadab4), hex(0xc4b096), hex(0xfaf4e4), false};
    const E4Wall sandW{hex(0xd4a878), hex(0xae8868), hex(0xf2e6cc), false};
    const E4Wall blue{hex(0xa4c4c8), hex(0x84a0b0), hex(0xf4f0e4), false};
    const E4Wall pink{hex(0xe0aaa0), hex(0xbc8a8a), hex(0xf8eee0), false};

    // The camera opens on roughly x 120..810 of this layer, so the signed shops sit fully inside
    // that span or fully outside it, never across the start view's edges or the tile seam.
    // trees behind the buildings first, so the roofs cut in front of them
    e4_tree(sc, 40, base - 4, 50, 3, 11, L, tl, td, 1);
    e4_tree(sc, 392, base - 2, 46, 0, 12, L, tl, td, 1);
    e4_pine(sc, 446, base, 104, 24, 13, L, td, 1);
    e4_tree(sc, 722, base - 2, 50, 2, 14, L, tl, td, 1);
    e4_pine(sc, 818, base, 86, 20, 16, L, td, 1);
    e4_tree(sc, 1168, base - 2, 54, 1, 15, L, tl, td, 1);

    // a row of shops on the left
    e4_building(sc, 150, base, 74, 112, 21, brick, "BAKERY", 0);
    e4_building(sc, 226, base, 64, 96, 22, cream, nullptr, 1);
    e4_building(sc, 292, base, 70, 120, 23, blue, "CAFE", 0);
    // the station
    e4_station(sc, 520, base, 150);
    // high street on the right
    e4_building(sc, 876, base, 72, 104, 31, sandW, "THE RAILWAY", 2);
    e4_building(sc, 950, base, 62, 118, 32, brick, nullptr, 1);
    e4_building(sc, 1014, base, 76, 100, 33, pink, "CHEMIST", 0);

    // street furniture and hedges along the pavement; lamps at uneven gaps
    const int lamps[7] = {22, 136, 372, 474, 708, 852, 1122};
    for (int i = 0; i < 7; ++i) e4_lamp(sc, lamps[i], base, 60 + i);
    e4_hedge(sc, 380, base, 34, 15, 81, L, 1);
    e4_hedge(sc, 690, base, 56, 14, 82, L, 1);
    e4_hedge(sc, 1100, base, 64, 18, 83, L, 1);
    e4_hedge(sc, 1210, base, 46, 13, 84, L, 1);
    e4_bench(sc, 742, base, 7);
    e4_bench(sc, 1140, base, 8);
    // a post box and a telephone box
    {
        int px = 360;
        e4_rect(sc, px, base - 18, 8, 18, hex(0xc8382e));
        e4_rect(sc, px - 1, base - 20, 10, 3, hex(0xa82a26));
        e4_rect(sc, px + 2, base - 14, 4, 1, hex(0x2a2a36));
        int bx = 784;
        e4_rect(sc, bx, base - 40, 14, 40, hex(0xc8382e));
        e4_rect(sc, bx + 11, base - 40, 3, 40, hex(0x9a2a26));
        e4_rect(sc, bx - 1, base - 43, 16, 3, hex(0xa82a26));
        for (int r = 0; r < 4; ++r) e4_rect(sc, bx + 2, base - 34 + r * 6, 8, 4, hex(0xb8d4e4));
        e4_rect(sc, bx + 2, base - 39, 8, 2, hex(0xf4ecd8));
    }
    // pavement and kerb: calm, flat bands (the game's ground covers most of it)
    e4_rect(sc, 0, base, W, 2, hex(0xc8c0b4));
    e4_rect(sc, 0, base + 2, W, H - base - 2, hex(0xa49c94));
    for (int x = 0; x < W; x += 24) e4_rect(sc, x, base + 2, 1, 6, hex(0x948c86));
    e4_rect(sc, 0, base + 8, W, 2, hex(0x8a847e));
}

bool bgEra4(Scene& sc, int layer)
{
    sc.fadeCol = hex(0xc6dcf0);   // a clear blue haze rather than the stage's warm horizon
    if (layer == 0) { sc.fade = 0; e4_sky(sc); }
    else if (layer == 1) { sc.fade = 0.12f; e4_far(sc); }
    else if (layer == 2) { sc.fade = 0.16f; e4_mid(sc); }
    else { sc.fade = 0.08f; e4_near(sc); }
    return true;
}
