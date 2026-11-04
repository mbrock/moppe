"""Builds the farm's poultry: models/<bird>.blend for each of them.

    Blender -b --factory-startup --python tools/figure/hens.py -- hen

builds one bird (or, with no bird named, every one of them):

  - hen: a Skånsk blommehöna, the Scanian landrace hen, her feathers
    "flowered" -- black, ochre, and white, speckled together -- with a
    small single comb;
  - rooster: her cock, gold and russet hackles over a dark breast and a
    high sickle tail of black gone green, a tall comb and long wattles;
  - chick: a few days old, buff down with a brown stripe down the back.

Each is authored life size in the game's frame -- x to the bird's left, y
up, z forward -- with its origin on the ground under its middle. Its
assemblies are rigid parts that moppe/game/hens.cc swings about their
pivots: the body (with its wings and tail); the head and neck, about the
neck's root (the point `neck`), which pecks; and the left leg, about the
hip (`hip`), which the game draws on both sides. The point `beak` is the
beak's tip. tools/figure/export_model.py writes each to
moppe/game/<bird>_model.{hh,cc}.

Rebuilding discards hand edits to the .blend files.
"""

import math
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
from shapes import V, RIGHT, UP, FORWARD, blob, merge, tube  # noqa
from animals import Beast, both, noise, path  # noqa


def flowered(seed, black=0.42, white=0.72):
    """The blommehöna's speckle: small spots of black, white, and ochre."""
    def paint(c):
        n = noise(c, 22.0, seed)
        if n > white:
            return "white"
        if n < black:
            return "black"
        return "ochre"
    return paint


def bird(name, palette, finish, size, coat, comb, wattles, tail, hackle=None,
         fluff=False, neck_at=(0, 0.27, 0.09), head_at=(0, 0.385, 0.145),
         head_size=1.0):
    """One bird, scaled by `size` from a hen's measures; `coat` paints the
    body and neck, `comb` and `wattles` are how big they are (0 for a
    chick), `tail` is the tail's kind ("hen", "sickle", or "stub"), and
    `hackle`, when given, paints the neck's long feathers."""
    k = size
    neck_at = V(neck_at) * k
    hip = V((0.045, 0.165, -0.01)) * k
    head_at = V(head_at) * k
    beak = head_at + V((0, -0.012, 0.058)) * k
    b = Beast(name, palette, finish)
    b.assembly("body", V((0, 0, 0)))
    b.assembly("head", neck_at)
    b.assembly("leg", hip)
    for point, at in (("neck", neck_at), ("hip", hip), ("beak", beak)):
        b.point(point, at)

    def round_body(p):
        if not fluff:
            return p
        return p * (1.0 + 0.04 * (noise(p, 60.0, 3) - 0.5))

    # The body: a breast carried high and forward, the back sloping up to
    # the tail.
    sections = [(V((0, 0.235, -0.15)), 0.06, 0.06),
                (V((0, 0.225, -0.11)), 0.095, 0.085),
                (V((0, 0.215, -0.04)), 0.115, 0.105),
                (V((0, 0.225, 0.04)), 0.11, 0.105),
                (V((0, 0.25, 0.10)), 0.085, 0.085),
                (V((0, 0.27, 0.135)), 0.05, 0.05)]
    if fluff:
        sections = [(V((0, 0.2, -0.09)), 0.06, 0.06),
                    (V((0, 0.2, -0.05)), 0.12, 0.12),
                    (V((0, 0.22, 0.04)), 0.12, 0.12),
                    (V((0, 0.24, 0.09)), 0.06, 0.06)]
    body = path([(c * k, w * k, d * k) for c, w, d in sections], RIGHT,
                sides=12, shape=round_body)
    b.part("body", "body", body, coat, levels=1)
    if not fluff:
        # The wings folded along the flanks.
        wing = path([(V((0.095, 0.25, 0.07)) * k, 0.045 * k, 0.012 * k),
                     (V((0.108, 0.235, -0.02)) * k, 0.06 * k, 0.016 * k),
                     (V((0.097, 0.245, -0.11)) * k, 0.045 * k, 0.012 * k),
                     (V((0.075, 0.265, -0.16)) * k, 0.022 * k, 0.006 * k)],
                    UP, sides=8)
        b.part("body", "wings", both(wing), coat, levels=1)
    if tail == "hen":
        feathers = []
        for s in (-1, 0, 1):
            root = V((0.02 * s, 0.25, -0.13)) * k
            feathers.append(path([(root, 0.03 * k, 0.012 * k),
                                  (root + V((0.02 * s, 0.07, -0.05)) * k,
                                   0.045 * k, 0.01 * k),
                                  (root + V((0.03 * s, 0.13, -0.07)) * k,
                                   0.025 * k, 0.006 * k)], RIGHT, sides=6))
        b.part("body", "tail", merge(*feathers), "black", levels=1)
    elif tail == "sickle":
        feathers = []
        for s in (-1, -0.4, 0.4, 1):
            root = V((0.02 * s, 0.25, -0.11)) * k
            pts = []
            for j in range(7):
                t = j / 6
                a = 0.4 + 2.2 * t
                pts.append((root + V((0.03 * s * t,
                                      0.19 * (math.sin(a) - math.sin(0.4)),
                                      -0.12 * (math.cos(0.4) - math.cos(a))
                                      - 0.05 * t)) * k,
                            (0.03 - 0.018 * t) * k, 0.008 * k))
            feathers.append(path(pts, RIGHT, sides=6))
        b.part("body", "tail", merge(*feathers), "sickle", levels=1)
    else:
        b.part("body", "tail", blob(V((0, 0.23, -0.1)) * k,
                                    (0.03 * k, 0.025 * k, 0.025 * k), 6, 4),
               coat, levels=1)

    # The head and neck, from the neck's root to the beak.
    neck = path([(neck_at + V((0, -0.01, -0.02)) * k, 0.05 * k, 0.05 * k),
                 (neck_at + V((0, 0.05, 0.02)) * k, 0.04 * k, 0.042 * k),
                 (head_at + V((0, -0.005, -0.005)) * k, 0.028 * k,
                  0.032 * k)],
                RIGHT, sides=10)
    b.part("head", "neck", neck, hackle or coat, levels=1)
    h = head_size
    b.part("head", "skull", blob(head_at, (0.026 * k * h, 0.03 * k * h,
                                           0.038 * k * h), 10, 6),
           hackle or coat, levels=1)
    b.part("head", "face", both(blob(head_at + V((0.018, -0.002, 0.012)) * k,
                                     (0.01 * k, 0.016 * k, 0.018 * k), 6, 4)),
           "comb" if comb > 0 else (hackle or coat), levels=1)
    b.part("head", "beak", path([(head_at + V((0, -0.008, 0.03)) * k,
                                  0.012 * k, 0.01 * k),
                                 (beak + V((0, 0.002, -0.012)) * k,
                                  0.006 * k, 0.005 * k),
                                 (beak, 0.001 * k, 0.001 * k)],
                                RIGHT, sides=6), "horn", levels=1)
    b.part("head", "eyes",
           both(blob(head_at + V((0.024, 0.008, 0.012)) * k,
                     (0.005 * k, 0.007 * k, 0.007 * k), 6, 4)), "eye",
           levels=0)
    if comb > 0:
        # A single comb of serrations over the skull, and wattles under
        # the beak.
        points = [path([(head_at + V((0, 0.024, 0.03)) * k, 0.008 * k,
                         0.004 * k),
                        (head_at + V((0, 0.03, 0.0)) * k, 0.012 * k, 0.005 * k),
                        (head_at + V((0, 0.024, -0.03)) * k, 0.008 * k,
                         0.004 * k)], UP, sides=6)]
        for j in range(5):
            t = j / 4
            base = head_at + V((0, 0.026, 0.03 - 0.06 * t)) * k
            h = comb * (0.022 + 0.02 * math.sin(math.pi * t)) * k
            points.append(path([(base, 0.01 * k, 0.005 * k),
                                (base + V((0, h, -0.004 * k)), 0.004 * k,
                                 0.003 * k)], FORWARD, sides=5))
        b.part("head", "comb", merge(*points), "comb", levels=1)
        b.part("head", "wattles",
               both(blob(head_at + V((0.008, -0.04 * wattles, 0.032)) * k,
                         (0.006 * k, 0.018 * wattles * k, 0.012 * k), 6, 4)),
               "comb", levels=1)

    # The leg: a feathered thigh, the bare yellow shank, and four toes.
    thigh = blob(hip + V((0, -0.02, 0)) * k, (0.032 * k, 0.045 * k,
                                              0.042 * k), 8, 5)
    b.part("leg", "thigh", thigh, coat, levels=1)
    knee = hip + V((0, -0.06, 0.005)) * k
    foot = V((hip.x, 0.012 * k, 0.012 * k))
    b.part("leg", "shank", tube([knee, foot], 0.008 * k, sides=6), "leg",
           levels=0)
    toes = []
    for a in (-0.45, 0.0, 0.45):
        d = V((math.sin(a), 0, math.cos(a)))
        toes.append(tube([foot, foot + d * 0.055 * k + V((0, -0.008, 0)) * k],
                         0.005 * k, sides=5))
    toes.append(tube([foot, foot + V((0, -0.006, -0.028)) * k], 0.005 * k,
                     sides=5))
    b.part("leg", "toes", merge(*toes), "leg", levels=0)
    b.save()


FINISH = {
    "black": dict(roughness=0.5, sheen=0.5),
    "ochre": dict(roughness=0.7, sheen=0.5),
    "white": dict(roughness=0.7, sheen=0.5),
    "comb": dict(roughness=0.45),
    "horn": dict(roughness=0.4),
    "leg": dict(roughness=0.5),
    "eye": dict(roughness=0.1, coat=1.0),
    "sickle": dict(roughness=0.3, sheen=0.6, coat=0.4),
    "hackle": dict(roughness=0.5, sheen=0.6),
    "down": dict(roughness=1.0, sheen=0.8),
    "stripe": dict(roughness=1.0, sheen=0.8),
}

BASE = {
    "black": (0.08, 0.07, 0.07),
    "ochre": (0.62, 0.40, 0.18),
    "white": (0.88, 0.86, 0.80),
    "comb": (0.75, 0.10, 0.09),
    "horn": (0.80, 0.70, 0.40),
    "leg": (0.80, 0.66, 0.26),
    "eye": (0.55, 0.30, 0.05),
}


def hen():
    bird("hen", BASE, FINISH, 1.0, flowered(5), comb=0.7, wattles=0.7,
         tail="hen")


def rooster():
    palette = dict(BASE, sickle=(0.04, 0.07, 0.05),
                   hackle=(0.72, 0.42, 0.14))

    def coat(c):
        if c.y > 0.25 * 1.2 and c.z > 0.0:
            return "hackle"
        return flowered(9, black=0.5, white=0.75)(c)

    bird("rooster", palette, FINISH, 1.2, coat, comb=1.4, wattles=1.3,
         tail="sickle", hackle="hackle")


def chick():
    palette = dict(BASE, down=(0.86, 0.74, 0.42), stripe=(0.42, 0.28, 0.15))

    def coat(c):
        return "stripe" if abs(c.x) < 0.012 and c.y > 0.13 and c.z < 0.02 \
            else "down"

    bird("chick", palette, FINISH, 0.45, coat, comb=0.0, wattles=0.0,
         tail="stub", fluff=True, neck_at=(0, 0.25, 0.06),
         head_at=(0, 0.31, 0.10), head_size=1.35)


BIRDS = {"hen": hen, "rooster": rooster, "chick": chick}

if __name__ == "__main__":
    names = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    for name in names or BIRDS:
        BIRDS[name]()
