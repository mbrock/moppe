"""Builds models/bike.blend: a Honda CT90 Trail 90, as rigid assemblies.

    Blender -b --factory-startup --python tools/figure/bike.py

The bike the game is ridden on: a 1972 CT90 at life size, drawn from
photographs and Honda's figures (1,220 mm between the axles, 2.75-17
trail tyres, a 775 mm seat, 165 mm under the skid plate). Its pressed
steel body runs from under the seat round the rear wheel in Candy Orange,
a silver shroud covers the spine down to the engine lying flat under it,
the exhaust climbs the right side in a corrugated shield to a perforated
heat guard at seat height, and a big chrome rack stands behind the
black, pleated seat. The fork is the telescopic one of 1969 onward, in
rubber gaiters under orange covers, with a round headlamp and the
speedometer on top, and the bars are braced and high, with mirrors on
long stalks. Two jump-jet bells hang either side of the engine, which no
CT90 had.

It is authored in the game's frame (x right, y up, z forward) with its
origin midway between the axles at their height, as mov::RigidBike's
trail90_rig has it. Each assembly is an empty -- frame, steering, fork
sliders, swingarm, shock bodies and shafts, wheel, nozzle -- whose
children are its parts; tools/figure/export_model.py writes each
assembly's parts in the empty's own frame, and the empties marked as
points (axles, pivots, grips, pegs, saddle) as model-space locations the
game places things by.

Moving parts stay rigid: the swingarm swings about its pivot, the twin
shocks' bodies and shafts turn to stay aimed at each other, and the fork
sliders telescope up into the gaiters. Rebuilding discards hand edits to
the .blend.
"""

import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(__file__))
from shapes import (V, RIGHT, UP, FORWARD, assembly, blob,  # noqa
                    loft, material, merge, part, slab, torus, tube)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "models", "bike.blend")

# Honda's figures: 17-inch rims under 2.75 tyres, 1,220 mm apart.
RIM_RADIUS = 0.216
WHEEL_RADIUS = 0.29
REAR_AXLE = V((0, 0, -0.61))
FRONT_AXLE = V((0, 0, 0.61))
# The ground lies a wheel's radius under the axles.
GROUND = -WHEEL_RADIUS

# The steering head, raked some 26 degrees, and the fork down to the axle.
STEERING_HEAD = V((0, 0.46, 0.38))
FRONT_AXLE_IN_STEERING = FRONT_AXLE - STEERING_HEAD
FORK = FRONT_AXLE_IN_STEERING.normalized()
FORK_LENGTH = FRONT_AXLE_IN_STEERING.length

SWINGARM_PIVOT = V((0, -0.03, -0.24))
SHOCK_TOP = V((0, 0.26, -0.585))
SHOCK_BOTTOM = V((0, 0.0, -0.54))
SHOCK_X = 0.118

PALETTE = {
    "orange": (0.93, 0.40, 0.07),
    "shroud": (0.62, 0.63, 0.64),
    "seat": (0.07, 0.07, 0.08),
    "alloy": (0.66, 0.67, 0.68),
    "dark_alloy": (0.34, 0.35, 0.37),
    "frame": (0.55, 0.56, 0.58),
    "chrome": (0.72, 0.74, 0.77),
    "rim": (0.70, 0.72, 0.75),
    "tire": (0.07, 0.07, 0.075),
    "rubber": (0.05, 0.05, 0.055),
    "black": (0.09, 0.09, 0.1),
    "lettering": (0.85, 0.85, 0.86),
    "lens": (1.0, 0.95, 0.75),
    "amber": (1.0, 0.62, 0.08),
    "tail_light": (1.0, 0.18, 0.1),
    "glass": (0.55, 0.62, 0.68),
    "nozzle": (0.36, 0.37, 0.4),
    "nozzle_inner": (0.12, 0.12, 0.14),
}

# How each paint finishes, as the rest of its Principled BSDF: glossy
# enamel on the pressed steel and a satin plastic shroud, a vinyl seat,
# bare cast alloy, chrome on the rack, rims, bars and exhaust, and
# rubber; the lamps' lenses let light through.
FINISH = {
    "orange": dict(roughness=0.28, coat=0.5),
    "shroud": dict(metallic=0.35, roughness=0.42, coat=0.2),
    "seat": dict(roughness=0.55, sheen=0.25),
    "alloy": dict(metallic=0.8, roughness=0.42),
    "dark_alloy": dict(metallic=0.7, roughness=0.5),
    "frame": dict(metallic=0.6, roughness=0.35, coat=0.3),
    "chrome": dict(metallic=1.0, roughness=0.08),
    "rim": dict(metallic=1.0, roughness=0.12),
    "tire": dict(roughness=0.92),
    "rubber": dict(roughness=0.85),
    "black": dict(roughness=0.6),
    "lettering": dict(metallic=0.8, roughness=0.2),
    "lens": dict(roughness=0.05, transmission=0.6, emission=0.6),
    "amber": dict(roughness=0.1, transmission=0.5, emission=0.3),
    "tail_light": dict(roughness=0.1, transmission=0.5, emission=0.6),
    "glass": dict(metallic=1.0, roughness=0.02),
    "nozzle": dict(metallic=0.9, roughness=0.35),
    "nozzle_inner": dict(metallic=0.4, roughness=0.7),
}

UNLIT = ("lens", "amber", "tail_light")


def along(points, count):
    """`count` points spaced evenly along the polyline `points`."""
    lengths = [(b - a).length for a, b in zip(points, points[1:])]
    total = sum(lengths)
    out = []
    for i in range(count):
        d = total * i / (count - 1)
        for (a, b), n in zip(zip(points, points[1:]), lengths):
            if d <= n or b is points[-1]:
                out.append(a + (b - a) * (min(d, n) / n))
                break
            d -= n
    return out


def corrugated(points, radius, ribs, ref=RIGHT, depth=0.006, sides=8):
    """A ribbed tube -- a gaiter, or the header's heat shield -- through
    `points`, its rings alternating in and out."""
    pts = along(points, 2 * ribs + 1)
    return loft([(p, radius - (depth if i % 2 else 0.0),
                  radius - (depth if i % 2 else 0.0))
                 for i, p in enumerate(pts)], ref, sides=sides)


def arc(centre, radius, start, stop, count, y_offset=0.0):
    """Points on a circle about the x axis through `centre`, at angles
    from straight up, positive toward the back."""
    pts = []
    for i in range(count):
        a = math.radians(start + (stop - start) * i / (count - 1))
        pts.append(centre + UP * (radius * math.cos(a) + y_offset) -
                   FORWARD * (radius * math.sin(a)))
    return pts


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    coll = bpy.data.collections.new("bike")
    scene.collection.children.link(coll)
    m = {name: material(name, c, emit=name in UNLIT, **FINISH[name])
         for name, c in PALETTE.items()}

    wheel(coll, m)
    chassis(coll, m)
    steering(coll, m)
    suspension(coll, m)
    nozzles(coll, m)

    assembly("fork_top", coll, STEERING_HEAD - FORK * 0.1, point="fork_top")
    assembly("muffler_tip", coll, V((0.155, 0.14, -0.9)),
             point="muffler_tip")

    # Where the rider sits, puts their feet, and holds on.
    assembly("saddle", coll, V((0, 0.49, -0.24)), point="saddle")
    for s, tag in ((-1, "left"), (1, "right")):
        assembly(f"peg_{tag}", coll, V((0.215 * s, -0.035, 0.02)),
                 point=f"peg_{tag}")
        assembly(f"grip_{tag}", coll,
                 STEERING_HEAD + V((0.31 * s, 0.275, -0.255)),
                 point=f"grip_{tag}")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
    print(f"wrote {OUT}: {len(coll.objects)} objects")


def wheel(coll, m):
    """A 17-inch spoked wheel around the x axle: a chrome rim, 36 spokes
    laced to a big drum hub, and a 2.75 trail tyre with a block tread.
    The rear empty exports it; the front one only displays a copy."""
    rear = assembly("wheel", coll, REAR_AXLE, mesh="wheel",
                    point="rear_axle")
    o = V((0, 0, 0))
    section = 0.036
    carcass = RIM_RADIUS + section
    part(rear, "tyre", torus(o, RIGHT, UP, carcass - 0.004,
                             (section, 0.034), 40, 8), m["tire"])
    blocks = []
    count = 44
    for i in range(count):
        a = 2 * math.pi * i / count
        out = UP * math.cos(a) + FORWARD * math.sin(a)
        tangent = RIGHT.cross(out)
        for x in ((-0.019, 0.019) if i % 2 else (-0.026, 0.0, 0.026)):
            centre = out * (WHEEL_RADIUS - 0.009) + RIGHT * x
            blocks.append(slab(centre, (0.0085, 0.009, 0.011),
                               axes=(RIGHT, out, tangent)))
    part(rear, "tread", merge(*blocks), m["tire"], levels=0, bevel=0.003,
         smooth=False)
    part(rear, "rim", torus(o, RIGHT, UP, RIM_RADIUS,
                            (0.012, 0.03), 40, 6), m["rim"])
    # The drum: a broad alloy hub with its brake plate on one side.
    part(rear, "hub", loft([
        (V((-0.06, 0, 0)), 0.03, 0.03),
        (V((-0.05, 0, 0)), 0.068, 0.068),
        (V((0.035, 0, 0)), 0.068, 0.068),
        (V((0.045, 0, 0)), 0.05, 0.05),
        (V((0.06, 0, 0)), 0.03, 0.03),
    ], UP, sides=14), m["alloy"])
    part(rear, "brake_plate", loft([
        (V((-0.064, 0, 0)), 0.064, 0.064),
        (V((-0.07, 0, 0)), 0.05, 0.05),
    ], UP, sides=14), m["dark_alloy"], levels=0, smooth=False)
    spokes = []
    for i in range(36):
        a = 2 * math.pi * i / 36
        out = UP * math.cos(a) + FORWARD * math.sin(a)
        tangent = RIGHT.cross(out)
        side = 0.045 if i % 2 else -0.045
        lean = 1 if (i // 2) % 2 else -1
        spokes.append(tube([RIGHT * side + out * 0.055 +
                            tangent * (0.02 * lean),
                            out * (RIM_RADIUS - 0.008) + RIGHT * (side * 0.15)],
                           0.0022, sides=4))
    part(rear, "spokes", merge(*spokes), m["chrome"], levels=0)

    front = assembly("front_wheel", coll, FRONT_AXLE, point="front_axle")
    for child in rear.children:
        dup = child.copy()
        coll.objects.link(dup)
        dup.parent = front


def chassis(coll, m):
    c = assembly("chassis", coll, V((0, 0, 0)), mesh="chassis")
    head = STEERING_HEAD

    # The head tube, and the spine: one big tube sweeping down from the
    # head to the pressed body, under a silver shroud.
    part(c, "head_tube", tube([head + UP * 0.05 - FORK * 0.0,
                               head + FORK * 0.13], 0.03, sides=10),
         m["frame"])
    spine = [head + FORK * 0.06 + FORWARD * -0.02, V((0, 0.33, 0.28)),
             V((0, 0.2, 0.14)), V((0, 0.12, 0.02)), V((0, 0.14, -0.1))]
    part(c, "spine", tube(spine, 0.035, sides=8), m["frame"])
    part(c, "shroud", loft([
        (head + FORK * 0.08 + FORWARD * -0.035, 0.05, 0.05),
        (V((0, 0.34, 0.285)), 0.062, 0.085),
        (V((0, 0.22, 0.165)), 0.068, 0.095),
        (V((0, 0.14, 0.05)), 0.07, 0.09),
        (V((0, 0.13, -0.06)), 0.075, 0.075),
    ], RIGHT, sides=10), m["shroud"])
    # The name in raised chrome down the shroud's sides, a slim plate
    # each side standing in for the letters.
    for s in (-1, 1):
        mid = V((0.072 * s, 0.24, 0.18))
        run = (V((0, 0.34, 0.285)) - V((0, 0.14, 0.05))).normalized()
        part(c, f"badge{s}", slab(mid, (0.004, 0.018, 0.085),
                                  axes=(RIGHT, RIGHT.cross(run) * -1, run)),
             m["lettering"], levels=0, bevel=0.003)

    # The cradle: a slim silver tube each side from under the head round
    # the engine's front to the footpeg bar.
    for s in (-1, 1):
        part(c, f"cradle{s}", tube([
            head + FORK * 0.12 + RIGHT * (0.02 * s),
            V((0.05 * s, 0.12, 0.33)),
            V((0.075 * s, -0.06, 0.3)),
            V((0.085 * s, -0.12, 0.18)),
            V((0.085 * s, -0.115, 0.0)),
        ], 0.011, sides=6), m["chrome"], levels=0)

    # The pressed steel body: the tank's housing under the seat, swelling
    # back to the rear fender, which arches over the wheel to the tail.
    part(c, "body", loft([
        (V((0, 0.16, -0.06)), 0.07, 0.07),
        (V((0, 0.2, -0.15)), 0.115, 0.115),
        (V((0, 0.24, -0.3)), 0.135, 0.12),
        (V((0, 0.27, -0.46)), 0.13, 0.095),
        (V((0, 0.31, -0.6)), 0.12, 0.06),
        (V((0, 0.33, -0.66)), 0.1, 0.04),
    ], RIGHT, sides=12), m["orange"])
    fender = arc(REAR_AXLE, 0.33, -35, 118, 10)
    part(c, "rear_fender", loft([(p, 0.068, 0.01) for p in fender],
                                RIGHT, sides=8), m["orange"])
    for s in (-1, 1):
        part(c, f"fender_skirt{s}", loft([
            (V((0.072 * s, 0.24, -0.38)), 0.008, 0.06),
            (V((0.074 * s, 0.24, -0.6)), 0.008, 0.08),
            (V((0.072 * s, 0.17, -0.82)), 0.008, 0.06),
        ], UP, sides=6), m["orange"])

    # The grey box under the seat each side: the air cleaner and the
    # battery and tools.
    for s in (-1, 1):
        part(c, f"side_box{s}", slab(V((0.1 * s, 0.07, -0.25)),
                                     (0.05, 0.085, 0.085)),
             m["shroud"], bevel=0.015)

    # The seat: a long single saddle, pleated across, its edge piped.
    part(c, "seat", loft([
        (V((0, 0.4, -0.03)), 0.08, 0.035),
        (V((0, 0.445, -0.09)), 0.13, 0.045),
        (V((0, 0.45, -0.25)), 0.14, 0.048),
        (V((0, 0.445, -0.4)), 0.13, 0.045),
        (V((0, 0.42, -0.45)), 0.1, 0.03),
    ], RIGHT, sides=12), m["seat"])
    pleats = []
    for i in range(7):
        z = -0.1 - 0.045 * i
        pleats.append(loft([(V((-0.11, 0.467, z)), 0.005, 0.003),
                            (V((-0.06, 0.489, z)), 0.005, 0.003),
                            (V((0.0, 0.495, z)), 0.005, 0.003),
                            (V((0.06, 0.489, z)), 0.005, 0.003),
                            (V((0.11, 0.467, z)), 0.005, 0.003)],
                           FORWARD, sides=4))
    part(c, "pleats", merge(*pleats), m["seat"], levels=0)
    part(c, "seat_trim", torus(V((0, 0.415, -0.24)), UP, FORWARD, 0.13,
                               (0.006, 0.006), 24, 4), m["chrome"],
         levels=0)

    # The rack: a chrome tube frame over the fender, a pierced plate
    # across it, and legs down to the body and the fender's sides.
    rack_y = 0.36
    rail = []
    for s in (-1, 1):
        rail.append(tube([
            V((0.12 * s, rack_y - 0.03, -0.45)),
            V((0.13 * s, rack_y, -0.5)),
            V((0.13 * s, rack_y, -0.97)),
            V((0.1 * s, rack_y + 0.005, -1.0)),
        ], 0.01, sides=6))
        rail.append(tube([V((0.12 * s, rack_y, -0.98)),
                          V((0.1 * s, 0.2, -0.96))], 0.009, sides=6))
        rail.append(tube([V((0.125 * s, rack_y, -0.72)),
                          V((0.09 * s, 0.25, -0.68))], 0.009, sides=6))
    rail.append(tube([V((-0.1, rack_y + 0.005, -1.0)),
                      V((0.1, rack_y + 0.005, -1.0))], 0.01, sides=6))
    # A grab hoop standing at the rack's rear end.
    rail.append(tube([V((-0.1, rack_y, -1.0)), V((-0.09, rack_y + 0.07, -1.01)),
                      V((0.09, rack_y + 0.07, -1.01)), V((0.1, rack_y, -1.0))],
                     0.009, sides=6))
    part(c, "rack", merge(*rail), m["chrome"], levels=0)
    part(c, "rack_plate", slab(V((0, rack_y + 0.004, -0.74)),
                               (0.12, 0.004, 0.23)), m["chrome"],
         levels=0, bevel=0.004)
    holes = []
    for i in range(4):
        for j in (-1, 1):
            holes.append(slab(V((0.055 * j, rack_y + 0.009, -0.58 - 0.105 * i)),
                              (0.03, 0.002, 0.03)))
    part(c, "rack_holes", merge(*holes), m["black"], levels=0)

    # The tail light and the plate's bracket below it.
    part(c, "tail_light", slab(V((0, 0.24, -1.0)), (0.045, 0.028, 0.025)),
         m["tail_light"], levels=0, bevel=0.008)
    part(c, "tail_base", slab(V((0, 0.24, -0.975)), (0.05, 0.032, 0.012)),
         m["black"], levels=0, bevel=0.006)
    part(c, "plate_bracket", slab(V((0, 0.13, -0.98)), (0.07, 0.06, 0.004),
                                  axes=(RIGHT, (UP + FORWARD * 0.2)
                                        .normalized(),
                                        (FORWARD - UP * 0.2).normalized())),
         m["black"], levels=0, bevel=0.004)

    # The engine lies flat under the spine: an alloy crankcase with its
    # covers, and a finned cylinder pointing forward.
    part(c, "crankcase", slab(V((0, -0.02, -0.04)), (0.1, 0.085, 0.13)),
         m["alloy"], bevel=0.035)
    part(c, "clutch_cover", loft([
        (V((0.1, -0.02, 0.0)), 0.075, 0.075),
        (V((0.135, -0.02, 0.0)), 0.065, 0.065),
    ], UP, sides=14), m["alloy"])
    part(c, "flywheel_cover", loft([
        (V((-0.1, -0.03, -0.05)), 0.07, 0.07),
        (V((-0.13, -0.03, -0.05)), 0.06, 0.06),
    ], UP, sides=14), m["alloy"])
    cylinder = V((0, 0.0, 0.1))
    part(c, "cylinder", loft([
        (cylinder, 0.055, 0.055),
        (cylinder + FORWARD * 0.16, 0.05, 0.05),
    ], RIGHT, sides=10), m["alloy"])
    fins = []
    for i in range(6):
        z = 0.115 + 0.025 * i
        fins.append(loft([(V((0, 0.0, z)), 0.075, 0.072),
                          (V((0, 0.0, z + 0.006)), 0.075, 0.072)],
                         RIGHT, sides=10))
    part(c, "fins", merge(*fins), m["alloy"], levels=0)
    part(c, "cam_cover", blob(V((0, 0.0, 0.27)), (0.05, 0.05, 0.03), 10, 6),
         m["alloy"])
    part(c, "skid_plate", slab(V((0, -0.115, 0.06)), (0.075, 0.006, 0.17),
                               axes=(RIGHT, (UP + FORWARD * 0.12)
                                     .normalized(),
                                     (FORWARD - UP * 0.12).normalized())),
         m["frame"], levels=0, bevel=0.004)

    # The exhaust: the header out of the cylinder's underside, up the
    # right side in a corrugated chrome shield, and the silencer running
    # back at seat height under a pierced heat guard.
    part(c, "header", corrugated([
        V((0.0, -0.05, 0.25)), V((0.07, -0.08, 0.27)),
        V((0.14, -0.03, 0.2)), V((0.155, 0.08, 0.08)),
        V((0.155, 0.14, -0.04)), V((0.155, 0.14, -0.1)),
    ], 0.028, 14, ref=RIGHT), m["chrome"], levels=0)
    part(c, "silencer", loft([
        (V((0.155, 0.14, -0.09)), 0.036, 0.036),
        (V((0.155, 0.14, -0.2)), 0.042, 0.042),
        (V((0.155, 0.14, -0.84)), 0.042, 0.042),
        (V((0.155, 0.14, -0.9)), 0.025, 0.025),
    ], UP, sides=12), m["chrome"])
    part(c, "heat_guard", loft([
        (V((0.192, 0.14, -0.13)), 0.01, 0.045),
        (V((0.196, 0.14, -0.5)), 0.01, 0.05),
        (V((0.192, 0.14, -0.84)), 0.01, 0.045),
    ], UP, sides=6), m["chrome"])
    slots = []
    for i in range(26):
        slots.append(slab(V((0.205, 0.14, -0.16 - 0.026 * i)),
                          (0.002, 0.034, 0.006)))
    part(c, "guard_slots", merge(*slots), m["black"], levels=0)

    # The footpeg bar across under the engine, with rubbers; the kick
    # lever on the right and the heel-and-toe shifter on the left.
    part(c, "peg_bar", tube([V((-0.24, -0.05, 0.02)),
                             V((0.24, -0.05, 0.02))], 0.011, sides=6),
         m["frame"], levels=0)
    for s in (-1, 1):
        part(c, f"peg_rubber{s}", loft([
            (V((0.17 * s, -0.05, 0.02)), 0.02, 0.02),
            (V((0.25 * s, -0.05, 0.02)), 0.021, 0.021),
        ], UP, sides=8), m["rubber"])
    part(c, "kick_lever", tube([V((0.14, -0.0, -0.12)), V((0.17, 0.02, -0.2)),
                                V((0.19, -0.02, -0.33))], 0.009, sides=5),
         m["chrome"], levels=0)
    part(c, "shifter", tube([V((-0.13, -0.04, -0.06)), V((-0.17, -0.07, 0.03)),
                             V((-0.2, -0.065, 0.1))], 0.008, sides=5),
         m["chrome"], levels=0)


def steering(coll, m):
    """What turns with the bars about the steering head: the clamps, the
    fork's orange covers and rubber gaiters, the headlamp and the
    speedometer, and the braced high bars with their mirrors."""
    st = assembly("steering", coll, STEERING_HEAD, mesh="steering",
                  point="steering_head")
    o = V((0, 0, 0))
    up = -FORK
    for s in (-1, 1):
        x = RIGHT * (0.075 * s)
        part(st, f"cover{s}", loft([
            (x + up * 0.09, 0.03, 0.03),
            (x + FORK * 0.06, 0.036, 0.036),
            (x + FORK * 0.16, 0.034, 0.034),
        ], RIGHT, sides=10), m["orange"])
        part(st, f"gaiter{s}", corrugated(
            [x + FORK * 0.16, x + FORK * 0.33], 0.029, 7, ref=RIGHT,
            depth=0.005), m["rubber"], levels=0)
        # Amber reflectors on the covers' fronts.
        part(st, f"reflector{s}", slab(x + FORK * 0.07 + RIGHT * (0.03 * s)
                                       + FORWARD * 0.012,
                                       (0.004, 0.015, 0.012)),
             m["amber"], levels=0, bevel=0.003)
    part(st, "top_clamp", slab(o + up * 0.075, (0.11, 0.012, 0.035)),
         m["frame"], bevel=0.008)
    part(st, "bottom_clamp", slab(o + FORK * 0.135, (0.11, 0.014, 0.04)),
         m["frame"], bevel=0.008)

    # The headlamp in an orange shell ahead of the fork, a chrome rim,
    # and the round speedometer on top looking back at the rider.
    lamp = up * 0.03 + FORWARD * 0.1
    part(st, "lamp_shell", blob(lamp, (0.082, 0.08, 0.075), 14, 8),
         m["orange"])
    part(st, "lamp_rim", torus(lamp + FORWARD * 0.06, FORWARD, UP, 0.066,
                               (0.008, 0.009), 24, 6), m["chrome"],
         levels=0)
    part(st, "lens", blob(lamp + FORWARD * 0.063, (0.062, 0.062, 0.018),
                          14, 6), m["lens"])
    dial = up * 0.1 + FORWARD * 0.055
    face = (UP - FORWARD * 0.5).normalized()
    part(st, "speedo", loft([
        (dial - face * 0.02, 0.038, 0.038),
        (dial + face * 0.02, 0.042, 0.042),
    ], RIGHT, sides=14), m["black"])
    part(st, "speedo_face", loft([
        (dial + face * 0.021, 0.034, 0.034),
        (dial + face * 0.023, 0.034, 0.034),
    ], RIGHT, sides=14), m["glass"], levels=0)

    # The bars: up out of the clamp, splaying, then back to the grips,
    # with a cross brace, black grips, levers, and long-stalked mirrors.
    bar = []
    for s in (-1, 1):
        bar.append(tube([
            V((0.035 * s, 0.085, -0.01)), V((0.07 * s, 0.14, -0.04)),
            V((0.15 * s, 0.24, -0.13)), V((0.23 * s, 0.27, -0.215)),
            V((0.34 * s, 0.278, -0.265)),
        ], 0.011, sides=8))
    bar.append(tube([V((-0.035, 0.085, -0.01)), V((0.035, 0.085, -0.01))],
                    0.012, sides=8))
    bar.append(tube([V((-0.135, 0.225, -0.115)), V((0.135, 0.225, -0.115))],
                    0.008, sides=6))
    part(st, "bars", merge(*bar), m["chrome"], levels=0)
    for s in (-1, 1):
        part(st, f"grip{s}", loft([
            (V((0.255 * s, 0.273, -0.228)), 0.016, 0.016),
            (V((0.345 * s, 0.28, -0.27)), 0.017, 0.017),
        ], UP, sides=8), m["rubber"])
        part(st, f"lever{s}", tube([
            V((0.21 * s, 0.27, -0.2)), V((0.27 * s, 0.262, -0.17)),
            V((0.32 * s, 0.262, -0.19)),
        ], 0.005, sides=4), m["chrome"], levels=0)
        stalk = [V((0.2 * s, 0.265, -0.19)), V((0.215 * s, 0.42, -0.2)),
                 V((0.24 * s, 0.5, -0.17))]
        part(st, f"mirror_stalk{s}", tube(stalk, 0.005, sides=4),
             m["chrome"], levels=0)
        glass = V((0.25 * s, 0.53, -0.16))
        part(st, f"mirror{s}", loft([
            (glass + FORWARD * 0.008, 0.042, 0.042),
            (glass - FORWARD * 0.006, 0.045, 0.045),
        ], RIGHT, sides=12), m["chrome"])
        part(st, f"mirror_glass{s}", loft([
            (glass - FORWARD * 0.0075, 0.038, 0.038),
            (glass - FORWARD * 0.009, 0.038, 0.038),
        ], RIGHT, sides=12), m["glass"], levels=0)
    part(st, "cables", tube([V((-0.2, 0.26, -0.19)), V((-0.12, 0.16, -0.12)),
                             V((-0.06, 0.02, -0.06)), V((-0.04, -0.1, -0.06))],
                            0.0045, sides=4), m["black"], levels=0)
    assembly("headlight", coll, STEERING_HEAD + lamp + FORWARD * 0.07,
             point="headlight")


def suspension(coll, m):
    # Fork sliders: chrome lowers rising into the gaiters, with the axle
    # lugs, the drum's brake arm, and the orange front fender on its
    # chrome stays, which rides up and down with the wheel.
    sl = assembly("fork_sliders", coll, FRONT_AXLE, mesh="fork_sliders")
    for s in (-1, 1):
        x = RIGHT * (0.075 * s)
        part(sl, f"slider{s}", tube([x - FORK * 0.02, x - FORK * 0.21],
                                    0.024, sides=10), m["chrome"])
        part(sl, f"lug{s}", slab(x, (0.016, 0.025, 0.028)), m["alloy"],
             bevel=0.008)
    fender = arc(V((0, 0, 0)), 0.33, -75, 60, 9)
    part(sl, "front_fender", loft([(p, 0.06, 0.009) for p in fender],
                                  RIGHT, sides=8), m["orange"])
    stays = []
    for s in (-1, 1):
        for a in (-55, 40):
            p = arc(V((0, 0, 0)), 0.33, a, a, 2)[0]
            stays.append(tube([RIGHT * (0.075 * s) + p * 0.08,
                               p + RIGHT * (0.062 * s)], 0.0055, sides=5))
    part(sl, "fender_stays", merge(*stays), m["chrome"], levels=0)

    # Swingarm: two orange pressed arms from the pivot to the axle, joined
    # ahead of the wheel, and the chain's guard along the left one.
    sa = assembly("swingarm", coll, SWINGARM_PIVOT, mesh="swingarm",
                  point="swingarm_pivot")
    axle = REAR_AXLE - SWINGARM_PIVOT
    for s in (-1, 1):
        x = RIGHT * (0.095 * s)
        part(sa, f"arm{s}", loft([
            (x + V((0, 0, 0.02)), 0.014, 0.035),
            (x + axle * 0.5, 0.014, 0.03),
            (x + axle * 1.06, 0.012, 0.022),
        ], RIGHT, sides=8), m["orange"])
    part(sa, "brace", tube([V((-0.095, 0, 0)) + axle * 0.15,
                            V((0.095, 0, 0)) + axle * 0.15], 0.018),
         m["orange"])
    part(sa, "pivot_bolt", tube([V((-0.12, 0, 0)), V((0.12, 0, 0))], 0.016,
                                sides=8), m["dark_alloy"])
    part(sa, "chain_guard", loft([
        (V((-0.118, 0.035, 0.02)), 0.008, 0.03),
        (V((-0.118, 0.04, 0.0)) + axle * 0.5, 0.008, 0.038),
        (V((-0.118, 0.035, 0.0)) + axle * 0.95, 0.008, 0.03),
    ], UP, sides=6), m["black"])
    part(sa, "chain", loft([
        (V((-0.1, -0.03, 0.04)), 0.004, 0.006),
        (V((-0.1, -0.035, 0.0)) + axle * 0.95, 0.004, 0.006),
    ], UP, sides=4), m["dark_alloy"], levels=0)
    for s in (-1, 1):
        part(sa, f"shock_mount{s}", slab(SHOCK_BOTTOM - SWINGARM_PIVOT +
                                         V((SHOCK_X * s, 0, 0)),
                                         (0.012, 0.018, 0.018)),
             m["orange"], bevel=0.005)

    # Twin shocks: a chrome-shrouded body with its spring hung from under
    # the rack, and a shaft rising from the swingarm into each.
    sb = assembly("shock_body", coll, SHOCK_TOP, mesh="shock_body",
                  point="shock_top")
    down = (SHOCK_BOTTOM - SHOCK_TOP).normalized()
    side = down.cross(RIGHT)
    for s in (-1, 1):
        x = RIGHT * (SHOCK_X * s)
        part(sb, f"top_eye{s}", tube([x - RIGHT * 0.012, x + RIGHT * 0.012],
                                     0.013, sides=8), m["black"], levels=0)
        part(sb, f"shroud{s}", tube([x + down * 0.01, x + down * 0.12],
                                    0.027, sides=12), m["chrome"])
        pts = []
        turns, steps = 6, 6 * 10
        for i in range(steps + 1):
            t = i / steps
            a = 2 * math.pi * turns * t
            pts.append(x + down * (0.12 + 0.12 * t) +
                       (RIGHT * math.cos(a) + side * math.sin(a)) * 0.024)
        part(sb, f"spring{s}", tube(pts, 0.0045, ref=RIGHT, sides=5),
             m["chrome"], levels=0)
    ss = assembly("shock_shaft", coll, SHOCK_BOTTOM, mesh="shock_shaft",
                  point="shock_bottom")
    for s in (-1, 1):
        x = RIGHT * (SHOCK_X * s)
        part(ss, f"shaft{s}", tube([x, x - down * 0.16], 0.009, sides=8),
             m["chrome"])
        part(ss, f"eye{s}", tube([x - RIGHT * 0.012, x + RIGHT * 0.012],
                                 0.013, sides=8), m["black"], levels=0)


def nozzles(coll, m):
    """One jump-jet nozzle, a bell along +z; the game turns it about x."""
    for s, tag in ((-1, "left"), (1, "right")):
        n = assembly(f"nozzle_{tag}", coll, V((0.16 * s, -0.04, -0.18)),
                     mesh="nozzle" if s > 0 else None,
                     point=f"nozzle_{tag}")
        n.rotation_euler = (math.radians(90), 0, 0)
        part(n, f"bell{s}", loft([
            (V((0, 0, -0.01)), 0.02, 0.02),
            (V((0, 0, 0.03)), 0.024, 0.024),
            (V((0, 0, 0.09)), 0.04, 0.04),
        ], UP, sides=12, close=(True, False)), m["nozzle"])
        part(n, f"throat{s}", loft([
            (V((0, 0, 0.085)), 0.034, 0.034),
            (V((0, 0, 0.05)), 0.018, 0.018),
        ], UP, sides=12), m["nozzle_inner"], levels=0)


build()
