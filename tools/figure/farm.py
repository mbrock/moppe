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

Each assembly exports at three levels of detail, so that nothing is drawn
finer than the screen can hold (places.cc picks one by how many pixels a
tier's finest part spans): the whole (`house`), whose finest parts are
some 5 cm -- battens, glazing bars, the coursed stones, the gap between
the barn's doors; `*_mid`, without those, whose finest are some 10 cm --
corner boards, fascia, the door's surround, the braces, the wheel's rims;
and `*_far`, the massing alone, where a window or a door with its
surround is one block in their mean colour. (A base's mid tier is its
far one.)

No two faces lie in one plane where either shows: a part that stands on,
in, or before another stands clear of it by at least 2 cm, or is sunk
into it, and nothing abuts edge to edge where a crack could open.
"""

import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(__file__))
from shapes import (V, RIGHT, UP, FORWARD, Cage, assembly, blob,  # noqa
                    loft, material, merge, part, slab, to_linear, torus,
                    tube)

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


NEAR, MID, FAR = 0, 1, 2
TIERS = (("", NEAR), ("_mid", MID), ("_far", FAR))


def placed(cage, centre, side, up, normal):
    """`cage`, given in a face's own frame (x along `side`, y up, z out
    along `normal`), set at `centre`."""
    out = Cage()
    out.verts = [centre + side * v.x + up * v.y + normal * v.z
                 for v in cage.verts]
    out.faces = [list(f) for f in cage.faces]
    return out


def frame_ring(outer, inner, z0, z1):
    """A rectangular frame in its own plane, one closed solid: the outer
    and inner half extents, from z0 to z1 out of it."""
    cage = Cage()
    for z in (z0, z1):
        for hx, hy in (outer, inner):
            cage.verts += [V((-hx, -hy, z)), V((hx, -hy, z)),
                           V((hx, hy, z)), V((-hx, hy, z))]
    o0, i0, o1, i1 = 0, 4, 8, 12
    for i in range(4):
        j = (i + 1) % 4
        cage.faces.append([o1 + i, o1 + j, i1 + j, i1 + i])
        cage.faces.append([i0 + i, i0 + j, o0 + j, o0 + i])
        cage.faces.append([o0 + i, o0 + j, o1 + j, o1 + i])
        cage.faces.append([i1 + i, i1 + j, i0 + j, i0 + i])
    return cage


def glazing(half, bar, z0, z1):
    """The bars dividing a window of half extents `half` into four panes,
    one cross-shaped solid `bar` wide."""
    w, h, b = half[0], half[1], bar / 2
    return prism([(-b, -h), (b, -h), (b, -b), (w, -b), (w, b), (b, b),
                  (b, h), (-b, h), (-b, b), (-w, b), (-w, -b), (-b, -b)],
                 z0, z1)


def blend(name, weights):
    """A material of the area-weighted mean of PALETTE colours, `weights`
    pairs of (name, area), averaged in linear light: what a pixel too
    coarse to tell them apart sees."""
    total = sum(a for _, a in weights)
    lin = [sum(to_linear(PALETTE[k][c]) * a for k, a in weights) / total
           for c in range(3)]
    shown = tuple(12.92 * c if c <= 0.0031308
                  else 1.055 * c ** (1 / 2.4) - 0.055 for c in lin)
    rough = sum(FINISH[k].get("roughness", 0.8) * a
                for k, a in weights) / total
    return material(name, shown, roughness=rough)


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


def window(asm, name, centre, normal, size, m, tier):
    """A white-framed window of four panes, set in a wall facing `normal`:
    the glass stands 5 cm out of the wall, its glazing bars 2 cm before
    it, and the frame round them 2 cm further, its inside overlapping the
    glass so no edge of the glass shows. Far off, where the frame is
    finer than a pixel, the window is one block in their mean colour."""
    side = UP.cross(normal).normalized()
    w, h = size
    border, bar = 0.07, 0.06
    if tier >= MID:
        ring = (w + 2 * border) * (h + 2 * border) - w * h
        bars = bar * (w + h) - bar * bar
        key = f"window_{w:g}x{h:g}"
        if key not in m:
            m[key] = blend(key, (("white", ring + bars),
                                 ("glass", w * h - bars)))
        part(asm, name, placed(slab(V((0, 0, 0.025)),
                                    (w / 2 + border, h / 2 + border,
                                     0.035)),
                               centre, side, UP, normal),
             m[key], levels=0, smooth=False)
        return
    part(asm, name + "_glass",
         placed(slab(V((0, 0, 0.02)), (w / 2 + 0.02, h / 2 + 0.02, 0.03)),
                centre, side, UP, normal), m["glass"], levels=0,
         smooth=False)
    part(asm, name + "_bars",
         placed(glazing((w / 2 + 0.01, h / 2 + 0.01), bar, 0.0, 0.07),
                centre, side, UP, normal), m["white"], levels=0,
         smooth=False)
    part(asm, name + "_frame",
         placed(frame_ring((w / 2 + border, h / 2 + border), (w / 2, h / 2),
                           -0.01, 0.09),
                centre, side, UP, normal), m["white"], levels=0,
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


def plinth(asm, name, centre, half, rows, seed, mat, tier):
    """A plinth of coursed fieldstones near to; the plain box they are
    laid on from further off, where a stone's 4 cm and the joints between
    them are finer than a pixel."""
    cage = stones(centre, half, rows, seed) if tier == NEAR else \
        slab(centre, half)
    part(asm, name, cage, mat, levels=0, smooth=False)


def mill(coll, m, suffix, tier):
    if tier != MID:
        base = assembly("mill_base" + suffix, coll, V((0, 0, 0)),
                        mesh="mill_base" + suffix)
        plinth(base, "plinth", V((0, 0.2, 0)), (2.2, 0.8, 2.5), 3, 7,
               m["stone"], tier)
    b = assembly("mill" + suffix, coll, V((0, 0, 0)), mesh="mill" + suffix)
    part(b, "walls", slab(V((0, 2.3, 0)), (2.0, 1.3, 2.3)), m["grey_timber"],
         levels=0, smooth=False)
    if tier == NEAR:
        for face, half, count in ((FORWARD, (2.0, 1.3), 12),
                                  (-FORWARD, (2.0, 1.3), 12),
                                  (RIGHT, (2.3, 1.3), 14),
                                  (-RIGHT, (2.3, 1.3), 14)):
            reach = 2.3 if abs(face.z) > 0.5 else 2.0
            boards(b, f"battens{face.x:+.0f}{face.z:+.0f}",
                   V((0, 2.3, 0)) + face * reach, face, half,
                   m["dark_timber"], count)
    gable(b, "gable", 2.0, 2.3, 3.6, 5.3, m["grey_timber"])
    roof(b, "roof", 2.5, 2.75, 3.4, 5.45, 0.09, m["shingle"])
    # The ridge board stands 6 cm over the roof's apex, its foot sunk in
    # the shingles.
    part(b, "ridge", slab(V((0, 5.53, 0)), (0.12, 0.10, 2.8)),
         m["dark_timber"], levels=0, smooth=False)
    # The door on the front, up two stone steps, and a hatch over it.
    part(b, "door", slab(V((0.6, 2.0, 2.33)), (0.5, 0.95, 0.04)),
         m["dark_timber"], levels=0, bevel=0.01, smooth=False)
    # Each step a riser below the one above, the first into the plinth.
    part(b, "steps", merge(slab(V((0.6, 0.66, 2.725)), (0.7, 0.16, 0.275)),
                           slab(V((0.6, 0.3, 3.15)), (0.75, 0.3, 0.25))),
         m["stone_dark"], levels=0, bevel=0.03, smooth=False)
    part(b, "hatch", slab(V((0, 4.3, 2.33)), (0.45, 0.4, 0.04)),
         m["dark_timber"], levels=0, smooth=False)
    window(b, "window", V((2.0, 2.6, -0.8)), RIGHT, (0.6, 0.5), m, tier)
    # The axle's bearing through the wall on the wheel's side.
    part(b, "bearing", slab(V((-2.25, 1.2, 0)), (0.3, 0.2, 0.2)),
         m["dark_timber"], levels=0, smooth=False)
    if tier <= MID:
        part(b, "axle_in", tube([V((-2.0, 1.2, 0)), V((-2.6, 1.2, 0))], 0.1,
                                sides=10), m["dark_timber"], levels=0)

    w = assembly("mill_wheel" + suffix, coll, V((-2.95, 1.2, 0)),
                 mesh="mill_wheel" + suffix,
                 point="mill_axle" if tier == NEAR else None)
    radius, half = 1.75, 0.3
    if tier <= MID:
        rims = [torus(RIGHT * s * half, RIGHT, UP, radius, (0.07, 0.05), 32,
                      4) for s in (-1, 1)]
        part(w, "rims", merge(*rims), m["dark_timber"], levels=0,
             smooth=False)
    if tier == NEAR:
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


def house(coll, m, suffix, tier):
    if tier != MID:
        base = assembly("house_base" + suffix, coll, V((0, 0, 0)),
                        mesh="house_base" + suffix)
        plinth(base, "plinth", V((0, 0.05, 0)), (3.7, 0.55, 2.7), 2, 11,
               m["stone"], tier)
        part(base, "chimney", slab(V((0.6, 3.1, -0.3)), (0.38, 2.9, 0.38)),
             m["stone_dark"], levels=0, bevel=0.02, smooth=False)
        part(base, "chimney_cap",
             slab(V((0.6, 6.05, -0.3)), (0.46, 0.06, 0.46)), m["slate"],
             levels=0, smooth=False)

    b = assembly("house" + suffix, coll, V((0, 0, 0)), mesh="house" + suffix)
    part(b, "walls", slab(V((0, 1.85, 0)), (3.5, 1.25, 2.5)), m["falu"],
         levels=0, smooth=False)
    gable(b, "gable", 2.5, 3.5, 3.1, 4.75, m["falu"], along_x=True)
    if tier == NEAR:
        for face, half, count, reach in ((FORWARD, (3.5, 1.25), 22, 2.5),
                                         (-FORWARD, (3.5, 1.25), 22, 2.5),
                                         (RIGHT, (2.5, 1.25), 16, 3.5),
                                         (-RIGHT, (2.5, 1.25), 16, 3.5)):
            boards(b, f"battens{face.x:+.0f}{face.z:+.0f}",
                   V((0, 1.85, 0)) + face * reach, face, half, m["falu"],
                   count)
    if tier <= MID:
        # White corner boards 8 cm out of each wall, and the fascia under
        # the eaves 3 cm before the battens.
        corners = []
        for sx in (-1, 1):
            for sz in (-1, 1):
                corners.append(slab(V((sx * 3.5, 1.85, sz * 2.5)),
                                    (0.08, 1.27, 0.08)))
        corners.append(slab(V((0, 3.12, 2.52)), (3.55, 0.06, 0.05)))
        corners.append(slab(V((0, 3.12, -2.52)), (3.55, 0.06, 0.05)))
        part(b, "corners", merge(*corners), m["white"], levels=0,
             smooth=False)
    roof(b, "roof", 3.0, 3.85, 2.95, 4.9, 0.1, m["slate"], along_x=True)
    part(b, "ridge", slab(V((0, 4.99, 0)), (3.9, 0.09, 0.1)), m["slate"],
         levels=0, smooth=False)
    # The front: a door between two windows, under a little porch roof.
    # The door stands 10 cm out of the wall and its white surround 7 cm,
    # both clear of the battens' 4 cm; far off they are one block.
    if tier <= MID:
        part(b, "door", slab(V((-0.9, 1.6, 2.54)), (0.45, 1.0, 0.06)),
             m["dark_timber"], levels=0, bevel=0.01, smooth=False)
        part(b, "door_frame", slab(V((-0.9, 1.65, 2.53)), (0.55, 1.08, 0.04)),
             m["white"], levels=0, smooth=False)
    else:
        if "door_block" not in m:
            m["door_block"] = blend("door_block",
                                    (("dark_timber", 0.9 * 2.0),
                                     ("white", 1.1 * 2.16 - 0.9 * 2.0)))
        part(b, "door", slab(V((-0.9, 1.65, 2.54)), (0.55, 1.08, 0.06)),
             m["door_block"], levels=0, smooth=False)
    part(b, "porch", slab(V((-0.9, 2.85, 2.9)), (0.8, 0.04, 0.45),
                          axes=(RIGHT, (UP + FORWARD * 0.3).normalized(),
                                (FORWARD - UP * 0.3).normalized())),
         m["slate"], levels=0, smooth=False)
    # The step a riser below the threshold, from the ground and into the
    # plinth.
    part(b, "step", slab(V((-0.9, 0.11, 2.95)), (0.6, 0.31, 0.3)),
         m["stone_dark"], levels=0, bevel=0.03, smooth=False)
    for i, x in enumerate((-2.5, 0.6, 2.3)):
        window(b, f"front{i}", V((x, 2.0, 2.5)), FORWARD, (0.8, 0.95), m,
               tier)
        window(b, f"back{i}", V((x, 2.0, -2.5)), -FORWARD, (0.8, 0.95), m,
               tier)
    for s in (-1, 1):
        window(b, f"end{s}", V((s * 3.5, 2.0, 0)), RIGHT * s, (0.8, 0.95), m,
               tier)
        window(b, f"loft{s}", V((s * 3.5, 3.8, 0)), RIGHT * s, (0.5, 0.5), m,
               tier)


def barn(coll, m, suffix, tier):
    if tier != MID:
        base = assembly("barn_base" + suffix, coll, V((0, 0, 0)),
                        mesh="barn_base" + suffix)
        plinth(base, "footing", V((0, -0.05, 0)), (4.6, 0.45, 3.1), 2, 23,
               m["stone"], tier)

    b = assembly("barn" + suffix, coll, V((0, 0, 0)), mesh="barn" + suffix)
    part(b, "walls", slab(V((0, 2.2, 0)), (4.4, 1.8, 2.9)), m["falu_old"],
         levels=0, smooth=False)
    gable(b, "gable", 2.9, 4.4, 4.0, 6.1, m["falu_old"], along_x=True)
    if tier == NEAR:
        for face, half, count, reach in ((FORWARD, (4.4, 1.8), 30, 2.9),
                                         (-FORWARD, (4.4, 1.8), 30, 2.9),
                                         (RIGHT, (2.9, 1.8), 20, 4.4),
                                         (-RIGHT, (2.9, 1.8), 20, 4.4)):
            boards(b, f"battens{face.x:+.0f}{face.z:+.0f}",
                   V((0, 2.2, 0)) + face * reach, face, half, m["falu_old"],
                   count)
    roof(b, "roof", 3.4, 4.75, 3.85, 6.25, 0.1, m["boards"], along_x=True)
    part(b, "ridge", slab(V((0, 6.36, 0)), (4.8, 0.10, 0.12)),
         m["dark_timber"], levels=0, smooth=False)
    # The broad double doors in the middle of the front, braced, and a
    # loft hatch in the gable. The 4 cm between the leaves shows only near
    # to; further off they are one.
    if tier == NEAR:
        doors = [slab(V((s * 0.8, 1.75, 2.93)), (0.78, 1.35, 0.05))
                 for s in (-1, 1)]
    else:
        doors = [slab(V((0, 1.75, 2.93)), (1.58, 1.35, 0.05))]
    part(b, "doors", merge(*doors), m["dark_timber"], levels=0, bevel=0.01,
         smooth=False)
    if tier <= MID:
        # The rails 3 cm before the doors, and the diagonal 2 cm before the
        # rails it crosses.
        braces = []
        for s in (-1, 1):
            d = (UP * 2.5 + RIGHT * s * 1.4).normalized()
            braces.append(slab(V((s * 0.8, 1.75, 3.0)), (0.06, 1.4, 0.03),
                               axes=(d.cross(FORWARD), d, FORWARD)))
            braces.append(slab(V((s * 0.8, 2.95, 2.99)), (0.74, 0.06, 0.02)))
            braces.append(slab(V((s * 0.8, 0.55, 2.99)), (0.74, 0.06, 0.02)))
        part(b, "braces", merge(*braces), m["white"], levels=0, smooth=False)
    # The ramp up to the doors, its top 6 cm under the footing's and its
    # back sunk in it.
    part(b, "ramp", slab(V((0, 0.17, 3.55)), (1.7, 0.17, 0.55)),
         m["stone_dark"], levels=0, bevel=0.04, smooth=False)
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
    for suffix, tier in TIERS:
        mill(coll, m, suffix, tier)
        house(coll, m, suffix, tier)
        barn(coll, m, suffix, tier)
    # Laid out side by side for the preview, each tier behind the last;
    # the game places each.
    for name, x in (("mill", 0.0), ("house", 10.0), ("barn", 22.0)):
        for k, (suffix, _) in enumerate(TIERS):
            for part_name in (name, name + "_base", name + "_wheel"):
                obj = bpy.data.objects.get(part_name + suffix)
                if obj is not None:
                    obj.location.x += x
                    obj.location.y += 14.0 * k
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
    print(f"wrote {OUT}: {len(coll.objects)} objects")


build()
