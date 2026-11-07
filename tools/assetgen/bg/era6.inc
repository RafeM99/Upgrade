// Background scene for stage 6 (full 3D, 1951, "Home"), included into SpriteGen.cpp just before
// bgLayer(). The 3D stage builds its own trees and houses as models and only uses the sky
// (layer 0) and one far cut-out (layer 1, stood far back and hazed by Renderer::drawBackdrop3D).
// So: a fresh morning sky with stepped clouds and a pale sun, and far mountains in layers that
// fade lighter and bluer with distance over a band of pines and cluster-canopy trees.
// Uses the E5Grid / e5_ helpers from era5.inc, which is included just before this file.

// A range of peaked mountains: lit on the left face of each peak, shadowed on the right, with a
// few lighter gullies running down the lit faces. Peaks wrap on x so the layer tiles.
static void e6_mountains(E5Grid& g, float base, float height, int nPeaks, int seed, Col lit, Col shade, Col rim, Col gully)
{
    struct Peak { float x, y, slope; };
    std::vector<Peak> pk;
    for (int i = 0; i < nPeaks; ++i)
        pk.push_back({(i + 0.2f + 0.6f * rnd(i, seed, 601)) * g.w / nPeaks, base - height * (0.55f + 0.45f * rnd(i, seed, 602)),
                      0.55f + 0.35f * rnd(i, seed, 603)});
    for (int x = 0; x < g.w; ++x) {
        float best = 1e9f;
        int bi = 0;
        float bdx = 0;
        for (int i = 0; i < nPeaks; ++i)
            for (int k = -1; k <= 1; ++k) {   // wrapped copies
                float dx = x + 0.5f - (pk[i].x + k * g.w);
                float y = pk[i].y + std::fabs(dx) * pk[i].slope + (rnd(x / 2, i, seed + 604) < 0.3f ? 1 : 0);
                if (y < best) { best = y; bi = i; bdx = dx; }
            }
        int y0 = (int)std::lround(std::min(best, base + 2));
        bool litSide = bdx < 0;
        for (int y = y0; y < g.h; ++y) {
            Col c = litSide ? lit : shade;
            if (y == y0) c = litSide ? rim : lit;
            // gullies: diagonal light streaks down the lit faces, dark ones on the shadow side
            float depth = y - y0;
            if (depth > 2 && depth < 22) {
                int streak = (int)(bdx * 0.7f + depth * (litSide ? 0.5f : -0.5f));
                if (((streak % 6) + 6) % 6 == 0 && rnd(bi, (int)depth / 3, seed + 605) < 0.8f) c = litSide ? gully : mixc(shade, lit, 0.4f);
            }
            g.set(x, y, c);
        }
    }
}

bool bgEra6(Scene& sc, int layer)
{
    const int W = sc.W, H = sc.H;
    const int b = 4;
    if (layer >= 2) return true;   // the 3D stage has models here; leave these transparent
    E5Grid g(W / b, H / b);
    const int gw = g.w, gh = g.h;
    sc.fade = layer == 1 ? 0.08f : 0.0f;   // the haze shader does the depth fade in game

    if (layer == 0) {
        const uint32_t bands[7] = {0x3e7ad0, 0x5290dc, 0x68a4e4, 0x82b8ea, 0x9ecaee, 0xbcdaf0, 0xdcecf2};
        e5_sky(g, bands, 7, 0.8f, 2);
        e5_sun(g, gw * 0.24f, gh * 0.2f, 7.0f, hex(0xfffbe6), hex(0xfff2c0), hex(0xf8f4e0));
        const Col ct = hex(0xffffff), cm = hex(0xe4eef8), cu = hex(0xa8bcdc);
        e5_cloud(g, gw * 0.08f, gh * 0.3f, 5.0f, ct, cm, cu, 11);
        e5_cloud(g, gw * 0.42f, gh * 0.14f, 6.5f, ct, cm, cu, 12);
        e5_cloud(g, gw * 0.62f, gh * 0.34f, 4.0f, ct, cm, cu, 13);
        e5_cloud(g, gw * 0.84f, gh * 0.2f, 6.0f, ct, cm, cu, 14);
        e5_cloud(g, gw * 0.3f, gh * 0.44f, 3.0f, ct, cm, cu, 15);
        for (int i = 0; i < 3; ++i) g.rect((int)(gw * (0.5f + 0.13f * i)), (int)(gh * (0.48f + 0.04f * i)), 12 + 4 * i, 1, hex(0xeef6fa));
        g.blit(sc, b);
        return true;
    }

    // layer 1: three mountain ranges, far = lighter and bluer, then rolling hills and a forest band
    e6_mountains(g, gh * 0.62f, gh * 0.26f, 5, 1, hex(0xa8c4e4), hex(0x90acd4), hex(0xc8dcf0), hex(0xbcd4ec));
    e6_mountains(g, gh * 0.71f, gh * 0.22f, 7, 2, hex(0x7ea4d0), hex(0x6488bc), hex(0xa4c4e4), hex(0x96b8de));
    e6_mountains(g, gh * 0.79f, gh * 0.15f, 9, 3, hex(0x5e8cb4), hex(0x46709e), hex(0x84acd0), hex(0x74a0c8));
    e5_ridge(g, gh * 0.83f, 5, 2.0f, 5, hex(0x4a8a6a), hex(0x6aa46a), hex(0x8cc078));
    // the forest band: pines and cluster-canopy broadleaves, cool morning greens
    const E5Pal pine = e5_pal(0x5a9a6a, 0x3a7a5e, 0x1e4e52);
    const E5Pal broad = e5_pal(0x9cc858, 0x5a9a48, 0x2a6050);
    int base = gh - 1;
    for (int i = 0; i < 60; ++i) {
        int x = (int)((i + rnd(i, 1, 610) * 0.7f) * gw / 60);
        if (i % 3 != 1) e5_pine(g, x, base - (int)(rnd(i, 2, 610) * 3), 9 + (int)(rnd(i, 3, 610) * 9), pine, hex(0x3a2a2a), i);
    }
    for (int i = 0; i < 26; ++i) {
        int x = (int)((i + rnd(i, 4, 611)) * gw / 26);
        float sz = 3.0f + 2.0f * rnd(i, 5, 611);
        e5_canopy(g, (float)x, base - sz * 0.8f, sz * 1.3f, sz * 1.05f, broad, i + 300, 5);
    }
    for (int x = 0; x < gw; ++x) g.rect(x, gh - 3, 1, 3, broad.dark);   // solid foot, meets the ground
    g.blit(sc, b);
    return true;
}
