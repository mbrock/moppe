"""Procedural model of Krišjānis' Long John cargo bike, from the snow photos.

Run:  Blender -b -P build_cargobike.py -- [--render]
Axes: +X forward, +Y left, +Z up, metres. Origin under the rear axle.
Dimensions were measured off ref/side.jpeg (26" rear wheel ~ 490 px => 1.37 mm/px).
"""
import bpy, bmesh, math, sys, os
from mathutils import Vector, Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
V = Vector

# ---------------------------------------------------------------- scene reset
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'

COLLS = {}
def link(obj, cname):
    if cname not in COLLS:
        c = bpy.data.collections.new(cname)
        scene.collection.children.link(c)
        COLLS[cname] = c
    COLLS[cname].objects.link(obj)
    return obj

# ---------------------------------------------------------------- materials
def mat(name, color, metal=0.0, rough=0.5):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1)
    b.inputs["Metallic"].default_value = metal
    b.inputs["Roughness"].default_value = rough
    return m

class Nodes:
    """Tiny helper for building shader node trees."""
    def __init__(self, m):
        self.nt = m.node_tree
    def __call__(self, kind, **inputs):
        n = self.nt.nodes.new(kind)
        for k, v in inputs.items():
            if hasattr(v, "bl_idname") or isinstance(v, bpy.types.NodeSocket):
                self.nt.links.new(v if isinstance(v, bpy.types.NodeSocket) else v.outputs[0],
                                  n.inputs[k])
            else:
                n.inputs[k].default_value = v
        return n
    def link(self, a, b):
        self.nt.links.new(a, b)

def ramp(nd, fac, stops):
    r = nd("ShaderNodeValToRGB")
    nd.link(fac, r.inputs["Fac"])
    els = r.color_ramp.elements
    while len(els) < len(stops):
        els.new(0.5)
    for el, (pos, col) in zip(els, stops):
        el.position, el.color = pos, (*col, 1) if len(col) == 3 else col
    return r

def noise(nd, coord, scale, detail=6.0, rough=0.6, distortion=0.0, w=None):
    n = nd("ShaderNodeTexNoise", Scale=scale, Detail=detail, Roughness=rough,
           Distortion=distortion)
    nd.link(coord, n.inputs["Vector"])
    return n

def steel_layers(nd, co, base, patina, rust):
    """Raw mild steel: mill-scale mottling, light winter flash rust, fine pitting bump."""
    mott = noise(nd, co, 7.0, 8, 0.62, 0.3)
    col = ramp(nd, mott.outputs["Fac"], [(0.38, base), (0.62, patina)])
    spot = noise(nd, co, 38.0, 4, 0.7)
    rmask = ramp(nd, spot.outputs["Fac"], [(0.66, (0, 0, 0)), (0.73, (1, 1, 1))])
    mix = nd("ShaderNodeMix", Factor=rmask.outputs["Color"])
    mix.data_type = 'RGBA'
    nd.link(col.outputs["Color"], mix.inputs[6])
    mix.inputs[7].default_value = (*rust, 1)
    fine = noise(nd, co, 900.0, 3, 0.5)
    streak = nd("ShaderNodeMapping")
    nd.link(co, streak.inputs["Vector"])
    streak.inputs["Scale"].default_value = (1400, 1400, 60)   # long grinding/brush streaks
    sn = noise(nd, streak.outputs[0], 1.0, 2, 0.5)
    rough = ramp(nd, sn.outputs["Fac"], [(0.35, (0.30,) * 3), (0.65, (0.52,) * 3)])
    rr = nd("ShaderNodeMix", Factor=rmask.outputs["Color"])
    rr.data_type = 'FLOAT'
    nd.link(rough.outputs["Color"], rr.inputs[2])
    rr.inputs[3].default_value = 0.85
    bump = nd("ShaderNodeBump", Strength=0.25, Distance=0.0004, Height=fine.outputs["Fac"])
    return mix.outputs[2], rr.outputs[0], bump.outputs["Normal"], rmask

def raw_steel(name, base=(0.30, 0.295, 0.29), patina=(0.17, 0.155, 0.14),
              rust=(0.24, 0.12, 0.06)):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nd = Nodes(m)
    b = m.node_tree.nodes["Principled BSDF"]
    co = nd("ShaderNodeTexCoord").outputs["Object"]
    col, rough, nrm, rmask = steel_layers(nd, co, base, patina, rust)
    nd.link(col, b.inputs["Base Color"])
    nd.link(rough, b.inputs["Roughness"])
    nd.link(nrm, b.inputs["Normal"])
    met = nd("ShaderNodeMix", Factor=rmask.outputs["Color"])
    met.data_type = 'FLOAT'
    met.inputs[2].default_value, met.inputs[3].default_value = 1.0, 0.2
    nd.link(met.outputs[0], b.inputs["Metallic"])
    return m

def worn_paint(name, paint=(0.22, 0.045, 0.06)):
    """The donor frame: old red/mauve paint, mostly scuffed back to steel."""
    m = raw_steel(name, base=(0.36, 0.33, 0.33), patina=(0.24, 0.17, 0.18))
    nt = m.node_tree
    nd = Nodes(m)
    steel = nt.nodes["Principled BSDF"]
    out = nt.nodes["Material Output"]
    p = nd("ShaderNodeBsdfPrincipled", **{"Base Color": (*paint, 1), "Roughness": 0.55})
    co = nt.nodes["Texture Coordinate"].outputs["Object"]
    chips = noise(nd, co, 22.0, 10, 0.7, 0.6)
    mask = ramp(nd, chips.outputs["Fac"], [(0.58, (0, 0, 0)), (0.61, (1, 1, 1))])
    mix = nd("ShaderNodeMixShader", Fac=mask.outputs["Color"])
    nd.link(steel.outputs[0], mix.inputs[1])
    nd.link(p.outputs[0], mix.inputs[2])
    nd.link(mix.outputs[0], out.inputs["Surface"])
    return m

def weld_metal(name):
    """TIG bead: bright, wire-brushed, with faint straw/blue heat tint."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nd = Nodes(m)
    b = m.node_tree.nodes["Principled BSDF"]
    co = nd("ShaderNodeTexCoord").outputs["Object"]
    n = noise(nd, co, 160.0, 4, 0.6)
    c = ramp(nd, n.outputs["Fac"], [(0.3, (0.62, 0.60, 0.58)), (0.5, (0.55, 0.47, 0.32)),
                                     (0.7, (0.42, 0.40, 0.52))])
    nd.link(c.outputs["Color"], b.inputs["Base Color"])
    b.inputs["Metallic"].default_value = 1.0
    b.inputs["Roughness"].default_value = 0.33
    return m

def ground_patch(name):
    """Flap-disc ground area around a weld: bright, ragged edges (transparent elsewhere)."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nd = Nodes(m)
    b = nt.nodes["Principled BSDF"]
    out = nt.nodes["Material Output"]
    b.inputs["Base Color"].default_value = (0.78, 0.77, 0.75, 1)
    b.inputs["Metallic"].default_value = 1.0
    co = nd("ShaderNodeTexCoord").outputs["Object"]
    mp = nd("ShaderNodeMapping")
    nd.link(co, mp.inputs["Vector"])
    mp.inputs["Scale"].default_value = (900, 900, 900)
    sc = noise(nd, mp.outputs[0], 1.0, 2, 0.5)
    rr = ramp(nd, sc.outputs["Fac"], [(0.4, (0.22,) * 3), (0.6, (0.40,) * 3)])
    nd.link(rr.outputs["Color"], b.inputs["Roughness"])
    edge = noise(nd, co, 70.0, 6, 0.7, 0.5)
    fade = nd("ShaderNodeAttribute")
    fade.attribute_name = "fade"
    mul = nd("ShaderNodeMath")
    mul.operation = 'MULTIPLY'
    nd.link(edge.outputs["Fac"], mul.inputs[0])
    nd.link(fade.outputs["Fac"], mul.inputs[1])
    mask = ramp(nd, mul.outputs[0], [(0.50, (0, 0, 0)), (0.53, (1, 1, 1))])
    tr = nd("ShaderNodeBsdfTransparent")
    mix = nd("ShaderNodeMixShader", Fac=mask.outputs["Color"])
    nd.link(tr.outputs[0], mix.inputs[1])
    nd.link(b.outputs[0], mix.inputs[2])
    nd.link(mix.outputs[0], out.inputs["Surface"])
    return m

M_STEEL = raw_steel("Raw steel")
M_OLD = worn_paint("Old frame (worn red)")
M_WELD = weld_metal("Weld bead")
M_GROUND = ground_patch("Ground steel")
FRAME_TUBES = []   # axis polylines of welded steel tubes, for add_welds()
M_RED = mat("Red paint", (0.55, 0.03, 0.03), rough=0.45)
M_ALU = mat("Aluminium", (0.82, 0.82, 0.84), metal=1.0, rough=0.25)
M_DARK = mat("Dark steel", (0.10, 0.10, 0.11), metal=1.0, rough=0.4)
M_BLACK = mat("Black paint", (0.02, 0.02, 0.022), rough=0.35)
M_PANEL = mat("Box panel", (0.025, 0.027, 0.03), rough=0.55)
M_GRIPTAPE = mat("Floor grip", (0.03, 0.03, 0.03), rough=0.95)
M_RUBBER = mat("Rubber", (0.015, 0.015, 0.015), rough=0.85)
M_SADDLE = mat("Saddle", (0.85, 0.62, 0.05), rough=0.45)
M_CABLE = mat("Cable housing", (0.01, 0.01, 0.01), rough=0.4)
M_SNOW = mat("Snow", (0.88, 0.89, 0.92), rough=0.8)

# ---------------------------------------------------------------- helpers
def fillet(points, r, seg=10):
    """Round every interior corner of a polyline with radius r (bent tube)."""
    pts = [V(p) for p in points]
    if len(pts) < 3 or r <= 0:
        return pts
    out = [pts[0]]
    for a, p, b in zip(pts, pts[1:], pts[2:]):
        u, v = (a - p).normalized(), (b - p).normalized()
        theta = u.angle(v)
        if theta > math.pi - 1e-3:
            out.append(p)
            continue
        rr = min(r, 0.45 * min((a - p).length, (b - p).length) * math.tan(theta / 2))
        d = rr / math.tan(theta / 2)
        t1, t2 = p + u * d, p + v * d
        c = p + (u + v).normalized() * (rr / math.sin(theta / 2))
        r1, r2 = t1 - c, t2 - c
        for i in range(seg + 1):
            out.append(c + r1.slerp(r2, i / seg).normalized() * rr)  # slerp returns unit
    out.append(pts[-1])
    return out

def tube(name, points, radius, material=M_STEEL, bend=0.0, cname="Frame",
         cyclic=False, res=4):
    cu = bpy.data.curves.new(name, 'CURVE')
    cu.dimensions = '3D'
    cu.bevel_depth = radius
    cu.bevel_resolution = res
    cu.use_fill_caps = True
    cu.twist_mode = 'MINIMUM'
    sp = cu.splines.new('POLY')
    pts = fillet(points, bend) if bend else [V(p) for p in points]
    sp.points.add(len(pts) - 1)
    for spt, p in zip(sp.points, pts):
        spt.co = (*p, 1)
    sp.use_cyclic_u = cyclic
    if material in (M_STEEL, M_OLD) and cname in ("Frame", "Rear end"):
        FRAME_TUBES.append({"name": name, "pts": pts, "r": radius, "old": material is M_OLD})
    ob = bpy.data.objects.new(name, cu)
    ob.data.materials.append(material)
    return link(ob, cname)

def my(p):
    return V((p[0], -p[1], p[2]))

def pair(name, points, radius, **kw):
    """Tube on the left (+Y) side and its mirror on the right."""
    tube(name + ".L", points, radius, **kw)
    tube(name + ".R", [my(p) for p in points], radius, **kw)

def mesh_obj(name, bm, material, cname, smooth=True):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    if smooth:
        me.shade_smooth()
    ob = bpy.data.objects.new(name, me)
    ob.data.materials.append(material)
    return link(ob, cname)

def orient(ob, a, b):
    ob.location = (V(a) + V(b)) / 2
    ob.rotation_mode = 'QUATERNION'
    ob.rotation_quaternion = V((0, 0, 1)).rotation_difference((V(b) - V(a)).normalized())

def cylinder(name, a, b, radius, material, cname, segs=32, r2=None):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=segs, radius1=radius,
                          radius2=radius if r2 is None else r2, depth=(V(b) - V(a)).length)
    ob = mesh_obj(name, bm, material, cname, smooth=False)
    orient(ob, a, b)
    ob.data.polygons.foreach_set("use_smooth", [len(p.vertices) == 4 for p in ob.data.polygons])
    return ob

def box(name, center, size, material, cname, bevel=0.0):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=size, verts=bm.verts)
    ob = mesh_obj(name, bm, material, cname, smooth=False)
    ob.location = center
    if bevel:
        m = ob.modifiers.new("Bevel", 'BEVEL')
        m.width, m.segments = bevel, 2
    return ob

def panel(name, verts, material, cname="Box", thick=0.003):
    """Flat sheet through the given coplanar outline, thickened."""
    bm = bmesh.new()
    bm.faces.new([bm.verts.new(v) for v in verts])
    ob = mesh_obj(name, bm, material, cname, smooth=False)
    s = ob.modifiers.new("Solidify", 'SOLIDIFY')
    s.thickness, s.offset = thick, 0
    return ob

def holed_plate(name, matrix, w, h, holes, hole_r, material, cname, thick=0.003):
    """Plate in its local XY plane with circular holes, placed by `matrix`."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=(w, h, thick), verts=bm.verts)
    ob = mesh_obj(name, bm, material, cname, smooth=False)
    ob.matrix_world = matrix
    cb = bmesh.new()
    for hx, hy in holes:
        r = bmesh.ops.create_cone(cb, cap_ends=True, segments=32, radius1=hole_r,
                                  radius2=hole_r, depth=thick * 4)
        bmesh.ops.translate(cb, vec=(hx, hy, 0), verts=r["verts"])
    cutter = mesh_obj(name + " cutter", cb, material, cname, smooth=False)
    cutter.matrix_world = matrix
    cutter.hide_render = True
    cutter.hide_set(True)
    cutter.display_type = 'WIRE'
    m = ob.modifiers.new("Holes", 'BOOLEAN')
    m.operation, m.object, m.solver = 'DIFFERENCE', cutter, 'EXACT'
    return ob

def cable(name, a, b, sag, side=0.0, r=0.0028, n=24):
    """Slack cable housing: quadratic sag between a and b, bowed sideways by `side`."""
    a, b = V(a), V(b)
    pts = []
    for i in range(n + 1):
        t = i / n
        k = 4 * t * (1 - t)
        pts.append(a.lerp(b, t) + V((0, side * k, -sag * k)))
    return tube(name, pts, r, M_CABLE, cname="Cables", res=2)

def torus_xz(name, center, major, minor, material, cname, scale_y=1.0):
    """Torus in the XZ plane (wheel ring), axle along Y."""
    bm = bmesh.new()
    sm, sn = 96, 20
    rings = []
    for i in range(sm):
        a = 2 * math.pi * i / sm
        ring = []
        for j in range(sn):
            b = 2 * math.pi * j / sn
            rr = major + minor * math.cos(b)
            ring.append(bm.verts.new((rr * math.cos(a), minor * math.sin(b) * scale_y,
                                      rr * math.sin(a))))
        rings.append(ring)
    for i in range(sm):
        r0, r1 = rings[i], rings[(i + 1) % sm]
        for j in range(sn):
            bm.faces.new((r0[j], r0[(j + 1) % sn], r1[(j + 1) % sn], r1[j]))
    ob = mesh_obj(name, bm, material, cname)
    ob.location = center
    return ob

# ---------------------------------------------------------------- welds
WELD_BM = {"bead": [], "patch": []}   # curve splines / patch sleeves, merged into few objects

def bead_spline(pts, radii, cyclic):
    WELD_BM["bead"].append((pts, radii, cyclic))

def rippled(n, ripples_per_pt=0.25, amp=0.28):
    """Stack-of-dimes radius profile: one ripple every 1/ripples_per_pt points."""
    return [1.0 + amp * (0.5 + 0.5 * math.cos(2 * math.pi * i * ripples_per_pt)) for i in range(n)]

def saddle(E, d, rB, Q, e, rA):
    """Intersection curve of tube B (ending at E, heading d) with tube A's cylinder (Q, e, rA).
    Returns [(point, outward normal)] around B."""
    u = d.orthogonal().normalized()
    v = d.cross(u)
    a = d - e * d.dot(e)
    A2 = a.length_squared
    n = max(64, int(2 * math.pi * rB * 1.35 / 0.0018) * 4)
    out = []
    for i in range(n):
        th = 2 * math.pi * i / n
        off = (u * math.cos(th) + v * math.sin(th)) * rB
        w0 = E + off - Q
        b = w0 - e * w0.dot(e)
        if A2 < 1e-6:
            t = 0.0
        else:
            B2, C = 2 * a.dot(b), b.length_squared - rA * rA
            disc = B2 * B2 - 4 * A2 * C
            t = (-B2 - math.sqrt(disc)) / (2 * A2) if disc >= 0 else -B2 / (2 * A2)
        t = max(t, -0.08)
        p = E + off + d * t
        nb = off.normalized()
        ra = (p - Q) - e * (p - Q).dot(e)
        na = ra.normalized() if ra.length > 1e-6 else nb
        out.append((p, (na + nb).normalized() if (na + nb).length > 1e-3 else nb))
    return out

def patch_sleeve(centre, d, r, length, fades):
    """Open cylinder just proud of a tube around a joint; `fades` = brightness-mask weight at
    evenly spaced rings along it (ground area is a ragged blob, densest at the weld)."""
    WELD_BM["patch"].append((centre, d, r, length, fades))

def closest_on_seg(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-12)))
    return a + ab * t

def add_welds():
    done = []
    for B in FRAME_TUBES:
        if B["name"].startswith(("Dropout", "Stand")):
            continue
        P = B["pts"]
        for E, prev in ((P[0], P[1]), (P[-1], P[-2])):
            d = (E - prev).normalized()
            best = None
            for A in FRAME_TUBES:
                if A is B:
                    continue
                for a, b in zip(A["pts"], A["pts"][1:]):
                    q = closest_on_seg(E, a, b)
                    dist = (E - q).length
                    if best is None or dist < best[0]:
                        best = (dist, A, a, b, q)
            if best is None:
                continue
            dist, A, a, b, q = best
            if dist > A["r"] + B["r"] + 0.003:
                continue
            if any((E - x).length < 0.004 and {A["name"], B["name"]} == s for x, s in done):
                continue
            done.append((E, {A["name"], B["name"]}))
            e = (b - a).normalized()
            ring = saddle(E, d, B["r"], q, e, A["r"])
            br = B["r"] * (0.16 if B["old"] else 0.23)       # factory welds are neater
            pts = [p + nrm * br * 0.35 for p, nrm in ring]
            bead_spline(pts, [br * k for k in rippled(len(pts), amp=0.12 if B["old"] else 0.28)],
                        True)
            if not (A["old"] and B["old"]):
                # ground-bright patches on both tubes around the joint
                patch_sleeve(E - d * 0.011, d, B["r"] + 0.0004, 0.022, (0.0, 0.75, 1.15, 1.3))
                patch_sleeve(q, e, A["r"] + 0.0004, 2 * B["r"] + 0.022,
                             (0.0, 0.9, 1.25, 1.25, 0.9, 0.0))

def weld_line(pts, r=0.0026):
    """Fillet seam along a plate edge."""
    pts = [V(p) for p in pts]
    dense = []
    for a, b in zip(pts, pts[1:]):
        k = max(2, int((b - a).length / 0.0006))
        dense += [a.lerp(b, i / k) for i in range(k)]
    dense.append(pts[-1])
    bead_spline(dense, [r * k for k in rippled(len(dense), 1 / 3, 0.3)], False)

def build_welds():
    cu = bpy.data.curves.new("Weld beads", 'CURVE')
    cu.dimensions = '3D'
    cu.bevel_depth = 1.0          # x per-point radius = actual bead radius (metres)
    cu.bevel_resolution = 2
    cu.use_fill_caps = True
    cu.twist_mode = 'MINIMUM'
    for pts, radii, cyc in WELD_BM["bead"]:
        sp = cu.splines.new('POLY')
        sp.points.add(len(pts) - 1)
        for spt, p, r in zip(sp.points, pts, radii):
            spt.co = (*p, 1)
            spt.radius = r
        sp.use_cyclic_u = cyc
    ob = bpy.data.objects.new("Weld beads", cu)
    ob.data.materials.append(M_WELD)
    link(ob, "Welds")
    bm = bmesh.new()
    fl = bm.verts.layers.float.new("fade")
    for c, d, r, L, fades in WELD_BM["patch"]:
        u = d.orthogonal().normalized()
        v = d.cross(u)
        rings = []
        for k, f in enumerate(fades):
            p0 = c + d * (L * (k / (len(fades) - 1) - 0.5))
            ring = []
            for i in range(32):
                a = 2 * math.pi * i / 32
                vt = bm.verts.new(p0 + (u * math.cos(a) + v * math.sin(a)) * r)
                vt[fl] = f
                ring.append(vt)
            rings.append(ring)
        for r0, r1 in zip(rings, rings[1:]):
            for i in range(32):
                j = (i + 1) % 32
                bm.faces.new((r0[i], r0[j], r1[j], r1[i]))
    me = bpy.data.meshes.new("Ground patches")
    bm.to_mesh(me)
    bm.free()
    # bmesh float layer -> mesh attribute "fade" (point domain), readable by the shader
    me.shade_smooth()
    ob = bpy.data.objects.new("Ground patches", me)
    ob.data.materials.append(M_GROUND)
    link(ob, "Welds")

# ---------------------------------------------------------------- key geometry
# Points solved jointly from the four photos (tools/bundle.py -> solved.json),
# riding pose. Comments give the solved value where a constant comes from it.
R_REAR, R_FRONT = 0.335, 0.253               # 26 x 2.0 rear, 20 x 2.0 front
AX_R = V((0.0, 0.0, R_REAR))
AX_F = V((1.894, 0.0, R_FRONT))               # AXF
BB = V((0.428, 0.0, 0.277))                   # BB
SA = math.radians(71.2)                       # BB -> STTOP / SEATCLAMP
SEAT_DIR = V((-math.cos(SA), 0, math.sin(SA)))
def st(l):
    return BB + SEAT_DIR * l
ST_TOP_L = (0.730 - BB.z) / SEAT_DIR.z        # STTOP z 0.730
CLAMP_L = (0.929 - BB.z) / SEAT_DIR.z         # SEATCLAMP z 0.929

HA = math.radians(71)                         # both steering axes
HEAD_DIR = V((-math.cos(HA), 0, math.sin(HA)))
FHT_TOP = V((1.726, 0, 0.731))                # FHT_TOP
def faxis(z):
    return FHT_TOP + HEAD_DIR * ((z - FHT_TOP.z) / HEAD_DIR.z)
FHT_BOT = faxis(0.55)

# hoop (one bent tube: legs -> top bar; the box top rails grow out of its feet)
HOOP_W = 0.248                                # HOOP_TC y .242 / HOOP_BC y .264
HOOP_X_TOP, HOOP_X_BOT = 0.800, 0.835         # legs lean back slightly
HOOP_Z_TOP, HOOP_Z_BOT = 0.968, 0.345         # straight runs sit at the vertex heights

# rider head tube, welded in front of the hoop top bar; centre mast below it
# short head tube: foot on the mast (solved HT_BOT .861/.894), top welded to the hoop top bar
HT_BOT = V((0.858, 0, 0.885))
HT_TOP = HT_BOT + HEAD_DIR * 0.110
MAST_BOT = V((0.719, 0, 0.211))               # MAST_BOT
MAST_BEND = V((0.690, 0, 0.800))              # MAST_BEND .691/.790 (vertex)

# upper spine: twin tubes from the seatstays to the hoop legs
SPINE_Z = 0.460                               # US_R z .452, US_F z .460

# cargo box rails
BOT_RC = V((0.865, 0.190, 0.205))             # rear end; side photo wants lower, others .22
BOT_FB = V((1.574, 0.200, 0.203))             # bottom rail, front bend
KINK = V((1.486, 0.228, 0.376))               # top rail kink
LOOP_TC = V((1.758, 0.185, 0.725))            # bulkhead top corner
FRONT_LOW = V((1.762, 0.150, 0.541))          # bulkhead lower corner
STAND_FOOT = V((0.884, 0.249, 0.0))           # STAND_FOOT (z -.017)

R_T, R_S = 0.0125, 0.0095                     # 25 mm main tube, 19 mm braces

def lerp_rail(a, b, x):
    t = (x - a.x) / (b.x - a.x)
    return a.lerp(b, t)

HOOP_FOOT = V((HOOP_X_BOT, HOOP_W, HOOP_Z_BOT))
def top_rail(x):
    return lerp_rail(HOOP_FOOT, KINK, x)
def bot_rail(x):
    return lerp_rail(BOT_RC, BOT_FB, x)

# ---------------------------------------------------------------- rear (old bike)
RE = "Rear end"
DROP = V((0, 0.068, R_REAR))
SS_TOP = st(ST_TOP_L - 0.04) + V((0, 0.022, 0))
tube("Seat tube", [BB, st(ST_TOP_L)], 0.0143, M_OLD, cname=RE)
# chainstays: must pass inside the chainring (y < 40 mm while x > 350) and crank arms, yet
# outside the 2.0" tyre (y > 42 mm at x ~ 330) -> the classic crimp, then straight to the drop
pair("Chainstay", [BB + V((-0.02, 0.022, 0)), V((0.352, 0.037, 0.287)), V((0.318, 0.0455, 0.291)),
                   DROP], 0.010, material=M_OLD, bend=0.06, cname=RE)
# seatstays bow out past the tyre crown, then turn in to the seat cluster
pair("Seatstay", [SS_TOP, SS_TOP.lerp(DROP, 0.28) + V((0, 0.022, 0)), DROP], 0.0085,
     material=M_OLD, bend=0.10, cname=RE)
pair("Dropout", [DROP + V((0.025, 0, 0.012)), DROP + V((-0.02, 0, -0.006))], 0.012,
     material=M_OLD, cname=RE)
tube("BB shell", [BB + V((0, 0.04, 0)), BB - V((0, 0.04, 0))], 0.02, M_OLD, cname=RE)
tube("Seat clamp", [st(ST_TOP_L - 0.02), st(ST_TOP_L + 0.01)], 0.017, M_RED, cname=RE)
for i, l in enumerate((0.20, 0.30)):  # red cable guides / tags
    tube(f"Red tag {i}", [st(l) - SEAT_DIR * 0.01, st(l) + SEAT_DIR * 0.012], 0.017, M_RED,
         cname=RE)

# ---------------------------------------------------------------- spine
t = (SPINE_Z - DROP.z) / (SS_TOP.z - DROP.z)
US_R = DROP.lerp(SS_TOP, t)                   # on the seatstay (solved x .113)
# Upper spine runs narrow (inside the crank arms: a 175 mm crank tip at TDC reaches z .463,
# level with the spine) and past the toe sweep, then kinks out to the hoop legs.
SPINE_NARROW_Y, SPINE_KINK_X = 0.045, 0.67
pair("Upper spine", [US_R, V((0.34, SPINE_NARROW_Y, SPINE_Z)),
                     V((SPINE_KINK_X, SPINE_NARROW_Y, SPINE_Z)), V((0.765, 0.205, SPINE_Z)),
                     V((HOOP_X_BOT - 0.012, HOOP_W, SPINE_Z))], R_T, bend=0.10)
tube("Hoop platform bar", [V((HOOP_X_BOT - 0.01, HOOP_W, SPINE_Z)),
                           V((HOOP_X_BOT - 0.01, -HOOP_W, SPINE_Z))], R_T)
# tapered platform plate lying on the upper spine tubes (narrow end towards the seat tube)
PZ = SPINE_Z + R_T + 0.0015
half = [(0.65, 0.040), (0.75, 0.170), (0.815, 0.232)]
outline = [(x, y) for x, y in half] + [(x, -y) for x, y in reversed(half)]
bm = bmesh.new()
bm.faces.new([bm.verts.new((x, y, PZ)) for x, y in outline])
plat = mesh_obj("Platform", bm, M_STEEL, "Frame", smooth=False)
sol = plat.modifiers.new("Solidify", 'SOLIDIFY')
sol.thickness, sol.offset = 0.003, 0
cb = bmesh.new()
for hx, hy in [(0.705, 0), (0.75, 0.06), (0.75, -0.06), (0.795, 0.13), (0.795, -0.13), (0.795, 0)]:
    r = bmesh.ops.create_cone(cb, cap_ends=True, segments=32, radius1=0.021, radius2=0.021,
                              depth=0.02)
    bmesh.ops.translate(cb, vec=(hx, hy, PZ), verts=r["verts"])
cut = mesh_obj("Platform cutter", cb, M_STEEL, "Frame", smooth=False)
cut.hide_render = True
cut.hide_set(True)
cut.display_type = 'WIRE'
bo = plat.modifiers.new("Holes", 'BOOLEAN')
bo.operation, bo.object, bo.solver = 'DIFFERENCE', cut, 'EXACT'
for sgn in (1, -1):   # fillet seams where the plate sits on the spine tubes
    weld_line([V((x, sgn * (y - 0.002), PZ)) for x, y in half], 0.0022)
weld_line([V((0.814, -0.235, PZ + 0.001)), V((0.814, 0.235, PZ + 0.001))], 0.0024)
# lower spine: twin tubes from the BB, dipping to the mast foot, out to the box corners
pair("Lower spine", [BB + V((0.01, 0.028, -0.02)), V((0.53, 0.030, 0.228)), V((0.62, 0.046, 0.214)),
                     V((MAST_BOT.x, 0.06, MAST_BOT.z)), BOT_RC + V((-0.01, 0, 0))], R_T,
     bend=0.08)
tube("Mast foot bar", [V((MAST_BOT.x, 0.06, MAST_BOT.z)), V((MAST_BOT.x, -0.06, MAST_BOT.z))],
     R_T)
tube("Mast", [MAST_BOT, MAST_BEND, HT_BOT + HEAD_DIR * 0.01], R_T, bend=0.09)

# ---------------------------------------------------------------- hoop + box top rails
hoop_pts = [LOOP_TC, KINK, HOOP_FOOT, V((HOOP_X_TOP, HOOP_W, HOOP_Z_TOP))]
hoop_pts = hoop_pts + [my(p) for p in reversed(hoop_pts)]
tube("Hoop and top rails", hoop_pts, R_T, bend=0.075)
tube("Head tube (rider)", [HT_BOT, HT_TOP + HEAD_DIR * 0.01], 0.019)
# rear corner posts: bottom rail rear end up into the hoop foot (side photo: same x)
pair("Rear corner post", [BOT_RC, HOOP_FOOT + V((0.0, -0.004, 0.0))], R_T)
# short bridges tying the narrow upper spine to the seat tube
for sgn, lab in ((1, "L"), (-1, "R")):
    pst = st((SPINE_Z - BB.z) / SEAT_DIR.z)
    tube(f"Spine bridge.{lab}", [pst, V((pst.x, sgn * SPINE_NARROW_Y, SPINE_Z))], R_S)

# ---------------------------------------------------------------- box bottom + bracing
pair("Bottom rail", [BOT_RC, BOT_FB, FRONT_LOW], R_T, bend=0.12)
for i, (xt, xb) in enumerate(((0.95, 1.05), (1.27, 1.18), (1.27, 1.38), (KINK.x, BOT_FB.x - 0.03))):
    pair(f"Side brace {i}", [top_rail(xt), bot_rail(xb)], R_S)
for x in (BOT_RC.x + 0.01, 1.15, 1.40, BOT_FB.x - 0.01):
    p = bot_rail(x)
    tube(f"Floor cross {x:.2f}", [p, my(p)], R_S)
# kickstand: two legs off the rear of the box, clamp blocks on the bottom rails
for s, lab in ((1, "L"), (-1, "R")):
    top = V((BOT_RC.x + 0.02, s * (BOT_RC.y + 0.03), BOT_RC.z + 0.02))
    foot = V((STAND_FOOT.x, s * STAND_FOOT.y, 0.0))
    tube(f"Stand leg.{lab}", [top, top.lerp(foot, 0.5) + V((-0.015, 0, 0)), foot], R_T,
         bend=0.2)
    box(f"Stand clamp.{lab}", top + V((0, -s * 0.005, 0.01)), V((0.05, 0.03, 0.06)), M_ALU,
        "Frame", bevel=0.003)

# ---------------------------------------------------------------- front bulkhead
tube("Head tube (front)", [FHT_BOT, FHT_TOP + HEAD_DIR * 0.02], 0.021)
cylinder("Headset cup", FHT_TOP + HEAD_DIR * 0.02, FHT_TOP + HEAD_DIR * 0.05, 0.024, M_ALU,
         "Frame")
def bh(y, z):  # point on the bulkhead plane (parallel to the steering axis)
    return faxis(z) + V((LOOP_TC.x - faxis(LOOP_TC.z).x, y, 0))
# loop: top bar between the top-rail corners, sides down to the lower corners, lower bar
tube("Bulkhead top bar", [LOOP_TC, my(LOOP_TC)], R_T)
pair("Bulkhead side", [LOOP_TC, bh(FRONT_LOW.y + 0.01, 0.62), FRONT_LOW], R_T, bend=0.05)
tube("Bulkhead lower bar", [FRONT_LOW, V((FHT_BOT.x + 0.03, 0.06, FRONT_LOW.z)),
                            V((FHT_BOT.x + 0.03, -0.06, FRONT_LOW.z)), my(FRONT_LOW)], R_T,
     bend=0.04)
tube("Plate bar", [bh(0.17, 0.605), bh(-0.17, 0.605)], R_S)
tilt = Matrix.Rotation(-(math.pi / 2 - HA), 4, 'Y') @ Matrix.Rotation(math.pi / 2, 4, 'Y')
for s, lab in ((1, "L"), (-1, "R")):
    c = bh(s * 0.098, 0.665)
    holed_plate(f"Bulkhead plate.{lab}", Matrix.Translation(c) @ tilt, 0.11, 0.15,
                [(dz, dy) for dz in (-0.026, 0.026) for dy in (-0.033, 0.033)], 0.019,
                M_STEEL, "Frame")
    M4 = Matrix.Translation(c) @ tilt
    for a, b in (((0.055, -0.075), (0.055, 0.075)), ((-0.055, -0.075), (-0.055, 0.075)),
                 ((-0.055, s * 0.075), (0.055, s * 0.075))):   # bottom, top, outer edges
        weld_line([M4 @ V((a[0], a[1], 0)), M4 @ V((b[0], b[1], 0))], 0.0021)

add_welds()
build_welds()

# ---------------------------------------------------------------- box panels
fz = BOT_RC.z + R_T
inset = 0.014
def iy(p, d=inset):
    return V((p.x, p.y - d if p.y > 0 else p.y + d, p.z))
panel("Floor", [(BOT_RC.x + 0.02, -0.19, fz), (BOT_FB.x, -0.19, BOT_FB.z + R_T),
                (BOT_FB.x, 0.19, BOT_FB.z + R_T), (BOT_RC.x + 0.02, 0.19, fz)],
      M_GRIPTAPE, thick=0.006)
for s, lab in ((1, "L"), (-1, "R")):
    S = lambda p: iy(p if s > 0 else my(p))
    panel(f"Side panel.{lab}", [S(BOT_RC + V((0.02, 0, 0))), S(BOT_FB),
                               S(BOT_FB.lerp(FRONT_LOW, 0.5) + V((0.03, 0, 0))),
                               S(KINK.lerp(LOOP_TC, 0.55)), S(KINK),
                               S(HOOP_FOOT + V((0.03, 0, 0.02)))], M_PANEL)
panel("Front wall", [iy(BOT_FB, 0.03), iy(my(BOT_FB), 0.03), iy(my(FRONT_LOW), 0.02),
                     iy(FRONT_LOW, 0.02)], M_PANEL)
panel("Rear wall", [(HOOP_X_BOT + 0.02, -0.24, fz), (HOOP_X_BOT + 0.02, 0.24, fz),
                    (HOOP_X_BOT + 0.01, 0.24, SPINE_Z - 0.02),
                    (HOOP_X_BOT + 0.01, -0.24, SPINE_Z - 0.02)], M_PANEL)

# ---------------------------------------------------------------- steering
STEER = "Steering"
steer_bot, steer_top = HT_BOT - HEAD_DIR * 0.03, HT_TOP + HEAD_DIR * 0.06
tube("Rider steerer", [steer_bot, steer_top], 0.0143, M_DARK, cname=STEER)
box("Steering clamp", HT_BOT + HEAD_DIR * 0.05 + V((0.035, 0, 0)), V((0.04, 0.06, 0.04)),
    M_ALU, STEER, bevel=0.003)
CLAMP = V((0.800, 0, 1.060))
tube("Stem", [steer_top - HEAD_DIR * 0.03, CLAMP], 0.019, M_ALU, cname=STEER)
cylinder("Stem cap", steer_top, steer_top + HEAD_DIR * 0.015, 0.016, M_DARK, STEER)
bar = [V((-0.06, 0.31, 0.03)), V((-0.02, 0.20, 0.02)), V((0, 0.09, 0.0)),
       V((0, -0.09, 0.0)), V((-0.02, -0.20, 0.02)), V((-0.06, -0.31, 0.03))]
tube("Handlebar", [CLAMP + p for p in bar], 0.011, M_DARK, bend=0.06, cname=STEER)
for s, lab in ((1, "L"), (-1, "R")):
    a = CLAMP + V((-0.06, s * 0.31, 0.03))
    d = (a - (CLAMP + V((-0.02, s * 0.20, 0.02)))).normalized()
    cylinder(f"Grip.{lab}", a - d * 0.12, a + d * 0.005, 0.016, M_RUBBER, STEER)
    lever = CLAMP + V((-0.03, s * 0.22, 0.03))
    tube(f"Brake lever.{lab}", [lever, lever + V((0.06, s * 0.04, -0.02)),
                                lever + V((0.07, s * 0.10, -0.03))], 0.005, M_DARK,
         cname=STEER)

crown = faxis(0.52)
f_top = FHT_TOP + HEAD_DIR * 0.055
tube("Fork steerer", [crown, f_top], 0.0143, M_DARK, cname=STEER)
f_arm = crown + HEAD_DIR * 0.005 + V((-0.035, 0, 0))   # steering clamp on the steerer below the head tube
box("Front steering clamp", f_arm, V((0.04, 0.06, 0.03)), M_ALU, STEER, bevel=0.003)
tube("Fork crown", [crown + V((0, 0.06, 0)), crown - V((0, 0.06, 0))], 0.016, M_BLACK,
     cname=STEER)
pair("Fork blade", [crown + V((0, 0.055, 0)), AX_F + V((0, 0.055, 0))], 0.013,
     material=M_BLACK, cname=STEER)

# cable steering: two housings sag from the rider clamp to the front clamp
for s in (1, -1):
    cable(f"Steering cable {s}", HT_BOT + HEAD_DIR * 0.05 + V((0.055, s * 0.02, 0)),
          f_arm + V((-0.02, s * 0.02, 0)), sag=0.22, side=s * 0.04,
          r=0.0035)
cable("Front brake cable", CLAMP + V((-0.02, 0.20, 0.02)), AX_F + V((-0.06, 0.07, 0.09)),
      sag=0.40, side=0.10)
cable("Rear brake cable", CLAMP + V((-0.02, -0.20, 0.02)), V((0.45, -0.07, SPINE_Z)),
      sag=0.25, side=-0.05)
cable("Motor cable", CLAMP + V((0, -0.10, 0.0)), V((0.45, -0.05, SPINE_Z)), sag=0.30,
      side=-0.02)

# ---------------------------------------------------------------- wheels
def wheel(name, axle, r_tyre, width, hub_r, hub_half, spokes, disc_side=1):
    cname = "Wheels"
    tm = width / 2
    major = r_tyre - tm
    torus_xz(f"{name} tyre", axle, major, tm, M_RUBBER, cname)
    rim_r = major - tm * 0.55
    torus_xz(f"{name} rim", axle, rim_r, 0.011, M_DARK, cname, scale_y=1.6)
    torus_xz(f"{name} rim stripe", axle, rim_r + 0.004, 0.0065, M_ALU, cname, scale_y=2.9)
    cylinder(f"{name} hub", axle + V((0, hub_half, 0)), axle - V((0, hub_half, 0)), hub_r,
             M_BLACK if hub_r > 0.05 else M_ALU, cname, segs=48)
    cylinder(f"{name} axle", axle + V((0, hub_half + 0.03, 0)),
             axle - V((0, hub_half + 0.03, 0)), 0.006, M_ALU, cname)
    dy = disc_side * (hub_half + 0.008)
    cylinder(f"{name} disc", axle + V((0, dy, 0)), axle + V((0, dy + 0.002, 0)),
             0.08, M_ALU, cname, segs=64)
    fl = hub_r * (0.85 if hub_r > 0.05 else 1.4)
    step = 2 * math.pi / (spokes / 2)
    for i in range(spokes):
        side = 1 if i % 2 == 0 else -1
        k = i // 2
        a_hub = k * step + (step / 2 if side < 0 else 0)
        lead = 1 if k % 2 == 0 else -1
        a_rim = a_hub + lead * step
        hp = axle + V((fl * math.cos(a_hub), side * hub_half * 0.8, fl * math.sin(a_hub)))
        rp = axle + V((rim_r * math.cos(a_rim), side * 0.003, rim_r * math.sin(a_rim)))
        tube(f"{name} spoke", [hp, rp], 0.0011, M_ALU, cname=cname, res=1)

wheel("Rear wheel", AX_R, R_REAR, 0.052, hub_r=0.085, hub_half=0.032, spokes=36)
wheel("Front wheel", AX_F, R_FRONT, 0.050, hub_r=0.02, hub_half=0.045, spokes=36)
box("Front caliper", AX_F + V((-0.07, 0.07, 0.04)), V((0.04, 0.025, 0.03)), M_DARK, "Wheels",
    bevel=0.004)
box("Rear caliper", AX_R + V((0.05, 0.05, 0.06)), V((0.04, 0.025, 0.03)), M_DARK, "Wheels",
    bevel=0.004)

# ---------------------------------------------------------------- drivetrain (right side)
DT = "Drivetrain"
PITCH = 0.0127                     # 1/2" chain
CRANK_L = 0.175                    # measured 176 mm through the solved side camera
# 38T on a 110 BCD 5-arm spider: ring OD 157 mm through the solved side camera and
# BCD/OD = 0.686 (110/160) both say 38T. 18T freewheel cog: assumed, not measurable.
N_RING, N_COG = 38, 18
CY = -0.055                        # chainline (y of chain centre plane)
def pitch_r(n):
    return PITCH / (2 * math.sin(math.pi / n))
CR_R, COG_R = pitch_r(N_RING), pitch_r(N_COG)

def xz(c, r, ang):
    return V((c.x + r * math.cos(ang), c.y, c.z + r * math.sin(ang)))

def prism(bm, outline, M, t):
    """Extrude a 2D outline (local x, z) through local y in [-t/2, t/2], placed by M."""
    f = [bm.verts.new(M @ V((x, t / 2, z))) for x, z in outline]
    k = [bm.verts.new(M @ V((x, -t / 2, z))) for x, z in outline]
    bm.faces.new(f)
    bm.faces.new(list(reversed(k)))
    n = len(outline)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((f[i], k[i], k[j], f[j]))

def sprocket(name, c, n, y, thick, r_in, material, phase, tip=0.0042):
    """Real tooth form: roller seats on the pitch circle, straight flanks, narrow tips."""
    R, rs, alpha = pitch_r(n), 0.00405, 2 * math.pi / n
    outer = []
    for k in range(n):
        phi = phase + k * alpha
        C = xz(c, R, phi)
        for j in range(9):
            g = math.radians(70 - j * 140 / 8)
            outer.append(xz(C, rs, phi + math.pi + g))
        Rt = R + tip
        dl = 0.0008 / Rt
        outer += [xz(c, Rt, phi + alpha / 2 - dl), xz(c, Rt, phi + alpha / 2 + dl)]
    inner = [xz(c, r_in, math.atan2(p.z - c.z, p.x - c.x)) for p in outer]
    bm = bmesh.new()
    def ring(pts, dy):
        return [bm.verts.new(p + V((0, dy - c.y + y, 0))) for p in pts]
    of, ob_, inf, inb = ring(outer, thick / 2), ring(outer, -thick / 2), ring(inner, thick / 2), \
        ring(inner, -thick / 2)
    m = len(outer)
    for i in range(m):
        j = (i + 1) % m
        bm.faces.new((of[i], of[j], inf[j], inf[i]))
        bm.faces.new((ob_[j], ob_[i], inb[i], inb[j]))
        bm.faces.new((of[j], of[i], ob_[i], ob_[j]))
        bm.faces.new((inf[i], inf[j], inb[j], inb[i]))
    return mesh_obj(name, bm, material, DT, smooth=False)

def chain_loop(c1, r1, c2, r2, y, n=600):
    d = V((c2.x - c1.x, c2.z - c1.z))
    L, a = d.length, math.atan2(d.y, d.x)
    beta = math.acos((r1 - r2) / L)
    pts = []
    for i in range(n + 1):
        t = (a + beta) + (2 * math.pi - 2 * beta) * i / n
        pts.append(V((c1.x + r1 * math.cos(t), y, c1.z + r1 * math.sin(t))))
    for i in range(1, n // 4):
        t = (a - beta) + (2 * beta) * i / (n // 4)
        pts.append(V((c2.x + r2 * math.cos(t), y, c2.z + r2 * math.sin(t))))
    return pts

# pin centres: walk the loop at equal spacing (even link count, closes exactly)
path = chain_loop(BB, CR_R, AX_R, COG_R, CY)
segs = list(zip(path, path[1:] + path[:1]))
L_tot = sum((b - a).length for a, b in segs)
n_links = 2 * round(L_tot / PITCH / 2)
step = L_tot / n_links
pins, acc, si = [], 0.0, 0
for i in range(n_links):
    target = i * step
    while acc + (segs[si][1] - segs[si][0]).length < target:
        acc += (segs[si][1] - segs[si][0]).length
        si += 1
    a, b = segs[si]
    pins.append(a.lerp(b, (target - acc) / (b - a).length))

def top_pin_angle(c, r):
    c = V((c.x, CY, c.z))
    on = [p for p in pins if abs((p - c).length - r) < 2e-4]
    p = max(on, key=lambda q: q.z)
    return math.atan2(p.z - c.z, p.x - c.x)

sprocket(f"Chainring {N_RING}T", BB, N_RING, CY, 0.0024, 0.050, M_DARK, top_pin_angle(BB, CR_R))
sprocket(f"Freewheel cog {N_COG}T", AX_R, N_COG, CY, 0.0024, 0.017, M_DARK,
         top_pin_angle(AX_R, COG_R))
cylinder("Freewheel body", AX_R + V((0, CY + 0.004, 0)), AX_R + V((0, CY + 0.016, 0)), 0.019,
         M_DARK, DT, segs=48)

# chain: alternating inner and outer links, rollers and pins at every pitch
def dogbone(length, h, waist, n=10):
    pts = [(length + h * math.cos(a), h * math.sin(a))
           for a in (math.radians(-90 + 180 * i / n) for i in range(n + 1))]
    pts += [(length * (1 - i / 6), waist + (h - waist) * (2 * i / 6 - 1) ** 2) for i in range(1, 6)]
    pts += [(h * math.cos(a), h * math.sin(a))
            for a in (math.radians(90 + 180 * i / n) for i in range(n + 1))]
    pts += [(length * i / 6, -(waist + (h - waist) * (2 * i / 6 - 1) ** 2)) for i in range(1, 6)]
    return pts
to_y = Matrix.Rotation(math.pi / 2, 4, 'X')
def link_mesh(name, inner):
    """One chain link in local coords: pin A at origin, pin B at (PITCH, 0, 0)."""
    bm = bmesh.new()
    yo, h = (0.0016, 0.0044) if inner else (0.0026, 0.0047)
    for sgn in (1, -1):
        prism(bm, dogbone(PITCH, h, 0.0033 if inner else 0.0035),
              Matrix.Translation((0, sgn * yo, 0)), 0.0008)
    bmesh.ops.create_cone(bm, cap_ends=True, segments=16, radius1=0.00388, radius2=0.00388,
                          depth=0.0024, matrix=to_y)
    bmesh.ops.create_cone(bm, cap_ends=True, segments=10, radius1=0.0018, radius2=0.0018,
                          depth=0.0068, matrix=to_y)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    me.materials.append(M_DARK)
    return me
LINK_MESH = (link_mesh("Chain inner link", True), link_mesh("Chain outer link", False))

# arc-length parametrisation of the closed pitch-line path (pins ride on it)
CH_CUM = [0.0]
for a_, b_ in segs:
    CH_CUM.append(CH_CUM[-1] + (b_ - a_).length)
def chain_at(sv):
    sv %= L_tot
    lo, hi = 0, len(segs) - 1
    while lo < hi:
        mid = (lo + hi) // 2
        if CH_CUM[mid + 1] < sv:
            lo = mid + 1
        else:
            hi = mid
    a_, b_ = segs[lo]
    return a_.lerp(b_, (sv - CH_CUM[lo]) / max((b_ - a_).length, 1e-12))
def link_matrix(sv):
    A, B = chain_at(sv), chain_at(sv + step)
    dv = B - A
    return Matrix.Translation(A) @ Matrix.Rotation(math.atan2(-dv.z, dv.x), 4, 'Y')
CHAIN_LINKS = []
for i in range(n_links):
    ob = bpy.data.objects.new(f"Chain link {i:03d}", LINK_MESH[i % 2])
    ob.matrix_world = link_matrix(i * step)
    CHAIN_LINKS.append(link(ob, DT))
# direction of chain travel along +s when the crank turns forward (ring top moves +x)
_on_ring = [k for k in range(len(path)) if abs((path[k] - V((BB.x, CY, BB.z))).length - CR_R) < 1e-4]
_top = max(_on_ring, key=lambda k: path[k].z)
CHAIN_DIR = 1.0 if (path[(_top + 1) % len(path)] - path[_top]).x > 0 else -1.0

# 5-arm spider (110 BCD) and flat tapered crank arms
CRANK = math.radians(-20)
bm = bmesh.new()
ys = CY - 0.006
for k in range(5):
    ang = CRANK + math.radians(36 + 72 * k)
    M = Matrix.Translation(BB + V((0, ys, 0))) @ Matrix.Rotation(-ang, 4, 'Y')
    prism(bm, [(0.0, -0.007), (0.055, -0.005), (0.055, 0.005), (0.0, 0.007)], M, 0.005)
    boss = xz(BB, 0.055, ang) + V((0, ys + 0.0005, 0))
    bmesh.ops.create_cone(bm, cap_ends=True, segments=24, radius1=0.0075, radius2=0.0075,
                          depth=0.006, matrix=Matrix.Translation(boss) @ to_y)
mesh_obj("Spider", bm, M_ALU, DT, smooth=False)
bm = bmesh.new()
for k in range(5):   # chainring bolts
    ang = CRANK + math.radians(36 + 72 * k)
    p = xz(BB, 0.055, ang)
    bmesh.ops.create_cone(bm, cap_ends=True, segments=24, radius1=0.0052, radius2=0.0052,
                          depth=0.002, matrix=Matrix.Translation(p + V((0, ys - 0.0035, 0))) @ to_y)
    bmesh.ops.create_cone(bm, cap_ends=True, segments=24, radius1=0.004, radius2=0.004,
                          depth=0.002, matrix=Matrix.Translation(p + V((0, CY + 0.002, 0))) @ to_y)
mesh_obj("Chainring bolts", bm, M_ALU, DT, smooth=False)
bm = bmesh.new()
for k in range(5):
    p = xz(BB, 0.055, CRANK + math.radians(36 + 72 * k))
    bmesh.ops.create_cone(bm, cap_ends=True, segments=6, radius1=0.0021, radius2=0.0021,
                          depth=0.0006, matrix=Matrix.Translation(p + V((0, ys - 0.0046, 0))) @ to_y)
mesh_obj("Bolt sockets", bm, M_BLACK, DT, smooth=False)
cylinder("Spider hub", BB + V((0, ys + 0.004, 0)), BB + V((0, ys - 0.006, 0)), 0.021, M_ALU, DT,
         segs=48)

def crank_outline(L, w0, w1, n=10):
    pts = [(L + w1 * math.cos(a), w1 * math.sin(a))
           for a in (math.radians(-90 + 180 * i / n) for i in range(n + 1))]
    pts += [(w0 * math.cos(a), w0 * math.sin(a))
            for a in (math.radians(90 + 180 * i / n) for i in range(n + 1))]
    return pts
def platform_pedal(lab, tip, s):
    """Alloy flat pedal inside the same 100 x 95 x 20 mm envelope the clearance and
    pedal-strike checks use: two end plates, front/rear bars, spindle barrel, 16 pins."""
    bm = bmesh.new()
    yc = s * 0.065
    for yo in (-0.0435, 0.0435):              # inner/outer end plates, 8 mm
        bmesh.ops.create_cube(bm, size=1.0, matrix=Matrix.Translation(tip + V((0, yc + s * yo, 0)))
                              @ Matrix.Diagonal((0.100, 0.008, 0.018, 1)))
    for xo in (-0.044, 0.044):                # front/rear bars carry the pins
        bmesh.ops.create_cube(bm, size=1.0, matrix=Matrix.Translation(tip + V((xo, yc, 0)))
                              @ Matrix.Diagonal((0.012, 0.087, 0.014, 1)))
        for yo in (-0.030, -0.010, 0.010, 0.030):
            for zs in (1, -1):                # pins end at +-10 mm (inside the envelope)
                bmesh.ops.create_cone(bm, cap_ends=True, segments=8, radius1=0.0017,
                                      radius2=0.0012, depth=0.003,
                                      matrix=Matrix.Translation(tip + V((xo, yc + yo, zs * 0.0085)))
                                      @ (Matrix.Identity(4) if zs > 0 else Matrix.Rotation(math.pi, 4, 'X')))
    for xo in (-0.016, 0.016):                # thin mid ribs
        bmesh.ops.create_cube(bm, size=1.0, matrix=Matrix.Translation(tip + V((xo, yc, 0)))
                              @ Matrix.Diagonal((0.005, 0.087, 0.010, 1)))
    ob = mesh_obj(f"Pedal.{lab} body", bm, M_DARK, DT, smooth=False)
    bv = ob.modifiers.new("Bevel", 'BEVEL')
    bv.width, bv.segments, bv.limit_method = 0.0012, 2, 'ANGLE'
    # sealed-bearing barrel round the spindle
    cylinder(f"Pedal.{lab} barrel", tip + V((0, s * 0.020, 0)), tip + V((0, s * 0.110, 0)), 0.0095,
             M_ALU, DT, segs=32)
    cylinder(f"Pedal.{lab} dust cap", tip + V((0, s * 0.110, 0)), tip + V((0, s * 0.1125, 0)), 0.008,
             M_BLACK, DT, segs=24)

for s, ang, lab in ((-1, CRANK, "R"), (1, CRANK + math.pi, "L")):
    y = s * 0.072
    tip = BB + V((CRANK_L * math.cos(ang), y, CRANK_L * math.sin(ang)))
    bm = bmesh.new()
    prism(bm, crank_outline(CRANK_L, 0.017, 0.012),
          Matrix.Translation(BB + V((0, y, 0))) @ Matrix.Rotation(-ang, 4, 'Y'), 0.013)
    ob = mesh_obj(f"Crank.{lab}", bm, M_ALU, DT, smooth=False)
    bv = ob.modifiers.new("Bevel", 'BEVEL')
    bv.width, bv.segments = 0.003, 3
    cylinder(f"Crank bolt.{lab}", BB + V((0, y + s * 0.006, 0)), BB + V((0, y + s * 0.009, 0)),
             0.009, M_DARK, DT)
    cylinder(f"Pedal spindle.{lab}", tip, tip + V((0, s * 0.02, 0)), 0.006, M_DARK, DT)
    platform_pedal(lab, tip, s)
cylinder("BB axle", BB + V((0, 0.072, 0)), BB - V((0, 0.072, 0)), 0.008, M_DARK, DT)

# ---------------------------------------------------------------- seat
seat_top = st(CLAMP_L)
tube("Seatpost", [st(ST_TOP_L - 0.06), seat_top], 0.0136, M_ALU, cname="Seat")
bm = bmesh.new()
bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=1.0)
for v in bm.verts:  # wide at the back, narrow nose towards +X
    x = v.co.x
    w = 0.085 * (1.0 - 0.65 * max(0.0, x) ** 1.4) if x > 0 else 0.085
    v.co = V((v.co.x * 0.14, v.co.y * w, v.co.z * 0.026 + 0.014 * x * x))
ob = mesh_obj("Saddle", bm, M_SADDLE, "Seat")
ob.location = seat_top + V((0.03, 0, 0.035))
ob.modifiers.new("Subsurf", 'SUBSURF').levels = 1
tube("Saddle clamp", [seat_top, seat_top + V((0.0, 0, 0.03))], 0.017, M_ALU, cname="Seat")

# ---------------------------------------------------------------- rig + kinematic simulation
G = 9.81
def empty(name, matrix, parent=None, size=0.05):
    e = bpy.data.objects.new(name, None)
    e.empty_display_size = size
    link(e, "Rig")
    bpy.context.view_layer.update()        # parents' matrix_world must be evaluated first
    if parent:
        e.parent = parent
    e.matrix_world = matrix
    bpy.context.view_layer.update()
    return e

def adopt(parent, objs):
    bpy.context.view_layer.update()        # fresh objects have identity matrix_world until then
    pw = parent.matrix_world.copy()
    for o in objs:
        mw = o.matrix_world.copy()
        o.parent = parent
        o.matrix_parent_inverse = pw.inverted()
        o.matrix_world = mw

def named(*prefixes):
    return [o for o in bpy.data.objects if o.name.startswith(prefixes) and o.parent is None
            and o.type in ('MESH', 'CURVE')]

def Z_TO(d):
    return V((0, 0, 1)).rotation_difference(d).to_matrix().to_4x4()

RIG = {}
RIG["root"] = empty("Bike", Matrix.Identity(4), size=0.3)
RIG["rear"] = empty("Rear wheel pivot", Matrix.Translation(AX_R), RIG["root"])
RIG["crank"] = empty("Crank pivot", Matrix.Translation(BB), RIG["root"])
RIG["steer_f_axis"] = empty("Front steering axis", Matrix.Translation(FHT_TOP) @ Z_TO(HEAD_DIR),
                            RIG["root"])
RIG["steer_f"] = empty("Front steering", RIG["steer_f_axis"].matrix_world, RIG["steer_f_axis"])
RIG["front"] = empty("Front wheel pivot", Matrix.Translation(AX_F), RIG["steer_f"])
RIG["steer_r_axis"] = empty("Rider steering axis", Matrix.Translation(HT_BOT) @ Z_TO(HEAD_DIR),
                            RIG["root"])
RIG["steer_r"] = empty("Rider steering", RIG["steer_r_axis"].matrix_world, RIG["steer_r_axis"])
# kickstand folds forward under the box while riding (pivot at the clamp blocks)
for s_, lab in ((1, "L"), (-1, "R")):
    top = V((BOT_RC.x + 0.02, s_ * (BOT_RC.y + 0.03), BOT_RC.z + 0.02))
    RIG[f"stand.{lab}"] = empty(f"Stand pivot.{lab}", Matrix.Translation(top), RIG["root"])
    adopt(RIG[f"stand.{lab}"], named(f"Stand leg.{lab}"))
    RIG[f"stand.{lab}"].rotation_euler = (0, math.radians(-88), 0)
pedal_piv = {}
for s_, ang, lab in ((-1, CRANK, "R"), (1, CRANK + math.pi, "L")):
    tip = BB + V((CRANK_L * math.cos(ang), s_ * 0.072, CRANK_L * math.sin(ang)))
    pedal_piv[lab] = empty(f"Pedal pivot.{lab}", Matrix.Translation(tip), RIG["crank"])
adopt(RIG["rear"], named("Rear wheel", "Freewheel cog", "Freewheel body"))
adopt(RIG["front"], named("Front wheel"))
adopt(RIG["steer_f"], named("Fork ", "Front steering clamp", "Front caliper"))
adopt(RIG["steer_r"], named("Rider steerer", "Steering clamp", "Stem", "Handlebar", "Grip.",
                            "Brake lever."))
for lab in ("L", "R"):
    adopt(pedal_piv[lab], named(f"Pedal.{lab}"))
adopt(RIG["crank"], named("Chainring", "Spider", "Bolt sockets", "Crank", "Pedal spindle", "BB axle"))
adopt(RIG["root"], [o for o in bpy.data.objects if o.parent is None and o.type in ('MESH', 'CURVE')])

SIM = dict(fps=24, seconds=8.0, cadence_rpm=70.0, steer_amp_deg=7.0, steer_period_s=4.0)
def steer_at(t):
    ramp_ = min(1.0, t / 1.0)
    return math.radians(SIM["steer_amp_deg"]) * ramp_ * math.sin(2 * math.pi * t / SIM["steer_period_s"])

def simulate():
    """Kinematics integrated per frame (rear-axle-referenced kinematic bicycle model):
    crank w -> chain speed w*N_ring*p/2pi -> cog/wheel w*N_ring/N_cog (freewheel engaged) ->
    v = w_wheel*R_rear; yaw rate v*tan(delta*sin(HA))/WB; lean = atan(v*yaw_rate/g)."""
    fps, n = SIM["fps"], int(SIM["fps"] * SIM["seconds"])
    w_c = SIM["cadence_rpm"] * 2 * math.pi / 60
    WB = AX_F.x - AX_R.x
    x = y = psi = 0.0
    states = []
    for f in range(n + 1):
        t = f / fps
        th_c = w_c * t
        # a chain transmits whole teeth: one pitch per tooth on both sprockets, so the drive
        # ratio is N_RING/N_COG (2.111), not CR_R/COG_R (2.103, polygon effect averaged out)
        s_ch = th_c / (2 * math.pi) * N_RING * step
        th_w = th_c * N_RING / N_COG
        v = w_c * N_RING / N_COG * R_REAR
        d = steer_at(t)
        psi_dot = v * math.tan(d * math.sin(HA)) / WB
        lean = math.atan(v * psi_dot / G)
        states.append(dict(f=f + 1, t=t, th_c=th_c, s_ch=s_ch, th_w=th_w,
                           th_f=th_w * R_REAR / R_FRONT, d=d, x=x, y=y, psi=psi, lean=lean, v=v))
        x += v * math.cos(psi) / fps
        y += v * math.sin(psi) / fps
        psi += psi_dot / fps
    return states

def keyframe_sim(states):
    scene.render.fps = SIM["fps"]
    scene.frame_start, scene.frame_end = 1, states[-1]["f"]
    for st_ in states:
        f = st_["f"]
        r = RIG["root"]
        r.location = (st_["x"], st_["y"], 0)
        r.rotation_euler = (-st_["lean"], 0, st_["psi"])
        r.keyframe_insert("location", frame=f)
        r.keyframe_insert("rotation_euler", frame=f)
        for key, ang in (("rear", st_["th_w"]), ("front", st_["th_f"]), ("crank", st_["th_c"])):
            RIG[key].rotation_euler = (0, ang, 0)
            RIG[key].keyframe_insert("rotation_euler", index=1, frame=f)
        for lab in ("L", "R"):
            pedal_piv[lab].rotation_euler = (0, -st_["th_c"], 0)
            pedal_piv[lab].keyframe_insert("rotation_euler", index=1, frame=f)
        for key in ("steer_f", "steer_r"):
            RIG[key].rotation_euler = (0, 0, st_["d"])
            RIG[key].keyframe_insert("rotation_euler", index=2, frame=f)
        for i, ob in enumerate(CHAIN_LINKS):   # parent inverse is identity: basis = bike-local
            ob.matrix_basis = link_matrix(i * step + CHAIN_DIR * st_["s_ch"])
            ob.keyframe_insert("location", frame=f)
            ob.keyframe_insert("rotation_euler", frame=f)
    scene.frame_set(1)

SIM_STATES = simulate()
keyframe_sim(SIM_STATES)

# ---------------------------------------------------------------- engineering spec
SADDLE_TOP = seat_top + V((0.0, 0, 0.06))
# Tube sections: outside diameters measured (model radii); walls are ASSUMED typical sizes
# for hand-built mild-steel cargo frames / 1990s hi-ten donor frames (not measurable).
# `wall=2.0` on the command line changes the new-tube wall for a sensitivity run.
S235, HITEN = 235e6, 300e6
def section_for(t):
    from engineering import tube_section
    n, D = t["name"], 2 * t["r"]
    if t["old"]:
        wall = {"Seat tube": 1.0, "BB shell": 3.0}.get(n.split(".")[0], 0.9)
        return tube_section(D, wall / 1000), HITEN
    if n.startswith("Head tube"):
        return tube_section(D, 0.0025), S235
    wall = next((float(a[5:]) for a in sys.argv if a.startswith("wall=")), 1.5) / 1000
    return tube_section(D, wall if D > 0.022 else min(wall, 0.0012)), S235

def tube_list():
    return [dict(name=t["name"], pts=[list(p) for p in t["pts"]], r=t["r"], old=t["old"])
            for t in FRAME_TUBES]

def engineering_spec():
    from engineering import tube_section
    tubes = [t for t in tube_list() if not t["name"].startswith("Stand")]
    members = []
    for t in tubes:
        sec, fy = section_for(t)
        members.append(dict(name=t["name"], pts=t["pts"], sec=sec, fy=fy))
    drop_l, drop_r = list(DROP), list(my(DROP))
    m_rider, m_cargo = 85.0, 100.0
    floor = [list(bot_rail(x)) for x in (0.90, 1.15, 1.40)]
    floor += [list(my(V(p))) for p in floor]
    cargo_each = m_cargo * G / len(floor)
    fork = (list(FHT_BOT), list(AX_F), tube_section(0.028, 0.002), "Fork (rigid, assumed)", 450e6)
    axle_r = (drop_l, drop_r, tube_section(0.012, 0.0059), "Rear axle 12 mm", 600e6)
    sup_ride = [(drop_l, [0, 1, 2]), (drop_r, [1, 2]), (list(AX_F), [1, 2])]
    seat_node = list(st(ST_TOP_L))
    bars_node = list(HT_TOP)
    def rider(f_dyn, frac_seat=0.65, frac_bars=0.15, frac_ped=0.20):
        W = m_rider * G * f_dyn
        return [(seat_node, [0, 0, -frac_seat * W]), (bars_node, [0, 0, -frac_bars * W]),
                (list(BB), [0, 0, -frac_ped * W])]
    T_chain = 1000 * CRANK_L / CR_R
    top_run = (V((AX_R.x, 0, AX_R.z + COG_R)) - V((BB.x, 0, BB.z + CR_R))).normalized()
    def pedal(side):
        y = side * (0.072 + 0.065)
        # 1000 N down on one pedal, crank forward; into the BB bearings as force + moment
        return [(list(BB), [0, 0, -1000, 1000 * y * -1, 0, 0]),
                (list(BB), list(top_run * T_chain)),
                (drop_r, list(-top_run * T_chain))]
    a_brake = 0.6 * G
    cases = [
        dict(name="ride_bump_2g", why="85 kg rider + 100 kg cargo, x2 dynamic factor (pothole)",
             loads=rider(2.0) + [(p, [0, 0, -2 * cargo_each]) for p in floor], supports=sup_ride),
        dict(name="ride_static_1g", why="85 kg rider + 100 kg cargo standing still",
             loads=rider(1.0) + [(p, [0, 0, -cargo_each]) for p in floor], supports=sup_ride),
        dict(name="pedal_right", why="1000 N on the right pedal, crank forward, + chain tension "
                                     "(alternating part only, for fatigue)",
             loads=pedal(-1), supports=sup_ride),
        dict(name="pedal_left", why="same, left pedal (fatigue partner case)",
             loads=pedal(1), supports=sup_ride),
        dict(name="brake_front_0.6g", why="0.6 g front-wheel stop, loaded (inertia at CoMs)",
             loads=rider(1.0) + [(p, [0, 0, -cargo_each]) for p in floor]
                   + [(seat_node, [m_rider * 0.8 * a_brake, 0, 0]),
                      (bars_node, [m_rider * 0.2 * a_brake, 0, 0])]
                   + [(p, [m_cargo / len(floor) * a_brake, 0, 0]) for p in floor],
             supports=[(drop_l, [1, 2]), (drop_r, [2]), (list(AX_F), [0, 1, 2])]),
    ]
    disp = {"box floor centre": list(bot_rail(1.20)), "BB": list(BB),
            "rider head tube top": list(HT_TOP), "front head tube top": list(FHT_TOP)}
    for c in cases:
        c["report_disp"] = disp
        c["report_reactions"] = {"rear L": drop_l, "rear R": drop_r, "front axle": list(AX_F)}
    # welded plates as equivalent 40x3 beams (FEA only): bulkhead plates tie the front head
    # tube to the loop sides; the platform plate braces the spine/hoop junction in-plane and
    # is welded around the mast at its tip (photos)
    peq, fyp = tube_section(0.040, 0.003), S235
    def side_y(z):   # bulkhead side tube y at height z (LOOP_TC -> bh(.16,.62) -> FRONT_LOW)
        return 0.16 + (LOOP_TC.y - 0.16) * (z - 0.62) / (LOOP_TC.z - 0.62)
    mz = MAST_BOT.lerp(MAST_BEND, (SPINE_Z - MAST_BOT.z) / (MAST_BEND.z - MAST_BOT.z))
    for sgn in (1, -1):
        for z in (0.63, 0.70):
            members.append(dict(name=f"Bulkhead plate {sgn:+d} z{z} (equiv.)",
                                pts=[list(faxis(z)), list(bh(sgn * side_y(z), z))], sec=peq, fy=fyp))
        members.append(dict(name=f"Platform-mast tie {sgn:+d} (equiv.)",
                            pts=[list(mz), [mz.x, sgn * SPINE_NARROW_Y, SPINE_Z]], sec=peq, fy=fyp))
        members.append(dict(name=f"Platform {sgn:+d} (equiv.)",
                            pts=[[SPINE_KINK_X, sgn * SPINE_NARROW_Y, SPINE_Z],
                                 [HOOP_X_BOT - 0.01, -sgn * 0.20, SPINE_Z]], sec=peq, fy=fyp))
    if "fix=brace" in sys.argv:   # HYPOTHETICAL: BB -> upper spine diagonal
        for sgn in (1, -1):
            members.append(dict(name=f"Proposed BB-spine brace {sgn:+d}",
                                pts=[[BB.x + 0.01, sgn * 0.028, BB.z - 0.02],
                                     [0.60, sgn * SPINE_NARROW_Y, SPINE_Z]],
                                sec=tube_section(0.022, 0.0015), fy=S235))
    if "fix=step" in sys.argv:    # HYPOTHETICAL: triangulate the top-chord step at the hoop
        for sgn in (1, -1):
            x0 = 0.71
            y0 = SPINE_NARROW_Y + (0.205 - SPINE_NARROW_Y) * (x0 - SPINE_KINK_X) / (0.765 - SPINE_KINK_X)
            members.append(dict(name=f"Proposed step brace {sgn:+d}",
                                pts=[[x0, sgn * y0, SPINE_Z], list(BOT_RC if sgn > 0 else my(BOT_RC))],
                                sec=tube_section(0.025, 0.0015), fy=S235))
    fea = dict(members=members, links=[fork, axle_r], join_tol=0.006, root=drop_l, cases=cases)
    clear = dict(
        frame=tubes, BB=list(BB), crank_len=CRANK_L, crank_y=0.072, crank_t=0.013, crank_w=0.030,
        pedal_off=0.065, pedal_size=[0.100, 0.095, 0.020],
        # EU 44 shoe (290 mm), ball at 70 % of length: (x from ball, width, height) sections
        shoe=dict(sections=[(0.030, 0.087, 0.10, 0.045), (-0.050, 0.030, 0.11, 0.065),
                            (-0.140, -0.050, 0.10, 0.100), (-0.203, -0.140, 0.08, 0.085)]),
        ring_r=CR_R + 0.0042, ring_t=0.0024, chain_y=CY,
        chain_path=[list(p) for p in path], chain_w=0.0075, chain_h=0.0094, chain_skip=["Dropout.R"],
        min_gap=dict(crank=0.005, pedal=0.010, foot=0.010, chainring=0.004, chain=0.004, tyre=0.006),
        tyres=[("rear", list(AX_R), R_REAR, 0.052, []), ("front", list(AX_F), R_FRONT, 0.050, [])],
        steer_axis_pt=list(FHT_TOP), steer_axis_dir=list(HEAD_DIR), front_axle=list(AX_F),
        front_tyre=(R_FRONT, 0.050), steer_skip=["Head tube (front)"], min_lock_deg=35.0,
        head_angle=round(math.degrees(HA), 1),
        saddle_top=list(SADDLE_TOP), knee_y=0.10, knee_r=0.055, hand_r=0.045,
        grip=list(CLAMP + V((-0.06, 0.31, 0.03))), rider_axis_pt=list(HT_BOT),
        rider_axis_dir=list(HEAD_DIR),
        teeth=(N_RING, N_COG), rear_R=R_REAR, chain_links=n_links, rear_axle=list(AX_R))
    return clear, fea

def run_checks():
    sys.path.insert(0, os.path.join(HERE, "tools"))
    import json, importlib, engineering
    importlib.reload(engineering)
    clear, fea = engineering_spec()
    rep = engineering.clearance(clear)
    fe = engineering.frame_fea(fea)
    # fatigue: alternating pedal cases -> stress range per element (conservative sum)
    pr, pl = fe["cases"]["pedal_right"]["elements"], fe["cases"]["pedal_left"]["elements"]
    worst = sorted(((a["vm"] + b["vm"], a["member"]) for a, b in zip(pr, pl)), reverse=True)[:5]
    fe["fatigue"] = dict(note="stress range ~ |vm_R| + |vm_L|; compare with welded-tube detail "
                              "category ~40 MPa at 2e6 cycles (EN 1993-1-9 hollow-section joints)",
                         worst=[dict(member=m, range_MPa=round(r / 1e6, 1)) for r, m in worst])
    out = dict(clearance=rep, fea={k: v for k, v in fe.items() if k != "cases"},
               cases={k: {kk: vv for kk, vv in v.items() if kk != "elements"}
                      for k, v in fe["cases"].items()})
    json.dump(out, open(os.path.join(HERE, "engineering.json"), "w"), indent=1)
    json.dump({k: v["elements"] for k, v in fe["cases"].items()},
              open(os.path.join(HERE, "fea_elements.json"), "w"))
    print("=== CHECKS")
    for c in rep["checks"]:
        print(f"{'OK  ' if c['ok'] else 'FAIL'} {c['check']}: {c['value']} {c['unit']} "
              f"(limit {c['limit']})")
    print("=== NUMBERS", json.dumps(rep["numbers"]))
    print("=== FEA nodes", fe["n_nodes"], "elems", fe["n_elems"], "dropped", fe["dropped"])
    print("=== UNJOINED ENDS", fe["unjoined"])
    for k, v in out["cases"].items():
        print(k, "max vm", v["max_vm_MPa"], "MPa  min SF", v["min_sf"], "disp", v.get("disp_mm"))
        for w in v["worst"][:4]:
            print("    ", w)
    print("=== FATIGUE", fe["fatigue"]["worst"])
    st1 = out["cases"]["ride_static_1g"]
    print("=== 1g static: payload (rider+cargo) at first yield ~", round(185 * st1["min_sf"]), "kg")

# ---------------------------------------------------------------- stage
bm = bmesh.new()
bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=120)
g = mesh_obj("Snow", bm, M_SNOW, "Stage", smooth=False)
g.location = (30, 0, 0)
# snow: wind-packed drifts as noise so motion reads in the animation
nd = Nodes(M_SNOW)
sb = M_SNOW.node_tree.nodes["Principled BSDF"]
co = nd("ShaderNodeTexCoord").outputs["Object"]
big = noise(nd, co, 0.35, 6, 0.55)
small = noise(nd, co, 9.0, 8, 0.6)
mixh = nd("ShaderNodeMath")
mixh.operation = 'ADD'
nd.link(big.outputs["Fac"], mixh.inputs[0])
nd.link(small.outputs["Fac"], mixh.inputs[1])
bump = nd("ShaderNodeBump", Strength=0.35, Distance=0.02)
nd.link(mixh.outputs[0], bump.inputs["Height"])
nd.link(bump.outputs["Normal"], sb.inputs["Normal"])
cr = ramp(nd, big.outputs["Fac"], [(0.35, (0.80, 0.82, 0.87)), (0.65, (0.93, 0.94, 0.97))])
nd.link(cr.outputs["Color"], sb.inputs["Base Color"])
sb.inputs["Subsurface Weight"].default_value = 0.25

world = bpy.data.worlds.new("World")
scene.world = world
world.use_nodes = True
bg = world.node_tree.nodes["Background"]
bg.inputs["Color"].default_value = (0.75, 0.78, 0.84, 1)
bg.inputs["Strength"].default_value = 0.9
ARGV = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
def arg(key, default=None):
    return next((a.split("=", 1)[1] for a in ARGV if a.startswith(key + "=")), default)
HDRI = arg("hdri")
if HDRI:   # Poly Haven CC0 HDRIs in hdri/ (snowy_field, snowy_park_01, winter_sky)
    envt = world.node_tree.nodes.new("ShaderNodeTexEnvironment")
    envt.image = bpy.data.images.load(os.path.join(HERE, "hdri", HDRI + ".hdr"))
    mp = world.node_tree.nodes.new("ShaderNodeMapping")
    tc = world.node_tree.nodes.new("ShaderNodeTexCoord")
    world.node_tree.links.new(tc.outputs["Generated"], mp.inputs["Vector"])
    mp.inputs["Rotation"].default_value = (0, 0, math.radians(float(arg("hdri_rot", "40"))))
    world.node_tree.links.new(mp.outputs[0], envt.inputs["Vector"])
    world.node_tree.links.new(envt.outputs["Color"], bg.inputs["Color"])
    bg.inputs["Strength"].default_value = float(arg("hdri_strength", "1.0"))

sun_d = bpy.data.lights.new("Sun", 'SUN')
sun_d.energy = 2.0 if not HDRI else float(arg("sun", "1.2"))
sun_d.angle = math.radians(15)
sun = link(bpy.data.objects.new("Sun", sun_d), "Stage")
sun.rotation_euler = (math.radians(35), math.radians(10), math.radians(-60))

target = link(bpy.data.objects.new("Target", None), "Stage")
target.location = (0.95, 0, 0.5)

def camera(name, loc, lens=40, ortho=None, aim=None):
    cd = bpy.data.cameras.new(name)
    cd.lens = lens
    if ortho:
        cd.type, cd.ortho_scale = 'ORTHO', ortho
    cam = link(bpy.data.objects.new(name, cd), "Stage")
    cam.location = loc
    c = cam.constraints.new('TRACK_TO')
    if aim is not None:
        t = link(bpy.data.objects.new(name + " target", None), "Stage")
        t.location = aim
    c.target = target if aim is None else t
    c.track_axis, c.up_axis = 'TRACK_NEGATIVE_Z', 'UP_Y'
    return cam

cams = {
    "front_right": camera("Cam front right", (3.3, -2.6, 1.7), lens=35),
    "side": camera("Cam side", (0.95, -6.0, 0.5), ortho=2.9),
    "rear_right": camera("Cam rear right", (-2.1, -1.7, 1.5), lens=30),
    "rear": camera("Cam rear", (-3.1, 0.0, 1.5), lens=45),
    "top": camera("Cam top", (0.95, 0.0, 6.0), ortho=2.9),
}
DETAIL = {
    "detail_drivetrain": camera("Cam detail drivetrain", (0.33, -0.62, 0.42), 50, aim=BB + V((-0.08, 0, 0))),
    "detail_box_welds": camera("Cam detail box welds", (1.18, -0.62, 0.62), 50,
                               aim=V((1.27, -0.23, 0.32))),
    "detail_spine": camera("Cam detail spine", (0.40, -0.75, 0.90), 40, aim=V((0.72, 0, 0.42))),
    "detail_bulkhead": camera("Cam detail bulkhead", (1.30, -0.55, 0.95), 45,
                              aim=V((1.74, 0, 0.62))),
}
cams.update(DETAIL)
HERO = {   # low, wide, dramatic
    "hero_low": camera("Cam hero low", (3.05, -1.55, 0.32), 24, aim=V((1.05, 0, 0.55))),
    "hero_drive": camera("Cam hero drivetrain", (0.15, -0.95, 0.30), 35, aim=V((0.42, -0.05, 0.32))),
    "hero_rear": camera("Cam hero rear", (-1.9, -0.9, 1.05), 28, aim=V((0.9, 0, 0.6))),
    "hero_top": camera("Cam hero top", (1.2, -1.1, 2.6), 28, aim=V((0.95, 0, 0.3))),
}
for c_ in HERO.values():
    c_.data.dof.use_dof = True
    c_.data.dof.aperture_fstop = 2.8
HERO["hero_drive"].data.dof.focus_distance = (V((0.15, -0.95, 0.30)) - V((0.42, -0.06, 0.30))).length
HERO["hero_low"].data.dof.focus_distance = (V((3.05, -1.55, 0.32)) - AX_F).length
HERO["hero_rear"].data.dof.focus_distance = 2.2
HERO["hero_top"].data.dof.focus_distance = 2.9
cams.update(HERO)
# chase camera: follows the bike's position (not its lean), aimed at the box
chase_t = bpy.data.objects.new("Chase target", None)
link(chase_t, "Stage")
chase_t.parent = RIG["root"]
chase_t.location = (0.95, 0, 0.55)
cd = bpy.data.cameras.new("Cam chase")
cd.lens = 32
chase = link(bpy.data.objects.new("Cam chase", cd), "Stage")
chase.location = (-1.7, -2.7, 1.25)
cl = chase.constraints.new('COPY_LOCATION')
cl.target, cl.use_offset = RIG["root"], True
tt = chase.constraints.new('TRACK_TO')
tt.target, tt.track_axis, tt.up_axis = chase_t, 'TRACK_NEGATIVE_Z', 'UP_Y'
cams["chase"] = chase
# bike-mounted drivetrain camera (rides along, leans with the bike): chain over the teeth
cd = bpy.data.cameras.new("Cam onboard drive")
cd.lens, cd.dof.use_dof, cd.dof.aperture_fstop = 30, True, 4.0
onb = link(bpy.data.objects.new("Cam onboard drive", cd), "Stage")
onb.parent = RIG["root"]
onb.location = (0.05, -0.78, 0.36)
onb_t = link(bpy.data.objects.new("Onboard target", None), "Stage")
onb_t.parent, onb_t.location = RIG["root"], (0.30, -0.05, 0.30)
tt = onb.constraints.new('TRACK_TO')
tt.target, tt.track_axis, tt.up_axis = onb_t, 'TRACK_NEGATIVE_Z', 'UP_Y'
cd.dof.focus_object = onb_t
cams["onboard"] = onb
cams["top"].constraints.clear()
cams["top"].rotation_euler = (0, 0, 0)
scene.camera = cams["front_right"]

# ---------------------------------------------------------------- render
# Cycles; CPU by default (headless runs on temple have no Metal device), gpu=HIP|METAL|OPTIX
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
GPU = arg("gpu")
if GPU:
    cp = bpy.context.preferences.addons["cycles"].preferences
    cp.compute_device_type = GPU
    cp.get_devices()
    for d_ in cp.devices:
        d_.use = d_.type == GPU
    print("GPU devices:", [(d_.name, d_.type, d_.use) for d_ in cp.devices])
    scene.cycles.device = 'GPU'
scene.cycles.samples = int(arg("samples", "48"))
scene.cycles.use_denoising = True
scene.render.resolution_x, scene.render.resolution_y = 1600, 1200
scene.view_settings.view_transform = 'AgX'

bpy.ops.file.pack_all()   # HDRI travels inside the .blend
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "cargobike.blend"))

out = os.path.join(HERE, "renders")
os.makedirs(out, exist_ok=True)
args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []

if "--check" in args:
    run_checks()

def fea_heatmap(case, exaggerate=4.0):
    """Beam elements coloured by von Mises / yield, nodes moved by U x exaggerate."""
    import json
    els = json.load(open(os.path.join(HERE, "fea_elements.json")))[case]
    for cn in list(COLLS):
        if cn != "Stage":
            COLLS[cn].hide_render = True
    m = bpy.data.materials.new("FEA ratio")
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    at = nt.nodes.new("ShaderNodeAttribute")
    at.attribute_name = "ratio"
    rp = nt.nodes.new("ShaderNodeValToRGB")
    els_r = rp.color_ramp.elements
    for pos, col in ((0.0, (0.05, 0.15, 0.9)), (0.25, (0.0, 0.8, 0.6)), (0.5, (0.9, 0.9, 0.1)),
                     (0.75, (1.0, 0.4, 0.0)), (1.0, (0.9, 0.0, 0.05))):
        el = els_r.new(pos) if pos not in (0.0, 1.0) else (els_r[0] if pos == 0 else els_r[-1])
        el.position, el.color = pos, (*col, 1)
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Strength"].default_value = 1.6
    o = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(at.outputs["Fac"], rp.inputs["Fac"])
    nt.links.new(rp.outputs["Color"], em.inputs["Color"])
    nt.links.new(em.outputs[0], o.inputs[0])
    bm = bmesh.new()
    lay = bm.verts.layers.float.new("ratio")
    for e in els:
        a_ = V(e["a"]) + V(e["ua"]) * exaggerate
        b_ = V(e["b"]) + V(e["ub"]) * exaggerate
        if (b_ - a_).length < 1e-5:
            continue
        rot_ = V((0, 0, 1)).rotation_difference((b_ - a_).normalized()).to_matrix().to_4x4()
        mtx = Matrix.Translation((a_ + b_) / 2) @ rot_
        r = bmesh.ops.create_cone(bm, cap_ends=True, segments=12, radius1=e["r"], radius2=e["r"],
                                  depth=(b_ - a_).length, matrix=mtx)
        for v in r["verts"]:
            v[lay] = min(1.0, e["vm"] / e["fy"])
    return mesh_obj(f"FEA {case}", bm, m, "FEA", smooth=True)

if "--fea-render" in args:
    case = next((a for a in args if a.startswith("case=")), "case=ride_bump_2g")[5:]
    fea_heatmap(case)
    scene.cycles.samples = 16
    tag = "_stepfix" if "fix=step" in sys.argv else ""
    for key in ("side", "front_right"):
        scene.camera = cams[key]
        scene.render.filepath = os.path.join(out, f"fea_{case}{tag}_{key}.png")
        bpy.ops.render.render(write_still=True)

if "--anim" in args:
    # frames=a-b (inclusive); writes renders/anim/f_####.png with the chase camera
    f0, f1 = map(int, arg("frames", f"{scene.frame_start}-{scene.frame_end}").split("-"))
    scene.camera = cams[arg("cam", "chase")]
    scene.render.resolution_x, scene.render.resolution_y = map(int, arg("res", "1280x720").split("x"))
    scene.render.use_motion_blur = True
    scene.render.motion_blur_shutter = float(arg("shutter", "0.5"))
    sub = arg("dir", "anim")
    os.makedirs(os.path.join(out, sub), exist_ok=True)
    for f in range(f0, f1 + 1):
        scene.frame_set(f)
        scene.render.filepath = os.path.join(out, sub, f"f_{f:04d}.png")
        bpy.ops.render.render(write_still=True)

if "--export-web" in args:
    # web/cargobike.glb (Z-up, Blender coords) + web/rig.json: the viewer re-runs simulate() in JS
    import json
    web = os.path.join(HERE, "web")
    os.makedirs(web, exist_ok=True)
    scene.frame_set(1)
    bpy.ops.object.select_all(action='DESELECT')
    skip = {"Stage", "FEA"}
    for o in bpy.data.objects:
        cols = {c.name for c in o.users_collection}
        if not (cols & skip):
            o.select_set(True)
    bpy.ops.export_scene.gltf(filepath=os.path.join(web, "cargobike.glb"), export_format='GLB',
                              use_selection=True, export_yup=False, export_apply=True,
                              export_animations=False, export_cameras=False, export_lights=False)
    rig = dict(
        sim=SIM, g=G, head_angle=HA, wheelbase=AX_F.x - AX_R.x, r_rear=R_REAR, r_front=R_FRONT,
        ring_r=CR_R, cog_r=COG_R, n_ring=N_RING, n_cog=N_COG, crank_l=CRANK_L, pitch=PITCH,
        chain_dir=CHAIN_DIR, chain_len=L_tot, chain_step=step, n_links=n_links,
        chain_path=[[round(p.x, 6), round(p.z, 6)] for p in path], chain_y=CY,
        nodes=dict(root=RIG["root"].name, rear=RIG["rear"].name, front=RIG["front"].name,
                   crank=RIG["crank"].name, steer_f=RIG["steer_f"].name,
                   steer_r=RIG["steer_r"].name, pedal_L=pedal_piv["L"].name,
                   pedal_R=pedal_piv["R"].name, links=[o.name for o in CHAIN_LINKS]))
    json.dump(rig, open(os.path.join(web, "rig.json"), "w"))
    fea = json.load(open(os.path.join(HERE, "fea_elements.json")))
    r6 = lambda v: [round(c, 5) for c in v]
    json.dump({k: [dict(a=r6(e["a"]), b=r6(e["b"]), r=e["r"], q=round(e["vm"] / e["fy"], 3),
                        m=e["member"], ua=r6(e["ua"]), ub=r6(e["ub"])) for e in v]
               for k, v in fea.items()}, open(os.path.join(web, "fea.json"), "w"))
    eng = json.load(open(os.path.join(HERE, "engineering.json")))
    json.dump(eng, open(os.path.join(web, "engineering.json"), "w"))

if "--render" in args:
    only = [a for a in args if a in cams] or [k for k in cams if k != "top" and k not in DETAIL]
    for key in only:
        scene.camera = cams[key]
        scene.render.filepath = os.path.join(out, f"{key}.png")
        bpy.ops.render.render(write_still=True)

if "--overlay" in args:
    # render the model through each photo camera solved by tools/bundle.py
    import json
    solved = json.load(open(os.path.join(HERE, "solved.json")))
    g.hide_render = True
    for cn in ("Box", "Cables"):  # show bare tubes so the frame fit is readable
        COLLS[cn].hide_render = True
    flat = bpy.data.materials.new("Overlay")
    flat.use_nodes = True
    nt = flat.node_tree
    nt.nodes.clear()
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = (1.0, 0.25, 0.0, 1)
    o = nt.nodes.new("ShaderNodeOutputMaterial")
    nt.links.new(em.outputs[0], o.inputs[0])
    scene.view_layers[0].material_override = flat
    scene.render.film_transparent = True
    scene.view_settings.view_transform = 'Standard'
    scene.cycles.samples = 8
    scene.cycles.use_denoising = False
    scene.render.resolution_x, scene.render.resolution_y = 2048, 1536
    for view, c in solved["cams"].items():
        Rm = Matrix.Rotation(V(c["rvec"]).length, 3, V(c["rvec"]).normalized())
        tv = V(c["tvec"])
        R_cw = Rm.transposed()
        flip = Matrix(((1, 0, 0), (0, -1, 0), (0, 0, -1)))
        cd = bpy.data.cameras.new("Photo " + view)
        cd.sensor_fit, cd.sensor_width = 'HORIZONTAL', 36.0
        cd.lens = c["f"] * 36.0 / 2048
        cd.clip_start = 0.05
        cam = link(bpy.data.objects.new("Photo " + view, cd), "Stage")
        cam.matrix_world = Matrix.Translation(-(R_cw @ tv)) @ (R_cw @ flip).to_4x4()
        scene.camera = cam
        scene.render.filepath = os.path.join(out, f"ov_{view}.png")
        bpy.ops.render.render(write_still=True)
