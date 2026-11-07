// Background scene for stage 0, included into SpriteGen.cpp just before bgLayer().
// "Last winter": a winter night street, in exactly two colours.
// Gran and Sam (and the enemies and platforms) are solid white with a black halo. They need a
// black backdrop to keep that outline, so the band where they run (the bottom four tiles above
// the ground line) is kept almost all black:
//   - scenery there is solid black silhouettes, each edged only by a single solid white line of
//     snow along its top surfaces (roof slopes, pine tiers, the car, the hedge);
//   - the snowfield is just its horizon: one rolling snow line, broken by everything in front;
//   - the only dense dither (checker) is up in the sky band: the snow caps of the mountains.
// Depth: far mountains carry flat dither and no outlines, the far town only lit windows, the
// mid pines and near street the solid snow edges. A crescent moon hangs below the HUD.

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

// The top of the snowfield at column x: a gentle wavy line that tiles across the width.
static int e0_contour(const Scene& sc, int x, float base, int seed, float a1, float a2)
{
    return (int)std::lround(base + ridge((float)x, sc.W, seed, a1, a2, 0));
}

// A snow-edged silhouette: every pixel inside is black, and the top pixel of each column run
// (the surface snow settles on) is solid white; with `sides` the side edges are lit too, so
// steep shapes keep an unbroken rim. inside(x, y) decides the shape.
// Flat tops longer than `maxFlat` pixels get a gap now and then, so no rim reads as a platform.
template <class F>
static void e0_solid(Scene& sc, int x0, int y0, int x1, int y1, F inside, bool rim = true, int maxFlat = 7, bool sides = false)
{
    int run = 0;
    for (int y = y0; y <= y1; ++y) {
        run = 0;
        for (int x = x0; x <= x1; ++x) {
            if (!inside(x, y)) { run = 0; continue; }
            bool top = rim && !inside(x, y - 1);
            // a flat stretch: the rim runs on along the same row
            if (top && inside(x - 1, y) && !inside(x - 1, y - 1)) ++run; else run = 0;
            if (top && run >= maxFlat && run % (maxFlat + 1) == maxFlat) top = false;
            bool side = rim && sides && (!inside(x - 1, y) || !inside(x + 1, y)) && inside(x, y + 1);
            e0_set(sc, x, y, top || side);
        }
    }
}

// A pine: a cone of drooping tiers, black, with snow on every tier's shoulders (`snow`).
static void e0_pine(Scene& sc, int x, int base, float h, int seed, bool snow, bool flip)
{
    int trunk = std::max(2, (int)(h * 0.12f));
    int tiers = h > 26 ? 5 : h > 15 ? 4 : 3;
    float top = base - h, bodyH = h - trunk;
    float tierH = bodyH / (tiers * 0.72f + 0.28f);
    float maxHalf = h * 0.27f;
    int tw = h > 20 ? 2 : 1;
    auto inside = [&](int px, int py) {
        int dx = px - x;
        if (flip) dx = -dx;
        if (py > base) return false;
        if (py > base - trunk) return dx >= -tw / 2 && dx < tw - tw / 2;
        if (py == (int)std::lround(top) - 1) return dx == 0;   // the leader
        for (int t = 0; t < tiers; ++t) {
            float y0 = top + t * tierH * 0.72f, y1 = y0 + tierH;
            int iy0 = (int)std::lround(y0), iy1 = (int)std::lround(y1);
            if (py < iy0 || py > iy1) continue;
            float half = maxHalf * (0.3f + 0.7f * (t + 1) / tiers);
            float fr = (py - y0) / std::max(1.0f, y1 - y0);
            int hw = (int)std::lround(half * std::min(1.0f, fr * 1.05f));
            if (py == iy1 && std::abs(dx) < hw - 1 && rnd(dx, t, seed) < 0.35f) continue;   // ragged hem
            if (std::abs(dx) <= hw) return true;
        }
        return false;
    };
    e0_solid(sc, x - (int)maxHalf - 2, (int)top - 2, x + (int)maxHalf + 2, base - trunk, inside, snow, 7, true);
    e0_solid(sc, x - 2, base - trunk + 1, x + 2, base, inside, false);
}

// A gable house: a black silhouette whose roof slopes and chimney carry a snow line, with lit
// windows as single solid lights. litMask picks which window slots are lit (one bit per slot).
static void e0_house(Scene& sc, int x, int base, int w, int wall, int roof, int litMask, int seed, bool chimney, bool snow)
{
    int top = base - wall;
    int cx = x + w - 3 - (int)(rnd(seed, 1, 3) * 2);
    auto inside = [&](int px, int py) {
        int u = px - x;
        if (py > base) return false;
        if (u >= 0 && u < w && py >= top) return true;
        if (chimney && u >= cx - x && u <= cx - x + 1 && py >= top - roof && py < top) return true;
        // the roof: a one-pixel eave each side, rising `roof` rows to the ridge
        if (py < top - roof || py >= top) return false;
        int r = top - py;
        int inset = (int)std::lround((float)r * (w / 2 + 1) / std::max(1, roof));
        return u >= -1 + inset && u <= w - inset;
    };
    e0_solid(sc, x - 2, top - roof - 1, x + w + 1, base, inside, snow);
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
    const float fieldBase = 58;   // the horizon of the snowfield
    auto fieldTop = [&](int x) {   // rolling, so it reads as a hill line and never as a platform
        float t = x * 6.2831853f / W;
        return (int)std::lround(fieldBase + 2.6f * std::sin(t * 7 + 0.6f) + 1.8f * std::sin(t * 3 + 2.1f) + 1.0f * std::sin(t * 13));
    };

    if (layer == 0) {   // the night sky: black, stars, a crescent moon below the HUD
        sc.rect(0, 0, (float)W, (float)H, ONEBIT_DARK);
        for (int k = 0; k < 42; ++k) {   // stars thin out toward the horizon
            float r = rnd(k, 2, 77);
            e0_put(sc, (int)(rnd(k, 1, 77) * W), 2 + (int)(r * r * 40));
        }
        float mx = W * 0.3f, my = 21;   // where the old scene had it, lit from the left
        for (int y = (int)my - 7; y <= (int)my + 7; ++y)
            for (int x = (int)mx - 7; x <= (int)mx + 7; ++x) {
                float dx = x + 0.5f - mx, dy = y + 0.5f - my, ex = dx - 2.8f, ey = dy + 2.0f;
                if (dx * dx + dy * dy < 6.3f * 6.3f) e0_set(sc, x, y, ex * ex + ey * ey > 5.6f * 5.6f);
            }
        return true;
    }

    if (layer == 1) {   // far: mountains up in the sky band, the snowfield's horizon, the town
        struct Peak { float x, top, slope; };
        const Peak pk[] = {{0.10f, 30, 0.70f}, {0.27f, 38, 0.80f}, {0.45f, 27, 0.62f}, {0.61f, 39, 0.85f},
                           {0.79f, 33, 0.72f}, {0.94f, 40, 0.80f}};
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
            for (int y = std::max(0, (int)bestTop); y < fieldBase + 6; ++y) {
                float below = y - pk[best].top;
                bool sunny = bdx < below * 0.15f;
                // the moonlit face: a checker snow cap torn into gullies, a fainter slope under
                // it, and black below row 42, so the characters' band stays dark
                int capLine = 7 + (int)(5 * std::fabs(std::sin(bdx * 0.45f + best)));
                int lvl = 0;
                if (sunny && y < 42) lvl = below < capLine ? 8 : 4;
                // the ridge itself: one solid snow line, fading out above the characters' band
                bool ridgeLine = y == (int)bestTop && y < 46;
                e0_set(sc, x, y, ridgeLine || e0::lit(x, y, lvl));
            }
        }
        // the snowfield: only its horizon, one solid snow line; black below it
        for (int x = 0; x < W; ++x) {
            int t = fieldTop(x), tp = fieldTop(x - 1);
            for (int y = std::min(t, tp + 1); y <= t; ++y) e0_put(sc, x, y);   // no gaps on slopes
            for (int y = t + 1; y < H; ++y) e0_dark(sc, x, y);
        }
        // the sleeping town on the horizon: black houses, no outline, a few lit windows
        struct Town { float x; int n; int seed; };
        const Town towns[] = {{0.03f, 4, 11}, {0.36f, 5, 12}, {0.73f, 3, 13}};
        for (const Town& t : towns) {
            float hx = t.x * W;
            for (int i = 0; i < t.n; ++i) {
                int w = 5 + (int)(rnd(i, t.seed, 1) * 4);
                int wall = 3 + (int)(rnd(i, t.seed, 2) * 3);
                int roof = 2 + (int)(rnd(i, t.seed, 3) * 2);
                int base = fieldTop((int)hx + w / 2) + 2;
                int mask = rnd(i, t.seed, 4) < 0.75f ? (rnd(i, t.seed, 5) < 0.5f ? 1 : 2) : 0;
                e0_house(sc, (int)hx, base, w, wall, roof, mask, t.seed * 7 + i, rnd(i, t.seed, 7) < 0.6f, false);
                hx += w + 2 + (int)(rnd(i, t.seed, 8) * 2);
                if (t.seed == 12 && i == 1) {   // the church: a nave and a steeple with a lit belfry
                    int cx = (int)hx, cb = fieldTop(cx + 6) + 2;
                    e0_house(sc, cx, cb, 9, 4, 3, 0, 99, false, false);
                    e0_darkRect(sc, cx + 9, cb - 9, 4, 10);
                    for (int r = 0; r < 6; ++r)
                        for (int dx = -(2 - r / 2); dx <= 2 - r / 2; ++dx) e0_dark(sc, cx + 11 + dx, cb - 10 - r);
                    e0_dark(sc, cx + 11, cb - 16);
                    e0_put(sc, cx + 10, cb - 7);
                    hx += 15;
                }
            }
        }
        return true;
    }

    if (layer == 2) {   // mid: clumps of pines on the snowfield, snow on their shoulders
        struct T { float x; float h; int sink; bool flip; };
        const T trees[] = {{0.020f, 15, 4, false}, {0.040f, 10, 3, true},  {0.058f, 13, 5, false},
                           {0.180f, 17, 5, true},  {0.200f, 11, 4, false},
                           {0.330f, 14, 5, false}, {0.346f, 9, 4, true},
                           {0.470f, 16, 6, true},  {0.490f, 19, 5, false}, {0.511f, 11, 4, false},
                           {0.640f, 14, 5, true},
                           {0.735f, 17, 5, false}, {0.912f, 13, 4, true}};   // kept clear of the near pines
        int i = 0;
        for (const T& t : trees) {
            int x = (int)(t.x * W);
            e0_pine(sc, x, fieldTop(x) + t.sink, t.h, 200 + i * 17, true, t.flip);
            ++i;
        }
        return true;
    }

    // near: the street. g is the ground line, level with the top of the ground tiles.
    const int g = 72;
    // behind the ground tiles (seen through the gaps): dark, so a gap reads as a gap
    for (int y = g; y < H; ++y)
        for (int x = 0; x < W; ++x) e0_dark(sc, x, y);

    // a street lamp: post, arm, hood, the lamp, a little light down the post
    auto lamp = [&](int lx, int hgt, bool flip) {
        int top = g - hgt, f = flip ? -1 : 1;
        int hx = lx + f * 5;
        e0_darkRect(sc, lx, top, 2, hgt);
        e0_darkRect(sc, lx - 1, g - 3, 4, 3);
        e0_darkRect(sc, lx + (flip ? -4 : 1), top - 1, 5, 1);
        e0_darkRect(sc, hx - 2, top, 5, 2);
        e0_put(sc, hx - 1, top + 2); e0_put(sc, hx, top + 2); e0_put(sc, hx + 1, top + 2);
        e0_put(sc, hx, top + 3);
        for (int x = hx - 2; x <= hx + 2; ++x) e0_put(sc, x, top - 1);   // snow on the hood
        e0_put(sc, lx + (flip ? 1 : 0), top - 1);
        for (int y = top + 1; y < top + 7; ++y) e0_put(sc, lx + (flip ? 0 : 1), y);   // lamplight on the post
    };

    {   // Gran's house: the biggest shape in the street, one warm kitchen window, smoke
        int hx = (int)(W * 0.075f), w = 26, wall = 16, roof = 11;
        int top = g - wall;
        e0_darkRect(sc, hx + 18, top - 10, 3, 8);   // chimney, behind the roof
        for (int x = hx + 18; x <= hx + 20; ++x) e0_put(sc, x, top - 11);
        e0_house(sc, hx, g, w, wall, roof, 0, 7, false, true);
        for (int k = 0; k < 9; ++k) {   // smoke: sparse puffs rising, drifting, thinning
            int sy = top - 13 - k * 2, sx = hx + 20 + (int)std::lround(std::sin(k * 0.8f) * 1.5f) + k / 2;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = -1; dx <= 1; ++dx) e0_glow(sc, sx + dx, sy + dy, std::max(2, 6 - k));
        }
        // the kitchen window: lit, a dark cross frame; the only warm light in the street
        int wx = hx + 3, wy = g - 12;
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 7; ++x) e0_set(sc, wx + x, wy + y, !(x == 3 || y == 2));
        e0_put(sc, hx + 16, g - 8); e0_put(sc, hx + 18, g - 8);   // the door's little panes, lit
        e0_put(sc, hx + 15, top + 3); e0_put(sc, hx + 20, top + 3); // snow on the sills
        // a picket fence along the front garden, snow on each picket's point
        for (int k = 0; k < 8; ++k) {
            int px = hx + w + 3 + k * 3;
            e0_darkRect(sc, px, g - 8, 2, 8);
            e0_put(sc, px, g - 9);
        }
        e0_darkRect(sc, hx + w + 3, g - 6, 23, 1);
        e0_darkRect(sc, hx + w + 3, g - 3, 23, 1);
    }
    lamp((int)(W * 0.225f), 31, false);
    lamp((int)(W * 0.665f), 27, true);
    {   // a low hedge, bumpy, with snow along its top
        int hx0 = (int)(W * 0.285f), hw = 16;
        auto inside = [&](int px, int py) {
            int u = px - hx0;
            if (u < 0 || u >= hw || py > g) return false;
            float f = (u + 0.5f) / hw;
            int top = g - 6 - (int)std::lround(2.0f * std::sin(f * 3.14159f)) - ((u / 4) % 2);
            return py >= top;
        };
        e0_solid(sc, hx0 - 1, g - 12, hx0 + hw, g, inside, true, 5);
    }
    {   // a car parked under snow: rounded roof, bonnet and boot, all edged with snow
        int cx = (int)(W * 0.455f), w = 24;
        auto inside = [&](int px, int py) {
            int u = px - cx;
            if (py > g || u < 0 || u >= w) return false;
            if (py >= g - 3) {   // wheels and the sill between them
                int a = u - 5, b = u - 18;
                if (std::abs(a) <= 2 || std::abs(b) <= 2) return !(py == g && (std::abs(a) == 2 || std::abs(b) == 2));
                return py == g - 3;
            }
            if (py >= g - 7) {   // the body: bonnet and boot slope down to the bumpers
                int end = std::min(u, w - 1 - u);
                return py >= g - 7 + (end < 1 ? 2 : end < 4 ? 1 : 0);
            }
            int r = py - (g - 13);   // the cabin: a rounded trapezoid
            if (r < 0) return false;
            int l = 8 - r * 3 / 4 + (r == 0 ? 1 : 0), rr = 15 + r * 3 / 4 - (r == 0 ? 1 : 0);
            return u >= l && u <= rr;
        };
        e0_solid(sc, cx - 1, g - 15, cx + w, g, inside, true, 5);
        e0_put(sc, cx + w - 1, g - 5);   // a tail light catching the lamp
    }
    {   // a bench: snow on the back rail and in a few drifts on the seat
        int bx = (int)(W * 0.585f);
        e0_darkRect(sc, bx, g - 5, 12, 1);
        e0_darkRect(sc, bx, g - 9, 12, 2);
        e0_darkRect(sc, bx + 1, g - 5, 1, 5); e0_darkRect(sc, bx + 10, g - 5, 1, 5);
        e0_darkRect(sc, bx, g - 9, 1, 4); e0_darkRect(sc, bx + 11, g - 9, 1, 4);
        for (int x = bx; x <= bx + 11; ++x) if ((x - bx) % 6 != 5) e0_put(sc, x, g - 10);
        for (int x = bx + 2; x <= bx + 4; ++x) e0_put(sc, x, g - 6);
        for (int x = bx + 7; x <= bx + 8; ++x) e0_put(sc, x, g - 6);
    }
    {   // a postbox with a snow cap
        int px = (int)(W * 0.745f);
        e0_darkRect(sc, px, g - 11, 5, 11);
        e0_darkRect(sc, px + 1, g - 12, 3, 1);
        e0_put(sc, px + 1, g - 13); e0_put(sc, px + 2, g - 13); e0_put(sc, px + 3, g - 13);
        e0_put(sc, px, g - 12); e0_put(sc, px + 4, g - 12);
        e0_put(sc, px + 1, g - 8);   // the slot catches the lamp
    }
    // near pines by the road: the tallest shapes in the scene
    e0_pine(sc, (int)(W * 0.825f), g, 36, 702, true, false);
    e0_pine(sc, (int)(W * 0.862f), g, 23, 703, true, true);
    e0_pine(sc, (int)(W * 0.965f), g, 29, 704, true, true);
    return true;
}
