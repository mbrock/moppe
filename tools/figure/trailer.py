"""Builds models/trailer.blend: the tractor's painted farm trailer.

    Blender -b --factory-startup --python tools/figure/trailer.py

A one-axle plank trailer of the old kind, someone's summer project: its
sides, headboard, and tailgate are boarded in six planks each painted a
colour of the rainbow, violet at the foot to red at the top, held by
navy steel stakes under a cream capping rail, with a navy ladder rack
over the headboard, a pine deck, and a sunflower-yellow A-frame drawbar
to an eye that drops over the tractor's drawbar pin. It runs on two
small wheels with pink rims, under the bed on leaf springs.

It is authored life size in the game's frame -- x to the left, y up, z
forward toward the tractor -- with its origin on the centre line, under
the axle and level with the ground it stands on. Its assemblies are the
body and one wheel (the game draws it twice, turned about for the
left-hand side); the points are the two axle ends and the hitch eye.
moppe/mov/tractor.hh agrees with the dimensions.

Rebuilding discards hand edits to the .blend.
"""

import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(__file__))
from shapes import (V, RIGHT, UP, FORWARD, assembly, loft, material,  # noqa
                    merge, part, slab, torus, tube)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "models", "trailer.blend")

# Key dimensions, metres; moppe/mov/tractor.hh agrees.
RADIUS = 0.36        # the wheels'
TRACK = 0.62         # the centre line to a wheel's centre
WIDTH = 0.085        # half a tyre's width
EYE = V((0, 0.47, 2.30))
FLOOR = 0.86         # the deck's top
BED = 0.78           # the centre line to a side's outer face
FRONT = 1.45         # the headboard's outer face, along z
BACK = -1.15         # the tailgate's
BOARD = 0.09         # each plank's height
THICK = 0.028        # and thickness
RAIL = 0.40          # the centre line to each chassis rail

# Bottom to top.
RAINBOW = ("violet", "blue", "green", "yellow", "orange", "red")

PALETTE = {
    "violet": (0.50, 0.26, 0.74),
    "blue": (0.12, 0.44, 0.84),
    "green": (0.18, 0.68, 0.30),
    "yellow": (0.99, 0.82, 0.14),
    "orange": (0.97, 0.48, 0.10),
    "red": (0.88, 0.14, 0.14),
    "navy": (0.10, 0.14, 0.32),
    "cream": (0.94, 0.90, 0.80),
    "pine": (0.76, 0.60, 0.38),
    "drawbar": (0.98, 0.76, 0.10),
    "rim": (0.94, 0.28, 0.58),
    "iron": (0.20, 0.20, 0.21),
    "steel": (0.62, 0.64, 0.66),
    "tire": (0.07, 0.07, 0.07),
    "tail_light": (1.0, 0.18, 0.10),
    "reflector": (1.0, 0.55, 0.08),
}

# Fresh gloss enamel on the planks, the stakes, the drawbar, and the rims,
# oiled pine on the deck, steel and rubber; the lamps glow through their
# lenses.
FINISH = {name: dict(roughness=0.32, coat=0.5) for name in RAINBOW}
FINISH.update({
    "navy": dict(metallic=0.3, roughness=0.4, coat=0.4),
    "cream": dict(roughness=0.38, coat=0.35),
    "pine": dict(roughness=0.7, sheen=0.1),
    "drawbar": dict(roughness=0.35, coat=0.5),
    "rim": dict(roughness=0.3, coat=0.6),
    "iron": dict(metallic=0.5, roughness=0.7),
    "steel": dict(metallic=0.9, roughness=0.25),
    "tire": dict(roughness=0.92),
    "tail_light": dict(roughness=0.1, transmission=0.5, emission=0.6),
    "reflector": dict(roughness=0.1, transmission=0.4, emission=0.4),
})


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    coll = bpy.data.collections.new("trailer")
    scene.collection.children.link(coll)
    m = {name: material(name, c, emit=name in ("tail_light", "reflector"),
                        **FINISH[name])
         for name, c in PALETTE.items()}

    wheels(coll, m)
    body(coll, m)
    assembly("eye", coll, EYE, point="eye")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
    print(f"wrote {OUT}: {len(coll.objects)} objects")


def wheel(asm, m):
    """A balloon tyre on a pink rim, about x, its face toward -x."""
    axis, ref = RIGHT, UP
    side = axis.cross(ref)
    r, w = RADIUS, WIDTH
    o = V((0, 0, 0))
    part(asm, "tyre", torus(o, axis, ref, r - 0.085, (0.085, w), 28, 8),
         m["tire"])
    treads = []
    for i in range(24):
        a = 2 * math.pi * i / 24
        out = ref * math.cos(a) + side * math.sin(a)
        treads.append(slab(out * (r - 0.006), (w * 0.8, 0.012, 0.016),
                           axes=(axis, out, axis.cross(out))))
    part(asm, "treads", merge(*treads), m["tire"], levels=0, smooth=False)
    part(asm, "rim", loft([
        (axis * 0.07, 0.205, 0.205),
        (axis * -0.07, 0.215, 0.215),
        (axis * -0.085, 0.17, 0.17),
        (axis * -0.095, 0.09, 0.09),
    ], ref, sides=18), m["rim"], levels=0)
    part(asm, "cap", loft([
        (axis * -0.095, 0.07, 0.07),
        (axis * -0.13, 0.055, 0.055),
        (axis * -0.14, 0.03, 0.03),
    ], ref, sides=12), m["steel"])
    nuts = []
    for i in range(5):
        a = 2 * math.pi * i / 5
        out = ref * math.cos(a) + side * math.sin(a)
        nuts.append(loft([(out * 0.12 + axis * -0.09, 0.013, 0.013),
                          (out * 0.12 + axis * -0.11, 0.011, 0.011)],
                         axis.cross(out), sides=6))
    part(asm, "nuts", merge(*nuts), m["steel"], levels=0)


def wheels(coll, m):
    """The right-hand wheel exports; the left-hand one only displays
    it."""
    first = assembly("wheel", coll, V((-TRACK, RADIUS, 0)), mesh="wheel",
                     point="wheel_r")
    wheel(first, m)
    other = assembly("wheel_l", coll, V((TRACK, RADIUS, 0)),
                     point="wheel_l")
    other.rotation_euler = (0, 0, math.pi)
    for child in first.children:
        dup = child.copy()
        coll.objects.link(dup)
        dup.parent = other


def boards(b, m, name, centre_of, half_of):
    """Six planks in the rainbow's colours, one over the next with a
    hairline between."""
    for i, colour in enumerate(RAINBOW):
        y = FLOOR + BOARD * (i + 0.5)
        part(b, f"{name}_{colour}", slab(centre_of(y), half_of),
             m[colour], levels=0, bevel=0.006, smooth=False)


def body(coll, m):
    b = assembly("body", coll, V((0, 0, 0)), mesh="body")
    length = FRONT - BACK
    middle = 0.5 * (FRONT + BACK)
    top = FLOOR + BOARD * len(RAINBOW)
    half_board = BOARD * 0.5 - 0.003

    # The chassis: two rails under the deck, cross members, and the axle
    # on its leaf springs.
    for s in (-1, 1):
        part(b, f"rail{s}", slab(V((s * RAIL, FLOOR - 0.085, middle)),
                                 (0.035, 0.05, length * 0.5 - 0.02)),
             m["navy"], levels=0, bevel=0.01)
        leaves = [slab(V((s * RAIL, 0.47 - 0.016 * k, 0.0)),
                       (0.03, 0.007, 0.42 - 0.11 * k)) for k in range(3)]
        part(b, f"spring{s}", merge(*leaves), m["iron"], levels=0)
        for z in (-0.42, 0.42):
            part(b, f"hanger{s}{z}", tube([V((s * RAIL, 0.46, z)),
                                           V((s * RAIL, FLOOR - 0.13, z))],
                                          0.018, sides=6), m["iron"],
                 levels=0)
        part(b, f"u_bolt{s}", slab(V((s * RAIL, RADIUS + 0.06, 0.0)),
                                   (0.04, 0.05, 0.035)), m["iron"],
             levels=0, bevel=0.008)
    part(b, "axle", tube([V((-(TRACK - 0.09), RADIUS, 0)),
                          V((TRACK - 0.09, RADIUS, 0))], 0.035, sides=8),
         m["iron"], levels=0)
    for z in (BACK + 0.10, -0.55, 0.62, FRONT - 0.10):
        part(b, f"cross{z}", slab(V((0, FLOOR - 0.06, z)),
                                  (BED - 0.04, 0.025, 0.03)),
             m["navy"], levels=0, bevel=0.006)

    # The deck, planked lengthwise in pine.
    planks = 9
    inner = BED - THICK
    for i in range(planks):
        x = -inner + inner * 2 * (i + 0.5) / planks
        part(b, f"deck{i}", slab(V((x, FLOOR - 0.018, middle)),
                                 (inner / planks - 0.004, 0.018,
                                  length * 0.5 - THICK)),
             m["pine"], levels=0, bevel=0.004, smooth=False)

    # The sides run the whole length; the headboard and the tailgate sit
    # between them.
    for s in (-1, 1):
        boards(b, m, f"side{s}",
               lambda y, s=s: V((s * (BED - THICK * 0.5), y, middle)),
               (THICK * 0.5, half_board, length * 0.5))
    boards(b, m, "head", lambda y: V((0, y, FRONT - THICK * 0.5)),
           (BED - THICK - 0.002, half_board, THICK * 0.5))
    boards(b, m, "gate", lambda y: V((0, y, BACK + THICK * 0.5)),
           (BED - THICK - 0.002, half_board, THICK * 0.5))

    # Navy stakes outside the boards, and the cream rail capping them.
    stake_z = (BACK + 0.03, -0.30, 0.60, FRONT - 0.03)
    for s in (-1, 1):
        for z in stake_z:
            part(b, f"stake{s}{z}", slab(V((s * (BED + 0.018),
                                             0.5 * (FLOOR - 0.10 + top),
                                             z)),
                                         (0.018, 0.5 * (top - FLOOR + 0.10),
                                          0.03)),
                 m["navy"], levels=0, bevel=0.006)
        part(b, f"cap{s}", slab(V((s * (BED - 0.004), top + 0.016, middle)),
                                (0.032, 0.016, length * 0.5 + 0.02)),
             m["cream"], levels=0, bevel=0.008)
    for z, name in ((FRONT - 0.004, "cap_head"), (BACK + 0.004, "cap_gate")):
        part(b, name, slab(V((0, top + 0.016, z)),
                           (BED - 0.036, 0.0155, 0.032)),
             m["cream"], levels=0, bevel=0.008)
    for x in (-0.36, 0.36):
        for z in (BACK - 0.01, FRONT + 0.01):
            part(b, f"post{x}{z}", slab(V((x, 0.5 * (FLOOR + top), z)),
                                        (0.025, 0.5 * (top - FLOOR), 0.01)),
                 m["navy"], levels=0, bevel=0.004)
    # The gate's hinges and pins.
    for x in (-0.55, 0.55):
        part(b, f"hinge{x}", slab(V((x, FLOOR + 0.03, BACK - 0.012)),
                                  (0.06, 0.02, 0.012)), m["steel"],
             levels=0, bevel=0.004)
    for s in (-1, 1):
        part(b, f"gate_pin{s}", tube([V((s * (BED + 0.04), top - 0.06,
                                           BACK + 0.05)),
                                       V((s * (BED + 0.04), top - 0.06,
                                          BACK - 0.03))], 0.012, sides=6),
             m["steel"], levels=0)

    # A ladder rack over the headboard, to lean a load against.
    rack_top = top + 0.42
    for x in (-(BED - 0.03), BED - 0.03):
        part(b, f"rack_post{x}", tube([V((x, top - 0.10, FRONT - 0.02)),
                                       V((x, rack_top, FRONT - 0.02))],
                                      0.022, sides=8), m["navy"], levels=0)
    for y in (top + 0.14, top + 0.28, rack_top):
        part(b, f"rung{y}", tube([V((-(BED - 0.03), y, FRONT - 0.02)),
                                  V((BED - 0.03, y, FRONT - 0.02))],
                                 0.016, sides=8), m["navy"], levels=0)

    # The drawbar: an A-frame from the rails to the eye, a brace across,
    # and the eye.
    for s in (-1, 1):
        part(b, f"drawbar{s}", tube([V((s * RAIL, FLOOR - 0.09, FRONT - 0.3)),
                                     V((s * RAIL, FLOOR - 0.09, FRONT)),
                                     V((s * 0.06, EYE.y + 0.02,
                                        EYE.z - 0.16))],
                                    0.032, sides=8), m["drawbar"],
             levels=0)
    part(b, "brace", tube([V((-0.30, FLOOR - 0.15, FRONT + 0.22)),
                           V((0.30, FLOOR - 0.15, FRONT + 0.22))],
                          0.022, sides=8), m["drawbar"], levels=0)
    part(b, "tongue", slab(V((0, EYE.y + 0.02, EYE.z - 0.12)),
                           (0.07, 0.03, 0.07)), m["drawbar"], levels=0,
         bevel=0.01)
    part(b, "eye_ring", torus(EYE, UP, FORWARD, 0.05, (0.022, 0.022), 16,
                              6), m["steel"], levels=0)

    # A light bar under the tailgate: lamps, amber reflectors, and the
    # number plate's space between.
    bar_y = FLOOR - 0.14
    part(b, "light_bar", slab(V((0, bar_y, BACK + 0.02)),
                              (BED - 0.06, 0.035, 0.02)), m["navy"],
         levels=0, bevel=0.006)
    for s in (-1, 1):
        part(b, f"lamp{s}", slab(V((s * (BED - 0.16), bar_y, BACK - 0.004)),
                                 (0.07, 0.03, 0.008)), m["tail_light"],
             levels=0, bevel=0.004)
        # A reflector triangle beside each lamp.
        c = V((s * (BED - 0.32), bar_y, BACK - 0.004))
        part(b, f"reflector{s}", loft([
            (c + FORWARD * 0.006, 0.045, 0.045),
            (c - FORWARD * 0.004, 0.045, 0.045)],
            UP, sides=3), m["reflector"], levels=0)
    part(b, "plate", slab(V((0, bar_y, BACK - 0.004)),
                          (0.17, 0.028, 0.004)), m["cream"], levels=0)


if __name__ == "__main__":
    build()
