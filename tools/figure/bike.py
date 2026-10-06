"""Builds models/bike.blend: the dirt bike, as rigid moving assemblies.

    Blender -b --factory-startup --python tools/figure/bike.py

The bike is authored in its model space, the game's frame at two thirds of
world scale (game/vehicle_render.cc scales it by 1.5): x right, y up, z
forward. Each assembly is an empty -- frame, steering, fork sliders,
swingarm, shock body and shaft, wheel, nozzle -- whose children are its
parts; tools/figure/export_model.py writes each assembly's parts in the
empty's own frame, and the empties marked as points (axles, pivots,
grips, pegs, saddle) as model-space locations the game places things by.

Moving parts stay rigid: the swingarm swings about its pivot, the shock
body and shaft turn to stay aimed at each other, and the fork sliders
telescope up the fork tubes. Rebuilding discards hand edits to the .blend.
"""

import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(__file__))
from shapes import (V, RIGHT, UP, FORWARD, Cage, assembly, blob,  # noqa
                    blender, loft, material, merge, part, slab, torus, tube)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "models", "bike.blend")

# Key points of the bike's model space.
REAR_AXLE = V((0, -0.55, -0.75))
STEERING_HEAD = V((0, 0.05, 0.55))
FRONT_AXLE_IN_STEERING = V((0, -0.60, 0.20))
FRONT_AXLE = STEERING_HEAD + FRONT_AXLE_IN_STEERING
SWINGARM_PIVOT = V((0, -0.30, -0.12))
SHOCK_TOP = V((0, -0.10, -0.26))
SHOCK_BOTTOM = V((0, -0.43, -0.52))
WHEEL_RADIUS = 0.415

FORK = (FRONT_AXLE_IN_STEERING - V((0, 0.10, -0.02))).normalized()

PALETTE = {
    "blue": (0.15, 0.50, 1.0),
    "white": (0.92, 0.93, 0.95),
    "seat": (0.10, 0.10, 0.14),
    "engine": (0.20, 0.21, 0.23),
    "engine_light": (0.50, 0.52, 0.55),
    "frame": (0.24, 0.25, 0.28),
    "silver": (0.66, 0.68, 0.72),
    "chrome": (0.80, 0.82, 0.86),
    "gold": (0.82, 0.62, 0.24),
    "tire": (0.06, 0.06, 0.07),
    "rim": (0.72, 0.74, 0.78),
    "hub": (0.42, 0.44, 0.48),
    "black": (0.08, 0.08, 0.10),
    "spring": (0.88, 0.28, 0.10),
    "nozzle": (0.55, 0.57, 0.62),
    "nozzle_inner": (0.12, 0.12, 0.14),
    "lens": (1.0, 0.95, 0.70),
    "tail_light": (1.0, 0.22, 0.12),
}


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    coll = bpy.data.collections.new("bike")
    scene.collection.children.link(coll)
    m = {name: material(name, c, emit=name in ("lens", "tail_light"))
         for name, c in PALETTE.items()}

    wheel(coll, m)
    chassis(coll, m)
    steering(coll, m)
    suspension(coll, m)
    nozzles(coll, m)

    assembly("fork_top", coll, STEERING_HEAD + V((0, 0.10, -0.02)),
             point="fork_top")
    assembly("muffler_tip", coll, V((0.17, -0.232, -0.81)),
             point="muffler_tip")

    # Where the rider sits, stands, and holds on.
    assembly("saddle", coll, V((0, 0.085, -0.03)), point="saddle")
    for s, tag in ((-1, "left"), (1, "right")):
        assembly(f"peg_{tag}", coll, V((0.2 * s, -0.27, 0.0)),
                 point=f"peg_{tag}")
        assembly(f"grip_{tag}", coll,
                 STEERING_HEAD + V((0.31 * s, 0.215, -0.12)),
                 point=f"grip_{tag}")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
    print(f"wrote {OUT}: {len(coll.objects)} objects")


def wheel(coll, m):
    """A spoked wheel around the x axle with a knobby tyre, a disc, and a
    hub. The rear empty exports it; the front one only displays a copy."""
    rear = assembly("wheel", coll, REAR_AXLE, mesh="wheel",
                    point="rear_axle")
    o = V((0, 0, 0))
    tyre_r = WHEEL_RADIUS - 0.1
    part(rear, "tyre", torus(o, RIGHT, UP, tyre_r, (0.1, 0.095), 28, 8),
         m["tire"])
    knobs = []
    for i in range(28):
        a = 2 * math.pi * (i + 0.5 * (i % 2)) / 28
        out = UP * math.cos(a) + FORWARD * math.sin(a)
        along = RIGHT.cross(out)
        for x in ((-0.045, 0.045) if i % 2 else (0.0,)):
            centre = out * (WHEEL_RADIUS - 0.005) + RIGHT * x
            knobs.append(slab(centre, (0.03, 0.022, 0.026),
                              axes=(RIGHT, out, along)))
    part(rear, "knobs", merge(*knobs), m["tire"], levels=0, bevel=0.006,
         smooth=False)
    part(rear, "rim", torus(o, RIGHT, UP, 0.265, (0.022, 0.03), 28, 6),
         m["rim"])
    part(rear, "hub", loft([
        (V((-0.09, 0, 0)), 0.035, 0.035),
        (V((-0.07, 0, 0)), 0.055, 0.055),
        (V((0.07, 0, 0)), 0.055, 0.055),
        (V((0.09, 0, 0)), 0.035, 0.035),
    ], UP, sides=10), m["hub"])
    spokes = []
    for i in range(18):
        a = 2 * math.pi * i / 18
        out = UP * math.cos(a) + FORWARD * math.sin(a)
        side = -0.06 if i % 2 else 0.06
        spokes.append(tube([V((side, 0, 0)) + out * 0.05,
                            out * 0.255 + RIGHT * (side * 0.2)], 0.006,
                           sides=4))
    part(rear, "spokes", merge(*spokes), m["chrome"], levels=0)
    part(rear, "disc", loft([
        (V((-0.1, 0, 0)), 0.15, 0.15),
        (V((-0.11, 0, 0)), 0.15, 0.15),
    ], UP, sides=16), m["silver"], levels=0, smooth=False)

    # A display copy at the front axle sharing the meshes.
    front = assembly("front_wheel", coll, FRONT_AXLE, point="front_axle")
    for child in rear.children:
        dup = child.copy()
        coll.objects.link(dup)
        dup.parent = front


def chassis(coll, m):
    c = assembly("chassis", coll, V((0, 0, 0)), mesh="chassis")

    # Frame: a steel cradle from the steering head down around the engine
    # to the swingarm pivot, and a subframe under the seat.
    for s in (-1, 1):
        part(c, f"down_tube{s}", tube([
            STEERING_HEAD + V((0.03 * s, -0.08, -0.02)),
            V((0.06 * s, -0.25, 0.37)),
            V((0.1 * s, -0.48, 0.24)),
            V((0.1 * s, -0.52, 0.02)),
            V((0.1 * s, -0.42, -0.12)),
        ], 0.022), m["frame"])
        part(c, f"top_tube{s}", tube([
            STEERING_HEAD + V((0.03 * s, 0.0, -0.04)),
            V((0.08 * s, -0.06, 0.15)),
            V((0.1 * s, -0.12, -0.12)),
            V((0.1 * s, -0.36, -0.13)),
        ], 0.025), m["frame"])
        part(c, f"subframe{s}", tube([
            V((0.09 * s, -0.12, -0.12)),
            V((0.085 * s, -0.03, -0.55)),
            V((0.07 * s, 0.0, -0.72)),
        ], 0.016), m["frame"])
        part(c, f"strut{s}", tube([
            V((0.1 * s, -0.33, -0.14)),
            V((0.085 * s, -0.04, -0.52)),
        ], 0.014), m["frame"])
    part(c, "head_tube", tube([
        STEERING_HEAD + V((0, 0.1, -0.03)),
        STEERING_HEAD + V((0, -0.12, 0.04)),
    ], 0.04, sides=8), m["frame"])

    # Engine: a rounded crankcase, a finned cylinder leaning forward, and a
    # round clutch cover.
    part(c, "crankcase", slab(V((0, -0.37, 0.03)), (0.12, 0.13, 0.17)),
         m["engine"], bevel=0.04)
    part(c, "clutch", loft([
        (V((0.12, -0.38, 0.0)), 0.09, 0.09),
        (V((0.15, -0.38, 0.0)), 0.08, 0.08),
    ], UP, sides=12), m["engine_light"])
    cyl = V((0, -0.17, 0.12))
    lean = (UP + FORWARD * 0.35).normalized()
    part(c, "cylinder", loft([
        (cyl - lean * 0.1, 0.075, 0.075),
        (cyl + lean * 0.12, 0.07, 0.07),
    ], RIGHT, sides=10), m["engine"])
    fins = []
    for i in range(5):
        fins.append(loft([
            (cyl + lean * (-0.06 + 0.04 * i), 0.1, 0.095),
            (cyl + lean * (-0.05 + 0.04 * i), 0.1, 0.095),
        ], RIGHT, sides=10))
    part(c, "fins", merge(*fins), m["engine_light"], levels=0)

    # Tank between the knees, radiator shrouds flaring to the sides, a
    # long flat seat, white side plates, and the rear fender's kicked-up
    # tail with a tail light.
    part(c, "tank", loft([
        (V((0, 0.05, 0.47)), 0.07, 0.06),
        (V((0, 0.07, 0.36)), 0.15, 0.1),
        (V((0, 0.04, 0.15)), 0.17, 0.12),
        (V((0, 0.01, -0.02)), 0.13, 0.1),
        (V((0, 0.02, -0.08)), 0.07, 0.06),
    ], RIGHT, sides=10), m["blue"])
    for s in (-1, 1):
        part(c, f"shroud{s}", slab(V((0.155 * s, -0.06, 0.26)),
                                   (0.008, 0.11, 0.16),
                                   axes=(RIGHT, (UP + FORWARD * 0.4)
                                         .normalized(),
                                         (FORWARD - UP * 0.4).normalized())),
             m["blue"], bevel=0.008)
        part(c, f"radiator{s}", slab(V((0.12 * s, -0.12, 0.3)),
                                     (0.04, 0.12, 0.08)),
             m["engine"], bevel=0.01)
        part(c, f"side_plate{s}", slab(V((0.13 * s, -0.04, -0.47)),
                                       (0.015, 0.09, 0.15),
                                       axes=(RIGHT, UP,
                                             (FORWARD + UP * 0.25)
                                             .normalized())),
             m["white"], bevel=0.02)
    part(c, "seat", loft([
        (V((0, 0.06, 0.02)), 0.07, 0.04),
        (V((0, 0.06, -0.08)), 0.12, 0.05),
        (V((0, 0.06, -0.4)), 0.13, 0.055),
        (V((0, 0.07, -0.66)), 0.11, 0.05),
        (V((0, 0.07, -0.7)), 0.07, 0.035),
    ], RIGHT, sides=10), m["seat"])
    part(c, "rear_fender", loft([
        (V((0, 0.02, -0.5)), 0.13, 0.025),
        (V((0, 0.05, -0.75)), 0.12, 0.02),
        (V((0, 0.15, -1.02)), 0.08, 0.015),
    ], RIGHT, sides=8), m["blue"])
    part(c, "tail_light", slab(V((0, 0.1, -0.96)), (0.035, 0.018, 0.02)),
         m["tail_light"], levels=0, bevel=0.006)

    # Exhaust: a header from the cylinder's front curling down the right
    # side, and a fat silencer under the seat with a dark end cap.
    part(c, "header", tube([
        cyl + lean * 0.02 + FORWARD * 0.07,
        V((0.05, -0.2, 0.3)),
        V((0.13, -0.3, 0.22)),
        V((0.17, -0.32, 0.0)),
        V((0.17, -0.27, -0.3)),
    ], 0.035, sides=8), m["silver"])
    part(c, "silencer", loft([
        (V((0.17, -0.27, -0.32)), 0.05, 0.05),
        (V((0.17, -0.255, -0.4)), 0.07, 0.075),
        (V((0.17, -0.24, -0.74)), 0.07, 0.075),
        (V((0.17, -0.235, -0.79)), 0.05, 0.055),
    ], UP, sides=10), m["chrome"])
    part(c, "end_cap", loft([
        (V((0.17, -0.235, -0.78)), 0.052, 0.057),
        (V((0.17, -0.232, -0.81)), 0.035, 0.035),
    ], UP, sides=10), m["black"], levels=0)

    # Footpegs, folded out on their brackets.
    for s in (-1, 1):
        part(c, f"peg{s}", slab(V((0.19 * s, -0.285, 0.0)),
                                (0.06, 0.016, 0.03)), m["silver"],
             bevel=0.008)


def steering(coll, m):
    """The clamp cluster turns with the bars about the steering head."""
    st = assembly("steering", coll, STEERING_HEAD, mesh="steering",
                  point="steering_head")
    for s in (-1, 1):
        x = 0.1 * s
        top = V((x, 0.13, -0.03))
        part(st, f"fork_tube{s}", tube([top, top + FORK * 0.4], 0.04,
                                       sides=10), m["chrome"])
    for y, z in ((0.1, -0.02), (-0.05, 0.03)):
        part(st, f"clamp{y}", slab(V((0, y, z)), (0.15, 0.025, 0.05)),
             m["engine_light"], bevel=0.015)

    # Bars rising from their clamps, with a pad, grips, and levers.
    part(st, "bar", tube([
        V((-0.34, 0.22, -0.13)), V((-0.2, 0.2, -0.08)),
        V((0, 0.19, -0.05)), V((0.2, 0.2, -0.08)),
        V((0.34, 0.22, -0.13)),
    ], 0.016, sides=8), m["silver"])
    for s in (-1, 1):
        part(st, f"riser{s}", tube([V((0.05 * s, 0.11, -0.02)),
                                    V((0.05 * s, 0.195, -0.05))], 0.02,
                                   sides=8), m["engine_light"])
    part(st, "bar_pad", loft([
        (V((-0.07, 0.2, -0.05)), 0.03, 0.03),
        (V((0.07, 0.2, -0.05)), 0.03, 0.03),
    ], UP, sides=8), m["blue"])
    for s in (-1, 1):
        part(st, f"grip{s}", loft([
            (V((0.25 * s, 0.208, -0.1)), 0.025, 0.025),
            (V((0.35 * s, 0.222, -0.135)), 0.026, 0.026),
        ], UP, sides=8), m["black"])
        part(st, f"lever{s}", tube([
            V((0.2 * s, 0.205, -0.07)), V((0.3 * s, 0.2, -0.02)),
        ], 0.007, sides=4), m["black"], levels=0)

    # Number plate with the headlight set into it, and the front fender
    # riding above the wheel.
    tilt = (UP + FORWARD * 0.3).normalized()
    plate = V((0, 0.0, 0.1))
    part(st, "plate", slab(plate, (0.13, 0.15, 0.015),
                           axes=(RIGHT, tilt, RIGHT.cross(tilt))),
         m["white"], bevel=0.03)
    part(st, "headlight_rim", loft([
        (plate + FORWARD * 0.01 - tilt * 0.04, 0.07, 0.07),
        (plate + FORWARD * 0.04 - tilt * 0.04, 0.07, 0.07),
    ], UP, sides=12), m["black"])
    part(st, "lens", blob(plate + FORWARD * 0.045 - tilt * 0.04,
                          (0.058, 0.058, 0.02), 12, 6), m["lens"])
    part(st, "front_fender", loft([
        (V((0, -0.14, -0.2)), 0.11, 0.015),
        (V((0, -0.11, 0.05)), 0.12, 0.02),
        (V((0, -0.14, 0.28)), 0.11, 0.018),
        (V((0, -0.2, 0.42)), 0.09, 0.012),
    ], RIGHT, sides=8), m["blue"])
    assembly("headlight", coll, STEERING_HEAD + plate + FORWARD * 0.05 -
             tilt * 0.04, point="headlight")


def suspension(coll, m):
    # Fork sliders: gold lowers that telescope up the fork tubes and
    # carry the front axle.
    sl = assembly("fork_sliders", coll, FRONT_AXLE, mesh="fork_sliders")
    for s in (-1, 1):
        x = 0.1 * s
        part(sl, f"slider{s}", tube([
            V((x, 0.0, 0.0)) - FORK * 0.02,
            V((x, 0.0, 0.0)) - FORK * 0.36,
        ], 0.048, sides=10), m["gold"])
        part(sl, f"lug{s}", slab(V((x, 0.0, 0.0)), (0.03, 0.04, 0.045)),
             m["gold"], bevel=0.012)

    # Swingarm: two tapered arms from the pivot to the axle blocks, joined
    # by a cross brace, with the chain guide below.
    sa = assembly("swingarm", coll, SWINGARM_PIVOT, mesh="swingarm",
                  point="swingarm_pivot")
    axle = REAR_AXLE - SWINGARM_PIVOT
    for s in (-1, 1):
        x = 0.1 * s
        part(sa, f"arm{s}", loft([
            (V((x, 0, 0.02)), 0.025, 0.05),
            (V((x, 0, 0)) + axle * 0.5, 0.025, 0.045),
            (V((x, 0, 0)) + axle * 1.04, 0.022, 0.035),
        ], RIGHT, sides=8), m["silver"])
    part(sa, "brace", tube([V((-0.1, 0, 0)) + axle * 0.22,
                            V((0.1, 0, 0)) + axle * 0.22], 0.025),
         m["silver"])
    part(sa, "pivot_bolt", tube([V((-0.13, 0, 0)), V((0.13, 0, 0))], 0.03,
                                sides=8), m["engine_light"])
    part(sa, "shock_mount", slab(SHOCK_BOTTOM - SWINGARM_PIVOT + V((0, -0.01,
                                                                   0)),
                                 (0.03, 0.03, 0.03)), m["silver"],
         bevel=0.008)

    # Shock: a body with a coil spring hung from the frame, and a shaft
    # rising from the swingarm into it.
    sb = assembly("shock_body", coll, SHOCK_TOP, mesh="shock_body",
                  point="shock_top")
    down = (SHOCK_BOTTOM - SHOCK_TOP).normalized()
    part(sb, "reservoir", tube([V((0, 0, 0)), down * 0.24], 0.038,
                               sides=10), m["black"])
    coil = []
    turns, steps = 7, 7 * 10
    pts = []
    for i in range(steps + 1):
        t = i / steps
        a = 2 * math.pi * turns * t
        u = RIGHT
        v = down.cross(RIGHT)
        pts.append(down * (0.04 + 0.26 * t) +
                   (u * math.cos(a) + v * math.sin(a)) * 0.055)
    coil.append(tube(pts, 0.011, ref=RIGHT, sides=5))
    part(sb, "spring", merge(*coil), m["spring"], levels=0)
    ss = assembly("shock_shaft", coll, SHOCK_BOTTOM, mesh="shock_shaft",
                  point="shock_bottom")
    part(ss, "shaft", tube([V((0, 0, 0)), -down * 0.22], 0.016, sides=8),
         m["chrome"])
    part(ss, "eye", tube([V((-0.025, 0, 0)), V((0.025, 0, 0))], 0.025,
                         sides=8), m["black"])


def nozzles(coll, m):
    """One jump-jet nozzle, a bell along +z; the game turns it about x."""
    for s, tag in ((-1, "left"), (1, "right")):
        n = assembly(f"nozzle_{tag}", coll, V((0.14 * s, -0.45, -0.35)),
                     mesh="nozzle" if s > 0 else None,
                     point=f"nozzle_{tag}")
        n.rotation_euler = (math.radians(90), 0, 0)
        part(n, f"bell{s}", loft([
            (V((0, 0, -0.02)), 0.05, 0.05),
            (V((0, 0, 0.08)), 0.06, 0.06),
            (V((0, 0, 0.22)), 0.095, 0.095),
        ], UP, sides=12, close=(True, False)), m["nozzle"])
        part(n, f"throat{s}", loft([
            (V((0, 0, 0.21)), 0.08, 0.08),
            (V((0, 0, 0.12)), 0.045, 0.045),
        ], UP, sides=12), m["nozzle_inner"], levels=0)


build()
