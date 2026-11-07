// Background scene for stage 2 (8-bit, 2006, "Sam's first day at school"), included into
// SpriteGen.cpp just before bgLayer(). A bright morning on the school road: blue sky with
// stepped clouds, far lavender hills, the street with the school and Gran's house, and lots of
// chunky trees whose canopies are built from block clusters in three flat greens (lit
// yellow-green top left, mid green, dark blue-teal shadow bottom right).
// Every colour is taken straight from PALETTE16, so quantizing changes nothing.

namespace e2 {
const uint32_t C_INK = 0x0f0d1a, C_NAVY = 0x26305e, C_PLUM = 0x6a2c6e, C_GREEN = 0x1e8a4c, C_BROWN = 0x8c4a2f,
               C_SLATE = 0x5a5468, C_GREY = 0xb8b6c8, C_WHITE = 0xfaf3e6, C_RED = 0xe8364f, C_ORANGE = 0xf28a1e,
               C_YELLOW = 0xf7d73a, C_LIME = 0x6fdc4a, C_BLUE = 0x3c9cf0, C_LAVENDER = 0x8a7fb4, C_PINK = 0xf28cb4,
               C_PEACH = 0xf5c9a0;
}

// One square block of b x b pixels (x wraps inside Px::set, so layers tile).
static void e2_block(Scene& sc, int x, int y, int b, uint32_t c)
{
    for (int j = 0; j < b; ++j)
        for (int i = 0; i < b; ++i) sc.put(x + i, y + j, hex(c));
}

struct E2Ell { float cx, cy, rx, ry; };

// A canopy made of block clusters: the union of a few ellipses (in block units, relative to
// the tree base), jagged at the edge, lit from the top left with clustered three-tone shading.
static void e2_canopy(Scene& sc, int x0, int y0, int b, const E2Ell* e, int n, int seed,
                      uint32_t lit, uint32_t mid, uint32_t dark)
{
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < n; ++i) {
        minx = std::min(minx, e[i].cx - e[i].rx); maxx = std::max(maxx, e[i].cx + e[i].rx);
        miny = std::min(miny, e[i].cy - e[i].ry); maxy = std::max(maxy, e[i].cy + e[i].ry);
    }
    for (int by = (int)std::floor(miny) - 1; by <= (int)std::ceil(maxy) + 1; ++by) {
        int off = (int)(rnd(by / 2, 7, seed) * 2);   // clusters stagger row to row
        for (int bx = (int)std::floor(minx) - 1; bx <= (int)std::ceil(maxx) + 1; ++bx) {
            float mx = bx + 0.5f, my = by + 0.5f;
            float best = 1e9f, nx = 0, ny = 0;
            for (int i = 0; i < n; ++i) {
                float dx = (mx - e[i].cx) / e[i].rx, dy = (my - e[i].cy) / e[i].ry;
                float d = dx * dx + dy * dy;
                if (d < best) { best = d; nx = dx; ny = dy; }
            }
            float jit = (rnd(bx, by, seed + 1) - 0.5f) * 0.35f;
            if (best > 1.0f + jit) continue;
            float cl = rnd((bx + off) / 2 + 50, (by + 40) / 2, seed + 2) - 0.5f;
            float sh = -0.55f * nx - 0.8f * ny + 0.55f * cl + (rnd(bx, by, seed + 3) - 0.5f) * 0.12f;
            uint32_t c = sh > 0.32f ? lit : sh < -0.42f ? dark : mid;
            e2_block(sc, x0 + bx * b, y0 + by * b, b, c);
        }
    }
}

// A straight 2-block trunk, lighter on the left, from the base up `h` blocks.
static void e2_trunk(Scene& sc, int x0, int y0, int b, int h)
{
    for (int j = 1; j <= h; ++j) {
        e2_block(sc, x0 - b, y0 - j * b, b, e2::C_BROWN);
        e2_block(sc, x0, y0 - j * b, b, e2::C_PLUM);
    }
}

static void e2_tuft(Scene& sc, int x0, int y0, int b, int w, int seed)
{
    for (int i = -w; i < w; ++i) {
        float r = rnd(i, 3, seed);
        uint32_t c = r < 0.45f ? e2::C_LIME : r < 0.85f ? e2::C_GREEN : e2::C_NAVY;
        e2_block(sc, x0 + i * b, y0 - b, b, c);
        if (std::abs(i) < w - 1 && rnd(i, 4, seed) < 0.45f)
            e2_block(sc, x0 + i * b, y0 - 2 * b, b, rnd(i, 5, seed) < 0.5f ? e2::C_LIME : e2::C_GREEN);
    }
}

// The six tree kinds of the reference: 0 round, 1 tall oval, 2 stepped pine, 3 forked with two
// canopies, 4 wide round, 5 tall narrow. b = block size in pixels, k = size in blocks.
static void e2_tree(Scene& sc, float xf, float yf, int kind, int b, float k, int seed)
{
    const uint32_t L = e2::C_LIME, M = e2::C_GREEN, D = e2::C_NAVY;
    int x0 = (int)xf, y0 = (int)yf;
    auto K = [&](float v) { return v * k; };
    if (kind == 2) {   // pine: stepped tiers, each wider at its bottom
        e2_trunk(sc, x0, y0, b, (int)std::lround(K(4)));
        int tiers = 5;
        float baseY = -K(3.5f), tierH = K(5.0f), step = K(3.6f);
        for (int t = 0; t < tiers; ++t) {
            float ty = baseY - t * step;                  // bottom of this tier (blocks)
            float hw = K(9.0f) * (1.0f - t * 0.17f);       // half width at the bottom
            for (int r = 0; r < (int)std::lround(tierH); ++r) {
                int by = (int)std::lround(ty) - 1 - r;
                float half = std::max(0.7f, hw * (1.0f - (float)r / tierH * 0.8f));
                int hb = (int)std::lround(half);
                for (int bx = -hb; bx < hb; ++bx) {
                    if (std::abs(bx) == hb - 1 && rnd(bx, by, seed) < 0.3f) continue;   // ragged tips
                    float nx = (bx + 0.5f) / std::max(1.0f, half);
                    float ny = 1.0f - 2.0f * r / tierH;
                    float cl = rnd((bx + 30) / 2, (by + 60) / 2, seed + 2) - 0.5f;
                    float sh = -0.45f * nx - 0.75f * ny + 0.5f * cl + 0.1f;
                    e2_block(sc, x0 + bx * b, y0 + by * b, b, sh > 0.3f ? L : sh < -0.4f ? D : M);
                }
            }
        }
        int top = (int)std::lround(baseY - (tiers - 1) * step - tierH);
        e2_block(sc, x0 - b, y0 + (top - 1) * b, b, L);
        e2_block(sc, x0, y0 + (top - 1) * b, b, M);
        e2_block(sc, x0 - b, y0 + (top - 2) * b, b, M);
        e2_tuft(sc, x0, y0, b, 5, seed + 9);
        return;
    }
    if (kind == 3) {   // forked: trunk splits into a Y, one big canopy and one smaller
        int th = (int)std::lround(K(5));
        e2_trunk(sc, x0, y0, b, th);
        for (int j = 0; j < (int)std::lround(K(6)); ++j) {   // left branch
            int bx = -1 - (int)(j * 0.6f), by = -th - 1 - j;
            e2_block(sc, x0 + bx * b, y0 + by * b, b, e2::C_BROWN);
            e2_block(sc, x0 + (bx + 1) * b, y0 + by * b, b, e2::C_PLUM);
        }
        for (int j = 0; j < (int)std::lround(K(4)); ++j) {   // right branch
            int bx = 1 + (int)(j * 1.1f), by = -th - 1 - (int)(j * 0.6f);
            e2_block(sc, x0 + (bx - 1) * b, y0 + by * b, b, e2::C_BROWN);
            e2_block(sc, x0 + bx * b, y0 + by * b, b, e2::C_PLUM);
        }
        E2Ell a[1] = {{K(-4.5f), K(-18), K(7), K(6.5f)}};
        E2Ell c[1] = {{K(5.5f), K(-13.5f), K(5.5f), K(5.5f)}};
        e2_canopy(sc, x0, y0, b, c, 1, seed + 20, L, M, D);
        e2_canopy(sc, x0, y0, b, a, 1, seed, L, M, D);
        e2_tuft(sc, x0, y0, b, 5, seed + 9);
        return;
    }
    E2Ell e[2];
    int n = 1, th = 6;
    switch (kind) {
    case 0: e[0] = {0, K(-13), K(8), K(7)}; th = (int)K(7); break;
    case 1: e[0] = {0, K(-17), K(6.5f), K(10.5f)}; th = (int)K(7); break;
    case 4: e[0] = {0, K(-12.5f), K(10.5f), K(7.5f)}; th = (int)K(6); break;
    default: e[0] = {0, K(-17), K(5.5f), K(11)}; e[1] = {K(-0.5f), K(-27), K(4), K(4.5f)}; n = 2; th = (int)K(7); break;
    }
    e2_trunk(sc, x0, y0, b, th);
    e2_canopy(sc, x0, y0, b, e, n, seed, L, M, D);
    e2_tuft(sc, x0, y0, b, 5, seed + 9);
}

// A low bush: one wide cluster canopy, sometimes with flowers.
static void e2_bush(Scene& sc, float x, float y, int b, float w, int seed, bool flowers)
{
    E2Ell e[2] = {{0, -3.2f, w, 3.4f}, {w * 0.35f, -4.0f, w * 0.55f, 3.0f}};
    e2_canopy(sc, (int)x, (int)y, b, e, 2, seed, e2::C_LIME, e2::C_GREEN, e2::C_NAVY);
    if (!flowers) return;
    for (int i = 0; i < (int)(w * 1.5f); ++i) {
        int fx = (int)x + (int)((rnd(i, 1, seed) * 2 - 1) * w * 0.8f) * b;
        int fy = (int)y - (int)(1.5f + rnd(i, 2, seed) * 4) * b;
        sc.put(fx, fy, hex(i % 2 ? e2::C_PINK : e2::C_YELLOW));
    }
}

// 3x5 letters for the school sign.
static void e2_text(Scene& sc, int x, int y, const char* t, uint32_t c)
{
    for (; *t; ++t, x += 4) {
        const char* g = nullptr;
        switch (*t) {
        case 'S': g = "111100111001111"; break;
        case 'C': g = "111100100100111"; break;
        case 'H': g = "101101111101101"; break;
        case 'O': g = "111101101101111"; break;
        case 'L': g = "100100100100111"; break;
        default: break;
        }
        if (!g) continue;
        for (int i = 0; i < 15; ++i)
            if (g[i] == '1') sc.put(x + i % 3, y + i / 3, hex(c));
    }
}

static void e2_rect(Scene& sc, int x, int y, int w, int h, uint32_t c) { sc.rect((float)x, (float)y, (float)w, (float)h, hex(c)); }

// Gran's house on the school road: peach walls, red stepped roof, chimney, flower box.
static void e2_granHouse(Scene& sc, int x, int g)
{
    using namespace e2;
    int w = 52, h = 30;
    e2_rect(sc, x, g - h, w, h, C_PEACH);
    e2_rect(sc, x + w - 10, g - h, 10, h, C_ORANGE);                 // shaded side wall
    for (int r = 0; r < 12; ++r) {                                  // stepped gable roof
        int inset = r * 2;
        e2_rect(sc, x - 4 + inset, g - h - 2 * r - 2, w + 8 - 2 * inset, 2, (r % 3 == 2) ? C_PLUM : C_RED);
        e2_rect(sc, x + w / 2 + 2, g - h - 2 * r - 2, std::max(0, w / 2 + 2 - inset), 2, C_PLUM);
    }
    e2_rect(sc, x + 36, g - h - 22, 6, 12, C_BROWN);                  // chimney
    e2_rect(sc, x + 35, g - h - 24, 8, 2, C_SLATE);
    for (int k = 0; k < 3; ++k) e2_rect(sc, x + 38 + k, g - h - 28 - k * 3, 2, 2, C_GREY);   // smoke
    e2_rect(sc, x + 6, g - 22, 12, 10, C_WHITE);                      // windows with a cross frame
    e2_rect(sc, x + 7, g - 21, 10, 8, C_BLUE);
    e2_rect(sc, x + 12, g - 21, 1, 8, C_WHITE); e2_rect(sc, x + 7, g - 17, 10, 1, C_WHITE);
    e2_rect(sc, x + 7, g - 21, 3, 3, C_WHITE);                        // glint
    e2_rect(sc, x + 5, g - 12, 14, 3, C_BROWN);                       // flower box
    for (int k = 0; k < 6; ++k) sc.put(x + 6 + k * 2, g - 13, hex(k % 2 ? C_PINK : C_RED));
    e2_rect(sc, x + 24, g - 18, 10, 18, C_BROWN);                     // door
    e2_rect(sc, x + 25, g - 17, 8, 7, C_PLUM);
    sc.put(x + 31, g - 9, hex(C_YELLOW));
    e2_rect(sc, x + 22, g - 1, 14, 1, C_GREY);                        // step
}

// The school: two floors of windows, a clock tower with a bell, a flag, a double door.
static void e2_school(Scene& sc, int x, int g)
{
    using namespace e2;
    int w = 120, h = 44;
    e2_rect(sc, x, g - h, w, h, C_PEACH);
    for (int y = g - h + 3; y < g; y += 4)                          // brick courses
        for (int bx = x + ((y / 4) % 2) * 3; bx < x + w; bx += 6) sc.put(bx, y, hex(C_ORANGE));
    e2_rect(sc, x + w - 8, g - h, 8, h, C_ORANGE);
    e2_rect(sc, x - 3, g - h - 4, w + 6, 4, C_RED);                   // flat roof edge
    e2_rect(sc, x - 3, g - h - 1, w + 6, 1, C_PLUM);
    int tx = x + w / 2 - 12;                                        // clock tower
    e2_rect(sc, tx, g - h - 26, 24, 22, C_PEACH);
    e2_rect(sc, tx + 18, g - h - 26, 6, 22, C_ORANGE);
    for (int r = 0; r < 7; ++r) e2_rect(sc, tx - 2 + r * 2, g - h - 28 - r * 2, 28 - r * 4, 2, r % 2 ? C_PLUM : C_RED);
    e2_rect(sc, tx + 5, g - h - 22, 12, 12, C_WHITE);                 // clock face
    e2_rect(sc, tx + 6, g - h - 23, 10, 14, C_WHITE);
    e2_rect(sc, tx + 10, g - h - 20, 2, 5, C_INK); e2_rect(sc, tx + 11, g - h - 16, 4, 1, C_INK);
    e2_rect(sc, tx + 23, g - h - 52, 1, 24, C_SLATE);                 // flag pole and flag
    e2_rect(sc, tx + 24, g - h - 52, 10, 6, C_RED);
    e2_rect(sc, tx + 24, g - h - 50, 10, 2, C_WHITE);
    for (int fl = 0; fl < 2; ++fl)
        for (int k = 0; k < 8; ++k) {
            if (fl == 1 && (k == 3 || k == 4)) continue;            // the door goes here
            int wx = x + 6 + k * 14, wy = g - h + 6 + fl * 18;
            e2_rect(sc, wx, wy, 9, 10, C_WHITE);
            e2_rect(sc, wx + 1, wy + 1, 7, 8, C_BLUE);
            e2_rect(sc, wx + 4, wy + 1, 1, 8, C_WHITE);
            e2_rect(sc, wx + 1, wy + 1, 2, 2, C_WHITE);
            e2_rect(sc, wx + 5, wy + 6, 3, 3, C_NAVY);
        }
    int dx = x + w / 2 - 10;                                        // double door with a porch
    e2_rect(sc, dx - 3, g - 22, 26, 3, C_RED);
    e2_rect(sc, dx, g - 19, 20, 19, C_NAVY);
    e2_rect(sc, dx + 9, g - 19, 2, 19, C_BLUE);
    e2_rect(sc, dx - 4, g - 2, 28, 2, C_GREY);
    e2_rect(sc, dx - 2, g - 4, 24, 2, C_GREY);
}

bool bgEra2(Scene& sc, int layer)
{
    using namespace e2;
    const int W = sc.W, H = sc.H;
    sc.fade = 0;   // every colour below is already a palette colour; depth comes from the choice
    if (layer == 0) {
        e2_rect(sc, 0, 0, W, H, C_BLUE);
        // a pale morning sun, stepped
        int sx = (int)(W * 0.80f), sy = (int)(H * 0.16f);
        for (int y = -10; y <= 10; ++y)
            for (int x = -10; x <= 10; ++x) {
                int bx = x / 2 * 2, by = y / 2 * 2;
                float d = std::sqrt((float)(bx * bx + by * by));
                if (d < 9) sc.put(sx + x, sy + y, hex(d < 6 ? C_WHITE : C_YELLOW));
            }
        const float cl[6][3] = {{0.08f, 0.16f, 9}, {0.27f, 0.09f, 6}, {0.45f, 0.22f, 8}, {0.62f, 0.10f, 5}, {0.93f, 0.25f, 7}, {0.36f, 0.33f, 4}};
        for (auto& c : cl) sc.cloud(W * c[0], H * c[1], c[2] * sc.s, hex(C_WHITE), hex(C_GREY));
        // a few birds
        const int birds[4][2] = {{180, 30}, {188, 26}, {196, 32}, {420, 20}};
        for (auto& bd : birds) {
            sc.put(bd[0] - 2, bd[1] - 1, hex(C_NAVY)); sc.put(bd[0] - 1, bd[1], hex(C_NAVY));
            sc.put(bd[0], bd[1] - 1, hex(C_NAVY)); sc.put(bd[0] + 1, bd[1], hex(C_NAVY)); sc.put(bd[0] + 2, bd[1] - 1, hex(C_NAVY));
        }
        return true;
    }
    if (layer == 1) {
        // far hills: a pale grey range, then lavender hills, then a dotted band of distant woods
        for (int x = 0; x < W; ++x) {
            int sx = x / 4 * 4;
            int t1 = (int)(H * 0.47f + ridge((float)sx, W, 5, 9, 4, 2)) / 2 * 2;
            for (int y = t1; y < H; ++y) sc.put(x, y, hex(y < t1 + 2 ? C_WHITE : C_GREY));
        }
        for (int x = 0; x < W; ++x) {
            int sx = x / 2 * 2;
            int t2 = (int)(H * 0.58f + ridge((float)sx, W, 2, 6, 3, 1.5f)) / 2 * 2;
            for (int y = t2; y < H; ++y) sc.put(x, y, hex(C_LAVENDER));
        }
        // distant tree bumps along the lavender hills, lit grey on top
        for (int i = 0; i < 70; ++i) {
            float fx = (i + rnd(i, 1, 41) * 0.6f) * W / 70.0f;
            int sx = (int)fx / 2 * 2;
            int top = (int)(H * 0.58f + ridge((float)sx, W, 2, 6, 3, 1.5f)) / 2 * 2;
            int r = 2 + (int)(rnd(i, 2, 41) * 3);
            for (int y = -r; y <= 0; ++y)
                for (int x = -r; x <= r; ++x) {
                    if (x * x + y * y * 2 > r * r + 1) continue;
                    Col c = hex((x + y < -r + 1) ? C_GREY : (x > r / 2 ? C_SLATE : C_LAVENDER));
                    sc.put(sx + x, top + y + 1, c);
                }
        }
        return true;
    }
    if (layer == 2) {
        // the street: a green verge rising behind, the school, Gran's house, trees between
        int g = (int)(H * 0.785f);
        for (int x = 0; x < W; ++x) {
            int sx = x / 4 * 4;
            int top = (int)(H * 0.675f + ridge((float)sx, W, 7, 3, 2, 1)) / 2 * 2;
            for (int y = top; y < g; ++y) {
                uint32_t c = y < top + 2 ? C_LIME : C_GREEN;
                if (y >= top + 2 && ((x / 2 + y / 2) % 9 == 0) && rnd(x / 2, y / 2, 3) < 0.5f) c = C_LIME;
                sc.put(x, y, hex(c));
            }
        }
        // trees far behind the buildings
        const struct { float x; int kind; float k; } back[] = {
            {0.05f, 2, 0.9f}, {0.31f, 0, 0.9f}, {0.39f, 5, 0.85f}, {0.47f, 2, 0.8f}, {0.77f, 4, 0.8f},
            {0.86f, 1, 0.9f}, {0.97f, 2, 0.75f}};
        int si = 0;
        for (auto& t : back) e2_tree(sc, W * t.x, (float)g - 6, t.kind, 1, t.k, 300 + si++ * 13);
        e2_granHouse(sc, (int)(W * 0.13f), g);
        e2_school(sc, (int)(W * 0.53f), g);
        // pavement and road
        e2_rect(sc, 0, g, W, 2, C_WHITE);
        e2_rect(sc, 0, g + 2, W, 4, C_GREY);
        for (int x = 0; x < W; x += 8) sc.put(x, g + 3, hex(C_LAVENDER));
        e2_rect(sc, 0, g + 6, W, H - g - 6, C_SLATE);
        for (int x = 0; x < W; x += 24) e2_rect(sc, x + 4, g + 14, 10, 2, C_WHITE);
        // a garden path and gate posts at Gran's
        e2_rect(sc, (int)(W * 0.13f) + 23, g, 12, 2, C_GREY);
        // mid trees in front of the verge, mixed kinds and sizes
        const struct { float x; int kind; float k; } mid[] = {
            {0.02f, 0, 1.1f}, {0.24f, 3, 1.2f}, {0.29f, 5, 1.0f}, {0.43f, 4, 1.15f}, {0.49f, 1, 1.1f},
            {0.78f, 2, 1.25f}, {0.84f, 0, 1.0f}, {0.92f, 3, 1.1f}};
        for (auto& t : mid) e2_tree(sc, W * t.x, (float)g, t.kind, 1, t.k, 500 + si++ * 17);
        // a kite over the school yard, its string falling to the yard
        int kx = (int)(W * 0.73f), ky = (int)(H * 0.20f);
        for (int y = -6; y <= 6; ++y) {
            int hw = 4 - std::abs(y) * 4 / 6;
            for (int x = -hw; x <= hw; ++x)
                sc.put(kx + x, ky + y, hex((x < 0) == (y < 0) ? C_YELLOW : C_RED));
        }
        for (int k = 0; k < 6; ++k) sc.put(kx + (k % 2), ky + 7 + k, hex(k % 2 ? C_PINK : C_WHITE));   // tail bows
        for (int k = 0; k < 60; k += 2) sc.put(kx - k / 3, ky + 6 + k, hex(C_WHITE));
        return true;
    }
    // ---- near layer: big trees, bushes, picket fence, mailbox, bike, school sign
    int g = (int)(H * 0.825f);
    const int b = 2;
    // picket fence in two runs
    auto fence = [&](int fx, int n) {
        for (int k = 0; k < n; ++k) {
            int px = fx + k * 6;
            e2_rect(sc, px, g - 14, 4, 14, C_WHITE);
            e2_rect(sc, px + 3, g - 14, 1, 14, C_GREY);
            e2_rect(sc, px + 1, g - 16, 2, 2, C_WHITE);
        }
        e2_rect(sc, fx - 1, g - 11, n * 6, 2, C_GREY);
        e2_rect(sc, fx - 1, g - 5, n * 6, 2, C_GREY);
    };
    fence((int)(W * 0.08f), 14);
    fence((int)(W * 0.60f), 11);
    // trees, all six kinds, mixed sizes
    const struct { float x; int kind; float k; } nearT[] = {
        {0.04f, 1, 1.5f}, {0.25f, 0, 1.3f}, {0.355f, 2, 1.45f}, {0.46f, 3, 1.35f},
        {0.595f, 5, 1.3f}, {0.75f, 4, 1.25f}, {0.89f, 0, 1.6f}};
    int si = 0;
    for (auto& t : nearT) e2_tree(sc, W * t.x, (float)g, t.kind, b, t.k, 900 + si++ * 23);
    // bushes along the fences
    const float bush[6][2] = {{0.12f, 6}, {0.27f, 5}, {0.40f, 7}, {0.65f, 5}, {0.81f, 7}, {0.97f, 5}};
    for (int i = 0; i < 6; ++i) e2_bush(sc, W * bush[i][0], (float)g, b, bush[i][1], 700 + i * 11, i % 2 == 0);
    // mailbox on a post
    int mx = (int)(W * 0.12f);
    e2_rect(sc, mx, g - 14, 2, 14, C_SLATE);
    e2_rect(sc, mx - 5, g - 21, 12, 7, C_RED);
    e2_rect(sc, mx - 5, g - 22, 11, 1, C_RED);
    e2_rect(sc, mx - 5, g - 15, 12, 1, C_PLUM);
    e2_rect(sc, mx + 6, g - 21, 1, 6, C_PLUM);
    e2_rect(sc, mx + 7, g - 25, 1, 6, C_SLATE);
    e2_rect(sc, mx + 7, g - 25, 3, 2, C_YELLOW);
    // a red bike leaning on the second fence
    int bx = (int)(W * 0.665f);
    auto line = [&](int x0, int y0, int x1, int y1, uint32_t c) {
        int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
        for (int k = 0; k <= n; ++k) {
            int x = x0 + (x1 - x0) * k / std::max(1, n), y = y0 + (y1 - y0) * k / std::max(1, n);
            sc.put(x, y, hex(c)); sc.put(x, y + 1, hex(c));
        }
    };
    for (int w2 = 0; w2 < 2; ++w2) {
        int cx = bx + w2 * 20, cy = g - 7;
        for (int y = -7; y <= 7; ++y)
            for (int x = -7; x <= 7; ++x) {
                int d = x * x + y * y;
                if (d <= 49 && d >= 25) sc.put(cx + x, cy + y, hex(C_INK));
            }
        sc.put(cx, cy, hex(C_GREY));
    }
    line(bx, g - 7, bx + 8, g - 7, C_RED);          // chain stay
    line(bx, g - 7, bx + 5, g - 16, C_RED);         // seat stay
    line(bx + 8, g - 7, bx + 5, g - 16, C_RED);     // seat tube
    line(bx + 5, g - 16, bx + 17, g - 16, C_RED);   // top tube
    line(bx + 8, g - 7, bx + 17, g - 16, C_RED);    // down tube
    line(bx + 17, g - 16, bx + 20, g - 7, C_SLATE); // fork
    e2_rect(sc, bx + 2, g - 19, 6, 2, C_INK);       // saddle
    e2_rect(sc, bx + 16, g - 20, 2, 4, C_SLATE);    // handlebar
    e2_rect(sc, bx + 14, g - 21, 6, 2, C_INK);
    e2_rect(sc, bx + 7, g - 7, 3, 2, C_GREY);       // pedal
    // the school sign: a blue board on two posts
    int sx = (int)(W * 0.53f);
    e2_rect(sc, sx + 2, g - 14, 2, 14, C_SLATE);
    e2_rect(sc, sx + 26, g - 14, 2, 14, C_SLATE);
    e2_rect(sc, sx - 1, g - 26, 32, 13, C_WHITE);
    e2_rect(sc, sx, g - 25, 30, 11, C_NAVY);
    e2_text(sc, sx + 4, g - 22, "SCHOOL", C_WHITE);
    // a calm verge along the bottom
    for (int x = 0; x < W; ++x) {
        sc.put(x, g, hex(C_GREEN));
        if (rnd(x / 2, 1, 77) < 0.3f) sc.put(x, g - 1, hex(C_GREEN));
    }
    e2_rect(sc, 0, g + 1, W, H - g - 1, C_GREEN);
    return true;
}
