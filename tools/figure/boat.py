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
import sys

import bmesh
import bpy
from mathutils import Matrix

sys.path.insert(0, os.path.dirname(__file__))
from shapes import (V, UP, FORWARD, Cage, assembly, blender, loft,  # noqa
                    material, merge, part, slab)

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

PALETTE = {
    "ply": (0.80, 0.70, 0.58),
    "ply_dark": (0.66, 0.55, 0.43),
    "ply_end": (0.56, 0.44, 0.32),
    "screw": (0.20, 0.20, 0.21),
}

FINISH = {
    "ply": dict(roughness=0.55, coat=0.15),
    "ply_dark": dict(roughness=0.6, coat=0.1),
    "ply_end": dict(roughness=0.7),
    "screw": dict(metallic=0.8, roughness=0.4),
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

    assembly("pivot", coll, V((-0.52, deck_y(0.52, 0.0) + 0.09,
                               z_sheer(0.0) + 0.14)), point="pivot")
    assembly("seat", coll, V((0, 0.33, COCKPIT_AFT + 0.25)), point="seat")

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
    mod = obj.modifiers.new("ply", "SOLIDIFY")
    mod.thickness = thickness
    mod.offset = -1
    mod.use_even_offset = True
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
        tool = bpy.data.objects.new(f"cutter{k}", mesh)
        obj.users_collection[0].objects.link(tool)
        tool.location = blender(centre)
        mod = obj.modifiers.new(f"cut{k}", "BOOLEAN")
        mod.operation = "DIFFERENCE"
        mod.solver = "EXACT"
        mod.object = tool
        with bpy.context.temp_override(object=obj, active_object=obj):
            bpy.ops.object.modifier_apply(modifier=mod.name)
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
            rows.append([V((sign * chine_hb(s), chine_y(s), Z0 + s * LENGTH)),
                         V((sign * planform(s), sheer_y(s), z_sheer(s)))])
        return grid(rows, lambda p: V((0, p.y, p.z)))

    ply(h, "side_port", side(1), m["ply"])
    ply(h, "side_starboard", side(-1), m["ply"])
    rows = [[V((-chine_hb(s), chine_y(s), Z0 + s * LENGTH)),
             V((chine_hb(s), chine_y(s), Z0 + s * LENGTH))] for s in ss]
    ply(h, "bottom", grid(rows, lambda p: p + UP * 0.3), m["ply_dark"])
    tr = [[V((-chine_hb(0), 0, Z0)), V((chine_hb(0), 0, Z0))],
          [V((-planform(0), sheer_y(0), Z0 - t)),
           V((planform(0), sheer_y(0), Z0 - t))]]
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


def recalc(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(obj.data)
    bm.free()
    for p in obj.data.polygons:
        p.use_smooth = False
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

    # Chine logs along the inside of each chine, and a keelson and two
    # stringers on the bottom, between the bulkheads.
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
    for x, tag in ((0.0, "keelson"), (0.36, "stringer_port"),
                   (-0.36, "stringer_starboard")):
        pts = []
        for z in zs:
            hc, yc, hs, ys = section_at(z)
            pts.append((V((x, yc + 0.012, z)), 0.03, 0.024))
        part(h, tag, loft(pts, UP, sides=4, turn=math.pi / 4),
             m["ply_end"], levels=0, smooth=False)

    # Floorboards between the stringers and the chine logs.
    boards = []
    for x0, x1 in ((-0.33, -0.03), (0.03, 0.33)):
        rows = []
        for z in zs[1:-1]:
            hc, yc, hs, ys = section_at(z)
            y = yc + 0.03
            rows.append([V((x0, y, z)), V((x1, y, z))])
        boards.append(grid(rows, lambda p: p - UP))
    ply(h, "floorboards", merge(*boards), m["ply"], thickness=0.012)

    # The thwart across the coaming, a quarter of the way aft.
    z = COCKPIT_FORE - 0.27 * span
    s = s_sheer(z)
    w = opening(s) + COAMING_W * 0.8
    obj = part(h, "thwart", slab(V((0, sheer_y(s) + DECK + COAMING_H - 0.012,
                                    z)), (w, 0.016, 0.055)),
               m["ply"], levels=0, smooth=False)
    bevelled(obj, 0.006, segments=2)


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
