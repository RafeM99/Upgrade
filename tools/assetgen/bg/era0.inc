// Background scene for stage 0, included into SpriteGen.cpp just before bgLayer().
// "Last winter": a winter night street, in exactly two colours.
// Gran and Sam (and the enemies and platforms) are solid white with a black halo, so the scenery
// is built the other way round: solid black silhouettes standing on big, smooth planes of snow
// drawn in ordered dither (Bayer 4x4, the matrix the stage's screen shader uses), plus a handful
// of tiny solid lights. Each plane is brighter than the one behind it, so depth reads as light:
//   sky    black, single-pixel stars, a crescent moon (below the HUD)
//   far    low snowy hills, the faintest tone (1/16, edge 4/16), a sleeping town on the ridge
//   mid    the snowfield (2/16, edge 8/16) with clumps of black pines
//   near   the street's snowbank (4/16, edge 8-12/16, also behind the ground tiles), Gran's
//          house with its lit kitchen window, lamps with pools of light, a fence, a car, a bench
// Dither is only used on big areas; small things are solid. Solid white never forms a long flat
// run, so nothing in the scenery can pass for a platform.

namespace e0 {
// Bayer 4x4 ordered dither: true when a pixel at light level `lvl` (0..16) is lit.
inline bool lit(int x, int y, int lvl)
{
    static const int m[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return m[(y & 3) * 4 + (x & 3)] < lvl;
}
}  // namespace e0

static void e0_set(Scene& sc, int x, int y, bool light)
{
    if (y < 0 || y >= sc.H) return;
    sc.p.set(x, y, light ? ONEBIT_LIGHT : ONEBIT_DARK);   // Px::set wraps x, so everything tiles
}
static void e0_put(Scene& sc, int x, int y) { e0_set(sc, x, y, true); }
static void e0_dark(Scene& sc, int x, int y) { e0_set(sc, x, y, false); }
// A pixel of an area at light level lvl/16 (dark where the pattern is off).
static void e0_tone(Scene& sc, int x, int y, int lvl) { e0_set(sc, x, y, e0::lit(x, y, lvl)); }
// Added light only: lights the pattern's pixels and leaves the rest as it is.
static void e0_glow(Scene& sc, int x, int y, int lvl)
{
    if (e0::lit(x, y, lvl)) e0_put(sc, x, y);
}
static void e0_darkRect(Scene& sc, int x, int y, int w, int h)
{
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) e0_dark(sc, x + i, y + j);
}

// The top of a snow plane at column x: a smooth line that tiles across the width.
static int e0_contour(const Scene& sc, int x, float base, int seed, float a1, float a2)
{
    return (int)std::lround(base + ridge((float)x, sc.W, seed, a1, a2, 0));
}
// A snow plane: everything below the contour, `edge` rows at edgeLvl along the top, then fillLvl.
static void e0_plane(Scene& sc, float base, int seed, float a1, float a2, int edgeLvl, int fillLvl, int edge)
{
    for (int x = 0; x < sc.W; ++x) {
        int top = e0_contour(sc, x, base, seed, a1, a2);
        for (int y = top; y < sc.H; ++y) e0_tone(sc, x, y, y < top + edge ? edgeLvl : fillLvl);
    }
}

// A pine in black: a cone of drooping tiers, ragged at the hem. With `snow`, every tier carries a
// layer of snow on its shoulders (a checker band), so the tree reads as stacked white chevrons
// even against the black sky; without it, it is a plain silhouette for the bright snowfield.
static void e0_pine(Scene& sc, int x, int base, float h, int seed, bool snow, bool flip)
{
    int trunk = std::max(2, (int)(h * 0.12f));
    e0_darkRect(sc, x - (h > 20 ? 1 : 0), base - trunk, h > 20 ? 2 : 1, trunk + 1);
    int tiers = h > 26 ? 5 : h > 15 ? 4 : 3;
    float top = base - h, bodyH = h - trunk;
    float tierH = bodyH / (tiers * 0.72f + 0.28f);
    float maxHalf = h * 0.27f;
    for (int t = tiers - 1; t >= 0; --t) {   // bottom tier first, so each upper tier sits on the one below
        float y0 = top + t * tierH * 0.72f, y1 = y0 + tierH;
        float half = maxHalf * (0.3f + 0.7f * (t + 1) / tiers);
        int iy0 = (int)std::lround(y0), iy1 = (int)std::lround(y1);
        // the first rows of this tier that show below the hem of the tier above
        int shows = t == 0 ? iy0 : (int)std::lround(top + (t - 1) * tierH * 0.72f + tierH) + 1;
        for (int y = iy0; y <= iy1; ++y) {
            float fr = (y - y0) / std::max(1.0f, y1 - y0);
            int hw = (int)std::lround(half * std::min(1.0f, fr * 1.05f));
            bool bottom = y == iy1;
            for (int dx = -hw; dx <= hw; ++dx) {
                if (bottom && std::abs(dx) < hw - 1 && rnd(dx, t, seed) < 0.35f) continue;   // ragged hem
                int px = x + (flip ? -dx : dx);
                bool moonSide = (flip ? dx : -dx) > 0;
                // snow: a band under the hem above, thicker on the moonlit side, and the tier's
                // moonlit edge
                bool lit = snow && !bottom &&
                           ((y < shows + (moonSide ? 3 : 2)) || (moonSide && std::abs(dx) >= hw - 1 && fr < 0.7f));
                e0_set(sc, px, y, lit && ((px + y) & 1));
            }
        }
    }
    e0_set(sc, x, (int)std::lround(top) - 1, snow);
}

// A gable house: black walls, a snow-covered roof as a flat dither (snow/16), lit windows as
// solid single lights. litMask picks which window slots are lit (one bit per slot).
static void e0_house(Scene& sc, int x, int base, int w, int wall, int roof, int litMask, int seed, bool chimney, int snow)
{
    int top = base - wall;
    if (chimney) {
        int cx = x + w - 3 - (int)(rnd(seed, 1, 3) * 2);
        e0_darkRect(sc, cx, top - roof, 2, roof);
    }
    e0_darkRect(sc, x, top, w, wall + 1);
    for (int r = 0; r <= roof; ++r) {
        int inset = (int)std::lround((float)r * (w / 2 + 1) / std::max(1, roof));
        for (int px = x - 1 + inset; px <= x + w - inset; ++px) e0_tone(sc, px, top - r, r == 0 ? 0 : snow);   // the eave stays dark
    }
    int nWin = std::max(1, (w - 2) / 3);
    for (int i = 0; i < nWin; ++i) {
        if (!((litMask >> i) & 1)) continue;
        int wx = x + 1 + i * 3 + (int)(rnd(i, seed, 4) * 1.5f);
        if (wx >= x + w - 1) continue;
        e0_put(sc, wx, top + 1 + (wall > 5 ? 1 : 0));
    }
}

bool bgEra0(Scene& sc, int layer)
{
    const int W = sc.W, H = sc.H;
    sc.fade = 0;   // two colours exactly
    const float fieldBase = 60;   // the far edge of the snowfield
    auto fieldTop = [&](int x) { return e0_contour(sc, x, fieldBase, 6, 1.6f, 1.0f); };

    if (layer == 0) {   // the night sky: black, stars, a crescent moon below the HUD
        sc.rect(0, 0, (float)W, (float)H, ONEBIT_DARK);
        for (int k = 0; k < 42; ++k) {   // stars thin out toward the horizon
            float r = rnd(k, 2, 77);
            e0_put(sc, (int)(rnd(k, 1, 77) * W), 2 + (int)(r * r * 44));
        }
        float mx = W * 0.68f, my = 24;
        for (int y = (int)my - 7; y <= (int)my + 7; ++y)
            for (int x = (int)mx - 7; x <= (int)mx + 7; ++x) {
                float dx = x + 0.5f - mx, dy = y + 0.5f - my, ex = dx - 2.8f, ey = dy + 2.0f;
                if (dx * dx + dy * dy < 6.3f * 6.3f) e0_set(sc, x, y, ex * ex + ey * ey > 5.6f * 5.6f);
            }
        return true;
    }

    if (layer == 1) {   // far: black mountains that only show their snow, and a sleeping town
        struct Peak { float x, top, slope; };
        const Peak pk[] = {{0.10f, 35, 0.70f}, {0.27f, 43, 0.80f}, {0.45f, 32, 0.62f}, {0.61f, 44, 0.85f},
                           {0.79f, 38, 0.72f}, {0.94f, 45, 0.80f}};
        const int np = (int)(sizeof(pk) / sizeof(pk[0]));
        for (int x = 0; x < W; ++x) {
            int best = 0;
            float bestTop = 1e9f, bdx = 0;
            for (int i = 0; i < np; ++i) {
                float dx = x + 0.5f - pk[i].x * W;
                dx -= std::round(dx / W) * W;   // wrap, so the range tiles
                float t = pk[i].top + std::abs(dx) * pk[i].slope;
                if (t < bestTop) { bestTop = t; best = i; bdx = dx; }
            }
            for (int y = std::max(0, (int)bestTop); y < fieldBase + 4; ++y) {
                float below = y - pk[best].top;
                bool sunny = bdx < below * 0.15f;
                // the moonlit face: a snow cap with its lower edge torn into gullies, then
                // snow slopes that fade into the dark toward the foot
                int capLine = 7 + (int)(5 * std::fabs(std::sin(bdx * 0.45f + best)));
                int lvl = 0;
                if (sunny) lvl = below < capLine ? 8 : 4;
                e0_tone(sc, x, y, lvl);
            }
        }
        // the town at the foot of the mountains: black houses, snowy roofs, a few lit windows
        struct Town { float x; int n; int seed; };
        const Town towns[] = {{0.03f, 4, 11}, {0.36f, 5, 12}, {0.73f, 3, 13}};
        for (const Town& t : towns) {
            float hx = t.x * W;
            for (int i = 0; i < t.n; ++i) {
                int w = 5 + (int)(rnd(i, t.seed, 1) * 4);
                int wall = 3 + (int)(rnd(i, t.seed, 2) * 3);
                int roof = 2 + (int)(rnd(i, t.seed, 3) * 2);
                int base = fieldTop((int)hx + w / 2) + 1;
                int mask = rnd(i, t.seed, 4) < 0.75f ? (rnd(i, t.seed, 5) < 0.5f ? 1 : 2) : 0;
                e0_house(sc, (int)hx, base, w, wall, roof, mask, t.seed * 7 + i, rnd(i, t.seed, 7) < 0.6f, 8);
                hx += w + 2 + (int)(rnd(i, t.seed, 8) * 2);
                if (t.seed == 12 && i == 1) {   // the church: a nave and a steeple with a lit belfry
                    int cx = (int)hx, cb = fieldTop(cx + 6) + 1;
                    e0_house(sc, cx, cb, 9, 4, 3, 0, 99, false, 8);
                    e0_darkRect(sc, cx + 9, cb - 9, 4, 10);
                    for (int r = 0; r < 6; ++r)
                        for (int dx = -(2 - r / 2); dx <= 2 - r / 2; ++dx) e0_tone(sc, cx + 11 + dx, cb - 10 - r, 8);
                    e0_dark(sc, cx + 11, cb - 16);
                    e0_put(sc, cx + 10, cb - 7);
                    hx += 15;
                }
            }
        }
        return true;
    }

    if (layer == 2) {   // mid: the moonlit snowfield, clumps of black pines standing in it
        for (int x = 0; x < W; ++x)
            for (int y = fieldTop(x); y < H; ++y) e0_tone(sc, x, y, 8);
        struct T { float x; float h; int sink; bool flip; };
        const T trees[] = {{0.020f, 13, 5, false}, {0.040f, 9, 4, true},  {0.058f, 11, 6, false},
                           {0.180f, 15, 6, true},  {0.200f, 10, 5, false},
                           {0.330f, 12, 6, false}, {0.346f, 8, 5, true},
                           {0.470f, 14, 7, true},  {0.490f, 16, 6, false}, {0.511f, 10, 5, false},
                           {0.640f, 12, 6, true},
                           {0.765f, 15, 6, false}, {0.784f, 9, 5, true},
                           {0.905f, 12, 5, true},  {0.925f, 16, 7, false}, {0.946f, 10, 5, true}};
        int i = 0;
        for (const T& t : trees) {
            int x = (int)(t.x * W);
            e0_pine(sc, x, fieldTop(x) + t.sink, t.h, 200 + i * 17, false, t.flip);
            ++i;
        }
        return true;
    }

    // near: the street. g is the ground line, level with the top of the ground tiles.
    const int g = 72;
    // behind the ground tiles (seen through the gaps) the snow falls away into a dark ditch
    for (int y = g; y < H; ++y)
        for (int x = 0; x < W; ++x) e0_tone(sc, x, y, y < g + 5 ? 8 : 0);

    // a street lamp: post, arm, hood, and the lamp itself with a small halo
    auto lamp = [&](int lx, int hgt, bool flip) {
        int top = g - hgt, f = flip ? -1 : 1;
        int hx = lx + f * 5;
        for (int y = top - 2; y <= top + 7; ++y)   // a small halo round the lamp
            for (int x = hx - 5; x <= hx + 5; ++x) {
                float dx = x - hx, dy = (y - (top + 2)) * 1.2f, d = dx * dx + dy * dy;
                if (d < 22 && d > 6) e0_tone(sc, x, y, d < 13 ? 8 : 4);
            }
        e0_darkRect(sc, lx, top, 2, hgt);
        e0_darkRect(sc, lx - 1, g - 3, 4, 3);
        e0_darkRect(sc, lx + (flip ? -4 : 1), top - 1, 5, 1);
        e0_darkRect(sc, hx - 2, top, 5, 2);
        e0_put(sc, hx - 1, top + 2); e0_put(sc, hx, top + 2); e0_put(sc, hx + 1, top + 2);
        e0_put(sc, hx, top + 3);
        e0_put(sc, hx - 1, top - 1);   // snow on the hood
        for (int y = top + 1; y < top + 8; ++y) e0_put(sc, lx + (flip ? 0 : 1), y);   // lamplight down the post
    };

    {   // Gran's house: the biggest shape in the street, one warm kitchen window, smoke
        int hx = (int)(W * 0.075f), w = 26, wall = 16, roof = 11;
        int top = g - wall;
        e0_darkRect(sc, hx + 18, top - 10, 3, 8);   // chimney, behind the roof
        e0_house(sc, hx, g, w, wall, roof, 0, 7, false, 8);
        e0_put(sc, hx + 18, top - 11); e0_put(sc, hx + 19, top - 11);
        for (int k = 0; k < 9; ++k) {   // smoke: puffs rising, drifting, thinning
            int sy = top - 13 - k * 2, sx = hx + 20 + (int)std::lround(std::sin(k * 0.8f) * 1.5f) + k / 2;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = -1; dx <= 1; ++dx) e0_glow(sc, sx + dx, sy + dy, std::max(3, 8 - k));
        }
        // the kitchen window: lit, a dark cross frame; the only warm light in the street
        int wx = hx + 3, wy = g - 12;
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 7; ++x) e0_set(sc, wx + x, wy + y, !(x == 3 || y == 2));
        e0_darkRect(sc, hx + 15, g - 9, 5, 9);                              // the door
        e0_put(sc, hx + 16, g - 8); e0_put(sc, hx + 18, g - 8);             // its little panes, lit
        e0_put(sc, hx + 15, top + 3); e0_put(sc, hx + 20, top + 3);         // snow on the sills
        // a picket fence along the front garden, a pixel of snow on some pickets
        for (int k = 0; k < 8; ++k) {
            int px = hx + w + 3 + k * 3;
            e0_darkRect(sc, px, g - 8, 2, 8);
            if (k % 3 == 0) e0_put(sc, px, g - 9);
        }
        e0_darkRect(sc, hx + w + 3, g - 6, 23, 1);
        e0_darkRect(sc, hx + w + 3, g - 3, 23, 1);
    }
    lamp((int)(W * 0.225f), 31, false);
    lamp((int)(W * 0.665f), 27, true);
    {   // a low hedge, bumpy, under a layer of snow
        int hx0 = (int)(W * 0.285f), hw = 16;
        for (int x = hx0; x < hx0 + hw; ++x) {
            float u = (x - hx0 + 0.5f) / hw;
            int top = g - 6 - (int)std::lround(2.0f * std::sin(u * 3.14159f)) - (((x - hx0) / 4) % 2);
            for (int y = top; y < g; ++y) e0_set(sc, x, y, y < top + 2 && ((x + y) & 1));
        }
    }
    {   // a car parked under a thick blanket of snow
        int cx = (int)(W * 0.455f), w = 24;
        for (int r = 0; r < 5; ++r) {   // the cabin, a trapezoid
            int y = g - 12 + r;
            for (int x = cx + 7 - r * 3 / 4; x <= cx + 16 + r * 3 / 4; ++x) e0_dark(sc, x, y);
        }
        for (int x = cx + 7; x <= cx + 16; ++x) e0_tone(sc, x, g - 13, 8);   // snow piled on the roof
        for (int x = cx + 8; x <= cx + 15; ++x) e0_tone(sc, x, g - 14, 8);
        for (int y = g - 7; y <= g - 2; ++y)   // the body, rounded at the corners
            for (int x = cx; x < cx + w; ++x) {
                bool corner = (x == cx || x == cx + w - 1) && (y == g - 7 || y == g - 2);
                if (!corner) e0_set(sc, x, y, y == g - 7 && (x < cx + 6 || x > cx + 17) && ((x + y) & 1));
            }
        for (int k = 0; k < 2; ++k) {   // wheels
            int wx = cx + (k ? 18 : 5);
            for (int y = g - 3; y < g; ++y)
                for (int x = wx - 2; x <= wx + 2; ++x)
                    if (!((x == wx - 2 || x == wx + 2) && y == g - 1)) e0_dark(sc, x, y);
        }
        e0_put(sc, cx + w - 1, g - 5);   // a tail light catching the lamp
    }
    {   // a bench with snow on its seat
        int bx = (int)(W * 0.585f);
        e0_darkRect(sc, bx, g - 5, 12, 1);
        e0_darkRect(sc, bx, g - 9, 12, 2);
        e0_darkRect(sc, bx + 1, g - 5, 1, 5); e0_darkRect(sc, bx + 10, g - 5, 1, 5);
        e0_darkRect(sc, bx, g - 9, 1, 4); e0_darkRect(sc, bx + 11, g - 9, 1, 4);
        for (int x = bx + 1; x <= bx + 10; x += 2) e0_put(sc, x, g - 6);
    }
    {   // a postbox with a snow cap
        int px = (int)(W * 0.745f);
        e0_darkRect(sc, px, g - 11, 5, 11);
        e0_darkRect(sc, px + 1, g - 12, 3, 1);
        e0_put(sc, px + 1, g - 13); e0_put(sc, px + 2, g - 13); e0_put(sc, px, g - 12);
        e0_put(sc, px + 1, g - 8);   // the slot catches the lamp
    }
    // near pines by the road: the tallest, darkest shapes in the scene
    e0_pine(sc, (int)(W * 0.825f), g, 36, 702, true, false);
    e0_pine(sc, (int)(W * 0.862f), g, 23, 703, true, true);
    e0_pine(sc, (int)(W * 0.965f), g, 29, 704, true, true);
    return true;
}
