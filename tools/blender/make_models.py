# make_models.py: builds the full 3D stage's Gran and the bat boss in Blender and exports
# each moving part as its own .glb into assets/models/. The game (src/render/Models3D.cpp)
# puts the parts together and animates them in code (run cycle, wing beats).
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


# ------------------------------------------------------------------ the bat (boss)
# About 2.6 units from wing tip to wing tip at rest. Faces +X. Pivot at the body centre.
def membrane(name, pts, material, thick=0.03):
    """A flat wing membrane through the given outline points (in the XZ plane at y=0)."""
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    vs = [bm.verts.new(Vector(p)) for p in pts]
    bm.faces.new(vs)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(o)
    bpy.context.view_layer.objects.active = o
    o.select_set(True)
    sol = o.modifiers.new("solid", "SOLIDIFY")
    sol.thickness = thick
    sol.offset = 0
    return finish(o, material, smooth=False)


def bat():
    fur = mat("fur", 0xc07a3a)
    furL = mat("furLight", 0xe0a060)
    furD = mat("furDark", 0x5a3426)
    ear = mat("ear", 0x3a2420)
    earIn = mat("earInner", 0x9a5a5a)
    nose = mat("nose", 0x2a1a1a)
    eye = mat("eye", 0x101018)
    glint = mat("glint", 0xffffff)
    claw = mat("claw", 0x2a1e1e)
    skinM = mat("wingSkin", 0x7a4a44)
    bone = mat("wingBone", 0x8a3a36)

    # body: a round furry body, a dark ruff, a fox-like face with tall ears
    parts = [
        sphere("body", (0, 0, 0), (0.55, 0.45, 0.5), fur, 24, 16),
        sphere("belly", (0.18, 0, -0.12), (0.38, 0.36, 0.36), furL, 20, 12),
        sphere("ruff", (0.2, 0, 0.22), (0.36, 0.42, 0.3), furD, 20, 12),
        sphere("rump", (-0.45, 0, -0.15), (0.3, 0.3, 0.25), fur, 16, 10),
        sphere("head", (0.5, 0, 0.32), (0.3, 0.3, 0.27), fur, 24, 16),
        sphere("mask", (0.62, 0, 0.3), (0.2, 0.24, 0.17), furD, 20, 12),
        sphere("snout", (0.8, 0, 0.26), (0.13, 0.1, 0.09), furL, 14, 10),
        sphere("nose", (0.92, 0, 0.28), (0.04, 0.05, 0.035), nose, 10, 8),
        cone("earL", (0.42, 0.17, 0.66), 0.11, 0.38, ear, rot=(-0.3, -0.15, 0)),
        cone("earR", (0.42, -0.17, 0.66), 0.11, 0.38, ear, rot=(0.3, -0.15, 0)),
        cone("earInL", (0.45, 0.17, 0.64), 0.07, 0.3, earIn, rot=(-0.3, -0.15, 0)),
        cone("earInR", (0.45, -0.17, 0.64), 0.07, 0.3, earIn, rot=(0.3, -0.15, 0)),
        sphere("eyeL", (0.74, 0.12, 0.36), (0.05, 0.045, 0.055), eye, 12, 8),
        sphere("eyeR", (0.74, -0.12, 0.36), (0.05, 0.045, 0.055), eye, 12, 8),
        sphere("glintL", (0.78, 0.13, 0.38), (0.015, 0.015, 0.015), glint, 6, 4),
        sphere("glintR", (0.78, -0.11, 0.38), (0.015, 0.015, 0.015), glint, 6, 4),
    ]
    # little legs and hooked feet hanging under the body
    for side in (1, -1):
        parts.append(cyl("leg", (-0.05, side * 0.18, -0.35), (0.0, side * 0.2, -0.62), 0.05, 0.035, furD, 8))
        for k in range(3):
            parts.append(cone("toe", (0.03 + k * 0.03, side * (0.16 + k * 0.03), -0.66), 0.02, 0.1, claw,
                              rot=(0, 2.4, 0)))
    export("bat_body", parts)

    # wings: an inner part (shoulder to wrist) and an outer hand with fingers.
    # Each is drawn for the left side (+Y); the right side is exported mirrored.
    for side, tag in ((1, "L"), (-1, "R")):
        y = side
        # inner wing: pivot at the shoulder; the arm bone runs out along +Y and back
        arm = cyl("arm", (0, 0, 0), (-0.05, 0.75 * y, 0.25), 0.05, 0.035, bone, 8)
        me = bpy.data.meshes.new("innerWeb")
        bm = bmesh.new()
        pts = [(0.02, 0, 0.02), (-0.05, 0.75 * y, 0.25), (-0.3, 0.72 * y, -0.05), (-0.55, 0.45 * y, -0.12),
               (-0.6, 0.15 * y, -0.18), (-0.35, 0.0, -0.1)]
        vs = [bm.verts.new(Vector(p)) for p in pts]
        bm.faces.new(vs if y > 0 else list(reversed(vs)))
        bmesh.ops.triangulate(bm, faces=bm.faces[:])
        bm.to_mesh(me)
        bm.free()
        web = bpy.data.objects.new("innerWeb", me)
        bpy.context.collection.objects.link(web)
        web.modifiers.new("solid", "SOLIDIFY").thickness = 0.03
        finish(web, skinM, smooth=False)
        export("bat_wing_in_" + tag, [arm, web])

        # outer wing (the hand): pivot at the wrist; three long fingers fan out with the web
        # between them, scalloped at the trailing edge; a hooked thumb claw at the wrist
        fingers = [((-0.1, 0.95 * y, 0.35), (-0.55, 0.9 * y, -0.05)),
                   ((-0.35, 0.95 * y, 0.05), (-0.75, 0.55 * y, -0.25)),
                   ((-0.35, 0.55 * y, -0.15), (-0.55, 0.15 * y, -0.4))]
        objs = []
        tips = []
        for i, (mid, tip) in enumerate(fingers):
            objs.append(cyl("f%da" % i, (0, 0, 0), mid, 0.03, 0.022, bone, 6))
            objs.append(cyl("f%db" % i, mid, tip, 0.022, 0.012, bone, 6))
            tips.append((mid, tip))
        objs.append(cone("thumb", (0.05, 0.05 * y, 0.08), 0.03, 0.14, claw, rot=(0, 0.6, 0)))
        objs.append(sphere("wrist", (0, 0, 0), (0.06, 0.06, 0.06), bone, 8, 6))
        # web: leading edge along finger 0, then each fingertip, with a scallop between
        outline = [(0.0, 0.0, 0.0), tips[0][0], tips[0][1]]
        for i in range(1, 3):
            a, b = Vector(tips[i - 1][1]), Vector(tips[i][1])
            dip = (a + b) / 2 + Vector((0.12, 0, 0.1))   # the scallop curves in toward the wrist
            outline += [tuple(dip), tips[i][1]]
        # back to the wrist past the inner wing's trailing edge
        outline += [(-0.2, 0.05 * y, -0.2)]
        me = bpy.data.meshes.new("outerWeb")
        bm = bmesh.new()
        vs = [bm.verts.new(Vector(p)) for p in outline]
        bm.faces.new(vs if y > 0 else list(reversed(vs)))
        bmesh.ops.triangulate(bm, faces=bm.faces[:])
        bm.to_mesh(me)
        bm.free()
        web = bpy.data.objects.new("outerWeb", me)
        bpy.context.collection.objects.link(web)
        web.modifiers.new("solid", "SOLIDIFY").thickness = 0.025
        finish(web, skinM, smooth=False)
        objs.append(web)
        export("bat_wing_out_" + tag, objs)


reset()
gran()
reset()
bat()
print("done")
