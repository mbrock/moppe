"""Builds models/farm.blend: the little farm by a lake's outlet.

    Blender -b --factory-startup --python tools/figure/farm.py

Three buildings of the old Nordic kind, each authored life size in the
game's frame -- x to the left of the way its front faces, y up, z out of
its front -- with its origin on the ground at its middle:

  - the mill: grey timber on a stone plinth, its undershot wheel on the
    -x side, which stands in the stream (the wheel is its own assembly,
    turning about its axle, the point `mill_axle`);
  - the house: a Falu-red board cottage with white corners and windows on
    a stone plinth, its stone chimney through the ridge;
  - the barn: larger, of red boards gone brown, with broad doors.

Each building's stone base (`*_base`) exports apart from the rest, so a
farm long left can be drawn as the plinths and chimney stack that stand
after the timber has gone. Rebuilding discards hand edits to the .blend.
"""

import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(__file__))
from shapes import (V, RIGHT, UP, FORWARD, Cage, assembly, blob,  # noqa
                    loft, material, merge, part, slab, torus, tube)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "models", "farm.blend")

PALETTE = {
    "stone": (0.52, 0.51, 0.48),
    "stone_dark": (0.36, 0.35, 0.33),
    "grey_timber": (0.47, 0.43, 0.38),
    "dark_timber": (0.27, 0.23, 0.19),
    "falu": (0.55, 0.14, 0.10),
    "falu_old": (0.42, 0.17, 0.12),
    "white": (0.88, 0.86, 0.80),
    "glass": (0.10, 0.13, 0.15),
    "shingle": (0.24, 0.21, 0.19),
    "slate": (0.20, 0.21, 0.23),
    "boards": (0.34, 0.30, 0.26),
    "iron": (0.20, 0.20, 0.21),
}

FINISH = {
    "stone": dict(roughness=0.95),
    "stone_dark": dict(roughness=0.95),
    "grey_timber": dict(roughness=0.9),
    "dark_timber": dict(roughness=0.85),
    "falu": dict(roughness=0.92),
    "falu_old": dict(roughness=0.95),
    "white": dict(roughness=0.6),
    "glass": dict(roughness=0.05, coat=0.8),
    "shingle": dict(roughness=0.9),
    "slate": dict(roughness=0.7),
    "boards": dict(roughness=0.92),
    "iron": dict(metallic=0.7, roughness=0.6),
}


def prism(profile, z0, z1):
    """The polygon `profile`, given as (x, y) pairs, swept from z0 to z1."""
    cage = Cage()
    area = sum(a[0] * b[1] - a[1] * b[0]
               for a, b in zip(profile, profile[1:] + profile[:1]))
    if area < 0:
        profile = list(reversed(profile))
    n = len(profile)
    for z in (z0, z1):
        cage.verts += [V((x, y, z)) for x, y in profile]
    cage.faces.append(list(reversed(range(n))))
    cage.faces.append(list(range(n, 2 * n)))
    for i in range(n):
        j = (i + 1) % n
        cage.faces.append([i, j, n + j, n + i])
    centre = V((sum(x for x, _ in profile) / n, sum(y for _, y in profile) / n,
                (z0 + z1) / 2))
    cage.orient(lambda p: centre)
    return cage


def turned(cage):
    """`cage` turned a quarter about up, so that what ran along z runs
    along x."""
    out = Cage()
    out.verts = [V((v.z, v.y, -v.x)) for v in cage.verts]
    out.faces = [list(f) for f in cage.faces]
    return out


def roof(asm, name, half_span, half_length, eave, ridge, thick, mat,
         along_x=False):
    """A pitched roof of two planes meeting at the ridge, which runs along
    z (or x), overhanging the walls by what `half_span` and `half_length`
    give beyond them."""
    panels = []
    for s in (-1, 1):
        a = V((s * half_span, eave, 0))
        b = V((0, ridge, 0))
        down = (a - b).normalized()
        normal = FORWARD.cross(down) * s
        if normal.y < 0:
            normal = -normal
        centre = (a + b) / 2 + normal * (thick / 2)
        panels.append(slab(centre, ((a - b).length / 2, thick / 2,
                                    half_length),
                           axes=(down, normal, FORWARD)))
    cage = merge(*panels)
    part(asm, name, turned(cage) if along_x else cage, mat, levels=0,
         bevel=0.02, smooth=False)


def gable(asm, name, half_width, half_length, eave, ridge, mat,
          along_x=False):
    """The walls' gable ends, as one wedge under the roof."""
    cage = prism([(-half_width, eave), (half_width, eave), (0, ridge)],
                 -half_length, half_length)
    part(asm, name, turned(cage) if along_x else cage, mat, levels=0,
         smooth=False)


def window(asm, name, centre, normal, size, m):
    """A white-framed window of four panes, set in a wall facing
    `normal`."""
    side = UP.cross(normal).normalized()
    w, h = size
    part(asm, name + "_frame",
         slab(centre + normal * 0.03, (w / 2 + 0.07, h / 2 + 0.07, 0.04),
              axes=(side, UP, normal)), m["white"], levels=0, bevel=0.01,
         smooth=False)
    panes = []
    for i in (-1, 1):
        for j in (-1, 1):
            panes.append(slab(centre + normal * 0.075 + side * (i * w / 4) +
                              UP * (j * h / 4),
                              (w / 4 - 0.03, h / 4 - 0.03, 0.01),
                              axes=(side, UP, normal)))
    part(asm, name + "_glass", merge(*panes), m["glass"], levels=0,
         smooth=False)


def boards(asm, name, centre, normal, half, mat, count):
    """Vertical battens over a wall face, for the board-and-batten
    look."""
    side = UP.cross(normal).normalized()
    battens = []
    for i in range(count):
        t = -1 + 2 * (i + 0.5) / count
        battens.append(slab(centre + normal * 0.02 + side * (t * half[0]),
                            (0.03, half[1], 0.02),
                            axes=(side, UP, normal)))
    part(asm, name, merge(*battens), mat, levels=0, smooth=False)


def stones(centre, half, rows, seed):
    """A plinth of coursed fieldstones: a box with stones standing a
    little proud of its faces."""
    out = [slab(centre, half)]
    rnd = seed
    for face in (RIGHT, -RIGHT, FORWARD, -FORWARD):
        side = UP.cross(face).normalized()
        reach = abs(half[0] * face.x) + abs(half[2] * face.z)
        span = abs(half[0] * side.x) + abs(half[2] * side.z)
        for r in range(rows):
            y = centre.y - half[1] + (r + 0.5) * 2 * half[1] / rows
            count = max(2, int(span / 0.32))
            for i in range(count):
                rnd = (rnd * 1103515245 + 12345) & 0x7fffffff
                j = (rnd % 1000) / 1000.0
                t = -1 + 2 * (i + 0.5 + 0.3 * (r % 2)) / count
                if abs(t) > 1:
                    continue
                c = centre + face * (reach + 0.02) + side * (t * span)
                c.y = y
                out.append(slab(c, (span / count * (0.42 + 0.08 * j),
                                    half[1] / rows * 0.42, 0.04),
                                axes=(side, UP, face)))
    return merge(*out)


def mill(coll, m):
    base = assembly("mill_base", coll, V((0, 0, 0)), mesh="mill_base")
    part(base, "plinth", stones(V((0, 0.2, 0)), (2.2, 0.8, 2.5), 3, 7),
         m["stone"], levels=0, smooth=False)
    b = assembly("mill", coll, V((0, 0, 0)), mesh="mill")
    part(b, "walls", slab(V((0, 2.3, 0)), (2.0, 1.3, 2.3)), m["grey_timber"],
         levels=0, smooth=False)
    for face, half, count in ((FORWARD, (2.0, 1.3), 12),
                              (-FORWARD, (2.0, 1.3), 12),
                              (RIGHT, (2.3, 1.3), 14),
                              (-RIGHT, (2.3, 1.3), 14)):
        reach = 2.3 if abs(face.z) > 0.5 else 2.0
        boards(b, f"battens{face.x:+.0f}{face.z:+.0f}",
               V((0, 2.3, 0)) + face * reach, face, half, m["dark_timber"],
               count)
    gable(b, "gable", 2.0, 2.3, 3.6, 5.3, m["grey_timber"])
    roof(b, "roof", 2.5, 2.75, 3.4, 5.45, 0.09, m["shingle"])
    part(b, "ridge", slab(V((0, 5.5, 0)), (0.12, 0.08, 2.8)),
         m["dark_timber"], levels=0, smooth=False)
    # The door on the front, up two stone steps, and a hatch over it.
    part(b, "door", slab(V((0.6, 2.0, 2.33)), (0.5, 0.95, 0.04)),
         m["dark_timber"], levels=0, bevel=0.01, smooth=False)
    part(b, "steps", merge(slab(V((0.6, 0.75, 2.75)), (0.7, 0.25, 0.25)),
                           slab(V((0.6, 0.3, 3.15)), (0.75, 0.3, 0.25))),
         m["stone_dark"], levels=0, bevel=0.03, smooth=False)
    part(b, "hatch", slab(V((0, 4.3, 2.33)), (0.45, 0.4, 0.04)),
         m["dark_timber"], levels=0, smooth=False)
    window(b, "window", V((2.0, 2.6, -0.8)), RIGHT, (0.6, 0.5), m)
    # The axle's bearing through the wall on the wheel's side.
    part(b, "bearing", slab(V((-2.25, 1.2, 0)), (0.3, 0.2, 0.2)),
         m["dark_timber"], levels=0, smooth=False)
    part(b, "axle_in", tube([V((-2.0, 1.2, 0)), V((-2.6, 1.2, 0))], 0.1,
                            sides=10), m["dark_timber"], levels=0)

    w = assembly("mill_wheel", coll, V((-2.95, 1.2, 0)), mesh="mill_wheel",
                 point="mill_axle")
    radius, half = 1.75, 0.3
    rims = [torus(RIGHT * s * half, RIGHT, UP, radius, (0.07, 0.05), 32, 4)
            for s in (-1, 1)]
    part(w, "rims", merge(*rims), m["dark_timber"], levels=0, smooth=False)
    spokes = []
    for i in range(8):
        a = 2 * math.pi * i / 8
        out = UP * math.cos(a) + FORWARD * math.sin(a)
        for s in (-1, 1):
            spokes.append(slab(RIGHT * s * half + out * (radius / 2),
                               (0.04, radius / 2, 0.05),
                               axes=(RIGHT, out, RIGHT.cross(out))))
    part(w, "spokes", merge(*spokes), m["grey_timber"], levels=0,
         smooth=False)
    paddles = []
    for i in range(16):
        a = 2 * math.pi * (i + 0.5) / 16
        out = UP * math.cos(a) + FORWARD * math.sin(a)
        paddles.append(slab(out * (radius - 0.12), (half + 0.04, 0.26, 0.025),
                            axes=(RIGHT, out, RIGHT.cross(out))))
    part(w, "paddles", merge(*paddles), m["grey_timber"], levels=0,
         smooth=False)
    part(w, "hub", tube([RIGHT * (half + 0.1), RIGHT * -(half + 0.1)], 0.16,
                        sides=10), m["dark_timber"], levels=0)
    part(w, "shaft", tube([RIGHT * (half + 0.1), RIGHT * 0.4 + RIGHT * half],
                          0.09, sides=8), m["iron"], levels=0)


def house(coll, m):
    base = assembly("house_base", coll, V((0, 0, 0)), mesh="house_base")
    part(base, "plinth", stones(V((0, 0.05, 0)), (3.7, 0.55, 2.7), 2, 11),
         m["stone"], levels=0, smooth=False)
    part(base, "chimney", slab(V((0.6, 3.1, -0.3)), (0.38, 2.9, 0.38)),
         m["stone_dark"], levels=0, bevel=0.02, smooth=False)
    part(base, "chimney_cap", slab(V((0.6, 6.05, -0.3)), (0.46, 0.06, 0.46)),
         m["slate"], levels=0, smooth=False)

    b = assembly("house", coll, V((0, 0, 0)), mesh="house")
    part(b, "walls", slab(V((0, 1.85, 0)), (3.5, 1.25, 2.5)), m["falu"],
         levels=0, smooth=False)
    gable(b, "gable", 2.5, 3.5, 3.1, 4.75, m["falu"], along_x=True)
    for face, half, count, reach in ((FORWARD, (3.5, 1.25), 22, 2.5),
                                     (-FORWARD, (3.5, 1.25), 22, 2.5),
                                     (RIGHT, (2.5, 1.25), 16, 3.5),
                                     (-RIGHT, (2.5, 1.25), 16, 3.5)):
        boards(b, f"battens{face.x:+.0f}{face.z:+.0f}",
               V((0, 1.85, 0)) + face * reach, face, half, m["falu"], count)
    corners = []
    for sx in (-1, 1):
        for sz in (-1, 1):
            corners.append(slab(V((sx * 3.5, 1.85, sz * 2.5)),
                                (0.08, 1.27, 0.08)))
    corners.append(slab(V((0, 3.12, 2.52)), (3.55, 0.06, 0.05)))
    corners.append(slab(V((0, 3.12, -2.52)), (3.55, 0.06, 0.05)))
    part(b, "corners", merge(*corners), m["white"], levels=0, smooth=False)
    roof(b, "roof", 3.0, 3.85, 2.95, 4.9, 0.1, m["slate"], along_x=True)
    part(b, "ridge", slab(V((0, 4.95, 0)), (3.9, 0.07, 0.1)), m["slate"],
         levels=0, smooth=False)
    # The front: a door between two windows, under a little porch roof.
    part(b, "door", slab(V((-0.9, 1.6, 2.52)), (0.45, 1.0, 0.04)),
         m["dark_timber"], levels=0, bevel=0.01, smooth=False)
    part(b, "door_frame", slab(V((-0.9, 1.65, 2.5)), (0.55, 1.08, 0.03)),
         m["white"], levels=0, smooth=False)
    part(b, "porch", slab(V((-0.9, 2.85, 2.9)), (0.8, 0.04, 0.45),
                          axes=(RIGHT, (UP + FORWARD * 0.3).normalized(),
                                (FORWARD - UP * 0.3).normalized())),
         m["slate"], levels=0, smooth=False)
    part(b, "step", slab(V((-0.9, 0.45, 2.95)), (0.6, 0.15, 0.35)),
         m["stone_dark"], levels=0, bevel=0.03, smooth=False)
    for i, x in enumerate((-2.5, 0.6, 2.3)):
        window(b, f"front{i}", V((x, 2.0, 2.5)), FORWARD, (0.8, 0.95), m)
        window(b, f"back{i}", V((x, 2.0, -2.5)), -FORWARD, (0.8, 0.95), m)
    for s in (-1, 1):
        window(b, f"end{s}", V((s * 3.5, 2.0, 0)), RIGHT * s, (0.8, 0.95), m)
        window(b, f"loft{s}", V((s * 3.5, 3.8, 0)), RIGHT * s, (0.5, 0.5), m)


def barn(coll, m):
    base = assembly("barn_base", coll, V((0, 0, 0)), mesh="barn_base")
    part(base, "footing", stones(V((0, -0.05, 0)), (4.6, 0.45, 3.1), 2, 23),
         m["stone"], levels=0, smooth=False)

    b = assembly("barn", coll, V((0, 0, 0)), mesh="barn")
    part(b, "walls", slab(V((0, 2.2, 0)), (4.4, 1.8, 2.9)), m["falu_old"],
         levels=0, smooth=False)
    gable(b, "gable", 2.9, 4.4, 4.0, 6.1, m["falu_old"], along_x=True)
    for face, half, count, reach in ((FORWARD, (4.4, 1.8), 30, 2.9),
                                     (-FORWARD, (4.4, 1.8), 30, 2.9),
                                     (RIGHT, (2.9, 1.8), 20, 4.4),
                                     (-RIGHT, (2.9, 1.8), 20, 4.4)):
        boards(b, f"battens{face.x:+.0f}{face.z:+.0f}",
               V((0, 2.2, 0)) + face * reach, face, half, m["falu_old"],
               count)
    roof(b, "roof", 3.4, 4.75, 3.85, 6.25, 0.1, m["boards"], along_x=True)
    part(b, "ridge", slab(V((0, 6.3, 0)), (4.8, 0.08, 0.12)),
         m["dark_timber"], levels=0, smooth=False)
    # The broad double doors in the middle of the front, braced, and a
    # loft hatch in the gable.
    doors = []
    for s in (-1, 1):
        doors.append(slab(V((s * 0.8, 1.75, 2.93)), (0.78, 1.35, 0.05)))
    part(b, "doors", merge(*doors), m["dark_timber"], levels=0, bevel=0.01,
         smooth=False)
    braces = []
    for s in (-1, 1):
        d = (UP * 2.5 + RIGHT * s * 1.4).normalized()
        braces.append(slab(V((s * 0.8, 1.75, 2.99)), (0.06, 1.4, 0.02),
                           axes=(d.cross(FORWARD), d, FORWARD)))
        braces.append(slab(V((s * 0.8, 2.95, 2.99)), (0.74, 0.06, 0.02)))
        braces.append(slab(V((s * 0.8, 0.55, 2.99)), (0.74, 0.06, 0.02)))
    part(b, "braces", merge(*braces), m["white"], levels=0, smooth=False)
    part(b, "ramp", slab(V((0, 0.2, 3.5)), (1.7, 0.2, 0.6)), m["stone_dark"],
         levels=0, bevel=0.04, smooth=False)
    for s in (-1, 1):
        part(b, f"hatch{s}", slab(V((s * 4.43, 4.6, 0)), (0.04, 0.55, 0.6)),
             m["dark_timber"], levels=0, smooth=False)


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    coll = bpy.data.collections.new("farm")
    scene.collection.children.link(coll)
    m = {name: material(name, c, **FINISH[name])
         for name, c in PALETTE.items()}
    mill(coll, m)
    house(coll, m)
    barn(coll, m)
    # Laid out side by side for the preview; the game places each.
    for name, x in (("house", 10.0), ("barn", 22.0)):
        for suffix in ("", "_base"):
            bpy.data.objects[name + suffix].location.x = x
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
    print(f"wrote {OUT}: {len(coll.objects)} objects")


build()
