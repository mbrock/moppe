"""Builds models/boat.blend: a flat-bottomed plywood skiff.

    Blender -b --factory-startup --python tools/figure/boat.py

A friend's design, after the Vietnamese canal boats whose long-tail
propeller runs in the boat's own trailing wave: a hard-chined skiff of
12 mm plywood some 4.8 m long and 1.55 m in the beam. Its bottom is flat
across and sweeps up forward to a fine, pointed bow; its sides flare a
little and stand straight up aft to a wide raked transom. It is decked
all round -- a long foredeck pitched to a ridge down its middle, narrow
side decks, and a short afterdeck carrying the motor's pivot block --
about an open cockpit whose padded coaming runs round it, chamfered at
the corners and drawn in to a short front. Inside, five frames with
lightening holes in their knees stand on the bottom between a forward
bulkhead and an after one with a locker opening, with a chine log down
each side, a keelson and two floor stringers, and one thwart across the
coaming. Rows of screws hold the deck down.

It is authored life size in the game's frame -- x to port, y up, z
forward -- with its origin on the centre line at the keel, a quarter of
the way forward from the transom (where the bottom is flat).

Rebuilding discards hand edits to the .blend.
"""

import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Matrix

sys.path.insert(0, os.path.dirname(__file__))
from shapes import (V, UP, FORWARD, Cage, assembly, blender, blob,  # noqa
                    loft, material, merge, mesh_object, part, slab)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "models", "boat.blend")

# Key dimensions, metres.
LENGTH = 4.8        # along the chine, transom to stem
Z0 = -1.2           # the transom's foot
Z1 = Z0 + LENGTH    # the stem
HALF_BEAM = 0.775   # at the sheer, amidships
FLARE = 0.93        # the chine's breadth as a fraction of the sheer's
DEPTH = 0.52        # the sheer above the keel aft
SPRING = 0.04       # how much the sheer rises toward the bow
ROCKER = 0.36       # how high the bottom sweeps up at the stem
RAKE = math.radians(10)
STEM = 0.015        # half the stem's width
PLY = 0.012
DECK = 0.009
LIP = 0.008         # how far the deck overhangs the sides
COCKPIT_AFT = Z0 - DEPTH * math.tan(RAKE) + 0.62
COCKPIT_FORE = Z1 - 1.42
SIDE_DECK = 0.15
FRONT_HALF = 0.30   # half the cockpit's short front edge
CORNER = 0.22       # the cockpit's chamfered after corners
RIDGE = 0.07        # the foredeck's pitch, rise over run
COAMING_W = 0.07
COAMING_H = 0.09
FRAME_T = 0.018
SHEET = 2.44        # a sheet of plywood's length; longer panels are joined
KERF = 0.0025       # the jigsaw's cut, left open as the joint's dark line
SOLE = 0.094        # the floorboards' top over the keel
HELM = V((-0.12, 0.36, COCKPIT_AFT + 0.20))   # where the helmsman sits

# The long-tail: its gimbal's pivot on the block, and the propeller's hub
# with the tail let down, two and a half metres aft and below the keel,
# where the stern wave lifts the water round it. mov::Boat agrees.
PIVOT = V((-0.52, DEPTH + DECK + 0.09, Z0 - DEPTH * math.tan(RAKE) + 0.14))
HUB = V((-0.12, -0.05, Z0 - 2.15))
TILLER = 0.86       # from the pivot to the throttle grip

# Latvijas Finieris film-faced birch plywood: the dark phenolic film on
# both faces, sheathed outside in glass cloth and epoxy, which is scuffed
# where the boat is dragged, beached, and stood on; the birch's pale plies
# show at every cut edge, and the CNC joints are filled with thickened
# epoxy. The sole is the anti-slip-faced sheet.
PALETTE = {
    "ply": (0.13, 0.105, 0.09),
    "ply_dark": (0.115, 0.095, 0.08),
    "ply_end": (0.80, 0.68, 0.50),
    "film_worn": (0.27, 0.235, 0.20),
    "film_mesh": (0.17, 0.14, 0.12),
    "seam": (0.56, 0.43, 0.28),
    "screw": (0.62, 0.63, 0.64),
    "engine_red": (0.78, 0.10, 0.07),
    "engine_black": (0.07, 0.07, 0.075),
    "alloy": (0.66, 0.67, 0.66),
    "steel": (0.55, 0.57, 0.58),
    "muffler": (0.24, 0.23, 0.22),
    "brass": (0.80, 0.64, 0.32),
    "rubber": (0.05, 0.05, 0.05),
}

FINISH = {
    "ply": dict(roughness=0.5, coat=0.5, coat_roughness=0.28),
    "ply_dark": dict(roughness=0.6, coat=0.35, coat_roughness=0.45),
    "ply_end": dict(roughness=0.5, coat=0.35, coat_roughness=0.3),
    "film_worn": dict(roughness=0.85),
    "film_mesh": dict(roughness=0.9, coat=0.15, coat_roughness=0.6),
    "seam": dict(roughness=0.4, coat=0.5, coat_roughness=0.3),
    "screw": dict(metallic=0.9, roughness=0.35),
    "engine_red": dict(roughness=0.38, coat=0.4),
    "engine_black": dict(roughness=0.55),
    "alloy": dict(metallic=0.8, roughness=0.45),
    "steel": dict(metallic=0.9, roughness=0.3),
    "muffler": dict(metallic=0.5, roughness=0.7),
    "brass": dict(metallic=1.0, roughness=0.25),
    "rubber": dict(roughness=0.9),
}


# The lines. The chine runs from the transom's foot to the stem; the
# sheer from the transom's top, raked aft, to the same stem. Each is
# parameterised by s, 0 at the transom and 1 at the stem.

def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def planform(s):
    """Half the breadth at the sheer: full aft, a little drawn in at the
    transom, and curving in over the forward 55% to a fine bow."""
    t = max(0.0, (s - 0.45) / 0.55)
    full = HALF_BEAM * (0.97 + 0.03 * min(1.0, s / 0.45))
    return max(STEM, full * (1 - t ** 2.1))


def transom_set():
    return DEPTH * math.tan(RAKE)


def s_chine(z):
    return (z - Z0) / LENGTH


def s_sheer(z):
    return (z - (Z0 - transom_set())) / (LENGTH + transom_set())


def z_sheer(s):
    return Z0 - transom_set() + s * (LENGTH + transom_set())


def sheer_y(s):
    return DEPTH + SPRING * s * s


def chine_y(s):
    return ROCKER * max(0.0, (s - 0.45) / 0.55) ** 2.0


def chine_hb(s):
    return max(STEM, planform(s) * FLARE)


def ridge(s):
    """The foredeck's pitch toward its ridge, nothing beside the
    cockpit."""
    return RIDGE * smoothstep(s_sheer(COCKPIT_FORE),
                              s_sheer(COCKPIT_FORE) + 0.06, s)


def deck_y(x, s):
    """The deck's top."""
    return sheer_y(s) + DECK + ridge(s) * max(0.0, planform(s) - abs(x))


def opening(s):
    """Half the cockpit opening's breadth at sheer station s."""
    z = z_sheer(s)
    z = max(COCKPIT_AFT, min(COCKPIT_FORE, z))
    s = s_sheer(z)
    hi = planform(s) - SIDE_DECK
    hi = min(hi, FRONT_HALF + (COCKPIT_FORE - z) * 1.05)
    hi -= max(0.0, CORNER - (z - COCKPIT_AFT))
    return hi


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    coll = bpy.data.collections.new("boat")
    scene.collection.children.link(coll)
    m = {name: material(name, c, **FINISH[name])
         for name, c in PALETTE.items()}

    hull = assembly("hull", coll, V((0, 0, 0)), mesh="hull")
    shell(hull, m)
    deck(hull, m)
    coaming(hull, m)
    interior(hull, m)
    fittings(hull, m)
    finish(hull, m)

    longtail(coll, m)
    assembly("seat_helm", coll, HELM, point="seat_helm")
    for sign, tag in ((1, "left"), (-1, "right")):
        assembly(f"foot_{tag}", coll,
                 HELM + V((sign * 0.16, SOLE - HELM[1], 0.42)),
                 point=f"foot_{tag}")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
    print(f"wrote {OUT}: {len(coll.objects)} objects")


# Helpers.

def grid(rows, inside, skip=lambda i, j: False):
    """Quads between consecutive rows of points, wound to face away from
    `inside`; `skip(i, j)` leaves out the quad after row i and column j."""
    cage = Cage()
    idx = []
    for row in rows:
        idx.append(list(range(len(cage.verts), len(cage.verts) + len(row))))
        cage.verts += row
    for i, (a, b) in enumerate(zip(idx, idx[1:])):
        for j in range(len(a) - 1):
            if not skip(i, j):
                cage.faces.append([a[j], a[j + 1], b[j + 1], b[j]])
    cage.orient(inside)
    return cage


def ply(asm, name, cage, mat, thickness=PLY):
    """A plywood panel: the sheet `cage`, thickened inward (against the
    way its faces look)."""
    obj = part(asm, name, cage, mat, levels=0, smooth=False)
    obj.data.materials.append(bpy.data.materials["ply_end"])
    mod = obj.modifiers.new("ply", "SOLIDIFY")
    mod.thickness = thickness
    mod.offset = -1
    mod.use_even_offset = True
    # The cut edge shows the birch's plies.
    mod.material_offset_rim = 1
    return obj


def plate(outline, z0, z1):
    """The polygon `outline`, given as (x, y) pairs, swept along z."""
    cage = Cage()
    area = sum(a[0] * b[1] - a[1] * b[0]
               for a, b in zip(outline, outline[1:] + outline[:1]))
    if area < 0:
        outline = list(reversed(outline))
    n = len(outline)
    for z in (z0, z1):
        cage.verts += [V((x, y, z)) for x, y in outline]
    cage.faces.append(list(reversed(range(n))))
    cage.faces.append(list(range(n, 2 * n)))
    for i in range(n):
        j = (i + 1) % n
        cage.faces.append([i, j, n + j, n + i])
    return cage


def bevelled(obj, width, segments=2, angle=30):
    mod = obj.modifiers.new("bevel", "BEVEL")
    mod.width = width
    mod.segments = segments
    mod.limit_method = "ANGLE"
    mod.angle_limit = math.radians(angle)
    mod.harden_normals = False
    return obj


def cut(obj, cutters):
    """Bores each (centre, radius, half length, axis) cylinder, or each
    (centre, half extents) box, out of `obj`, and applies it."""
    for k, c in enumerate(cutters):
        mesh = bpy.data.meshes.new(f"cutter{k}")
        bm = bmesh.new()
        if len(c) == 4:
            centre, radius, half, axis = c
            bmesh.ops.create_cone(bm, cap_ends=True, segments=20,
                                  radius1=radius, radius2=radius,
                                  depth=half * 2)
            if axis == "z":   # the game's z is Blender's -y
                bmesh.ops.rotate(bm, verts=bm.verts,
                                 matrix=Matrix.Rotation(math.pi / 2, 3, "X"))
        else:
            centre, half = c
            bmesh.ops.create_cube(bm, size=1.0)
            bmesh.ops.scale(bm, verts=bm.verts,
                            vec=(half[0] * 2, half[2] * 2, half[1] * 2))
        bm.to_mesh(mesh)
        bm.free()
        birch = bpy.data.materials["ply_end"]
        mesh.materials.append(birch)
        if birch.name not in obj.data.materials:
            obj.data.materials.append(birch)
        tool = bpy.data.objects.new(f"cutter{k}", mesh)
        obj.users_collection[0].objects.link(tool)
        tool.location = blender(centre)
        mod = obj.modifiers.new(f"cut{k}", "BOOLEAN")
        mod.operation = "DIFFERENCE"
        mod.solver = "EXACT"
        mod.material_mode = "TRANSFER"
        mod.object = tool
        with bpy.context.temp_override(object=obj, active_object=obj):
            bpy.ops.object.modifier_apply(modifier=mod.name)
        bpy.data.objects.remove(tool)
        bpy.data.meshes.remove(mesh)


def serpent(a0, a1, start, periods, pitch, amp, n=40):
    """The CNC-cut joint's line as (along, across) pairs from a0 to a1:
    straight to `start`, then a chain of round lobes, `periods` pairs of
    them, alternating to either side and each drawn in on a neck so that
    the two sheets lock together, then straight on. Each lobe is the same
    shape turned about, so the cut reads as a row of S's."""
    q = 0.105 * pitch      # how far the flanks swing back, making necks
    pts = [(a0, 0.0)]
    for i in range(periods * n + 1):
        t = i / n          # in periods
        # The swing fades in and out over the first and last quarter, so
        # that the line leaves and rejoins the straight going forward
        # (doubling back there would fold the cutter over itself).
        fade = min(1.0, t / 0.25, (periods - t) / 0.25)
        along = start + pitch * t - fade * q * math.sin(4 * math.pi * t)
        across = amp * math.sin(2 * math.pi * t)
        pts.append((along, across))
    pts.append((a1, 0.0))
    return pts


def slit(points, normal, width=KERF, depth=0.05):
    """A thin blade along the polyline `points`, lying in the plane square
    to `normal` and reaching `depth` through it either way: what the
    jigsaw takes out."""
    n_hat = normal.normalized()
    left, right = [], []
    for i, p in enumerate(points):
        a = points[max(0, i - 1)]
        b = points[min(len(points) - 1, i + 1)]
        side = n_hat.cross((b - a).normalized()).normalized()
        left.append(p + side * (width / 2))
        right.append(p - side * (width / 2))
    ring = left + list(reversed(right))
    m = len(ring)
    cage = Cage()
    cage.verts = [p - n_hat * depth for p in ring] + \
        [p + n_hat * depth for p in ring]
    cage.faces = [list(range(m)), list(reversed(range(m, 2 * m)))]
    cage.faces += [[i, (i + 1) % m, m + (i + 1) % m, m + i]
                   for i in range(m)]
    return cage


def jigsaw(obj, blades, seam):
    """Cuts `obj`, its plies made solid first, apart along each blade,
    the cut's walls the epoxy `seam` that fills it."""
    with bpy.context.temp_override(object=obj, active_object=obj):
        for mod in list(obj.modifiers):
            bpy.ops.object.modifier_apply(modifier=mod.name)
    coll = obj.users_collection[0]
    for k, cage in enumerate(blades):
        if seam.name not in obj.data.materials:
            obj.data.materials.append(seam)
        tool = mesh_object(f"{obj.name}_kerf{k}", cage, seam, coll,
                           levels=0, smooth=False)
        recalc(tool)
        mod = obj.modifiers.new(f"joint{k}", "BOOLEAN")
        mod.operation = "DIFFERENCE"
        mod.solver = "EXACT"
        mod.material_mode = "TRANSFER"
        mod.object = tool
        with bpy.context.temp_override(object=obj, active_object=obj):
            bpy.ops.object.modifier_apply(modifier=mod.name)
        if len(obj.data.vertices) == 0:
            raise RuntimeError(f"the joint cut {obj.name} away entirely")
        mesh = tool.data
        bpy.data.objects.remove(tool)
        bpy.data.meshes.remove(mesh)


# The hull.

STATIONS = 120


def shell(h, m):
    ss = [i / STATIONS for i in range(STATIONS + 1)]
    t = transom_set()

    def side(sign):
        rows = []
        for s in ss:
            a = V((sign * chine_hb(s), chine_y(s), Z0 + s * LENGTH))
            b = V((sign * planform(s), sheer_y(s), z_sheer(s)))
            rows.append([a.lerp(b, k / 8) for k in range(9)])
        return grid(rows, lambda p: V((0, p.y, p.z)))

    sides = (ply(h, "side_port", side(1), m["ply"]),
             ply(h, "side_starboard", side(-1), m["ply"]))
    rows = [[V((chine_hb(s) * (k / 5 - 1), chine_y(s), Z0 + s * LENGTH))
             for k in range(11)] for s in ss]
    bottom = ply(h, "bottom", grid(rows, lambda p: p + UP * 0.3),
                 m["ply_dark"])

    # The sides and the bottom are each two sheets, the forward one a
    # whole sheet long, joined by a CNC-cut serpentine joint: three S's
    # up the whole depth of a side, as in the drawing, and three across
    # the bottom.
    z = Z1 - SHEET
    sc, ssh = s_chine(z), s_sheer(z)
    y0, y1 = chine_y(sc), sheer_y(ssh)
    pitch = (y1 - y0 - 0.04) / 3
    for obj, sign in zip(sides, (1, -1)):
        x = sign * 0.5 * (chine_hb(sc) + planform(ssh))
        line = serpent(y0 - 0.03, y1 + 0.03, y0 + 0.02, 3, pitch,
                       sign * 0.034)
        jigsaw(obj, [slit([V((x, y, z + off)) for y, off in line],
                          V((1, 0, 0)), depth=0.08)], m["seam"])
    hb = chine_hb(sc) + 0.03
    line = serpent(-hb, hb, -0.45, 3, 0.30, 0.045)
    jigsaw(bottom, [slit([V((x, chine_y(sc), z + off)) for x, off in line],
                         UP, depth=0.03)], m["seam"])
    foot = [V((-chine_hb(0), 0, Z0)), V((chine_hb(0), 0, Z0))]
    head = [V((-planform(0), sheer_y(0), Z0 - t)),
            V((planform(0), sheer_y(0), Z0 - t))]
    tr = [[(foot[0].lerp(head[0], j / 6)).lerp(foot[1].lerp(head[1], j / 6),
                                               k / 12)
           for k in range(13)] for j in range(7)]
    ply(h, "transom", grid(tr, lambda p: p + FORWARD * 0.3), m["ply"],
        thickness=0.018)
    # The stem: a narrow upright post where the sides meet.
    part(h, "stem", plate([(-STEM, chine_y(1) - 0.01),
                           (STEM, chine_y(1) - 0.01),
                           (STEM, sheer_y(1)), (-STEM, sheer_y(1))],
                          Z1 - 0.03, Z1 + 0.004), m["ply_end"], levels=0,
         smooth=False)


def deck_stations():
    a, f = s_sheer(COCKPIT_AFT), s_sheer(COCKPIT_FORE)
    ss = {i / 240 for i in range(241)}
    ss |= {a, a + CORNER / (LENGTH + transom_set()), f}
    # Where the front chamfer meets the side deck.
    lo, hi = a, f
    for _ in range(40):
        mid = (lo + hi) / 2
        z = z_sheer(mid)
        if planform(mid) - SIDE_DECK < FRONT_HALF + \
                (COCKPIT_FORE - z) * 1.05:
            lo = mid
        else:
            hi = mid
    ss.add(lo)
    return sorted(ss), a, f


def deck(h, m):
    ss, a, f = deck_stations()
    rows = []
    for s in ss:
        hb, hi = planform(s) + LIP, opening(s)
        if s > f:
            hi = min(hi, 0.5 * planform(s))
        rows.append([V((x, deck_y(x, s), z_sheer(s)))
                     for x in (-hb, -hi, 0.0, hi, hb)])
    inside = [a - 1e-9 <= s0 and s1 <= f + 1e-9
              for s0, s1 in zip(ss, ss[1:])]
    ply(h, "deck", grid(rows, lambda p: p - UP,
                        skip=lambda i, j: inside[i] and j in (1, 2)),
        m["ply"], thickness=DECK)


def opening_loop():
    """The cockpit's edge, round from the port after corner, forward,
    across the front, and back down the starboard side."""
    ss, a, f = deck_stations()
    inner = [s for s in ss if a - 1e-9 <= s <= f + 1e-9]
    port = [V((opening(s), sheer_y(s) + DECK, z_sheer(s))) for s in inner]
    loop = port + [V((-p.x, p.y, p.z)) for p in reversed(port)]
    out = []
    for p in loop:
        if not out or (p - out[-1]).length > 1e-4:
            out.append(p)
    if (out[0] - out[-1]).length < 1e-4:
        out.pop()
    return out


def mitred_normals(loop):
    """The outward (away from the cockpit) normal at each corner of the
    loop, lengthened to mitre the corner."""
    n = len(loop)
    seg = []
    for i in range(n):
        d = loop[(i + 1) % n] - loop[i]
        seg.append(V((d.z, 0, -d.x)).normalized())
    out = []
    for i in range(n):
        a, b = seg[i - 1], seg[i]
        mid = (a + b).normalized()
        out.append(mid / max(0.3, mid.dot(a)))
    return out


def sweep(loop, normals, section):
    """Sweeps `section`, (outward, up) pairs, round the closed loop."""
    cage = Cage()
    k = len(section)
    for p, nrm in zip(loop, normals):
        cage.verts += [p + nrm * o + UP * u for o, u in section]
    n = len(loop)
    for i in range(n):
        a, b = i * k, ((i + 1) % n) * k
        for j in range(k):
            jj = (j + 1) % k
            cage.faces.append([a + j, a + jj, b + jj, b + j])
    return cage


def coaming(h, m):
    loop = opening_loop()
    normals = mitred_normals(loop)
    below = -DECK - 0.06
    section = [(0.0, below), (0.0, COAMING_H), (COAMING_W, COAMING_H),
               (COAMING_W, 0.0), (0.016, 0.0), (0.016, below)]
    cage = sweep(loop, normals, section)
    obj = part(h, "coaming", cage, m["ply"], levels=0, smooth=True)
    recalc(obj)
    bevelled(obj, 0.018, segments=3, angle=60)
    # A seam where the cap meets the face below it.
    seam = sweep(loop, normals, [(-0.002, COAMING_H - 0.035),
                                 (-0.002, COAMING_H - 0.031),
                                 (0.003, COAMING_H - 0.031),
                                 (0.003, COAMING_H - 0.035)])
    recalc(part(h, "coaming_seam", seam, m["ply_end"], levels=0,
                smooth=False))


def recalc(obj, smooth=False):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(obj.data)
    bm.free()
    for p in obj.data.polygons:
        p.use_smooth = smooth
    obj.data.update()


# Inside.

def section_at(z):
    """The hull's inside at z: (chine half breadth, chine height, sheer
    half breadth, deck underside)."""
    sc, ss = s_chine(z), s_sheer(z)
    return (chine_hb(sc) - PLY, chine_y(sc) + PLY,
            planform(ss) - PLY, sheer_y(ss))


def bezier(a, c, b, n=6):
    return [a * (1 - t) ** 2 + c * 2 * t * (1 - t) + b * t * t
            for t in (i / n for i in range(1, n))]


def frame_outline(z):
    hc, yc, hs, ys = section_at(z)
    floor = yc + 0.07

    def wall(y, inset):
        t = (y - yc) / (ys - yc)
        return V((hc + (hs - hc) * t - inset, y, 0))

    top = V((hs - 0.24, ys, 0))
    k1 = wall(ys - 0.32, 0.07)
    k2 = wall(yc + 0.17, 0.07)
    foot = V((hc - 0.24, floor, 0))
    port = [V((hc, yc, 0)), V((hs, ys, 0)), top]
    port += bezier(top, wall(ys - 0.10, 0.16), k1, 8)
    port += [k1, k2]
    port += bezier(k2, wall(floor, 0.06), foot)
    port += [foot]
    star = [V((-p.x, p.y, 0)) for p in reversed(port)]
    return [(p.x, p.y) for p in port + star]


def interior(h, m):
    span = COCKPIT_FORE - COCKPIT_AFT
    frames = [COCKPIT_AFT + span * (k + 0.75) / 5.4 for k in range(5)]
    for k, z in enumerate(frames):
        obj = part(h, f"frame{k}", plate(frame_outline(z), z - FRAME_T / 2,
                                         z + FRAME_T / 2),
                   m["ply_dark"], levels=0, smooth=False)
        hc, yc, hs, ys = section_at(z)
        holes = []
        for sign in (1, -1):
            for dy, inset, r in ((0.12, 0.08, 0.034),
                                 (0.22, 0.055, 0.027)):
                y = ys - dy
                t = (y - yc) / (ys - yc)
                x = hc + (hs - hc) * t - inset
                holes.append((V((sign * x, y, z)), r, 0.05, "z"))
            # A limber hole each side, so water runs aft along the floor.
            holes.append((V((sign * (hc - 0.05), yc, z)), 0.025, 0.05, "z"))
        cut(obj, holes)

    # The bulkheads closing the cockpit, the after one with a locker
    # opening under the afterdeck.
    for name, z in (("bulkhead_fore", COCKPIT_FORE + 0.005),
                    ("bulkhead_aft", COCKPIT_AFT - 0.005)):
        hc, yc, hs, ys = section_at(z)
        obj = part(h, name, plate([(hc, yc), (hs, ys), (-hs, ys),
                                   (-hc, yc)], z - 0.006, z + 0.006),
                   m["ply"], levels=0, smooth=False)
        if name == "bulkhead_aft":
            cut(obj, [(V((0, (yc + ys) / 2 + 0.02, z)), (0.42, 0.13, 0.05))])
            frame = []
            for x0, y0, x1, y1 in ((-0.46, -0.17, 0.46, -0.13),
                                   (-0.46, 0.13, 0.46, 0.17),
                                   (-0.46, -0.17, -0.42, 0.17),
                                   (0.42, -0.17, 0.46, 0.17)):
                c = V(((x0 + x1) / 2, (yc + ys) / 2 + 0.02 + (y0 + y1) / 2,
                       z + 0.015))
                frame.append(slab(c, ((x1 - x0) / 2, (y1 - y0) / 2, 0.009)))
            part(h, "locker_frame", merge(*frame), m["ply_dark"], levels=0,
                 smooth=False)

    # Chine logs along the inside of each chine, between the bulkheads.
    zs = [COCKPIT_AFT + (COCKPIT_FORE - COCKPIT_AFT) * i / 30
          for i in range(31)]
    for sign, tag in ((1, "port"), (-1, "starboard")):
        pts = []
        for z in zs:
            hc, yc, hs, ys = section_at(z)
            pts.append((V((sign * (hc - 0.022), yc + 0.022, z)), 0.03,
                        0.03))
        part(h, f"chine_log_{tag}", loft(pts, UP, sides=4,
                                         turn=math.pi / 4), m["ply_end"],
             levels=0, smooth=False)

    # The sole: floorboards laid on the frames' floors from the transom to
    # the forward bulkhead, above the waterline, so that the bilge under
    # them stays out of sight (and the water's surface with it).
    zs = [Z0 + 0.03 + (COCKPIT_FORE - Z0 - 0.03) * i / 40 for i in range(41)]
    rows = []
    for z in zs:
        hc, yc, hs, ys = section_at(z)
        w = hc - 0.03
        rows.append([V((x, SOLE, z)) for x in (-w, w)])
    sole = ply(h, "sole", grid(rows, lambda p: p - UP), m["ply"],
               thickness=0.012)
    # The sole too is longer than a sheet, and joined like the hull,
    # between the second and third frames.
    z = COCKPIT_AFT + (COCKPIT_FORE - COCKPIT_AFT) * 2.25 / 5.4
    w = section_at(z)[0] - 0.03 + 0.02
    # One S in the middle of an otherwise straight seam, as drawn.
    line = serpent(-w, w, -0.09, 1, 0.18, 0.04)
    jigsaw(sole, [slit([V((x, SOLE - 0.006, z + off)) for x, off in line],
                       UP, depth=0.02)], m["seam"])
    seams = []
    for x in (-0.42, -0.14, 0.14, 0.42):
        seams.append(slab(V((x, SOLE + 0.0008, (COCKPIT_AFT + COCKPIT_FORE)
                             / 2)), (0.004, 0.001,
                                     (COCKPIT_FORE - COCKPIT_AFT) / 2 - 0.02)))
    part(h, "sole_seams", merge(*seams), m["ply_end"], levels=0,
         smooth=False)

    # The helmsman's seat, a plank across the after end of the cockpit on
    # cleats against the hull.
    hc, yc, hs, ys = section_at(HELM[2])
    t = (HELM[1] - 0.02 - yc) / (ys - yc)
    w = hc + (hs - hc) * t - 0.004
    obj = part(h, "helm_seat", slab(V((0, HELM[1] - 0.016, HELM[2])),
                                    (w, 0.016, 0.13)), m["ply"], levels=0,
               smooth=False)
    bevelled(obj, 0.005, segments=2)
    for sign in (1, -1):
        part(h, f"seat_cleat{sign}", slab(
            V((sign * (w - 0.02), HELM[1] - 0.05, HELM[2])),
            (0.02, 0.018, 0.12)), m["ply_end"], levels=0, smooth=False)

    # The thwart across the coaming, a quarter of the way aft.
    z = COCKPIT_FORE - 0.27 * span
    s = s_sheer(z)
    w = opening(s) + COAMING_W * 0.8
    obj = part(h, "thwart", slab(V((0, sheer_y(s) + DECK + COAMING_H - 0.012,
                                    z)), (w, 0.016, 0.055)),
               m["ply"], levels=0, smooth=False)
    bevelled(obj, 0.006, segments=2)


# The finish.

def applied(obj):
    with bpy.context.temp_override(object=obj, active_object=obj):
        for mod in list(obj.modifiers):
            bpy.ops.object.modifier_apply(modifier=mod.name)
    return obj


def game_of(v):
    return V((v[0], v[2], -v[1]))


def blotches(p):
    """Smooth pseudo-noise in 0..1 over a few centimetres to a decimetre,
    for scuffs."""
    a = math.sin(p.dot(V((23.1, 7.3, 11.7))) + 1.3) * \
        math.sin(p.dot(V((-9.2, 19.4, 13.1))) + 0.4)
    b = math.sin(p.dot(V((41.0, -33.0, 27.0))) + 2.1) * \
        math.sin(p.dot(V((17.0, 45.0, -38.0))) + 0.7)
    return 0.5 + 0.35 * a + 0.15 * b


def wear(h, name, obj, worn, outward, weight, seed):
    """Scuffs the film through to the glass where `weight` (of a place
    and its normal, in the game's frame) says the boat takes knocks: small
    ragged patches laid just over the faces that look `outward`, drawn
    out fore and aft the way a hull is scraped over stones and slipways."""
    rng = random.Random(seed)
    cages = []
    for f in obj.data.polygons:
        c, n = game_of(f.center), game_of(f.normal)
        if not outward(n) or f.material_index != 0:
            continue
        w = weight(c, n) * blotches(c)
        count = f.area / 0.00035 * max(0.0, w - 0.33) ** 2
        while count > 0:
            if rng.random() >= count:
                break
            count -= 1
            # Somewhere on the face, by its own corners.
            corners = [game_of(obj.data.vertices[i].co) for i in f.vertices]
            k = [rng.random() for _ in corners]
            p = sum((q * x for q, x in zip(corners, k)), V((0, 0, 0))) / \
                sum(k)
            along = FORWARD - n * n.dot(FORWARD)
            if along.length < 0.2:
                along = UP - n * n.dot(UP)
            along = along.normalized()
            across = n.cross(along)
            size = 0.004 + 0.02 * rng.random() ** 2
            stretch = 2.0 + 4.0 * rng.random()
            # Each patch a hair off the surface at its own height, so that
            # overlapping ones never share a plane.
            lift = 0.0003 + 0.0009 * rng.random()
            cage = Cage()
            sides = 9
            for j in range(sides):
                t = 2 * math.pi * j / sides
                r = size * (0.55 + 0.45 * rng.random())
                cage.verts.append(p + n * lift +
                                  along * (math.cos(t) * r * stretch) +
                                  across * (math.sin(t) * r))
            cage.faces = [list(range(sides))]
            cage.orient(lambda q, p=p, n=n: p - n)
            cages.append(cage)
    if cages:
        part(h, f"{name}_scuffs", merge(*cages), worn, levels=0,
             smooth=False)


def birch_edges(obj, birch):
    """A cut piece of plywood: the faces round its thin way are its
    edges, which show the birch."""
    applied(obj)
    if birch.name not in obj.data.materials:
        obj.data.materials.append(birch)
    k = list(obj.data.materials).index(birch)
    lo = [min(v.co[i] for v in obj.data.vertices) for i in range(3)]
    hi = [max(v.co[i] for v in obj.data.vertices) for i in range(3)]
    thin = min(range(3), key=lambda i: hi[i] - lo[i])
    for f in obj.data.polygons:
        if abs(f.normal[thin]) < 0.5 and f.material_index == 0:
            f.material_index = k


def finish(h, m):
    parts = {o.name: o for o in h.children}
    for name, o in parts.items():
        if name.startswith(("frame", "bulkhead", "locker_frame", "thwart",
                            "helm_seat", "pivot_block", "stem")):
            birch_edges(o, m["ply_end"])
    parts["sole"].data.materials[0] = m["film_mesh"]

    def s_of(c):
        return max(0.0, min(1.0, s_chine(c.z)))

    def side_weight(c, n):
        s = s_of(c)
        chine, sheer = chine_y(s), sheer_y(s_sheer(c.z))
        return (0.95 * math.exp(-(c.y - chine) / 0.05) +
                0.5 * math.exp(-(sheer - c.y) / 0.035) +
                0.7 * smoothstep(0.86, 1.0, s) +
                0.6 * smoothstep(0.08, 0.0, s))

    def bottom_weight(c, n):
        s = s_of(c)
        return 0.35 + 0.55 * smoothstep(0.5, 0.95, s) + \
            0.35 * smoothstep(0.75, 1.0, abs(c.x) / max(chine_hb(s), 0.05))

    def deck_weight(c, n):
        s = s_sheer(c.z)
        edge = math.exp(-(planform(s) - abs(c.x)) / 0.03)
        # Where feet go over the side decks, and round the pivot.
        tread = 0.45 * smoothstep(0.35, 0.0, s) + \
            0.25 * (1 - smoothstep(0.0, 0.2, abs(abs(c.x) - planform(s) +
                                                   SIDE_DECK / 2)))
        return 0.5 * edge + tread

    def transom_weight(c, n):
        return 0.8 * math.exp(-c.y / 0.06) + \
            0.5 * math.exp(-(planform(0) - abs(c.x)) / 0.04)

    panels = (("side_port", lambda n: n.x > 0.3, side_weight),
              ("side_starboard", lambda n: n.x < -0.3, side_weight),
              ("bottom", lambda n: n.y < -0.3, bottom_weight),
              ("deck", lambda n: n.y > 0.3, deck_weight),
              ("transom", lambda n: n.z < -0.3, transom_weight))
    mode = os.environ.get("MOPPE_BOAT_WEAR", "geometry")
    if mode == "geometry":
        for seed, (name, outward, weight) in enumerate(panels, 1):
            wear(h, name, parts[name], m["film_worn"], outward, weight, seed)
        return
    for name, outward, weight in panels:
        wear_attribute(parts[name], outward, weight, mode == "vertex")
    for name in ("ply", "ply_dark"):
        weathered(m[name], m["film_worn"], mode)


def wear_attribute(obj, outward, weight, blotched):
    """The wear `weight` gives each corner of the faces that look
    `outward`, as the corner attribute "wear" (none inside); `blotched`
    breaks it up by the blotches, as vertex colours would carry it."""
    applied(obj)
    me = obj.data
    attr = me.attributes.new("wear", "FLOAT", "CORNER")
    for f in me.polygons:
        n = game_of(f.normal)
        for li in f.loop_indices:
            c = game_of(me.vertices[me.loops[li].vertex_index].co)
            w = 0.0
            if outward(n) and f.material_index == 0:
                w = min(1.0, weight(c, n))
                if blotched:
                    w = smoothstep(0.30, 0.50, w * blotches(c))
            attr.data[li].value = w


def weathered(mat, worn, mode):
    """Gives the film's material its wear from the "wear" attribute: as it
    stands (vertex), or broken by noise in the boat's own frame into
    ragged scuffs through to the glass cloth and fine scratches drawn fore
    and aft, with the film itself matte and glossy by turns (procedural).
    """
    tree = mat.node_tree
    nodes, links = tree.nodes, tree.links
    bsdf = nodes["Principled BSDF"]
    film = tuple(bsdf.inputs["Base Color"].default_value)
    worn_colour = worn.node_tree.nodes["Principled BSDF"].inputs[
        "Base Color"].default_value
    rough = bsdf.inputs["Roughness"].default_value
    coat = bsdf.inputs["Coat Weight"].default_value

    def node(kind, **props):
        n = nodes.new(kind)
        for k, v in props.items():
            setattr(n, k, v)
        return n

    def math_node(op, a, b=None, clamp=False):
        n = node("ShaderNodeMath", operation=op, use_clamp=clamp)
        for i, x in enumerate((a, b)):
            if x is None:
                continue
            if isinstance(x, (int, float)):
                n.inputs[i].default_value = x
            else:
                links.new(x, n.inputs[i])
        return n.outputs[0]

    def ramp(x, lo, hi):
        n = node("ShaderNodeMapRange", interpolation_type="SMOOTHSTEP")
        links.new(x, n.inputs["Value"])
        n.inputs["From Min"].default_value = lo
        n.inputs["From Max"].default_value = hi
        return n.outputs["Result"]

    def noise(coords, scale, detail, roughness=0.6):
        n = node("ShaderNodeTexNoise")
        links.new(coords, n.inputs["Vector"])
        n.inputs["Scale"].default_value = scale
        n.inputs["Detail"].default_value = detail
        n.inputs["Roughness"].default_value = roughness
        return n.outputs["Fac"]

    attr = node("ShaderNodeAttribute", attribute_name="wear")
    wear = attr.outputs["Fac"]
    if mode == "vertex":
        amount = wear
        matte = None
    else:
        obj = node("ShaderNodeTexCoord").outputs["Object"]
        # Ragged scuffs: the wear pushed over a threshold by a fine,
        # rough noise, so its edges fray.
        fray = noise(obj, 22.0, 12.0, 0.7)
        scuff = ramp(math_node("ADD", math_node("MULTIPLY", wear, 0.85),
                               math_node("SUBTRACT", fray, 0.5)),
                     0.52, 0.56)
        # Scratches: thin lines along a noise's contour, the noise drawn
        # out along the boat (Blender's y) so they run fore and aft.
        stretch = node("ShaderNodeMapping")
        links.new(obj, stretch.inputs["Vector"])
        stretch.inputs["Scale"].default_value = (70.0, 4.0, 70.0)
        streak = noise(stretch.outputs["Vector"], 1.0, 3.0, 0.5)
        line = ramp(math_node("ABSOLUTE", math_node("SUBTRACT", streak, 0.5)),
                    0.010, 0.003)
        scratch = math_node("MULTIPLY", line, ramp(wear, 0.15, 0.5))
        amount = math_node("MAXIMUM", scuff,
                           math_node("MULTIPLY", scratch, 0.8))
        # The film's own sheen comes and goes: handled, rained on, dried.
        matte = noise(obj, 3.0, 4.0, 0.5)

    colour = node("ShaderNodeMix", data_type="RGBA")
    colour.inputs["A"].default_value = film
    colour.inputs["B"].default_value = worn_colour
    links.new(amount, colour.inputs["Factor"])
    links.new(colour.outputs["Result"], bsdf.inputs["Base Color"])
    r = node("ShaderNodeMapRange")
    links.new(amount, r.inputs["Value"])
    r.inputs["To Min"].default_value = rough
    r.inputs["To Max"].default_value = 0.85
    if matte is not None:
        film_rough = node("ShaderNodeMapRange")
        links.new(matte, film_rough.inputs["Value"])
        film_rough.inputs["From Min"].default_value = 0.35
        film_rough.inputs["From Max"].default_value = 0.65
        film_rough.inputs["To Min"].default_value = rough - 0.12
        film_rough.inputs["To Max"].default_value = rough + 0.15
        links.new(film_rough.outputs["Result"], r.inputs["To Min"])
    links.new(r.outputs["Result"], bsdf.inputs["Roughness"])
    c = node("ShaderNodeMapRange")
    links.new(amount, c.inputs["Value"])
    c.inputs["To Min"].default_value = coat
    c.inputs["To Max"].default_value = 0.05
    links.new(c.outputs["Result"], bsdf.inputs["Coat Weight"])


# The long-tail.

def shaft_axes():
    """Down the shaft from the pivot toward the propeller, and a left and
    an up square to it."""
    aft = (HUB - PIVOT).normalized()
    left = UP.cross(-aft).normalized()
    up = (-aft).cross(left)
    return aft, left, up


def longtail(coll, m):
    """A small air-cooled petrol engine on a gimbal over the pivot block,
    with the long shaft running straight back from its crankshaft to the
    propeller and the tiller forward from it: the whole thing swings to
    steer and tips to lift the propeller. Its assembly turns about the
    pivot; the propeller spins on its own about the shaft."""
    lt = assembly("longtail", coll, PIVOT, mesh="longtail", point="pivot")
    aft, left, up = shaft_axes()
    fwd = -aft
    # Cages are in the assembly's frame, about the pivot.
    o = V((0, 0, 0))
    hub = HUB - PIVOT

    def at(f, u, l=0.0):
        return o + fwd * f + up * u + left * l

    axes = (left, up, fwd)
    # The gimbal: a fork on a vertical pin in the block, and the plate the
    # engine is bolted to, hinged across it.
    part(lt, "pin", loft([(o - UP * 0.10, 0.022, 0.022),
                          (o + UP * 0.03, 0.022, 0.022)], FORWARD,
                         sides=10), m["steel"], levels=0)
    fork = [slab(o + left * (s * 0.075) + UP * 0.02, (0.008, 0.045, 0.035),
                 axes=axes) for s in (1, -1)]
    part(lt, "fork", merge(*fork), m["steel"], levels=0, bevel=0.004)
    part(lt, "hinge", loft([(o + left * 0.085 + UP * 0.03, 0.014, 0.014),
                            (o - left * 0.085 + UP * 0.03, 0.014, 0.014)],
                           UP, sides=8), m["steel"], levels=0)
    part(lt, "plate", slab(at(0.24, 0.035), (0.17, 0.008, 0.25),
                           axes=axes), m["steel"], levels=0, bevel=0.004)

    # The engine: a GX200-type single, its crankshaft on the shaft's line
    # a little above the plate, the cylinder leaning to starboard.
    part(lt, "crankcase", slab(at(0.26, 0.15), (0.13, 0.11, 0.14),
                               axes=axes), m["alloy"], levels=1,
         bevel=0.03)
    lean = (up * math.cos(0.45) - left * math.sin(0.45)).normalized()
    head = at(0.24, 0.22, -0.10) + lean * 0.10
    fins = []
    for k in range(7):
        c = at(0.24, 0.20, -0.10) + lean * (0.03 + 0.022 * k)
        fins.append(slab(c, (0.075, 0.006, 0.075),
                         axes=(lean.cross(fwd), lean, fwd)))
    part(lt, "cylinder", merge(*fins), m["alloy"], levels=0)
    part(lt, "head", slab(head + lean * 0.06, (0.06, 0.03, 0.07),
                          axes=(lean.cross(fwd), lean, fwd)),
         m["alloy"], levels=0, bevel=0.012)
    part(lt, "plug", loft([(head + lean * 0.09, 0.012, 0.012),
                           (head + lean * 0.14, 0.009, 0.009)], fwd,
                          sides=8), m["engine_black"], levels=0)
    # The fan shroud over the flywheel at the front, the recoil starter on
    # it facing the helmsman, and its pull handle.
    part(lt, "shroud", slab(at(0.41, 0.17, -0.01), (0.17, 0.16, 0.035),
                            axes=axes), m["engine_red"], levels=1,
         bevel=0.03)
    starter = at(0.45, 0.15)
    part(lt, "starter", loft([(starter - fwd * 0.01, 0.10, 0.10),
                              (starter + fwd * 0.02, 0.095, 0.095),
                              (starter + fwd * 0.03, 0.06, 0.06)], up,
                             sides=20), m["engine_black"], levels=0)
    part(lt, "pull", slab(at(0.49, 0.24, 0.06), (0.045, 0.012, 0.012),
                          axes=axes), m["engine_black"], levels=0,
         bevel=0.006)
    # The tank on top, with its cap.
    part(lt, "tank", slab(at(0.25, 0.34, 0.01), (0.15, 0.065, 0.13),
                          axes=axes), m["engine_red"], levels=1, bevel=0.035)
    part(lt, "cap", loft([(at(0.30, 0.40, 0.06), 0.03, 0.03),
                          (at(0.30, 0.425, 0.06), 0.028, 0.028)], fwd,
                         sides=12), m["engine_black"], levels=0)
    # The air filter to port, the muffler and its guard to starboard over
    # the cylinder, the exhaust's mouth aimed aft.
    part(lt, "air_filter", slab(at(0.30, 0.20, 0.17), (0.05, 0.08, 0.09),
                                axes=axes), m["engine_black"], levels=1,
         bevel=0.025)
    part(lt, "muffler", loft([(at(0.14, 0.27, -0.21), 0.05, 0.05),
                              (at(0.30, 0.27, -0.21), 0.05, 0.05)], up,
                             sides=14), m["muffler"], levels=0)
    part(lt, "muffler_guard", slab(at(0.22, 0.27, -0.27), (0.008, 0.06,
                                                          0.11),
                                   axes=axes), m["engine_black"], levels=0,
         bevel=0.004)
    part(lt, "tailpipe", loft([(at(0.12, 0.27, -0.21), 0.014, 0.014),
                               (at(0.08, 0.26, -0.25), 0.014, 0.014)], up,
                              sides=8), m["muffler"], levels=0)
    assembly("exhaust", coll, PIVOT + at(0.07, 0.26, -0.26), point="exhaust")
    # The shaft: a coupling off the crankshaft, the tube back to the
    # propeller with a skeg under its end, and the bearing at the hub.
    # The shaft runs on from the crankshaft, its tube held over the
    # hinge by a bracket, back to the propeller.
    crank = at(0.12, 0.15)
    part(lt, "shaft", loft([(crank, 0.026, 0.026),
                            (crank - fwd * 0.10, 0.024, 0.024),
                            (hub + fwd * 0.07, 0.020, 0.020)], up,
                           sides=12), m["steel"], levels=0)
    part(lt, "bracket", slab(at(-0.02, 0.09), (0.03, 0.06, 0.05),
                             axes=axes), m["steel"], levels=0, bevel=0.006)
    part(lt, "bearing", loft([(hub + fwd * 0.16, 0.026, 0.026),
                              (hub + fwd * 0.05, 0.03, 0.03),
                              (hub + fwd * 0.03, 0.022, 0.022)], up,
                             sides=12), m["steel"], levels=0)
    part(lt, "skeg", slab(hub + fwd * 0.12 - up * 0.045,
                          (0.003, 0.025, 0.07), axes=axes),
         m["steel"], levels=0)
    # The tiller, forward from the plate, with the twist grip on its end.
    grip = at(TILLER, 0.12)
    part(lt, "tiller", loft([(at(0.40, 0.05), 0.016, 0.016),
                             (at(0.60, 0.08), 0.016, 0.016),
                             (grip - fwd * 0.07, 0.016, 0.016)], up,
                            sides=8), m["steel"], levels=0)
    part(lt, "grip", loft([(grip - fwd * 0.08, 0.019, 0.019),
                           (grip + fwd * 0.07, 0.019, 0.019)], up,
                          sides=10), m["rubber"], levels=1)
    part(lt, "throttle_cable", loft([(grip - fwd * 0.09, 0.004, 0.004),
                                     (at(0.55, 0.16, 0.03), 0.004, 0.004),
                                     (at(0.33, 0.24, 0.10), 0.004,
                                      0.004)], up, sides=4),
         m["engine_black"], levels=0)
    assembly("tiller_grip", coll, PIVOT + grip, point="tiller_grip")

    # The propeller, three bronze blades round a hub about its own z,
    # which the game turns down the shaft.
    p = assembly("propeller", coll, HUB, mesh="propeller", point="hub")
    part(p, "boss", loft([(V((0, 0, 0.03)), 0.022, 0.022),
                          (V((0, 0, -0.02)), 0.026, 0.026),
                          (V((0, 0, -0.05)), 0.008, 0.008)], UP, sides=12),
         m["brass"], levels=1)
    blades = []
    for k in range(3):
        a = 2 * math.pi * k / 3
        out = V((math.cos(a), math.sin(a), 0))
        side = FORWARD.cross(out)
        pitch = math.radians(28)
        face = (side * math.cos(pitch) + FORWARD * math.sin(pitch))
        thick = out.cross(face)
        blades.append(blob(out * 0.07, (0.045, 0.006, 0.035), 16, 6))
        b = blades[-1]
        # Turn the blob from its own axes into the blade's.
        for i, v in enumerate(b.verts):
            q = v - out * 0.07
            b.verts[i] = out * 0.07 + out * q.x / 0.045 * 0.05 + \
                thick * q.y + face * q.z
    recalc(part(p, "blades", merge(*blades), m["brass"], levels=0),
           smooth=True)


# Fittings.

def fittings(h, m):
    # The long-tail's pivot block on the afterdeck, to starboard, with
    # its pin hole.
    z = z_sheer(0.0) + 0.14
    y = deck_y(0.52, 0.0)
    obj = part(h, "pivot_block", slab(V((-0.52, y + 0.045, z)),
                                      (0.08, 0.045, 0.12)),
               m["ply_dark"], levels=0, smooth=False)
    cut(obj, [(V((-0.52, y + 0.09, z + 0.03)), 0.014, 0.05, "y")])
    bevelled(obj, 0.008, segments=2)

    # Screws along the deck's edge and round the coaming.
    heads = []

    def screw(p):
        heads.append(loft([(p - UP * 0.002, 0.0045, 0.0045),
                           (p + UP * 0.0012, 0.0045, 0.0045)],
                          FORWARD, sides=6))

    def along(points, pitch):
        carry = 0.0
        for a, b in zip(points, points[1:]):
            d = (b - a).length
            t = carry
            while t < d:
                screw(a + (b - a) * (t / d))
                t += pitch
            carry = t - d

    ss = [i / 160 for i in range(161)]
    for sign in (1, -1):
        edge = []
        for s in ss:
            x = sign * (planform(s) - 0.02)
            edge.append(V((x, deck_y(x, s), z_sheer(s))))
        along(edge, 0.11)
    tx = planform(0) - 0.02
    along([V((x, deck_y(x, 0.0), z_sheer(0.0) + 0.012))
           for x in (-tx, tx)], 0.11)
    loop = opening_loop()
    normals = mitred_normals(loop)
    ring = [p + n * (COAMING_W + 0.022) for p, n in zip(loop, normals)]
    along(ring + ring[:1], 0.11)
    part(h, "screws", merge(*heads), m["screw"], levels=0, smooth=False)


if __name__ == "__main__":
    build()
