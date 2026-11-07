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

// Leaf colour sets (lit, mid, shadow): shadows lean blue or purple, light leans yellow. It is
// early September, so a few trees have started to turn.
struct E2Hue { uint32_t lit, mid, dark; };
static const E2Hue E2_HUES[6] = {
    {e2::C_LIME, e2::C_GREEN, e2::C_NAVY},      // 0 summer green
    {e2::C_LIME, e2::C_GREEN, e2::C_SLATE},     // 1 green with a purple-grey shade
    {e2::C_YELLOW, e2::C_LIME, e2::C_GREEN},    // 2 turning, yellow-lit
    {e2::C_YELLOW, e2::C_ORANGE, e2::C_BROWN},  // 3 autumn
    {e2::C_ORANGE, e2::C_BROWN, e2::C_PLUM},    // 4 copper beech
    {e2::C_GREEN, e2::C_SLATE, e2::C_NAVY}};    // 5 dark evergreen

// A crown (ellipse, in pixels) filled with clumps; appended to bl.
static void e2_crown(std::vector<E1Blob>& bl, float cx, float cy, float rx, float ry, int seed)
{
    std::vector<E1Blob> c;
    float m = std::min(rx, ry);
    c.push_back({cx, cy, m * 0.6f});
    for (int ring = 0; ring < 2; ++ring) {
        int n = ring == 0 ? 5 : 8 + (ry > rx * 1.2f ? 3 : 0) + (rx > ry * 1.2f ? 2 : 0);
        float d = ring == 0 ? 0.4f : 0.72f;
        for (int i = 0; i < n; ++i) {
            float a = (i + 0.5f * ring) * 6.2831853f / n + (rnd(i, seed + ring, 61) - 0.5f) * 0.5f;
            float rr = m * (0.28f + 0.1f * rnd(i, seed + ring, 62));
            c.push_back({cx + std::cos(a) * rx * d, cy + std::sin(a) * ry * d, rr});
        }
    }
    std::sort(c.begin() + 1, c.end(), [](const E1Blob& p, const E1Blob& q) { return p.y < q.y; });
    for (auto& q : c) bl.push_back(q);
}

static void e2_paintCanopy(Scene& sc, const std::vector<E1Blob>& bl, const E2Hue& h)
{
    clumpCanopy(bl, 1, 2, [&](int x, int y, int l, bool) { sc.put(x, y, hex(l == 0 ? h.lit : l == 1 ? h.mid : h.dark)); });
}

// A straight trunk tw pixels wide, lighter on the left, from the base up h pixels.
static void e2_trunk(Scene& sc, int x0, int y0, int tw, int h)
{
    for (int j = 1; j <= h; ++j)
        for (int i = 0; i < tw; ++i) sc.put(x0 - tw / 2 + i, y0 - j, hex(i < (tw + 1) / 2 ? e2::C_BROWN : e2::C_PLUM));
}

// A grass tuft at a tree's foot: a short, calm row of blades.
static void e2_tuft(Scene& sc, int x0, int y0, int w, int seed)
{
    for (int i = -w; i < w; ++i) {
        int h = 1 + (int)(rnd(i, 4, seed) * 2.5f) * (std::abs(i) < w - 2 ? 1 : 0);
        for (int j = 1; j <= h; ++j) sc.put(x0 + i, y0 - j, hex(j == h && (i + seed) % 3 == 0 ? e2::C_LIME : e2::C_GREEN));
    }
}

// The six tree kinds of the reference: 0 round, 1 tall oval, 2 stepped pine, 3 forked with two
// canopies, 4 wide round, 5 tall narrow. k = size (1 = about 16 px crown), hue = E2_HUES index,
// flip mirrors the tree, so no two neighbours are the same.
static void e2_tree(Scene& sc, float xf, float yf, int kind, float k, int seed, int hue, bool flip)
{
    const E2Hue& H = E2_HUES[hue];
    int x0 = (int)xf, y0 = (int)yf;
    float f = flip ? -1.0f : 1.0f;
    auto K = [&](float v) { return v * k; };
    int tw = std::max(2, (int)std::lround(K(2.2f)));
    std::vector<E1Blob> bl;
    if (kind == 2) {   // pine: stepped tiers, each wider at its bottom, clean three-tone shading
        e2_trunk(sc, x0, y0, tw, (int)K(7));
        int tiers = 5;
        float baseY = -K(6), tierH = K(10), step = K(7);
        for (int t = 0; t < tiers; ++t) {
            float ty = baseY - t * step;
            float hw = K(16.0f) * (1.0f - t * 0.17f);
            int rows = (int)std::lround(tierH);
            for (int r = 0; r < rows; ++r) {
                int y = y0 + (int)std::lround(ty) - 1 - r;
                float half = std::max(1.0f, hw * (1.0f - (float)r / tierH * 0.8f));
                int hb = (int)std::lround(half);
                if (r == 0 && rnd(t, seed, 5) < 0.5f) hb -= 1;
                for (int bx = -hb; bx < hb; ++bx) {
                    float nx = f * (bx + 0.5f) / std::max(1.0f, half);
                    uint32_t c = H.mid;
                    if (nx < -0.3f && r > 1) c = H.lit;
                    if (nx > 0.4f || r < 2) c = H.dark;
                    if (r < 2 && nx < -0.5f) c = H.mid;
                    sc.put(x0 + bx, y, hex(c));
                }
            }
        }
        int top = y0 + (int)std::lround(baseY - (tiers - 1) * step - tierH);
        for (int j = 1; j <= (int)K(3); ++j) sc.put(x0 - 1, top - j, hex(H.mid));
        e2_tuft(sc, x0, y0, (int)K(9), seed + 9);
        return;
    }
    if (kind == 3) {   // forked: the trunk splits into a Y, a big canopy and a smaller one
        int th = (int)K(10);
        e2_trunk(sc, x0, y0, tw, th);
        for (int j = 0; j < (int)K(12); ++j) {
            int bx = (int)(f * (-1 - j * 0.6f)), by = -th - 1 - j;
            for (int i = 0; i < std::max(2, tw - 1); ++i) sc.put(x0 + bx + i - 1, y0 + by, hex(i == 0 ? e2::C_BROWN : e2::C_PLUM));
        }
        for (int j = 0; j < (int)K(8); ++j) {
            int bx = (int)(f * (1 + j * 1.1f)), by = -th - 1 - (int)(j * 0.6f);
            for (int i = 0; i < std::max(2, tw - 1); ++i) sc.put(x0 + bx + i - 1, y0 + by, hex(i == 0 ? e2::C_BROWN : e2::C_PLUM));
        }
        e2_crown(bl, x0 + f * K(11), y0 - K(27), K(11), K(11), seed + 20);
        e2_paintCanopy(sc, bl, H);
        bl.clear();
        e2_crown(bl, x0 - f * K(9), y0 - K(36), K(14), K(13), seed);
        e2_paintCanopy(sc, bl, H);
        e2_tuft(sc, x0, y0, (int)K(9), seed + 9);
        return;
    }
    float cx = 0, cy = -26, rx = 16, ry = 14, th = 14;
    switch (kind) {
    case 0: break;
    case 1: cy = -34; rx = 13; ry = 21; break;
    case 4: cy = -25; rx = 21; ry = 15; th = 12; break;
    default: cy = -36; rx = 11; ry = 24; break;   // tall narrow
    }
    e2_trunk(sc, x0, y0, tw, (int)K(th) + (int)K(ry * 0.5f));
    e2_crown(bl, x0 + f * K(cx), y0 + K(cy), K(rx), K(ry), seed);
    if (kind == 5) e2_crown(bl, x0 - f * K(1), y0 + K(cy - ry * 0.95f), K(rx * 0.6f), K(ry * 0.35f), seed + 3);
    e2_paintCanopy(sc, bl, H);
    e2_tuft(sc, x0, y0, (int)K(9), seed + 9);
}

// A low bush: a wide clump canopy, sometimes with flowers. w = half width in pixels.
static void e2_bush(Scene& sc, float x, float y, float w, int seed, int hue, bool flowers)
{
    std::vector<E1Blob> bl;
    float h = w * (0.45f + 0.15f * rnd(seed, 1, 7));
    float f = rnd(seed, 2, 7) < 0.5f ? -1.0f : 1.0f;
    e2_crown(bl, x, y - h * 0.9f, w, h, seed);
    bl.push_back({x + f * w * 0.45f, y - h * 1.3f, h * 0.7f});
    e2_paintCanopy(sc, bl, E2_HUES[hue]);
    for (int i = 0; flowers && i < (int)(w * 0.6f); ++i) {
        int fx = (int)x + (int)((rnd(i, 1, seed) * 2 - 1) * w * 0.8f);
        int fy = (int)y - (int)(2 + rnd(i, 2, seed) * h * 1.2f);
        sc.put(fx, fy, hex(i % 2 ? e2::C_PINK : e2::C_WHITE));
    }
}

// A stepped cloud in 2-px blocks, each one its own shape (puff count, sizes and spread from
// seed). White top, warm peach underside in the morning light.
static void e2_cloud(Scene& sc, float cx, float cy, float size, int seed, float stretch)
{
    const int b = 2;
    struct P { float x, y, r; };
    std::vector<P> puffs;
    int n = 2 + (int)(rnd(seed, 1, 91) * 4);
    float span = size * (1.0f + stretch);
    int big = (int)(rnd(seed, 2, 91) * n);
    for (int i = 0; i < n; ++i) {
        float t = n == 1 ? 0 : (float)i / (n - 1) * 2 - 1;
        float r = size * (0.45f + 0.35f * rnd(seed, 10 + i, 91)) * (i == big ? 1.4f : 1.0f);
        puffs.push_back({cx + t * span + (rnd(seed, 20 + i, 91) - 0.5f) * size * 0.5f, cy + size * 0.35f - r * 0.75f, r});
    }
    float bottom = cy + size * 0.4f;
    auto in = [&](int gx, int gy) {
        float mx = (gx + 0.5f) * b, my = (gy + 0.5f) * b;
        if (my > bottom) return false;
        for (auto& p : puffs)
            if ((mx - p.x) * (mx - p.x) + (my - p.y) * (my - p.y) * 1.6f < p.r * p.r) return true;
        return false;
    };
    int gx0 = (int)std::floor((cx - span - size * 2) / b), gx1 = (int)std::ceil((cx + span + size * 2) / b);
    int gy0 = (int)std::floor((cy - size * 2.5f) / b), gy1 = (int)std::ceil(bottom / b) + 1;
    for (int gy = gy0; gy <= gy1; ++gy)
        for (int gx = gx0; gx <= gx1; ++gx) {
            if (!in(gx, gy)) continue;
            bool under = !in(gx, gy + 1) || (!in(gx, gy + 2) && (gx + seed) % 3 != 0);
            e2_block(sc, gx * b, gy * b, b, under ? e2::C_PEACH : e2::C_WHITE);
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
        // a hazy September morning: lilac-grey sky, warm peach low down behind the hills
        for (int y = 0; y < H; ++y) {
            bool low = y >= (int)(H * 0.42f) || (y >= (int)(H * 0.4f) && ((y + 0) & 1) == 0 && ((y / 2) % 2 == 0));
            e2_rect(sc, 0, y, W, 1, low ? C_PEACH : C_GREY);
        }
        // a pale morning sun, stepped, white with a yellow ring
        int sx = (int)(W * 0.80f), sy = (int)(H * 0.27f);
        for (int y = -10; y <= 10; ++y)
            for (int x = -10; x <= 10; ++x) {
                int bx = x / 2 * 2, by = y / 2 * 2;
                float d = std::sqrt((float)(bx * bx + by * by));
                if (d < 9) sc.put(sx + x, sy + y, hex(d < 6 ? C_WHITE : C_YELLOW));
            }
        // clouds kept below the top ~13% (the HUD), every one a different shape
        const float cl[6][4] = {{0.08f, 0.21f, 8, 0.8f}, {0.27f, 0.17f, 5, 2.2f}, {0.45f, 0.26f, 7, 0.3f},
                                {0.62f, 0.19f, 4, 1.4f}, {0.93f, 0.25f, 6, 0.6f}, {0.36f, 0.33f, 3.5f, 2.8f}};
        int ci = 0;
        for (auto& c : cl) e2_cloud(sc, W * c[0], H * c[1], c[2] * sc.s, 17 + 11 * ci++, c[3]);
        const int birds[4][2] = {{180, 40}, {188, 36}, {196, 42}, {420, 34}};
        for (auto& bd : birds) {
            sc.put(bd[0] - 2, bd[1] - 1, hex(C_SLATE)); sc.put(bd[0] - 1, bd[1], hex(C_SLATE));
            sc.put(bd[0], bd[1] - 1, hex(C_SLATE)); sc.put(bd[0] + 1, bd[1], hex(C_SLATE)); sc.put(bd[0] + 2, bd[1] - 1, hex(C_SLATE));
        }
        return true;
    }
    if (layer == 1) {
        // far mountains: lavender, faces turned to the morning sun catch a warm peach light
        auto mtop = [&](int x) { int sx = ((x % W) + W) % W / 4 * 4; return (int)(H * 0.47f + ridge((float)sx, W, 5, 9, 4, 2)) / 2 * 2; };
        for (int x = 0; x < W; ++x) {
            int t1 = mtop(x);
            bool lit = mtop(x + 8) < mtop(x - 8);   // rising to the right: facing the sun (left)
            for (int y = t1; y < H; ++y) sc.put(x, y, hex(lit && y < t1 + 6 ? C_PEACH : C_LAVENDER));
        }
        // nearer hills: slate-purple distance, with clean round woods along the crest
        auto htop = [&](int x) { int sx = ((x % W) + W) % W / 2 * 2; return (int)(H * 0.6f + ridge((float)sx, W, 2, 6, 3, 1.5f)) / 2 * 2; };
        for (int x = 0; x < W; ++x)
            for (int y = htop(x); y < H; ++y) sc.put(x, y, hex(C_SLATE));
        for (int i = 0; i < 60; ++i) {
            float fx = (i + rnd(i, 1, 41) * 0.6f) * W / 60.0f;
            int sx = (int)fx, top = htop(sx);
            int r = 3 + (int)(rnd(i, 2, 41) * 3);
            for (int y = -r; y <= 0; ++y)
                for (int x = -r; x <= r; ++x) {
                    if (x * x + y * y > r * r) continue;
                    bool cap = (x + 1) * (x + 1) + (y + 1) * (y + 1) > (r - 1) * (r - 1) && x < 0 && y < -r / 2;
                    sc.put(sx + x, top + y + 1, hex(cap ? C_LAVENDER : C_SLATE));
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
                sc.put(x, y, hex(c));
            }
        }
        // trees far behind the buildings
        const struct { float x; int kind; float k; } back[] = {
            {0.05f, 2, 0.9f}, {0.31f, 0, 0.9f}, {0.39f, 5, 0.85f}, {0.47f, 2, 0.8f}, {0.77f, 4, 0.8f},
            {0.86f, 1, 0.9f}, {0.97f, 2, 0.75f}};
        int si = 0;
        for (auto& t : back) { e2_tree(sc, W * t.x, (float)g - 6, t.kind, t.k * 0.5f, 300 + si * 13, si % 3 == 0 ? 5 : 1, si % 2 == 1); ++si; }
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
        for (auto& t : mid) { const int hues[8] = {0, 2, 1, 3, 0, 5, 1, 2}; e2_tree(sc, W * t.x, (float)g, t.kind, t.k * 0.5f, 500 + si * 17, hues[si % 8], si % 2 == 0); ++si; }
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
    for (auto& t : nearT) { const int hues[7] = {0, 3, 5, 1, 2, 4, 0}; e2_tree(sc, W * t.x, (float)g, t.kind, t.k, 900 + si * 23, hues[si], si % 2 == 1); ++si; }
    // bushes along the fences
    const float bush[6][2] = {{0.12f, 6}, {0.27f, 5}, {0.40f, 7}, {0.65f, 5}, {0.81f, 7}, {0.97f, 5}};
    for (int i = 0; i < 6; ++i) e2_bush(sc, W * bush[i][0], (float)g, bush[i][1] * b * (0.85f + 0.3f * rnd(i, 1, 70)), 700 + i * 11, i % 3 == 2 ? 2 : (i % 2 ? 1 : 0), i % 2 == 0);
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
    int sx = (int)(W * 0.415f);   // inside the opening view, never cut by the screen edge
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
