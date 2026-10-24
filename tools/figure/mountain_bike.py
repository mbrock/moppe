"""Builds models/mountain_bike.blend: a classic hardtail mountainbike.

    Blender -b --factory-startup --python tools/figure/mountain_bike.py

An early-nineties hardtail at life size: a steel frame in deep green
with cream panels and lugs, a short suspension fork, cantilever brakes, a
triple chainset, flat bars with bar ends, toe clips, and gumwall tyres.
It is authored in the game's frame (x right, y up, z forward) with its
origin midway between the axles at their height, as mov::RigidBike's
mountain_bike_rig has it.

Each assembly is an empty -- frame, steering, fork lowers, wheel,
cassette, crank, pedal -- whose children are its parts, exported in the
empty's own frame by tools/figure/export_model.py; the empties marked as
points (axles, steering axis, bottom bracket, saddle, grips, crank end)
are model-space locations the game places parts and the rider by. The
steering turns about the head tube's own axis, the fork lowers slide up
it, and the crank turns about the bottom bracket while the pedals stay
level. Rebuilding discards hand edits to the .blend.
"""

import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(__file__))
from shapes import (V, RIGHT, UP, FORWARD, assembly, blob,  # noqa
                    loft, material, merge, part, slab, torus, tube)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "models", "mountain_bike.blend")

WHEEL_RADIUS = 0.335
REAR_AXLE = V((0, 0, -0.53))
FRONT_AXLE = V((0, 0, 0.53))
BB = V((0, -0.035, -0.11))
SEAT_DIR = V((0, math.sin(math.radians(73)), -math.cos(math.radians(73))))
SEAT_TOP = BB + SEAT_DIR * 0.46
SADDLE = BB + SEAT_DIR * 0.69
# The steering axis at 71 degrees, through a point 4 cm behind the axle.
AXIS = V((0, math.sin(math.radians(71)), -math.cos(math.radians(71))))
AXIS_FORWARD = V((0, math.cos(math.radians(71)), math.sin(math.radians(71))))
CROWN = FRONT_AXLE - AXIS_FORWARD * 0.04 + AXIS * 0.42
HEAD_BOTTOM = CROWN + AXIS * 0.012
HEAD_TOP = CROWN + AXIS * 0.135
STEER_TOP = CROWN + AXIS * 0.2
STEM_END = STEER_TOP + V((0, 0.025, 0.115))
CRANK = 0.175
# The pedal's body stands this far outboard of the crank's end.
PEDAL_OUT = 0.055

PALETTE = {
    "paint": (0.06, 0.30, 0.22),
    "cream": (0.88, 0.84, 0.70),
    "black": (0.06, 0.06, 0.07),
    "silver": (0.70, 0.71, 0.73),
    "alloy": (0.58, 0.60, 0.63),
    "dark_alloy": (0.24, 0.25, 0.27),
    "tread": (0.07, 0.07, 0.07),
    "gumwall": (0.62, 0.45, 0.25),
    "saddle": (0.09, 0.08, 0.08),
    "grip": (0.10, 0.10, 0.11),
    "chain": (0.33, 0.33, 0.34),
    "red": (0.72, 0.10, 0.08),
    "bottle": (0.86, 0.88, 0.90),
    "reflector": (1.0, 0.55, 0.12),
}

# How each paint finishes: a deep metallic enamel under clear coat,
# polished and bead-blasted alloys, rubber and gum, a vinyl saddle, a
# milky bottle, and the pedals' amber reflectors.
FINISH = {
    "paint": dict(metallic=0.45, roughness=0.3, coat=0.8),
    "cream": dict(roughness=0.35, coat=0.8),
    "black": dict(roughness=0.45, coat=0.3),
    "silver": dict(metallic=1.0, roughness=0.14),
    "alloy": dict(metallic=0.9, roughness=0.32),
    "dark_alloy": dict(metallic=0.7, roughness=0.4),
    "tread": dict(roughness=0.9),
    "gumwall": dict(roughness=0.75),
    "saddle": dict(roughness=0.55, sheen=0.2),
    "grip": dict(roughness=0.85),
    "chain": dict(metallic=0.9, roughness=0.45),
    "red": dict(roughness=0.4, coat=0.5),
    "bottle": dict(roughness=0.4, transmission=0.3),
    "reflector": dict(roughness=0.1, transmission=0.5),
}


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    coll = bpy.data.collections.new("mountain_bike")
    scene.collection.children.link(coll)
    m = {name: material(name, c, **FINISH[name])
         for name, c in PALETTE.items()}

    wheel(coll, m)
    frame(coll, m)
    steering(coll, m)
    lowers(coll, m)
    drivetrain(coll, m)

    assembly("saddle", coll, SADDLE + V((0, 0.045, -0.02)), point="saddle")
    assembly("bottom_bracket", coll, BB, point="bottom_bracket")
    assembly("steering_axis", coll, CROWN, point="steering_head")
    assembly("steering_top", coll, STEER_TOP, point="steering_top")
    for s, tag in ((-1, "left"), (1, "right")):
        assembly(f"grip_{tag}", coll, STEM_END + V((0.245 * s, 0.0, -0.03)),
                 point=f"grip_{tag}")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
    print(f"wrote {OUT}: {len(coll.objects)} objects")


def wheel(coll, m):
    """A 26-inch wheel round the x axle: a knobbly black tread on tan
    gumwalls, a polished rim, 32 crossed spokes, and a hub. The rear empty
    exports it; the front one shows a copy."""
    rear = assembly("wheel", coll, REAR_AXLE, mesh="wheel",
                    point="rear_axle")
    o = V((0, 0, 0))
    part(rear, "sidewall", torus(o, RIGHT, UP, 0.301, (0.02, 0.024), 36, 8),
         m["gumwall"])
    part(rear, "tread", torus(o, RIGHT, UP, 0.316, (0.012, 0.025), 36, 6),
         m["tread"])
    knobs = []
    for i in range(60):
        a = 2 * math.pi * (i + 0.5 * (i % 2)) / 60
        out = UP * math.cos(a) + FORWARD * math.sin(a)
        along = RIGHT.cross(out)
        for x in ((-0.019, 0.019) if i % 2 else (-0.006, 0.006)):
            centre = out * (WHEEL_RADIUS - 0.007) + RIGHT * x
            knobs.append(slab(centre, (0.006, 0.006, 0.005),
                              axes=(RIGHT, out, along)))
    part(rear, "knobs", merge(*knobs), m["tread"], levels=0, smooth=False)
    part(rear, "rim", torus(o, RIGHT, UP, 0.285, (0.012, 0.012), 36, 6),
         m["silver"])
    part(rear, "hub", loft([
        (V((-0.05, 0, 0)), 0.01, 0.01),
        (V((-0.04, 0, 0)), 0.026, 0.026),
        (V((-0.032, 0, 0)), 0.016, 0.016),
        (V((0.032, 0, 0)), 0.016, 0.016),
        (V((0.04, 0, 0)), 0.026, 0.026),
        (V((0.05, 0, 0)), 0.01, 0.01),
    ], UP, sides=10), m["silver"])
    spokes = []
    for i in range(32):
        side = 0.038 if i % 2 else -0.038
        a = 2 * math.pi * i / 32
        # Each spoke leaves the flange a few holes round from where it
        # meets the rim, crossing its neighbours.
        h = a + (0.55 if (i // 2) % 2 else -0.55)
        flange = (UP * math.cos(h) + FORWARD * math.sin(h)) * 0.024
        rim = (UP * math.cos(a) + FORWARD * math.sin(a)) * 0.276
        spokes.append(tube([RIGHT * side + flange, rim + RIGHT * side * 0.15],
                           0.0012, sides=3))
    part(rear, "spokes", merge(*spokes), m["silver"], levels=0)

    front = assembly("front_wheel", coll, FRONT_AXLE, point="front_axle")
    for child in rear.children:
        dup = child.copy()
        coll.objects.link(dup)
        dup.parent = front


def frame(coll, m):
    f = assembly("frame", coll, V((0, 0, 0)), mesh="frame")
    t = 0.0165

    # The main triangle in round steel tubes, the lugs and panels in
    # cream: a band near each end of the down tube and the seat tube.
    down_top = HEAD_BOTTOM + AXIS * 0.02
    part(f, "down_tube", tube([down_top, BB + V((0, 0.02, 0.02))], 0.019,
                              sides=10), m["paint"])
    top_front = HEAD_TOP - AXIS * 0.02
    top_back = SEAT_TOP - SEAT_DIR * 0.025
    part(f, "top_tube", tube([top_front, top_back], t, sides=10),
         m["paint"])
    part(f, "seat_tube", tube([BB, SEAT_TOP + SEAT_DIR * 0.02], 0.0165,
                              sides=10), m["paint"])
    part(f, "head_tube", tube([HEAD_BOTTOM, HEAD_TOP], 0.022, sides=12),
         m["paint"])
    for a, b, r, length in ((down_top, BB, 0.0195, 0.34),
                            (BB, SEAT_TOP, 0.017, 0.28)):
        d = (b - a).normalized()
        mid = a + d * length
        part(f, f"panel{length}", tube([mid, mid + d * 0.11], r + 0.0008,
                                       sides=10), m["cream"], levels=0)
    for name, at, along, r in (
            ("head_lug_top", HEAD_TOP, AXIS, 0.025),
            ("head_lug_bottom", HEAD_BOTTOM, AXIS, 0.025),
            ("seat_lug", SEAT_TOP, SEAT_DIR, 0.02)):
        part(f, name, tube([at - along * 0.022, at + along * 0.012], r,
                           sides=12), m["cream"])
    part(f, "bb_shell", tube([BB - RIGHT * 0.036, BB + RIGHT * 0.036],
                             0.02, sides=12), m["cream"])

    # The rear triangle: stays to the dropouts either side of the wheel,
    # with a bridge between the seat stays carrying the brake's straddle
    # cable stop.
    for s in (-1, 1):
        drop = REAR_AXLE + RIGHT * (0.066 * s)
        part(f, f"chainstay{s}", tube([
            BB + RIGHT * (0.03 * s) + V((0, 0.0, -0.01)),
            BB + RIGHT * (0.055 * s) + V((0, 0.01, -0.12)),
            drop + V((0, 0.002, 0.02)),
        ], 0.0105, sides=8), m["paint"])
        part(f, f"seatstay{s}", tube([
            SEAT_TOP + RIGHT * (0.02 * s) - SEAT_DIR * 0.03,
            drop + V((0, 0.03, 0.01)),
        ], 0.0085, sides=8), m["paint"])
        part(f, f"dropout{s}", slab(drop + V((0, 0.012, 0.015)),
                                    (0.004, 0.02, 0.022)), m["dark_alloy"],
             levels=0, bevel=0.003)
    stay = SEAT_TOP + (REAR_AXLE - SEAT_TOP) * 0.32
    part(f, "bridge", tube([stay - RIGHT * 0.04, stay + RIGHT * 0.04],
                           0.007, sides=6), m["paint"], levels=0)

    # The seat post, clamp, and a padded saddle sloping just nose up.
    part(f, "seat_post", tube([SEAT_TOP, SADDLE - SEAT_DIR * 0.01], 0.0135,
                              sides=10), m["silver"])
    part(f, "seat_clamp", tube([SEAT_TOP + SEAT_DIR * 0.005,
                                SEAT_TOP + SEAT_DIR * 0.025], 0.019,
                               sides=10), m["black"], levels=0)
    s0 = SADDLE + V((0, 0.03, 0))
    part(f, "saddle_shell", loft([
        (s0 + V((0, 0.006, 0.15)), 0.018, 0.012),
        (s0 + V((0, 0.006, 0.10)), 0.03, 0.017),
        (s0 + V((0, 0.0, 0.0)), 0.06, 0.022),
        (s0 + V((0, -0.002, -0.08)), 0.083, 0.025),
        (s0 + V((0, 0.004, -0.115)), 0.07, 0.02),
    ], RIGHT, sides=10), m["saddle"])
    part(f, "saddle_rails", merge(*[
        tube([s0 + V((0.022 * s, -0.022, 0.09)),
              s0 + V((0.03 * s, -0.035, 0.0)),
              s0 + V((0.035 * s, -0.022, -0.09))], 0.0035, sides=4)
        for s in (-1, 1)]), m["silver"], levels=0)

    # A bottle in its cage on the down tube.
    d = (BB - down_top).normalized()
    side = RIGHT.cross(d).normalized()
    if side.y < 0:
        side = -side
    base = down_top + d * 0.2 + side * 0.045
    part(f, "bottle", loft([
        (base - d * 0.005, 0.018, 0.018),
        (base + d * 0.01, 0.035, 0.035),
        (base + d * 0.17, 0.035, 0.035),
        (base + d * 0.2, 0.026, 0.026),
        (base + d * 0.215, 0.012, 0.012),
        (base + d * 0.23, 0.009, 0.009),
    ], RIGHT, sides=10), m["bottle"])
    part(f, "bottle_cage", merge(*[
        tube([down_top + d * (0.21 + 0.13 * k) + side * 0.02,
              base + d * (0.03 + 0.13 * k) + side * 0.036], 0.0025,
             sides=4) for k in (0, 1)]), m["silver"], levels=0)

    # Rear cantilevers on the seat stays, linked by their straddle wire.
    canti = []
    for s in (-1, 1):
        boss = stay + V((0.04 * s, -0.035, -0.015))
        canti.append(tube([boss, boss + V((0.03 * s, 0.03, -0.01))], 0.005,
                          sides=4))
        canti.append(slab(boss + V((0, -0.005, 0.0)), (0.004, 0.016, 0.006)))
    canti.append(tube([stay + V((-0.07, -0.005, -0.025)),
                       stay + V((0, 0.03, -0.015)),
                       stay + V((0.07, -0.005, -0.025))], 0.0012, sides=3))
    part(f, "rear_brake", merge(*canti), m["silver"], levels=0)

    # Derailleurs and the chain: a front mech on the seat tube, a rear
    # mech hanging below the right dropout, and the chain's loop between
    # the middle ring and the cassette.
    fd = BB + SEAT_DIR * 0.13 + RIGHT * 0.03 + V((0, 0, 0.015))
    part(f, "front_mech", slab(fd, (0.012, 0.022, 0.03)), m["silver"],
         levels=0, bevel=0.003)
    rd = REAR_AXLE + RIGHT * 0.07 + V((0, -0.06, 0.0))
    part(f, "rear_mech", merge(
        slab(rd + V((0, 0.03, 0.0)), (0.008, 0.03, 0.012)),
        slab(rd + V((0.002, -0.025, 0.01)), (0.006, 0.04, 0.014)),
    ), m["silver"], levels=0, bevel=0.003)
    for k, (dy, dz) in enumerate(((0.012, 0.0), (-0.065, 0.02))):
        part(f, f"jockey{k}", tube([rd + V((-0.006, dy, dz)),
                                    rd + V((0.012, dy, dz))], 0.012,
                                   sides=10), m["black"], levels=0)
    ring = 0.083
    cog = 0.04
    x = 0.047
    top = [BB + V((x, ring, 0.0)), REAR_AXLE + V((x, cog, 0.0))]
    bottom = [BB + V((x, -ring, 0.0)), rd + V((x - 0.04, -0.065, 0.02)),
              rd + V((x - 0.04, 0.012, 0.0)),
              REAR_AXLE + V((x, -cog, 0.0))]
    part(f, "chain", merge(
        loft([(p, 0.0035, 0.0045) for p in top], RIGHT, sides=4),
        loft([(p, 0.0035, 0.0045) for p in bottom], RIGHT, sides=4),
    ), m["chain"], levels=0)
    # The chain wrapping the back of the cassette's cog.
    arc = [REAR_AXLE + V((x, cog * math.cos(a), -cog * math.sin(a)))
           for a in [math.pi * i / 6 for i in range(7)]]
    part(f, "chain_wrap", loft([(p, 0.0035, 0.0045) for p in arc], RIGHT,
                               sides=4), m["chain"], levels=0)

    # Cables: from the levers' housings round the head tube along the top
    # tube to the brakes and mechs.
    cables = [tube([HEAD_TOP + V((0.03 * s, 0.0, 0.02)),
                    top_front + V((0.0, 0.03, -0.04)) + RIGHT * (0.01 * s),
                    top_back + V((0, 0.02, 0.0)) + RIGHT * (0.01 * s),
                    stay + V((0, 0.04, 0.0))], 0.0022, sides=4)
              for s in (-1, 1)]
    part(f, "cables", merge(*cables), m["black"], levels=0)


def steering(coll, m):
    """The fork crown and stanchions, steerer, stem, and bars, turning
    together about the head tube's axis."""
    st = assembly("steering", coll, CROWN, mesh="steering")
    up = AXIS
    fwd = AXIS_FORWARD
    # A black crown carrying silver stanchions either side of the tyre.
    part(st, "crown", slab(V((0, 0, 0)), (0.06, 0.016, 0.026),
                           axes=(RIGHT, up, fwd)), m["black"], bevel=0.008)
    for s in (-1, 1):
        x = RIGHT * (0.052 * s)
        part(st, f"stanchion{s}", tube([x - up * 0.01, x - up * 0.16],
                                       0.0135, sides=10), m["silver"])
    part(st, "steerer", tube([V((0, 0, 0)), STEER_TOP - CROWN], 0.0145,
                             sides=10), m["dark_alloy"])
    part(st, "headset", tube([STEER_TOP - CROWN - up * 0.065,
                              STEER_TOP - CROWN - up * 0.045], 0.024,
                             sides=12), m["silver"], levels=0)

    # A long, low stem, flat bars swept back a little with bar ends,
    # grips, levers, and thumb shifters.
    stem_start = STEER_TOP - CROWN - up * 0.02
    stem_end = STEM_END - CROWN
    part(st, "stem", merge(
        tube([stem_start, stem_end], 0.013, sides=8),
        tube([stem_start - up * 0.03, stem_start + up * 0.012], 0.017,
             sides=10),
        tube([stem_end - RIGHT * 0.022, stem_end + RIGHT * 0.022], 0.017,
             sides=10),
    ), m["alloy"])
    bar = [stem_end + V((s * x, 0, -0.03 * (x / 0.28) ** 2))
           for s, x in ((-1, 0.28), (-1, 0.14), (1, 0.0), (1, 0.14),
                        (1, 0.28))]
    part(st, "bar", tube(bar, 0.011, ref=UP, sides=8), m["alloy"])
    for s in (-1, 1):
        end = stem_end + V((0.28 * s, 0, -0.03))
        part(st, f"grip{s}", tube([end - RIGHT * (0.12 * s),
                                   end + RIGHT * (0.004 * s)], 0.016,
                                  sides=8), m["grip"])
        part(st, f"bar_end{s}", tube([end + RIGHT * (0.006 * s),
                                      end + V((0.008 * s, 0.05, 0.11))],
                                     0.012, sides=8), m["dark_alloy"])
        lever = end - RIGHT * (0.14 * s)
        part(st, f"lever{s}", merge(
            slab(lever, (0.016, 0.014, 0.018)),
            tube([lever + V((0.005 * s, -0.004, 0.02)),
                  lever + V((0.07 * s, -0.012, 0.045)),
                  lever + V((0.1 * s, -0.016, 0.04))], 0.004, sides=4),
        ), m["black"], levels=0)
        part(st, f"shifter{s}", slab(lever + V((0.025 * s, 0.018, -0.005)),
                                     (0.012, 0.007, 0.016)), m["dark_alloy"],
             levels=0, bevel=0.003)


def lowers(coll, m):
    """The fork's lowers, sliding up the stanchions, with the brake arch
    and cantilevers, from the axle."""
    lw = assembly("fork_lowers", coll, FRONT_AXLE, mesh="fork_lowers")
    up = AXIS
    back = -AXIS_FORWARD * 0.04
    for s in (-1, 1):
        x = RIGHT * (0.052 * s) + back
        part(lw, f"slider{s}", loft([
            (x + up * 0.0, 0.017, 0.017),
            (x + up * 0.03, 0.019, 0.019),
            (x + up * 0.24, 0.019, 0.019),
            (x + up * 0.255, 0.0165, 0.0165),
        ], RIGHT, sides=10), m["black"])
        part(lw, f"fork_drop{s}", slab(RIGHT * (0.052 * s) + back * 0.5,
                                       (0.006, 0.014, 0.03),
                                       axes=(RIGHT, up, AXIS_FORWARD)),
             m["dark_alloy"], levels=0, bevel=0.003)
    arch = back + up * 0.2
    part(lw, "arch", tube([arch + V((-0.052, 0, 0)),
                           arch + V((-0.03, 0.035, 0.005)),
                           arch + V((0.03, 0.035, 0.005)),
                           arch + V((0.052, 0, 0))], 0.01, ref=UP, sides=8),
         m["black"])
    canti = []
    for s in (-1, 1):
        boss = RIGHT * (0.06 * s) + back + up * 0.29 + AXIS_FORWARD * 0.015
        canti.append(tube([boss, boss + RIGHT * (0.03 * s) + up * 0.035],
                          0.005, sides=4))
        canti.append(slab(boss - up * 0.006, (0.004, 0.016, 0.006),
                          axes=(RIGHT, up, AXIS_FORWARD)))
    yoke = back + up * 0.36 + AXIS_FORWARD * 0.02
    canti.append(tube([RIGHT * -0.09 + back + up * 0.325,
                       yoke, RIGHT * 0.09 + back + up * 0.325], 0.0012,
                      sides=3))
    part(lw, "front_brake", merge(*canti), m["silver"], levels=0)
    # The red fork decal.
    for s in (-1, 1):
        part(lw, f"decal{s}", slab(RIGHT * (0.0705 * s) + back + up * 0.14,
                                   (0.001, 0.05, 0.007),
                                   axes=(RIGHT, up, AXIS_FORWARD)),
             m["red"], levels=0)


def drivetrain(coll, m):
    """A triple chainset turning about the bottom bracket, the pedals that
    stay level on it, and the cassette that turns with the rear wheel."""
    cr = assembly("crank", coll, BB, mesh="crank")
    for k, (r, x) in enumerate(((0.083, 0.047), (0.064, 0.041),
                                (0.045, 0.035))):
        teeth = []
        n = int(r * 560)
        for i in range(n):
            a = 2 * math.pi * i / n
            out = UP * math.cos(a) + FORWARD * math.sin(a)
            teeth.append(slab(RIGHT * x + out * (r + 0.002),
                              (0.0018, 0.004, 0.0028),
                              axes=(RIGHT, out, RIGHT.cross(out))))
        part(cr, f"ring{k}", merge(
            torus(RIGHT * x, RIGHT, UP, r - 0.005, (0.006, 0.0018), 32, 4),
            *teeth), m["silver"] if k else m["dark_alloy"], levels=0)
    spider = []
    for i in range(5):
        a = 2 * math.pi * i / 5
        out = UP * math.cos(a) + FORWARD * math.sin(a)
        spider.append(tube([RIGHT * 0.045, RIGHT * 0.045 + out * 0.08],
                           0.006, sides=4))
    part(cr, "spider", merge(*spider), m["alloy"], levels=0)
    for s, sign in ((1, -1), (-1, 1)):
        # Arms opposed: the right one down at rest, the left one up.
        part(cr, f"arm{s}", loft([
            (RIGHT * (0.056 * s), 0.008, 0.017),
            (RIGHT * (0.062 * s) + UP * (sign * CRANK * 0.5), 0.007, 0.013),
            (RIGHT * (0.066 * s) + UP * (sign * CRANK), 0.007, 0.012),
        ], FORWARD, sides=8), m["alloy"])
    part(cr, "spindle", tube([RIGHT * -0.06, RIGHT * 0.06], 0.008, sides=8),
         m["silver"], levels=0)
    assembly("crank_end", coll, BB + V((0.066, -CRANK, 0)),
             point="crank_end")

    # One pedal, centred on its body: a cage, a toe clip and strap ahead,
    # and amber reflectors front and back.
    pedal_at = BB + V((0.066 + PEDAL_OUT, -CRANK, 0))
    pd = assembly("pedal", coll, pedal_at, mesh="pedal")
    part(pd, "cage", merge(
        slab(V((0, 0, 0.0)), (0.045, 0.006, 0.006)),
        slab(V((0, 0, 0.034)), (0.045, 0.008, 0.004)),
        slab(V((0, 0, -0.034)), (0.045, 0.008, 0.004)),
        slab(V((0.045, 0, 0.0)), (0.004, 0.008, 0.036)),
        slab(V((-0.045, 0, 0.0)), (0.004, 0.008, 0.036)),
    ), m["dark_alloy"], levels=0, bevel=0.002)
    part(pd, "toe_clip", tube([
        V((-0.035, 0.0, 0.038)), V((-0.03, 0.035, 0.085)),
        V((0.0, 0.06, 0.11)), V((0.03, 0.035, 0.085)),
        V((0.035, 0.0, 0.038)),
    ], 0.0028, ref=FORWARD, sides=4), m["silver"], levels=0)
    part(pd, "strap", torus(V((0, 0.035, 0.03)), FORWARD, UP, 0.045,
                            (0.002, 0.008), 12, 4), m["black"], levels=0)
    for z in (0.039, -0.039):
        part(pd, f"reflector{z}", slab(V((0, 0, z)), (0.018, 0.005, 0.002)),
             m["reflector"], levels=0)

    # A display copy on the left crank, up at rest.
    other = assembly("pedal_left", coll,
                     BB + V((-0.066 - PEDAL_OUT, CRANK, 0)))
    for child in pd.children:
        dup = child.copy()
        coll.objects.link(dup)
        dup.parent = other

    cs = assembly("cassette", coll, REAR_AXLE, mesh="cassette")
    cogs = []
    for k in range(7):
        r = 0.022 + 0.0045 * k
        cogs.append(torus(RIGHT * (0.05 - 0.004 * k), RIGHT, UP, r,
                          (0.004, 0.0012), 24, 4))
    part(cs, "cogs", merge(*cogs), m["silver"], levels=0)


build()
