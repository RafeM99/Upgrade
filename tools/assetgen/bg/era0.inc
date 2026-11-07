// Background scene for stage 0, included into SpriteGen.cpp just before bgLayer().
// 2025, "The night she got lost": a quiet winter street at night, in two colours.
// Gran and Sam are solid white figures, so everything here recedes: dotted outlines only, almost
// no fill. The composition still follows the reference style: layered depth (far ridge and a row
// of houses, mid trees, near street), and trees with recognisable cluster-built crowns, drawn as
// stepped dotted outlines (round, pine, forked).

static void e0_put(Scene& sc, int x, int y)
{
    if (y < 0 || y >= sc.H) return;
    sc.p.set(x, y, ONEBIT_LIGHT);   // Px::set wraps x, so everything tiles
}
// A dotted line (Bresenham, every `gap`-th step lit).
static void e0_line(Scene& sc, int x0, int y0, int x1, int y1, int gap = 2)
{
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, n = 0;
    for (;;) {
        if (n++ % gap == 0) e0_put(sc, x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

struct E0Blob { float x, y, r; };

// Outline of a crown built from clumps, stepped on a 2-px grid like the reference trees, drawn
// dotted. Inner clump edges (the top of a lower clump in front) get a sparser dotted arc, so the
// crown reads as clusters without any fill.
static void e0_crown(Scene& sc, const std::vector<E0Blob>& bl, int seed, bool inner)
{
    if (bl.empty()) return;
    const int b = 2;
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (auto& q : bl) {
        minx = std::min(minx, q.x - q.r); maxx = std::max(maxx, q.x + q.r);
        miny = std::min(miny, q.y - q.r); maxy = std::max(maxy, q.y + q.r);
    }
    int gx0 = (int)std::floor(minx / b) - 1, gx1 = (int)std::ceil(maxx / b) + 1;
    int gy0 = (int)std::floor(miny / b) - 1, gy1 = (int)std::ceil(maxy / b) + 1;
    int gw = gx1 - gx0 + 1, gh = gy1 - gy0 + 1;
    std::vector<int> who((size_t)gw * gh, -1);
    for (int gy = 0; gy < gh; ++gy)
        for (int gx = 0; gx < gw; ++gx) {
            float mx = (gx + gx0 + 0.5f) * b, my = (gy + gy0 + 0.5f) * b;
            for (int k = (int)bl.size() - 1; k >= 0; --k) {
                float j = 1.0f + 0.1f * (rnd(gx + gx0, gy + gy0, seed + k) - 0.5f);
                float dx = mx - bl[k].x, dy = (my - bl[k].y) * 1.08f, r = bl[k].r * j;
                if (dx * dx + dy * dy < r * r) { who[(size_t)gy * gw + gx] = k; break; }
            }
        }
    auto cell = [&](int px, int py) {   // pixel -> clump index (or -1)
        int gx = (int)std::floor((float)px / b) - gx0, gy = (int)std::floor((float)py / b) - gy0;
        if (gx < 0 || gy < 0 || gx >= gw || gy >= gh) return -1;
        return who[(size_t)gy * gw + gx];
    };
    for (int py = gy0 * b; py < (gy1 + 1) * b; ++py)
        for (int px = gx0 * b; px < (gx1 + 1) * b; ++px) {
            int k = cell(px, py);
            if (k < 0) continue;
            sc.p.set(px, py, ONEBIT_DARK);   // the crown hides what is behind it (dark = sky)
            bool edge = cell(px - 1, py) < 0 || cell(px + 1, py) < 0 || cell(px, py - 1) < 0 || cell(px, py + 1) < 0;
            if (edge) {
                if (((px + py) & 1) == 0) e0_put(sc, px, py);
            } else if (inner) {
                int u = cell(px, py - 1);
                // the top edge of a clump that sits in front, only on its sunny (upper left) half
                if (u >= 0 && u < k && px < bl[k].x + bl[k].r * 0.2f && (px % 3) == 0) e0_put(sc, px, py);
            }
        }
}

// Trees: 0 round, 1 pine (stepped tiers), 2 forked (two crowns), 3 tall oval. R = crown radius.
static void e0_tree(Scene& sc, int x, int base, int kind, float R, int seed, bool inner)
{
    std::vector<E0Blob> bl;
    auto crown = [&](float cx, float cy, float rx, float ry) {
        std::vector<E0Blob> c;
        c.push_back({cx, cy, std::min(rx, ry) * 0.65f});
        int n = ry > rx * 1.2f ? 8 : 6;
        for (int i = 0; i < n; ++i) {
            float a = i * 6.2831853f / n + rnd(i, seed, 61) * 0.6f;
            float rr = std::min(rx, ry) * (0.42f + 0.14f * rnd(i, seed, 62));
            c.push_back({cx + std::cos(a) * (rx - rr * 0.95f), cy + std::sin(a) * (ry - rr * 0.95f), rr});
        }
        std::sort(c.begin() + 1, c.end(), [](const E0Blob& p, const E0Blob& q) { return p.y < q.y; });
        for (auto& q : c) bl.push_back(q);
    };
    if (kind == 1) {   // pine: stepped tiers, outline only
        int th = std::max(3, (int)(R * 0.5f));
        e0_line(sc, x, base, x, base - th);
        int tiers = 3 + (R > 8 ? 1 : 0);
        float top = base - th - R * 2.4f, tierH = R * 2.4f / tiers;
        for (int t = 0; t < tiers; ++t) {
            int y0 = (int)(top + t * tierH * 0.8f), y1 = (int)(y0 + tierH * 1.3f);
            int half = (int)std::lround(R * (0.4f + 0.6f * (t + 1) / tiers));
            int hs = std::max(1, half / 3);
            for (int y = y0; y <= y1; ++y) {   // hide what is behind the tier
                int h = (int)std::lround((float)half * (y - y0) / std::max(1, y1 - y0));
                for (int xx = -h; xx <= h; ++xx) sc.p.set(x + xx, y, ONEBIT_DARK);
            }
            // stepped slopes down each side, then the tier's flat bottom
            for (int y = y0; y <= y1; y += 2) {
                int h = std::max(1, (int)std::lround((float)half * (y - y0) / std::max(1, y1 - y0)) / 2 * 2);
                e0_put(sc, x - h, y);
                e0_put(sc, x + h, y);
            }
            e0_line(sc, x - half, y1, x - hs, y1);
            e0_line(sc, x + hs, y1, x + half, y1);
        }
        e0_put(sc, x, (int)top - 2);
        return;
    }
    if (kind == 2) {   // forked
        int fy = base - (int)(R * 0.9f);
        e0_line(sc, x, base, x, fy);
        int ax = x - (int)(R * 0.8f), ay = fy - (int)(R * 0.8f), bx = x + (int)(R * 0.9f), by = fy - (int)(R * 0.5f);
        e0_line(sc, x, fy, ax, ay);
        e0_line(sc, x, fy, bx, by);
        crown((float)bx, by - R * 0.5f, R * 0.7f, R * 0.65f);
        e0_crown(sc, bl, seed + 1, inner);
        bl.clear();
        crown((float)ax, ay - R * 0.55f, R * 0.85f, R * 0.8f);
        e0_crown(sc, bl, seed, inner);
        return;
    }
    float rx = R, ry = kind == 3 ? R * 1.4f : R * 0.92f;
    float cy = base - R * 0.7f - ry;
    e0_line(sc, x, base, x, (int)(cy + ry * 0.6f));
    crown((float)x, cy, rx, ry);
    e0_crown(sc, bl, seed, inner);
}

bool bgEra0(Scene& sc, int layer)
{
    const int W = sc.W, H = sc.H;
    sc.fade = 0;   // two colours exactly; depth comes from size and how sparse the dots are

    if (layer == 0) {   // the night sky: a few stars, a thin crescent moon
        sc.rect(0, 0, (float)W, (float)H, ONEBIT_DARK);
        const int stars[11][2] = {{12, 8}, {44, 19}, {71, 5}, {118, 14}, {150, 11}, {176, 26}, {190, 4}, {231, 7}, {259, 20}, {290, 15}, {303, 3}};
        for (auto& st : stars) e0_put(sc, st[0] * W / 313, st[1]);
        sc.disc(W * 0.3f, 14, 6, 6, ONEBIT_LIGHT, ONEBIT_LIGHT, false);
        sc.disc(W * 0.3f + 3, 12, 6, 6, ONEBIT_DARK, ONEBIT_DARK, false);
        return true;
    }

    if (layer == 1) {   // far: a gentle ridge with tiny pines, and a row of houses with a few lit windows
        auto ridgeY = [&](int x) { return (int)std::lround(H * 0.44f + ridge((float)x, W, 3, 3.5f, 1.5f, 0)); };
        for (int x = 0; x < W; x += 2) e0_put(sc, x, ridgeY(x));
        // tiny far pines on the first ridge
        const float pines[7] = {0.04f, 0.08f, 0.22f, 0.25f, 0.66f, 0.7f, 0.95f};
        for (float f : pines) {
            int x = (int)(W * f), y = ridgeY(x);
            for (int k = 0; k < 6; k += 2) { e0_put(sc, x - k / 2, y - 6 + k); e0_put(sc, x + k / 2, y - 6 + k); }
            e0_put(sc, x, y - 7);
        }
        // the far row of houses: gable roofs only, chimneys, a few windows still lit
        int base = (int)(H * 0.62f);
        float hx = W * 0.32f;
        int i = 0;
        while (hx < W * 0.9f) {
            if (hx > W * 0.58f && hx < W * 0.74f) { hx = W * 0.74f; continue; }   // a gap in the row
            int w = 8 + (int)(rnd(i, 1, 5) * 5) / 2 * 2, wall = 4 + (int)(rnd(i, 2, 5) * 3), roof = w / 2;
            int x0 = (int)hx, top = base - wall;
            sc.rect((float)x0, (float)top, (float)w + 1, (float)wall + 1, ONEBIT_DARK);   // hide the ridge behind
            for (int k = 0; k < roof; ++k) sc.rect((float)x0 + k, (float)top - k, (float)w + 1 - 2 * k, 1, ONEBIT_DARK);
            e0_line(sc, x0, top, x0 + w / 2, top - roof);
            e0_line(sc, x0 + w, top, x0 + w / 2, top - roof);
            e0_line(sc, x0, top + 2, x0, base, 2);
            if (rnd(i, 3, 5) < 0.5f) { e0_put(sc, x0 + w - 3, top - roof / 2 - 2); e0_put(sc, x0 + w - 3, top - roof / 2); }
            if (rnd(i, 4, 5) < 0.45f) e0_put(sc, x0 + 2 + (int)(rnd(i, 6, 5) * (w - 4)), top + 2);
            hx += w + 3 + (int)(rnd(i, 7, 5) * 4);
            ++i;
        }
        return true;
    }

    if (layer == 2) {   // mid: a few trees in outline and falling snow
        int g = (int)(H * 0.74f);
        e0_tree(sc, (int)(W * 0.05f), g, 0, 11, 1, false);
        e0_tree(sc, (int)(W * 0.27f), g, 1, 10, 2, false);
        e0_tree(sc, (int)(W * 0.55f), g, 3, 9, 4, false);
        e0_tree(sc, (int)(W * 0.73f), g, 2, 10, 5, false);
        // low snow banks under the trees: short dotted humps
        const float banks[4] = {0.05f, 0.27f, 0.55f, 0.73f};
        for (float f : banks) {
            int x = (int)(W * f);
            for (int k = -8; k <= 8; k += 2) e0_put(sc, x + k, g + (std::abs(k) > 5 ? 1 : 0));
        }
        // falling snow: sparse single dots, kept out of the lowest band
        for (int k = 0; k < 18; ++k) {
            int x = (int)(rnd(k, 1, 404) * W), y = 4 + (int)(rnd(k, 2, 404) * H * 0.62f);
            e0_put(sc, x, y);
        }
        return true;
    }

    // near: the street. Gran's house with one warm window, a street lamp, trees, snow.
    int g = (int)(H * 0.84f);
    {   // Gran's house, a dotted outline; the kitchen window is the only lit thing in the street
        int hx = (int)(W * 0.13f);
        sc.rect((float)hx, (float)g - 14, 21, 15, ONEBIT_DARK);
        for (int k = 0; k < 10; ++k) sc.rect((float)hx + k, (float)g - 14 - k * 0.75f, (float)21 - 2 * k, 1, ONEBIT_DARK);
        e0_line(sc, hx, g - 14, hx + 20, g - 14);
        e0_line(sc, hx, g - 14, hx, g);
        e0_line(sc, hx + 20, g - 14, hx + 20, g);
        e0_line(sc, hx - 2, g - 14, hx + 10, g - 23);
        e0_line(sc, hx + 22, g - 14, hx + 10, g - 23);
        e0_line(sc, hx + 15, g - 19, hx + 15, g - 23);   // chimney
        e0_line(sc, hx + 17, g - 20, hx + 17, g - 23);
        e0_line(sc, hx + 12, g - 8, hx + 16, g - 8);      // door
        e0_line(sc, hx + 12, g - 8, hx + 12, g);
        e0_line(sc, hx + 16, g - 8, hx + 16, g);
        sc.rect((float)hx + 4, (float)g - 10, 3, 3, ONEBIT_LIGHT);   // the lit window, small and solid
        // snow on the roof edge: a few dots
        for (int k = 0; k < 4; ++k) e0_put(sc, hx + 1 + k * 3, g - 16 - k * 2);
    }
    {   // a street lamp: thin pole, hood, a small lamp, and a sparse ring of light on the snow
        int lx = (int)(W * 0.64f), top = g - 30;
        for (int y = top; y <= g; ++y) if ((y & 1) == 0 || y > g - 3) e0_put(sc, lx, y);
        e0_line(sc, lx, top, lx + 5, top, 1);
        e0_line(sc, lx + 3, top + 1, lx + 7, top + 1, 2);
        sc.rect((float)lx + 4, (float)top + 2, 2, 1, ONEBIT_LIGHT);
        for (int k = 1; k < 5; ++k) {   // falling light: a few dots spreading out, fading
            int y = top + 3 + k * 5;
            e0_put(sc, lx + 5 - k * 2, y);
            e0_put(sc, lx + 5 + k * 2, y);
        }
        for (int k = -9; k <= 9; k += 3) e0_put(sc, lx + 5 + k, g - 1 + (std::abs(k) > 6 ? 0 : 1) - 1);
    }
    e0_tree(sc, (int)(W * 0.42f), g, 2, 15, 11, true);
    e0_tree(sc, (int)(W * 0.83f), g, 1, 15, 12, false);
    e0_tree(sc, (int)(W * 0.97f), g, 0, 16, 13, true);
    for (int k = 0; k < 10; ++k) {   // a few nearer flakes, up in the sky band
        int x = (int)(rnd(k, 1, 505) * W), y = 3 + (int)(rnd(k, 2, 505) * H * 0.45f);
        e0_put(sc, x, y);
    }
    return true;
}
