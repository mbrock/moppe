"""Builds models/helicopter.blend: a small red two-seat helicopter.

    Blender -b --factory-startup --python tools/figure/helicopter.py

The helicopter is authored life size in the game's frame -- x to the
pilot's left, y up, z forward -- with its origin on the centre line under
the rotor mast, level with the bottom of the skids, so that it stands on
the ground it is placed at. Its assemblies are the body (cabin, canopy,
tail, skids, and the cockpit inside), the main rotor, which turns about
its mast, and the tail rotor, which turns about its own axle;
tools/figure/export_model.py writes each in its empty's own frame, and
the empties marked as points -- the two hubs, the seats, the pilot's
cyclic, collective, and pedals, and the beacon -- as model-space
locations the game places them by.

The cabin is a closed shell, and the game culls back faces, so from the
pilot's seat it is the cockpit that shows and the world all round it.

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
OUT = os.path.join(ROOT, "models", "helicopter.blend")

# Key dimensions, metres.
ROTOR_HUB = V((0, 2.68, 0))
ROTOR_RADIUS = 4.8
TAIL_HUB = V((0.30, 1.86, -5.95))
TAIL_RADIUS = 0.78
SKID = 1.05        # the centre line to a skid
SEAT = 0.33        # a seat's distance from the centre line
PILOT = V((-SEAT, 0.92, 0.10))      # on the right, as pilots sit
PASSENGER = V((SEAT, 0.92, 0.10))

PALETTE = {
    "paint": (0.80, 0.12, 0.09),
    "paint_dark": (0.50, 0.07, 0.06),
    "white": (0.92, 0.92, 0.90),
    "glass": (0.17, 0.25, 0.31),
    "steel": (0.55, 0.57, 0.60),
    "black": (0.06, 0.06, 0.07),
    "rotor": (0.13, 0.13, 0.14),
    "tip": (0.96, 0.78, 0.10),
    "seat": (0.22, 0.22, 0.25),
    "panel": (0.09, 0.09, 0.10),
    "dial": (0.75, 0.80, 0.78),
    "lens": (1.0, 0.95, 0.72),
    "port": (1.0, 0.15, 0.10),
    "starboard": (0.20, 1.0, 0.35),
    "beacon": (1.0, 0.25, 0.10),
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
    coll = bpy.data.collections.new("helicopter")
    scene.collection.children.link(coll)
    m = {name: material(name, c,
                        emit=name in ("lens", "port", "starboard", "beacon"))
         for name, c in PALETTE.items()}

    body(coll, m)
    main_rotor(coll, m)
    tail_rotor(coll, m)

    assembly("seat_pilot", coll, PILOT, point="seat_pilot")
    assembly("seat_passenger", coll, PASSENGER, point="seat_passenger")
    assembly("beacon", coll, V((0, 2.42, -0.95)), point="beacon")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
    print(f"wrote {OUT}: {len(coll.objects)} objects")


def body(coll, m):
    b = assembly("body", coll, V((0, 0, 0)), mesh="body")

    # The cabin: a rounded pod, deepest under the seats, drawn back into
    # the tail boom.
    part(b, "cabin", loft([
        (V((0, 1.02, 2.02)), 0.16, 0.16),
        (V((0, 1.10, 1.86)), 0.50, 0.46),
        (V((0, 1.26, 1.40)), 0.72, 0.66),
        (V((0, 1.36, 0.60)), 0.78, 0.76),
        (V((0, 1.40, -0.20)), 0.76, 0.76),
        (V((0, 1.52, -0.95)), 0.58, 0.60),
        (V((0, 1.66, -1.55)), 0.30, 0.34),
    ], RIGHT, sides=12), m["paint"], levels=2)

    # The canopy bulges out of the front of it, tinted, and down to the
    # doors' windows.
    part(b, "canopy", blob(V((0, 1.50, 0.86)), (0.75, 0.62, 1.08), 16, 10),
         m["glass"], levels=1)
    for s in (-1, 1):
        part(b, f"door_frame{s}", tube([V((s * 0.74, 0.95, -0.28)),
                                        V((s * 0.78, 1.55, -0.30)),
                                        V((s * 0.66, 2.02, -0.25))],
                                       0.03, ref=FORWARD, sides=6),
             m["paint_dark"], levels=0)
        # A white flash along each flank.
        part(b, f"stripe{s}", loft([
            (V((s * 0.765, 1.05, 1.15)), 0.012, 0.05),
            (V((s * 0.80, 1.10, 0.40)), 0.012, 0.07),
            (V((s * 0.77, 1.18, -0.50)), 0.012, 0.07),
            (V((s * 0.56, 1.38, -1.20)), 0.012, 0.05),
        ], UP, sides=6), m["white"], levels=0)

    # The engine and transmission fairing over the cabin's back, with
    # the mast standing out of it and the beacon on its tail.
    part(b, "fairing", blob(V((0, 2.10, -0.45)), (0.44, 0.30, 0.95), 14, 8),
         m["paint"], levels=1)
    part(b, "intake", slab(V((0, 2.16, 0.36)), (0.22, 0.10, 0.10)),
         m["black"], levels=0, bevel=0.03)
    part(b, "exhaust", tube([V((0, 2.05, -1.25)), V((0, 2.08, -1.48))],
                            0.11, sides=10), m["black"], levels=0)
    part(b, "mast", tube([V((0, 2.30, 0.0)), ROTOR_HUB - UP * 0.06],
                         0.07, sides=10), m["steel"], levels=0)
    part(b, "swashplate", loft([(V((0, 2.42, 0.0)), 0.17, 0.17),
                                (V((0, 2.47, 0.0)), 0.17, 0.17)],
                               RIGHT, sides=14), m["steel"], levels=0)
    part(b, "beacon_lamp", blob(V((0, 2.42, -0.95)), (0.05, 0.05, 0.05),
                                8, 4), m["beacon"], levels=0)
    part(b, "landing_light", loft([(V((0, 0.82, 1.70)), 0.07, 0.07),
                                   (V((0, 0.80, 1.74)), 0.06, 0.06)],
                                  FORWARD, sides=12), m["lens"], levels=0)

    # The tail boom, tapering out to the fins and the tail rotor's
    # gearbox.
    part(b, "boom", loft([
        (V((0, 1.68, -1.40)), 0.22, 0.24),
        (V((0, 1.76, -3.60)), 0.15, 0.16),
        (V((0, 1.84, -5.95)), 0.10, 0.11),
    ], RIGHT, sides=10), m["paint"], levels=1)
    part(b, "gearbox", blob(V((0.10, 1.86, -5.95)), (0.14, 0.12, 0.16), 10,
                            6), m["steel"], levels=1)
    part(b, "fin", prism([(1.80, -5.45), (2.75, -6.20), (2.75, -6.42),
                          (1.78, -6.10)], -0.035, 0.035), m["paint"],
         levels=0, bevel=0.015, smooth=False)
    part(b, "fin_tip", prism([(2.60, -6.09), (2.78, -6.23), (2.78, -6.45),
                              (2.60, -6.32)], -0.04, 0.04), m["white"],
         levels=0, bevel=0.01, smooth=False)
    part(b, "ventral_fin", prism([(1.80, -5.55), (1.25, -6.05),
                                  (1.30, -6.20), (1.80, -5.98)],
                                 -0.03, 0.03), m["paint"], levels=0,
         bevel=0.012, smooth=False)
    part(b, "tail_skid", tube([V((0, 1.30, -6.08)), V((0, 1.18, -6.25))],
                              0.02, sides=6), m["steel"], levels=0)
    part(b, "stabilizer", slab(V((0, 1.80, -4.90)), (0.68, 0.025, 0.17)),
         m["paint"], levels=0, bevel=0.012)
    for s, light in ((1, "port"), (-1, "starboard")):
        part(b, f"stabilizer_end{s}", slab(V((s * 0.69, 1.86, -4.90)),
                                           (0.015, 0.09, 0.15)),
             m["white"], levels=0, bevel=0.006)
        part(b, f"nav_light{s}", blob(V((s * 0.71, 1.90, -4.80)),
                                      (0.025, 0.025, 0.035), 6, 4),
             m[light], levels=0)

    # Skids on two arched cross tubes.
    for s in (-1, 1):
        x = s * SKID
        part(b, f"skid{s}", tube([V((x, 0.045, -1.40)),
                                  V((x, 0.045, 1.35)),
                                  V((x, 0.09, 1.62)),
                                  V((x, 0.26, 1.78))], 0.045, ref=RIGHT,
                                 sides=8), m["steel"], levels=0)
        part(b, f"skid_step{s}", slab(V((s * (SKID - 0.02), 0.11, 0.30)),
                                      (0.06, 0.012, 0.22)), m["black"],
             levels=0, bevel=0.006)
    for z in (0.95, -0.72):
        part(b, f"cross_tube{z}", tube([
            V((SKID, 0.05, z)), V((SKID - 0.04, 0.40, z)),
            V((0.78, 0.64, z)), V((-0.78, 0.64, z)),
            V((-SKID + 0.04, 0.40, z)), V((-SKID, 0.05, z)),
        ], 0.04, ref=FORWARD, sides=8), m["steel"], levels=0)

    cockpit(b, m)


def cockpit(b, m):
    """What the pilot sees inside: the floor, the seats, the panel, and
    the controls."""
    part(b, "floor", slab(V((0, 0.66, 0.75)), (0.60, 0.02, 0.95)),
         m["black"], levels=0)
    back = (UP - FORWARD * 0.20).normalized()
    out = RIGHT.cross(back)
    for p, s in ((PILOT, -1), (PASSENGER, 1)):
        part(b, f"cushion{s}", slab(p - UP * 0.07, (0.22, 0.07, 0.24)),
             m["seat"], bevel=0.04)
        part(b, f"seat_back{s}", slab(p + V((0, 0.36, -0.30)),
                                      (0.21, 0.32, 0.05),
                                      axes=(RIGHT, back, out)),
             m["seat"], bevel=0.04)
        part(b, f"seat_base{s}", slab(p - UP * 0.19, (0.18, 0.06, 0.20)),
             m["black"], levels=0)
        # Pedals on the floor ahead of each seat.
        for side in (-1, 1):
            part(b, f"pedal{s}{side}", slab(p + V((side * 0.11, -0.22,
                                                   0.80)),
                                            (0.04, 0.06, 0.015),
                                            axes=(RIGHT, back, out)),
                 m["steel"], levels=0)

    # The instrument panel on its pedestal, its face tipped up toward the
    # seats.
    face = (FORWARD * -1 + UP * 0.45).normalized()
    across = RIGHT
    panel_up = face.cross(across) * -1
    centre = V((0, 1.22, 1.36))
    part(b, "panel", slab(centre, (0.56, 0.13, 0.04),
                          axes=(across, panel_up, face * -1)),
         m["panel"], levels=0, bevel=0.015)
    part(b, "pedestal", slab(V((0, 0.92, 1.26)), (0.10, 0.25, 0.08)),
         m["panel"], levels=0, bevel=0.01)
    dials = []
    for i, x in enumerate((-0.42, -0.26, -0.10, 0.10, 0.26, 0.42)):
        c = centre + across * x + face * 0.045 + panel_up * (
            0.03 if i % 2 else -0.02)
        dials.append(loft([(c, 0.055, 0.055), (c + face * 0.006, 0.05,
                                                0.05)], across, sides=12))
    part(b, "dials", merge(*dials), m["dial"], levels=0)

    # The cyclic stick between the pilot's knees and the collective lever
    # at their left side.
    cyclic = PILOT + V((0, -0.24, 0.42))
    part(b, "cyclic", tube([cyclic, cyclic + V((0, 0.40, -0.06))], 0.016,
                           sides=6), m["black"], levels=0)
    part(b, "cyclic_grip", blob(cyclic + V((0, 0.44, -0.07)),
                                (0.025, 0.06, 0.025), 8, 5), m["black"])
    collective = PILOT + V((0.27, -0.10, -0.05))
    part(b, "collective", tube([collective, collective + V((0, 0.16, 0.42))],
                               0.016, sides=6), m["black"], levels=0)


def main_rotor(coll, m):
    """Two blades on a teetering hub: it turns about the mast (the
    empty's y)."""
    r = assembly("main_rotor", coll, ROTOR_HUB, mesh="main_rotor",
                 point="rotor_hub")
    o = V((0, 0, 0))
    part(r, "hub", blob(o, (0.16, 0.08, 0.16), 10, 6), m["steel"])
    part(r, "cap", blob(o + UP * 0.08, (0.07, 0.07, 0.07), 8, 5),
         m["black"])
    blades = []
    tips = []
    for s in (-1, 1):
        span = ROTOR_RADIUS - 0.25
        blades.append(slab(V((s * (0.25 + span / 2), 0, 0)),
                           (span / 2, 0.022, 0.14)))
        tips.append(slab(V((s * (ROTOR_RADIUS - 0.18), 0.002, 0)),
                         (0.18, 0.024, 0.142)))
        blades.append(slab(V((s * 0.22, 0, 0)), (0.18, 0.04, 0.10)))
    part(r, "blades", merge(*blades), m["rotor"], levels=0, bevel=0.01)
    part(r, "tips", merge(*tips), m["tip"], levels=0, bevel=0.008)


def tail_rotor(coll, m):
    """Two small blades on the boom's left: it turns about its axle (the
    empty's x)."""
    t = assembly("tail_rotor", coll, TAIL_HUB, mesh="tail_rotor",
                 point="tail_hub")
    o = V((0, 0, 0))
    part(t, "hub", loft([(o - RIGHT * 0.04, 0.06, 0.06),
                         (o + RIGHT * 0.06, 0.05, 0.05)], UP, sides=10),
         m["steel"])
    blades, tips = [], []
    for s in (-1, 1):
        span = TAIL_RADIUS - 0.06
        blades.append(slab(V((0.03, s * (0.06 + span / 2), 0)),
                           (0.012, span / 2, 0.06)))
        tips.append(slab(V((0.032, s * (TAIL_RADIUS - 0.08), 0)),
                         (0.014, 0.08, 0.062)))
    part(t, "blades", merge(*blades), m["white"], levels=0, bevel=0.004)
    part(t, "tips", merge(*tips), m["paint"], levels=0, bevel=0.004)


if __name__ == "__main__":
    build()
