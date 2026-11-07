// Background scene for stage 5 (2.5D, 1958, "The hay meadow"), included into SpriteGen.cpp just
// before bgLayer(). A golden late afternoon: warm sky with stepped clouds and a low sun, layered
// hills, a farmhouse and barn, hedgerows, haystacks and bale rows, and cluster-built trees in
// three flat, hue-shifted tones (lit side yellow-green, shadow side blue-teal).
//
// The scene is laid out in "art units" (E5Grid::K image pixels each) but every shape is painted
// at the layer's full resolution: curves, slopes and rims step in single image pixels, not in
// coarse 4 px blocks. The 2.5D and 3D stages draw smooth geometry in front of these layers, so
// the backdrop has to be just as fine or the two clash. Flat colour areas, hard edges, no
// gradients. The grid wraps on x so every parallax layer tiles.
//
// In game the layers 1..3 stand as cut-outs at real depths and the haze shader mixes them
// towards the dusk horizon (Renderer::drawBackdrop3D); the bottom 14-16% of layers 2 and 3 sits
// below the lawn. The e5_ helpers are also used by era6.inc (included right after this file).

struct E5Grid {
    static constexpr int K = 4;   // image pixels per art unit
    int fw, fh;                   // size in image pixels
    int w, h;                     // size in art units: the scene is laid out in these
    std::vector<Col> c;           // one entry per image pixel
    E5Grid(int W, int H) : fw(W), fh(H), w(W / K), h(H / K), c((size_t)W * H, Col{0, 0, 0, 0}) {}
    void setf(int x, int y, Col col)
    {
        if (y < 0 || y >= fh) return;
        x = ((x % fw) + fw) % fw;
        c[(size_t)y * fw + x] = col;
    }
    Col getf(int x, int y) const
    {
        if (y < 0 || y >= fh) return Col{0, 0, 0, 0};
        x = ((x % fw) + fw) % fw;
        return c[(size_t)y * fw + x];
    }
    // Calls f(fx, fy, ax, ay) for every image pixel whose centre lies in the art-unit box
    // [x0, x1) x [y0, y1); (ax, ay) is that centre in art units.
    template <class F>
    void each(float x0, float y0, float x1, float y1, F f)
    {
        int fx0 = (int)std::floor(x0 * K), fx1 = (int)std::ceil(x1 * K);
        int fy0 = std::max(0, (int)std::floor(y0 * K)), fy1 = std::min(fh, (int)std::ceil(y1 * K));
        for (int fy = fy0; fy < fy1; ++fy)
            for (int fx = fx0; fx < fx1; ++fx) {
                float ax = (fx + 0.5f) / K, ay = (fy + 0.5f) / K;
                if (ax < x0 || ax >= x1 || ay < y0 || ay >= y1) continue;
                f(fx, fy, ax, ay);
            }
    }
    void rectf(float x0, float y0, float ww, float hh, Col col)
    {
        each(x0, y0, x0 + ww, y0 + hh, [&](int fx, int fy, float, float) { setf(fx, fy, col); });
    }
    void set(int x, int y, Col col) { rectf((float)x, (float)y, 1, 1, col); }   // one whole art unit
    Col get(int x, int y) const { return getf(x * K, y * K); }
    void rect(int x0, int y0, int ww, int hh, Col col) { rectf((float)x0, (float)y0, (float)ww, (float)hh, col); }
    void blit(Scene& sc) const
    {
        for (int y = 0; y < fh; ++y)
            for (int x = 0; x < fw; ++x) {
                const Col& k = c[(size_t)y * fw + x];
                if (k.a > 0) sc.put(x, y, k);
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
    float sx = rx / std::min(rx, ry), sy = ry / std::min(rx, ry);
    g.each(cx - rx * 1.4f, cy - ry * 1.4f, cx + rx * 1.4f, cy + ry * 1.4f, [&](int fx, int fy, float px, float py) {
        int best = -1;
        float bestD = 1e9f;
        // the rim steps in little leaf clumps (three image pixels), never single stray pixels
        float edge = 1.0f + (rnd(fx / 3, fy / 3, seed * 7 + 11) - 0.5f) * 0.14f;
        for (size_t i = 0; i < bl.size(); ++i) {
            float dx = (px - bl[i].x) / sx, dy = (py - bl[i].y) / sy;
            float d = std::sqrt(dx * dx + dy * dy) / bl[i].r;
            if (d < edge && d < bestD) { bestD = d; best = (int)i; }
        }
        if (best < 0) return;
        const Blob& b = bl[best];
        float nx = (px - b.x) / (b.r * sx), ny = (py - b.y) / (b.r * sy);
        float local = -(nx * 0.6f + ny * 0.8f);
        float glob = -((px - cx) / rx * 0.35f + (py - cy) / ry * 0.95f);
        float v = 0.5f * local + 0.5f * glob + (rnd(fx / 3, fy / 3, seed + 77) - 0.5f) * 0.08f;
        g.setf(fx, fy, v > 0.22f ? p.lit : (v > -0.32f ? p.mid : p.dark));
    });
}

// A straight trunk, lighter on the left half, darker on the right; `fork` adds two angled limbs.
static void e5_trunk(E5Grid& g, int x, int baseY, int h, int w, Col lit, Col dark, bool fork = false)
{
    float l = (float)(x - w / 2);
    g.rectf(l, (float)(baseY - h), w * 0.5f, (float)h, lit);
    g.rectf(l + w * 0.5f, (float)(baseY - h), w * 0.5f, (float)h, dark);
    if (fork) {
        float top = (float)(baseY - h), len = h / 2.0f + 2.0f;
        g.each(x - len, top - len, x + len + 3, top + 0.5f, [&](int fx, int fy, float ax, float ay) {
            float k = top + 0.5f - ay;   // how far up the limbs
            if (std::fabs(ax - (x - 0.5f - k * 0.5f)) < 0.45f) g.setf(fx, fy, lit);
            else if (ax >= x + 1.0f + k * 0.5f && ax < x + 2.4f + k * 0.5f) g.setf(fx, fy, dark);
        });
    }
}

// A grass tuft at the foot of a tree: a fan of thin blades.
static void e5_tuft(E5Grid& g, int x, int y, const E5Pal& p, int seed)
{
    const int K = E5Grid::K;
    for (int j = -10; j <= 10; j += 2) {
        int hgt = 3 + (int)(rnd(j, seed, 61) * 5);
        int bx = x * K + 2 + j, by = y * K + K - 1;
        int lean = j < 0 ? -1 : 1;
        for (int k = 0; k < hgt; ++k) g.setf(bx + (k > hgt / 2 ? lean : 0), by - k, (j < 0 || k == hgt - 1) ? p.lit : p.mid);
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
    g.rectf(x + 0.2f, (float)(baseY - trunkH), 0.6f, (float)trunkH, trunk);
    int tiers = std::max(2, h / 6), top = baseY - h, bodyH = h - trunkH;
    const float cx = x + 0.5f;
    for (int t = 0; t < tiers; ++t) {
        int ty0 = top + t * bodyH / (tiers + 1), ty1 = top + (t + 2) * bodyH / (tiers + 1);
        if (t == tiers - 1) ty1 = baseY - trunkH;
        float maxHalf = 1.0f + (h * 0.32f) * (t + 1) / tiers;
        g.each(cx - maxHalf - 1, (float)ty0, cx + maxHalf + 1, (float)ty1, [&](int fx, int fy, float ax, float ay) {
            float f = (ay - ty0) / (ty1 - ty0);
            float half = maxHalf * f + 0.3f;
            if ((fy / 2) % 2 == 1 && half > 1 && rnd(fy / 2, t, seed) < 0.5f) half -= 0.5f;   // ragged needle tips
            float u = ax - cx;
            if (std::fabs(u) > half) return;
            Col c = u > 0.3f ? p.dark : (u > -half * 0.5f - 0.5f || ay > ty1 - 1.5f ? p.mid : p.lit);
            if (u < 0 && ay < ty0 + 1.5f) c = p.lit;
            g.setf(fx, fy, c);
        });
    }
}

// A layered ridge with a lit face where the ground rises to the right (light from the left),
// a lighter rim along the top, and fill down to the bottom.
// whole periods only, so it tiles
static float e5_ridgeTop(const E5Grid& g, float x, float base, int seed, float amp, int period, float a2 = 0.5f, float a3 = 0.25f)
{
    float t = x / g.w * 6.2831853f;
    return base + amp * std::sin(t * period + seed) + amp * a2 * std::sin(t * (period * 2 + 1) + seed * 1.7f) +
           amp * a3 * std::sin(t * period * 6 + seed * 2.3f);
}

static void e5_ridge(E5Grid& g, float base, int seed, float amp, int period, Col fill, Col lit, Col rim, float a2 = 0.5f, float a3 = 0.25f)
{
    auto top = [&](float x) { return e5_ridgeTop(g, x, base, seed, amp, period, a2, a3); };
    const int K = E5Grid::K;
    for (int fx = 0; fx < g.fw; ++fx) {
        float ax = (fx + 0.5f) / K, y0 = top(ax);
        float slope = top(ax + 2) - top(ax - 2);
        for (int fy = std::max(0, (int)std::floor(y0 * K)); fy < g.fh; ++fy) {
            float ay = (fy + 0.5f) / K;
            if (ay < y0) continue;
            Col c = fill;
            if (ay < y0 + 0.5f) c = rim;
            else if (slope < -0.25f && ay < y0 + 0.5f + (-slope - 0.25f) * 6) c = lit;   // grows smoothly with the slope
            g.setf(fx, fy, c);
        }
    }
}

// A stepped cloud: stacked blobs with a flat bottom, three tones from the top down. The lit top
// follows the bumps of the blobs.
static void e5_cloud(E5Grid& g, float cx, float cy, float size, Col top, Col mid, Col under, int seed)
{
    struct B { float x, y, r; };
    B bl[5] = {{cx, cy - size * 0.35f, size}, {cx - size * 1.2f, cy, size * 0.7f}, {cx + size * 1.3f, cy + size * 0.05f, size * 0.62f},
               {cx - size * 2.1f, cy + size * 0.3f, size * 0.4f}, {cx + size * 2.2f, cy + size * 0.3f, size * 0.38f}};
    float flat = cy + size * 0.5f;
    (void)seed;
    g.each(cx - size * 3, cy - size * 1.5f, cx + size * 3, flat, [&](int fx, int fy, float px, float py) {
        bool in = false;
        float lift = 0;   // how far down from its own blob top this point is
        for (auto& b : bl) {
            float dx = (px - b.x), dy = (py - b.y) * 1.6f;
            if (dx * dx + dy * dy < b.r * b.r) {
                if (!in) lift = 1e9f;
                in = true;
                float topY = b.y - std::sqrt(std::max(0.0f, b.r * b.r - dx * dx)) / 1.6f;
                lift = std::min(lift, py - topY);
            }
        }
        if (!in) return;
        float rel = (py - (cy - size * 1.3f)) / (flat - (cy - size * 1.3f));
        Col c = rel < 0.82f ? mid : under;
        if (lift < size * 0.45f && rel < 0.8f) c = top;
        g.setf(fx, fy, c);
    });
}

// Flat sky bands with a stepped (staircase) edge between each pair.
static void e5_sky(E5Grid& g, const uint32_t* bands, int n, float endFrac, int seed)
{
    g.each(0, 0, (float)g.w, (float)g.h, [&](int fx, int fy, float ax, float ay) {
        float f = ay / (g.h * endFrac) * n;
        f += (((int)ax / 6 + seed) % 3 == 0 ? 0.08f : 0.0f);   // a small stair offset so the band edges step
        int b = std::min(n - 1, std::max(0, (int)f));
        g.setf(fx, fy, hex(bands[b]));
    });
}

// A flat sun: a crisp disc inside one ring of lighter sky.
static void e5_sun(E5Grid& g, float cx, float cy, float r, Col core, Col ring1, Col ring2)
{
    g.each(cx - r * 2, cy - r * 2, cx + r * 2, cy + r * 2, [&](int fx, int fy, float ax, float ay) {
        float dx = ax - cx, dy = ay - cy, d = std::sqrt(dx * dx + dy * dy);
        if (d < r) g.setf(fx, fy, core);
        else if (d < r * 1.3f) g.setf(fx, fy, ring1);
        (void)ring2;
    });
}

// A traditional domed haystack, lit on the left, with short straw strokes.
static void e5_haystack(E5Grid& g, int x, int baseY, int w, int h, const E5Pal& p, int seed)
{
    float rx = w * 0.5f, cx = x + 0.5f;
    g.each(cx - rx - 2, (float)(baseY - h), cx + rx + 2, (float)baseY, [&](int fx, int fy, float ax, float ay) {
        float t = (ay - (baseY - h)) / h;   // 0 top .. 1 base
        float half = t < 0.55f ? rx * std::sqrt(std::max(0.0f, 1 - (0.55f - t) * (0.55f - t) / (0.55f * 0.55f))) : rx * (1.0f + (t - 0.55f) * 0.15f);
        float u = (ax - cx) / (half + 0.5f);
        if (std::fabs(u) > 1.0f) return;
        Col c = u < -0.25f ? p.lit : (u < 0.45f ? p.mid : p.dark);
        if (t > 0.86f) c = u < 0.2f ? p.mid : p.dark;   // shaded skirt
        if ((fy + fx / 5 + seed) % 5 == 0 && t > 0.2f && rnd(fx / 5, fy, seed) < 0.5f) c = c.r == p.lit.r ? p.mid : p.dark;   // straw
        g.setf(fx, fy, c);
    });
    g.rectf(cx - 0.25f, baseY - h - 0.75f, 0.5f, 0.75f, p.dark);   // the little top knot
}

// A small rectangular bale seen slightly from the side: lit top, front face, dark end.
static void e5_bale(E5Grid& g, int x, int baseY, int w, int h, const E5Pal& p)
{
    g.rectf((float)x, (float)(baseY - h), (float)w, 0.75f, p.lit);
    g.rectf((float)x, baseY - h + 0.75f, w - 0.75f, h - 0.75f, p.mid);
    g.rectf(x + w - 0.75f, baseY - h + 0.75f, 0.75f, h - 0.75f, p.dark);
    if (w > 3) g.rectf(x + w / 3.0f, baseY - h + 0.75f, 0.5f, h - 0.75f, p.dark);   // the twine
}

// A stook: sheaves leaned together, a little triangle.
static void e5_stook(E5Grid& g, int x, int baseY, int h, const E5Pal& p)
{
    float cx = x + 0.5f;
    g.each(cx - h - 1, (float)(baseY - h), cx + h + 1, (float)baseY, [&](int fx, int fy, float ax, float ay) {
        float u = ax - cx, half = (ay - (baseY - h)) * 0.667f + 0.6f;
        if (std::fabs(u) > half) return;
        g.setf(fx, fy, u < -0.3f ? p.lit : (u <= 0.3f ? p.mid : p.dark));
    });
}

// A hedgerow: a lumpy band of small clusters along y (its bottom row is y).
static void e5_hedge(E5Grid& g, int x0, int x1, int y, int hgt, const E5Pal& p, int seed)
{
    const int K = E5Grid::K;
    for (int fx = x0 * K; fx < x1 * K; ++fx) {
        float ax = (fx + 0.5f) / K;
        float lump = 0.5f + 0.5f * std::sin(ax * 0.9f + seed) * std::sin(ax * 0.37f + seed * 2.0f);
        float top = y - hgt + lump * 2 + rnd(fx / 3, seed, 71) * 0.8f;
        int phase = ((int)std::floor(ax) + seed) % 5;
        for (int fy = (int)std::ceil(top * K - 0.5f); fy < (y + 1) * K; ++fy) {
            float rel = ((fy + 0.5f) / K - top) / std::max(1.0f, y + 1 - top);
            Col c = rel < 0.34f ? (phase < 3 ? p.lit : p.mid) : (rel < 0.7f ? p.mid : p.dark);
            g.setf(fx, fy, c);
        }
    }
}

static void e5_fence(E5Grid& g, int x0, int x1, int y, Col post, Col postD, Col rail)
{
    g.rectf((float)x0, y - 3 + 0.25f, (float)(x1 - x0), 0.5f, rail);
    g.rectf((float)x0, y - 1 + 0.25f, (float)(x1 - x0), 0.5f, rail);
    for (int x = x0; x < x1; x += 5) {
        g.rectf((float)x, (float)(y - 4), 0.75f, 5, post);
        g.rectf(x + 0.75f, (float)(y - 4), 0.5f, 5, postD);
    }
}

// The farmhouse: whitewashed walls, a red-brown tiled roof, chimneys, warm lit windows.
static void e5_farmhouse(E5Grid& g, int x, int baseY, int w, int h)
{
    Col wall = hex(0xf2e2c4), wallD = hex(0xc8b0a0), roof = hex(0xb84a32), roofL = hex(0xd8683e), roofD = hex(0x7a2c30);
    Col win = hex(0xffd878), frame = hex(0x5a3a3a), door = hex(0x3a5a6a), stone = hex(0x9a8a90);
    int roofH = w / 3;
    g.rect(x, baseY - h, w, h, wall);
    g.rect(x + w - 3, baseY - h, 3, h, wallD);   // shaded gable end
    g.rectf((float)x, baseY - 0.75f, (float)w, 0.75f, stone);   // plinth
    // a long hipped roof: the eaves overhang two units, each course of tiles a little band
    float eaves = (float)(baseY - h);
    g.each(x - 2.0f, eaves - roofH, x + w + 2.0f, eaves, [&](int fx, int fy, float ax, float ay) {
        float r = eaves - ay;   // height above the eaves
        float l = x - 2 + r, rr = x + w + 2 - r;
        if (ax < l || ax >= rr) return;
        Col c = ((int)(r * 2) % 3 == 0) ? roofD : roof;   // courses of tiles
        if (r >= roofH - 1.5f) c = roofL;                  // the ridge catches the sun
        if (ax > rr - (r + 2.5f)) c = roofD;               // the hip on the shaded end
        else if (ax < l + 1.5f) c = roofL;                 // the lit hip
        g.setf(fx, fy, c);
    });
    g.rectf(x + w / 5.0f, eaves - roofH - 3, 1.5f, 4, stone);   // chimneys
    g.rectf(x + w * 3 / 4.0f - 2, eaves - roofH - 2, 1.5f, 3, stone);
    for (int i = 0; i < 3; ++i)   // a curl of smoke
        g.rectf(x + w / 5.0f + (i % 2) * 0.75f, eaves - roofH - 4.5f - i * 1.6f, 0.75f + 0.25f * i, 0.75f,
                mixc(hex(0x9aa8c8), hex(0xc8d0e4), i / 2.0f));
    int nWin = std::max(2, w / 7);
    for (int i = 0; i < nWin; ++i) {
        int wx = x + 2 + i * (w - 6) / nWin;
        for (int row = 0; row < 2; ++row) {
            if (row == 1 && i == nWin / 2) continue;
            float wy = (float)(baseY - h + 2 + row * 5);
            g.rect(wx, (int)wy, 3, 3, win);
            g.rectf(wx + 1.25f, wy, 0.5f, 3, frame);       // glazing bars
            g.rectf((float)wx, wy + 1.25f, 3, 0.5f, frame);
            g.rectf(wx + 2.5f, wy, 0.5f, 3, frame);        // the shaded reveal
        }
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
    for (float i = x + 0.5f; i < x + w - 3; i += 1.5f) g.rectf(i, (float)(baseY - h), 0.5f, (float)h, wallL);   // boards
    g.rect(x + w - 3, baseY - h, 3, h, wallD);
    int roofH = w / 3 + 1;
    float eaves = (float)(baseY - h + 1), cx = x + w / 2 + 0.5f, span = w / 2 + 2.5f;
    g.each(cx - span, eaves - roofH, cx + span, eaves, [&](int fx, int fy, float ax, float ay) {
        float r = eaves - ay, half = span * (1 - r / roofH), u = ax - cx;
        if (std::fabs(u) > half) return;
        Col c = u < 0 ? (fx % 6 < 2 ? roof : roofL) : roofD;   // corrugations on the lit side
        g.setf(fx, fy, c);
    });
    // the open door, hay piled inside
    int dw = w / 3, dx = x + w / 2 - dw / 2, dh = h * 3 / 4;
    g.rect(dx, baseY - dh, dw, dh, dark);
    g.each((float)dx, (float)(baseY - dh), (float)(dx + dw), (float)baseY, [&](int fx, int fy, float ax, float ay) {
        float hh = dh / 2.0f + 2 * std::sin((ax - dx) * 0.9f), top = baseY - hh;
        if (ay < top) return;
        g.setf(fx, fy, ay < top + 0.5f ? hay : (ax < dx + dw / 2.0f ? hay : hayD));
    });
    g.rect(x + w / 2 - 1, baseY - h - 3, 2, 2, hex(0x2a1a2a));   // hay loft hatch
}

bool bgEra5(Scene& sc, int layer)
{
    const int W = sc.W, H = sc.H;
    E5Grid g(W, H);
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
            float y = gh * (0.36f + 0.06f * i), x0 = gw * (0.6f + 0.05f * i), len = 14.0f + i * 4;
            g.rectf(x0, y, len, 0.5f, hex(0xfce4c0));
        }
        g.blit(sc);
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
        const float rowH[8] = {3, 4, 5, 6, 7, 8, 9, 10};
        auto rowOf = [&](float d, float& start) {
            int r = 0;
            float acc = 0;
            while (r < 7 && d >= acc + rowH[r]) { acc += rowH[r]; ++r; }
            start = acc;
            return r;
        };
        const int K = E5Grid::K;
        for (int fx = 0; fx < g.fw; ++fx) {
            float ax = (fx + 0.5f) / K, top = e5_ridgeTop(g, ax, b3, 11, a3, 4);
            for (int fy = (int)std::floor((top + 2) * K); fy < g.fh; ++fy) {
                float ay = (fy + 0.5f) / K, d = ay - top - 2;
                if (d < 0) continue;
                float start;
                int row = rowOf(d, start);
                float span = 14.0f + row * 6, shift = rnd(row, 0, 310) * 40 + (ay - top) / 2;
                int cut = (int)std::floor((ax + shift) / span);
                int k = (int)(rnd(cut, row, 311) * 5);
                Col c = hex(patch[k]);
                bool hedgeRow = d - start < 0.6f && rnd(cut, row, 313) < 0.8f;
                bool hedgeCut = std::fmod(ax + shift, span) < 0.6f;
                if (hedgeRow || hedgeCut) c = hex(0x4a7048);
                else if (k % 2 == 0 && fy % 4 == 0) c = mul(c, 0.94f);   // mown lines in the hay
                g.setf(fx, fy, c);
            }
        }
        // tree clumps sitting on the hedge lines, bigger lower down
        for (int i = 0; i < 70; ++i) {
            int x = (int)(rnd(i, 1, 314) * gw), row = 1 + (int)(rnd(i, 2, 314) * 5);
            float top = e5_ridgeTop(g, (float)x, b3, 11, a3, 4), acc = 0;
            for (int r = 0; r < row; ++r) acc += rowH[r];
            float sz = 1.2f + row * 0.45f;
            e5_canopy(g, (float)x, top + 2 + acc - sz * 0.6f, sz * 1.3f, sz, leafFar, i + 200, 4);
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
        g.rect(0, gh - 3, gw, 3, leafFar.dark);
        g.blit(sc);
        return true;
    }

    if (layer == 2) {
        // mid: the hay meadow with the farm. The bottom 16% sits below the lawn.
        int fieldTop = (int)(gh * 0.66f);
        // gently rolling field edge, mown in rows
        g.each(0, fieldTop - 2.0f, (float)gw, (float)gh, [&](int fx, int fy, float ax, float ay) {
            float top = fieldTop + 1.5f * std::sin(ax * 6.2831853f / gw * 3 + 1.0f);
            if (ay < top) return;
            float rr = std::fmod(ay - top, 5.0f);
            Col c = hex(0xe8c470);
            if (ay < top + 0.5f) c = hex(0xf8e098);
            else if (rr >= 2.0f && rr < 2.75f) c = hex(0xd2a858);   // mown rows
            else if (rr >= 3.0f && rr < 3.5f) c = hex(0xf2d488);
            g.setf(fx, fy, c);
        });
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
        g.blit(sc);
        return true;
    }

    // near: rows of bales and big haystacks in front of a hedge with cluster trees. Drawn smaller
    // and further back in game; its bottom 14% sits below the lawn.
    int ground = (int)(gh * 0.8f);
    // stubble ground strip
    g.each(0, (float)ground, (float)gw, (float)gh, [&](int fx, int fy, float, float ay) {
        Col c = hex(0xe0b860);
        if (ay < ground + 0.5f) c = hex(0xf4d68a);
        else if ((fx * 3 + fy * 7) % 29 == 0) c = hex(0xc8984a);
        g.setf(fx, fy, c);
    });
    // a hedge built of leaf clumps, with a few big cluster trees standing out of it
    e5_hedge(g, 0, gw, ground - 1, 6, hedge, 9);
    for (int i = 0; i < gw / 3; ++i) {
        float x = i * 3.0f + rnd(i, 1, 320) * 2, y = ground - 3.2f - rnd(i, 2, 320) * 1.6f;
        e5_canopy(g, x, y, 2.0f + rnd(i, 3, 320) * 0.8f, 1.7f, hedge, 400 + i, 3);
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
    g.blit(sc);
    return true;
}
