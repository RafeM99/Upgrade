// Background scene for stage 3 (16-bit, 1979, "The camping trip"), included into SpriteGen.cpp
// just before bgLayer(). A bright daytime woodland in the treehouse style: blue sky with stepped
// clouds and a pale sun, layered blue mountains, a dense band of pines and clustered broadleaf
// trees with a glimpse of the lake, and near the camera Gran's treehouse in a huge clustered
// tree, fences, bushes, a grey rock and the family's little tent.
// Every colour is taken straight from PALETTE32, and the depth fade is off (the stage's
// horizon colour is a sunset orange, which would muddy a daytime scene).

namespace {

// Palette entries used here (all from PALETTE32).
const uint32_t E3_WHITE = 0xf4f0e6, E3_LAV = 0xa8a4d0, E3_LAVD = 0x6e6a9e, E3_PURD = 0x463a66, E3_NIGHT = 0x2a2140;
const uint32_t E3_SKYD = 0x2a3a6a, E3_SKY2 = 0x3a6ab0, E3_SKY = 0x5aa0e0, E3_SKYL = 0x9ad0f0, E3_SLATE = 0x4a7a9a;
const uint32_t E3_G0 = 0x1a3a2a, E3_G1 = 0x2a6a3a, E3_G2 = 0x4a9a3a, E3_G3 = 0x8ac84a, E3_G4 = 0xd0e870;
const uint32_t E3_W0 = 0x3a2018, E3_W1 = 0x6a3a22, E3_W2 = 0x9a5a30, E3_W3 = 0xc88a4a, E3_W4 = 0xf0c070;
const uint32_t E3_R0 = 0x5a1a2a, E3_R1 = 0xa02a3a, E3_R2 = 0xe04a3a, E3_R3 = 0xf08a4a, E3_LIGHT = 0xf8d070;

struct E3Blob { float x, y, r; };

// Four tones for a cluster canopy: lit (top left, yellow-green), mid, shadow, deep (teal).
struct E3Leaf { uint32_t lit, mid, dark, deep; };

// A canopy built from round clusters, drawn back to front on a grid of b-pixel blocks, so the
// edges step. Each cluster is shaded on its own (light from the top left), which gives the
// bunched look; the lower blobs of the whole canopy sink toward the deep tone.
void e3_canopy(Scene& sc, const std::vector<E3Blob>& blobs, E3Leaf L, int b, int seed, float cy0, float cy1)
{
    for (size_t i = 0; i < blobs.size(); ++i) {
        const E3Blob& o = blobs[i];
        int x0 = (int)std::floor((o.x - o.r - b) / b), x1 = (int)std::ceil((o.x + o.r + b) / b);
        int y0 = (int)std::floor((o.y - o.r - b) / b), y1 = (int)std::ceil((o.y + o.r + b) / b);
        for (int gy = y0; gy <= y1; ++gy)
            for (int gx = x0; gx <= x1; ++gx) {
                float mx = gx * b + b * 0.5f, my = gy * b + b * 0.5f;
                float nx = (mx - o.x) / o.r, ny = (my - o.y) / (o.r * 0.88f);
                float d = nx * nx + ny * ny;
                float jag = 1.0f + (rnd(gx, gy, seed + (int)i) - 0.5f) * 0.28f;
                if (d > jag) continue;
                float v = -(0.55f * nx + 0.85f * ny) + (rnd(gx, gy, seed * 7 + 3) - 0.5f) * 0.3f;
                // whole-canopy shading: lower clusters darker
                float t = cy1 > cy0 ? (my - cy0) / (cy1 - cy0) : 0.5f;
                v -= (t - 0.45f) * 0.9f;
                uint32_t c = v > 0.42f ? L.lit : v > -0.15f ? L.mid : v > -0.75f ? L.dark : L.deep;
                sc.rect((float)gx * b, (float)gy * b, (float)b, (float)b, hex(c));
            }
    }
}

// A broadleaf tree: straight trunk (lit left, dark right), a few clusters, a grass tuft.
void e3_broadleaf(Scene& sc, float x, float base, float k, int kind, int seed, E3Leaf L, int b, float trunkK = 1.0f)
{
    float th = 22 * k * trunkK, tw = std::max(2.0f, 4 * k);
    sc.rect(x - tw * 0.5f, base - th, tw, th, hex(E3_W1));
    sc.rect(x - tw * 0.5f, base - th, std::max(1.0f, tw * 0.35f), th, hex(E3_W2));
    sc.rect(x + tw * 0.5f - std::max(1.0f, tw * 0.3f), base - th, std::max(1.0f, tw * 0.3f), th, hex(E3_W0));
    std::vector<E3Blob> bl;
    float cy = base - th - 10 * k;
    if (kind == 0) {   // round
        bl = {{x - 7 * k, cy + 2 * k, 9 * k}, {x + 7 * k, cy + 3 * k, 9 * k}, {x, cy - 5 * k, 11 * k}, {x - 2 * k, cy + 5 * k, 9 * k}};
    } else if (kind == 1) {   // tall oval
        bl = {{x, cy - 12 * k, 8 * k}, {x - 4 * k, cy - 2 * k, 9 * k}, {x + 4 * k, cy + 2 * k, 9 * k}, {x, cy + 6 * k, 8 * k}};
    } else {   // forked: two round canopies
        sc.rect(x - tw * 0.5f - 4 * k, base - th - 4 * k, tw * 0.6f, 6 * k, hex(E3_W1));
        bl = {{x - 9 * k, cy + 2 * k, 9 * k}, {x - 7 * k, cy - 6 * k, 8 * k}, {x + 8 * k, cy + 6 * k, 8 * k}};
    }
    e3_canopy(sc, bl, L, b, seed, cy - 18 * k, cy + 14 * k);
}

// A stepped pine: tiers that widen downward, lit on the left, teal shadow on the right.
// Every tree differs: width, tier count and droop, a lean of the tip, a lopsided side (flip),
// ragged hems and an occasional broken tier, all from its seed.
void e3_pine(Scene& sc, float x, float base, float h, int seed, uint32_t lit, uint32_t mid, uint32_t dark, uint32_t trunk)
{
    int ix = (int)std::lround(x);
    int top = (int)std::lround(base - h);
    int trunkH = std::max(2, (int)(h * (0.08f + 0.08f * rnd(seed, 1, 701))));
    for (int k = -1; k < 1; ++k) for (int y = (int)(base - trunkH); y < (int)base; ++y) sc.put(((ix + k) % sc.W + sc.W) % sc.W, y, hex(trunk));
    int bodyH = (int)h - trunkH;
    int tiers = std::max(3, (int)(bodyH / (7.0f + 5.0f * rnd(seed, 2, 701))));
    float tierH = (float)bodyH / tiers;
    float maxHalf = h * (0.17f + 0.12f * rnd(seed, 3, 701));
    float droop = 0.3f + 0.35f * rnd(seed, 4, 701);                 // how much each tier narrows at its top
    float side = rnd(seed, 5, 701) < 0.5f ? -1.0f : 1.0f;           // flip: which side is fuller
    float lop = 0.12f + 0.2f * rnd(seed, 6, 701);
    float lean = (rnd(seed, 7, 701) - 0.5f) * 3.0f;                 // tip offset in pixels
    for (int y = 0; y < bodyH; ++y) {
        int tier = (int)(y / tierH);
        float inTier = (y - tier * tierH) / tierH;          // 0 at tier top .. 1 at its bottom
        float tierMax = maxHalf * (0.3f + 0.7f * (tier + 1) / tiers) * (0.85f + 0.3f * rnd(seed, 20 + tier, 702));
        float half = tierMax * (droop + (1 - droop) * inTier);
        if (tier == 0) half = tierMax * inTier;
        float u = (float)y / bodyH;
        int cx = ix + (int)std::lround(lean * (1 - u));
        int hwL = (int)std::lround(half * (side < 0 ? 1 + lop : 1 - lop * 0.5f));
        int hwR = (int)std::lround(half * (side > 0 ? 1 + lop : 1 - lop * 0.5f));
        for (int dx = -hwL; dx <= hwR; ++dx) {
            int edge = dx < 0 ? hwL : hwR;
            // ragged needle tips on the tier bottoms, in 2px clumps
            if (std::abs(dx) >= edge - 1 && inTier > 0.65f && rnd((cx + dx) / 2, tier, seed) < 0.55f) continue;
            float rel = dx < 0 ? (float)dx / std::max(1, hwL) : (float)dx / std::max(1, hwR);
            uint32_t c = rel < -0.3f ? lit : rel < 0.3f ? mid : dark;
            if (inTier > 0.8f && rel > -0.5f) c = c == lit ? mid : dark;   // tier underside shadow
            sc.put(((cx + dx) % sc.W + sc.W) % sc.W, top + y, hex(c));   // wrapped: the rows tile
        }
    }
}

// A cloud of stepped blocks: white top, grey-blue underside.
void e3_cloud(Scene& sc, float cx, float cy, float w, int seed)
{
    const int b = 2;
    std::vector<E3Blob> bl;
    int n = 3 + (int)(rnd(seed, 1, 77) * 3);
    for (int i = 0; i < n; ++i) {
        float t = n > 1 ? (float)i / (n - 1) : 0.5f;
        float r = w * (0.16f + 0.12f * std::sin(t * 3.14159f)) * (0.8f + 0.4f * rnd(seed, i, 78));
        bl.push_back({cx - w * 0.5f + t * w, cy - r * 0.3f, r});
    }
    float bottom = cy + w * 0.08f;
    for (int pass = 0; pass < 2; ++pass)
        for (auto& o : bl)
            for (float y = std::floor((o.y - o.r) / b) * b; y < bottom; y += b)
                for (float x = std::floor((o.x - o.r * 1.4f) / b) * b; x < o.x + o.r * 1.4f; x += b) {
                    float mx = x + 1, my = y + 1;
                    float nx = (mx - o.x) / (o.r * 1.4f), ny = (my - o.y) / o.r;
                    if (nx * nx + ny * ny > 1) continue;
                    bool under = my > cy - 1;
                    if (pass == 0) sc.rect(x, y, b, b, hex(under ? E3_LAV : E3_WHITE));
                    else if (!under && ny > 0.35f && nx > 0.1f) sc.rect(x, y, b, b, hex(E3_SKYL));
                }
}

// A mountain range: straight-sided peaks at irregular spacing (tiling), each with a lit left face,
// a shaded right face split along a single ridge crease, and a thin lit rim on the left skyline.
void e3_range(Scene& sc, float baseY, float amp, int seed, int peaks, uint32_t lit, uint32_t body, uint32_t shade)
{
    const int W = sc.W, H = sc.H;
    std::vector<float> px(peaks), ph(peaks), pw(peaks), pc(peaks);
    for (int i = 0; i < peaks; ++i) {
        px[i] = (i + 0.15f + 0.7f * rnd(i, 0, seed)) * W / peaks;
        ph[i] = amp * (0.55f + 0.45f * rnd(i, 1, seed));
        pw[i] = ph[i] * (1.5f + 0.9f * rnd(i, 2, seed));              // half-width at the base
        pc[i] = 0.25f + 0.35f * rnd(i, 3, seed);                      // crease slope (x per y)
    }
    auto wdx = [&](float a, float b) { float d = a - b; while (d > W * 0.5f) d -= W; while (d < -W * 0.5f) d += W; return d; };
    for (int x = 0; x < W; ++x) {
        float best = -1e9f; int bi = 0; float bdx = 0;
        for (int i = 0; i < peaks; ++i) {
            float d = wdx(x + 0.5f, px[i]);
            float hh = ph[i] * (1 - std::fabs(d) / pw[i]);
            if (hh > best) { best = hh; bi = i; bdx = d; }
        }
        // a small shoulder on the long slopes, smooth so no columns appear
        best += 2.0f * std::sin(x * 6.2832f * 11 / W + seed);
        int ty = (int)std::lround(baseY - best);
        float apexY = baseY - ph[bi];
        for (int y = std::max(0, ty); y < H; ++y) {
            uint32_t c = body;
            float crease = (y - apexY) * pc[bi];                 // the ridge line runs down-right of the apex
            if (bdx > crease) c = shade;                          // right of the crease: shadow face
            else if (bdx < 0 && y - ty < 2) c = lit;              // lit rim along the left skyline
            if (bdx > crease && y - ty < 1) c = body;             // soft rim on the shadow side
            sc.put(x, y, hex(c));
        }
    }
}

// The treehouse tree, the house, ladder, swing and lanterns.
void e3_treehouse(Scene& sc, float tx, float g)
{
    const float H = (float)sc.H;
    // trunk: flared base, three tones, bark streaks
    float trTop = H * 0.42f;
    for (int y = (int)trTop; y < (int)g; ++y) {
        float t = (y - trTop) / (g - trTop);
        float half = 9 + 2 * t + (t > 0.82f ? (t - 0.82f) * 55 : 0);
        int x0 = (int)std::lround(tx - half), x1 = (int)std::lround(tx + half);
        for (int x = x0; x <= x1; ++x) {
            float rel = (x - tx) / half;
            uint32_t c = rel < -0.45f ? E3_W2 : rel < 0.35f ? E3_W1 : E3_W0;
            if (rel > -0.45f && rel < 0.3f && rnd(x, y / 5, 301) < 0.13f) c = E3_W0;   // bark streak
            if (rel < -0.55f && rnd(x, y / 4, 302) < 0.2f) c = E3_W3;
            sc.put(x, y, hex(c));
        }
    }
    // branches reaching into the canopy
    auto branch = [&](float x0, float y0, float x1, float y1, float w) {
        int n = (int)std::max(std::abs(x1 - x0), std::abs(y1 - y0));
        for (int i = 0; i <= n; ++i) {
            float t = (float)i / n, x = x0 + (x1 - x0) * t, y = y0 + (y1 - y0) * t;
            float ww = w * (1 - 0.5f * t);
            sc.rect(std::round(x - ww * 0.5f), std::round(y - ww * 0.5f), std::ceil(ww), std::ceil(ww * 0.6f), hex(E3_W1));
            sc.rect(std::round(x - ww * 0.5f), std::round(y - ww * 0.5f), std::ceil(ww), 1, hex(E3_W2));
        }
    };
    branch(tx - 4, trTop + 8, tx - 34, trTop - 18, 6);
    branch(tx + 4, trTop + 6, tx + 36, trTop - 16, 6);
    branch(tx, trTop + 4, tx + 3, trTop - 26, 7);

    // canopy: a big bunch of clusters
    std::vector<E3Blob> bl;
    const float cx = tx, cy = H * 0.335f, ck = 0.74f;   // the whole crown stays below ~15% of the height
    const float pts[][3] = {
        {-46, 8, 18}, {46, 10, 18}, {-30, -12, 20}, {28, -14, 20}, {0, -28, 21}, {-14, -40, 15}, {18, -40, 14},
        {-58, 22, 14}, {60, 24, 14}, {-38, 24, 16}, {38, 26, 16}, {0, -6, 22}, {-20, 10, 17}, {22, 12, 17}};
    for (auto& q : pts) bl.push_back({cx + q[0] * ck, cy + q[1] * ck, q[2] * ck});
    e3_canopy(sc, bl, {E3_G3, E3_G2, E3_G1, E3_G0}, 3, 311, cy - 50 * ck, cy + 38 * ck);
    // a few bright leaf highlights on top
    for (int i = 0; i < 26; ++i) {
        float a = rnd(i, 0, 312) * 6.28f, r = rnd(i, 1, 312);
        float x = cx - 30 + rnd(i, 2, 312) * 45, y = cy - 40 + r * 24;
        (void)a;
        int bx = (int)(x / 3) * 3, by = (int)(y / 3) * 3;
        // only on lit leaves
        Color* d = (Color*)sc.p.img.data;
        int wx = ((bx % sc.W) + sc.W) % sc.W;
        if (by < 0 || by >= sc.H) continue;
        Color o = d[(size_t)by * sc.W + wx];
        if (o.a > 0 && o.r == 0x8a) sc.rect((float)bx, (float)by, 3, 3, hex(E3_G4));
    }

    // the platform, house and balcony, sitting in the lower canopy
    const float py = H * 0.47f;                // platform floor top
    const float hx0 = tx - 30, hx1 = tx + 26;   // platform extent
    // support beams under the platform
    for (int i = 0; i < 2; ++i) {
        float bx = i == 0 ? hx0 + 4 : hx1 - 6;
        for (int k = 0; k < 14; ++k) sc.rect(bx + (i == 0 ? k * 0.8f : -k * 0.8f), py + 3 + k, 2, 1, hex(E3_W0));
    }
    // house body: horizontal planks
    const float wx0 = tx - 22, wx1 = tx + 14, wTop = py - 26;
    for (int y = (int)wTop; y < (int)py; ++y) {
        int row = (int)(y - wTop) / 3;
        for (int x = (int)wx0; x < (int)wx1; ++x) {
            uint32_t c = ((int)(y - wTop) % 3 == 2) ? E3_W1 : (row % 2 ? E3_W2 : E3_W3);
            if (x >= wx1 - 6) c = ((int)(y - wTop) % 3 == 2) ? E3_W0 : E3_W1;   // shaded side
            if (((x * 7 + row * 13) % 17) == 0 && ((int)(y - wTop) % 3) != 2) c = E3_W1;   // plank joints
            sc.put(x, y, hex(c));
        }
    }
    // red roof, stepped overhang, dark underside
    const float rTop = wTop - 16, rcx = (wx0 + wx1) * 0.5f;
    for (int y = (int)rTop; y < (int)wTop + 2; ++y) {
        float t = (y - rTop) / (wTop + 2 - rTop);
        float half = 4 + t * ((wx1 - wx0) * 0.5f + 6);
        for (int x = (int)(rcx - half); x <= (int)(rcx + half); ++x) {
            uint32_t c = x < rcx - 2 ? E3_R1 : E3_R0;
            if (x < rcx - half + 3 || y == (int)rTop) c = E3_R2;            // lit eave edge
            if (y >= (int)wTop) c = E3_R0;                                   // underside
            if (((y - (int)rTop) % 4) == 3 && c == E3_R1) c = E3_R0;          // shingle rows
            sc.put(x, y, hex(c));
        }
    }
    // lit window with a cross frame
    const float wnx = tx - 12, wny = wTop + 7;
    sc.rect(wnx - 1, wny - 1, 14, 12, hex(E3_W0));
    sc.rect(wnx, wny, 12, 10, hex(E3_LIGHT));
    sc.rect(wnx, wny, 12, 2, hex(E3_W4));
    sc.rect(wnx + 5, wny, 2, 10, hex(E3_W1));
    sc.rect(wnx, wny + 5, 12, 1, hex(E3_W1));
    // a small door on the shaded side
    sc.rect(wx1 - 5, wTop + 9, 4, 17, hex(E3_W0));
    // floor beam
    sc.rect(hx0 - 2, py, hx1 - hx0 + 4, 3, hex(E3_W1));
    sc.rect(hx0 - 2, py, hx1 - hx0 + 4, 1, hex(E3_W3));
    sc.rect(hx0 - 2, py + 3, hx1 - hx0 + 4, 1, hex(E3_W0));
    // balcony railing on the right
    const float bx0 = wx1, bx1 = hx1 + 2;
    sc.rect(bx0, py - 10, bx1 - bx0, 2, hex(E3_W3));
    sc.rect(bx0, py - 9, bx1 - bx0, 1, hex(E3_W1));
    for (float x = bx0 + 1; x < bx1; x += 3) sc.rect(x, py - 8, 1, 8, hex(E3_W2));
    // railing on the left too, in front of the wall
    sc.rect(hx0 - 2, py - 8, 8, 2, hex(E3_W3));
    for (float x = hx0 - 2; x < hx0 + 6; x += 3) sc.rect(x, py - 6, 1, 6, hex(E3_W2));

    // ladder from the balcony to the ground, right of the trunk
    const float lx = tx + 14;
    for (int y = (int)py + 4; y < (int)g; ++y) {
        sc.put((int)lx, y, hex(E3_W3));
        sc.put((int)lx + 1, y, hex(E3_W2));
        sc.put((int)lx + 8, y, hex(E3_W2));
        sc.put((int)lx + 9, y, hex(E3_W1));
    }
    for (float y = py + 8; y < g - 1; y += 6) {
        sc.rect(lx + 2, y, 6, 1, hex(E3_W3));
        sc.rect(lx + 2, y + 1, 6, 1, hex(E3_W1));
    }

    // rope swing from the left branch
    const float sx0 = tx - 30, sy0 = H * 0.40f, sy1 = H * 0.70f;
    for (int y = (int)sy0; y < (int)sy1; ++y) {
        sc.put((int)sx0, y, hex(E3_W4));
        sc.put((int)sx0 + 9, y, hex(E3_W3));
    }
    sc.rect(sx0 - 2, sy1, 14, 2, hex(E3_W2));
    sc.rect(sx0 - 2, sy1 + 2, 14, 1, hex(E3_W0));

    // hanging lanterns: one under each platform corner, one from the branch
    auto lantern = [&](float x, float y, float chain) {
        sc.rect(x, y, 1, chain, hex(E3_W0));
        float ly = y + chain;
        sc.rect(x - 2, ly, 5, 1, hex(E3_W0));
        sc.rect(x - 2, ly + 1, 5, 5, hex(E3_W1));
        sc.rect(x - 1, ly + 1, 3, 4, hex(E3_LIGHT));
        sc.put((int)x, ly + 2, hex(E3_WHITE));
        sc.rect(x - 2, ly + 6, 5, 1, hex(E3_W0));
    };
    lantern(hx0 + 2, py + 4, 5);
    lantern(hx1 - 1, py + 4, 4);
    lantern(tx - 40, H * 0.41f, 6);
}

// A fence of rough posts and two rails (posts lit on the left).
void e3_fence(Scene& sc, float x0, float x1, float g)
{
    const float h = 15;
    for (int r = 0; r < 2; ++r) {
        float ry = g - h + 3 + r * 6;
        sc.rect(x0, ry, x1 - x0, 2, hex(E3_W2));
        sc.rect(x0, ry + 2, x1 - x0, 1, hex(E3_W0));
    }
    for (float x = x0; x <= x1 - 3; x += 11) {
        float ph = h + (rnd((int)x, 1, 401) < 0.5f ? 1 : 0);
        sc.rect(x, g - ph, 3, ph, hex(E3_W1));
        sc.rect(x, g - ph, 1, ph, hex(E3_W3));
        sc.rect(x + 2, g - ph, 1, ph, hex(E3_W0));
        sc.rect(x, g - ph, 3, 1, hex(E3_W3));
    }
}

void e3_rock(Scene& sc, float x, float g, float w, float h)
{
    for (int y = 0; y < (int)h; ++y) {
        float t = (float)y / h;
        float half = w * 0.5f * std::sqrt(std::max(0.0f, 1 - (1 - t) * (1 - t))) ;
        half = std::floor(half / 2) * 2 + 1;
        for (int dx = -(int)half; dx <= (int)half; ++dx) {
            float nx = dx / half, ny = (t - 0.6f) * 2;
            float v = -(0.6f * nx + 0.8f * ny);
            uint32_t c = v > 0.35f ? E3_LAV : v > -0.4f ? E3_SLATE : E3_SKYD;
            if (y == 0 || (std::abs(dx) == (int)half)) c = c == E3_LAV ? E3_SLATE : E3_SKYD;
            if (v > 0.9f && rnd(dx, y, 501) < 0.4f) c = E3_WHITE;
            sc.put((int)(x + dx), (int)(g - h + y), hex(c));
        }
    }
}

// The camping tent: an orange A-frame with a dark open door, guy ropes and pegs.
void e3_tent(Scene& sc, float cx, float g)
{
    const float h = 24, half = 22;
    for (int y = 0; y < (int)h; ++y) {
        float t = (float)y / h;
        float hw = 2 + t * half;
        for (int dx = (int)-hw; dx <= (int)hw; ++dx) {
            uint32_t c = dx < -1 ? E3_R3 : E3_R2;
            if (dx < (int)-hw + 2) c = E3_LIGHT;                    // sunlit left edge
            if (dx > hw * 0.55f) c = E3_R1;                       // shaded right flank
            if (y >= h - 2) c = dx < 0 ? E3_R2 : E3_R0;
            // the door: a dark triangle, flap tied back
            float door = (t - 0.35f) * 9;
            if (t > 0.35f && dx > -door * 0.6f && dx < door * 0.9f) c = t > 0.92f ? E3_NIGHT : E3_W0;
            if (t > 0.35f && dx >= (int)(-door * 0.6f) - 2 && dx <= (int)(-door * 0.6f) && c != E3_LIGHT) c = E3_W4;   // tied flap
            sc.put((int)(cx + dx), (int)(g - h + y), hex(c));
        }
    }
    // ridge pole tips and guy ropes
    sc.rect(cx - 1, g - h - 3, 2, 3, hex(E3_W1));
    for (int i = 0; i < 18; ++i) {
        sc.put((int)(cx - 4 - i * 1.6f), (int)(g - h + 6 + i * 1.0f), hex(E3_LAV));
        sc.put((int)(cx + 4 + i * 1.6f), (int)(g - h + 6 + i * 1.0f), hex(E3_LAVD));
    }
    sc.rect(cx - 34, g - 2, 2, 3, hex(E3_W1));
    sc.rect(cx + 33, g - 2, 2, 3, hex(E3_W1));
}

void e3_bushRow(Scene& sc, float x0, float x1, float g, int seed, float hgt)
{
    std::vector<E3Blob> bl;
    for (float x = x0; x < x1; x += 7 + 6 * rnd((int)x, 0, seed)) {
        float r = hgt * (0.5f + 0.5f * rnd((int)x, 1, seed));
        bl.push_back({x, g - r * 0.55f, r});
    }
    e3_canopy(sc, bl, {E3_G3, E3_G2, E3_G1, E3_G0}, 2, seed, g - hgt * 1.3f, g + 2);
}

} // namespace

bool bgEra3(Scene& sc, int layer)
{
    const int W = sc.W, H = sc.H;   // 782 x 220
    sc.fade = 0;                    // daytime: every layer keeps its own palette colours

    if (layer == 0) {
        // sky: flat bands with clean edges, no dither. The darker top band ends under the HUD,
        // the pale horizon band starts low enough that the mountains hide its edge.
        const int b1 = (int)(H * 0.09f), b2 = (int)(H * 0.50f);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) sc.put(x, y, hex(y < b1 ? E3_SKY2 : y < b2 ? E3_SKY : E3_SKYL));
        // pale sun with a soft stepped rim
        const float sx = W * 0.79f, sy = H * 0.24f;
        sc.disc(sx, sy, 15, 15, hex(E3_SKYL), hex(E3_SKYL), false);
        sc.disc(sx, sy, 12, 12, hex(E3_LIGHT), hex(E3_LIGHT), false);
        sc.disc(sx - 1, sy - 1, 10, 10, hex(E3_WHITE), hex(E3_WHITE), false);
        // stepped clouds
        const float cl[][3] = {{0.07f, 0.21f, 42}, {0.19f, 0.17f, 30}, {0.36f, 0.25f, 26}, {0.52f, 0.19f, 46},
                               {0.66f, 0.29f, 30}, {0.94f, 0.20f, 38}, {0.86f, 0.36f, 22}, {0.30f, 0.37f, 18}};
        for (int i = 0; i < 8; ++i) e3_cloud(sc, W * cl[i][0], H * cl[i][1], cl[i][2], 10 + i);
        // a few lone wisps
        for (int i = 0; i < 6; ++i) {
            float x = W * rnd(i, 0, 90), y = H * (0.16f + 0.24f * rnd(i, 1, 90));
            sc.rect(std::floor(x / 2) * 2, std::floor(y / 2) * 2, 10, 2, hex(E3_SKYL));
        }
        return true;
    }
    if (layer == 1) {
        // far range: pale and hazy; nearer range: slate blue with lit faces
        e3_range(sc, H * 0.58f, H * 0.32f, 7, 5, E3_SKYL, E3_SLATE, E3_SKY2);
        e3_range(sc, H * 0.68f, H * 0.27f, 11, 7, E3_SLATE, E3_SKY2, E3_SKYD);
        return true;
    }
    if (layer == 2) {
        // a far row of blue-green pines, then a dense row of dark pines and clustered broadleaves
        const float base = H * 0.745f;
        sc.rect(0, base - 22, (float)W, H - (base - 22), hex(E3_G0));
        // far blue pines: irregular gaps (clumps and clearings), varied height and two tints
        for (float x = 0; x < W;) {
            int i = (int)x;
            bool teal = rnd(i, 2, 201) < 0.35f;
            float bOff = -14 + 5 * rnd(i, 3, 201);
            e3_pine(sc, x, base + bOff, 24 + 22 * rnd(i, 1, 201), 210 + i, teal ? E3_G1 : E3_SLATE, teal ? E3_SLATE : E3_SKY2, E3_SKYD, E3_SKYD);
            x += 5 + 12 * rnd(i, 0, 201) * rnd(i, 4, 201) + (rnd(i, 5, 201) < 0.08f ? 14 : 0);
        }
        // near forest: pines of three hues and broadleaves, uneven spacing, varied foot height
        for (float x = 0; x < W;) {
            int i = (int)x;
            float foot = base + 1 + 4 * rnd(i, 6, 202);
            float r = rnd(i, 3, 202);
            if (r < 0.2f && x > 24 && x < W - 24) {
                E3Leaf L{E3_G2, E3_G1, E3_G0, E3_SKYD};
                if (rnd(i, 7, 202) < 0.4f) L = {E3_G3, E3_G2, E3_G1, E3_G0};
                e3_broadleaf(sc, x, foot, 0.8f + 0.5f * rnd(i, 4, 202), (int)(rnd(i, 5, 202) * 3), 220 + i, L, 2, 0.45f);
                x += 14 + 10 * rnd(i, 0, 202);
            } else {
                float hu = rnd(i, 8, 202);
                uint32_t a = E3_G2, m = E3_G1, d = E3_G0;          // the usual green
                if (hu < 0.25f) { a = E3_G1; m = E3_G0; d = E3_SKYD; }          // dark spruce
                else if (hu > 0.85f) { a = E3_G3; m = E3_G2; d = E3_G1; }      // young, yellow-green
                float hgt = 30 + 30 * rnd(i, 1, 202) * rnd(i, 9, 202) + 8 * rnd(i, 10, 202);
                e3_pine(sc, x, foot, hgt, 260 + i, a, m, d, E3_W0);
                x += 6 + 10 * rnd(i, 0, 202) + (rnd(i, 11, 202) < 0.1f ? 10 : 0);
            }
        }
        // the lake: a strip of water below the forest with stepped reflections and glints
        const float ly = base + 2;
        for (int y = (int)ly; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                uint32_t c = y < ly + 2 ? E3_G1 : E3_SKY2;
                if (y >= ly + 2 && y < ly + 7 && rnd(x / 3, 0, 230) < 0.55f) c = E3_SKYD;   // forest reflection
                if (y >= ly + 7 && ((y - (int)ly) % 5) == 0 && rnd(x / 6, y, 231) < 0.35f) c = E3_SKY;
                if (rnd(x / 4, y, 232) < 0.012f) c = E3_SKYL;
                sc.put(x, y, hex(c));
            }
        return true;
    }

    // ---- near layer
    const float g = H * 0.82f;    // where the game's ground strip begins (checked in game)
    // grass base so nothing gapes above the ground strip
    for (int x = 0; x < W; ++x) {
        int tuft = (rnd(x / 2, 0, 601) < 0.5f ? 1 : 0) + (rnd(x / 3, 1, 601) < 0.2f ? 2 : 0);
        for (int y = (int)g - tuft; y < H; ++y) sc.put(x, y, hex(y < g + 1 ? E3_G3 : E3_G2));
    }
    // low bushes in the back of the near layer, broken up so the lake shows through
    e3_bushRow(sc, W * 0.02f, W * 0.13f, g, 610, 10);
    e3_bushRow(sc, W * 0.38f, W * 0.47f, g, 611, 9);
    e3_bushRow(sc, W * 0.66f, W * 0.73f, g, 612, 8);
    e3_bushRow(sc, W * 0.90f, W * 1.06f, g, 613, 11);
    // fences
    e3_fence(sc, W * 0.03f, W * 0.13f, g);
    e3_fence(sc, W * 0.88f, W * 0.99f, g);
    // a second big clustered tree on the far side of the clearing: forked trunk, wide crown
    {
        float x = W * 0.86f, ct = H * 0.40f;
        for (int y = (int)(ct + 10); y < (int)g; ++y) {
            float half = 6 + (y > g - 8 ? (y - (g - 8)) * 0.9f : 0);
            for (int dx = (int)-half; dx <= (int)half; ++dx) {
                uint32_t c = dx < -half * 0.4f ? E3_W2 : dx < half * 0.4f ? E3_W1 : E3_W0;
                if (c == E3_W1 && rnd(x + dx, y / 4, 621) < 0.12f) c = E3_W0;
                sc.put((int)x + dx, y, hex(c));
            }
        }
        for (int i = 0; i < 26; ++i) {   // two limbs forking into the crown
            sc.rect(x - 2 - i * 0.9f, ct + 14 - i, 4, 2, hex(E3_W1));
            sc.rect(x - 2 - i * 0.9f, ct + 14 - i, 1, 2, hex(E3_W2));
            sc.rect(x - 1 + i * 0.8f, ct + 16 - i, 4, 2, hex(E3_W0));
        }
        std::vector<E3Blob> bl;
        const float pts[][3] = {{-30, 4, 14}, {28, 6, 14}, {-14, -12, 16}, {14, -14, 15}, {0, -26, 14},
                                {-36, 18, 11}, {36, 20, 11}, {-16, 14, 14}, {16, 16, 14}, {0, 0, 16}};
        for (auto& q : pts) bl.push_back({x + q[0], ct + q[1], q[2]});
        e3_canopy(sc, bl, {E3_G3, E3_G2, E3_G1, E3_G0}, 3, 620, ct - 40, ct + 34);
    }
    // a smaller tall tree beside the tent
    e3_broadleaf(sc, W * 0.70f, g, 1.5f, 1, 630, {E3_G3, E3_G2, E3_G1, E3_G0}, 2);
    // the camp: tent, rock
    e3_tent(sc, W * 0.58f, g + 1);
    e3_rock(sc, W * 0.48f, g + 1, 18, 10);
    e3_rock(sc, W * 0.495f, g + 1, 8, 5);
    // the treehouse
    e3_treehouse(sc, W * 0.25f, g + 1);
    // little flowers in the grass edge
    for (int i = 0; i < 30; ++i) {
        int x = (int)(rnd(i, 0, 640) * W), y = (int)g + 1 + (int)(rnd(i, 1, 640) * 3);
        sc.put(x, y, hex(i % 3 == 0 ? E3_WHITE : i % 3 == 1 ? E3_LIGHT : E3_G4));
    }
    return true;
}
