"""Builds models/jeep.blend: the open jeep two can ride in.

    Blender -b --factory-startup --python tools/figure/jeep.py

The jeep is authored life size in the game's frame -- x to the driver's
left, y up, z forward -- with its origin on the centre line, midway between the axles
and at their height when it stands on level ground. Its assemblies are
the body, one wheel (the game draws it four times, turned about for the
right-hand side), and the steering wheel, which turns about its column;
tools/figure/export_model.py writes each in its empty's own frame, and
the empties marked as points -- the four axles, the two front seats, the
pedals and the passenger's footrest, the steering wheel's hub, a point
down its column, and its rim where the hands hold it, the headlights --
as model-space locations the game places wheels and riders by.

Rebuilding discards hand edits to the .blend.
"""

import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(__file__))
from shapes import (V, RIGHT, UP, FORWARD, Cage, assembly, blob,  # noqa
                    blender, loft, material, merge, part, slab, torus, tube)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "models", "jeep.blend")

# Key dimensions, metres.
WHEEL_RADIUS = 0.40
TRACK = 0.80       # axle centre line to a wheel's centre
AXLE = 1.20        # the origin to either axle
TUB = 0.62         # half the body's width
SILL = 0.12        # the floor above the axles
RAIL = 0.74        # the top of the body's sides
SEAT = 0.33        # a front seat's distance from the centre line

STEERING_WHEEL = V((SEAT, 0.82, 0.16))
COLUMN = (V((SEAT, 0.62, 0.46)) - STEERING_WHEEL).normalized()

PALETTE = {
    "olive": (0.33, 0.39, 0.22),
    "olive_dark": (0.24, 0.29, 0.17),
    "black": (0.07, 0.07, 0.08),
    "trim": (0.13, 0.13, 0.15),
    "tire": (0.06, 0.06, 0.07),
    "steel": (0.50, 0.52, 0.55),
    "hub": (0.36, 0.38, 0.41),
    "seat": (0.36, 0.21, 0.12),
    "floor": (0.17, 0.18, 0.17),
    "lens": (1.0, 0.95, 0.72),
    "amber": (1.0, 0.55, 0.10),
    "tail_light": (1.0, 0.20, 0.12),
}

# How each paint finishes, as the rest of its Principled BSDF: flat
# army enamel, dull painted steel, a leather seat, bright bolts and
# bumpers, and knobbly rubber; the lamps' lenses let light through.
FINISH = {
    "olive": dict(roughness=0.62, coat=0.1),
    "olive_dark": dict(roughness=0.66, coat=0.1),
    "black": dict(roughness=0.6),
    "trim": dict(metallic=0.3, roughness=0.5),
    "tire": dict(roughness=0.92),
    "steel": dict(metallic=0.85, roughness=0.32),
    "hub": dict(metallic=0.4, roughness=0.55),
    "seat": dict(roughness=0.55, sheen=0.25),
    "floor": dict(metallic=0.3, roughness=0.85),
    "lens": dict(roughness=0.05, transmission=0.6, emission=0.6),
    "amber": dict(roughness=0.1, transmission=0.5, emission=0.6),
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


def strip(points, thickness):
    """A profile that follows `points` (y, z) and is `thickness` deep
    beneath them: a bent plate, for prism."""
    under = [(y - thickness, z) for y, z in reversed(points)]
    return list(points) + under


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    coll = bpy.data.collections.new("jeep")
    scene.collection.children.link(coll)
    m = {name: material(name, c,
                        emit=name in ("lens", "amber", "tail_light"),
                        **FINISH[name])
         for name, c in PALETTE.items()}

    wheels(coll, m)
    body(coll, m)
    cage(coll, m)
    seats(coll, m)
    steering(coll, m)

    # Where the riders sit and put their feet.
    assembly("seat_driver", coll, V((SEAT, 0.43, -0.16)),
             point="seat_driver")
    assembly("seat_passenger", coll, V((-SEAT, 0.43, -0.16)),
             point="seat_passenger")
    for s, tag in ((1, "left"), (-1, "right")):
        assembly(f"pedal_{tag}", coll, V((SEAT + 0.10 * s, 0.20, 0.36)),
                 point=f"pedal_{tag}")
        assembly(f"footrest_{tag}", coll, V((-SEAT + 0.10 * s, 0.20, 0.36)),
                 point=f"footrest_{tag}")
        assembly(f"headlight_{tag}", coll, V((0.33 * s, 0.56, 1.69)),
                 point=f"headlight_{tag}")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
    print(f"wrote {OUT}: {len(coll.objects)} objects")


def tyre(asm, m, axis, ref, prefix=""):
    """A fat knobbly tyre on a steel wheel about `axis`, its face toward
    -axis."""
    o = V((0, 0, 0))
    side = axis.cross(ref)
    part(asm, prefix + "tyre",
         torus(o, axis, ref, WHEEL_RADIUS - 0.125, (0.125, 0.13), 28, 8),
         m["tire"])
    knobs = []
    for i in range(24):
        a = 2 * math.pi * (i + 0.5 * (i % 2)) / 24
        out = ref * math.cos(a) + side * math.sin(a)
        along = axis.cross(out)
        for x in ((-0.07, 0.07) if i % 2 else (0.0,)):
            knobs.append(slab(out * (WHEEL_RADIUS - 0.012) + axis * x,
                              (0.05, 0.02, 0.034), axes=(axis, out, along)))
    part(asm, prefix + "knobs", merge(*knobs), m["tire"], levels=0,
         bevel=0.008, smooth=False)
    part(asm, prefix + "rim", loft([
        (axis * 0.09, 0.19, 0.19),
        (axis * -0.07, 0.21, 0.21),
        (axis * -0.10, 0.20, 0.20),
        (axis * -0.105, 0.13, 0.13),
    ], ref, sides=16), m["steel"], levels=0)
    part(asm, prefix + "cap", loft([
        (axis * -0.105, 0.075, 0.075),
        (axis * -0.15, 0.06, 0.06),
    ], ref, sides=10), m["hub"])
    nuts = []
    for i in range(5):
        a = 2 * math.pi * i / 5
        out = ref * math.cos(a) + side * math.sin(a)
        nuts.append(loft([(out * 0.105 + axis * -0.10, 0.016, 0.016),
                          (out * 0.105 + axis * -0.125, 0.014, 0.014)],
                         axis.cross(out), sides=6))
    part(asm, prefix + "nuts", merge(*nuts), m["hub"], levels=0)


def wheels(coll, m):
    """The right front wheel exports; the other three only display it."""
    first = assembly("wheel", coll, V((-TRACK, 0, AXLE)), mesh="wheel",
                     point="wheel_fr")
    tyre(first, m, RIGHT, UP)
    for tag, x, z in (("fl", TRACK, AXLE), ("rr", -TRACK, -AXLE),
                      ("rl", TRACK, -AXLE)):
        other = assembly(f"wheel_{tag}", coll, V((x, 0, z)),
                         point=f"wheel_{tag}")
        if x > 0:
            other.rotation_euler = (0, 0, math.pi)
        for child in first.children:
            dup = child.copy()
            coll.objects.link(dup)
            dup.parent = other


def body(coll, m):
    b = assembly("body", coll, V((0, 0, 0)), mesh="body")
    rear, cowl = -1.77, 0.60

    # The tub: a floor, sides scooped low where the riders step in, a
    # tailgate, and the cowl the windscreen stands on.
    part(b, "floor", slab(V((0, SILL + 0.02, (rear + cowl) / 2)),
                          (TUB, 0.03, (cowl - rear) / 2)), m["floor"],
         levels=0, bevel=0.01)
    part(b, "underside", slab(V((0, 0.03, -0.1)), (0.40, 0.08, 1.55)),
         m["black"], levels=0, bevel=0.03)
    side = [(SILL, rear), (RAIL, rear), (RAIL, -0.52), (0.50, -0.40),
            (0.50, 0.30), (0.79, 0.50), (0.79, cowl), (SILL, cowl)]
    for s in (-1, 1):
        part(b, f"side{s}", prism(side, s * TUB, s * (TUB - 0.045)),
             m["olive"], levels=0, bevel=0.012, smooth=False)
        part(b, f"step{s}", tube([V((s * (TUB + 0.07), -0.02, -0.50)),
                                  V((s * (TUB + 0.07), -0.02, 0.50))],
                                 0.035, sides=8), m["black"])
    part(b, "tailgate", slab(V((0, (SILL + RAIL) / 2, rear + 0.025)),
                             (TUB, (RAIL - SILL) / 2, 0.025)), m["olive"],
         levels=0, bevel=0.012)
    part(b, "cowl", prism([(SILL, 0.46), (0.74, 0.46), (0.80, 0.52),
                           (0.80, cowl), (SILL, cowl)], -TUB, TUB),
         m["olive"], levels=0, bevel=0.012, smooth=False)
    part(b, "dash", slab(V((0, 0.63, 0.452)), (TUB - 0.08, 0.085, 0.012)),
         m["trim"], levels=0, bevel=0.008)
    for x in (-0.10, 0.02):
        part(b, f"dial{x}", loft([(V((SEAT + x + 0.04, 0.64, 0.445)),
                                   0.04, 0.04),
                                  (V((SEAT + x + 0.04, 0.64, 0.435)),
                                   0.036, 0.036)], UP, sides=12),
             m["steel"], levels=0)

    # The bonnet, falling a little toward a flat upright grille with its
    # slots and round lamps.
    hood = 0.47
    part(b, "bonnet", prism([(0.30, cowl - 0.01), (0.80, cowl - 0.01),
                             (0.75, 1.63), (0.30, 1.65)], -hood, hood),
         m["olive"], levels=0, bevel=0.03, smooth=False)
    part(b, "grille", slab(V((0, 0.53, 1.655)), (hood, 0.23, 0.025)),
         m["olive_dark"], levels=0, bevel=0.012)
    slots = [slab(V((0.06 * i, 0.53, 1.683)), (0.017, 0.15, 0.008))
             for i in range(-3, 4)]
    part(b, "slots", merge(*slots), m["black"], levels=0)
    for s in (-1, 1):
        lamp = V((0.33 * s, 0.56, 1.68))
        part(b, f"lamp_ring{s}", loft([(lamp, 0.10, 0.10),
                                       (lamp + FORWARD * 0.02, 0.095,
                                        0.095)], UP, sides=16),
             m["steel"], levels=0)
        part(b, f"lamp{s}", loft([(lamp + FORWARD * 0.02, 0.08, 0.08),
                                  (lamp + FORWARD * 0.035, 0.06, 0.06)],
                                 UP, sides=16), m["lens"], levels=0)
        part(b, f"indicator{s}", slab(V((0.33 * s, 0.40, 1.684)),
                                      (0.045, 0.022, 0.008)), m["amber"],
             levels=0, bevel=0.004)

    # Flat black wings over the wheels: the front ones run back from the
    # nose and drop to the step, the rear ones stand out from the tub.
    front = strip([(0.22, 0.52), (0.60, 0.72), (0.60, 1.58), (0.40, 1.76)],
                  0.045)
    back = strip([(0.20, -1.76), (0.60, -1.62), (0.60, -0.78),
                  (0.20, -0.64)], 0.045)
    for s in (-1, 1):
        part(b, f"wing{s}", prism(front, s * (hood - 0.02),
                                  s * (TRACK + 0.16)), m["black"],
             levels=0, bevel=0.012, smooth=False)
        part(b, f"flare{s}", prism(back, s * (TUB - 0.02),
                                   s * (TRACK + 0.16)), m["black"],
             levels=0, bevel=0.012, smooth=False)

    # Bumpers, a hoop over the front one, and tow hooks.
    part(b, "front_bumper", slab(V((0, 0.10, 1.86)), (0.84, 0.07, 0.06)),
         m["black"], bevel=0.03)
    part(b, "bull_bar", tube([V((-0.36, 0.12, 1.90)), V((-0.36, 0.46, 1.93)),
                              V((-0.30, 0.52, 1.93)), V((0.30, 0.52, 1.93)),
                              V((0.36, 0.46, 1.93)), V((0.36, 0.12, 1.90))],
                             0.03, ref=FORWARD, sides=8), m["black"])
    part(b, "rear_bumper", slab(V((0, 0.10, -1.85)), (0.74, 0.06, 0.06)),
         m["black"], bevel=0.03)
    for s in (-1, 1):
        part(b, f"bumper_mount{s}", slab(V((0.30 * s, 0.10, 1.74)),
                                         (0.04, 0.05, 0.10)), m["black"],
             levels=0)
        part(b, f"tail_light{s}", slab(V((0.52 * s, 0.52, rear - 0.008)),
                                       (0.05, 0.075, 0.012)),
             m["tail_light"], levels=0, bevel=0.006)

    # The spare on the tailgate.
    spare = V((0, 0.66, rear - 0.16))
    tyre(Parts(b, spare), m, FORWARD, UP, prefix="spare_")
    part(b, "spare_mount", tube([spare + FORWARD * 0.02,
                                 spare + FORWARD * 0.16], 0.06, sides=8),
         m["black"], levels=0)


class Parts:
    """Stands in for an assembly so that `tyre` can hang its parts on the
    body, moved to `offset`."""

    def __init__(self, asm, offset):
        self.asm = asm
        self.offset = offset


_part = part


def part(asm, name, cage, mat, **options):  # noqa: F811
    if isinstance(asm, Parts):
        moved = Cage()
        moved.verts = [v + asm.offset for v in cage.verts]
        moved.faces = cage.faces
        return _part(asm.asm, name, moved, mat, **options)
    return _part(asm, name, cage, mat, **options)


def cage(coll, m):
    """The windscreen's frame and the roll cage, in black tube."""
    c = assembly("cage", coll, V((0, 0, 0)), mesh="cage")
    r = 0.03
    screen_foot, screen_top = V((0.58, 0.80, 0.56)), V((0.56, 1.40, 0.38))
    hoop_foot, hoop_top = V((0.585, RAIL, -0.52)), V((0.565, 1.43, -0.52))
    tail_foot, tail_top = V((0.585, RAIL, -1.72)), V((0.565, 1.40, -1.42))

    def both(p):
        return V((-p.x, p.y, p.z)), p

    def hoop(name, foot, top):
        (fl, fr), (tl, tr) = both(foot), both(top)
        bend = V((0.05, 0, 0))
        part(c, name, tube([fl, tl - UP * 0.06, tl + bend, tr - bend,
                            tr - UP * 0.06, fr], r, ref=FORWARD, sides=8),
             m["black"])

    hoop("screen", screen_foot, screen_top)
    hoop("hoop", hoop_foot, hoop_top)
    hoop("tail", tail_foot, tail_top)
    part(c, "screen_sill", tube(list(both(screen_foot)), r * 0.9, sides=8),
         m["black"])
    for s in (-1, 1):
        def side(p):
            return V((p.x * s, p.y, p.z))
        part(c, f"rail{s}", tube([side(screen_top), side(hoop_top),
                                  side(tail_top)], r, ref=RIGHT, sides=8),
             m["black"])
        part(c, f"brace{s}", tube([side(hoop_top) - UP * 0.25,
                                   side(tail_foot) + UP * 0.02], r * 0.85,
                                  ref=RIGHT, sides=8), m["black"])
        # A wing mirror on a short stalk.
        stalk = V((0.60 * s, 0.86, 0.52))
        part(c, f"mirror_stalk{s}", tube([stalk, stalk + V((0.14 * s, 0.05,
                                                            0.0))], 0.012,
                                         sides=6), m["black"], levels=0)
        part(c, f"mirror{s}", slab(stalk + V((0.19 * s, 0.07, 0.0)),
                                   (0.06, 0.045, 0.012)), m["trim"],
             bevel=0.01)


def seats(coll, m):
    s_asm = assembly("seats", coll, V((0, 0, 0)), mesh="seats")
    back = (UP - FORWARD * 0.18).normalized()
    out = RIGHT.cross(back)
    for s in (-1, 1):
        x = SEAT * s
        part(s_asm, f"cushion{s}", slab(V((x, 0.34, -0.12)),
                                        (0.23, 0.07, 0.25)), m["seat"],
             bevel=0.04)
        part(s_asm, f"back{s}", slab(V((x, 0.70, -0.43)),
                                     (0.22, 0.29, 0.055),
                                     axes=(RIGHT, back, out)), m["seat"],
             bevel=0.04)
        part(s_asm, f"headrest{s}", slab(V((x, 1.08, -0.50)),
                                         (0.12, 0.08, 0.045),
                                         axes=(RIGHT, back, out)),
             m["seat"], bevel=0.03)
        part(s_asm, f"seat_base{s}", slab(V((x, 0.22, -0.14)),
                                          (0.19, 0.06, 0.20)), m["black"],
             levels=0)
    part(s_asm, "bench", slab(V((0, 0.34, -1.08)), (0.56, 0.07, 0.22)),
         m["seat"], bevel=0.04)
    part(s_asm, "bench_back", slab(V((0, 0.66, -1.36)), (0.56, 0.26, 0.05),
                                   axes=(RIGHT, back, out)), m["seat"],
         bevel=0.04)
    part(s_asm, "bench_base", slab(V((0, 0.22, -1.10)), (0.54, 0.06, 0.20)),
         m["black"], levels=0)
    # The gear lever between the seats.
    part(s_asm, "lever", tube([V((0, 0.16, 0.10)), V((0, 0.46, 0.05))],
                              0.013, sides=6), m["steel"], levels=0)
    part(s_asm, "knob", blob(V((0, 0.48, 0.05)), (0.03, 0.03, 0.03), 8, 5),
         m["black"])


def steering(coll, m):
    """The wheel turns about its column; the column itself stays with the
    seats."""
    column = assembly("column", coll, V((0, 0, 0)), mesh="column")
    part(column, "shaft", tube([STEERING_WHEEL + COLUMN * 0.02,
                                STEERING_WHEEL + COLUMN * 0.36], 0.022,
                               sides=8), m["black"], levels=0)

    w = assembly("steering_wheel", coll, STEERING_WHEEL,
                 mesh="steering_wheel", point="steering_hub")
    o = V((0, 0, 0))
    radius = 0.17
    part(w, "rim", torus(o, COLUMN, RIGHT, radius, (0.018, 0.018), 20, 6),
         m["black"])
    up = COLUMN.cross(RIGHT)
    spokes = [tube([COLUMN * 0.03, d * (radius - 0.01)], 0.012, sides=6)
              for d in (RIGHT, -RIGHT, up)]
    part(w, "spokes", merge(*spokes), m["trim"], levels=0)
    part(w, "boss", loft([(COLUMN * 0.05, 0.04, 0.04),
                          (COLUMN * -0.01, 0.045, 0.045)], RIGHT, sides=10),
         m["trim"])

    assembly("steering_column", coll, STEERING_WHEEL + COLUMN,
             point="steering_column")
    assembly("grip_left", coll, STEERING_WHEEL + RIGHT * radius,
             point="grip_left")
    assembly("grip_right", coll, STEERING_WHEEL - RIGHT * radius,
             point="grip_right")


if __name__ == "__main__":
    build()
