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
#
# Pipeline per part (export()): shading (smooth, or smooth by angle 30 degrees for hard-surface
# parts), bevels (0.006, 3 segments, angle limited, hardened normals), join, merge by distance,
# Smart UV Project, then two Cycles bakes:
#   - ambient occlusion (64 samples, distance 0.3) into the alpha of the colour attribute "Col",
#     whose rgb holds painted colour (the jeans' light wash, the wool's tones; white elsewhere).
#     It is exported as COLOR_0; the game's lit3d shader multiplies rgb into the base colour
#     and uses alpha as AO.
#   - base colour textures for the materials that have a procedural recipe (walnut grain,
#     the ivory dial's age mottle), packed into the .glb.
# Metallic/roughness are real PBR values; the game draws a stepped toon highlight on metals.
# The emissive colour (the Forgetting's cyan eye ring) is exported and drawn unlit.
import math
import os
import random
import sys

import bpy
import bmesh
from mathutils import Matrix, Vector, noise

OUT = "assets/models"
args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
if args:
    OUT = args[0]
os.makedirs(OUT, exist_ok=True)
FAST = os.environ.get("MODELS_FAST") == "1"   # quick preview builds: fewer bake samples

ANGLE = math.radians(30)


# ------------------------------------------------------------------ helpers
def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    MATS.clear()


MATS = {}
RECIPES = {}   # material name -> (bake function, texture size)


def mat(name, hexcol, metal=0.0, rough=0.8, emit=False, base=None):
    """A material. Colours are stored as the sRGB values picked (the game reads the factors as
    bytes). metal/rough are real PBR values; emit exports the colour as emissive too."""
    if name in MATS:
        return MATS[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    r, g, b = rgb(hexcol)
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (r, g, b, 1)
    bsdf.inputs["Metallic"].default_value = metal
    bsdf.inputs["Roughness"].default_value = rough
    if emit:
        bsdf.inputs["Emission Color"].default_value = (r, g, b, 1)
        bsdf.inputs["Emission Strength"].default_value = 1.0
    m.diffuse_color = (r, g, b, 1)
    MATS[name] = m
    return m


def base_of(m):
    return tuple(m.node_tree.nodes.get("Principled BSDF").inputs["Base Color"].default_value)[:3]


def finish(obj, material, shade="smooth"):
    """shade: "smooth" (organic), "angle" (smooth by angle 30 degrees) or "flat"."""
    obj.data.materials.clear()
    obj.data.materials.append(material)
    obj["shade"] = shade
    return obj


def bevel(obj, width=0.006):
    m = obj.modifiers.new("bev", "BEVEL")
    m.width = width
    m.segments = 3
    m.limit_method = "ANGLE"
    m.angle_limit = ANGLE
    m.harden_normals = True
    m.use_clamp_overlap = True
    return obj


def sphere(name, loc, scale, material, seg=16, rings=10, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=seg, ring_count=rings, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    o.scale = scale
    return finish(o, material)


def icosphere(name, loc, scale, material, subdiv=2):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=subdiv, radius=1, location=loc)
    o = bpy.context.active_object
    o.name = name
    o.scale = scale
    return finish(o, material)


def cyl(name, a, b, r1, r2, material, verts=12, shade="angle"):
    """A tapered cylinder from point a to point b."""
    a, b = Vector(a), Vector(b)
    d = b - a
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r1, radius2=r2, depth=d.length,
                                    location=(a + b) / 2)
    o = bpy.context.active_object
    o.name = name
    o.rotation_mode = "QUATERNION"
    o.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(d.normalized())
    return finish(o, material, shade)


def cube(name, loc, scale, material, bevel_w=0.006, rot=(0, 0, 0)):
    """A box, scale applied, bevelled (width clamped to the box's size)."""
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    o.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    w = min(bevel_w, 0.3 * min(scale))
    if w > 0.0005:
        bevel(o, w)
    return finish(o, material, "angle")


def torus(name, loc, r_major, r_minor, material, rot=(0, 0, 0), seg=16, minor=6):
    bpy.ops.mesh.primitive_torus_add(major_radius=r_major, minor_radius=r_minor, major_segments=seg,
                                     minor_segments=minor, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    return finish(o, material)


def cone(name, loc, r, depth, material, rot=(0, 0, 0), verts=10):
    bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r, radius2=0, depth=depth, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    return finish(o, material, "angle")


def new_obj(name, bm, material, shade="smooth"):
    me = bpy.data.meshes.new(name)
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(o)
    return finish(o, material, shade)


def tube(name, r, length, material, sides=12, rings=8, off=(0, 0), r2=None, z0=0.0):
    """A straight tube hanging from z0 down to z0-length (tapering to r2), with rings so it can
    be twisted or bent."""
    r2 = r if r2 is None else r2
    bm = bmesh.new()
    rows = []
    for i in range(rings + 1):
        t = i / rings
        z = z0 - length * t
        rr = r + (r2 - r) * t
        rows.append([bm.verts.new((off[0] + rr * math.cos(2 * math.pi * k / sides),
                                   off[1] + rr * math.sin(2 * math.pi * k / sides), z)) for k in range(sides)])
    for i in range(rings):
        for k in range(sides):
            a, b = rows[i][k], rows[i][(k + 1) % sides]
            c, d = rows[i + 1][(k + 1) % sides], rows[i + 1][k]
            bm.faces.new((a, d, c, b))
    bm.faces.new(rows[0])
    bm.faces.new(list(reversed(rows[-1])))
    return new_obj(name, bm, material, "angle")


def lathe(name, profile, material, seg=24, sx=1.0):
    """A body of revolution about Z from (z, r) pairs, bottom to top; depth along X is sx * r."""
    bm = bmesh.new()
    rows = []
    for z, r in profile:
        rows.append([bm.verts.new((sx * r * math.cos(2 * math.pi * k / seg), r * math.sin(2 * math.pi * k / seg), z))
                     for k in range(seg)])
    for i in range(len(rows) - 1):
        for k in range(seg):
            bm.faces.new((rows[i][k], rows[i][(k + 1) % seg], rows[i + 1][(k + 1) % seg], rows[i + 1][k]))
    bm.faces.new(list(reversed(rows[0])))
    bm.faces.new(rows[-1])
    return new_obj(name, bm, material, "angle")


def sweep(name, pts, radius, material, sides=6, caps=True, flat=1.0, up=(0, 0, 1)):
    """A tube along a polyline (parallel-transported frames). radius: number or list per point;
    flat squashes the section along the frame's second axis."""
    pts = [Vector(p) for p in pts]
    n = len(pts)
    rad = radius if isinstance(radius, (list, tuple)) else [radius] * n
    tans = []
    for i in range(n):
        a, b = pts[max(i - 1, 0)], pts[min(i + 1, n - 1)]
        tans.append((b - a).normalized())
    N = Vector(up).cross(tans[0])
    if N.length < 1e-4:
        N = Vector((1, 0, 0)).cross(tans[0])
    N.normalize()
    bm = bmesh.new()
    rows = []
    for i in range(n):
        T = tans[i]
        N = (N - T * N.dot(T)).normalized()
        B = T.cross(N)
        rows.append([bm.verts.new(pts[i] + rad[i] * (math.cos(2 * math.pi * k / sides) * N +
                                                     flat * math.sin(2 * math.pi * k / sides) * B))
                     for k in range(sides)])
    for i in range(n - 1):
        for k in range(sides):
            bm.faces.new((rows[i][k], rows[i][(k + 1) % sides], rows[i + 1][(k + 1) % sides], rows[i + 1][k]))
    if caps:
        bm.faces.new(list(reversed(rows[0])))
        bm.faces.new(rows[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return new_obj(name, bm, material)


def prism(name, outline, depth, x, material, plane="YZ"):
    """A flat shape drawn in one plane, given `depth` across it and centred on x.
    plane "YZ": (y, z) points seen from +X (hands, swan neck); "XZ": (x, z) points seen from the
    side (the bracket feet), extruded along Y."""
    bm = bmesh.new()
    if plane == "YZ":
        front = [bm.verts.new((x + depth / 2, a, b)) for (a, b) in outline]
        back = [bm.verts.new((x - depth / 2, a, b)) for (a, b) in outline]
    else:
        front = [bm.verts.new((a, x + depth / 2, b)) for (a, b) in outline]
        back = [bm.verts.new((a, x - depth / 2, b)) for (a, b) in outline]
    n = len(outline)
    bm.faces.new(front)
    bm.faces.new(list(reversed(back)))
    for k in range(n):
        bm.faces.new((front[k], back[k], back[(k + 1) % n], front[(k + 1) % n]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return new_obj(name, bm, material, "angle")


def curve_tube(name, pts, radius, material, res=1):
    """A bevelled Bezier curve (auto handles) through pts, converted to a mesh."""
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = radius
    cu.bevel_resolution = res
    cu.resolution_u = 6
    cu.use_fill_caps = True
    sp = cu.splines.new("BEZIER")
    sp.bezier_points.add(len(pts) - 1)
    for bp, p in zip(sp.bezier_points, pts):
        bp.co = p
        bp.handle_left_type = bp.handle_right_type = "AUTO"
    o = bpy.data.objects.new(name, cu)
    bpy.context.collection.objects.link(o)
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.convert(target="MESH")
    o = bpy.context.active_object
    return finish(o, material)


def cut(obj, cutter):
    """Boolean: obj minus cutter; the cutter is removed. New faces keep the cutter's material."""
    for m in list(obj.modifiers):   # apply pending modifiers first so the cut sees the real shape
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=m.name)
    m = obj.modifiers.new("cut", "BOOLEAN")
    m.operation = "DIFFERENCE"
    m.object = cutter
    m.solver = "EXACT"
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


def lerp3(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def paint(obj, fn):
    """Vertex colour: fn(local co, local normal) -> the colour wanted (sRGB values like the
    materials). Stored as a multiplier of the material's base colour in the "Col" attribute."""
    me = obj.data
    base = base_of(obj.data.materials[0])
    if "Col" not in me.color_attributes:
        me.color_attributes.new("Col", "FLOAT_COLOR", "CORNER")
    at = me.color_attributes["Col"]
    me.calc_loop_triangles()
    for poly in me.polygons:
        for li in poly.loop_indices:
            v = me.vertices[me.loops[li].vertex_index]
            c = fn(v.co, v.normal)
            at.data[li].color = tuple(min(1.0, c[i] / max(base[i], 1e-3)) for i in range(3)) + (1.0,)
    return obj


def shade(o):
    mode = o.get("shade", "smooth")
    me = o.data
    for p in me.polygons:
        p.use_smooth = mode != "flat"
    if mode == "angle":
        me.set_sharp_from_angle(angle=ANGLE + 0.01)


def bake_setup():
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = 8 if FAST else 64
    if sc.world is None:
        sc.world = bpy.data.worlds.new("W")
    sc.world.light_settings.distance = 0.3


def bake_ao(o):
    """AO into the alpha of Col (rgb keeps the painted colour)."""
    me = o.data
    ao = me.color_attributes.new("AO", "FLOAT_COLOR", "CORNER")
    me.color_attributes.active_color = ao
    bpy.context.scene.cycles.samples = 8 if FAST else 64
    bpy.ops.object.bake(type="AO", target="VERTEX_COLORS")
    col = me.color_attributes["Col"]
    n = len(col.data)
    a, c = [0.0] * (4 * n), [0.0] * (4 * n)
    me.color_attributes["AO"].data.foreach_get("color", a)
    col.data.foreach_get("color", c)
    for i in range(n):
        c[4 * i + 3] = a[4 * i]
    col.data.foreach_set("color", c)
    me.color_attributes.remove(me.color_attributes["AO"])
    me.color_attributes.active_color = me.color_attributes["Col"]
    me.color_attributes.render_color_index = me.color_attributes.active_color_index


def bake_textures(o):
    """Bakes each material that has a procedural recipe into an image packed with the model."""
    mats = [m for m in o.data.materials if m and m.name in RECIPES]
    if not mats:
        return
    dummy = bpy.data.images.new("dummy", 8, 8)
    added = []
    for m in o.data.materials:
        nt = m.node_tree
        img_node = nt.nodes.new("ShaderNodeTexImage")
        if m in mats:
            fn, size = RECIPES[m.name]
            img = bpy.data.images.new("tex_" + m.name, size, size, alpha=False)
            img.colorspace_settings.name = "Non-Color"   # store the bytes as baked, like the factors
            img_node.image = img
            fn(nt)
        else:
            img_node.image = dummy
            added.append((nt, img_node))
        nt.nodes.active = img_node
        img_node.select = True
    sc = bpy.context.scene
    sc.cycles.samples = 4
    sc.render.bake.margin = 4
    bpy.ops.object.bake(type="DIFFUSE", pass_filter={"COLOR"}, target="IMAGE_TEXTURES", margin=4)
    for nt, node in added:
        nt.nodes.remove(node)
    for m in mats:
        nt = m.node_tree
        node = nt.nodes.active
        node.image.pack()
        bsdf = nt.nodes.get("Principled BSDF")
        for l in list(bsdf.inputs["Base Color"].links):
            nt.links.remove(l)
        bsdf.inputs["Base Color"].default_value = (1, 1, 1, 1)
        nt.links.new(node.outputs["Color"], bsdf.inputs["Base Color"])
        for nd in list(nt.nodes):   # drop the procedural nodes
            if nd not in (node, bsdf) and nd.type != "OUTPUT_MATERIAL":
                nt.nodes.remove(nd)


TRIS = {}


def export(name, objs, ao=True):
    """Shade, apply modifiers and transforms, join (keeping materials), clean, UV, bake, export."""
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        bpy.context.view_layer.objects.active = o
        o.select_set(True)
        shade(o)
        for m in list(o.modifiers):
            bpy.ops.object.modifier_apply(modifier=m.name)
        me = o.data
        if "Col" not in me.color_attributes:
            at = me.color_attributes.new("Col", "FLOAT_COLOR", "CORNER")
            at.data.foreach_set("color", [1.0] * (4 * len(at.data)))
        for uv in list(me.uv_layers):
            me.uv_layers.remove(uv)
    for o in objs:
        o.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    o = bpy.context.active_object
    o.name = name
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    # merge by distance, then Smart UV Project
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.remove_doubles(threshold=0.0001)
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.02)
    bpy.ops.object.mode_set(mode="OBJECT")
    me = o.data
    me.color_attributes.active_color = me.color_attributes["Col"]
    if ao:
        bake_ao(o)
    bake_textures(o)
    me.calc_loop_triangles()
    TRIS[name] = len(me.loop_triangles)
    path = os.path.join(OUT, name + ".glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True,
                              export_apply=True, export_yup=True, export_normals=True,
                              export_texcoords=True, export_materials="EXPORT",
                              export_vertex_color="ACTIVE", export_all_vertex_colors=False,
                              export_animations=False)
    print("wrote", path, TRIS[name], "tris")
    bpy.ops.object.delete()


# ------------------------------------------------------------------ faces (Gran and Sam)
class Head:
    """The head ellipsoid (centre, radii), for putting features on its surface."""

    def __init__(self, c, r):
        self.c, self.r = Vector(c), Vector(r)

    def pt(self, y, z, off=0.0):
        c, r = self.c, self.r
        q = 1 - (y / r.y) ** 2 - ((z - c.z) / r.z) ** 2
        p = Vector((c.x + r.x * math.sqrt(max(q, 0.0)), y, z))
        n = Vector(((p.x - c.x) / r.x ** 2, y / r.y ** 2, (z - c.z) / r.z ** 2)).normalized()
        return p + n * off, n

    def inside(self, p, k=1.0):
        d = Vector(p) - self.c
        return (d.x / self.r.x) ** 2 + (d.y / self.r.y) ** 2 + (d.z / self.r.z) ** 2 < k * k


def on_face(name, head, y, z, off, scale, material, seg=10, rings=6, dy=0.0, dz=0.0):
    """A small ellipsoid on the face, its X axis along the surface normal."""
    p, n = head.pt(y, z, off)
    o = sphere(name, p + Vector((0, dy, dz)), scale, material, seg, rings)
    o.rotation_mode = "QUATERNION"
    o.rotation_quaternion = Vector((1, 0, 0)).rotation_difference(n)
    return o


def head_mesh(name, head, nose, material, seg=24, rings=16):
    """The head ellipsoid with the nose pulled out of it: (z, height, pull, width)."""
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=seg, v_segments=rings, radius=1.0)
    nz, pull, w = nose
    for v in bm.verts:
        p = Vector((head.c.x + v.co.x * head.r.x, v.co.y * head.r.y, head.c.z + v.co.z * head.r.z))
        if v.co.x > 0:
            p.x += pull * math.exp(-(p.y ** 2 + (p.z - nz) ** 2) / (2 * w * w)) * v.co.x
        v.co = p
    return new_obj(name, bm, material)


def face(head, skin, cheek, ink, iris_col, eye_y, eye_z, mouth_z, mouth_w, brow_col, nose):
    """Eyes (white, iris, pupil, catchlight), a smiling Bezier mouth, cheeks and a nose tip."""
    white = mat("eyeWhite", 0xf6f2ea, rough=0.3)
    iris = mat("iris_%06x" % iris_col, iris_col, rough=0.3)
    shine = mat("catchlight", 0xffffff, rough=0.2)
    out = []
    for s in (1, -1):
        y = s * eye_y
        out.append(on_face("sclera", head, y, eye_z, -0.004, (0.012, 0.022, 0.026), white, 10, 6))
        out.append(on_face("iris", head, y, eye_z, 0.006, (0.006, 0.013, 0.015), iris, 8, 5))
        out.append(on_face("pupil", head, y, eye_z, 0.0095, (0.004, 0.007, 0.008), ink, 6, 4))
        out.append(on_face("catch", head, y + 0.004, eye_z + 0.006, 0.0105, (0.003, 0.003, 0.003), shine, 6, 4))
        out.append(on_face("cheek", head, s * 0.095, mouth_z + 0.035, -0.024, (0.036, 0.046, 0.036), cheek, 10, 6))
        a, _ = head.pt(s * 0.09, eye_z + 0.06, 0.004)
        b, _ = head.pt(s * 0.035, eye_z + 0.067, 0.004)
        out.append(cyl("brow", a, b, 0.009, 0.009, brow_col, 6, shade="smooth"))
    pts = []
    for k in range(3):
        y = (k - 1) * mouth_w
        z = mouth_z + (0.012 if k != 1 else -0.004)
        p, _ = head.pt(y, z, -0.002)
        pts.append(p)
    out.append(curve_tube("mouth", pts, 0.0075, mat("mouth", 0x6a2a2a, rough=0.5)))
    nz, pull, _w = nose
    p, _ = head.pt(0.0, nz, pull - 0.01)
    out.append(sphere("noseTip", p, (0.032, 0.03, 0.036), skin, 12, 8))
    return out


def perm_shell(name, centre, radii, head, material, subdiv=3, strength=0.025, size=0.06, keep=None):
    """A displaced, subdivided shell for a perm: icosphere, lumps along the normals (Clouds-like
    fractal noise, strength and size as in a Displace modifier), faces hidden in the head dropped."""
    bm = bmesh.new()
    bmesh.ops.create_icosphere(bm, subdivisions=subdiv, radius=1.0)
    c, r = Vector(centre), Vector(radii)
    for v in bm.verts:
        u = v.co.normalized()
        p = Vector((c.x + u.x * r.x, c.y + u.y * r.y, c.z + u.z * r.z))
        n = Vector((u.x / r.x, u.y / r.y, u.z / r.z)).normalized()
        cl = 0.5 + 0.5 * noise.fractal(p / size, 1.0, 2.0, 3)   # 0..1 like Clouds
        v.co = p + n * strength * (cl - 0.3)
    dead = [f for f in bm.faces if (head and head.inside(f.calc_center_median(), 0.97)) or
            (keep and not keep(f.calc_center_median()))]
    bmesh.ops.delete(bm, geom=dead, context="FACES")
    return new_obj(name, bm, material)


# ------------------------------------------------------------------ Gran
# About 1.5 units tall. Hip joint 0.55 above the feet, shoulders 0.4 above the hips. The
# camera sees her about 96 pixels tall, so she is built from a few big readable shapes: a big
# head (1.3x), curly white perm and bun, bright round glasses, the red cardigan and her scarf.
HEAD_K = 1.3


def gran():
    coat = mat("coat", 0x9a3a32, rough=0.9)        # warm red-brown cardigan coat (wool)
    coatD = mat("coatD", 0x6a2422, rough=0.9)
    trous = mat("trousers", 0x4a4650, rough=0.8)
    shoe = mat("shoe", 0x2a2024, rough=0.5)
    skin = mat("skin", 0xf2c2a0, rough=0.55)
    cheek = mat("cheek", 0xf0b4a0, rough=0.55)
    hair = mat("hair", 0xe4e2ec, rough=0.7)
    hairD = mat("hairD", 0xb4b0c4, rough=0.7)
    ink = mat("ink", 0x1c1420, rough=0.4)
    rims = mat("glassRims", 0x2f6fe0, rough=0.35)   # bright blue: reads as two rings against her skin
    scarf = mat("scarf", 0xe0b040, rough=0.9)

    # body: pivot at the hips. A cardigan with a waist: hem, waist, bust, shoulders.
    prof = [(-0.22, 0.245), (-0.15, 0.236), (-0.05, 0.215), (0.06, 0.188), (0.17, 0.205),
            (0.28, 0.2), (0.35, 0.18), (0.4, 0.12), (0.42, 0.05)]
    sx = 0.9
    plk = [(sx * r + 0.004, 0.0, z) for z, r in prof[:-2]]
    parts = [
        lathe("torso", prof, coat, 24, sx),
        sphere("shoulders", (0, 0, 0.36), (0.16, 0.25, 0.09), coat, 24, 10),
        cyl("hem", (0, 0, -0.225), (0, 0, -0.13), 0.252, 0.242, coatD, 24),
        cyl("collar", (0, 0, 0.4), (0, 0, 0.5), 0.12, 0.1, scarf, 16),
        sphere("scarfKnot", (0.11, 0.03, 0.41), (0.065, 0.065, 0.055), scarf, 12, 8),
        cube("scarfEnd", (0.17, 0.05, 0.27), (0.03, 0.07, 0.2), scarf, rot=(0.15, 0.25, 0)),
        cube("pocketL", (0.185, 0.13, -0.07), (0.04, 0.1, 0.09), coatD, rot=(0, 0, 0.5)),
        cube("pocketR", (0.185, -0.13, -0.07), (0.04, 0.1, 0.09), coatD, rot=(0, 0, -0.5)),
        sweep("placket", plk, 0.012, coatD, 6),
    ]
    export("gran_body", parts)

    # head: pivot at the base of the neck; everything above the neck is 1.3x
    H = Head((0.02, 0, 0.22), (0.17, 0.165, 0.18))
    nose = (0.205, 0.03, 0.03)
    head = [head_mesh("head", H, nose, skin)]
    head += face(H, skin, cheek, ink, 0x4a6a9a, 0.06, 0.235, 0.14, 0.04, hairD, nose)
    head += [
        sphere("earL", (0.0, 0.165, 0.21), (0.03, 0.02, 0.045), skin, 8, 6),
        sphere("earR", (0.0, -0.165, 0.21), (0.03, 0.02, 0.045), skin, 8, 6),
        # the perm: one lumpy shell over the top and back, the ears and face left clear
        perm_shell("perm", (-0.03, 0, 0.25), (0.188, 0.185, 0.182), H, hair, 3,
                   strength=0.03, size=0.08,
                   keep=lambda p: not (p.x > -0.06 and p.z < 0.33 - 0.5 * max(0.0, 0.12 - p.x)) and
                   not (abs(p.y) > 0.12 and 0.16 < p.z < 0.25 and -0.05 < p.x < 0.05)),
        perm_shell("bun", (-0.08, 0, 0.45), (0.1, 0.1, 0.085), None, hair, 3, 0.025, 0.07),
        torus("bunBand", (-0.08, 0, 0.395), 0.07, 0.016, hairD, seg=12, minor=5),
    ]
    # glasses: two bright rims just in front of the eyes, the bridge resting on the nose, the arms
    for s in (1, -1):
        head.append(torus("rim", (0.196, s * 0.062, 0.24), 0.046, 0.008, rims,
                          rot=(0, math.pi / 2, s * 0.22), seg=16, minor=5))
        head.append(cyl("arm", (0.18, s * 0.106, 0.25), (0.0, s * 0.168, 0.24), 0.006, 0.006, rims, 6, "smooth"))
    head.append(sweep("bridge", [(0.2, 0.018, 0.245), (0.203, 0.0, 0.252), (0.2, -0.018, 0.245)], 0.006, rims, 6))
    scale_about(head, (0, 0, 0.08), HEAD_K)
    export("gran_head", [cyl("neck", (0, 0, -0.02), (0, 0, 0.12), 0.06, 0.06, skin, 12, "smooth")] + head)

    # upper arm: pivot at the shoulder, hanging down -Z
    export("gran_upperarm", [
        sphere("shoulderCap", (0, 0, -0.01), (0.075, 0.075, 0.075), coat, 12, 8),
        tube("upper", 0.068, 0.21, coat, 16, 3, r2=0.06),
    ])
    # forearm with cuff and hand (palm, four fingers, thumb): pivot at the elbow
    export("gran_forearm", [
        tube("lower", 0.06, 0.2, coat, 16, 3, r2=0.055, z0=0.03),
        cyl("cuff", (0, 0, -0.15), (0, 0, -0.19), 0.064, 0.064, coatD, 16),
    ] + hand(skin))
    # thigh: pivot at the hip
    export("gran_thigh", [tube("thigh", 0.085, 0.3, trous, 16, 5, r2=0.072, z0=0.02)])
    # shin with shoe: pivot at the knee
    export("gran_shin", [
        tube("shin", 0.072, 0.27, trous, 16, 4, r2=0.06, z0=0.03),
        sphere("shoe", (0.05, 0, -0.27), (0.11, 0.065, 0.045), shoe, 16, 8),
        cube("sole", (0.05, 0, -0.305), (0.22, 0.12, 0.02), mat("sole", 0xe8e0d0, rough=0.8)),
    ])


def hand(skin):
    """A palm, four short fingers curling forward and a thumb (pivot at the elbow above)."""
    out = [sphere("palm", (0.01, 0, -0.228), (0.045, 0.034, 0.048), skin, 12, 8)]
    for k in range(4):
        y = -0.021 + 0.014 * k
        out.append(cyl("finger", (0.014, y, -0.255), (0.026, y, -0.292 + 0.004 * abs(k - 1.5)),
                       0.009, 0.0075, skin, 6, "smooth"))
    out.append(cyl("thumb", (0.03, 0.026, -0.212), (0.056, 0.03, -0.246), 0.011, 0.009, skin, 6, "smooth"))
    return out


# ------------------------------------------------------------------ Sam
# Gran's grandson, built to the same standard and on the same skeleton as Gran (so the game
# animates both with one run cycle): blue hoodie with the hood down, light-wash jeans, white
# trainers, brown hair under a teal cap worn forward. Head 1.3x like Gran's.
def sam():
    top = mat("hoodie", 0x3c8cf0, rough=0.9)
    topD = mat("hoodieD", 0x2a64b4, rough=0.9)
    jeans = mat("jeans", 0x3d5a96, rough=0.8)    # the light wash; vertex colour darkens to #2b3f6b
    jeansD = mat("jeansD", 0x1f2e50, rough=0.8)
    shoeW = mat("trainer", 0xf4f4f4, rough=0.6)
    toeM = mat("trainerToe", 0xdedad4, rough=0.6)
    stripe = mat("trainerStripe", 0xe8364f, rough=0.6)
    skin = mat("samSkin", 0xe9b58c, rough=0.55)
    cheek = mat("samCheek", 0xe8a888, rough=0.55)
    hair = mat("samHair", 0x6b3e22, rough=0.7)
    cap = mat("cap", 0x22c4b0, rough=0.8)
    capD = mat("capD", 0x168a7c, rough=0.8)
    ink = mat("samInk", 0x1c1420, rough=0.4)
    white = mat("string", 0xf4f4f4, rough=0.8)

    # the hood: a bent cylinder (a 120 degree arc) lying round the back of the neck
    hood = []
    for k in range(13):
        a = math.radians(-60 + 10 * k)
        hood.append((0.02 - 0.12 * math.cos(a), 0.12 * math.sin(a), 0.43 - 0.03 * math.cos(a)))
    export("sam_body", [
        lathe("torso", [(-0.16, 0.2), (-0.05, 0.198), (0.1, 0.19), (0.25, 0.195), (0.33, 0.185),
                        (0.38, 0.14), (0.41, 0.06)], top, 24, 0.92),
        sphere("shoulders", (0, 0, 0.36), (0.16, 0.235, 0.08), top, 24, 10),
        cyl("band", (0, 0, -0.18), (0, 0, -0.1), 0.206, 0.204, topD, 24),
        sweep("hood", hood, 0.055, topD, 10, flat=0.75, up=(1, 0, 0)),
        cube("pocket", (0.185, 0, -0.03), (0.04, 0.24, 0.13), topD),
        cyl("stringL", (0.17, 0.04, 0.4), (0.205, 0.05, 0.22), 0.008, 0.008, white, 6, "smooth"),
        cyl("stringR", (0.17, -0.04, 0.4), (0.205, -0.05, 0.22), 0.008, 0.008, white, 6, "smooth"),
    ])

    H = Head((0.02, 0, 0.21), (0.16, 0.155, 0.17))
    nose = (0.19, 0.025, 0.028)
    head = [head_mesh("head", H, nose, skin)]
    head += face(H, skin, cheek, ink, 0x5a3a20, 0.06, 0.225, 0.125, 0.034, hair, nose)
    head += [
        sphere("earL", (0.0, 0.155, 0.2), (0.03, 0.02, 0.045), skin, 8, 6),
        sphere("earR", (0.0, -0.155, 0.2), (0.03, 0.02, 0.045), skin, 8, 6),
        sphere("hair", (-0.03, 0, 0.25), (0.165, 0.168, 0.15), hair, 20, 12),
        # the cap: a crown, a button on top and the brim forward
        sphere("capCrown", (0.0, 0, 0.3), (0.17, 0.172, 0.11), cap, 24, 12),
        sphere("capButton", (0.0, 0, 0.41), (0.025, 0.025, 0.018), capD, 8, 6),
        cube("brim", (0.19, 0, 0.305), (0.15, 0.22, 0.02), capD, rot=(0, -0.12, 0)),
    ]
    # hair: eight flattened clumps along the hairline under the cap, fringe to the nape
    for k in range(8):
        a = -2.5 + k * (5.0 / 7)
        x, y = 0.01 + 0.155 * math.cos(a), 0.158 * math.sin(a)
        z = 0.255 - 0.035 * (1 - math.cos(a)) / 2 + (0.012 if abs(a) < 0.5 else 0)
        head.append(sphere("clump%d" % k, (x, y, z), (0.05, 0.035, 0.024), hair, 10, 6,
                           rot=(0.0, 0.35, a)))
    scale_about(head, (0, 0, 0.08), HEAD_K)
    export("sam_head", [cyl("neck", (0, 0, -0.02), (0, 0, 0.12), 0.055, 0.055, skin, 12, "smooth")] + head)

    export("sam_upperarm", [
        sphere("shoulderCap", (0, 0, -0.01), (0.07, 0.07, 0.07), top, 12, 8),
        tube("upper", 0.064, 0.21, top, 16, 3, r2=0.058),
    ])
    export("sam_forearm", [
        tube("lower", 0.058, 0.2, top, 16, 3, r2=0.054, z0=0.03),
        cyl("cuff", (0, 0, -0.15), (0, 0, -0.19), 0.06, 0.06, topD, 16),
    ] + hand(skin))

    # light-wash jeans: lighter down the front and over the knee, darker at the seams
    dark, light = rgb(0x2b3f6b), rgb(0x3d5a96)

    def wash(z0, z1, knee):
        def fn(co, n):
            t = (co.z - z1) / (z0 - z1)                 # 1 at the top, 0 at the bottom
            front = max(n.x, 0.0) * (1 - 0.6 * abs(n.y))
            k = 1 - abs(t - knee) * 1.4 if knee is not None else 0.5
            return lerp3(dark, light, min(1.0, max(0.0, 0.25 + 0.55 * front + 0.3 * k)))
        return fn
    export("sam_thigh", [paint(tube("thigh", 0.08, 0.3, jeans, 16, 5, r2=0.068, z0=0.02),
                               wash(0.02, -0.28, 0.0))])
    export("sam_shin", [
        paint(tube("shin", 0.068, 0.25, jeans, 16, 4, r2=0.062, z0=0.03), wash(0.03, -0.22, 1.0)),
        cyl("turnUp", (0, 0, -0.18), (0, 0, -0.23), 0.071, 0.071, jeansD, 16),
        sphere("shoe", (0.04, 0, -0.27), (0.12, 0.07, 0.05), shoeW, 16, 8),
        sphere("toeCap", (0.12, 0, -0.284), (0.055, 0.066, 0.036), toeM, 12, 6),
        cube("heelTab", (-0.075, 0, -0.245), (0.03, 0.05, 0.06), stripe, rot=(0, 0.3, 0)),
        cube("sole", (0.05, 0, -0.305), (0.24, 0.13, 0.025), mat("samSole", 0xd8d4cc, rough=0.8)),
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
CLOCK_H = 2.0


def walnut_recipe(dark, light):
    """Walnut grain: Wave bands (scale 6, distortion 4, detail 8) mixed with stretched Noise,
    through a ramp between two walnut tones. Grain runs up the case."""
    def fn(nt):
        n = nt.nodes
        tc = n.new("ShaderNodeTexCoord")
        mp = n.new("ShaderNodeMapping")
        mp.inputs["Rotation"].default_value = (0, 0, math.radians(45))
        mp.inputs["Scale"].default_value = (1.0, 1.0, 0.12)
        nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
        wv = n.new("ShaderNodeTexWave")
        wv.wave_type = "BANDS"
        wv.inputs["Scale"].default_value = 6
        wv.inputs["Distortion"].default_value = 4
        wv.inputs["Detail"].default_value = 8
        nt.links.new(mp.outputs["Vector"], wv.inputs["Vector"])
        mp2 = n.new("ShaderNodeMapping")
        mp2.inputs["Scale"].default_value = (8.0, 8.0, 0.6)
        nt.links.new(tc.outputs["Object"], mp2.inputs["Vector"])
        nz = n.new("ShaderNodeTexNoise")
        nz.inputs["Scale"].default_value = 5
        nz.inputs["Detail"].default_value = 6
        nt.links.new(mp2.outputs["Vector"], nz.inputs["Vector"])
        mix = n.new("ShaderNodeMix")
        mix.data_type = "FLOAT"
        mix.inputs["Factor"].default_value = 0.4
        nt.links.new(wv.outputs["Fac"], mix.inputs["A"])
        nt.links.new(nz.outputs["Fac"], mix.inputs["B"])
        ramp = n.new("ShaderNodeValToRGB")
        ramp.color_ramp.elements[0].position = 0.25
        ramp.color_ramp.elements[0].color = dark + (1,)
        ramp.color_ramp.elements[1].position = 0.75
        ramp.color_ramp.elements[1].color = light + (1,)
        nt.links.new(mix.outputs["Result"], ramp.inputs["Fac"])
        nt.links.new(ramp.outputs["Color"], n.get("Principled BSDF").inputs["Base Color"])
    return fn


def ivory_recipe(nt):
    """Ivory with a slight age mottle: soft noise, a few darker blotches."""
    n = nt.nodes
    nz = n.new("ShaderNodeTexNoise")
    nz.inputs["Scale"].default_value = 9
    nz.inputs["Detail"].default_value = 4
    ramp = n.new("ShaderNodeValToRGB")
    els = ramp.color_ramp.elements
    els[0].position, els[0].color = 0.3, rgb(0xd8ccae) + (1,)
    els[1].position, els[1].color = 0.62, rgb(0xf2ead6) + (1,)
    nt.links.new(nz.outputs["Fac"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], n.get("Principled BSDF").inputs["Base Color"])


def clock():
    walnut = mat("walnut", 0x5a3420, rough=0.45)
    edge = mat("walnutEdge", 0x8a5a34, rough=0.45)
    footM = mat("walnutFoot", 0x3e2416, rough=0.45)
    brass = mat("brass", 0xc89a3c, metal=1.0, rough=0.3)
    ivory = mat("ivory", 0xefe6cf, rough=0.6)
    ink = mat("clockInk", 0x1c1420, rough=0.5)
    inside = mat("clockInside", 0x1a100a, rough=0.9)
    RECIPES["walnut"] = (walnut_recipe(rgb(0x48281a), rgb(0x6c4026)), 1024)
    RECIPES["walnutEdge"] = (walnut_recipe(rgb(0x74482a), rgb(0x9e6a40)), 512)
    RECIPES["walnutFoot"] = (walnut_recipe(rgb(0x301a10), rgb(0x4c2e1c)), 256)
    RECIPES["ivory"] = (ivory_recipe, 256)

    case = []
    # plinth with a lighter fielded panel on the front
    case.append(cube("plinth", (0, 0, 0.18), (0.30, 0.48, 0.36), walnut))
    case.append(cube("plinthPanel", (0.152, 0, 0.18), (0.012, 0.34, 0.22), edge, 0.004))
    # stepped moulding up to the trunk
    case.append(cube("mould1", (0, 0, 0.375), (0.32, 0.5, 0.03), walnut))
    case.append(cube("mould2", (0, 0, 0.405), (0.29, 0.44, 0.03), walnut))
    # the trunk: a hollow box so the pendulum can swing inside
    tz, th = 0.42 + 0.425, 0.85
    case.append(cube("trunkBack", (-0.12, 0, tz), (0.02, 0.38, th), walnut))
    case.append(cube("trunkSideL", (0, 0.18, tz), (0.26, 0.02, th), walnut))
    case.append(cube("trunkSideR", (0, -0.18, tz), (0.26, 0.02, th), walnut))
    case.append(cube("trunkInside", (-0.105, 0, tz), (0.008, 0.34, th - 0.02), inside, 0))
    # the front frame: four bars round the door (no boolean, so no interior faces)
    case.append(cube("frameL", (0.12, 0.165, tz), (0.02, 0.05, th), walnut))
    case.append(cube("frameR", (0.12, -0.165, tz), (0.02, 0.05, th), walnut))
    case.append(cube("frameB", (0.12, 0, 0.42 + 0.025), (0.02, 0.28, 0.05), walnut))
    case.append(cube("frameT", (0.12, 0, 0.42 + th - 0.025), (0.02, 0.28, 0.05), walnut))
    # the door, pushed in 0.01: a lighter edge around a walnut panel, with the round window
    door = cube("door", (0.112, 0, tz), (0.012, 0.28, 0.75), edge, 0)
    panel = cube("doorPanel", (0.12, 0, tz), (0.006, 0.24, 0.71), walnut, 0)
    LENT_Z = 0.62
    for o in (door, panel):
        c = cyl("lenticleCut", (0.2, 0, LENT_Z), (0.0, 0, LENT_Z), 0.08, 0.08, inside, 24)
        cut(o, c)
    case += [door, panel]
    case.append(torus("lenticleRing", (0.124, 0, LENT_Z), 0.085, 0.009, brass, rot=(0, math.pi / 2, 0), seg=24))
    # moulding out to the hood
    case.append(cube("mould3", (0, 0, 1.285), (0.29, 0.44, 0.03), walnut))
    case.append(cube("mould4", (0, 0, 1.315), (0.33, 0.52, 0.03), walnut))
    # the hood, its break-arch top and the columns at the front corners
    case.append(cube("hood", (0, 0, 1.49), (0.32, 0.50, 0.32), walnut))
    case.append(cyl("arch", (-0.16, 0, 1.65), (0.16, 0, 1.65), 0.17, 0.17, walnut, 32))
    case.append(cube("hoodTop", (0, 0, 1.665), (0.33, 0.52, 0.03), walnut))
    for s in (1, -1):
        case.append(cyl("column", (0.15, s * 0.225, 1.36), (0.15, s * 0.225, 1.62), 0.025, 0.025, brass, 16))
        case.append(cube("capLow", (0.15, s * 0.225, 1.345), (0.06, 0.06, 0.03), brass, 0.004))
        case.append(cube("capHigh", (0.15, s * 0.225, 1.635), (0.06, 0.06, 0.03), brass, 0.004))
    # dial: a square plate, the ivory dial, a brass ring and twelve hour marks
    DIAL = (0.178, 0, 1.49)
    case.append(cube("dialPlate", (0.163, 0, 1.49), (0.01, 0.30, 0.30), brass, 0.003))
    case.append(cyl("dial", (0.165, 0, 1.49), (0.175, 0, 1.49), 0.14, 0.14, ivory, 32))
    case.append(torus("dialRing", (0.176, 0, 1.49), 0.14, 0.008, brass, rot=(0, math.pi / 2, 0), seg=32))
    for k in range(12):
        a = k * math.pi / 6
        big = k % 3 == 0
        case.append(cube("hour%d" % k, (0.177, 0.112 * math.sin(a), 1.49 + 0.112 * math.cos(a)),
                         (0.006, 0.014 if big else 0.009, 0.034 if big else 0.022), ink, 0, rot=(-a, 0, 0)))
    # the arch: an ivory half disc where the moon would be. The game draws the eyes on it.
    moon = cyl("moon", (0.163, 0, 1.65), (0.172, 0, 1.65), 0.135, 0.135, ivory, 32)
    cut(moon, cube("moonCut", (0.17, 0, 1.55), (0.1, 0.4, 0.2), ivory, 0))
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
        bevel(o, 0.005)
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
    pend = [cyl("rod", (0, 0, 0), (0, 0, -L), 0.006 / f, 0.006 / f, brass, 8, "smooth"),
            cyl("bob", (-0.012, 0, -L), (0.012, 0, -L), 0.048, 0.048, brass, 24),
            torus("bobRim", (0.013, 0, -L), 0.04, 0.006, mat("brassDark", 0x8a6420, metal=1.0, rough=0.35),
                  rot=(0, math.pi / 2, 0))]
    scale_about(pend, (0, 0, 0), f)
    export("clock_pendulum", pend)

    # the hands: flat spade shapes about 0.02-0.03 wide, pivot at the dial centre, pointing up (+Z)
    def hand(name, length, shaft, w):
        out = [(-shaft, 0.0), (shaft, 0.0), (shaft * 0.8, length * 0.6), (w, length * 0.74),
               (0.0, length), (-w, length * 0.74), (-shaft * 0.8, length * 0.6)]
        o = prism(name, [(y, z - 0.012) for (y, z) in out], 0.006, 0.0, ink)
        hub = cyl("hub", (-0.004, 0, 0), (0.006, 0, 0), 0.016, 0.016, brass, 12)
        scale_about([o, hub], (0, 0, 0), f)
        export(name, [o, hub])
    hand("clock_hour", 0.085, 0.012, 0.022)
    hand("clock_minute", 0.122, 0.009, 0.016)

    # bracket feet: pivot at the ankle (top). Side profile: an ogee bracket curving out to a
    # scrolled toe at the front, an arched cut-out underneath; bevelled. A volute at the toe.
    prof = [(-0.13, 0.0), (0.13, 0.0)]
    for i in range(1, 9):   # the ogee: out then in, down the front
        t = i / 8
        prof.append((0.13 + 0.03 * math.sin(math.pi * t) - 0.01 * t, -0.075 * t))
    prof += [(0.135, -0.085), (0.145, -0.095), (0.05, -0.095)]
    for i in range(1, 8):   # the arch underneath
        a = math.pi * i / 8
        prof.append((0.05 - 0.1 * (1 - math.cos(a)) / 2, -0.095 + 0.035 * math.sin(a)))
    prof += [(-0.05, -0.095), (-0.135, -0.095), (-0.14, -0.05)]
    vol = []
    for i in range(16):   # the volute: a spiral strip curling in at the toe
        a = 1.8 * math.pi * i / 15
        r = 0.032 - 0.016 * i / 15
        vol.append((0.128 + r * math.cos(a + 1.2), -0.04 + r * math.sin(a + 1.2)))
    vol_in = [(x + (0.128 - x) * 0.32, z + (-0.04 - z) * 0.32) for x, z in vol]
    for name in ("clock_foot_l", "clock_foot_r"):
        body = prism("bracket", prof, 0.11, 0.0, footM, "XZ")
        bevel(body, 0.006)
        foot = [body]
        for s in (1, -1):
            foot.append(prism("volute", vol + list(reversed(vol_in)), 0.012, s * 0.05, edge, "XZ"))
        scale_about(foot, (0, 0, 0), f)
        export(name, foot)

    print("CLOCK scale %.5f" % f)
    print("CLOCK pendulum pivot %.4f %.4f" % (PIV[0] * f, PIV[2] * f))
    print("CLOCK dial centre %.4f %.4f" % (DIAL[0] * f, DIAL[2] * f))
    print("CLOCK face (arch) %.4f %.4f" % (0.172 * f, 1.71 * f))
    print("CLOCK foot height %.4f" % (0.095 * f))


# ------------------------------------------------------------------ the Forgetting (boss)
# A ball of Gran's dark knitting wool coming unravelled: memory unravelling. A lumpy wool core
# wound round with one continuous strand of yarn, two steel knitting needles stuck through, one
# hollow eye (pale inside, a glowing cyan ring) with a dark pupil (its own part, so the game
# turns it to the players), and strands of two-ply yarn hanging below (chains of the same
# twisted piece, ending in a frayed tip). Faces +X. Pivot at the centre of the ball.
def forgetting():
    wool = mat("wool", 0x5e5a66, rough=0.9)
    woolD = mat("woolDark", 0x4c4856, rough=0.9)
    woolL = mat("woolLight", 0x6c6876, rough=0.9)
    yarnM = mat("woolYarn", 0xffffff, rough=0.9)      # colour comes from the vertex colours
    steel = mat("needle", 0xb8bdc7, metal=1.0, rough=0.25)   # (0.72, 0.74, 0.78)
    steelD = mat("needleKnob", 0x8a8e9a, metal=1.0, rough=0.3)
    sclera = mat("eyeWhite", 0xdcd8e8, rough=0.4)
    cyan = mat("eyeRing", 0x7fe8ff, rough=0.4, emit=True)
    EYE = Vector((0.9, 0, 0.12))

    # the core: icosphere with a lumpy displace, the eye hollowed out of the front
    core = icosphere("core", (0, 0, 0), (1, 1, 1), woolD, 3)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    for v in core.data.vertices:
        n = noise.fractal(v.co * (1 / 0.25), 0.5, 2.0, 3)
        v.co = v.co.normalized() * (0.86 + 0.04 * n)
    cut(core, sphere("hollow", (EYE.x + 0.18, 0, EYE.z), (0.36, 0.36, 0.36), sclera, 24, 16))

    # one continuous strand wound round and round, the winding plane turning a little each
    # wrap; it breaks off round the eye. Tones vary along it (+-25%): cool plum where darker,
    # warmer grey where lighter, and a little warmer on top.
    base, plum, warm = rgb(0x5e5a66), rgb(0x463a58), rgb(0x7c7672)
    WR, PER = 24, 24
    runs, run = [], []
    ref = Vector((0.31, 0.17, 0.93)).normalized()
    for i in range(WR * PER + 1):
        s = i / PER
        th, ph = 0.9 * s, math.pi / 2 + 1.2 * math.sin(0.53 * s)
        k = Vector((math.sin(ph) * math.cos(th), math.sin(ph) * math.sin(th), math.cos(ph)))
        u = k.cross(ref).normalized()
        w = k.cross(u)
        t = 2 * math.pi * s
        d = (math.cos(t) * u + math.sin(t) * w).normalized()
        d = (d + 0.04 * noise.noise_vector(d * 3 + Vector((s, 0, 0)))).normalized()
        r = 0.9 + 0.002 * s + 0.015 * noise.noise(d * 4)
        p = d * r
        val = 0.25 * max(-1.0, min(1.0, 1.6 * noise.noise(Vector((s * 0.9, 1.7, 0.3))))) + 0.06 * d.z
        col = lerp3(base, warm, val / 0.25) if val > 0 else lerp3(base, plum, -val / 0.25)
        if (p - EYE).length < 0.42:
            if len(run) > 2:
                runs.append(run)
            run = []
            continue
        run.append((p, col))
    if len(run) > 2:
        runs.append(run)
    body = [core]
    for j, rn in enumerate(runs):
        o = sweep("yarn%d" % j, [p for p, _ in rn], 0.04, yarnM, 5)
        cols = [c for _, c in rn]
        me = o.data
        at = me.color_attributes.new("Col", "FLOAT_COLOR", "CORNER")
        for poly in me.polygons:
            for li in poly.loop_indices:
                vi = me.loops[li].vertex_index
                at.data[li].color = cols[min(vi // 5, len(cols) - 1)] + (1.0,)
        body.append(o)
    body.append(torus("eyeRing", (EYE.x - 0.01, 0, EYE.z), 0.31, 0.024, cyan, rot=(0, math.pi / 2, 0), seg=32))
    # two steel knitting needles crossing through the ball at 30 degrees, a knob on each end
    X = Vector((-0.2, 0, 0.55))   # where they cross, inside the top of the ball
    for s in (1, -1):
        d = Vector((-0.2, s * 0.26, 1)).normalized()
        p0, p1 = X - d * 0.5, X + d * 0.9
        body.append(cyl("needle", tuple(p0), tuple(p1), 0.035, 0.03, steel, 12))
        body.append(sphere("knob", tuple(p1), (0.065, 0.065, 0.065), steelD, 12, 8))
    export("forget_body", body)
    print("FORGET eye centre %.3f %.3f" % (EYE.x, EYE.z))

    # the pupil: a dark ball with a catchlight, placed in the pale hollow by the game
    export("forget_pupil", [sphere("pupil", (0, 0, 0), (0.09, 0.09, 0.09), mat("pupil", 0x1a1426, rough=0.3), 14, 10),
                            sphere("shine", (0.072, 0.03, 0.035), (0.022, 0.022, 0.022), mat("shine", 0xffffff), 8, 6)])

    # one strand piece: two plies of yarn side by side, twisted half a turn; pivot at the top
    plies = [tube("plyA", 0.06, 0.40, wool, 12, 10, off=(0, 0.05)),
             tube("plyB", 0.06, 0.40, woolD, 12, 10, off=(0, -0.05))]
    for o in plies:
        o["shade"] = "smooth"
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
        tip.append(cyl("fray%d" % k, tuple(p0), tuple(p1), 0.025, 0.004, [wool, woolL][k % 2], 8, "smooth"))
    export("forget_tip", tip)


ONLY = os.environ.get("MODELS_ONLY", "")
for fn in (gran, sam, clock, forgetting):
    if ONLY and fn.__name__ not in ONLY.split(","):
        continue
    reset()
    bake_setup()
    fn()
print("TRIS", TRIS)
print("done")
