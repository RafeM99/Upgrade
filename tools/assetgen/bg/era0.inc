// Background scene for stage 0, included into SpriteGen.cpp just before bgLayer().
// 2025, "The night she got lost": a quiet winter street at night, in two colours.
// Gran and Sam are solid white figures, so the scenery is built from SOLID DARK silhouettes
// that sit behind them: every shape is filled with the night colour and only its top edge
// catches the light (snow lying on roofs, branches, walls and hedges). Large areas (the far
// fields, a snowy roof, the lamp's light) get a sparse ordered dither and nothing else, so they
// stay far darker than the characters. One pixel density everywhere: 1 px.

static void e0_L(Scene& sc, int x, int y)
{
    if (y >= 0 && y < sc.H) sc.p.set(x, y, ONEBIT_LIGHT);   // Px::set wraps x, so layers tile
}
static void e0_D(Scene& sc, int x, int y)
{
    if (y >= 0 && y < sc.H) sc.p.set(x, y, ONEBIT_DARK);
}
static bool e0_bayer(int x, int y, float d) { return bayer4f(((x % 312) + 312) % 312, y) < d; }

// A snow-topped silhouette: every pixel inside is night-dark, the topmost pixel of each column
// run is snow (light), the row under it is half snow on wide flat tops, and the body may carry
// a sparse dither (`fill`, 0 = none). inside(x, y) decides the shape.
template <class F>
static void e0_shape(Scene& sc, int x0, int y0, int x1, int y1, F inside, float fill, bool thickSnow = true, bool sides = false)
{
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            if (!inside(x, y)) continue;
            bool top = !inside(x, y - 1);
            // on steep slopes (pine tiers) the rim follows the side too, so it never breaks up
            if (sides && !top && inside(x, y + 1))
                top = (!inside(x - 1, y) && !inside(x - 1, y - 1)) || (!inside(x + 1, y) && !inside(x + 1, y - 1));
            bool second = !top && !inside(x, y - 2) && inside(x - 1, y - 1) && inside(x + 1, y - 1) &&
                          inside(x - 2, y - 1) && inside(x + 2, y - 1);   // thick snow only on wide flat tops
            if (top) e0_L(sc, x, y);
            else if (second && thickSnow) e0_L(sc, x, y);
            else if (fill > 0 && e0_bayer(x, y, fill)) e0_L(sc, x, y);
            else e0_D(sc, x, y);
        }
}

// A snowy pine: stepped tiers, each a little wider at the bottom, snow on every tier's upper
// edge. h = height, w = half width of the lowest tier.
static void e0_pine(Scene& sc, int x, int base, int h, int w, int tiers, bool flip, int seed)
{
    int trunk = std::max(2, h / 6);
    float tierH = (float)(h - trunk) / (tiers * 0.75f + 0.25f);
    int top = base - h;
    auto inside = [&](int px, int py) {
        int dx = px - x;
        if (flip) dx = -dx;
        if (py > base) return false;
        if (py > base - trunk) return dx == 0 || dx == -1;
        for (int t = 0; t < tiers; ++t) {
            float ty0 = top + t * tierH * 0.75f, ty1 = ty0 + tierH;
            if (py < ty0 || py > ty1) continue;
            float f = (py - ty0) / tierH;
            float half = (1.0f + (w * (0.45f + 0.55f * (t + 1) / tiers) - 1) * f);
            // ragged needle tips on the bottom row of a tier
            if (py >= (int)ty1 - 1 && rnd(dx, t, seed) < 0.35f) half -= 1;
            if (dx >= -half - 0.5f && dx <= half - 0.5f + (dx > 0 ? 0.3f : 0)) return true;
        }
        return false;
    };
    e0_shape(sc, x - w - 2, top - 1, x + w + 2, base, inside, 0, false, true);
}

// A round evergreen (holly or yew) built from clumps, snow on each clump's top.
static void e0_bush(Scene& sc, float x, float base, float r, int n, int seed, bool trunk)
{
    struct C { float x, y, r; };
    std::vector<C> cl;
    float cy = trunk ? base - r * 1.25f : base - r * 0.7f;
    cl.push_back({x, cy, r * 0.75f});
    for (int i = 0; i < n; ++i) {
        float a = 3.14159f + i * 3.14159f / (n - 1) + (rnd(i, seed, 3) - 0.5f) * 0.4f;
        float rr = r * (0.42f + 0.18f * rnd(i, seed, 4));
        cl.push_back({x + std::cos(a) * (r - rr), cy + std::sin(a) * (r - rr) * 0.9f, rr});
    }
    auto inside = [&](int px, int py) {
        if (py > base) return false;
        if (trunk && py > cy && std::abs(px + 0.5f - x) < 1.0f) return true;
        for (auto& c : cl) {
            float dx = px + 0.5f - c.x, dy = (py + 0.5f - c.y) * 1.1f;
            if (dx * dx + dy * dy < c.r * c.r) return true;
        }
        return false;
    };
    e0_shape(sc, (int)(x - r - 2), (int)(cy - r * 1.3f - 2), (int)(x + r + 2), (int)base, inside, 0, true);
}

bool bgEra0(Scene& sc, int layer)
{
    const int W = sc.W, H = sc.H;
    sc.fade = 0;   // two colours exactly

    if (layer == 0) {   // the night sky: stars (none in the HUD band), a crescent moon
        // the dark sky, and a faint glow of town light and snow toward the horizon: ordered
        // dither in a few flat steps, so the dark silhouettes in front read against it
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float d = y < H * 0.38f ? 0 : 0.05f;
                if (d > 0 && e0_bayer(x, y, d)) e0_L(sc, x, y); else e0_D(sc, x, y);
            }
        const int stars[14][2] = {{12, 14}, {44, 22}, {63, 12}, {101, 27}, {118, 17}, {150, 13}, {176, 25},
                                  {199, 16}, {231, 11}, {247, 27}, {259, 21}, {284, 13}, {303, 26}, {30, 29}};
        for (auto& st : stars) e0_L(sc, st[0] * W / 312, st[1]);
        // two brighter stars: a tiny cross
        for (int k = -1; k <= 1; ++k) { e0_L(sc, 214 * W / 312 + k, 20); e0_L(sc, 214 * W / 312, 20 + k); }
        for (int k = -1; k <= 1; ++k) { e0_L(sc, 22 * W / 312 + k, 25); e0_L(sc, 22 * W / 312, 25 + k); }
        int mx = (int)(W * 0.3f), my = 21;
        for (int y = -6; y <= 6; ++y)
            for (int x = -6; x <= 6; ++x) {
                bool in = x * x + y * y <= 36, cut = (x - 3) * (x - 3) + (y + 2) * (y + 2) <= 30;
                if (in && !cut) e0_L(sc, mx + x, my + y);
            }
        return true;
    }

    if (layer == 1) {   // far: moonlit snowfields with a sparse dither, a village row on the crest
        auto ridgeY = [&](int x) { return (int)std::lround(H * 0.49f + ridge((float)x, W, 3, 3.0f, 1.4f, 0.4f)); };
        for (int x = 0; x < W; ++x) {
            int t = ridgeY(x);
            e0_L(sc, x, t);
            for (int y = t + 1; y < H; ++y) {   // moonlit snow: a sparse even dither
                if (e0_bayer(x, y, 0.15f)) e0_L(sc, x, y); else e0_D(sc, x, y);
            }
        }
        // the village: dark houses of different sizes, snow on the roofs, a few windows lit
        float hx = W * 0.3f;
        int i = 0;
        while (hx < W * 0.92f) {
            if (hx > W * 0.55f && hx < W * 0.7f) { hx = W * 0.7f; continue; }   // a gap in the row
            int w = 7 + (int)(rnd(i, 1, 5) * 6), wall = 3 + (int)(rnd(i, 2, 5) * 4), roof = (w + 1) / 2;
            int x0 = (int)hx, base = ridgeY(x0 + w / 2) + 3, top = base - wall;
            bool church = i == 3;
            auto inside = [&](int px, int py) {
                int u = px - x0;
                if (u < 0 || u > w || py > base + 2) return false;
                if (py >= top) return true;
                if (church && u >= w / 2 - 1 && u <= w / 2 + 1 && py >= top - roof - 6) return true;   // a steeple
                return py >= top - roof + std::abs(u - w / 2);
            };
            e0_shape(sc, x0, top - roof - 8, x0 + w, base + 2, inside, 0, false);
            if (church) e0_L(sc, x0 + w / 2, top - roof - 7);
            if (rnd(i, 3, 5) < 0.6f) e0_L(sc, x0 + 2 + (int)(rnd(i, 6, 5) * (w - 4)), top + 1);
            if (rnd(i, 4, 5) < 0.3f) e0_L(sc, x0 + 2 + (int)(rnd(i, 7, 5) * (w - 4)), top + 2);
            hx += w + 2 + (int)(rnd(i, 8, 5) * 5);
            ++i;
        }
        // small far pines between the houses
        const float pines[8][2] = {{0.03f, 7}, {0.07f, 9}, {0.11f, 6}, {0.21f, 8}, {0.25f, 6}, {0.6f, 9}, {0.64f, 7}, {0.96f, 8}};
        for (int k = 0; k < 8; ++k) {
            int x = (int)(W * pines[k][0]);
            e0_pine(sc, x, ridgeY(x) + 2, (int)pines[k][1], 2 + k % 2, 2, k % 2, 40 + k);
        }
        return true;
    }

    if (layer == 2) {   // mid: a nearer snowfield, a stand of snowy pines and evergreens
        int g = (int)(H * 0.82f);
        auto fieldY = [&](int x) { return (int)std::lround(H * 0.68f + ridge((float)x, W, 6, 1.6f, 0.8f, 0)); };
        for (int x = 0; x < W; ++x) {
            int t = fieldY(x);
            e0_L(sc, x, t);
            for (int y = t + 1; y < H; ++y) {
                if (e0_bayer(x + 2, y + 2, 0.1f)) e0_L(sc, x, y); else e0_D(sc, x, y);
            }
        }
        // pines of different heights and widths, some mirrored, in loose groups
        const struct { float x; int h, w, tiers; } pn[] = {
            {0.02f, 24, 6, 4}, {0.07f, 17, 5, 3}, {0.235f, 21, 6, 3}, {0.27f, 27, 7, 4},
            {0.505f, 19, 5, 3}, {0.69f, 25, 7, 4}, {0.735f, 15, 4, 3}, {0.9f, 22, 6, 4}};
        int k = 0;
        for (auto& p : pn) {
            int x = (int)(W * p.x);
            e0_pine(sc, x, fieldY(x) + 2, p.h, p.w, p.tiers, k % 2 == 1, 60 + k);
            ++k;
        }
        e0_bush(sc, W * 0.13f, (float)fieldY((int)(W * 0.13f)) + 2, 7, 5, 81, true);
        e0_bush(sc, W * 0.585f, (float)fieldY((int)(W * 0.585f)) + 2, 5.5f, 4, 82, true);
        e0_bush(sc, W * 0.82f, (float)fieldY((int)(W * 0.82f)) + 2, 6.5f, 5, 83, true);
        // falling snow, sparse, kept below the HUD band and above the players
        for (int i = 0; i < 16; ++i) {
            int x = (int)(rnd(i, 1, 404) * W), y = 11 + (int)(rnd(i, 2, 404) * H * 0.42f);
            e0_L(sc, x, y);
        }
        return true;
    }

    // near: the street. Gran's house, a low garden wall, a hedge, snowbanks, a street lamp.
    int g = (int)(H * 0.82f);   // where the game's ground starts
    {   // snowbanks along the whole street, so the band above the ground is never empty
        auto bankY = [&](int x) {
            float v = H * 0.775f + ridge((float)x, W, 9, 1.4f, 1.0f, 0.5f);
            return (int)std::lround(v);
        };
        auto inside = [&](int px, int py) { return py >= bankY(((px % W) + W) % W) && py <= g + 3; };
        e0_shape(sc, 0, (int)(H * 0.7f), W - 1, g + 3, inside, 0.05f, false);
    }
    {   // Gran's house: dark walls, a snowy roof (sparse dither), the lit kitchen window
        int hx = (int)(W * 0.12f), hw = 26, wall = 15, roof = 12;
        int top = g - wall;
        auto inside = [&](int px, int py) {
            int u = px - hx;
            if (py > g + 2) return false;
            if (u >= 18 && u <= 20 && py >= top - roof + 1 && py <= top - roof + 7) return true;   // chimney
            if (u >= 0 && u <= hw && py >= top) return true;
            return u >= -2 && u <= hw + 2 && py >= top - roof + std::abs(u - hw / 2) * roof / (hw / 2 + 2);
        };
        // the roof gets a light dither (snow), the walls stay dark
        for (int y = top - roof - 1; y <= g + 2; ++y)
            for (int x = hx - 3; x <= hx + hw + 3; ++x) {
                if (!inside(x, y)) continue;
                bool topEdge = !inside(x, y - 1);
                bool roofArea = y < top && !(x - hx >= 18 && x - hx <= 20 && y < top - roof + 8 && !topEdge);
                if (topEdge) e0_L(sc, x, y);
                else if (y == top && (x < hx || x > hx + hw)) e0_L(sc, x, y);        // eaves
                else if (roofArea && e0_bayer(x, y, 0.25f)) e0_L(sc, x, y);
                else e0_D(sc, x, y);
            }
        // the kitchen window, lit: solid light with a dark cross
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 6; ++x) {
                bool bar = x == 2 || y == 2;
                if (bar) e0_D(sc, hx + 4 + x, top + 3 + y); else e0_L(sc, hx + 4 + x, top + 3 + y);
            }
        // the door, a dark frame with a light knob
        for (int y = top + 6; y <= g; ++y) { e0_L(sc, hx + 15, y); e0_L(sc, hx + 20, y); }
        for (int x = hx + 15; x <= hx + 20; ++x) e0_L(sc, x, top + 6);
        e0_L(sc, hx + 19, top + 11);
        // window light falling on the snow in front
        for (int y = g - 3; y <= g + 1; ++y)
            for (int x = hx + 1; x <= hx + 12; ++x) {
                float d = std::abs(x - (hx + 6.5f)) / 6.5f + (g + 1 - y) * 0.15f;
                if (e0_bayer(x, y, 0.5f * (1 - d))) e0_L(sc, x, y);
            }
    }
    {   // a low garden wall with snow on the coping, stones hinted by a few light chips
        int wx0 = (int)(W * 0.24f), wx1 = (int)(W * 0.47f), wt = g - 7;
        auto inside = [&](int px, int py) {
            if (px < wx0 || px > wx1 || py > g + 2) return false;
            if (px % 22 == 3 || px % 22 == 4) return py >= wt - 2;   // pillars
            return py >= wt;
        };
        e0_shape(sc, wx0, wt - 3, wx1, g + 2, inside, 0, true);
        for (int x = wx0 + 3; x < wx1 - 2; x += 5) e0_L(sc, x + ((x / 5) % 2) * 2, wt + 3 + (x / 5) % 2);
    }
    {   // a street lamp: dark pole with a light edge, a lit lamp, and a dithered pool of light
        int lx = (int)(W * 0.6f), top = g - 33;
        for (int y = top; y <= g; ++y) { e0_D(sc, lx + 1, y); if (y % 3 != 0) e0_L(sc, lx, y); }
        for (int x = lx; x <= lx + 6; ++x) e0_L(sc, x, top);      // the arm
        for (int x = lx + 3; x <= lx + 8; ++x) e0_D(sc, x, top + 1);
        e0_L(sc, lx + 3, top + 1); e0_L(sc, lx + 8, top + 1);
        for (int x = lx + 4; x <= lx + 7; ++x) { e0_L(sc, x, top + 2); e0_L(sc, x, top + 3); }   // the lamp
        // the cone of light and the pool on the snow, thinning with distance from the lamp
        float cx = lx + 5.5f;
        for (int y = top + 4; y <= g + 1; ++y) {
            float t = (float)(y - top - 4) / (g - top - 4);
            float half = 2.5f + t * 10.0f;
            for (int x = (int)(cx - half - 4); x <= (int)(cx + half + 4); ++x) {
                float u = std::abs(x + 0.5f - cx) / half;
                float d = 0;
                if (u < 1) d = 0.14f * (1 - t * 0.5f);
                if (y >= g - 3) {   // the pool on the ground, brighter in the middle
                    float pu = std::abs(x + 0.5f - cx) / 14.0f;
                    if (pu < 1) d = std::max(d, 0.5f * (1 - pu));
                }
                if (d > 0 && e0_bayer(x, y, d)) e0_L(sc, x, y);
            }
        }
    }
    {   // a hedge: clumps, dark, snow on each clump's top
        float hx0 = W * 0.7f, hx1 = W * 0.97f;
        struct C { float x, y, r; };
        std::vector<C> cl;
        for (float x = hx0; x < hx1; x += 5.0f) cl.push_back({x, g - 6.5f + rnd((int)x, 1, 9) * 2.5f, 4.0f + rnd((int)x, 2, 9) * 2.0f});
        auto inside = [&](int px, int py) {
            if (py > g + 2) return false;
            if (px >= hx0 && px <= hx1 - 3 && py >= g - 5) return true;
            for (auto& c : cl) {
                float dx = px + 0.5f - c.x, dy = (py + 0.5f - c.y) * 1.15f;
                if (dx * dx + dy * dy < c.r * c.r) return true;
            }
            return false;
        };
        e0_shape(sc, (int)hx0 - 8, g - 16, (int)hx1 + 8, g + 2, inside, 0, true);
    }
    // one tall pine and a round evergreen in the near street
    e0_pine(sc, (int)(W * 0.52f), g, 40, 10, 5, false, 91);
    e0_bush(sc, W * 0.905f, (float)g - 6, 10, 6, 92, true);
    return true;
}
