// Background scene for stage 5 (2.5D, 1958, "The hay meadow"), included into SpriteGen.cpp just
// before bgLayer(). A golden late afternoon: warm sky with stepped clouds and a low sun, layered
// hills, a farmhouse and barn, hedgerows, haystacks and bale rows, and cluster-built trees in
// three flat, hue-shifted tones (lit side yellow-green, shadow side blue-teal).
//
// Everything is painted on a coarse "art pixel" grid (E5Grid, one cell = b x b image pixels)
// and then blown up with hard edges, so the scene reads as crisp chunky pixel art even though
// this is a smooth stage. The grid wraps on x so every parallax layer tiles.
//
// In game the layers 1..3 stand as cut-outs at real depths and the haze shader mixes them
// towards the dusk horizon (Renderer::drawBackdrop3D); the bottom 14-16% of layers 2 and 3 sits
// below the lawn. The e5_ helpers are also used by era6.inc (included right after this file).

struct E5Grid {
    int w, h;
    std::vector<Col> c;
    E5Grid(int W, int H) : w(W), h(H), c((size_t)W * H, Col{0, 0, 0, 0}) {}
    void set(int x, int y, Col col)
    {
        if (y < 0 || y >= h) return;
        x = ((x % w) + w) % w;
        c[(size_t)y * w + x] = col;
    }
    Col get(int x, int y) const
    {
        if (y < 0 || y >= h) return Col{0, 0, 0, 0};
        x = ((x % w) + w) % w;
        return c[(size_t)y * w + x];
    }
    void rect(int x0, int y0, int ww, int hh, Col col)
    {
        for (int y = y0; y < y0 + hh; ++y)
            for (int x = x0; x < x0 + ww; ++x) set(x, y, col);
    }
    // blow the grid up into the layer, b image pixels per cell
    void blit(Scene& sc, int b) const
    {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const Col& k = c[(size_t)y * w + x];
                if (k.a <= 0) continue;
                for (int yy = 0; yy < b; ++yy)
                    for (int xx = 0; xx < b; ++xx) sc.put(x * b + xx, y * b + yy, k);
            }
    }
};

struct E5Pal { Col lit, mid, dark; };

static E5Pal e5_pal(uint32_t a, uint32_t b, uint32_t c) { return {hex(a), hex(b), hex(c)}; }

// A canopy built from clusters: a few overlapping blobs, each lit on its top-left and shaded on
// its bottom-right, plus an overall shade so the underside of the crown is the dark teal.
static void e5_canopy(E5Grid& g, float cx, float cy, float rx, float ry, const E5Pal& p, int seed, int nBlobs = 6)
{
    struct Blob { float x, y, r; };
    std::vector<Blob> bl;
    bl.push_back({cx, cy + ry * 0.12f, std::min(rx, ry) * 0.72f});
    for (int i = 0; i < nBlobs; ++i) {
        float a = 3.14159f + (i + 0.5f) / nBlobs * 3.14159f + (rnd(i, seed, 501) - 0.5f) * 0.5f;   // around the top half
        float d = 0.42f + 0.12f * rnd(i, seed, 502);
        float r = std::min(rx, ry) * (0.42f + 0.16f * rnd(i, seed, 503));
        bl.push_back({cx + std::cos(a) * rx * d * 1.05f, cy + std::sin(a) * ry * d, r});
    }
    // lower side clusters so the crown is round, not a dome
    bl.push_back({cx - rx * 0.5f, cy + ry * 0.3f, std::min(rx, ry) * 0.48f});
    bl.push_back({cx + rx * 0.5f, cy + ry * 0.32f, std::min(rx, ry) * 0.48f});
    int x0 = (int)std::floor(cx - rx * 1.4f), x1 = (int)std::ceil(cx + rx * 1.4f);
    int y0 = (int)std::floor(cy - ry * 1.4f), y1 = (int)std::ceil(cy + ry * 1.4f);
    float sx = rx / std::min(rx, ry), sy = ry / std::min(rx, ry);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            int best = -1;
            float bestD = 1e9f;
            for (size_t i = 0; i < bl.size(); ++i) {
                float dx = (px - bl[i].x) / sx, dy = (py - bl[i].y) / sy;
                float d = std::sqrt(dx * dx + dy * dy) / bl[i].r;
                float edge = 1.0f + (rnd(x, y, seed * 7 + 11) - 0.5f) * 0.22f;   // stepped, jagged rim
                if (d < edge && d < bestD) { bestD = d; best = (int)i; }
            }
            if (best < 0) continue;
            const Blob& b = bl[best];
            float nx = (px - b.x) / (b.r * sx), ny = (py - b.y) / (b.r * sy);
            float local = -(nx * 0.6f + ny * 0.8f);
            float glob = -((px - cx) / rx * 0.35f + (py - cy) / ry * 0.95f);
            float v = 0.5f * local + 0.5f * glob + (rnd(x, y, seed + 77) - 0.5f) * 0.12f;
            Col col = v > 0.22f ? p.lit : (v > -0.32f ? p.mid : p.dark);
            g.set(x, y, col);
        }
}

// A straight trunk, lighter on the left, darker on the right; `fork` adds two angled limbs.
static void e5_trunk(E5Grid& g, int x, int baseY, int h, int w, Col lit, Col dark, bool fork = false)
{
    for (int y = baseY - h; y < baseY; ++y)
        for (int i = 0; i < w; ++i) g.set(x - w / 2 + i, y, i < (w + 1) / 2 ? lit : dark);
    if (fork) {
        for (int k = 0; k < h / 2 + 2; ++k) {
            g.set(x - 1 - k / 2, baseY - h - k, lit);
            g.set(x + 1 + k / 2, baseY - h - k, dark);
            g.set(x + 2 + k / 2, baseY - h - k, dark);
        }
    }
}

static void e5_tuft(E5Grid& g, int x, int y, const E5Pal& p, int seed)
{
    for (int i = -3; i <= 3; ++i) {
        int hgt = 1 + (rnd(i, seed, 61) < 0.5f ? 1 : 0);
        for (int k = 0; k < hgt; ++k) g.set(x + i, y - k, rnd(i, k + seed, 62) < 0.5f ? p.lit : p.mid);
    }
}

// A broadleaf tree: kind 0 round, 1 tall oval, 2 forked with two crowns, 3 wide round, 4 tall narrow.
static void e5_tree(E5Grid& g, int x, int baseY, float size, int kind, const E5Pal& p, Col tl, Col td, int seed, bool tuft = true)
{
    float s = size;
    int tw = std::max(1, (int)std::lround(s * 0.35f));
    if (kind == 2) {
        e5_trunk(g, x, baseY, (int)(s * 1.0f), tw, tl, td, true);
        e5_canopy(g, x - s * 0.75f, baseY - s * 2.3f, s * 0.95f, s * 0.85f, p, seed, 5);
        e5_canopy(g, x + s * 0.9f, baseY - s * 1.8f, s * 0.75f, s * 0.7f, p, seed + 3, 5);
    } else {
        float th = kind == 3 ? 0.7f : 0.85f;
        e5_trunk(g, x, baseY, (int)(s * th) + 2, tw, tl, td);
        float rx = s, ry = s;
        if (kind == 1) { rx = s * 0.85f; ry = s * 1.4f; }
        if (kind == 3) { rx = s * 1.35f; ry = s * 0.95f; }
        if (kind == 4) { rx = s * 0.6f; ry = s * 1.6f; }
        e5_canopy(g, (float)x, baseY - s * th - ry * 0.95f, rx, ry, p, seed, kind == 3 ? 7 : 6);
    }
    if (tuft) e5_tuft(g, x, baseY - 1, p, seed);
}

// A pine in stepped tiers: lit on the left, the dark teal on the right.
static void e5_pine(E5Grid& g, int x, int baseY, int h, const E5Pal& p, Col trunk, int seed)
{
    int trunkH = std::max(1, h / 7);
    for (int y = baseY - trunkH; y < baseY; ++y) g.set(x, y, trunk);
    int tiers = std::max(2, h / 6), top = baseY - h, bodyH = h - trunkH;
    for (int t = 0; t < tiers; ++t) {
        int ty0 = top + t * bodyH / (tiers + 1), ty1 = top + (t + 2) * bodyH / (tiers + 1);
        if (t == tiers - 1) ty1 = baseY - trunkH;
        float maxHalf = 1.0f + (h * 0.32f) * (t + 1) / tiers;
        for (int y = ty0; y < ty1; ++y) {
            float f = (float)(y - ty0 + 1) / (ty1 - ty0);
            int half = (int)std::lround(maxHalf * f);
            if ((y - ty0) % 2 == 1 && half > 1 && rnd(y, t, seed) < 0.5f) half -= 1;   // ragged needle tips
            for (int i = -half; i <= half; ++i) {
                Col c = i > 0 ? p.dark : (i > -half / 2 - 1 || y > ty1 - 2 ? p.mid : p.lit);
                if (i < 0 && y < ty0 + 2) c = p.lit;
                g.set(x + i, y, c);
            }
        }
    }
}

// A layered ridge with a lit face where the ground rises to the right (light from the left),
// a lighter rim along the top, and fill down to the bottom.
// whole periods only, so it tiles
static float e5_ridgeTop(const E5Grid& g, int x, float base, int seed, float amp, int period, float a2 = 0.5f, float a3 = 0.25f)
{
    float t = (float)x / g.w * 6.2831853f;
    return base + amp * std::sin(t * period + seed) + amp * a2 * std::sin(t * (period * 2 + 1) + seed * 1.7f) +
           amp * a3 * std::sin(t * period * 6 + seed * 2.3f);
}

static void e5_ridge(E5Grid& g, float base, int seed, float amp, int period, Col fill, Col lit, Col rim, float a2 = 0.5f, float a3 = 0.25f)
{
    auto top = [&](int x) { return e5_ridgeTop(g, x, base, seed, amp, period, a2, a3); };
    for (int x = 0; x < g.w; ++x) {
        int y0 = (int)std::lround(top(x));
        float slope = top(x + 2) - top(x - 2);
        for (int y = y0; y < g.h; ++y) {
            Col c = fill;
            if (y < y0 + 1) c = rim;
            else if (slope < -0.25f && y < y0 + 3 + (int)(-slope * 3)) c = lit;
            g.set(x, y, c);
        }
    }
}

// A stepped cloud: stacked blobs with a flat bottom, three tones from the top down.
static void e5_cloud(E5Grid& g, float cx, float cy, float size, Col top, Col mid, Col under, int seed)
{
    struct B { float x, y, r; };
    B bl[5] = {{cx, cy - size * 0.35f, size}, {cx - size * 1.2f, cy, size * 0.7f}, {cx + size * 1.3f, cy + size * 0.05f, size * 0.62f},
               {cx - size * 2.1f, cy + size * 0.3f, size * 0.4f}, {cx + size * 2.2f, cy + size * 0.3f, size * 0.38f}};
    float flat = cy + size * 0.5f;
    for (int y = (int)(cy - size * 1.5f); y <= (int)flat; ++y)
        for (int x = (int)(cx - size * 3); x <= (int)(cx + size * 3); ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            bool in = false;
            for (auto& b : bl) {
                float dx = (px - b.x), dy = (py - b.y) * 1.6f;
                if (dx * dx + dy * dy < b.r * b.r) in = true;
            }
            if (!in || py > flat) continue;
            float rel = (py - (cy - size * 1.3f)) / (flat - (cy - size * 1.3f));
            Col c = rel < 0.55f ? top : (rel < 0.82f ? mid : under);
            if (rel >= 0.55f && rel < 0.82f && rnd(x, y, seed) < 0.15f) c = top;
            g.set(x, y, c);
        }
}

// Flat sky bands with a stepped (staircase) edge between each pair.
static void e5_sky(E5Grid& g, const uint32_t* bands, int n, float endFrac, int seed)
{
    for (int y = 0; y < g.h; ++y)
        for (int x = 0; x < g.w; ++x) {
            float f = (float)y / (g.h * endFrac) * n;
            // a small stair offset so the band edges step
            f += ((x / 6 + seed) % 3 == 0 ? 0.08f : 0.0f);
            int b = std::min(n - 1, std::max(0, (int)f));
            g.set(x, y, hex(bands[b]));
        }
}

// A flat sun with two halo rings of slightly lighter sky.
static void e5_sun(E5Grid& g, float cx, float cy, float r, Col core, Col ring1, Col ring2)
{
    for (int y = (int)(cy - r * 2.2f); y <= (int)(cy + r * 2.2f); ++y)
        for (int x = (int)(cx - r * 2.2f); x <= (int)(cx + r * 2.2f); ++x) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = std::sqrt(dx * dx + dy * dy);
            if (d < r) g.set(x, y, core);
            else if (d < r * 1.35f) g.set(x, y, ring1);
            else if (d < r * 1.75f) g.set(x, y, mixc(g.get(x, y), ring2, 0.5f));
        }
}

// A traditional domed haystack, lit on the left.
static void e5_haystack(E5Grid& g, int x, int baseY, int w, int h, const E5Pal& p, int seed)
{
    float rx = w * 0.5f;
    for (int y = baseY - h; y < baseY; ++y) {
        float t = (float)(y - (baseY - h)) / h;   // 0 top .. 1 base
        float half = t < 0.55f ? rx * std::sqrt(std::max(0.0f, 1 - (0.55f - t) * (0.55f - t) / (0.55f * 0.55f))) : rx * (1.0f + (t - 0.55f) * 0.15f);
        int hw = (int)std::lround(half);
        for (int i = -hw; i <= hw; ++i) {
            float u = (i + 0.5f) / (hw + 0.5f);
            Col c = u < -0.25f ? p.lit : (u < 0.45f ? p.mid : p.dark);
            if (t > 0.86f) c = u < 0.2f ? p.mid : p.dark;                       // shaded skirt
            if ((y + i / 3 + seed) % 4 == 0 && t > 0.2f && rnd(i, y, seed) < 0.5f) c = c.r == p.lit.r ? p.mid : p.dark;   // straw lines
            g.set(x + i, y, c);
        }
    }
    g.set(x, baseY - h - 1, p.dark);   // the little top knot
}

// A small rectangular bale seen slightly from the side: lit top, front face, dark end.
static void e5_bale(E5Grid& g, int x, int baseY, int w, int h, const E5Pal& p)
{
    g.rect(x, baseY - h, w, 1, p.lit);
    g.rect(x, baseY - h + 1, w - 1, h - 1, p.mid);
    g.rect(x + w - 1, baseY - h + 1, 1, h - 1, p.dark);
    if (w > 3) g.rect(x + w / 3, baseY - h + 1, 1, h - 1, p.dark);   // the twine
}

// A stook: sheaves leaned together, a little triangle.
static void e5_stook(E5Grid& g, int x, int baseY, int h, const E5Pal& p)
{
    for (int y = 0; y < h; ++y) {
        int half = y * 2 / 3 + 1;
        for (int i = -half; i <= half; ++i) g.set(x + i, baseY - h + y, i < 0 ? p.lit : (i == 0 ? p.mid : p.dark));
    }
}

// A hedgerow: a lumpy band of small clusters along y.
static void e5_hedge(E5Grid& g, int x0, int x1, int y, int hgt, const E5Pal& p, int seed)
{
    for (int x = x0; x < x1; ++x) {
        float lump = 0.5f + 0.5f * std::sin(x * 0.9f + seed) * std::sin(x * 0.37f + seed * 2.0f);
        int top = y - hgt + (int)std::lround(lump * 2 + rnd(x, seed, 71) * 1.2f);
        for (int yy = top; yy <= y; ++yy) {
            float rel = (float)(yy - top) / std::max(1, y - top);
            int phase = (x + seed) % 5;
            Col c = rel < 0.34f ? (phase < 3 ? p.lit : p.mid) : (rel < 0.7f ? p.mid : p.dark);
            g.set(x, yy, c);
        }
    }
}

static void e5_fence(E5Grid& g, int x0, int x1, int y, Col post, Col postD, Col rail)
{
    for (int x = x0; x < x1; ++x) {
        g.set(x, y - 3, rail);
        g.set(x, y - 1, rail);
    }
    for (int x = x0; x < x1; x += 5) {
        g.rect(x, y - 4, 1, 5, post);
        g.set(x + 1, y - 4, postD);
        g.set(x + 1, y - 2, postD);
        g.set(x + 1, y, postD);
    }
}

// The farmhouse: whitewashed walls, a red-brown tiled roof, chimneys, warm lit windows.
static void e5_farmhouse(E5Grid& g, int x, int baseY, int w, int h)
{
    Col wall = hex(0xf2e2c4), wallD = hex(0xc8b0a0), roof = hex(0xb84a32), roofL = hex(0xd8683e), roofD = hex(0x7a2c30);
    Col win = hex(0xffd878), frame = hex(0x5a3a3a), door = hex(0x3a5a6a), stone = hex(0x9a8a90);
    int roofH = w / 3;
    g.rect(x, baseY - h, w, h, wall);
    g.rect(x + w - 3, baseY - h, 3, h, wallD);                 // shaded gable end
    g.rect(x, baseY - 1, w, 1, stone);                         // plinth
    for (int r = 0; r < roofH; ++r) {                          // a long hipped roof, eaves at r = 0
        int l = x - 2 + r, rr = x + w + 1 - r, y = baseY - h - 1 - r;
        for (int i = l; i <= rr; ++i) {
            Col c = (r % 3 == 0) ? roofD : roof;               // courses of tiles
            if (r >= roofH - 2) c = roofL;                     // the ridge catches the sun
            if (i > rr - (r + 3)) c = roofD;                   // the hip on the shaded end
            else if (i < l + 2) c = roofL;                     // the lit hip
            g.set(i, y, c);
        }
    }
    g.rect(x + w / 5, baseY - h - roofH - 3, 2, 4, stone);    // chimneys
    g.rect(x + w * 3 / 4 - 2, baseY - h - roofH - 2, 2, 3, stone);
    g.set(x + w / 5, baseY - h - roofH - 4, hex(0x9aa8c8));    // a curl of smoke
    g.set(x + w / 5 + 1, baseY - h - roofH - 6, hex(0xb0bcd8));
    g.set(x + w / 5, baseY - h - roofH - 8, hex(0xc8d0e4));
    int nWin = std::max(2, w / 7);
    for (int i = 0; i < nWin; ++i) {
        int wx = x + 2 + i * (w - 6) / nWin;
        g.rect(wx, baseY - h + 2, 3, 3, win);
        g.set(wx + 2, baseY - h + 2, frame);
        if (i != nWin / 2) { g.rect(wx, baseY - h + 7, 3, 3, win); g.set(wx + 2, baseY - h + 7, frame); }
    }
    int dx = x + 2 + (nWin / 2) * (w - 6) / nWin;
    g.rect(dx, baseY - 6, 3, 5, door);
}

// The barn: dark weatherboard, a big open door with hay inside, a corrugated roof.
static void e5_barn(E5Grid& g, int x, int baseY, int w, int h)
{
    Col wall = hex(0x9a4a3a), wallL = hex(0xb85e44), wallD = hex(0x6a2e34), roof = hex(0x7a7890), roofL = hex(0xa8a4b4), roofD = hex(0x4a4a64);
    Col hay = hex(0xf0c860), hayD = hex(0xb88a3a), dark = hex(0x3a2230);
    g.rect(x, baseY - h, w, h, wall);
    for (int i = x; i < x + w; i += 2) g.rect(i, baseY - h, 1, h, (i - x) % 4 == 0 ? wallL : wall);   // boards
    g.rect(x + w - 3, baseY - h, 3, h, wallD);
    int roofH = w / 3 + 1;
    for (int r = 0; r < roofH; ++r) {
        int half = (w / 2 + 2) - r * (w / 2 + 2) / roofH;
        for (int i = -half; i <= half; ++i) {
            Col c = i < 0 ? (r < roofH - 2 && (i + r) % 3 == 0 ? roofL : roofL) : roofD;
            if (i < 0 && (i % 3 == 0)) c = roof;
            g.set(x + w / 2 + i, baseY - h - r, c);
        }
    }
    // the open door, hay piled inside
    int dw = w / 3, dx = x + w / 2 - dw / 2, dh = h * 3 / 4;
    g.rect(dx, baseY - dh, dw, dh, dark);
    for (int i = 0; i < dw; ++i) {
        int hh = dh / 2 + (int)(2 * std::sin(i * 0.9f));
        for (int k = 0; k < hh; ++k) g.set(dx + i, baseY - 1 - k, k == hh - 1 ? hay : (i < dw / 2 ? hay : hayD));
    }
    g.rect(x + w / 2 - 1, baseY - h - 3, 2, 2, hex(0x2a1a2a));   // hay loft hatch
}

bool bgEra5(Scene& sc, int layer)
{
    const int W = sc.W, H = sc.H;
    const int b = 4;                        // image pixels per art pixel
    E5Grid g(W / b, H / b);
    const int gw = g.w, gh = g.h;
    // the haze shader already fades the cut-outs towards the dusk; keep the painted fade light
    const float fades[4] = {0, 0.0f, 0.06f, 0.0f};
    sc.fade = fades[layer];

    // golden-hour foliage, hay and wood, each a lit / mid / shadow trio with a hue shift
    const E5Pal leaf = e5_pal(0xc8d850, 0x6aa040, 0x2a5a52);
    const E5Pal leafWarm = e5_pal(0xd8d058, 0x88a43c, 0x3a6050);
    const E5Pal leafFar = e5_pal(0x9cb460, 0x6a8a5a, 0x4a6062);
    const E5Pal hedge = e5_pal(0xb0c84a, 0x5a8a3e, 0x2a4e4a);
    const E5Pal hay = e5_pal(0xfce08a, 0xe2b458, 0xa8703a);
    const E5Pal hayFar = e5_pal(0xf4d494, 0xd8b070, 0xb08a60);
    const Col trunkL = hex(0xa86a3a), trunkD = hex(0x5e3a34);

    if (layer == 0) {
        const uint32_t bands[7] = {0x5e92d6, 0x76a6de, 0x92b8e0, 0xb4c8dc, 0xe6d6b4, 0xf6cc94, 0xf8b880};
        e5_sky(g, bands, 7, 0.78f, 1);
        e5_sun(g, gw * 0.8f, gh * 0.3f, 7.0f, hex(0xfff6d0), hex(0xfce6a4), hex(0xfce0a8));
        const Col ct = hex(0xfff8ec), cm = hex(0xf6dcc4), cu = hex(0xd8aaa8);
        e5_cloud(g, gw * 0.12f, gh * 0.22f, 7.0f, ct, cm, cu, 1);
        e5_cloud(g, gw * 0.38f, gh * 0.12f, 5.0f, ct, cm, cu, 2);
        e5_cloud(g, gw * 0.57f, gh * 0.3f, 4.5f, ct, cm, cu, 3);
        e5_cloud(g, gw * 0.93f, gh * 0.12f, 4.0f, ct, cm, cu, 4);
        e5_cloud(g, gw * 0.7f, gh * 0.09f, 3.0f, ct, cm, cu, 6);
        e5_cloud(g, gw * 0.27f, gh * 0.42f, 2.6f, hex(0xfce8d0), hex(0xf0c8b0), hex(0xd8a8a0), 5);
        // a few long thin strands low near the sun
        for (int i = 0; i < 4; ++i) {
            int y = (int)(gh * (0.36f + 0.06f * i)), x0 = (int)(gw * (0.6f + 0.05f * i)), len = 14 + i * 4;
            g.rect(x0, y, len, 1, hex(0xfce4c0));
        }
        g.blit(sc, b);
        return true;
    }

    if (layer == 1) {
        // far: blue-violet downs, then warmer nearer hills patched with fields and hedges
        e5_ridge(g, gh * 0.46f, 3, 5.0f, 2, hex(0x8a84b8), hex(0xa8a0d0), hex(0xc4bce0));
        e5_ridge(g, gh * 0.56f, 7, 4.0f, 3, hex(0x6a8aa0), hex(0x8aa8b0), hex(0xa8c0b8));
        const float b3 = gh * 0.66f, a3 = 3.0f;
        e5_ridge(g, b3, 11, a3, 4, hex(0x6a9a4a), hex(0x8ab450), hex(0xb8cc68));
        // patchwork on the near hill: rows that follow the hill and widen towards us, cut into
        // fields by hedges, with clumps of trees along the hedges
        const uint32_t patch[5] = {0xe8c868, 0x8ab44a, 0xd8b050, 0xa8c458, 0x74a046};
        const int rowH[8] = {3, 4, 5, 6, 7, 8, 9, 10};
        auto rowOf = [&](int d, int& start) {
            int r = 0, acc = 0;
            while (r < 7 && d >= acc + rowH[r]) { acc += rowH[r]; ++r; }
            start = acc;
            return r;
        };
        for (int x = 0; x < gw; ++x) {
            int top = (int)std::lround(e5_ridgeTop(g, x, b3, 11, a3, 4));
            for (int y = top + 2; y < gh; ++y) {
                int start, row = rowOf(y - top - 2, start);
                int span = 14 + row * 6, shift = (int)(rnd(row, 0, 310) * 40) + (y - top) / 2;
                int cut = (x + shift) / span;
                int k = (int)(rnd(cut, row, 311) * 5);
                Col c = hex(patch[k]);
                bool hedgeRow = y - top - 2 == start && rnd(cut, row, 313) < 0.8f;
                bool hedgeCut = (x + shift) % span == 0;
                if (hedgeRow || hedgeCut) c = hex(0x4a7048);
                else if (k % 2 == 0 && (x + y) % 4 == 0) c = mul(c, 0.94f);   // mown lines in the hay
                g.set(x, y, c);
            }
        }
        // tree clumps sitting on the hedge lines, bigger lower down
        for (int i = 0; i < 70; ++i) {
            int x = (int)(rnd(i, 1, 314) * gw), row = 1 + (int)(rnd(i, 2, 314) * 5);
            int top = (int)std::lround(e5_ridgeTop(g, x, b3, 11, a3, 4)), acc = 0;
            for (int r = 0; r < row; ++r) acc += rowH[r];
            float sz = 1.2f + row * 0.45f;
            e5_canopy(g, (float)x, (float)(top + 2 + acc) - sz * 0.6f, sz * 1.3f, sz, leafFar, i + 200, 4);
        }
        // a far wood: a dense band of small cluster trees and a few pines
        int base = gh - 1;
        for (int i = 0; i < 44; ++i) {
            int x = (int)((i + rnd(i, 1, 301) * 0.6f) * gw / 44);
            float sz = 3.0f + 2.5f * rnd(i, 2, 301);
            if (i % 7 == 3) e5_pine(g, x, base, (int)(sz * 3.2f), leafFar, trunkD, i);
            else e5_canopy(g, (float)x, base - sz * 0.9f, sz * 1.25f, sz, leafFar, i + 40, 4);
        }
        // the band's bottom stays solid so the cut-out meets the lawn
        for (int x = 0; x < gw; ++x) g.rect(x, gh - 3, 1, 3, leafFar.dark);
        g.blit(sc, b);
        return true;
    }

    if (layer == 2) {
        // mid: the hay meadow with the farm. The bottom 16% sits below the lawn.
        int fieldTop = (int)(gh * 0.66f);
        // gently rolling field edge
        for (int x = 0; x < gw; ++x) {
            int top = fieldTop + (int)std::lround(1.5f * std::sin(x * 6.2831853f / gw * 3 + 1.0f));
            for (int y = top; y < gh; ++y) {
                Col c = hex(0xe8c470);
                if (y < top + 1) c = hex(0xf8e098);
                else if ((y - top) % 5 == 2) c = hex(0xd2a858);   // mown rows
                else if ((y - top) % 5 == 3 && (x + y) % 3 == 0) c = hex(0xf2d488);
                g.set(x, y, c);
            }
        }
        // hedgerows along the field top, with gaps
        e5_hedge(g, 0, gw / 3, fieldTop, 4, hedge, 1);
        e5_hedge(g, gw / 3 + 10, gw * 2 / 3, fieldTop, 4, hedge, 2);
        e5_hedge(g, gw * 2 / 3 + 8, gw - 2, fieldTop, 4, hedge, 3);
        // the farm: house and barn, with trees around them
        int farmX = (int)(gw * 0.18f);
        e5_tree(g, farmX - 10, fieldTop + 1, 7.0f, 3, leaf, trunkL, trunkD, 21, false);
        e5_farmhouse(g, farmX, fieldTop + 2, 34, 13);
        e5_barn(g, farmX + 40, fieldTop + 2, 24, 15);
        e5_tree(g, farmX + 72, fieldTop + 1, 6.0f, 1, leaf, trunkL, trunkD, 22, false);
        // trees along the hedge line elsewhere
        const int tk[6] = {0, 4, 2, 0, 1, 3};
        for (int i = 0; i < 6; ++i) {
            int x = (int)(gw * (0.48f + i * 0.085f)) + (int)(rnd(i, 3, 302) * 4);
            e5_tree(g, x, fieldTop + 1, 5.0f + 1.8f * rnd(i, 4, 302), tk[i], i % 2 ? leafWarm : leaf, trunkL, trunkD, 30 + i, false);
        }
        // haystacks and stooks out in the meadow, smaller further up
        for (int i = 0; i < 7; ++i) {
            int x = (int)(gw * (0.44f + i * 0.08f)) + (int)(rnd(i, 5, 303) * 5);
            e5_haystack(g, x, fieldTop + 6 + (i % 2) * 2, 7 + (i % 3), 7 + (i % 2), hayFar, i);
        }
        for (int i = 0; i < 18; ++i) e5_stook(g, (int)(gw * 0.05f + i * 6.5f) % gw, fieldTop + 12 + (i % 3), 3, hay);
        g.blit(sc, b);
        return true;
    }

    // near: rows of bales and big haystacks in front of a hedge with cluster trees. Drawn smaller
    // and further back in game; its bottom 14% sits below the lawn.
    int ground = (int)(gh * 0.8f);
    // stubble ground strip
    for (int x = 0; x < gw; ++x)
        for (int y = ground; y < gh; ++y) {
            Col c = hex(0xe0b860);
            if (y == ground) c = hex(0xf4d68a);
            else if ((x * 3 + y * 5) % 11 == 0) c = hex(0xc8984a);
            g.set(x, y, c);
        }
    // a hedge with a few big cluster trees standing out of it
    e5_hedge(g, 0, gw, ground - 6, 7, hedge, 9);
    for (int x = 0; x < gw; ++x)
        for (int y = ground - 6; y < ground; ++y) {
            float r = rnd(x / 2, y / 2, 320);   // 2x2 leaf clusters
            g.set(x, y, r < 0.25f && y < ground - 3 ? hedge.lit : (r < 0.6f ? hedge.mid : hedge.dark));
        }
    const int kinds[5] = {0, 2, 1, 3, 4};
    for (int i = 0; i < 5; ++i) {
        int x = (int)(gw * (0.1f + i * 0.2f)) + (int)(rnd(i, 1, 304) * 6);
        e5_tree(g, x, ground - 2, 8.0f + 2.5f * rnd(i, 2, 304), kinds[i], i % 2 ? leafWarm : leaf, trunkL, trunkD, 50 + i);
    }
    // a gate and fence in one gap
    e5_fence(g, (int)(gw * 0.22f), (int)(gw * 0.36f), ground - 1, hex(0xb07a48), hex(0x6a4434), hex(0x9a6a40));
    // big haystacks and rows of bales on the stubble
    for (int i = 0; i < 4; ++i) {
        int x = (int)(gw * (0.06f + i * 0.25f)) + (int)(rnd(i, 3, 305) * 8);
        e5_haystack(g, x, ground + 3, 14 + (i % 2) * 3, 13 + (i % 2) * 2, hay, i + 10);
    }
    for (int i = 0; i < 14; ++i) {
        int x = (int)(gw * 0.16f) + i * 18 + (int)(rnd(i, 4, 306) * 4);
        if (i % 4 == 3) continue;
        e5_bale(g, x, ground + 5, 9, 5, hay);
        if (i % 2 == 0) e5_bale(g, x + 2, ground + 1, 7, 4, hay);
    }
    g.blit(sc, b);
    return true;
}
