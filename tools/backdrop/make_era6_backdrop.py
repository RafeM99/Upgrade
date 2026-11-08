#!/usr/bin/env python3
"""Renders the 3D stage's (era 6) backdrop: assets/art/era6/bg_0.png (sky) and bg_1.png (the
mountains, foothills and farmland strip that stands at z -100). Replaces the stepped pixel art:
smooth, anti-aliased (rendered 2x and averaged), tiling sideways, 2560x720 like the old 1280x360
at double density. Everything is procedural from a fixed seed.
  sky:   smooth vertical gradient (dithered, no banding), soft sun glow, soft fBM clouds
  strip: three ridges: ridged-multifractal alpine peaks (uneven, asymmetric, snow by height,
         rock by slope), fBM forest foothills, farmland with crop rows, hedgerows, distant trees.
         Each fades to the horizon colour with a vertical haze gradient (lighter at the base).
Run:  python3 -I tools/backdrop/make_era6_backdrop.py
"""
import os
import numpy as np
from PIL import Image

W, H, SS = 2560, 720, 1   # edges are anti-aliased analytically (coverage), so no supersampling
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "assets", "art", "era6")
HORIZON = np.array([0.86, 0.92, 0.97])
rng = np.random.default_rng(66)


def smooth(a):
    return a * a * (3 - 2 * a)


def vnoise(x, y, fx, fy, seed):
    """Value noise, periodic in x over the strip's width (fx lattice cells across it)."""
    r = np.random.default_rng(seed)
    gx, gy = int(fx), int(fy) + 2
    grid = r.random((gy + 1, gx))
    u = x / W * gx
    v = y * fy
    i0 = np.floor(u).astype(int); j0 = np.floor(v).astype(int)
    fu = smooth(u - i0); fv = smooth(v - j0)
    i0 %= gx; i1 = (i0 + 1) % gx
    j0 = np.clip(j0, 0, gy - 1); j1 = np.clip(j0 + 1, 0, gy)
    a = grid[j0, i0] * (1 - fu) + grid[j0, i1] * fu
    b = grid[j1, i0] * (1 - fu) + grid[j1, i1] * fu
    return a * (1 - fv) + b * fv


def fbm(x, y, fx, fy, oct_, seed):
    s = 0; amp = 1; tot = 0
    for o in range(oct_):
        s = s + amp * vnoise(x, y, fx * 2 ** o, fy * 2 ** o, seed + o * 7)
        tot += amp; amp *= 0.5
    return s / tot


def mix(a, b, t):
    return a + (b - a) * t[..., None]


def sky():
    w, h = W * SS, H * SS
    x = (np.arange(w)[None, :] + 0.5) / SS
    y = (np.arange(h)[:, None] + 0.5) / h
    xx = np.broadcast_to(x, (h, w)); yy = np.broadcast_to(y, (h, w))
    top, mid, hor = np.array([0.27, 0.52, 0.86]), np.array([0.50, 0.73, 0.94]), HORIZON
    t = np.clip(yy / 0.5, 0, 1)
    img = mix(np.broadcast_to(top, (h, w, 3)), np.broadcast_to(mid, (h, w, 3)), smooth(np.clip(t * 1.6, 0, 1)))
    img = mix(img, np.broadcast_to(hor, (h, w, 3)), smooth(np.clip((t - 0.45) / 0.55, 0, 1)) ** 1.3)
    # soft clouds: fBM thresholded, thicker in a band, lit on top and bluish underneath
    n = fbm(xx, yy, 6, 7, 5, 11) * 0.8 + 0.2 * fbm(xx, yy, 14, 20, 3, 31)
    band = np.exp(-((yy - 0.12) / 0.13) ** 2)
    m = smooth(np.clip((n * band * 1.55 - 0.52) / 0.2, 0, 1))
    lit = np.clip(0.5 + (fbm(xx, yy - 0.012, 6, 7, 5, 11) * 0.8 + 0.2 * fbm(xx, yy - 0.012, 14, 20, 3, 31)
                         - n) * 40, 0, 1)
    cloud = mix(np.broadcast_to([0.80, 0.85, 0.93], (h, w, 3)), np.broadcast_to([1.0, 0.99, 0.97], (h, w, 3)), lit)
    img = mix(img, cloud, m * 0.92)
    # the sun, high on the right so the cottage never covers it: a soft disc and a wide glow
    sx, sy = 800.0, 0.2 * H
    d = np.hypot(xx - sx, (yy * H - sy) * 1.0)
    glow = np.exp(-(d / 110.0) ** 1.2) * 0.30 + np.exp(-(d / 420.0)) * 0.07
    img = img + glow[..., None] * np.array([1.0, 0.93, 0.78]) * (1 - 0.5 * m[..., None])
    disc = smooth(np.clip((30.0 - d) / 6.0, 0, 1))
    img = mix(img, np.broadcast_to([1.0, 0.98, 0.9], (h, w, 3)), disc)
    img = img.reshape(H, SS, W, SS, 3).mean((1, 3))
    img += (rng.random((H, W, 1)) - 0.5) / 255.0 * 1.5   # dither: no banding in 8 bits
    return np.clip(img, 0, 1)


def ridge_profile(x, seed, base, amp, freq0, skew):
    """Heights in px above the bottom: ridged multifractal peaks, uneven and asymmetric."""
    r = np.random.default_rng(seed)
    f = np.zeros_like(x); a = 1.0; tot = 0.0
    xw = x + 90 * np.sin(x / W * 2 * np.pi * 3 + r.random() * 6) + 40 * np.sin(x / W * 2 * np.pi * 7 + r.random() * 6)
    for k in range(6):
        fr = freq0 * 2 ** k
        ph = r.random() * 6.28
        f = f + a * (1 - np.abs(np.sin(xw / W * np.pi * fr + ph + skew * np.sin(xw / W * np.pi * fr * 0.5 + r.random() * 6))))
        tot += a; a *= 0.42
    f = f / tot
    lo, hi = np.percentile(f, 8), np.percentile(f, 99.5)
    f = np.clip((f - lo) / (hi - lo), 0, 1) ** 1.35
    k = np.exp(-0.5 * (np.arange(-24, 25) / 8.0) ** 2); k /= k.sum()
    f = np.convolve(np.concatenate([f[-24:], f, f[:24]]), k, 'valid')
    return base + amp * f


def strip():
    w, h = W * SS, H * SS
    x = (np.arange(w)[None, :] + 0.5) / SS
    rowp = (h - 1 - np.arange(h)[:, None] + 0.5) / SS          # px above the bottom
    xx = np.broadcast_to(x, (h, w)); rr = np.broadcast_to(rowp, (h, w))
    out = np.zeros((h, w, 4))

    def put(top1d, rgb, cov_soft=0.7):
        nonlocal out
        top = np.broadcast_to(top1d[None, :], (h, w))
        cov = np.clip((top - rr) / cov_soft + 0.5, 0, 1)
        out[..., :3] = out[..., :3] * (1 - cov[..., None]) + rgb * cov[..., None]
        out[..., 3] = out[..., 3] + (1 - out[..., 3]) * cov

    def haze(rgb, top1d, base_amt, grad):
        top = np.broadcast_to(top1d[None, :], rr.shape)
        v = np.clip(1 - (rr / np.maximum(top, 1)), 0, 1)       # 0 at the crest, 1 at the base
        return mix(rgb, np.broadcast_to(HORIZON, rgb.shape), np.clip(base_amt + grad * v ** 1.5, 0, 0.95))

    x1 = x[0]
    # ---- far alpine ridge: peaks 1:1.6 apart in height, snow, rock by slope
    top_far = ridge_profile(x1, 3, 190, 255, 2.5, 0.9)
    slope = np.gradient(top_far, edge_order=1) * SS
    slope = np.broadcast_to(slope[None, :], rr.shape)
    top = np.broadcast_to(top_far[None, :], rr.shape)
    stri = fbm(xx + rr * 0.35, rr / 700.0, 48, 4, 3, 51)                         # vertical gullies
    sh = np.clip(0.62 - slope * 0.5 + (stri - 0.5) * 0.3, 0.12, 1.12)
    snowline = 330 + 40 * fbm(xx, rr * 0 + 0.3, 24, 1, 3, 77)
    snow = smooth(np.clip((rr - snowline) / 26.0 + (stri - 0.5) * 0.9, 0, 1))
    rock = np.array([0.50, 0.52, 0.62]); rockd = np.array([0.30, 0.33, 0.45])
    forest = np.array([0.27, 0.40, 0.40])
    base = mix(np.broadcast_to(rockd, (h, w, 3)), np.broadcast_to(rock, (h, w, 3)), np.clip(sh, 0, 1))
    base = mix(base, np.broadcast_to(forest, (h, w, 3)) * (0.7 + 0.5 * sh[..., None]), smooth(np.clip((200 - rr) / 90.0, 0, 1)))
    snowc = mix(np.broadcast_to([0.62, 0.70, 0.86], (h, w, 3)), np.broadcast_to([0.98, 0.98, 1.0], (h, w, 3)), np.clip(sh * 1.1, 0, 1))
    far = mix(base, snowc, snow)
    far = haze(far, top_far, 0.42, 0.40)
    put(top_far, far)

    # ---- foothills: fBM, forested, bluish
    fb = fbm(x1[None, :], np.zeros((1, w)), 9, 1, 5, 91)[0]
    top_mid = 150 + 120 * fb + 30 * fbm(x1[None, :], np.zeros((1, w)), 40, 1, 3, 17)[0]
    slope = np.broadcast_to((np.gradient(top_mid) * SS)[None, :], rr.shape)
    tex = fbm(xx, rr / 300.0, 70, 10, 3, 5)
    sh = np.clip(0.6 - slope * 0.9 + (tex - 0.5) * 0.3, 0.2, 1.1)
    col = mix(np.broadcast_to([0.20, 0.34, 0.34], (h, w, 3)), np.broadcast_to([0.36, 0.52, 0.42], (h, w, 3)), np.clip(sh, 0, 1))
    col = haze(col, top_mid, 0.22, 0.38)
    put(top_mid, col)

    # ---- farmland: low rolling fields, each field a slightly different hue, crop rows, hedgerows
    top_near = 70 + 42 * fbm(x1[None, :], np.zeros((1, w)), 7, 1, 4, 33)[0]
    cuts = np.cumsum(rng.uniform(60, 200, 60)); cuts = cuts[cuts < W]
    fid = np.searchsorted(cuts, x1)
    palette = np.array([[0.55, 0.70, 0.32], [0.70, 0.74, 0.34], [0.46, 0.64, 0.34], [0.78, 0.72, 0.38], [0.40, 0.58, 0.34]])
    fc = palette[rng.integers(0, len(palette), len(cuts) + 1)] + rng.uniform(-0.04, 0.04, (len(cuts) + 1, 3))
    fcol = fc[fid]                                                    # (w,3)
    depth = np.clip(1 - rr / np.maximum(np.broadcast_to(top_near[None, :], rr.shape), 1), 0, 1)
    rows = 0.5 + 0.5 * np.sin((rr * (0.9 + 2.2 * (1 - depth)) + 0.0) * 1.4 + (xx * 0.02) * (fid[None, :] % 3 - 1))
    stripe = 0.93 + 0.07 * rows * np.broadcast_to((fid % 2 == 0)[None, :], rr.shape)
    col = np.broadcast_to(fcol[None, :, :], (h, w, 3)) * stripe[..., None]
    col *= (0.9 + 0.2 * fbm(xx, rr / 200.0, 60, 6, 3, 8))[..., None]
    col = haze(col, top_near, 0.10, 0.30)
    put(top_near, col)
    # hedgerows: dark thin lines on field boundaries, and rows of distant trees of different sizes
    for cx in cuts:
        tn = np.interp(cx, x1, top_near)
        hw = rng.uniform(1.6, 3.2)
        hh = rng.uniform(5, 11)
        mask = (np.abs(xx - cx) < hw) & (rr < tn + hh) & (rr > tn - 70 * rng.uniform(0.5, 1.0))
        hc = np.array([0.22, 0.38, 0.24]) * rng.uniform(0.9, 1.15)
        cov = mask.astype(float)
        out[..., :3] = mix(out[..., :3], np.broadcast_to(haze(np.broadcast_to(hc, (h, w, 3)), top_near, 0.1, 0.1), (h, w, 3)), cov * 0.85)
        out[..., 3] = np.maximum(out[..., 3], cov)
    for i in range(170):                                            # distant trees and copses
        cx = rng.uniform(0, W); tn = np.interp(cx, x1, top_near)
        sz = rng.uniform(5, 16) * (1.5 if rng.random() < 0.12 else 1.0)
        ry = tn - rng.uniform(0, 45) + rng.uniform(-3, 3)
        aspect = rng.uniform(0.9, 1.5)
        r0 = max(0, int(h - (ry + sz * aspect * 2.4) * SS)); r1 = min(h, int(h - (ry - 3) * SS) + 1)
        c0 = int(cx - sz * 2) * SS; c1 = int(cx + sz * 2) * SS
        cols = np.arange(c0, c1) % w                                 # wraps round the seam
        X = (np.arange(c0, c1)[None, :] + 0.5) / SS; R = rowp[r0:r1]
        d = np.hypot((X - cx) / sz, (R - ry - sz * aspect * 0.6) / (sz * aspect))
        lit = np.clip(0.55 - ((X - cx) / sz) * 0.35 + ((R - ry) / (sz * aspect * 2)) * 0.5, 0, 1)
        tc = mix(np.broadcast_to([0.17, 0.32, 0.22], d.shape + (3,)), np.broadcast_to([0.36, 0.55, 0.30], d.shape + (3,)), lit) * rng.uniform(0.9, 1.1)
        tc = tc * 0.88 + HORIZON * 0.12 + HORIZON * 0.18 * (1 - min(1, (tn - ry + 45) / 90.0)) - tc * 0.18 * (1 - min(1, (tn - ry + 45) / 90.0))
        cov = np.clip((1 - d) * sz, 0, 1)
        trunk = ((np.abs(X - cx) < 0.7) & (R >= ry) & (R < ry + sz * 0.5)).astype(float) * 0.7
        cov = np.maximum(cov, trunk)
        sel = np.ix_(np.arange(r0, r1), cols)
        out[sel + (slice(0, 3),)] = mix(out[sel + (slice(0, 3),)], tc, cov)
        out[sel + (3,)] = np.maximum(out[sel + (3,)], cov)

    # straight (un-premultiplied) alpha, area-averaged down
    a = out[..., 3:4]
    pm = out[..., :3] * a
    pm = pm.reshape(H, SS, W, SS, 3).mean((1, 3)); al = a.reshape(H, SS, W, SS, 1).mean((1, 3))
    rgb = np.where(al > 1e-4, pm / np.maximum(al, 1e-4), HORIZON * 0.7)   # clear texels carry sky colour, so filtering never darkens an edge
    return np.clip(np.concatenate([rgb, al], -1), 0, 1)


def save(arr, name):
    Image.fromarray((arr * 255 + 0.5).astype(np.uint8)).save(os.path.join(OUT, name))


if __name__ == "__main__":
    s = sky()
    save(np.concatenate([s, np.ones((H, W, 1))], -1), "bg_0.png")
    save(strip(), "bg_1.png")
    print("written bg_0.png, bg_1.png")
