# make_ground.py: the ground tile kit of the 3D stage (era 6), exported as .glb into assets/models/.
#
# Run with the bpy module:   python3 tools/blender/make_ground.py assets/models
#
# Pieces (one tile each, 1 wide x 1 tall x 4 deep, origin at the tile centre; the game stands the
# front face at +Z like the box code did):
#   ground_grass_0..5   grass top: bevelled, noisy grass lip over a soil face, standing blades, pebbles, roots
#   ground_earth_0..4   earth mid: soil face with wavy strata ledges, pebbles, a root or two
#   ground_deep_0..3    deep earth: darker, cooler, larger stones, cracks
# Models3D::drawGround picks a variant per tile from a world hash and merges them per chunk; if the
# files are missing it falls back to the box code.
#
# How it is built (Blender Z up, front face at -Y, the glTF exporter makes it Y up / +Z):
#   - every surface is a grid made with bmesh-free from_pydata, displaced by mathutils.noise (Perlin) with a
#     window w(x) * w(z) that is 0 at the tile edges, so every variant meets every other one seamlessly
#     (edge columns and rows are identical: same profile, same colour);
#   - the grass lip is a profile swept along X (5 points: top, rounded front, lip bottom, undercut, soil)
#     with per-column noise on overhang (0.04..0.1), lip height (0.2..0.3) and ragged lower edge;
#   - strata: bands of earth colour along wavy lines, each pushed out 0.012 so they read as ledges;
#   - pebbles: Poisson-disc spacing (min distance 0.15..0.2), radius 0.03..0.07, flattened 0.65, sunk 30% of
#     their height into the soil, 6 x 4 vertex spheres with noise;
#   - blades: Poisson spaced (min 0.11), 3 verts per side, tapering and bent, double sided (3 + 3 tris);
#   - roots: tapered 3 sided tubes crawling out from under the lip;
#   - vertex colour: rgb is the painted colour (sRGB values, the game's shader multiplies them straight
#     into the base colour), alpha is analytic ambient occlusion (under the lip, round pebbles, in the
#     soil crevices, dark at the back). Models3D multiplies the alpha into rgb when it merges the tiles.
import math
import os
import random
import sys

import bpy
from mathutils import Vector, noise

args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
OUT = args[0] if args else "assets/models"
# --lite: the same tiles at about a third of the triangles (coarser lip and face grids, fewer pebbles
# and blades), written as groundlite_*.glb; the 2.5D meadow (era 5) draws these
LITE = "--lite" in args
if LITE:
    args = [a for a in args if a != "--lite"]
    OUT = args[0] if args else "assets/models"
os.makedirs(OUT, exist_ok=True)

DY = 2.0  # half depth


def hexc(h):
    return ((h >> 16 & 255) / 255.0, (h >> 8 & 255) / 255.0, (h & 255) / 255.0)


def mix(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def mul(a, k):
    return tuple(min(1.0, c * k) for c in a)


def sstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def win(x):  # 0 at the tile edge, 1 inside (x in -0.5..0.5)
    return sstep(0.0, 0.09, 0.5 - abs(x))


class Builder:
    def __init__(self):
        self.v, self.c, self.f, self.fm = [], [], [], []
        self.mat = 0   # material slot for the faces added next: 0 soil, 1 grass, 2 rock

    def vert(self, p, col, ao=1.0):
        self.v.append(tuple(p))
        self.c.append((col[0], col[1], col[2], ao))
        return len(self.v) - 1

    def tri(self, a, b, c):
        self.f.append((a, b, c))
        self.fm.append(self.mat)

    def quad(self, a, b, c, d):
        self.tri(a, b, c)
        self.tri(a, c, d)

    def grid(self, fn, nu, nv, flip=False):
        """fn(i, j) -> (pos, rgb, ao); builds nu x nv quads. Counter-clockwise seen from outside unless flip."""
        ids = [[self.vert(*fn(i, j)) for j in range(nv + 1)] for i in range(nu + 1)]
        for i in range(nu):
            for j in range(nv):
                a, b, c, d = ids[i][j], ids[i + 1][j], ids[i + 1][j + 1], ids[i][j + 1]
                if flip:
                    self.quad(a, d, c, b)
                else:
                    self.quad(a, b, c, d)
        return ids


def poisson(rng, n_try, x0, x1, z0, z1, dmin, avoid=()):
    pts = []
    for _ in range(n_try):
        p = (rng.uniform(x0, x1), rng.uniform(z0, z1))
        if all(math.hypot(p[0] - q[0], p[1] - q[1]) >= dmin for q in pts) and \
           all(math.hypot(p[0] - q[0], p[1] - q[1]) >= q[2] for q in avoid):
            pts.append(p)
    return pts


class Tile:
    def __init__(self, kind, var):
        self.kind, self.var = kind, var
        self.b = Builder()
        self.rng = random.Random({'grass': 1, 'earth': 2, 'deep': 3}[kind] * 1000 + var * 17 + 3)
        self.off = Vector((var * 13.7 + 40.1 * len(kind), var * 5.3 + 11.0, 7.7 * len(kind)))
        if kind == "grass":
            self.soil, self.soil2, self.strata = hexc(0x8a5a3a), hexc(0x74482e), [hexc(0x9a6a44), hexc(0x6c4430), hexc(0x4a3a30)]
        elif kind == "earth":
            self.soil, self.soil2, self.strata = hexc(0x74482e), hexc(0x64402c), [hexc(0x865a3a), hexc(0x5c3c2c), hexc(0x4a3a30)]
        else:
            self.soil, self.soil2, self.strata = hexc(0x5e3a26), hexc(0x4e3424), [hexc(0x6a4a38), hexc(0x4a3030), hexc(0x3a2c34)]
        self.stone = [hexc(h) for h in (0xc8bca8, 0x9a948c, 0xc4a87a, 0x7a6e64, 0xdcd4c6)]
        self.stone_pts = []

    def n3(self, x, y, z, f=1.0):
        return noise.noise(Vector((x * f, y * f, z * f)) + self.off)

    # ---------------------------------------------------------------- the soil face (front, y = -DY)
    def face(self, z_top, z_bot, nu, lip_ao):
        rng = self.rng
        var = self.var
        self.b.mat = 0
        # strata lines at fixed heights per kind (so every variant meets every other at the tile edge);
        # the variant only changes how they wave inside the tile (windowed to 0 at the edges)
        base = {"grass": [-0.06, -0.33], "earth": [0.3, 0.0, -0.3], "deep": [0.22, -0.2]}[self.kind]
        lines = [(zl, rng.uniform(0.025, 0.055), k) for k, zl in enumerate(base)]
        self.lines = lines
        pebbles = []
        if self.kind == "deep":
            dmin, rlo, rhi, cap = 0.2, 0.045, 0.1, 5
        elif self.kind == "earth":
            dmin, rlo, rhi, cap = 0.16, 0.03, 0.075, 6
        else:
            dmin, rlo, rhi, cap = 0.15, 0.028, 0.065, 4
        ptop = z_top - 0.12 if self.kind != "grass" else 0.1
        for (px, pz) in poisson(rng, 90, -0.4, 0.4, z_bot + 0.1, ptop, dmin):
            if len(pebbles) >= (max(2, cap // 2) if LITE else cap):
                break
            r = rlo + (rhi - rlo) * rng.random() ** 1.8
            pebbles.append((px, pz, r))
        self.pebbles = pebbles
        # rows: the top, a pair (just above / just below) for each strata line, a filler between, the bottom
        rows = [("top", None)]
        for (zl, amp, k) in lines:
            rows += [("fill", k), ("above", k), ("below", k)]
        rows += [("fill", len(lines)), ("bot", None)]
        nv = len(rows) - 1
        layer_cols = [self.soil] + self.strata[:len(lines)]

        def line_z(x, k):
            zl, amp, _ = lines[k]
            return zl + amp * self.n3(x, 0.0, k * 3 + var, 2.2) * win(x)

        def fn(i, j):
            x = i / nu - 0.5
            kind_, k = rows[j]
            if kind_ == "top":
                z = z_top
            elif kind_ == "bot":
                z = z_bot
            elif kind_ == "above":
                z = line_z(x, k) + 0.012
            elif kind_ == "below":
                z = line_z(x, k) - 0.012
            else:   # between the previous line (or top) and the next line (or bottom)
                zu = z_top if k == 0 else line_z(x, k - 1) - 0.012
                zd = z_bot if k >= len(lines) else line_z(x, k) + 0.012
                z = (zu + zd) * 0.5
            w = win(x) * win(z)
            # which layer this row belongs to: lines above it
            lay = sum(1 for (_, _, kk) in lines if z < line_z(x, kk) - 0.001)
            col = layer_cols[min(lay, len(layer_cols) - 1)]
            bump = self.n3(x, z, var, 4.5)
            col = mix(col, self.soil2, 0.25 * (0.5 + 0.5 * self.n3(x, z, 3, 2.5)))
            col = mul(col, 1.3 + 0.14 * bump * w)
            if kind_ == "below":
                col = mul(col, 0.8)   # the shaded foot of a ledge
            y = -DY - (0.014 * lay + 0.025 * bump * w)
            ao = 1.0
            if lip_ao:
                ao *= 1.0 - 0.45 * math.exp(-max(0.0, 0.22 - z) / 0.1)
            if self.kind != "grass":
                ao *= 0.9 + 0.1 * sstep(z_bot - 0.5, z_top, z)
            if kind_ == "below":
                ao *= 0.8
            for (px, pz, r) in pebbles:
                d = math.hypot(x - px, z - pz)
                if d < r * 2.4:
                    ao *= 1.0 - 0.4 * (1.0 - d / (r * 2.4))
            return ((x, y, z), col, ao)

        return self.b.grid(fn, nu, nv)

    # ---------------------------------------------------------------- pebbles sunk 30 % in the face
    def stones(self):
        rng = self.rng
        self.b.mat = 2
        for (px, pz, r) in self.pebbles:
            ry = r * 0.65
            cy = -DY - ry * 0.4 - 0.014 * len(self.lines)  # 30 % of the height lies inside the soil
            base = self.stone[rng.randrange(5)]
            if self.kind == "deep":
                base = mix(base, hexc(0x5a5468), 0.45)
            base = mix(base, self.soil, 0.12 + 0.25 * rng.random())
            seg, ring = 6, 4
            rot = rng.uniform(-0.5, 0.5)
            ids = []
            for a in range(ring + 1):
                th = math.pi * a / ring
                row = []
                for s in range(seg if 0 < a < ring else 1):
                    ph = 2 * math.pi * s / seg
                    nx, ny, nz = math.sin(th) * math.cos(ph), -math.sin(th) * math.sin(ph) * 0.0 + math.cos(th) * 0.0, 0
                    # unit sphere with the pole along -Y (towards the camera)
                    ux, uy, uz = math.sin(th) * math.cos(ph), -math.cos(th), math.sin(th) * math.sin(ph)
                    k = 1.0 + 0.18 * self.n3(ux * 2 + px * 9, uz * 2 + pz * 9, uy * 2)
                    x = px + (ux * math.cos(rot) - uz * math.sin(rot)) * r * k
                    z = pz + (ux * math.sin(rot) + uz * math.cos(rot)) * r * 0.82 * k
                    y = cy + uy * ry * k
                    lit = 0.78 + 0.3 * (uz * 0.6 - ux * 0.25)  # light from top left
                    row.append(self.b.vert((x, y, z), mul(base, lit * 1.12), 0.8 + 0.2 * (uz * 0.5 + 0.5)))
                ids.append(row)
            self.rings(ids, seg)
            self.stone_pts.append((px, pz, r))

    def rings(self, ids, seg):
        b = self.b
        pole0, pole1 = ids[0][0], ids[-1][0]
        for s in range(seg):
            s2 = (s + 1) % seg
            b.tri(pole0, ids[1][s2], ids[1][s])
            for a in range(1, len(ids) - 2):
                b.quad(ids[a][s], ids[a][s2], ids[a + 1][s2], ids[a + 1][s])
            b.tri(pole1, ids[-2][s], ids[-2][s2])

    # ---------------------------------------------------------------- roots
    def roots(self, n, z0):
        rng = self.rng
        self.b.mat = 0
        for _ in range(n):
            x = rng.uniform(-0.3, 0.3)
            z = z0
            ang = rng.uniform(-0.5, 0.5)
            r0 = rng.uniform(0.02, 0.032)
            pts = []
            seg = 5
            for s in range(seg + 1):
                pts.append((x, z, r0 * (1 - 0.85 * s / seg)))
                step = rng.uniform(0.07, 0.11)
                ang += rng.uniform(-0.45, 0.45)
                x = max(-0.42, min(0.42, x + math.sin(ang) * step))
                z -= math.cos(ang) * step
            col = hexc(0x6a4a30)
            rings = []
            for s, (px, pz, rr) in enumerate(pts):
                ring = []
                for q in range(3):
                    a = 2 * math.pi * q / 3 + 0.5
                    ring.append(self.b.vert((px + math.cos(a) * rr, -DY - 0.012 - rr * 0.35 + math.sin(a) * rr * 0.6, pz), mul(col, 0.9 + 0.2 * (s / seg)), 0.95))
                rings.append(ring)
            for s in range(seg):
                for q in range(3):
                    q2 = (q + 1) % 3
                    self.b.quad(rings[s][q], rings[s][q2], rings[s + 1][q2], rings[s + 1][q])

    # ---------------------------------------------------------------- the grass cap
    def cap(self):
        rng = self.rng
        self.b.mat = 1
        var = self.var
        gLit, gMid, gShade = hexc(0x86c24a), hexc(0x5aa84a), hexc(0x3c9a5c)
        nu = 6 if LITE else 12

        def prof(x):
            w = win(x)
            over = 0.04 + w * (0.02 + 0.045 * (0.5 + 0.5 * self.n3(x, 1, 5, 3.0)))   # how far the lip hangs out
            hgt = 0.26 + w * 0.05 * self.n3(x, 2, 6, 2.5)                              # lip height
            rag = w * 0.06 * max(0.0, self.n3(x, 3, 7, 9.0))                           # ragged lower edge: blades hanging
            return over, hgt, rag

        # profile points: top back, top front, rounded front, lip bottom, undercut
        def fn_rows():
            rows = []
            for i in range(nu + 1):
                x = i / nu - 0.5
                over, hgt, rag = prof(x)
                zt, zb = 0.5, 0.5 - hgt - rag
                yf = -DY - over
                top = mix(mix(gMid, gLit, 0.5 + 0.5 * self.n3(x, 0, var, 2.0)), gShade, 0.2 * (1 - win(x)) * 0)
                front = mix(gShade, gMid, 0.45 + 0.4 * self.n3(x, 4, var, 3.0))
                under = mul(self.soil2, 0.5)
                rows.append([
                    ((x, yf + 0.38, zt), top, 1.0),
                    ((x, yf + 0.05, zt - 0.005), mix(top, gLit, 0.3), 1.0),
                    ((x, yf, zt - hgt * 0.35), mix(front, gMid, 0.35), 0.95),
                    ((x, yf - 0.004, zt - hgt * 0.8 - rag * 0.5), front, 0.8),
                    ((x, yf - 0.0, zb), mul(front, 0.7), 0.55),
                    ((x, -DY + 0.12, zb + 0.015), under, 0.4),
                    ((x, -DY - 0.0, zb - 0.01), mul(self.soil, 0.5), 0.45),
                ])
            return rows

        rows = fn_rows()
        ids = [[self.b.vert(*p) for p in r] for r in rows]
        for i in range(nu):
            for j in range(len(rows[0]) - 1):
                a, b_, c, d = ids[i][j], ids[i + 1][j], ids[i + 1][j + 1], ids[i][j + 1]
                self.b.quad(a, d, c, b_)  # profile runs top -> down: reversed order faces up/out
        self.lip_bottom_z = lambda x: 0.5 - prof(x)[1] - prof(x)[2]

    def top(self):
        var = self.var
        self.b.mat = 1
        gLit, gMid, gShade = hexc(0x86c24a), hexc(0x5aa84a), hexc(0x3c9a5c)
        nu, nv = (3, 3) if LITE else (5, 5)
        y0, y1 = -DY - 0.0 + 0.38, DY

        def fn(i, j):
            x = i / nu - 0.5
            y = y0 + (y1 - y0) * j / nv
            w = win(x)
            n = 0.5 + 0.5 * self.n3(x * 1.1, y, var, 1.3)
            z = 0.5 + 0.018 * self.n3(x, y, var, 5.0) * w * (1 if 0 < j < nv else 0)
            col = mix(mix(gMid, gLit, n * 1.1), mix(gShade, gMid, 0.6), 0.25 * (1 - n))
            # an odd patch of dry or clover colour
            p = self.n3(x, y, var + 9, 1.7)
            if p > 0.3:
                col = mix(col, hexc(0x9ab04a), (p - 0.3) * 1.4)
            ao = 0.85 + 0.15 * (1 - j / nv) + 0.0
            return ((x, y, z), mul(col, 1.18), ao)

        self.b.grid(fn, nu, nv)
        # flat pebbles on the surface, sunk a third into the turf
        if self.var in (2, 4) and not LITE:
            rng = self.rng
            for (px, py) in poisson(rng, 40, -0.35, 0.35, -1.2, 1.4, 0.45)[:2]:
                r = rng.uniform(0.04, 0.07)
                base = mix(self.stone[rng.randrange(5)], gMid, 0.15)
                seg, ring = 6, 3
                ids = []
                for a in range(ring + 1):
                    th = (math.pi / 2) * a / ring
                    row = []
                    for s in range(seg if a > 0 else 1):
                        ph = 2 * math.pi * s / seg
                        ux, uy, uz = math.sin(th) * math.cos(ph), math.sin(th) * math.sin(ph), math.cos(th)
                        k = 1.0 + 0.18 * self.n3(ux * 3 + px * 9, uy * 3 + py * 9, 1)
                        row.append(self.b.vert((px + ux * r * k, py + uy * r * 0.8 * k, 0.5 - r * 0.2 + uz * r * 0.6), mul(base, 0.8 + 0.3 * uz), 0.7 + 0.3 * uz))
                    ids.append(row)
                b = self.b
                for s in range(seg):
                    s2 = (s + 1) % seg
                    b.tri(ids[0][0], ids[1][s], ids[1][s2])
                    for a in range(1, ring):
                        b.quad(ids[a][s], ids[a + 1][s], ids[a + 1][s2], ids[a][s2])

    def blades(self, n):
        rng = self.rng
        self.b.mat = 1
        gLit, gMid, gShade = hexc(0x86c24a), hexc(0x5aa84a), hexc(0x3c9a5c)
        pts = poisson(rng, 200, -0.46, 0.46, -DY + 0.45, DY - 0.1, 0.17)[:n]
        for (bx, by) in pts:
            sc = rng.uniform(0.6, 1.2)
            h = 0.19 * sc
            wd = rng.choice((0.018, 0.035, 0.024)) * sc
            yaw = rng.uniform(0, math.pi)
            bend = rng.uniform(-0.5, 0.5) * h
            nz = 0.5 + 0.5 * self.n3(bx * 2, by, 3)
            tipc = mix(gMid, hexc(0xb6d25a), nz)
            basec = mix(gShade, gMid, nz)
            dx, dy = math.cos(yaw), math.sin(yaw)
            bd = rng.uniform(-1, 1)
            b = self.b
            def p(off, up, curl):
                return (bx + dx * off + dy * curl * bend * bd, by + dy * off - dx * curl * bend * bd, 0.5 + up)
            v0 = b.vert(p(-wd, -0.01, 0), mul(basec, 0.8), 0.55)
            v1 = b.vert(p(wd, -0.01, 0), mul(basec, 0.8), 0.55)
            v2 = b.vert(p(-wd * 0.6, h * 0.55, 0.35), mix(basec, tipc, 0.5), 0.85)
            v3 = b.vert(p(wd * 0.6, h * 0.55, 0.35), mix(basec, tipc, 0.5), 0.85)
            v4 = b.vert(p(0, h, 1.0), tipc, 1.0)
            b.quad(v0, v1, v3, v2)
            b.tri(v2, v3, v4)
            b.quad(v1, v0, v2, v3)  # back side
            b.tri(v3, v2, v4)

    def sides(self, grass):
        b = self.b
        b.mat = 0
        for sx in (-0.5, 0.5):
            def vv(y, z, col, ao=1.0):
                return b.vert((sx, y, z), col, ao)
            a = vv(-DY, -0.5, mul(self.soil2, 0.8), 0.7)
            c = vv(DY, -0.5, mul(self.soil2, 0.8), 0.7)
            d = vv(DY, 0.5 - (0.26 if grass else 0.0), self.soil, 0.8)
            e = vv(-DY, 0.5 - (0.26 if grass else 0.0), self.soil, 0.8)
            if sx > 0:
                b.quad(a, c, d, e)
            else:
                b.quad(a, e, d, c)
            if grass:
                b.mat = 1
                g1 = vv(-DY, 0.5 - 0.26, hexc(0x3c9a5c), 0.8)
                g2 = vv(DY, 0.5 - 0.26, hexc(0x3c9a5c), 0.8)
                g3 = vv(DY, 0.5, hexc(0x5aa84a), 1.0)
                g4 = vv(-DY, 0.5, hexc(0x5aa84a), 1.0)
                if sx > 0:
                    b.quad(g1, g2, g3, g4)
                else:
                    b.quad(g1, g4, g3, g2)
                b.mat = 0


def build(kind, var):
    t = Tile(kind, var)
    if kind == "grass":
        t.cap()
        t.top()
        t.blades(7 if LITE else 12 + (var % 3) * 2)
        t.face(0.21, -0.5, 5 if LITE else 10, True)
        t.stones()
        if var % 3 == 0:
            t.roots(1, 0.19)
        t.sides(True)
    else:
        t.face(0.5, -0.5, 5 if LITE else 10, False)
        t.stones()
        if kind == "earth":
            if var % 3 == 1:
                t.roots(1, 0.45)
        else:
            # a crack: a thin dark ribbon
            rng = t.rng
            x, z = rng.uniform(-0.3, 0.3), 0.45
            col = hexc(0x2a1c1c)
            prev = None
            for s in range(7):
                a = (t.b.vert((x - 0.012, -DY - 0.02, z), col, 0.4), t.b.vert((x + 0.012, -DY - 0.02, z), col, 0.4))
                if prev:
                    t.b.quad(prev[0], prev[1], a[1], a[0])
                prev = a
                x = max(-0.42, min(0.42, x + rng.uniform(-0.07, 0.07)))
                z -= rng.uniform(0.1, 0.16)
        t.sides(False)
    return t


def export(name, t):
    me = bpy.data.meshes.new(name)
    b = t.b
    me.from_pydata(b.v, [], b.f)
    me.update()
    for p in me.polygons:
        p.use_smooth = True
    me.set_sharp_from_angle(angle=math.radians(50))
    at = me.color_attributes.new("Col", "FLOAT_COLOR", "CORNER")
    flat = []
    for lp in me.loops:
        flat.extend(b.c[lp.vertex_index])
    at.data.foreach_set("color", flat)
    me.color_attributes.active_color = at
    me.color_attributes.render_color_index = me.color_attributes.active_color_index
    # three slots; the base colour factor's red byte tells the game which CC0 texture to multiply in
    # (255 earth, 250 grass, 245 rock; see Models3D::recordPart)
    for nm, red in (("soil", 1.0), ("grass", 250 / 255.0), ("rock", 245 / 255.0)):
        mat = bpy.data.materials.new(nm)
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes["Principled BSDF"]
        bsdf.inputs["Base Color"].default_value = (red, 1, 1, 1)
        bsdf.inputs["Roughness"].default_value = 0.95
        me.materials.append(mat)
    for p, m_ in zip(me.polygons, b.fm):
        p.material_index = m_
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    me.calc_loop_triangles()
    n = len(me.loop_triangles)
    path = os.path.join(OUT, name + ".glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_apply=True,
                              export_yup=True, export_normals=True, export_texcoords=False,
                              export_materials="EXPORT", export_vertex_color="ACTIVE",
                              export_all_vertex_colors=False, export_animations=False)
    bpy.ops.object.delete()
    print("wrote", path, n, "tris")
    return n


if __name__ == "__main__":
    total = {}
    for kind, count in (("grass", 6), ("earth", 5), ("deep", 4)):
        for v in range(count):
            total[(kind, v)] = export("ground%s_%s_%d" % ("lite" if LITE else "", kind, v), build(kind, v))
    print("tris min/max", min(total.values()), max(total.values()))
