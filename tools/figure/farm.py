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
    "hay": (0.72, 0.62, 0.36),
    "straw": (0.70, 0.62, 0.42),
    "swill": (0.32, 0.26, 0.18),
    "shadow": (0.06, 0.05, 0.045),
    "old_boards": (0.42, 0.36, 0.29),
    "daylight": (0.80, 0.84, 0.88),
    "lamplight": (1.0, 0.78, 0.42),
    "tin": (0.66, 0.67, 0.68),
    "water": (0.12, 0.15, 0.16),
    "sacking": (0.62, 0.55, 0.42),
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
    "hay": dict(roughness=0.95, sheen=0.3),
    "straw": dict(roughness=0.95, sheen=0.3),
    "swill": dict(roughness=0.3),
    "shadow": dict(roughness=1.0),
    "old_boards": dict(roughness=0.95),
    "daylight": dict(roughness=1.0),
    "lamplight": dict(roughness=0.2, emission=4.0),
    "tin": dict(metallic=0.8, roughness=0.4),
    "water": dict(roughness=0.05, coat=0.6),
    "sacking": dict(roughness=1.0, sheen=0.4),
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


def boards(asm, name, centre, normal, half, mat, count, gap=None):
    """Vertical battens over a wall face, for the board-and-batten
    look; none across `gap`, a (low, high) span along the face's side."""
    side = UP.cross(normal).normalized()
    battens = []
    for i in range(count):
        t = -1 + 2 * (i + 0.5) / count
        if gap and gap[0] - 0.05 < t * half[0] < gap[1] + 0.05:
            continue
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
    # The walls are 12 cm of boards round one room that can be walked
    # into, through the doorway at the front (x -1.35 to -0.45, 2 m high);
    # the end walls run into the long ones rather than meeting them edge to
    # edge.
    t = 0.12
    walls = [slab(V((0, 1.85, -2.5 + t / 2)), (3.5, 1.25, t / 2)),
             slab(V((-3.5 + t / 2, 1.85, 0)), (t / 2, 1.25, 2.5 - t / 2)),
             slab(V((3.5 - t / 2, 1.85, 0)), (t / 2, 1.25, 2.5 - t / 2)),
             slab(V(((-3.5 - 1.35) / 2, 1.85, 2.5 - t / 2)),
                  ((3.5 - 1.35) / 2, 1.25, t / 2)),
             slab(V(((3.5 - 0.45) / 2, 1.85, 2.5 - t / 2)),
                  ((3.5 + 0.45) / 2, 1.25, t / 2)),
             slab(V((-0.9, 2.85, 2.5 - t / 2)), (0.45, 0.25, t / 2))]
    part(b, "walls", merge(*walls), m["falu"], levels=0, smooth=False)
    # Lined inside with planed boards, 3 cm proud of the walls' inner
    # faces and sunk into them, the doorway left open; the chimney stack
    # stands in the room, the hearth of it.
    i = 2.5 - t + 0.005
    j = 3.5 - t + 0.005
    lining = [slab(V((0, 1.85, -i)), (j, 1.25, 0.015)),
              slab(V((-j, 1.85, 0)), (0.015, 1.25, i)),
              slab(V((j, 1.85, 0)), (0.015, 1.25, i)),
              slab(V(((-j - 1.35) / 2, 1.85, i)),
                   ((j - 1.35) / 2, 1.25, 0.015)),
              slab(V(((j - 0.45) / 2, 1.85, i)),
                   ((j + 0.45) / 2, 1.25, 0.015)),
              slab(V((-0.9, 2.85, i)), (0.45, 0.25, 0.015))]
    part(b, "lining", merge(*lining), m["grey_timber"], levels=0,
         smooth=False)
    # Inside: a board floor 4 cm over the plinth, a ceiling under the
    # loft, and a table, a bench, and a bed.
    part(b, "floor", slab(V((0, 0.62, 0)), (3.5 - t - 0.01, 0.02,
                                           2.5 - t - 0.01)),
         m["boards"], levels=0, smooth=False)
    part(b, "ceiling", slab(V((0, 3.0, 0)), (3.5 - t - 0.01, 0.04,
                                            2.5 - t - 0.01)),
         m["grey_timber"], levels=0, smooth=False)
    if tier <= MID:
        furniture = [slab(V((1.6, 1.38, 0.4)), (0.7, 0.03, 0.45)),
                     slab(V((1.6, 1.36, -1.7)), (0.6, 0.02, 0.18))]
        for sx in (-1, 1):
            for sz in (-1, 1):
                furniture.append(slab(V((1.6 + sx * 0.6, 1.0, 0.4 + sz * 0.37)),
                                      (0.04, 0.35, 0.04)))
        furniture.append(slab(V((1.6, 1.1, 1.25)), (0.65, 0.025, 0.16)))
        for sx in (-1, 1):
            furniture.append(slab(V((1.6 + sx * 0.55, 0.85, 1.25)),
                                  (0.03, 0.22, 0.13)))
        part(b, "furniture", merge(*furniture), m["grey_timber"], levels=0,
             smooth=False)
        part(b, "bed", merge(slab(V((-2.4, 0.95, -1.5)), (0.95, 0.3, 0.5)),
                             slab(V((-3.27, 1.35, -1.5)), (0.08, 0.7, 0.5))),
             m["dark_timber"], levels=0, smooth=False)
        part(b, "bedding", slab(V((-2.35, 1.29, -1.5)), (0.85, 0.05, 0.44)),
             m["white"], levels=0, smooth=False)
    gable(b, "gable", 2.5, 3.5, 3.1, 4.75, m["falu"], along_x=True)
    if tier == NEAR:
        for face, half, count, reach in ((FORWARD, (3.5, 1.25), 22, 2.5),
                                         (-FORWARD, (3.5, 1.25), 22, 2.5),
                                         (RIGHT, (2.5, 1.25), 16, 3.5),
                                         (-RIGHT, (2.5, 1.25), 16, 3.5)):
            boards(b, f"battens{face.x:+.0f}{face.z:+.0f}",
                   V((0, 1.85, 0)) + face * reach, face, half, m["falu"],
                   count, gap=(-1.35, -0.45) if face == FORWARD else None)
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
        # The door stands open, swung in against the wall to the right of
        # the doorway, under a white frame round the opening.
        part(b, "door", slab(V((0.0, 1.6, 2.5 - t - 0.04)),
                             (0.45, 1.0, 0.03)),
             m["dark_timber"], levels=0, bevel=0.01, smooth=False)
        part(b, "door_frame",
             placed(frame_ring((0.55, 1.08), (0.45, 1.0), -0.01, 0.07),
                    V((-0.9, 1.6, 2.5)), RIGHT, UP, FORWARD),
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
    part(b, "step", merge(slab(V((-0.9, 0.11, 2.95)), (0.6, 0.31, 0.3)),
                          slab(V((-0.9, -0.05, 3.4)), (0.65, 0.25, 0.2))),
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


# The barn's measures, which places.cc reads through its exported points.
BARN_HX, BARN_HZ = 6.0, 4.0     # half its length and depth outside
BARN_T = 0.14                   # its walls' boards
BARN_FLOOR = 0.34               # the floor's top over the ground
BARN_EAVE, BARN_RIDGE = 4.3, 7.3
BARN_CEILING = 3.5              # the loft's boards' underside
BARN_DOOR = 1.7                 # half the doorway's width
BARN_DOOR_TOP = 3.25
# Stalls along the back wall, each (x from, x to, front's z, kind): two
# horses and two cows with their heads to the manger, then the pigs' pen.
BARN_STALLS = ((5.86, 4.06, -0.8, "horse"), (4.06, 2.26, -0.8, "horse"),
               (2.26, 0.91, -1.4, "cow"), (0.91, -0.44, -1.4, "cow"))
BARN_PEN = (-0.44, -3.4, -1.2)  # x from, x to, the pen's front
BARN_MANGER = -3.3              # the manger's front board
BARN_NESTS = (-3.55, -3.0, -2.45, -1.9)   # nest boxes' z on the end wall
BARN_NEST_Y = 0.84              # the nest shelf's top


def barn_inside(b, m, tier):
    """What stands in the byre: the stalls with their manger of hay, the
    pigs' pen and trough, the hens' nest boxes and perches, a lantern from
    the middle beam, a ladder to the loft, loose hay, and a few things of
    the work."""
    ix, iz, fl = BARN_HX - BARN_T, BARN_HZ - BARN_T, BARN_FLOOR
    # The manger runs the length of the stalls: a low board before a bed
    # of hay against the back wall.
    x0, x1 = BARN_PEN[0], ix
    part(b, "manger", slab(V(((x0 + x1) / 2, fl + 0.11, BARN_MANGER)),
                           ((x1 - x0) / 2 - 0.005, 0.12, 0.03)),
         m["dark_timber"], levels=0, smooth=False)
    if tier == NEAR:
        hay = []
        rnd = 5
        x = x0 + 0.25
        while x < x1 - 0.2:
            rnd = (rnd * 1103515245 + 12345) & 0x7fffffff
            j = (rnd % 1000) / 1000.0
            hay.append(blob(V((x, fl + 0.12, (BARN_MANGER - iz) / 2)),
                            (0.32 + 0.1 * j, 0.16 + 0.06 * j, 0.26), 8, 5))
            x += 0.42
        part(b, "manger_hay", merge(*hay), m["hay"], levels=1)
    else:
        part(b, "manger_hay", slab(V(((x0 + x1) / 2, fl + 0.1,
                                      (BARN_MANGER - iz) / 2)),
                                   ((x1 - x0) / 2 - 0.02, 0.12,
                                    (BARN_MANGER - (-iz)) / 2 - 0.04)),
             m["hay"], levels=0, smooth=False)
    # The stalls' plank partitions, each with a post at its front up to
    # the loft, and a bar over it.
    walls, posts = [], []
    xs = [s[1] for s in BARN_STALLS]
    fronts = [min(BARN_STALLS[k][2], BARN_STALLS[k + 1][2])
              if k + 1 < len(BARN_STALLS) else BARN_STALLS[k][2]
              for k in range(len(BARN_STALLS))]
    for x, front, (_, _, _, kind) in zip(xs, fronts, BARN_STALLS):
        h = 1.4 if kind == "horse" else 1.2
        walls.append(slab(V((x, fl + h / 2 - 0.01, (front - iz) / 2)),
                          (0.03, h / 2, (front + iz) / 2 - 0.005)))
        posts.append(slab(V((x, (fl + BARN_CEILING) / 2, front)),
                          (0.07, (BARN_CEILING - fl) / 2 + 0.01, 0.07)))
    # The pigs' pen: a lower wall along its front and its far side.
    px0, px1, pz = BARN_PEN
    walls.append(slab(V(((px0 + px1) / 2, fl + 0.47, pz)),
                      ((px0 - px1) / 2 - 0.03, 0.48, 0.03)))
    walls.append(slab(V((px1, fl + 0.47, (pz - iz) / 2)),
                      (0.03, 0.48, (pz + iz) / 2 - 0.005)))
    posts.append(slab(V((px1, fl + 0.55, pz)), (0.06, 0.56, 0.06)))
    part(b, "stalls", merge(*walls), m["grey_timber"], levels=0,
         smooth=False)
    part(b, "posts", merge(*posts), m["dark_timber"], levels=0, smooth=False)
    if tier == NEAR:
        # Each partition's top rail, 3 cm proud of its boards.
        rails = []
        for x, front, (_, _, _, kind) in zip(xs, fronts, BARN_STALLS):
            h = 1.4 if kind == "horse" else 1.2
            rails.append(slab(V((x, fl + h + 0.02, (front - iz) / 2)),
                              (0.05, 0.04, (front + iz) / 2 - 0.08)))
        rails.append(slab(V(((px0 + px1) / 2, fl + 0.97, pz)),
                          ((px0 - px1) / 2 - 0.08, 0.04, 0.05)))
        rails.append(slab(V((px1, fl + 0.97, (pz - iz) / 2)),
                          (0.05, 0.04, (pz + iz) / 2 - 0.08)))
        part(b, "stall_rails", merge(*rails), m["dark_timber"], levels=0,
             smooth=False)
    # Straw on the pen's floor and the stalls', and the pigs' trough.
    straw = [slab(V(((px0 + px1) / 2, fl + 0.015, (pz - iz) / 2)),
                  ((px0 - px1) / 2 - 0.04, 0.02, (pz + iz) / 2 - 0.04))]
    for x_a, x_b, front, _ in BARN_STALLS:
        straw.append(slab(V(((x_a + x_b) / 2, fl + 0.01,
                             (front + BARN_MANGER) / 2)),
                          (abs(x_a - x_b) / 2 - 0.05, 0.015,
                           (front - BARN_MANGER) / 2 - 0.05)))
    part(b, "straw", merge(*straw), m["straw"], levels=0, smooth=False)
    tx = (px0 + px1) / 2
    trough = [slab(V((tx, fl + 0.02, -iz + 0.25)), (0.62, 0.03, 0.17)),
              slab(V((tx, fl + 0.12, -iz + 0.09)), (0.62, 0.12, 0.025)),
              slab(V((tx, fl + 0.12, -iz + 0.41)), (0.62, 0.12, 0.025)),
              slab(V((tx - 0.6, fl + 0.12, -iz + 0.25)), (0.025, 0.12, 0.15)),
              slab(V((tx + 0.6, fl + 0.12, -iz + 0.25)), (0.025, 0.12, 0.15))]
    part(b, "trough", merge(*trough), m["dark_timber"], levels=0,
         smooth=False)
    part(b, "swill", slab(V((tx, fl + 0.1, -iz + 0.25)), (0.56, 0.02, 0.13)),
         m["swill"], levels=0, smooth=False)

    # The hens' corner: a row of nest boxes on a shelf along the end wall,
    # a ladder of perches beside them, and a plank up to the boxes.
    nx = -ix
    shelf_y = BARN_NEST_Y
    z0, z1 = BARN_NESTS[0] - 0.29, BARN_NESTS[-1] + 0.29
    boxes = [slab(V((nx + 0.26, shelf_y - 0.02, (z0 + z1) / 2)),
                  (0.26, 0.03, (z1 - z0) / 2)),
             slab(V((nx + 0.26, shelf_y + 0.44, (z0 + z1) / 2)),
                  (0.27, 0.02, (z1 - z0) / 2 + 0.01)),
             slab(V((nx + 0.5, shelf_y + 0.04, (z0 + z1) / 2)),
                  (0.02, 0.07, (z1 - z0) / 2))]
    for k in range(len(BARN_NESTS) + 1):
        z = z0 + (z1 - z0) * k / len(BARN_NESTS)
        boxes.append(slab(V((nx + 0.26, shelf_y + 0.21, z)),
                          (0.25, 0.22, 0.015)))
    # The shelf's legs.
    for z in (z0 + 0.04, z1 - 0.04):
        boxes.append(slab(V((nx + 0.46, (fl + shelf_y) / 2, z)),
                          (0.03, (shelf_y - fl) / 2, 0.03)))
    part(b, "nest_boxes", merge(*boxes), m["grey_timber"], levels=0,
         smooth=False)
    if tier == NEAR:
        nests = [blob(V((nx + 0.27, shelf_y + 0.05, z)), (0.2, 0.06, 0.2), 8, 4)
                 for z in BARN_NESTS]
        part(b, "nest_straw", merge(*nests), m["straw"], levels=1)
        # The perches: poles across the corner, one above and behind the
        # other, over a board for what falls.
        poles = []
        for k, (y, dz) in enumerate(((1.05, -3.25), (1.35, -3.6))):
            poles.append(tube([V((nx - 0.02, y, dz)), V((-3.48, y, dz))], 0.025,
                              sides=6))
        part(b, "perches", merge(*poles), m["dark_timber"], levels=0)
        part(b, "perch_board", slab(V(((nx + -3.48) / 2, 0.62, -3.42)),
                                    ((-3.48 - nx) / 2, 0.015, 0.3)),
             m["boards"], levels=0, smooth=False)
        # The hens' plank up to the boxes, with slats across it.
        foot = V((nx + 1.15, fl + 0.02, -1.15))
        top = V((nx + 0.52, shelf_y, -1.62))
        d = (top - foot).normalized()
        side = UP.cross(d).normalized()
        n = d.cross(side)
        plank = [slab((foot + top) / 2, ((top - foot).length / 2, 0.012, 0.11),
                      axes=(d, n, side))]
        for k in range(1, 5):
            c = foot + (top - foot) * (k / 5) + n * 0.025
            plank.append(slab(c, (0.012, 0.012, 0.1), axes=(d, n, side)))
        part(b, "hen_ladder", merge(*plank), m["grey_timber"], levels=0,
             smooth=False)

    # The beams across under the loft, its boards, and the hatch the
    # ladder goes up to.
    beams = [slab(V((x, BARN_CEILING - 0.1, 0)), (0.09, 0.11, iz - 0.003))
             for x in (-4.6, -2.3, 0.0, 2.3, 4.6)]
    part(b, "beams", merge(*beams), m["dark_timber"], levels=0, smooth=False)
    part(b, "loft", slab(V((0, BARN_CEILING + 0.03, 0)),
                         (ix - 0.005, 0.03, iz - 0.005)), m["boards"],
         levels=0, smooth=False)
    hatch = V((-1.15, BARN_CEILING - 0.02, 1.2))
    part(b, "hatch", slab(hatch, (0.4, 0.01, 0.45)), m["shadow"], levels=0,
         smooth=False)
    if tier == NEAR:
        foot = V((-1.15, fl, 2.35))
        top = hatch + V((0, 0.35, 0.3))
        rails = []
        for s in (-1, 1):
            o = V((s * 0.24, 0, 0))
            rails.append(tube([foot + o, top + o], 0.03, sides=6))
        rungs = []
        for k in range(1, 11):
            c = foot + (top - foot) * (k / 11.5)
            rungs.append(tube([c + V((-0.24, 0, 0)), c + V((0.24, 0, 0))],
                              0.018, sides=5))
        part(b, "ladder", merge(*rails), m["grey_timber"], levels=0)
        part(b, "ladder_rungs", merge(*rungs), m["grey_timber"], levels=0)
        # The lantern from the middle beam, a little toward the door.
        lamp = V((0.0, 2.72, 0.7))
        part(b, "lantern_chain", tube([V((0.0, BARN_CEILING - 0.2, 0.7)),
                                       lamp + V((0, 0.17, 0))], 0.008,
                                      sides=4), m["iron"], levels=0)
        part(b, "lantern_frame",
             merge(slab(lamp + V((0, 0.13, 0)), (0.09, 0.025, 0.09)),
                   slab(lamp + V((0, -0.13, 0)), (0.1, 0.02, 0.1)),
                   tube([lamp + V((0, 0.15, 0)), lamp + V((0, 0.2, 0))],
                        0.04, sides=6)),
             m["iron"], levels=0, smooth=False)
        part(b, "lantern_glass", slab(lamp, (0.075, 0.12, 0.075)),
             m["lamplight"], levels=0, smooth=False)
        # Loose hay heaped in the front corner, a fork stood in it, a
        # pair of milk churns, a tub, and sacks by the other corner.
        heap = merge(blob(V((4.7, fl + 0.35, 2.85)), (1.05, 0.5, 0.85), 10, 6),
                     blob(V((5.2, fl + 0.6, 3.25)), (0.6, 0.55, 0.5), 8, 5),
                     blob(V((3.9, fl + 0.2, 3.3)), (0.55, 0.28, 0.45), 8, 5))
        part(b, "hay_heap", heap, m["hay"], levels=1)
        fork = V((3.75, fl, 2.3))
        tip = V((4.15, fl + 1.65, 2.9))
        part(b, "fork_shaft", tube([fork, tip], 0.018, sides=5),
             m["grey_timber"], levels=0)
        prongs = []
        d = (tip - fork).normalized()
        for s in (-1, 0, 1):
            o = V((0.05 * s, 0, -0.03 * s))
            prongs.append(tube([fork + o + d * 0.05, fork + o - d * 0.2], 0.007,
                               sides=4))
        part(b, "fork_prongs", merge(*prongs), m["iron"], levels=0)
        churns = []
        for x, z in ((-5.35, 3.3), (-4.9, 3.45)):
            churns.append(tube([V((x, fl - 0.01, z)), V((x, fl + 0.5, z))],
                               0.17, sides=12))
            churns.append(tube([V((x, fl + 0.5, z)), V((x, fl + 0.62, z))],
                               0.09, sides=10))
            churns.append(tube([V((x, fl + 0.62, z)), V((x, fl + 0.66, z))],
                               0.11, sides=10))
        part(b, "churns", merge(*churns), m["tin"], levels=0)
        part(b, "tub", tube([V((-5.2, fl - 0.01, 2.4)), V((-5.2, fl + 0.4, 2.4))],
                            0.34, sides=14), m["dark_timber"], levels=0)
        part(b, "tub_water", slab(V((-5.2, fl + 0.35, 2.4)), (0.24, 0.01, 0.24)),
             m["water"], levels=0, smooth=False)
        sacks = merge(blob(V((-5.3, fl + 0.3, 1.4)), (0.28, 0.32, 0.22), 8, 5),
                      blob(V((-4.85, fl + 0.26, 1.2)), (0.27, 0.28, 0.21), 8, 5))
        part(b, "sacks", sacks, m["sacking"], levels=1)


def barn(coll, m, suffix, tier):
    hx, hz, t, fl = BARN_HX, BARN_HZ, BARN_T, BARN_FLOOR
    eave, ridge = BARN_EAVE, BARN_RIDGE
    door, door_top = BARN_DOOR, BARN_DOOR_TOP
    if tier != MID:
        base = assembly("barn_base" + suffix, coll, V((0, 0, 0)),
                        mesh="barn_base" + suffix)
        # A footing of fieldstones from well below the ground to the
        # floor, and an earthen ramp up to the doors, faced with stone.
        plinth(base, "footing", V((0, -0.35, 0)), (hx + 0.1, 0.65, hz + 0.1),
               3, 23, m["stone"], tier)
        ramp = turned(prism([(-hz, fl - 0.04), (-hz - 1.9, -0.12),
                             (-hz - 1.9, -0.7), (-hz, -0.7)],
                            -door - 0.25, door + 0.25))
        part(base, "ramp", ramp, m["stone_dark"], levels=0, smooth=False)

    b = assembly("barn" + suffix, coll, V((0, 0, 0)), mesh="barn" + suffix)
    wy = (fl - 0.04 + eave) / 2
    wh = (eave - (fl - 0.04)) / 2
    walls = [slab(V((0, wy, -hz + t / 2)), (hx, wh, t / 2)),
             slab(V((-hx + t / 2, wy, 0)), (t / 2, wh, hz - t)),
             slab(V((hx - t / 2, wy, 0)), (t / 2, wh, hz - t)),
             slab(V(((hx + door) / 2, wy, hz - t / 2)),
                  ((hx - door) / 2, wh, t / 2)),
             slab(V((-(hx + door) / 2, wy, hz - t / 2)),
                  ((hx - door) / 2, wh, t / 2)),
             slab(V((0, (door_top + eave) / 2, hz - t / 2)),
                  (door, (eave - door_top) / 2, t / 2))]
    part(b, "walls", merge(*walls), m["falu"], levels=0, smooth=False)
    gable(b, "gable", hz, hx, eave, ridge, m["falu"], along_x=True)
    # Over the gable by 2 cm, its eaves out past the long walls.
    slope = (ridge - eave) / hz
    over = 0.5
    roof(b, "roof", hz + over, hx + 0.4, eave + 0.02 - slope * over,
         ridge + 0.02, 0.1, m["shingle"], along_x=True)
    part(b, "ridge", slab(V((0, ridge + 0.17, 0)), (hx + 0.45, 0.09, 0.13)),
         m["dark_timber"], levels=0, smooth=False)
    # The floor of boards, and the threshold over the doorway.
    ix, iz = hx - t, hz - t
    part(b, "floor", slab(V((0, fl - 0.02, 0)), (ix - 0.005, 0.02, iz - 0.005)),
         m["boards"], levels=0, smooth=False)
    part(b, "threshold", slab(V((0, fl + 0.005, hz - t / 2)),
                              (door - 0.005, 0.025, t / 2 + 0.04)),
         m["dark_timber"], levels=0, smooth=False)

    if tier == NEAR:
        for face, half, count, reach, gap in (
                (FORWARD, (hx, wh), 40, hz, (-door, door)),
                (-FORWARD, (hx, wh), 40, hz, None),
                (RIGHT, (hz, wh), 26, hx, None),
                (-RIGHT, (hz, wh), 26, hx, None)):
            boards(b, f"battens{face.x:+.0f}{face.z:+.0f}",
                   V((0, wy, 0)) + face * reach, face, half, m["falu"],
                   count, gap=gap)
    if tier <= MID:
        # White corner boards, the fascia under the eaves, barge boards up
        # the gables, and a white frame round the doorway.
        trim = []
        for sx in (-1, 1):
            for sz in (-1, 1):
                trim.append(slab(V((sx * hx, wy, sz * hz)), (0.08, wh + 0.02,
                                                            0.08)))
        for sz in (-1, 1):
            trim.append(slab(V((0, eave - 0.08, sz * (hz + 0.05))),
                             (hx + 0.05, 0.08, 0.03)))
        for sx in (-1, 1):
            for sz in (-1, 1):
                a = V((sx * (hx + 0.43), eave + 0.02 - slope * over - 0.06,
                       sz * (hz + over)))
                c = V((sx * (hx + 0.43), ridge + 0.02 - 0.06, 0))
                d = (c - a).normalized()
                trim.append(slab((a + c) / 2, (0.025, 0.11, (c - a).length / 2),
                                 axes=(RIGHT, RIGHT.cross(d).normalized(), d)))
        for sx in (-1, 1):
            trim.append(slab(V((sx * (door + 0.07), (fl + door_top) / 2 + 0.05,
                                hz + 0.03)),
                             (0.07, (door_top - fl) / 2 + 0.05, 0.045)))
        trim.append(slab(V((0, door_top + 0.07, hz + 0.03)),
                         (door + 0.14, 0.07, 0.045)))
        part(b, "trim", merge(*trim), m["white"], levels=0, smooth=False)
        # The hay door high in the left gable, white framed.
        part(b, "hay_door", slab(V((hx + 0.05, 5.0, 0)), (0.04, 0.6, 0.55)),
             m["dark_timber"], levels=0, smooth=False)
        part(b, "hay_door_frame",
             placed(frame_ring((0.67, 0.72), (0.55, 0.6), -0.01, 0.09),
                    V((hx, 5.0, 0)), -FORWARD, UP, RIGHT),
             m["white"], levels=0, smooth=False)

    # The big doors stand open, swung out square to the wall either side
    # of the doorway: boards in a white frame, braced corner to corner.
    leaf_h = door_top - fl - 0.06
    leaf_w = door - 0.04
    for s in (-1, 1):
        x = s * (door + 0.17)
        centre = V((x, fl + 0.02 + leaf_h / 2, hz + 0.1 + leaf_w / 2))
        part(b, f"door{s:+d}", slab(centre, (0.04, leaf_h / 2, leaf_w / 2)),
             m["falu"], levels=0, bevel=0.01 if tier == NEAR else 0.0,
             smooth=False)
        if tier <= MID:
            braces = []
            for o in (-1, 1):
                face = V((x + o * 0.06, 0, 0))
                fx = face.x
                braces.append(slab(V((fx, centre.y + leaf_h / 2 - 0.07,
                                      centre.z)), (0.02, 0.07, leaf_w / 2)))
                braces.append(slab(V((fx, centre.y - leaf_h / 2 + 0.07,
                                      centre.z)), (0.02, 0.07, leaf_w / 2)))
                braces.append(slab(V((fx, centre.y, centre.z - leaf_w / 2
                                      + 0.07)), (0.02, leaf_h / 2, 0.07)))
                braces.append(slab(V((fx, centre.y, centre.z + leaf_w / 2
                                      - 0.07)), (0.02, leaf_h / 2, 0.07)))
                for k in (-1, 1):
                    d = (UP * (leaf_h - 0.2) + FORWARD * k *
                         (leaf_w - 0.2)).normalized()
                    braces.append(slab(V((fx + 0.004 * k, centre.y,
                                          centre.z)),
                                       (0.02, ((leaf_h - 0.2) ** 2 +
                                               (leaf_w - 0.2) ** 2) ** 0.5
                                        / 2, 0.06),
                                       axes=(RIGHT, d, RIGHT.cross(d))))
            part(b, f"door_braces{s:+d}", merge(*braces), m["white"],
                 levels=0, smooth=False)
    if tier == NEAR:
        # The doors' iron strap hinges and the hasp.
        hinges = []
        for s in (-1, 1):
            for y in (fl + 0.5, door_top - 0.5):
                hinges.append(slab(V((s * (door + 0.17) + s * 0.07, y,
                                      hz + 0.45)), (0.012, 0.03, 0.33)))
        part(b, "hinges", merge(*hinges), m["iron"], levels=0, smooth=False)

    # Windows: small ones over the stalls in the back wall and in the
    # ends, each with a pale pane inside where the daylight comes in.
    lights = ((V((4.96, 2.3, -hz)), -FORWARD), (V((3.16, 2.3, -hz)), -FORWARD),
              (V((0.9, 2.3, -hz)), -FORWARD), (V((-2.0, 2.3, -hz)), -FORWARD),
              (V((-hx, 2.3, 1.4)), -RIGHT), (V((hx, 2.3, 0.6)), RIGHT))
    for k, (at, normal) in enumerate(lights):
        window(b, f"window{k}", at, normal, (0.7, 0.5), m, tier)

    if tier <= MID:
        # Lined inside with old boards, sunk 1 cm in the walls' faces and
        # standing 2 cm off them; the panes of the windows show through.
        li, lj = iz - 0.005, ix - 0.005
        lh = (BARN_CEILING - fl) / 2
        ly = (BARN_CEILING + fl) / 2 - 0.01
        lining = [slab(V((0, ly, -li)), (lj, lh, 0.015)),
                  slab(V((-lj, ly, 0)), (0.015, lh, li)),
                  slab(V((lj, ly, 0)), (0.015, lh, li)),
                  slab(V(((lj + door) / 2, ly, li)), ((lj - door) / 2, lh,
                                                      0.015)),
                  slab(V((-(lj + door) / 2, ly, li)), ((lj - door) / 2, lh,
                                                       0.015)),
                  slab(V((0, (door_top + BARN_CEILING) / 2, li)),
                       (door, (BARN_CEILING - door_top) / 2, 0.015))]
        part(b, "lining", merge(*lining), m["old_boards"], levels=0,
             smooth=False)
        panes = []
        for at, normal in lights:
            inner = at - normal * (hz - li + 0.025 if abs(normal.z) > 0.5
                                   else hx - lj + 0.025)
            side = UP.cross(normal).normalized()
            panes.append(placed(slab(V((0, 0, 0)), (0.35, 0.25, 0.008)),
                                inner, side, UP, -normal))
        part(b, "panes_inside", merge(*panes), m["daylight"], levels=0,
             smooth=False)
        barn_inside(b, m, tier)
    else:
        # From far off the doorway is the dark of the barn within.
        part(b, "doorway", slab(V((0, (fl + door_top) / 2, hz - t / 2)),
                                (door + 0.01, (door_top - fl) / 2, 0.02)),
             m["shadow"], levels=0, smooth=False)

    if tier == NEAR:
        # Points the game places by: the floor, each stall's manger (where
        # its beast's muzzle goes), the pen's corners, the nests, the
        # lantern, and the doorway on the floor.
        assembly("barn_floor", coll, V((0, fl, 0)), point="barn_floor")
        for k, (xa, xb, front, kind) in enumerate(BARN_STALLS):
            assembly(f"barn_stall_{k}", coll,
                     V(((xa + xb) / 2, fl, (BARN_MANGER + -iz) / 2)),
                     point=f"barn_stall_{k}")
        px0, px1, pz = BARN_PEN
        assembly("barn_pen_low", coll, V((px1 + 0.1, fl, -iz + 0.5)),
                 point="barn_pen_low")
        assembly("barn_pen_high", coll, V((px0 - 0.1, fl, pz - 0.15)),
                 point="barn_pen_high")
        for k, z in enumerate(BARN_NESTS):
            assembly(f"barn_nest_{k}", coll, V((-ix + 0.27, BARN_NEST_Y + 0.08,
                                                z)),
                     point=f"barn_nest_{k}")
        assembly("barn_lantern", coll, V((0.0, 2.72, 0.7)),
                 point="barn_lantern")
        assembly("barn_doorway", coll, V((0, fl, hz)), point="barn_doorway")


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
    for name, x in (("mill", 0.0), ("house", 10.0), ("barn", 25.0)):
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
