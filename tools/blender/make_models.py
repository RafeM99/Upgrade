# make_models.py: builds the 3D stage's characters in Blender and exports each moving part as its
# own .glb into assets/models/: Gran, Sam, the grandfather clock (an enemy) and the Forgetting
# (the final boss, a ball of Gran's knitting wool coming unravelled). The game
# (src/render/Models3D.cpp) puts the parts together and animates them in code.
#
# Run with Blender:   blender --background --python tools/blender/make_models.py -- assets/models
# or with the bpy module:  python tools/blender/make_models.py assets/models
#
# Conventions (Blender, Z up): every part faces +X, its pivot (the joint it turns about) is at
# the origin. The glTF exporter turns Z-up into Y-up, which is what the game uses.
import math
import os
import random
import sys

import bpy
import bmesh
from mathutils import Matrix, Vector

OUT = "assets/models"
args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
if args:
    OUT = args[0]
os.makedirs(OUT, exist_ok=True)


# ------------------------------------------------------------------ helpers
def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


MATS = {}


def mat(name, hexcol):
    if name in MATS:
        return MATS[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    r, g, b = ((hexcol >> 16) & 255) / 255, ((hexcol >> 8) & 255) / 255, (hexcol & 255) / 255
    # Blender colours are linear; glTF stores them linear too and the game reads them as sRGB
    # bytes, so store the sRGB value directly to keep the colours as picked.
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (r, g, b, 1)
    bsdf.inputs["Roughness"].default_value = 1.0
    m.diffuse_color = (r, g, b, 1)
    MATS[name] = m
    return m


def finish(obj, material, smooth=True, subdiv=0):
    obj.data.materials.clear()
    obj.data.materials.append(material)
    if subdiv:
        mod = obj.modifiers.new("sub", "SUBSURF")
        mod.levels = subdiv
        mod.render_levels = subdiv
    for p in obj.data.polygons:
        p.use_smooth = smooth
    return obj


def sphere(name, loc, scale, material, seg=16, rings=10, smooth=True, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=seg, ring_count=rings, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    o.scale = scale
    return finish(o, material, smooth)


def cyl(name, a, b, r1, r2, material, verts=12, smooth=True):
    """A tapered cylinder from point a to point b."""
    a, b = Vector(a), Vector(b)
    d = b - a
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r1, radius2=r2, depth=d.length,
                                    location=(a + b) / 2)
    o = bpy.context.active_object
    o.name = name
    o.rotation_mode = "QUATERNION"
    o.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(d.normalized())
    return finish(o, material, smooth)


def cube(name, loc, scale, material, bevel=0.0, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    o.scale = scale
    if bevel:
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        m = o.modifiers.new("bev", "BEVEL")
        m.width = bevel
        m.segments = 2
    return finish(o, material, smooth=False)


def torus(name, loc, r_major, r_minor, material, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_torus_add(major_radius=r_major, minor_radius=r_minor, major_segments=16,
                                     minor_segments=6, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    return finish(o, material, True)


def cone(name, loc, r, depth, material, rot=(0, 0, 0), verts=10):
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r, radius2=0, depth=depth, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    return finish(o, material, True)


def export(name, objs):
    """Apply modifiers and transforms, join into one object (keeping materials) and export."""
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
        bpy.context.view_layer.objects.active = o
        for m in list(o.modifiers):
            bpy.ops.object.modifier_apply(modifier=m.name)
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    o = bpy.context.active_object
    o.name = name
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    path = os.path.join(OUT, name + ".glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True,
                              export_apply=True, export_yup=True, export_normals=True,
                              export_texcoords=False, export_materials="EXPORT",
                              export_animations=False)
    print("wrote", path)
    bpy.ops.object.delete()




def tube(name, r, length, material, sides=12, rings=8, off=(0, 0)):
    """A straight tube hanging from z 0 down to -length, with rings so it can be twisted or bent."""
    bm = bmesh.new()
    rows = []
    for i in range(rings + 1):
        z = -length * i / rings
        rows.append([bm.verts.new((off[0] + r * math.cos(2 * math.pi * k / sides),
                                   off[1] + r * math.sin(2 * math.pi * k / sides), z)) for k in range(sides)])
    for i in range(rings):
        for k in range(sides):
            a, b = rows[i][k], rows[i][(k + 1) % sides]
            c, d = rows[i + 1][(k + 1) % sides], rows[i + 1][k]
            bm.faces.new((a, d, c, b))
    bm.faces.new(rows[0])
    bm.faces.new(list(reversed(rows[-1])))
    me = bpy.data.meshes.new(name)
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(o)
    return finish(o, material, True)


def prism(name, outline, depth, x, material, smooth=False):
    """A flat shape drawn in the front view (y, z points, anticlockwise seen from +X), given
    `depth` along X and centred on x. Used for the clock's hands, the swan neck and the feet."""
    bm = bmesh.new()
    front = [bm.verts.new((x + depth / 2, y, z)) for (y, z) in outline]
    back = [bm.verts.new((x - depth / 2, y, z)) for (y, z) in outline]
    n = len(outline)
    bm.faces.new(front)
    bm.faces.new(list(reversed(back)))
    for k in range(n):
        bm.faces.new((front[k], back[k], back[(k + 1) % n], front[(k + 1) % n]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(o)
    return finish(o, material, smooth)


def cut(obj, cutter):
    """Boolean: obj minus cutter; the cutter is removed. New faces keep the cutter's material."""
    m = obj.modifiers.new("cut", "BOOLEAN")
    m.operation = "DIFFERENCE"
    m.object = cutter
    m.solver = "EXACT"
    m.use_self = True
    try:
        m.material_mode = "TRANSFER"
    except (AttributeError, TypeError):
        pass
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=m.name)
    bpy.data.objects.remove(cutter, do_unlink=True)
    return obj


def scale_about(objs, pivot, f):
    """Scales whole objects about a point (used to make heads bigger, and to size the clock)."""
    p = Vector(pivot)
    for o in objs:
        o.location = p + (Vector(o.location) - p) * f
        o.scale = Vector(o.scale) * f


def rgb(hexcol):
    return (((hexcol >> 16) & 255) / 255, ((hexcol >> 8) & 255) / 255, (hexcol & 255) / 255)


# ------------------------------------------------------------------ Gran
# About 1.5 units tall. Hip joint 0.55 above the feet, shoulders 0.4 above the hips. The
# camera sees her about 96 pixels tall, so she is built from a few big readable shapes: a big
# head (1.3x), curly white perm and bun, bright round glasses, the red cardigan and her scarf.
# No buttons or cheeks: at that size they only turn into noise.
HEAD_K = 1.3


def gran():
    coat = mat("coat", 0x9a3a32)        # warm red-brown cardigan coat
    coatD = mat("coatD", 0x6a2422)
    trous = mat("trousers", 0x4a4650)
    shoe = mat("shoe", 0x2a2024)
    skin = mat("skin", 0xf2c2a0)
    hair = mat("hair", 0xe4e2ec)
    hairD = mat("hairD", 0xb4b0c4)
    ink = mat("ink", 0x1c1420)
    rims = mat("glassRims", 0x2f6fe0)   # bright blue: reads as two rings against her skin
    scarf = mat("scarf", 0xe0b040)

    # body: pivot at the hips. One tapered cardigan shape, not a pile of balls.
    parts = [
        cyl("torso", (0, 0, -0.2), (0, 0, 0.36), 0.23, 0.18, coat, 24),
        sphere("shoulders", (0, 0, 0.36), (0.17, 0.25, 0.09), coat, 24, 10),
        cyl("hem", (0, 0, -0.22), (0, 0, -0.12), 0.24, 0.225, coatD, 24),
        cyl("collar", (0, 0, 0.4), (0, 0, 0.5), 0.12, 0.1, scarf, 16),
        sphere("scarfKnot", (0.11, 0.03, 0.41), (0.065, 0.065, 0.055), scarf, 12, 8),
        cube("scarfEnd", (0.16, 0.05, 0.27), (0.03, 0.07, 0.2), scarf, 0.01, rot=(0.15, 0.25, 0)),
        cube("pocketL", (0.17, 0.14, -0.06), (0.05, 0.11, 0.09), coatD, 0.01, rot=(0, 0, 0.45)),
        cube("pocketR", (0.17, -0.14, -0.06), (0.05, 0.11, 0.09), coatD, 0.01, rot=(0, 0, -0.45)),
        cyl("placket", (0.215, 0, -0.2), (0.18, 0, 0.36), 0.014, 0.014, coatD, 6),
    ]
    export("gran_body", parts)

    # head: pivot at the base of the neck; everything above the neck is 1.3x
    head = [
        sphere("head", (0.02, 0, 0.22), (0.17, 0.165, 0.18), skin, 24, 16),
        sphere("nose", (0.19, 0, 0.2), (0.04, 0.035, 0.045), skin, 10, 8),
        sphere("earL", (0.0, 0.165, 0.21), (0.03, 0.02, 0.045), skin, 8, 6),
        sphere("earR", (0.0, -0.165, 0.21), (0.03, 0.02, 0.045), skin, 8, 6),
        # hair: a cap swept back, a row of tight perm curls round the hairline, and the bun
        sphere("hairCap", (-0.02, 0, 0.27), (0.175, 0.178, 0.16), hair, 24, 14),
        sphere("hairBack", (-0.09, 0, 0.19), (0.12, 0.17, 0.16), hair, 16, 10),
        sphere("bun", (-0.08, 0, 0.45), (0.1, 0.1, 0.085), hair, 16, 10),
        torus("bunBand", (-0.08, 0, 0.4), 0.07, 0.016, hairD),
        # eyes behind the glasses, and a small smile
        sphere("eyeL", (0.183, 0.06, 0.235), (0.012, 0.02, 0.026), ink, 8, 6),
        sphere("eyeR", (0.183, -0.06, 0.235), (0.012, 0.02, 0.026), ink, 8, 6),
        cyl("browL", (0.18, 0.09, 0.295), (0.185, 0.035, 0.3), 0.009, 0.009, hairD, 6),
        cyl("browR", (0.18, -0.09, 0.295), (0.185, -0.035, 0.3), 0.009, 0.009, hairD, 6),
        cyl("mouth", (0.18, 0.035, 0.14), (0.18, -0.035, 0.14), 0.009, 0.009, ink, 6),
        # glasses well clear of the face (rims at x 0.2): two bright rings, a bridge, the arms
        torus("rimL", (0.2, 0.062, 0.24), 0.046, 0.011, rims, rot=(0, math.pi / 2, 0)),
        torus("rimR", (0.2, -0.062, 0.24), 0.046, 0.011, rims, rot=(0, math.pi / 2, 0)),
        cyl("bridge", (0.205, 0.02, 0.25), (0.205, -0.02, 0.25), 0.009, 0.009, rims, 6),
        cyl("armL", (0.19, 0.105, 0.25), (0.0, 0.168, 0.24), 0.007, 0.007, rims, 6),
        cyl("armR", (0.19, -0.105, 0.25), (0.0, -0.168, 0.24), 0.007, 0.007, rims, 6),
    ]
    # perm curls: small rings standing round the front and sides of the hairline
    for k in range(7):
        a = -1.25 + k * (2.5 / 6)
        x, y = 0.0 + 0.14 * math.cos(a), 0.15 * math.sin(a)
        z = 0.355 - 0.03 * abs(math.sin(a))
        head.append(torus("curl%d" % k, (x, y, z), 0.032, 0.02, hair, rot=(0, math.pi / 2 - 0.5, a)))
    for s in (1, -1):
        head.append(torus("sideCurl", (-0.02, s * 0.165, 0.3), 0.034, 0.02, hair, rot=(math.pi / 2, 0, 0)))
    scale_about(head, (0, 0, 0.08), HEAD_K)
    export("gran_head", [cyl("neck", (0, 0, -0.02), (0, 0, 0.12), 0.06, 0.06, skin, 12)] + head)

    # upper arm: pivot at the shoulder, hanging down -Z
    export("gran_upperarm", [
        sphere("shoulderCap", (0, 0, -0.01), (0.075, 0.075, 0.075), coat, 12, 8),
        cyl("upper", (0, 0, 0), (0, 0, -0.21), 0.068, 0.06, coat, 12),
    ])
    # forearm with cuff and hand: pivot at the elbow
    export("gran_forearm", [
        cyl("lower", (0, 0, 0.03), (0, 0, -0.17), 0.06, 0.055, coat, 12),
        cyl("cuff", (0, 0, -0.15), (0, 0, -0.19), 0.064, 0.064, coatD, 12),
        sphere("hand", (0.01, 0, -0.24), (0.05, 0.04, 0.06), skin, 12, 8),
        sphere("thumb", (0.04, 0.03, -0.22), (0.02, 0.018, 0.03), skin, 8, 6),
    ])
    # thigh: pivot at the hip
    export("gran_thigh", [
        cyl("thigh", (0, 0, 0.02), (0, 0, -0.28), 0.085, 0.072, trous, 12),
    ])
    # shin with shoe: pivot at the knee
    export("gran_shin", [
        cyl("shin", (0, 0, 0.03), (0, 0, -0.24), 0.072, 0.06, trous, 12),
        sphere("shoe", (0.05, 0, -0.27), (0.11, 0.065, 0.045), shoe, 14, 8),
        cube("sole", (0.05, 0, -0.305), (0.22, 0.12, 0.02), mat("sole", 0xe8e0d0), 0.008),
    ])


# ------------------------------------------------------------------ Sam
# Gran's grandson, built to the same standard and on the same skeleton as Gran (so the game
# animates both with one run cycle): blue hoodie with the hood down, jeans, white trainers,
# brown hair under a teal cap worn forward. Head 1.3x like Gran's.
def sam():
    top = mat("hoodie", 0x3c8cf0)
    topD = mat("hoodieD", 0x2a64b4)
    jeans = mat("jeans", 0x2b3f6b)
    jeansD = mat("jeansD", 0x1f2e50)
    shoeW = mat("trainer", 0xf4f4f4)
    stripe = mat("trainerStripe", 0xe8364f)
    skin = mat("samSkin", 0xe9b58c)
    hair = mat("samHair", 0x6b3e22)
    cap = mat("cap", 0x22c4b0)
    capD = mat("capD", 0x168a7c)
    ink = mat("samInk", 0x1c1420)
    white = mat("string", 0xf4f4f4)

    export("sam_body", [
        cyl("torso", (0, 0, -0.16), (0, 0, 0.36), 0.2, 0.18, top, 24),
        sphere("shoulders", (0, 0, 0.36), (0.16, 0.235, 0.08), top, 24, 10),
        cyl("band", (0, 0, -0.18), (0, 0, -0.1), 0.205, 0.205, topD, 24),
        # the hood lying in a thick roll behind the neck
        torus("hood", (-0.05, 0, 0.42), 0.12, 0.05, topD, rot=(0, -0.5, 0)),
        cube("pocket", (0.19, 0, -0.03), (0.04, 0.24, 0.13), topD, 0.01),
        cyl("stringL", (0.17, 0.04, 0.4), (0.21, 0.05, 0.22), 0.008, 0.008, white, 6),
        cyl("stringR", (0.17, -0.04, 0.4), (0.21, -0.05, 0.22), 0.008, 0.008, white, 6),
    ])

    head = [
        sphere("head", (0.02, 0, 0.21), (0.16, 0.155, 0.17), skin, 24, 16),
        sphere("nose", (0.18, 0, 0.19), (0.032, 0.03, 0.035), skin, 10, 8),
        sphere("earL", (0.0, 0.155, 0.2), (0.03, 0.02, 0.045), skin, 8, 6),
        sphere("earR", (0.0, -0.155, 0.2), (0.03, 0.02, 0.045), skin, 8, 6),
        sphere("hair", (-0.03, 0, 0.25), (0.165, 0.168, 0.15), hair, 24, 14),
        sphere("eyeL", (0.17, 0.06, 0.225), (0.014, 0.022, 0.03), ink, 8, 6),
        sphere("eyeR", (0.17, -0.06, 0.225), (0.014, 0.022, 0.03), ink, 8, 6),
        cyl("browL", (0.17, 0.09, 0.28), (0.175, 0.035, 0.285), 0.01, 0.01, hair, 6),
        cyl("browR", (0.17, -0.09, 0.28), (0.175, -0.035, 0.285), 0.01, 0.01, hair, 6),
        cyl("mouth", (0.17, 0.03, 0.125), (0.17, -0.03, 0.125), 0.009, 0.009, ink, 6),
        # the cap: a crown, a button on top and the brim forward
        sphere("capCrown", (0.0, 0, 0.3), (0.17, 0.172, 0.11), cap, 24, 12),
        sphere("capButton", (0.0, 0, 0.41), (0.025, 0.025, 0.018), capD, 8, 6),
        cube("brim", (0.19, 0, 0.305), (0.15, 0.22, 0.02), capD, 0.01, rot=(0, -0.12, 0)),
    ]
    for s in (1, -1):   # tufts of hair out from under the cap over the ears
        head.append(sphere("tuft", (-0.04, s * 0.15, 0.25), (0.06, 0.04, 0.05), hair, 10, 8))
    scale_about(head, (0, 0, 0.08), HEAD_K)
    export("sam_head", [cyl("neck", (0, 0, -0.02), (0, 0, 0.12), 0.055, 0.055, skin, 12)] + head)

    export("sam_upperarm", [
        sphere("shoulderCap", (0, 0, -0.01), (0.07, 0.07, 0.07), top, 12, 8),
        cyl("upper", (0, 0, 0), (0, 0, -0.21), 0.064, 0.058, top, 12),
    ])
    export("sam_forearm", [
        cyl("lower", (0, 0, 0.03), (0, 0, -0.17), 0.058, 0.054, top, 12),
        cyl("cuff", (0, 0, -0.15), (0, 0, -0.19), 0.06, 0.06, topD, 12),
        sphere("hand", (0.01, 0, -0.235), (0.048, 0.038, 0.055), skin, 12, 8),
        sphere("thumb", (0.04, 0.03, -0.215), (0.02, 0.018, 0.028), skin, 8, 6),
    ])
    export("sam_thigh", [
        cyl("thigh", (0, 0, 0.02), (0, 0, -0.28), 0.08, 0.068, jeans, 12),
    ])
    export("sam_shin", [
        cyl("shin", (0, 0, 0.03), (0, 0, -0.22), 0.068, 0.062, jeans, 12),
        cyl("turnUp", (0, 0, -0.18), (0, 0, -0.23), 0.071, 0.071, jeansD, 12),
        sphere("shoe", (0.05, 0, -0.27), (0.12, 0.07, 0.05), shoeW, 14, 8),
        cube("sole", (0.05, 0, -0.305), (0.24, 0.13, 0.025), mat("samSole", 0xd8d4cc), 0.008),
        cube("stripe", (0.04, 0, -0.27), (0.1, 0.142, 0.018), stripe, 0.004, rot=(0, -0.4, 0)),
    ])


# ------------------------------------------------------------------ the grandfather clock
# A real longcase clock: plinth (base), a long trunk and the hood (about 22 / 50 / 28%), with a
# break-arch top and a swan-neck pediment. It walks on two carved bracket feet. Its face (eyes
# and brows, drawn by the game so they can glow) sits in the arch above the dial, where a moon
# disc would be, so the hands never cross it. The pendulum hangs inside the trunk and only its
# weight shows, through a round window (the lenticle) in the door.
# Built at the sizes below, then scaled so the case is CLOCK_H tall. The game reads the pivots
# printed at the end (pendulum, dial centre, arch face, feet).
CLOCK_H = 1.7


def clock():
    walnut = mat("walnut", 0x5a3420)
    edge = mat("walnutEdge", 0x8a5a34)
    footM = mat("walnutFoot", 0x3e2416)
    brass = mat("brass", 0xc89a3c)
    ivory = mat("ivory", 0xefe6cf)
    ink = mat("clockInk", 0x1c1420)
    inside = mat("clockInside", 0x1a100a)

    case = []
    # plinth with a lighter fielded panel on the front
    case.append(cube("plinth", (0, 0, 0.18), (0.30, 0.48, 0.36), walnut, 0.008))
    case.append(cube("plinthPanel", (0.152, 0, 0.18), (0.012, 0.34, 0.22), edge, 0.004))
    # stepped moulding up to the trunk
    case.append(cube("mould1", (0, 0, 0.375), (0.32, 0.5, 0.03), walnut, 0.006))
    case.append(cube("mould2", (0, 0, 0.405), (0.29, 0.44, 0.03), walnut, 0.006))
    # the trunk: a hollow box so the pendulum can swing inside
    tz, th = 0.42 + 0.425, 0.85
    case.append(cube("trunkBack", (-0.12, 0, tz), (0.02, 0.38, th), walnut))
    case.append(cube("trunkSideL", (0, 0.18, tz), (0.26, 0.02, th), walnut))
    case.append(cube("trunkSideR", (0, -0.18, tz), (0.26, 0.02, th), walnut))
    case.append(cube("trunkInside", (-0.105, 0, tz), (0.008, 0.34, th - 0.02), inside))
    frame = cube("trunkFront", (0.12, 0, tz), (0.02, 0.38, th), walnut)
    cut(frame, cube("doorHole", (0.12, 0, tz), (0.1, 0.28, 0.75), walnut))
    case.append(frame)
    # the door, pushed in 0.01: a lighter edge around a walnut panel, with the round window
    door = cube("door", (0.112, 0, tz), (0.012, 0.28, 0.75), edge)
    panel = cube("doorPanel", (0.12, 0, tz), (0.006, 0.24, 0.71), walnut)
    LENT_Z = 0.62
    for o in (door, panel):
        c = cyl("lenticleCut", (0.2, 0, LENT_Z), (0.0, 0, LENT_Z), 0.08, 0.08, inside, 24, smooth=False)
        cut(o, c)
    case += [door, panel]
    case.append(torus("lenticleRing", (0.124, 0, LENT_Z), 0.085, 0.009, brass, rot=(0, math.pi / 2, 0)))
    # moulding out to the hood
    case.append(cube("mould3", (0, 0, 1.285), (0.29, 0.44, 0.03), walnut, 0.006))
    case.append(cube("mould4", (0, 0, 1.315), (0.33, 0.52, 0.03), walnut, 0.006))
    # the hood, its break-arch top and the columns at the front corners
    case.append(cube("hood", (0, 0, 1.49), (0.32, 0.50, 0.32), walnut, 0.006))
    case.append(cyl("arch", (-0.16, 0, 1.65), (0.16, 0, 1.65), 0.17, 0.17, walnut, 32, smooth=True))
    case.append(cube("hoodTop", (0, 0, 1.665), (0.33, 0.52, 0.03), walnut, 0.006))
    for s in (1, -1):
        case.append(cyl("column", (0.15, s * 0.225, 1.36), (0.15, s * 0.225, 1.62), 0.025, 0.025, brass, 12))
        case.append(cube("capLow", (0.15, s * 0.225, 1.345), (0.06, 0.06, 0.03), brass, 0.004))
        case.append(cube("capHigh", (0.15, s * 0.225, 1.635), (0.06, 0.06, 0.03), brass, 0.004))
    # dial: a square plate, the ivory dial, a brass ring and twelve hour marks
    DIAL = (0.178, 0, 1.49)
    case.append(cube("dialPlate", (0.163, 0, 1.49), (0.01, 0.30, 0.30), edge))
    case.append(cyl("dial", (0.165, 0, 1.49), (0.175, 0, 1.49), 0.14, 0.14, ivory, 32, smooth=False))
    case.append(torus("dialRing", (0.176, 0, 1.49), 0.14, 0.008, brass, rot=(0, math.pi / 2, 0)))
    for k in range(12):
        a = k * math.pi / 6
        big = k % 3 == 0
        case.append(cube("hour%d" % k, (0.177, 0.112 * math.sin(a), 1.49 + 0.112 * math.cos(a)),
                         (0.006, 0.014 if big else 0.009, 0.034 if big else 0.022), ink, rot=(-a, 0, 0)))
    # the arch: an ivory half disc where the moon would be. The game draws the eyes on it.
    moon = cyl("moon", (0.163, 0, 1.65), (0.172, 0, 1.65), 0.135, 0.135, ivory, 32, smooth=False)
    cut(moon, cube("moonCut", (0.17, 0, 1.55), (0.1, 0.4, 0.2), ivory))
    case.append(moon)
    # the swan neck: an S-curve each side rising to a scroll, and three brass finials
    for s in (1, -1):
        pts = []
        N = 14
        for i in range(N + 1):   # outer edge then inner edge of a band 0.035 wide
            t = i / N
            y = 0.25 - 0.18 * t
            z = 1.68 + 0.17 * (3 * t * t - 2 * t * t * t)
            pts.append((y, z))
        band = []
        for i, (y, z) in enumerate(pts):
            j = min(i, N - 1)
            dy, dz = pts[j + 1][0] - pts[j][0], pts[j + 1][1] - pts[j][1]
            L = math.hypot(dy, dz)
            band.append((y - dz / L * 0.0175, z + dy / L * 0.0175))
        for i, (y, z) in reversed(list(enumerate(pts))):
            j = min(i, N - 1)
            dy, dz = pts[j + 1][0] - pts[j][0], pts[j + 1][1] - pts[j][1]
            L = math.hypot(dy, dz)
            band.append((y + dz / L * 0.0175, z - dy / L * 0.0175))
        o = prism("swan", [(s * y, z) for (y, z) in band] if s > 0 else [(s * y, z) for (y, z) in reversed(band)],
                  0.04, 0.13, walnut)
        case.append(o)
        case.append(torus("scroll", (0.13, s * 0.07, 1.85), 0.025, 0.012, walnut, rot=(0, math.pi / 2, 0)))
        case.append(cube("sideBlock", (0.0, s * 0.235, 1.7), (0.06, 0.06, 0.04), walnut, 0.004))
        case.append(sphere("sideFinial", (0.0, s * 0.235, 1.76), (0.04, 0.04, 0.04), brass, 12, 8))
    case.append(cube("midBlock", (0.0, 0, 1.835), (0.06, 0.06, 0.03), walnut, 0.004))
    case.append(sphere("midFinial", (0.0, 0, 1.89), (0.04, 0.04, 0.04), brass, 12, 8))

    top = 1.93
    f = CLOCK_H / top
    scale_about(case, (0, 0, 0), f)
    export("clock_case", case)

    # the pendulum: pivot where it hangs, inside the trunk; only the weight shows in the window
    PIV = (0.05, 0, 1.0)
    L = PIV[2] - LENT_Z
    pend = [cyl("rod", (0, 0, 0), (0, 0, -L), 0.006, 0.006, brass, 6),
            cyl("bob", (-0.012, 0, -L), (0.012, 0, -L), 0.048, 0.048, brass, 24, smooth=False),
            torus("bobRim", (0.013, 0, -L), 0.04, 0.006, mat("brassDark", 0x8a6420), rot=(0, math.pi / 2, 0))]
    scale_about(pend, (0, 0, 0), f)
    export("clock_pendulum", pend)

    # the hands: flat spade shapes, pivot at the dial centre, pointing up (+Z)
    def hand(name, length, w):
        out = [(-w * 0.35, 0.0), (w * 0.35, 0.0), (w * 0.3, length * 0.62), (w, length * 0.75),
               (0.0, length), (-w, length * 0.75), (-w * 0.3, length * 0.62)]
        o = prism(name, [(y, z - 0.012) for (y, z) in out], 0.006, 0.0, ink)
        hub = cyl("hub", (-0.004, 0, 0), (0.006, 0, 0), 0.014, 0.014, brass, 12)
        scale_about([o, hub], (0, 0, 0), f)
        export(name, [o, hub])
    hand("clock_hour", 0.085, 0.016)
    hand("clock_minute", 0.122, 0.012)

    # bracket feet: pivot at the ankle (top), a block with a scrolled toe
    for name in ("clock_foot_l", "clock_foot_r"):
        foot = [cube("block", (0.0, 0, -0.03), (0.26, 0.11, 0.06), footM, 0.008),
                cyl("toe", (0.13, -0.055, -0.055), (0.13, 0.055, -0.055), 0.04, 0.04, footM, 16),
                cyl("heel", (-0.13, -0.055, -0.06), (-0.13, 0.055, -0.06), 0.03, 0.03, footM, 12)]
        scale_about(foot, (0, 0, 0), f)
        export(name, foot)

    print("CLOCK scale %.5f" % f)
    print("CLOCK pendulum pivot %.4f %.4f" % (PIV[0] * f, PIV[2] * f))
    print("CLOCK dial centre %.4f %.4f" % (DIAL[0] * f, DIAL[2] * f))
    print("CLOCK face (arch) %.4f %.4f" % (0.172 * f, 1.71 * f))
    print("CLOCK foot height %.4f" % (0.095 * f))


# ------------------------------------------------------------------ the Forgetting (boss)
# A ball of Gran's dark knitting wool coming unravelled: memory unravelling. A lumpy wool core
# with yarn wrapped round it, two knitting needles stuck through, one hollow eye with a cyan
# ring and a white pupil (its own part, so the game turns it to the players), and strands of
# two-ply yarn hanging below (chains of the same twisted piece, ending in a frayed tip).
# Faces +X. Pivot at the centre of the ball.
def forgetting():
    wool = mat("wool", 0x5e5a66)
    woolD = mat("woolDark", 0x4c4856)
    woolL = mat("woolLight", 0x6c6876)
    steel = mat("needle", 0xb8bcc8)
    steelD = mat("needleKnob", 0x8a8e9a)
    black = mat("eyeHollow", 0x0c0a12)
    cyan = mat("eyeRing", 0x7fe8ff)
    rng = random.Random(11)

    core = sphere("core", (0, 0, 0), (1, 1, 1), wool, 32, 16)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    # lumpy wool surface (the Clouds displacement, done with Blender's noise so it runs anywhere)
    from mathutils import noise
    for v in core.data.vertices:
        n = noise.fractal(v.co * (1 / 0.25), 0.5, 2.0, 3)
        v.co = v.co.normalized() * (0.9 + 0.05 * n)
    parts = [core]
    # yarn wraps: tori turned to random angles round the ball, thick enough to stand proud
    for k in range(20):
        rot = (rng.uniform(0, math.pi), rng.uniform(0, math.pi), rng.uniform(0, math.pi))
        m = [wool, woolD, woolL][k % 3]
        bpy.ops.mesh.primitive_torus_add(major_radius=0.9, minor_radius=0.03, major_segments=36,
                                         minor_segments=5, location=(0, 0, 0), rotation=rot)
        o = bpy.context.active_object
        o.name = "wrap%d" % k
        finish(o, m, True)
        parts.append(o)
    # one mesh, then the eye hollowed out of the front (the hollow is lined near-black)
    bpy.ops.object.select_all(action="DESELECT")
    for o in parts:
        o.select_set(True)
    bpy.context.view_layer.objects.active = core
    bpy.ops.object.join()
    ball = bpy.context.active_object
    EYE = Vector((0.9, 0, 0.12))
    hollow = sphere("hollow", (EYE.x + 0.18, 0, EYE.z), (0.36, 0.36, 0.36), black, 24, 16)
    cut(ball, hollow)
    body = [ball,
            torus("eyeRing", (EYE.x - 0.01, 0, EYE.z), 0.31, 0.022, cyan, rot=(0, math.pi / 2, 0))]
    # two knitting needles crossing through the ball at 30 degrees, a knob on each end
    X = Vector((-0.2, 0, 0.55))   # where they cross, inside the top of the ball
    for s in (1, -1):
        d = Vector((-0.2, s * 0.26, 1)).normalized()
        p0, p1 = X - d * 0.5, X + d * 0.9
        body.append(cyl("needle", tuple(p0), tuple(p1), 0.025, 0.025, steel, 10))
        body.append(sphere("knob", tuple(p1), (0.05, 0.05, 0.05), steelD, 10, 8))
    export("forget_body", body)
    print("FORGET eye centre %.3f %.3f" % (EYE.x, EYE.z))

    # the pupil: a small white ball, placed in the hollow by the game
    export("forget_pupil", [sphere("pupil", (0, 0, 0), (0.09, 0.09, 0.09), mat("pupil", 0xf8f6f0), 14, 10)])

    # one strand piece: two plies of yarn side by side, twisted half a turn; pivot at the top
    plies = [tube("plyA", 0.06, 0.40, wool, 16, 10, off=(0, 0.05)),
             tube("plyB", 0.06, 0.40, woolD, 16, 10, off=(0, -0.05))]
    for o in plies:
        m = o.modifiers.new("twist", "SIMPLE_DEFORM")
        m.deform_method = "TWIST"
        m.angle = math.pi
        m.deform_axis = "Z"
    export("forget_seg", plies)
    # the frayed tip: one cone splitting into four thin ones fanning out
    tip = [cone("tipCone", (0, 0, -0.1), 0.08, 0.2, wool, rot=(math.pi, 0, 0), verts=12)]
    for k in range(4):
        a = k * math.pi / 2 + 0.4
        d = Vector((math.cos(a) * 0.35, math.sin(a) * 0.35, -1)).normalized()
        p0 = Vector((0, 0, -0.16))
        p1 = p0 + d * 0.24
        tip.append(cyl("fray%d" % k, tuple(p0), tuple(p1), 0.025, 0.004, [wool, woolL][k % 2], 8))
    export("forget_tip", tip)


reset()
gran()
reset()
sam()
reset()
clock()
reset()
forgetting()
print("done")
