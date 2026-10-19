"""Builds models/tractor.blend: a little red farm tractor of the old kind.

    Blender -b --factory-startup --python tools/figure/tractor.py

The tractor is authored life size in the game's frame -- x to the
driver's left, y up, z forward -- with its origin on the centre line,
midway between the axles and level with the ground it stands on (its
wheels are not the same size, so no axle height would do). Its
assemblies are the body, one rear wheel and one front wheel (the game
draws each twice, turned about for the left-hand side), and the steering
wheel, which turns about its column; tools/figure/export_model.py writes
each in its empty's own frame, and the empties marked as points -- the
four axles, the seat, the pedals, the steering wheel's hub, a point down
its column, and its rim where the hands hold it, the exhaust's mouth, and
the headlamps -- as model-space locations the game places wheels and the
driver by.

It is open, red over grey iron: a long bonnet on the engine, a pan seat
on a stalk between big ribbed rear wheels under round mudguards, a raked
steering column, and an upright exhaust stack.

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
OUT = os.path.join(ROOT, "models", "tractor.blend")

# Key dimensions, metres; moppe/mov/tractor.hh agrees.
REAR_RADIUS = 0.66
REAR_TRACK = 0.76    # the centre line to a rear wheel's centre
REAR_AXLE = -0.80    # the origin to the rear axle, along z
REAR_WIDTH = 0.17    # half a rear tyre's width
FRONT_RADIUS = 0.37
FRONT_TRACK = 0.66
FRONT_AXLE = 1.10
FRONT_WIDTH = 0.08

SEAT = V((0, 1.16, -1.00))
STEERING_WHEEL = V((0, 1.62, -0.50))
COLUMN = (V((0, 1.22, 0.06)) - STEERING_WHEEL).normalized()
WHEEL_RIM = 0.20     # the steering wheel's radius
EXHAUST = V((-0.17, 2.06, 0.92))

PALETTE = {
    "paint": (0.74, 0.10, 0.07),
    "paint_dark": (0.48, 0.06, 0.05),
    "iron": (0.30, 0.31, 0.32),
    "iron_dark": (0.18, 0.18, 0.19),
    "cream": (0.90, 0.84, 0.64),
    "black": (0.06, 0.06, 0.07),
    "tire": (0.07, 0.07, 0.07),
    "steel": (0.58, 0.60, 0.62),
    "seat": (0.10, 0.10, 0.11),
    "grille": (0.70, 0.70, 0.66),
    "dial": (0.82, 0.84, 0.78),
    "lens": (1.0, 0.95, 0.72),
    "tail_light": (1.0, 0.20, 0.12),
}

# How each paint finishes, as the rest of its Principled BSDF: old
# enamel on the sheet metal and the wheels, still with some shine, cast
# iron under it, a painted pan seat, a bright grille, a glazed dial, and
# ribbed rubber; the lamps' lenses let light through.
FINISH = {
    "paint": dict(roughness=0.42, coat=0.35),
    "paint_dark": dict(roughness=0.48, coat=0.25),
    "iron": dict(metallic=0.6, roughness=0.7),
    "iron_dark": dict(metallic=0.5, roughness=0.75),
    "cream": dict(roughness=0.42, coat=0.3),
    "black": dict(roughness=0.6),
    "tire": dict(roughness=0.92),
    "steel": dict(metallic=0.85, roughness=0.32),
    "seat": dict(metallic=0.2, roughness=0.45, coat=0.2),
    "grille": dict(metallic=0.75, roughness=0.35),
    "dial": dict(roughness=0.08, coat=1.0),
    "lens": dict(roughness=0.05, transmission=0.6, emission=0.6),
    "tail_light": dict(roughness=0.1, transmission=0.5, emission=0.6),
}


def prism(profile, x0, x1):
    """The polygon `profile`, given as (y, z) pairs, swept from x0 to x1."""
    cage = Cage()
    x0, x1 = min(x0, x1), max(x0, x1)
    area = sum(a[0] * b[1] - a[1] * b[0]
               for a, b in zip(profile, profile[1:] + profile[:1]))
    if area < 0:
        profile = list(reversed(profile))
    n = len(profile)
    for x in (x0, x1):
        cage.verts += [V((x, y, z)) for y, z in profile]
    cage.faces.append(list(reversed(range(n))))
    cage.faces.append(list(range(n, 2 * n)))
    for i in range(n):
        j = (i + 1) % n
        cage.faces.append([i, j, n + j, n + i])
    return cage


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    coll = bpy.data.collections.new("tractor")
    scene.collection.children.link(coll)
    m = {name: material(name, c, emit=name in ("lens", "tail_light"),
                        **FINISH[name])
         for name, c in PALETTE.items()}

    wheels(coll, m)
    body(coll, m)
    hitch(coll, m)
    steering(coll, m)

    assembly("seat_driver", coll, SEAT, point="seat_driver")
    for s, tag in ((1, "left"), (-1, "right")):
        assembly(f"pedal_{tag}", coll, V((0.37 * s, 0.68, -0.40)),
                 point=f"pedal_{tag}")
        assembly(f"headlight_{tag}", coll, V((0.33 * s, 1.10, 1.30)),
                 point=f"headlight_{tag}")
    assembly("exhaust", coll, EXHAUST, point="exhaust")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
    print(f"wrote {OUT}: {len(coll.objects)} objects")


def rear_tyre(asm, m):
    """A tall tyre with chevron bars on a cream disc wheel, about x, its
    face toward -x."""
    axis, ref = RIGHT, UP
    side = axis.cross(ref)
    r, w = REAR_RADIUS, REAR_WIDTH
    o = V((0, 0, 0))
    part(asm, "tyre", torus(o, axis, ref, r - 0.16, (0.15, w), 32, 10),
         m["tire"])
    # The bars run in from each shoulder toward the middle, the two rows
    # staggered by half a pitch, leaning back from the way they roll.
    bars = []
    count = 18
    for i in range(count):
        for row in (-1, 1):
            a = 2 * math.pi * (i + (0.5 if row > 0 else 0.0)) / count
            out = ref * math.cos(a) + side * math.sin(a)
            along = axis.cross(out)
            across = (axis * row + along * 0.75).normalized()
            centre = out * (r - 0.04) + axis * (row * w * 0.5) + \
                along * 0.03
            bars.append(slab(centre, (w * 0.55, 0.035, 0.028),
                             axes=(across, out, across.cross(out))))
    part(asm, "bars", merge(*bars), m["tire"], levels=0, bevel=0.01,
         smooth=False)
    part(asm, "rim", loft([
        (axis * 0.13, 0.33, 0.33),
        (axis * 0.00, 0.35, 0.35),
        (axis * -0.13, 0.34, 0.34),
        (axis * -0.14, 0.30, 0.30),
    ], ref, sides=20), m["cream"], levels=0)
    # The dished centre, offset outward, and the hub through it.
    part(asm, "disc", loft([
        (axis * -0.05, 0.31, 0.31),
        (axis * -0.11, 0.20, 0.20),
        (axis * -0.15, 0.13, 0.13),
    ], ref, sides=20), m["cream"], levels=0)
    part(asm, "hub", loft([
        (axis * -0.15, 0.10, 0.10),
        (axis * -0.24, 0.085, 0.085),
        (axis * -0.26, 0.05, 0.05),
    ], ref, sides=12), m["iron"])
    nuts = []
    for i in range(8):
        a = 2 * math.pi * i / 8
        out = ref * math.cos(a) + side * math.sin(a)
        nuts.append(loft([(out * 0.165 + axis * -0.14, 0.016, 0.016),
                          (out * 0.165 + axis * -0.165, 0.014, 0.014)],
                         axis.cross(out), sides=6))
    part(asm, "nuts", merge(*nuts), m["steel"], levels=0)


def front_tyre(asm, m):
    """A narrow ribbed tyre on a cream wheel, about x, its face toward
    -x."""
    axis, ref = RIGHT, UP
    r, w = FRONT_RADIUS, FRONT_WIDTH
    o = V((0, 0, 0))
    part(asm, "tyre", torus(o, axis, ref, r - 0.09, (0.09, w), 28, 8),
         m["tire"])
    ribs = [torus(axis * x, axis, ref, r - 0.008, (0.012, 0.011), 32, 4)
            for x in (-0.035, 0.035)]
    part(asm, "ribs", merge(*ribs), m["tire"], levels=0)
    part(asm, "rim", loft([
        (axis * 0.06, 0.20, 0.20),
        (axis * -0.06, 0.21, 0.21),
        (axis * -0.075, 0.17, 0.17),
        (axis * -0.09, 0.08, 0.08),
    ], ref, sides=16), m["cream"], levels=0)
    part(asm, "cap", loft([
        (axis * -0.09, 0.065, 0.065),
        (axis * -0.15, 0.045, 0.045),
    ], ref, sides=10), m["iron"])


def wheels(coll, m):
    """The right-hand wheels export; the left-hand ones only display
    them."""
    pairs = (("rear_wheel", rear_tyre, REAR_TRACK, REAR_RADIUS, REAR_AXLE,
              "r"),
             ("front_wheel", front_tyre, FRONT_TRACK, FRONT_RADIUS,
              FRONT_AXLE, "f"))
    for name, make, track, radius, z, tag in pairs:
        first = assembly(name, coll, V((-track, radius, z)), mesh=name,
                         point=f"wheel_{tag}r")
        make(first, m)
        other = assembly(f"{name}_l", coll, V((track, radius, z)),
                         point=f"wheel_{tag}l")
        other.rotation_euler = (0, 0, math.pi)
        for child in first.children:
            dup = child.copy()
            coll.objects.link(dup)
            dup.parent = other


def body(coll, m):
    b = assembly("body", coll, V((0, 0, 0)), mesh="body")
    rear_y = REAR_RADIUS

    # The iron: the engine block and sump ahead, the gearbox and the rear
    # axle's housing under the seat, its trumpets out to the wheels.
    part(b, "engine", slab(V((0, 0.70, 0.62)), (0.21, 0.19, 0.66)),
         m["iron"], levels=0, bevel=0.04)
    part(b, "sump", slab(V((0, 0.47, 0.60)), (0.17, 0.07, 0.55)),
         m["iron_dark"], levels=0, bevel=0.03)
    part(b, "gearbox", slab(V((0, 0.72, -0.40)), (0.24, 0.22, 0.42)),
         m["iron"], levels=0, bevel=0.05)
    part(b, "diff", blob(V((0, rear_y, REAR_AXLE)), (0.30, 0.30, 0.26), 14,
                         8), m["iron"], levels=1)
    for s in (-1, 1):
        part(b, f"trumpet{s}", loft([
            (V((s * 0.26, rear_y, REAR_AXLE)), 0.15, 0.15),
            (V((s * 0.48, rear_y, REAR_AXLE)), 0.09, 0.09),
            (V((s * (REAR_TRACK - 0.10), rear_y, REAR_AXLE)), 0.08, 0.08),
        ], UP, sides=12), m["iron"], levels=1)
        part(b, f"head{s}", slab(V((s * 0.215, 0.98, 0.70)),
                                 (0.02, 0.08, 0.40)), m["iron_dark"],
             levels=0, bevel=0.015)

    # The bonnet, rounded and a little narrower at its nose, over the
    # engine, with the radiator's grille upright at the front.
    part(b, "bonnet", loft([
        (V((0, 1.05, -0.06)), 0.29, 0.24),
        (V((0, 1.07, 0.55)), 0.285, 0.235),
        (V((0, 1.06, 1.25)), 0.26, 0.22),
        (V((0, 1.04, 1.38)), 0.24, 0.20),
    ], RIGHT, sides=12), m["paint"], levels=2)
    part(b, "grille_frame", slab(V((0, 0.98, 1.40)), (0.235, 0.26, 0.03)),
         m["paint_dark"], levels=0, bevel=0.03)
    part(b, "grille", slab(V((0, 0.98, 1.425)), (0.19, 0.21, 0.012)),
         m["grille"], levels=0, bevel=0.006)
    slats = [slab(V((0, 0.80 + 0.045 * i, 1.44)), (0.18, 0.009, 0.006))
             for i in range(9)]
    part(b, "slats", merge(*slats), m["black"], levels=0)
    part(b, "badge", slab(V((0, 1.215, 1.40)), (0.07, 0.025, 0.01)),
         m["cream"], levels=0, bevel=0.008)
    part(b, "stripe", loft([
        (V((0, 1.282, -0.05)), 0.035, 0.006),
        (V((0, 1.30, 0.55)), 0.035, 0.006),
        (V((0, 1.282, 1.30)), 0.035, 0.006),
    ], RIGHT, sides=6), m["cream"], levels=0)
    part(b, "radiator_cap", loft([(V((0, 1.27, 1.18)), 0.045, 0.045),
                                  (V((0, 1.31, 1.18)), 0.04, 0.04)],
                                 RIGHT, sides=10), m["steel"])
    # The front bolster the axle swings from, and the axle.
    part(b, "bolster", slab(V((0, 0.48, FRONT_AXLE)), (0.18, 0.11, 0.20)),
         m["iron"], levels=0, bevel=0.03)
    part(b, "front_axle", slab(V((0, FRONT_RADIUS + 0.04, FRONT_AXLE)),
                               (FRONT_TRACK - 0.12, 0.05, 0.055)),
         m["iron_dark"], levels=0, bevel=0.02)
    for s in (-1, 1):
        x = s * (FRONT_TRACK - 0.11)
        part(b, f"kingpin{s}", tube([V((x, FRONT_RADIUS - 0.06, FRONT_AXLE)),
                                     V((x, FRONT_RADIUS + 0.14, FRONT_AXLE))],
                                    0.035, sides=8), m["iron_dark"],
             levels=0)
        # The headlamps on short arms either side of the radiator.
        lamp = V((0.33 * s, 1.10, 1.30))
        part(b, f"lamp_arm{s}", tube([V((0.24 * s, 1.08, 1.28)), lamp],
                                     0.018, sides=6), m["iron_dark"],
             levels=0)
        part(b, f"lamp_bowl{s}", loft([
            (lamp - FORWARD * 0.07, 0.04, 0.04),
            (lamp - FORWARD * 0.02, 0.085, 0.085),
            (lamp + FORWARD * 0.02, 0.09, 0.09),
        ], UP, sides=14), m["paint"], levels=1)
        part(b, f"lamp{s}", loft([(lamp + FORWARD * 0.02, 0.075, 0.075),
                                  (lamp + FORWARD * 0.035, 0.05, 0.05)],
                                 UP, sides=14), m["lens"], levels=0)
    # The drag link from the steering box down to the left king pin.
    part(b, "drag_link", tube([V((0.25, 0.92, 0.05)),
                               V((0.40, 0.66, 0.55)),
                               V((FRONT_TRACK - 0.16, FRONT_RADIUS + 0.12,
                                  FRONT_AXLE - 0.12))], 0.022, sides=6),
         m["iron_dark"], levels=0)

    # The exhaust stack on the right and the air cleaner on the left.
    part(b, "exhaust_pipe", tube([V((-0.17, 1.20, 0.92)), EXHAUST],
                                 0.042, sides=10), m["black"], levels=0)
    part(b, "exhaust_cap", loft([(EXHAUST - UP * 0.02, 0.05, 0.05),
                                 (EXHAUST + UP * 0.01, 0.052, 0.052)],
                                RIGHT, sides=10), m["iron_dark"], levels=0)
    part(b, "air_pipe", tube([V((0.17, 1.22, 0.72)), V((0.17, 1.52, 0.72))],
                             0.035, sides=10), m["paint_dark"], levels=0)
    part(b, "air_bowl", blob(V((0.17, 1.58, 0.72)), (0.07, 0.07, 0.07),
                             12, 6), m["steel"], levels=1)

    # The scuttle behind the bonnet holding the dials, and the fuel
    # filler on top.
    part(b, "scuttle", slab(V((0, 1.06, -0.12)), (0.28, 0.22, 0.09)),
         m["paint"], levels=0, bevel=0.04)
    part(b, "dash", slab(V((0, 1.12, -0.215)), (0.20, 0.09, 0.01)),
         m["black"], levels=0, bevel=0.006)
    dials = []
    for x in (-0.09, 0.09):
        c = V((x, 1.13, -0.228))
        dials.append(loft([(c, 0.05, 0.05), (c - FORWARD * 0.006, 0.045,
                                              0.045)], RIGHT, sides=12))
    part(b, "dials", merge(*dials), m["dial"], levels=0)
    part(b, "filler", loft([(V((0, 1.28, -0.08)), 0.05, 0.05),
                            (V((0, 1.32, -0.08)), 0.045, 0.045)], RIGHT,
                           sides=10), m["steel"])

    # Footplates either side of the gearbox, the clutch and brake pedals,
    # and the levers by the seat.
    for s in (-1, 1):
        part(b, f"footplate{s}", slab(V((s * 0.43, 0.62, -0.30)),
                                      (0.15, 0.015, 0.36)), m["black"],
             levels=0, bevel=0.006)
        part(b, f"pedal{s}", tube([V((s * 0.25, 0.72, -0.12)),
                                   V((s * 0.33, 0.78, -0.02))], 0.016,
                                  sides=6), m["iron_dark"], levels=0)
        part(b, f"pedal_pad{s}", slab(V((s * 0.34, 0.79, 0.0)),
                                      (0.05, 0.012, 0.035)), m["black"],
             levels=0, bevel=0.004)
    part(b, "gear_lever", tube([V((-0.10, 0.94, -0.55)),
                                V((-0.16, 1.32, -0.62))], 0.013,
                               sides=6), m["steel"], levels=0)
    part(b, "gear_knob", blob(V((-0.165, 1.34, -0.625)),
                              (0.03, 0.03, 0.03), 8, 5), m["black"])
    part(b, "hydraulic_lever", tube([V((0.20, 0.96, -0.82)),
                                     V((0.25, 1.22, -0.70))], 0.012,
                                    sides=6), m["steel"], levels=0)
    part(b, "hydraulic_knob", blob(V((0.252, 1.235, -0.695)),
                                   (0.025, 0.025, 0.025), 8, 5),
         m["black"])

    # The pan seat on its spring stalk.
    part(b, "seat_stalk", tube([V((0, 0.92, -0.62)), V((0, 1.02, -0.80)),
                                SEAT + V((0, -0.06, 0.02))], 0.035,
                               sides=8), m["steel"], levels=0)
    part(b, "seat_pan", blob(SEAT, (0.22, 0.05, 0.19), 14, 6,
                             shape=lambda u, p: p + V((0, 0.10 * (
                                 u.x * u.x + max(0.0, -u.z) * 0.6), 0))),
         m["seat"], levels=1)
    back = (UP - FORWARD * 0.25).normalized()
    part(b, "seat_back", slab(SEAT + V((0, 0.22, -0.19)),
                              (0.18, 0.11, 0.03),
                              axes=(RIGHT, back, RIGHT.cross(back))),
         m["seat"], bevel=0.03)

    # Round mudguards over the rear wheels with flat skirts toward the
    # seat, a lamp on the left one's back.
    for s in (-1, 1):
        x = s * REAR_TRACK
        r = REAR_RADIUS + 0.07
        arc = []
        for i in range(10):
            a = math.radians(12 + 150 * i / 9)
            arc.append((rear_y + r * math.sin(a),
                        REAR_AXLE + r * math.cos(a)))
        part(b, f"mudguard{s}", loft(
            [(V((x, y, z)), (REAR_WIDTH + 0.04) * math.sqrt(2),
              0.012 * math.sqrt(2)) for y, z in arc],
            RIGHT, sides=4, turn=math.pi / 4), m["paint"], levels=0,
            bevel=0.008)
        inner = s * (REAR_TRACK - REAR_WIDTH - 0.04)
        part(b, f"skirt{s}", prism(arc + [(0.63, arc[-1][1]),
                                          (0.63, arc[0][1])],
                                   inner, inner - s * 0.012),
             m["paint"], levels=0, bevel=0.004, smooth=False)
        part(b, f"handle{s}", tube([V((inner - s * 0.01, 1.30, -0.62)),
                                    V((inner + s * 0.05, 1.40, -0.75)),
                                    V((inner + s * 0.05, 1.40, -0.95)),
                                    V((inner - s * 0.01, 1.30, -1.08))],
                                   0.014, sides=6), m["steel"], levels=0)
    lamp = V((REAR_TRACK, rear_y + 0.40, REAR_AXLE - 0.62))
    part(b, "tail_lamp", slab(lamp, (0.04, 0.035, 0.02)), m["tail_light"],
         levels=0, bevel=0.008)


def hitch(coll, m):
    """The three-point linkage and the drawbar behind."""
    h = assembly("hitch", coll, V((0, 0, 0)), mesh="hitch")
    back = REAR_AXLE - 0.62
    for s in (-1, 1):
        part(h, f"lower_link{s}", tube([V((s * 0.20, 0.50, REAR_AXLE - 0.05)),
                                        V((s * 0.34, 0.42, back))], 0.03,
                                       sides=8), m["iron_dark"], levels=0)
        part(h, f"lift_arm{s}", tube([V((s * 0.18, 1.00, REAR_AXLE - 0.15)),
                                      V((s * 0.20, 0.98, back + 0.25)),
                                      V((s * 0.30, 0.48, back + 0.18))],
                                     0.02, sides=6), m["iron"], levels=0)
    part(h, "top_link", tube([V((0, 0.98, REAR_AXLE - 0.30)),
                              V((0, 0.92, back + 0.02))], 0.028, sides=8),
         m["paint_dark"], levels=0)
    part(h, "drawbar", slab(V((0, 0.42, back)), (0.40, 0.03, 0.05)),
         m["iron_dark"], levels=0, bevel=0.012)
    part(h, "pto", tube([V((0, 0.62, REAR_AXLE - 0.28)),
                         V((0, 0.62, back + 0.10))], 0.04, sides=8),
         m["steel"], levels=0)


def steering(coll, m):
    """The wheel turns about its column; the column itself stays with the
    body."""
    column = assembly("column", coll, V((0, 0, 0)), mesh="column")
    part(column, "shaft", tube([STEERING_WHEEL + COLUMN * 0.03,
                                STEERING_WHEEL + COLUMN * 0.62], 0.025,
                               sides=8), m["iron_dark"], levels=0)

    w = assembly("steering_wheel", coll, STEERING_WHEEL,
                 mesh="steering_wheel", point="steering_hub")
    o = V((0, 0, 0))
    radius = WHEEL_RIM
    part(w, "rim", torus(o, COLUMN, RIGHT, radius, (0.016, 0.016), 22, 6),
         m["black"])
    up = COLUMN.cross(RIGHT)
    spokes = []
    for k in range(3):
        a = 2 * math.pi * k / 3 + math.pi / 2
        d = RIGHT * math.cos(a) + up * math.sin(a)
        spokes.append(tube([COLUMN * 0.035, d * (radius - 0.01)], 0.01,
                           sides=6))
    part(w, "spokes", merge(*spokes), m["steel"], levels=0)
    part(w, "boss", loft([(COLUMN * 0.05, 0.035, 0.035),
                          (COLUMN * -0.01, 0.04, 0.04)], RIGHT, sides=10),
         m["black"])

    assembly("steering_column", coll, STEERING_WHEEL + COLUMN,
             point="steering_column")
    assembly("grip_left", coll, STEERING_WHEEL + RIGHT * radius,
             point="grip_left")
    assembly("grip_right", coll, STEERING_WHEEL - RIGHT * radius,
             point="grip_right")


if __name__ == "__main__":
    build()
