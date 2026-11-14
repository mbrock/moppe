"""Builds models/jet_ski.blend: a sit-down jet ski of the early nineties.

    Blender -b --factory-startup --python tools/figure/jet_ski.py

A two-seat runabout of the kind that filled every lake in 1991: a white
glass-fibre hull 2.9 m long and a metre in the beam, a vee bottom between
hard chines, a black rub rail round the bond line, footwells either side
of a long saddle, a cowl rising ahead of it to the bars, and flashes of
teal and magenta down its flanks. A 650 cc two-stroke twin under the
saddle turns a jet pump: an intake grate under the hull, and a nozzle on
the transom that the bars swing.

It is authored life size in the game's frame -- x to port, y up, z
forward -- with its origin on the keel under the front of the saddle. Its
assemblies are the hull (with the saddle and everything fixed to it),
the bars, which turn about the steering column, and the nozzle, which
swings about its pivot; tools/figure/export_model.py writes each in its
empty's own frame, and the empties marked as points -- the saddle, the
footwells, the grips, the column's foot and head, the jet, and the
intake -- as model-space locations the game places the rider and the
jet by. mov::JetSki's lines agree with the hull's.

Rebuilding discards hand edits to the .blend.
"""

import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(__file__))
from shapes import (V, RIGHT, UP, FORWARD, Cage, assembly, blob,  # noqa
                    loft, material, merge, part, slab, torus, tube)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "models", "jet_ski.blend")

# The hull's lines, metres; mov/jet_ski.cc agrees.
LENGTH = 2.9
Z0 = -1.05          # the transom
Z1 = Z0 + LENGTH    # the stem
HALF_BEAM = 0.52    # at the bond line
CHINE = 0.86        # the chine's breadth as a share of the bond line's
DEADRISE = 0.12     # the chine over the keel, aft
SHEER = 0.36        # the bond line over the keel, aft
SPRING = 0.10       # its rise to the stem
ROCKER = 0.30       # the keel's rise at the stem
STEM = 0.05         # half the stem's width

LIP = 0.035         # the gunwale's top over the bond line
FLOOR = 0.27        # the footwells' floor over the keel
PEDESTAL = 0.52     # the saddle's base

# Where the rider sits and what they hold; mov::JetSki agrees.
SADDLE = V((0.0, 0.66, -0.30))
COLUMN = V((0.0, 0.80, 0.42))       # the steering column's foot
COLUMN_TOP = V((0.0, 0.97, 0.34))   # and the middle of the bars
GRIP = V((0.31, 0.985, 0.285))      # the left grip; the right mirrors it
FOOT = V((0.31, FLOOR, 0.02))
NOZZLE = V((0.0, 0.13, -1.09))
INTAKE = V((0.0, 0.03, -0.55))

PALETTE = {
    "gelcoat": (0.93, 0.93, 0.90),
    "teal": (0.04, 0.56, 0.58),
    "magenta": (0.84, 0.11, 0.48),
    "rubber": (0.05, 0.05, 0.055),
    "mat": (0.30, 0.31, 0.33),
    "saddle": (0.09, 0.10, 0.14),
    "saddle_trim": (0.04, 0.50, 0.53),
    "black": (0.07, 0.07, 0.075),
    "alloy": (0.66, 0.67, 0.66),
    "steel": (0.58, 0.60, 0.62),
    "dial": (0.80, 0.84, 0.82),
    "lanyard": (0.85, 0.10, 0.08),
}

# Polished gelcoat, vinyl, moulded rubber, and bright metal.
FINISH = {
    "gelcoat": dict(roughness=0.22, coat=0.6, coat_roughness=0.08),
    "teal": dict(roughness=0.25, coat=0.6, coat_roughness=0.08),
    "magenta": dict(roughness=0.25, coat=0.6, coat_roughness=0.08),
    "rubber": dict(roughness=0.85),
    "mat": dict(roughness=0.95),
    "saddle": dict(roughness=0.55, sheen=0.2),
    "saddle_trim": dict(roughness=0.55, sheen=0.2),
    "black": dict(roughness=0.5),
    "alloy": dict(metallic=0.85, roughness=0.4),
    "steel": dict(metallic=0.9, roughness=0.25),
    "dial": dict(roughness=0.08, coat=1.0),
    "lanyard": dict(roughness=0.6),
}


def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


# Each line by s, 0 at the transom and 1 at the stem.

def fore(s):
    return max(0.0, (s - 0.5) / 0.5)


def planform(s):
    return max(STEM, HALF_BEAM * (1 - fore(s) ** 2.2))


def keel_y(s):
    return ROCKER * fore(s) ** 2


def sheer_y(s):
    return SHEER + SPRING * s * s


def chine_y(s):
    k = fore(s) ** 2
    return (keel_y(s) + DEADRISE) * (1 - k) + (sheer_y(s) - 0.03) * k


def station(s):
    return Z0 + s * LENGTH


def keel(s):
    return V((0, keel_y(s), station(s)))


def chine(s, side):
    return V((side * max(STEM, planform(s) * CHINE), chine_y(s),
              station(s)))


def bond(s, side):
    return V((side * planform(s), sheer_y(s), station(s)))


def grid(rows, inside):
    """A sheet through rows of points, wound to face away from `inside`."""
    cage = Cage()
    n = len(rows[0])
    for row in rows:
        cage.verts += row
    for i in range(len(rows) - 1):
        for j in range(n - 1):
            a = i * n + j
            cage.faces.append([a, a + 1, a + n + 1, a + n])
    cage.orient(inside)
    return cage


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    coll = bpy.data.collections.new("jet_ski")
    scene.collection.children.link(coll)
    m = {name: material(name, c, **FINISH[name])
         for name, c in PALETTE.items()}

    h = assembly("hull", coll, V((0, 0, 0)), mesh="hull")
    shell(h, m)
    deck(h, m)
    saddle(h, m)
    flashes(h, m)
    fittings(h, m)
    bars(coll, m)
    nozzle(coll, m)

    assembly("seat", coll, SADDLE, point="seat")
    assembly("column", coll, COLUMN, point="column")
    assembly("column_top", coll, COLUMN_TOP, point="column_top")
    assembly("grip_left", coll, GRIP, point="grip_left")
    assembly("grip_right", coll, V((-GRIP.x, GRIP.y, GRIP.z)),
             point="grip_right")
    assembly("foot_left", coll, FOOT, point="foot_left")
    assembly("foot_right", coll, V((-FOOT.x, FOOT.y, FOOT.z)),
             point="foot_right")
    assembly("intake", coll, INTAKE, point="intake")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
    print(f"wrote {OUT}: {len(coll.objects)} objects")


# The hull below the bond line.

STATIONS = 40


def shell(h, m):
    ss = [i / STATIONS for i in range(STATIONS + 1)]

    def inside(p):
        return V((0, 0.26, p.z))

    # Each half of the bottom and each side is its own sheet, so that
    # the keel and the chines stay hard.
    for side, name in ((1, "port"), (-1, "starboard")):
        part(h, f"bottom_{name}",
             grid([[keel(s).lerp(chine(s, side), k / 4) for k in range(5)]
                   for s in ss], inside), m["gelcoat"], levels=0)
        part(h, f"side_{name}",
             grid([[chine(s, side).lerp(bond(s, side), k / 3)
                    for k in range(4)] for s in ss], inside),
             m["gelcoat"], levels=0)

    # The transom, and the stem's narrow face.
    for name, s, off in (("transom", 0.0, 0.3), ("stem", 1.0, -0.3)):
        cage = Cage()
        cage.verts = [keel(s), chine(s, 1), bond(s, 1), bond(s, -1),
                      chine(s, -1)]
        cage.faces = [[0, 1, 2, 3, 4]]
        cage.orient(lambda p, off=off: p + FORWARD * off)
        part(h, name, cage, m["gelcoat"], levels=0, smooth=False)

    # The rub rail round the bond line, and a bumper on the stem.
    line = [bond(s, 1) for s in ss]
    line += [V((0, sheer_y(1), Z1 + 0.035))]
    line += [bond(s, -1) for s in reversed(ss)]
    part(h, "rub_rail", loft([(p + UP * 0.004, 0.024, 0.02) for p in line],
                             UP, sides=6, turn=math.pi / 6),
         m["rubber"], levels=0)
    part(h, "bumper", blob(V((0, sheer_y(1) - 0.035, Z1 + 0.01)),
                           (0.085, 0.07, 0.06), 10, 6), m["rubber"])


# The deck: one moulding, drawn as its height over the keel at each
# place inside the bond line.

HOOD = [(0.12, PEDESTAL), (0.26, 0.63), (0.38, 0.755), (0.50, 0.80),
        (0.66, 0.785), (0.90, 0.725), (1.20, 0.635), (1.50, 0.555),
        (1.72, 0.51), (Z1, 0.495)]


def hood_top(z):
    def at(z):
        if z <= HOOD[0][0]:
            return HOOD[0][1]
        for (z0, y0), (z1, y1) in zip(HOOD, HOOD[1:]):
            if z <= z1:
                return y0 + (y1 - y0) * (z - z0) / (z1 - z0)
        return HOOD[-1][1]
    return (at(z - 0.07) + 2 * at(z) + at(z + 0.07)) / 4


def deck_height(x, z):
    s = (z - Z0) / LENGTH
    hb = planform(s)
    ax = abs(x)
    rim = sheer_y(s)
    gunwale = rim + LIP
    # The footwells run from the after platform to the cowl, either side
    # of the pedestal the saddle sits on.
    well = smoothstep(0.54, 0.38, z) * smoothstep(-1.00, -0.90, z)
    pedestal = smoothstep(0.215, 0.165, ax)
    top = 0.40 + (PEDESTAL - 0.40) * smoothstep(-0.90, -0.76, z)
    inside = FLOOR + (top - FLOOR) * pedestal
    interior = gunwale + (inside - gunwale) * well
    y = gunwale + (interior - gunwale) * smoothstep(hb - 0.05, hb - 0.085,
                                                    ax)
    # The cowl, rising ahead of the saddle and running down to the stem.
    w = min(0.31, hb - 0.07)
    if z > 0.1 and w > 0.02 and ax < w:
        bump = (1 - (ax / w) ** 2.6) ** 0.6
        y = max(y, gunwale + (hood_top(z) - gunwale) * bump)
    # Down to the bond line at its edge.
    return rim + (y - rim) * smoothstep(hb, hb - 0.03, ax)


DECK_U = [0.0, 0.1, 0.2, 0.28, 0.32, 0.36, 0.40, 0.44, 0.52, 0.62, 0.72,
          0.80, 0.84, 0.88, 0.92, 0.96, 1.0]


def deck(h, m):
    us = [-u for u in reversed(DECK_U[1:])] + DECK_U
    stations = 64
    rows = []
    for i in range(stations + 1):
        s = i / stations
        z = station(s)
        hb = planform(s)
        rows.append([V((u * hb, deck_height(u * hb, z), z)) for u in us])
    obj = part(h, "deck", grid(rows, lambda p: V((p.x, -1.0, p.z))),
               m["gelcoat"], levels=1)
    # Grey traction mats on the footwells' floors and the after platform.
    obj.data.materials.append(m["mat"])
    for p in obj.data.polygons:
        c = V((p.center.x, p.center.z, -p.center.y))
        level = p.normal.z > 0.985
        floor = abs(c.y - FLOOR) < 0.012 and abs(c.x) > 0.23
        platform = c.z < -0.93 and abs(c.x) < planform(0) - 0.10
        if level and (floor or platform):
            p.material_index = 1


def saddle(h, m):
    """A long vinyl saddle for two, on the pedestal."""
    part(h, "saddle", loft([
        (V((0, 0.555, -0.86)), 0.11, 0.045),
        (V((0, 0.595, -0.76)), 0.17, 0.08),
        (V((0, 0.585, -0.30)), 0.175, 0.078),
        (V((0, 0.59, 0.04)), 0.165, 0.075),
        (V((0, 0.615, 0.22)), 0.135, 0.07),
        (V((0, 0.66, 0.33)), 0.08, 0.04),
    ], RIGHT, sides=10), m["saddle"], levels=1)
    # A band of trim round its skirt.
    for side in (1, -1):
        part(h, f"saddle_trim{side}", loft([
            (V((side * 0.176, 0.535, -0.74)), 0.006, 0.014),
            (V((side * 0.181, 0.530, -0.30)), 0.006, 0.014),
            (V((side * 0.171, 0.535, 0.06)), 0.006, 0.014),
            (V((side * 0.142, 0.56, 0.24)), 0.006, 0.014),
        ], RIGHT, sides=6), m["saddle_trim"], levels=0)


def flashes(h, m):
    """Two flashes down each flank, a broad teal one and a magenta one
    over it, sweeping up toward the bow."""
    def band(side, name, mat, s0, s1, low, high):
        rows = []
        n = 28
        for i in range(n + 1):
            s = s0 + (s1 - s0) * i / n
            # Tapered to a point at each end.
            taper = min(1.0, 4.5 * i / n, 3.0 * (n - i) / n)
            sweep = 0.22 * (i / n) ** 2
            mid = 0.5 * (low + high) + sweep
            half = 0.5 * (high - low) * taper
            a, b = chine(s, side), bond(s, side)
            out = V((side, 0.25, 0)).normalized() * 0.006
            rows.append([a.lerp(b, min(0.97, mid - half)) + out,
                         a.lerp(b, min(0.98, mid + half)) + out])
        part(h, name, grid(rows, lambda p: V((0, p.y, p.z))), mat,
             levels=0)

    for side in (1, -1):
        band(side, f"flash_teal{side}", m["teal"], 0.10, 0.80, 0.16, 0.52)
        band(side, f"flash_magenta{side}", m["magenta"], 0.20, 0.86, 0.60,
             0.76)


def fittings(h, m):
    # The column's shroud on the cowl, with a dial in its after face.
    part(h, "shroud", loft([
        (COLUMN + V((0, -0.06, 0.05)), 0.085, 0.10),
        (COLUMN + V((0, 0.05, 0.0)), 0.07, 0.08),
        (COLUMN_TOP + V((0, -0.04, 0.0)), 0.05, 0.055),
    ], RIGHT, sides=10), m["gelcoat"], levels=1)
    face = (V((0, 0.55, -1))).normalized()
    c = COLUMN + V((0, 0.045, -0.085))
    part(h, "dial", loft([(c, 0.036, 0.036), (c + face * 0.006, 0.032,
                                              0.032)], RIGHT, sides=12),
         m["dial"], levels=0)
    part(h, "dial_rim", torus(c + face * 0.003, face, RIGHT, 0.037,
                              (0.005, 0.005), segments=16, sides=6),
         m["black"], levels=0)

    # A vent each side of the cowl, and the fuel filler on its right.
    for side in (1, -1):
        part(h, f"vent{side}", slab(V((side * 0.20, 0.70, 0.74)),
                                    (0.035, 0.012, 0.09),
                                    axes=(RIGHT,
                                          V((side * 0.75, 1, 0.12))
                                          .normalized(), FORWARD)),
             m["black"], levels=0, bevel=0.008)
    part(h, "filler", loft([(V((-0.13, 0.735, 1.02)), 0.035, 0.035),
                            (V((-0.13, 0.755, 1.02)), 0.03, 0.03)],
                           RIGHT, sides=10), m["black"], levels=0)

    # The grab handle behind the saddle, and the eye in the stem.
    part(h, "grab_handle", tube([V((0.13, 0.41, -0.885)),
                                 V((0.13, 0.505, -0.935)),
                                 V((0.0, 0.52, -0.945)),
                                 V((-0.13, 0.505, -0.935)),
                                 V((-0.13, 0.41, -0.885))], 0.016,
                                ref=FORWARD, sides=6), m["black"], levels=0)
    part(h, "bow_eye", torus(V((0, sheer_y(1) - 0.10, Z1 + 0.012)), RIGHT,
                             UP, 0.026, (0.007, 0.007), segments=12,
                             sides=6), m["steel"], levels=0)

    # Under the stern: the pump's intake grate, the ride plate, and the
    # pump's housing through the transom.
    bars_ = [slab(INTAKE + V((x, -0.014, 0)), (0.006, 0.012, 0.13))
             for x in (-0.05, -0.025, 0.0, 0.025, 0.05)]
    bars_.append(slab(INTAKE + V((0, -0.012, 0.13)), (0.065, 0.012, 0.012)))
    bars_.append(slab(INTAKE + V((0, -0.012, -0.13)), (0.065, 0.012,
                                                       0.012)))
    part(h, "grate", merge(*bars_), m["alloy"], levels=0, smooth=False)
    part(h, "ride_plate", slab(V((0, 0.012, -0.90)), (0.11, 0.012, 0.15)),
         m["alloy"], levels=0, bevel=0.004, smooth=False)
    part(h, "pump", loft([(V((0, NOZZLE.y, Z0 + 0.06)), 0.075, 0.075),
                          (V((0, NOZZLE.y, Z0 - 0.015)), 0.068, 0.068),
                          (NOZZLE + FORWARD * 0.005, 0.05, 0.05)],
                         RIGHT, sides=12), m["alloy"], levels=0)


def bars(coll, m):
    """The bars on the column's head: they turn about the column, which
    leans back toward the rider."""
    b = assembly("bars", coll, COLUMN, mesh="bars")
    o = COLUMN
    top = COLUMN_TOP - o
    grip = GRIP - o
    part(b, "stem", tube([V((0, 0, 0)), top], 0.02, ref=RIGHT, sides=8),
         m["black"], levels=0)
    points = [V((s * grip.x, grip.y, grip.z)) for s in (1,)]
    line = [V((grip.x + 0.03, grip.y + 0.002, grip.z - 0.006)),
            V((grip.x - 0.08, grip.y - 0.004, grip.z + 0.02)),
            V((0.10, top.y, top.z)), V((-0.10, top.y, top.z)),
            V((-grip.x + 0.08, grip.y - 0.004, grip.z + 0.02)),
            V((-grip.x - 0.03, grip.y + 0.002, grip.z - 0.006))]
    del points
    part(b, "bar", tube(line, 0.013, ref=UP, sides=8), m["steel"],
         levels=0)
    for side in (1, -1):
        part(b, f"grip{side}", tube([
            V((side * (grip.x - 0.055), grip.y - 0.002, grip.z + 0.012)),
            V((side * (grip.x + 0.045), grip.y + 0.003, grip.z - 0.009))],
            0.019, ref=UP, sides=8), m["rubber"], levels=0)
    # The pad over the clamp, the throttle's lever under the right grip,
    # and the stop switch with its lanyard by the left.
    part(b, "pad", slab(top + V((0, 0.012, 0.0)), (0.085, 0.028, 0.03)),
         m["saddle_trim"], bevel=0.012)
    part(b, "throttle", tube([
        V((-(grip.x - 0.09), grip.y - 0.012, grip.z + 0.03)),
        V((-(grip.x - 0.02), grip.y - 0.03, grip.z + 0.045))], 0.006,
        ref=UP, sides=6), m["black"], levels=0)
    part(b, "switch", slab(V((grip.x - 0.09, grip.y - 0.004, grip.z +
                              0.016)), (0.016, 0.02, 0.02)), m["black"],
         levels=0, bevel=0.005)
    part(b, "lanyard", tube([
        V((grip.x - 0.09, grip.y - 0.02, grip.z + 0.0)),
        V((grip.x - 0.10, grip.y - 0.07, grip.z - 0.03)),
        V((grip.x - 0.08, grip.y - 0.11, grip.z - 0.07))], 0.005,
        ref=RIGHT, sides=5), m["lanyard"], levels=0)


def nozzle(coll, m):
    """The steering nozzle: it swings about its pivot's y."""
    n = assembly("nozzle", coll, NOZZLE, mesh="nozzle", point="jet")
    part(n, "cone", loft([(V((0, 0, 0.03)), 0.062, 0.062),
                          (V((0, 0, -0.05)), 0.055, 0.055),
                          (V((0, 0, -0.13)), 0.042, 0.042)],
                         RIGHT, sides=12, close=(True, True)), m["steel"],
         levels=0)
    part(n, "bore", loft([(V((0, 0, -0.131)), 0.034, 0.034),
                          (V((0, 0, -0.133)), 0.03, 0.03)],
                         RIGHT, sides=12), m["black"], levels=0)


if __name__ == "__main__":
    build()
