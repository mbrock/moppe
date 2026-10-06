"""Builds models/glider.blend: the hang glider.

    Blender -b --factory-startup --python tools/figure/glider.py

Authored in the glider's frame at world scale (x right, y up, z forward,
metres, origin at the glider's position). A Rogallo wing: bowed white
leading edges, a keel and crossbar, a king post with its upper rigging,
and a billowed sail of striped panels whose underside is a darker skin;
below hangs the A-frame control bar with its lower rigging and the hang
strap. The `glider` assembly is always drawn; `tether`, the lines down
to an attached bike, only while one hangs there. Points mark where the
pilot's harness hangs and where the hands hold the basebar.
"""

import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(__file__))
from shapes import (V, RIGHT, UP, FORWARD, Cage, assembly,  # noqa: E402
                    material, merge, part, tube)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "models", "glider.blend")

NOSE = V((0, 0.12, 1.8))
TAIL = V((0, 0.08, -2.0))
TIP = V((4.6, 0.12, -0.8))
KEEL_HANG = V((0, 0.06, -0.3))
APEX = V((0, 0.06, -0.15))
KING_POST_TOP = V((0, 0.9, -0.25))
BASEBAR_END = V((0.75, -1.38, 0.42))
HARNESS = V((0, -1.12, -0.42))

# Sail panels from the keel out to the tips: (outer edge as a fraction of
# the half span, top colour, underside colour).
PANELS = [
    (0.12, "white", "white_under"),
    (0.38, "blue", "blue_under"),
    (0.5, "mustard", "mustard_under"),
    (0.62, "red", "red_under"),
    (1.0, "blue", "blue_under"),
]

PALETTE = {
    "white": (0.92, 0.92, 0.88),
    "blue": (0.15, 0.45, 0.95),
    "mustard": (0.92, 0.68, 0.18),
    "red": (0.80, 0.22, 0.13),
    "white_under": (0.62, 0.64, 0.66),
    "blue_under": (0.08, 0.22, 0.48),
    "mustard_under": (0.55, 0.40, 0.12),
    "red_under": (0.48, 0.13, 0.08),
    "tube": (0.86, 0.88, 0.90),
    "alloy": (0.62, 0.64, 0.68),
    "black": (0.08, 0.08, 0.10),
    "wire": (0.20, 0.20, 0.22),
    "strap": (0.22, 0.17, 0.12),
}


def leading_edge(t, s):
    """A point along the leading edge, t from nose (0) to tip (1), bowed
    forward and up a little between."""
    tip = V((TIP.x * s, TIP.y, TIP.z))
    bow = math.sin(math.pi * t)
    return NOSE.lerp(tip, t) + V((0, 0.06 * bow, 0.18 * bow))


def trailing_edge(t, s):
    """The trailing edge from the keel's tail (0) to the tip (1), its
    scallop curving forward between."""
    tip = V((TIP.x * s, TIP.y, TIP.z))
    return TAIL.lerp(tip, t) + V((0, 0.0, 0.45 * math.sin(math.pi * t)))


def sail_point(u, v):
    """u across the span (-1..1), v along the chord (0 leading edge)."""
    s = 1 if u >= 0 else -1
    t = abs(u)
    p = leading_edge(t, s).lerp(trailing_edge(t, s), v)
    billow = 0.22 * math.sin(math.pi * v) * (1 - t ** 2) ** 0.5
    return p + UP * (0.05 + billow)


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    coll = bpy.data.collections.new("glider")
    scene.collection.children.link(coll)
    m = {name: material(name, c) for name, c in PALETTE.items()}

    g = assembly("glider", coll, mesh="glider")
    sail(g, m)

    # Airframe: keel, bowed leading edges, crossbar, king post.
    part(g, "keel", tube([NOSE, TAIL], 0.032, sides=8), m["alloy"])
    for s in (-1, 1):
        part(g, f"leading_edge{s}",
             tube([leading_edge(i / 8, s) for i in range(9)], 0.045,
                  sides=8), m["tube"])
        cross = leading_edge(0.5, s)
        part(g, f"crossbar{s}", tube([V((0, 0.02, -0.05)),
                                      cross - UP * 0.02], 0.03, sides=8),
             m["alloy"])
    part(g, "king_post", tube([V((0, 0.1, -0.25)), KING_POST_TOP], 0.025,
                              sides=8), m["alloy"])
    part(g, "nose_cone", tube([NOSE + FORWARD * 0.02, NOSE - FORWARD * 0.18],
                              0.06, sides=10), m["black"])

    # Control frame: two downtubes from the apex to a basebar, held by
    # rigging to the nose, tail, and leading edges; the upper rigging
    # runs from the king post's top.
    for s in (-1, 1):
        end = V((BASEBAR_END.x * s, BASEBAR_END.y, BASEBAR_END.z))
        part(g, f"downtube{s}", tube([APEX, end], 0.022, sides=8),
             m["alloy"])
    part(g, "basebar", tube([V((-BASEBAR_END.x, BASEBAR_END.y,
                                BASEBAR_END.z)), BASEBAR_END], 0.022,
                            sides=8), m["black"])
    wires = []
    for s in (-1, 1):
        end = V((BASEBAR_END.x * s, BASEBAR_END.y, BASEBAR_END.z))
        for to in (NOSE, TAIL, leading_edge(0.55, s), leading_edge(1.0, s)):
            wires.append(tube([end, to], 0.005, sides=4))
        for to in (leading_edge(0.55, s), leading_edge(1.0, s)):
            wires.append(tube([KING_POST_TOP, to], 0.005, sides=4))
    for to in (NOSE, TAIL):
        wires.append(tube([KING_POST_TOP, to], 0.005, sides=4))
    part(g, "rigging", merge(*wires), m["wire"], levels=0)
    part(g, "hang_strap", tube([KEEL_HANG, HARNESS], 0.012, sides=4),
         m["strap"], levels=0)

    t = assembly("tether", coll, mesh="tether")
    lines = [tube([HARNESS, V((0.45 * s, -2.25, -0.25))], 0.012, sides=4)
             for s in (-1, 1)]
    part(t, "lines", merge(*lines), m["wire"], levels=0)

    assembly("harness", coll, HARNESS, point="harness")
    for s, tag in ((-1, "left"), (1, "right")):
        assembly(f"grip_{tag}", coll,
                 V((0.36 * s, BASEBAR_END.y, BASEBAR_END.z)),
                 point=f"grip_{tag}")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
    print(f"wrote {OUT}: {len(coll.objects)} objects")


def sail(g, m):
    """The sail as a grid, a material per panel, thickened by a Solidify
    modifier whose inner skin takes the darker underside colours."""
    columns = 40
    rows = 10
    cage = Cage()
    us = [-1 + 2 * i / columns for i in range(columns + 1)]
    for u in us:
        for j in range(rows + 1):
            cage.verts.append(sail_point(u, j / rows))
    faces, panels = [], []
    for i in range(columns):
        mid = abs(0.5 * (us[i] + us[i + 1]))
        panel = next(k for k, (edge, _, _) in enumerate(PANELS)
                     if mid <= edge)
        for j in range(rows):
            a = i * (rows + 1) + j
            b = (i + 1) * (rows + 1) + j
            faces.append([a, b, b + 1, a + 1])
            panels.append(panel)
    cage.faces = faces
    cage.orient(lambda p: p - UP)

    obj = part(g, "sail", cage, m[PANELS[0][1]], levels=1)
    mesh = obj.data
    mesh.materials.clear()
    for _, top, _ in PANELS:
        mesh.materials.append(m[top])
    for _, _, under in PANELS:
        mesh.materials.append(m[under])
    mesh.polygons.foreach_set("material_index", panels)
    solid = obj.modifiers.new("skin", "SOLIDIFY")
    solid.thickness = 0.02
    solid.offset = -1
    solid.use_rim = True
    solid.material_offset = len(PANELS)
    solid.material_offset_rim = len(PANELS)
    # Thicken before smoothing so both skins subdivide together.
    bpy.context.view_layer.objects.active = obj
    with bpy.context.temp_override(object=obj):
        bpy.ops.object.modifier_move_to_index(modifier="skin", index=0)


build()
