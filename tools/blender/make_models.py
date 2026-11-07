# make_models.py: builds the full 3D stage's Gran and the Forgetting (the boss) in Blender and exports
# each moving part as its own .glb into assets/models/. The game (src/render/Models3D.cpp)
# puts the parts together and animates them in code (run cycle, waving tentacles).
#
# Run with Blender:   blender --background --python tools/blender/make_models.py -- assets/models
# or with the bpy module:  python tools/blender/make_models.py assets/models
#
# Conventions (Blender, Z up): every part faces +X, its pivot (the joint it turns about) is at
# the origin. The glTF exporter turns Z-up into Y-up, which is what the game uses.
import math
import os
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
    return finish(o, material, smooth=bevel > 0)


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


# ------------------------------------------------------------------ Gran
# About 1.45 units tall. Hip joint at z 0.55 above the feet, shoulders at about 1.0.
def gran():
    coat = mat("coat", 0x9a3a32)        # warm red-brown cardigan coat
    coatD = mat("coatD", 0x6a2422)
    trous = mat("trousers", 0x4a4650)
    shoe = mat("shoe", 0x2a2024)
    skin = mat("skin", 0xf2c2a0)
    cheek = mat("cheek", 0xe89a8a)
    hair = mat("hair", 0xd8d6e0)
    hairD = mat("hairD", 0xa8a4b4)
    ink = mat("ink", 0x1c1420)
    lens = mat("glass", 0x3a3040)
    btn = mat("button", 0xf0e0c0)
    scarf = mat("scarf", 0xe0b040)

    # body: pivot at the hips (0, 0, 0); the coat flares a little at the hem
    parts = [
        cyl("torso", (0, 0, -0.16), (0, 0, 0.42), 0.21, 0.17, coat, 16),
        sphere("chest", (0.02, 0, 0.3), (0.19, 0.2, 0.17), coat),
        sphere("belly", (0.03, 0, 0.08), (0.2, 0.21, 0.18), coat),
        sphere("shoulders", (0, 0, 0.4), (0.15, 0.24, 0.08), coat),
        cyl("hem", (0, 0, -0.2), (0, 0, -0.1), 0.235, 0.215, coatD, 16),
        cyl("collar", (0, 0, 0.42), (0, 0, 0.5), 0.12, 0.1, scarf, 12),
        sphere("scarfKnot", (0.1, 0.03, 0.43), (0.06, 0.06, 0.05), scarf),
        cube("scarfEnd", (0.13, 0.05, 0.33), (0.03, 0.06, 0.14), scarf, 0.01, rot=(0.2, 0.2, 0)),
        cube("pocketL", (0.13, 0.15, -0.02), (0.06, 0.1, 0.08), coatD, 0.01, rot=(0, 0, 0.5)),
        cube("pocketR", (0.13, -0.15, -0.02), (0.06, 0.1, 0.08), coatD, 0.01, rot=(0, 0, -0.5)),
        cyl("placket", (0.19, 0, -0.18), (0.17, 0, 0.4), 0.012, 0.012, coatD, 6),
    ]
    for i in range(3):
        parts.append(sphere("btn%d" % i, (0.2 - i * 0.004, 0.025, 0.28 - i * 0.14), (0.025, 0.025, 0.025), btn, 8, 6))
    export("gran_body", parts)

    # head: pivot at the base of the neck
    parts = [
        cyl("neck", (0, 0, -0.02), (0, 0, 0.1), 0.06, 0.06, skin, 10),
        sphere("head", (0.02, 0, 0.22), (0.17, 0.165, 0.18), skin, 20, 14),
        sphere("nose", (0.185, 0, 0.2), (0.04, 0.035, 0.045), skin, 10, 8),
        sphere("cheekL", (0.15, 0.09, 0.16), (0.035, 0.03, 0.025), cheek, 8, 6),
        sphere("cheekR", (0.15, -0.09, 0.16), (0.035, 0.03, 0.025), cheek, 8, 6),
        sphere("earL", (0.0, 0.165, 0.21), (0.03, 0.02, 0.045), skin, 8, 6),
        sphere("earR", (0.0, -0.165, 0.21), (0.03, 0.02, 0.045), skin, 8, 6),
        # hair: a cap swept back, curls over the ears, and a big bun on top
        sphere("hairCap", (-0.02, 0, 0.28), (0.17, 0.175, 0.15), hair, 20, 12),
        sphere("hairBack", (-0.09, 0, 0.2), (0.12, 0.17, 0.15), hair, 16, 10),
        sphere("curlL", (0.03, 0.15, 0.3), (0.06, 0.05, 0.05), hair, 10, 8),
        sphere("curlR", (0.03, -0.15, 0.3), (0.06, 0.05, 0.05), hair, 10, 8),
        sphere("fringe", (0.1, 0, 0.36), (0.09, 0.14, 0.05), hair, 14, 8),
        sphere("bun", (-0.06, 0, 0.45), (0.1, 0.1, 0.085), hair, 14, 10),
        torus("bunBand", (-0.06, 0, 0.4), 0.07, 0.015, hairD),
        # glasses: two round rims, a bridge and the arms back to the ears
        torus("rimL", (0.175, 0.06, 0.24), 0.04, 0.008, lens, rot=(0, math.pi / 2, 0)),
        torus("rimR", (0.175, -0.06, 0.24), 0.04, 0.008, lens, rot=(0, math.pi / 2, 0)),
        cyl("bridge", (0.18, 0.025, 0.25), (0.18, -0.025, 0.25), 0.007, 0.007, lens, 6),
        cyl("armL", (0.17, 0.1, 0.25), (0.0, 0.165, 0.24), 0.006, 0.006, lens, 6),
        cyl("armR", (0.17, -0.1, 0.25), (0.0, -0.165, 0.24), 0.006, 0.006, lens, 6),
        sphere("eyeL", (0.175, 0.06, 0.235), (0.012, 0.016, 0.02), ink, 8, 6),
        sphere("eyeR", (0.175, -0.06, 0.235), (0.012, 0.016, 0.02), ink, 8, 6),
        cyl("mouth", (0.17, 0.03, 0.135), (0.17, -0.03, 0.135), 0.008, 0.008, ink, 6),
    ]
    export("gran_head", parts)

    # upper arm: pivot at the shoulder, hanging down -Z
    export("gran_upperarm", [
        sphere("shoulderCap", (0, 0, -0.01), (0.075, 0.075, 0.075), coat, 12, 8),
        cyl("upper", (0, 0, 0), (0, 0, -0.21), 0.068, 0.06, coat, 12),
    ])
    # forearm with cuff and hand: pivot at the elbow
    export("gran_forearm", [
        sphere("elbow", (0, 0, 0), (0.06, 0.06, 0.06), coat, 12, 8),
        cyl("lower", (0, 0, 0), (0, 0, -0.17), 0.06, 0.055, coat, 12),
        cyl("cuff", (0, 0, -0.15), (0, 0, -0.19), 0.062, 0.062, coatD, 12),
        sphere("hand", (0.01, 0, -0.24), (0.05, 0.04, 0.06), skin, 12, 8),
        sphere("thumb", (0.04, 0.03, -0.22), (0.02, 0.018, 0.03), skin, 8, 6),
    ])
    # thigh: pivot at the hip
    export("gran_thigh", [
        cyl("thigh", (0, 0, 0), (0, 0, -0.26), 0.085, 0.07, trous, 12),
    ])
    # shin with shoe: pivot at the knee
    export("gran_shin", [
        sphere("knee", (0, 0, 0), (0.07, 0.07, 0.07), trous, 12, 8),
        cyl("shin", (0, 0, 0), (0, 0, -0.24), 0.07, 0.06, trous, 12),
        sphere("shoe", (0.05, 0, -0.27), (0.11, 0.065, 0.045), shoe, 14, 8),
        cube("sole", (0.05, 0, -0.305), (0.22, 0.12, 0.02), mat("sole", 0xe8e0d0), 0.008),
    ])


# ------------------------------------------------------------------ the Forgetting (boss)
# A big floating tentacle alien: a bulbous dome with one huge eye, two small ones and a mouth,
# and eight long tentacles that hang and curl below it. Faces +X. Pivot at the dome's centre.
# Each tentacle is a chain of the same segment (pivot at its top), bent and waved in code.
def forgetting():
    skin = mat("fSkin", 0x3a2a6e)
    skinL = mat("fSkinLight", 0x6a4ab0)
    skinD = mat("fSkinDark", 0x221a46)
    under = mat("fUnder", 0xb08ac8)
    sclera = mat("fSclera", 0xf4eedc)
    iris = mat("fIris", 0xf7d73a)
    pupil = mat("fPupil", 0x0f0d1a)
    lid = mat("fLid", 0x2a1f58)
    mouthM = mat("fMouth", 0x1a0f22)
    tooth = mat("fTooth", 0xf4eedc)
    glow = mat("fGlow", 0x7fe8ff)
    sucker = mat("fSucker", 0xd8b4e8)

    parts = [
        sphere("dome", (0, 0, 0.1), (0.95, 0.9, 0.8), skin, 32, 20),
        sphere("brow", (0.35, 0, 0.45), (0.55, 0.7, 0.35), skinD, 24, 14),
        sphere("skirt", (0, 0, -0.42), (0.92, 0.88, 0.22), skinD, 32, 12),
        sphere("underside", (0, 0, -0.5), (0.7, 0.68, 0.16), under, 28, 10),
        # the huge eye with heavy lids
        sphere("eyeWhite", (0.68, 0, 0.12), (0.34, 0.38, 0.36), sclera, 24, 16),
        sphere("iris", (0.93, 0, 0.12), (0.12, 0.24, 0.24), iris, 20, 14),
        sphere("pupil", (1.02, 0, 0.12), (0.06, 0.06, 0.18), pupil, 12, 10),
        sphere("glintBig", (1.03, 0.08, 0.24), (0.03, 0.04, 0.04), sclera, 8, 6),
        sphere("lidTop", (0.66, 0, 0.36), (0.36, 0.44, 0.14), lid, 20, 10, rot=(0, -0.25, 0)),
        sphere("lidBottom", (0.68, 0, -0.14), (0.32, 0.4, 0.1), lid, 20, 10, rot=(0, 0.25, 0)),
        # mouth: a wide dark slit below the eye with little teeth
        sphere("mouth", (0.72, 0, -0.32), (0.18, 0.42, 0.07), mouthM, 20, 8),
    ]
    for k in range(5):
        y = -0.28 + k * 0.14
        parts.append(cone("tooth%d" % k, (0.84, y, -0.27), 0.03, 0.08, tooth, rot=(math.pi, 0, 0)))
    # two small eyes on stalks, one each side, looking about
    for side in (1, -1):
        parts.append(cyl("stalk", (0.3, side * 0.55, 0.55), (0.45, side * 0.75, 0.95), 0.05, 0.04, skin, 10))
        parts.append(sphere("smallEye", (0.47, side * 0.78, 1.0), (0.1, 0.1, 0.1), sclera, 14, 10))
        parts.append(sphere("smallPupil", (0.56, side * 0.8, 1.0), (0.03, 0.045, 0.045), pupil, 8, 6))
    # glowing antennae on top
    for side in (1, -1):
        parts.append(cyl("antenna", (-0.2, side * 0.25, 0.75), (-0.35, side * 0.4, 1.25), 0.035, 0.02, skinD, 8))
        parts.append(sphere("antennaTip", (-0.36, side * 0.41, 1.29), (0.07, 0.07, 0.07), glow, 12, 8))
    # light spots and bumps over the dome
    import random
    rng = random.Random(7)
    for k in range(16):
        th = rng.uniform(0, 2 * math.pi)
        ph = rng.uniform(0.25, 1.2)
        if math.cos(th) > 0.55 and ph > 0.6:
            continue   # keep the face clear
        x, y, z = 0.95 * math.cos(th) * math.sin(ph), 0.9 * math.sin(th) * math.sin(ph), 0.1 + 0.8 * math.cos(ph)
        r = rng.uniform(0.06, 0.13)
        parts.append(sphere("spot%d" % k, (x, y, z), (r, r, r * 0.6), skinL if k % 3 else skinD, 10, 8))
    export("forget_body", parts)

    # one tentacle segment: pivot at the top, hanging down -Z, 0.38 long, suckers on the front
    seg = [cyl("seg", (0, 0, 0.02), (0, 0, -0.4), 0.13, 0.11, skin, 14),
           sphere("joint", (0, 0, 0), (0.13, 0.13, 0.1), skin, 14, 8)]
    for k in range(2):
        seg.append(sphere("sucker%d" % k, (0.11, 0, -0.1 - k * 0.17), (0.03, 0.06, 0.06), sucker, 10, 8))
    seg.append(cyl("stripe", (0, 0, -0.18), (0, 0, -0.22), 0.125, 0.12, skinL, 14))
    export("forget_seg", seg)
    # the tentacle tip: a tapering curl
    export("forget_tip", [
        sphere("joint", (0, 0, 0), (0.11, 0.11, 0.08), skin, 12, 8),
        cyl("tipA", (0, 0, 0), (0.0, 0, -0.25), 0.11, 0.07, skin, 12),
        cyl("tipB", (0.0, 0, -0.25), (0.12, 0, -0.42), 0.07, 0.025, skin, 10),
        sphere("tipSucker", (0.08, 0, -0.18), (0.025, 0.05, 0.05), sucker, 8, 6),
    ])


reset()
gran()
reset()
forgetting()
print("done")
