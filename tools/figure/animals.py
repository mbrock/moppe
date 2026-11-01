"""Builds the animals' models: models/<species>.blend for each of them.

    Blender -b --factory-startup --python tools/figure/animals.py -- horse

builds one species (or, with no species named, every one of them):

  - horse: a North Swedish horse (nordsvensk brukshäst), bay with black
    points, the heavy, short-legged working horse of the Swedish farm;
  - cow: a Swedish mountain cow (fjällko), hornless, white with black
    ears, muzzle, and spots;
  - sheep: a Gotland sheep, grey curly fleece, black head and legs;
  - reindeer: grey-brown with a pale neck and mane, antlered;
  - moose: a bull, near black with pale stockings, a bell under his
    throat and broad palmate antlers;
  - roe_deer: a buck in his autumn coat, grey-brown with the white rump
    patch, and short antlers.

Each is authored life size in the game's frame -- x to the animal's left,
y up, z forward -- with its origin on the ground under the middle of its
body, standing square. Its assemblies are rigid parts that the game swings
about their pivots: the body; the neck, about its root low in the
chest, below the withers (the point `withers`), which lets the head reach
the grass; the head, about the poll at the neck's end (`poll`); one
foreleg's upper part (to the knee, the carpus) and lower part (to the
hoof), and one hind leg's (to the hock, and from it to the hoof), which
the game draws on both sides; and the tail, about its root. Points mark
the pivots in model space -- `shoulder`, `carpus`, `fore_hoof`, `hip`,
`hock`, `hind_hoof` on the left side, `withers`, `poll`, `tail_root` --
and `muzzle`, where the mouth crops the grass. Each leg part is modelled
round its own bone, so the left side's serves the right mirrored by
moving it across. tools/figure/export_model.py writes each species to
moppe/game/<species>_model.{hh,cc}; moppe/game/animals.cc poses them.

Rebuilding discards hand edits to the .blend files.
"""

import math
import os
import random
import sys

import bpy

sys.path.insert(0, os.path.dirname(__file__))
from shapes import (V, RIGHT, UP, FORWARD, Cage, assembly, blob,  # noqa
                    blender, loft, material, merge, tube)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


# -- noise ---------------------------------------------------------------

def noise(p, scale, seed=0):
    """A smooth field, 0..1, of a point, with features some 1/`scale`
    metres across: eight plane waves in directions and phases drawn from
    `seed`, so its level sets are rounded blobs rather than the boxes a
    lattice's interpolation leaves."""
    rng = random.Random(seed * 7919 + 17)
    total = 0.0
    for _ in range(8):
        d = V((rng.gauss(0, 1), rng.gauss(0, 1), rng.gauss(0, 1)))
        d.normalize()
        k = scale * 2 * math.pi * rng.uniform(0.6, 1.4)
        total += math.cos(k * d.dot(V(p)) + rng.uniform(0, 2 * math.pi))
    return 0.5 + total / (2 * math.sqrt(8) * 1.3)


# -- shapes --------------------------------------------------------------

def barrel(sections, sides=18, shape=None, rings=0):
    """The trunk: rings across z, each (z, centre height, half width,
    height above the centre, depth below it), so the back can be flatter
    than the belly is round. `shape` may move each point. With `rings`,
    that many are interpolated between the given ones, for a finer cage
    to shape."""
    if rings:
        z0, z1 = sections[0][0], sections[-1][0]
        fine = []
        for n in range(rings):
            z = z0 + (z1 - z0) * n / (rings - 1)
            k = max(i for i in range(len(sections) - 1)
                    if sections[i][0] <= z + 1e-9) if n < rings - 1 \
                else len(sections) - 2
            a, b = sections[k], sections[k + 1]
            t = (z - a[0]) / (b[0] - a[0])
            fine.append(tuple(a[j] + (b[j] - a[j]) * t for j in range(5)))
        sections = fine
    cage = Cage()
    rings = []
    for z, yc, hw, top, bottom in sections:
        ring = []
        for i in range(sides):
            t = 2 * math.pi * i / sides
            c, s = math.cos(t), math.sin(t)
            # A little squarer than an ellipse across the back and flanks.
            sq = 1.0 + 0.08 * (1 - abs(math.cos(2 * t)))
            p = V((hw * c * sq, yc + (top if s > 0 else bottom) * s, z))
            if shape:
                p = shape(p)
            cage.verts.append(p)
            ring.append(len(cage.verts) - 1)
        rings.append(ring)
    for a, b in zip(rings, rings[1:]):
        for i in range(sides):
            k = (i + 1) % sides
            cage.faces.append([a[i], a[k], b[k], b[i]])
    cage.faces.append(list(rings[0]))
    cage.faces.append(list(rings[-1]))
    axis = [V((0, yc, z)) for z, yc, _, _, _ in sections]

    def inside(p):
        best = min(axis, key=lambda c: abs(c.z - p.z))
        return best

    cage.orient(inside)
    return cage


def path(points, ref=RIGHT, sides=10, close=(True, True), shape=None):
    """A loft through (point, half width along `ref`, half depth)."""
    cage = loft(points, ref, sides=sides, close=close)
    if shape:
        cage.verts = [shape(v) for v in cage.verts]
    return cage


def along(a, b, t):
    return a + (b - a) * t


def ear(base, out, length, width, thick=0.012, sides=6):
    """A leaf-shaped ear from `base` pointing along `out`."""
    out = out.normalized()
    ref = UP if abs(out.dot(UP)) < 0.8 else FORWARD
    return loft([(base, width * 0.55, thick),
                 (base + out * length * 0.35, width, thick),
                 (base + out * length * 0.75, width * 0.6, thick * 0.8),
                 (base + out * length, width * 0.12, thick * 0.5)],
                ref, sides=sides)


def mirror(cage):
    out = Cage()
    out.verts = [V((-v.x, v.y, v.z)) for v in cage.verts]
    out.faces = [list(reversed(f)) for f in cage.faces]
    return out


def both(cage):
    return merge(cage, mirror(cage))


# -- objects -------------------------------------------------------------

class Beast:
    """One species' assemblies in its own collection and .blend."""

    def __init__(self, name, palette, finish):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        self.name = name
        self.coll = bpy.data.collections.new(name)
        bpy.context.scene.collection.children.link(self.coll)
        self.m = {k: material(k, c, **finish.get(k, {}))
                  for k, c in palette.items()}
        self.asm = {}
        self.pivot = {}

    def assembly(self, mesh, pivot):
        a = assembly(mesh, self.coll, pivot, mesh=mesh)
        self.asm[mesh] = a
        self.pivot[mesh] = V(pivot)
        return a

    def point(self, name, at):
        assembly(name, self.coll, V(at), point=name)

    def part(self, mesh, name, cage, paint, levels=1, smooth=True):
        """A part of assembly `mesh`, its cage in model space; `paint` is
        a material's name or a function of a face's middle (model space)
        giving one, so a coat can be patched."""
        asm, pivot = self.asm[mesh], self.pivot[mesh]
        data = bpy.data.meshes.new(name)
        data.from_pydata([tuple(blender(v - pivot)) for v in cage.verts],
                         [], cage.faces)
        data.update()
        for poly in data.polygons:
            poly.use_smooth = smooth
        obj = bpy.data.objects.new(name, data)
        self.coll.objects.link(obj)
        obj.parent = asm
        obj.location = (0, 0, 0)
        if levels > 0:
            mod = obj.modifiers.new("smooth", "SUBSURF")
            mod.levels = levels
            mod.render_levels = levels
        if not callable(paint):
            data.materials.append(self.m[paint])
            return obj
        # A patched coat is painted face by face after one subdivision
        # more than the surface needs, so that the patches' edges are
        # finer than its shading shows.
        obj.modifiers["smooth"].levels += 1
        bpy.context.view_layer.update()
        graph = bpy.context.evaluated_depsgraph_get()
        dense = bpy.data.meshes.new_from_object(obj.evaluated_get(graph))
        obj.modifiers.clear()
        obj.data = dense
        bpy.data.meshes.remove(data)
        dense.materials.clear()
        names = []
        for poly in dense.polygons:
            c = poly.center
            k = paint(V((c.x, c.z, -c.y)) + pivot)
            if k not in names:
                names.append(k)
                dense.materials.append(self.m[k])
            poly.material_index = names.index(k)
        return obj

    def save(self):
        out = os.path.join(ROOT, "models", f"{self.name}.blend")
        os.makedirs(os.path.dirname(out), exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=out, compress=True)
        print(f"wrote {out}: {len(self.coll.objects)} objects")


def frame(b, s):
    """The parts every species shares, from its description `s`."""
    b.assembly("body", V((0, 0, 0)))
    b.assembly("neck", s["withers"])
    b.assembly("head", s["poll"])
    b.assembly("fore_upper", s["shoulder"])
    b.assembly("fore_lower", s["carpus"])
    b.assembly("hind_upper", s["hip"])
    b.assembly("hind_lower", s["hock"])
    b.assembly("tail", s["tail_root"])
    for k in ("withers", "poll", "shoulder", "carpus", "fore_hoof", "hip",
              "hock", "hind_hoof", "tail_root", "muzzle"):
        b.point(k, s[k])


def leg(b, mesh, sections, paint, x, levels=1, sides=10):
    """A leg part through (y, z, half width, half depth) at side `x`,
    rounded off past its ends so that it meets the next part as a joint
    does, whichever way the two are bent."""
    pts = [(V((x, y, z)), w, d) for y, z, w, d in sections]
    (top, wt, dt), (top2, _, _) = pts[0], pts[1]
    (end2, _, _), (end, we, de) = pts[-2], pts[-1]
    up = (top - top2).normalized()
    down = (end - end2).normalized()
    pts = ([(top + up * 0.45 * dt, wt * 0.55, dt * 0.55)] + pts +
           [(end + down * 0.45 * de, we * 0.55, de * 0.55)])
    b.part(mesh, mesh, path(pts, RIGHT, sides=sides), paint, levels)


def hoof(b, mesh, top, bottom, top_r, bottom_r, paint, cloven=False):
    """A hoof from `top` to its sole at `bottom`; a cloven one is split
    into two claws."""
    if not cloven:
        cage = path([(top, top_r[0], top_r[1]),
                     (along(top, bottom, 0.6), (top_r[0] + bottom_r[0]) / 2,
                      (top_r[1] + bottom_r[1]) / 2),
                     (bottom, bottom_r[0], bottom_r[1])], RIGHT, sides=10)
        b.part(mesh, mesh + "_hoof", cage, paint, levels=1)
        return
    claws = []
    for s in (-1, 1):
        o = V((s * bottom_r[0] * 0.48, 0, 0))
        claws.append(path([(top + o * 0.5, top_r[0] * 0.55, top_r[1]),
                           (bottom + o + V((0, 0.004, 0)),
                            bottom_r[0] * 0.5, bottom_r[1])],
                          RIGHT, sides=8))
    b.part(mesh, mesh + "_hoof", merge(*claws), paint, levels=1)


def eyes(b, at, side, r=0.018, paint="eye"):
    cage = both(blob(at + V((side, 0, 0)), (r * 0.6, r, r * 1.1), 8, 5))
    b.part("head", "eyes", cage, paint, levels=1)


# -- the horse -------------------------------------------------------------

def horse():
    palette = {
        "coat": (0.29, 0.15, 0.08),
        "dark": (0.07, 0.055, 0.045),
        "muzzle": (0.16, 0.12, 0.10),
        "blaze": (0.86, 0.83, 0.77),
        "hoof": (0.20, 0.18, 0.16),
        "eye": (0.03, 0.025, 0.02),
    }
    finish = {
        "coat": dict(roughness=0.55, sheen=0.4),
        "dark": dict(roughness=0.7, sheen=0.3),
        "muzzle": dict(roughness=0.6),
        "blaze": dict(roughness=0.6, sheen=0.3),
        "hoof": dict(roughness=0.5),
        "eye": dict(roughness=0.1, coat=1.0),
    }
    poll = V((0, 1.84, 1.10))
    tip = V((0, 1.36, 1.44))
    s = dict(withers=V((0, 1.06, 0.62)), poll=poll,
             shoulder=V((0.17, 1.12, 0.52)), carpus=V((0.17, 0.46, 0.53)),
             fore_hoof=V((0.17, 0, 0.56)),
             hip=V((0.19, 1.18, -0.55)), hock=V((0.19, 0.54, -0.72)),
             hind_hoof=V((0.19, 0, -0.66)),
             tail_root=V((0, 1.42, -0.84)), muzzle=tip + V((0, -0.03, 0.02)))
    b = Beast("horse", palette, finish)
    frame(b, s)

    b.part("body", "trunk", barrel([
        (-0.82, 1.24, 0.08, 0.10, 0.12),
        (-0.77, 1.27, 0.22, 0.24, 0.27),
        (-0.64, 1.29, 0.30, 0.27, 0.32),
        (-0.44, 1.27, 0.32, 0.25, 0.31),
        (-0.20, 1.21, 0.34, 0.28, 0.36),
        (0.08, 1.19, 0.35, 0.30, 0.40),
        (0.32, 1.21, 0.31, 0.34, 0.40),
        (0.52, 1.20, 0.26, 0.30, 0.38),
        (0.68, 1.15, 0.20, 0.22, 0.30),
        (0.78, 1.12, 0.10, 0.12, 0.16),
    ]), "coat", levels=2)

    neck = [(V((0, 1.26, 0.46)), 0.20, 0.27),
            (V((0, 1.40, 0.72)), 0.17, 0.28),
            (V((0, 1.60, 0.92)), 0.13, 0.22),
            (V((0, 1.76, 1.04)), 0.10, 0.17),
            (V((0, 1.86, 1.11)), 0.085, 0.12)]
    b.part("neck", "neck", path(neck, sides=12), "coat", levels=2)
    # The mane lies along the crest, falling to one side.
    crest = [V((0.02, 1.47, 0.60)), V((0.04, 1.62, 0.74)),
             V((0.045, 1.78, 0.88)), V((0.04, 1.90, 1.00)),
             V((0.02, 1.95, 1.08))]
    b.part("neck", "mane", path([(c, 0.035, 0.07) for c in crest],
                                RIGHT, sides=8), "dark")

    def head_paint(c):
        t = (c - poll).dot((tip - poll).normalized()) / (tip - poll).length
        front = (c - poll).cross(V((1, 0, 0))).normalized()
        if t > 0.86:
            return "muzzle"
        if 0.15 < t < 0.75 and abs(c.x) < 0.03 and c.y > 1.4 + 0 * front.y:
            return "blaze"
        return "coat"

    head = [(poll + V((0, 0.02, -0.04)), 0.075, 0.08),
            (poll, 0.09, 0.12),
            (along(poll, tip, 0.18), 0.105, 0.15),
            (along(poll, tip, 0.42), 0.085, 0.10),
            (along(poll, tip, 0.72), 0.068, 0.078),
            (along(poll, tip, 0.92), 0.072, 0.082),
            (tip, 0.05, 0.055)]
    b.part("head", "skull", path(head, sides=12), head_paint, levels=2)
    b.part("head", "ears", both(ear(poll + V((0.055, 0.07, 0.0)),
                                    V((0.25, 1, 0.15)), 0.15, 0.04)),
           "coat")
    b.part("head", "forelock", path([(poll + V((0, 0.08, -0.02)), 0.03, 0.03),
                                     (poll + V((0, 0.04, 0.10)), 0.045, 0.02),
                                     (poll + V((0, -0.06, 0.15)), 0.03, 0.012)],
                                    RIGHT, sides=6), "dark")
    eyes(b, along(poll, tip, 0.2) + V((0, 0.05, 0)), 0.085)
    b.part("head", "nostrils", both(blob(tip + V((0.032, 0.0, -0.01)),
                                         (0.012, 0.018, 0.012), 6, 4)),
           "dark", levels=0)

    x = s["shoulder"].x
    leg(b, "fore_upper", [(1.10, 0.52, 0.07, 0.11), (0.92, 0.55, 0.085, 0.12),
                          (0.72, 0.54, 0.075, 0.09), (0.55, 0.53, 0.06, 0.07),
                          (0.45, 0.53, 0.056, 0.064)],
        lambda c: "dark" if c.y < 0.62 else "coat", x)
    leg(b, "fore_lower", [(0.48, 0.53, 0.058, 0.066), (0.40, 0.53, 0.048, 0.05),
                          (0.24, 0.535, 0.043, 0.047),
                          (0.15, 0.545, 0.052, 0.058),
                          (0.085, 0.565, 0.042, 0.046)], "dark", x)
    hoof(b, "fore_lower", V((x, 0.10, 0.565)), V((x, 0.0, 0.585)),
         (0.046, 0.05), (0.062, 0.07), "hoof")
    x = s["hip"].x
    leg(b, "hind_upper", [(1.20, -0.53, 0.09, 0.20), (0.98, -0.47, 0.11, 0.19),
                          (0.80, -0.57, 0.085, 0.12), (0.64, -0.67, 0.06, 0.085),
                          (0.53, -0.73, 0.055, 0.075)],
        lambda c: "dark" if c.y < 0.66 else "coat", x)
    leg(b, "hind_lower", [(0.56, -0.73, 0.056, 0.075), (0.44, -0.705, 0.046, 0.05),
                          (0.25, -0.69, 0.043, 0.047),
                          (0.15, -0.685, 0.052, 0.058),
                          (0.085, -0.67, 0.042, 0.046)], "dark", x)
    hoof(b, "hind_lower", V((x, 0.10, -0.67)), V((x, 0.0, -0.65)),
         (0.046, 0.05), (0.062, 0.07), "hoof")

    r = s["tail_root"]
    b.part("tail", "tail", path([(r + V((0, 0.03, 0.06)), 0.04, 0.04),
                                 (r, 0.06, 0.06),
                                 (r + V((0, -0.12, -0.10)), 0.08, 0.06),
                                 (r + V((0, -0.40, -0.16)), 0.09, 0.06),
                                 (r + V((0, -0.70, -0.14)), 0.08, 0.045),
                                 (r + V((0, -0.92, -0.10)), 0.03, 0.02)],
                                RIGHT, sides=8), "dark")
    b.save()


# -- the mountain cow ----------------------------------------------------

def spots(seed, threshold):
    def paint(c):
        n = noise(c, 1.7, seed)
        return "black" if n > threshold else "white"
    return paint


def cow():
    palette = {
        "white": (0.88, 0.86, 0.80),
        "black": (0.07, 0.065, 0.06),
        "pink": (0.82, 0.60, 0.56),
        "hoof": (0.16, 0.15, 0.14),
        "eye": (0.03, 0.025, 0.02),
    }
    finish = {
        "white": dict(roughness=0.7, sheen=0.3),
        "black": dict(roughness=0.65, sheen=0.3),
        "pink": dict(roughness=0.55),
        "hoof": dict(roughness=0.5),
        "eye": dict(roughness=0.1, coat=1.0),
    }
    poll = V((0, 1.22, 1.00))
    tip = V((0, 0.96, 1.44))
    s = dict(withers=V((0, 0.90, 0.56)), poll=poll,
             shoulder=V((0.19, 0.96, 0.48)), carpus=V((0.19, 0.36, 0.49)),
             fore_hoof=V((0.19, 0, 0.51)),
             hip=V((0.21, 1.02, -0.55)), hock=V((0.21, 0.45, -0.69)),
             hind_hoof=V((0.21, 0, -0.64)),
             tail_root=V((0, 1.26, -0.80)), muzzle=tip + V((0, -0.03, 0.01)))
    b = Beast("cow", palette, finish)
    frame(b, s)

    coat = spots(7, 0.66)

    def body_paint(c):
        if c.y < 0.72:
            return "white"
        return coat(c)

    def hips(p):
        # The hook bones stand out over the flanks.
        k = math.exp(-((p.z + 0.58) / 0.12) ** 2) * max(0.0, (p.y - 1.15) / 0.15)
        return p + V((math.copysign(0.05 * k, p.x), 0.02 * k, 0))

    b.part("body", "trunk", barrel([
        (-0.82, 1.13, 0.08, 0.10, 0.12),
        (-0.77, 1.15, 0.22, 0.16, 0.28),
        (-0.62, 1.13, 0.30, 0.18, 0.38),
        (-0.38, 1.07, 0.36, 0.23, 0.42),
        (-0.08, 1.03, 0.38, 0.26, 0.44),
        (0.22, 1.03, 0.36, 0.27, 0.43),
        (0.46, 1.04, 0.29, 0.26, 0.42),
        (0.64, 1.00, 0.22, 0.22, 0.40),
        (0.76, 0.98, 0.12, 0.14, 0.24),
    ], shape=hips), body_paint, levels=2)
    udder = blob(V((0, 0.62, -0.42)), (0.13, 0.10, 0.15), 10, 6)
    teats = [tube([V((sx * 0.06, 0.56, -0.42 + sz * 0.07)),
                   V((sx * 0.06, 0.48, -0.42 + sz * 0.07))], 0.014, sides=6)
             for sx in (-1, 1) for sz in (-1, 1)]
    b.part("body", "udder", merge(udder, *teats), "pink")

    neck = [(V((0, 1.00, 0.50)), 0.20, 0.32),
            (V((0, 1.06, 0.70)), 0.17, 0.28),
            (V((0, 1.14, 0.88)), 0.135, 0.22),
            (V((0, 1.21, 1.00)), 0.11, 0.16)]
    b.part("neck", "neck", path(neck, sides=12), coat, levels=2)
    b.part("neck", "dewlap", path([(V((0, 0.78, 0.62)), 0.035, 0.10),
                                   (V((0, 0.84, 0.80)), 0.04, 0.10),
                                   (V((0, 0.96, 0.96)), 0.03, 0.06)],
                                  RIGHT, sides=8), "white")

    def head_paint(c):
        t = (c - poll).dot((tip - poll).normalized()) / (tip - poll).length
        if t > 0.82:
            return "black"
        # Black rings about the eyes, the fjällko's spectacles.
        for e in (eye_at + V((0.09, 0, 0)), eye_at + V((-0.09, 0, 0))):
            if (c - e).length < 0.05:
                return "black"
        return "white"

    eye_at = along(poll, tip, 0.22) + V((0, 0.04, 0))
    head = [(poll + V((0, 0.0, -0.05)), 0.09, 0.08),
            (poll, 0.11, 0.11),
            (along(poll, tip, 0.22), 0.115, 0.13),
            (along(poll, tip, 0.52), 0.088, 0.11),
            (along(poll, tip, 0.80), 0.078, 0.095),
            (along(poll, tip, 0.94), 0.085, 0.085),
            (tip, 0.06, 0.05)]
    b.part("head", "skull", path(head, sides=12), head_paint, levels=2)
    b.part("head", "poll_tuft", blob(poll + V((0, 0.07, 0.0)),
                                     (0.07, 0.04, 0.05), 8, 5), "white")
    b.part("head", "ears", both(ear(poll + V((0.09, 0.02, 0.0)),
                                    V((1, 0.05, -0.2)), 0.17, 0.06,
                                    thick=0.014)), "black")
    eyes(b, eye_at, 0.10)
    b.part("head", "nostrils", both(blob(tip + V((0.035, -0.005, -0.005)),
                                         (0.014, 0.016, 0.012), 6, 4)),
           "pink", levels=0)

    def leg_paint(c):
        return "black" if noise(c, 3.0, 3) > 0.74 else "white"

    x = s["shoulder"].x
    leg(b, "fore_upper", [(0.94, 0.48, 0.08, 0.12), (0.74, 0.50, 0.09, 0.11),
                          (0.52, 0.49, 0.07, 0.08), (0.36, 0.49, 0.056, 0.064)],
        leg_paint, x)
    leg(b, "fore_lower", [(0.38, 0.49, 0.058, 0.064), (0.30, 0.49, 0.046, 0.05),
                          (0.15, 0.495, 0.043, 0.046),
                          (0.09, 0.505, 0.05, 0.052)], leg_paint, x)
    hoof(b, "fore_lower", V((x, 0.08, 0.51)), V((x, 0.0, 0.535)),
         (0.05, 0.05), (0.06, 0.07), "hoof", cloven=True)
    x = s["hip"].x
    leg(b, "hind_upper", [(1.02, -0.54, 0.10, 0.22), (0.80, -0.50, 0.12, 0.17),
                          (0.62, -0.60, 0.08, 0.11), (0.45, -0.70, 0.055, 0.075)],
        leg_paint, x)
    leg(b, "hind_lower", [(0.48, -0.70, 0.056, 0.072), (0.32, -0.665, 0.046, 0.05),
                          (0.15, -0.65, 0.043, 0.046),
                          (0.09, -0.645, 0.05, 0.052)], leg_paint, x)
    hoof(b, "hind_lower", V((x, 0.08, -0.645)), V((x, 0.0, -0.625)),
         (0.05, 0.05), (0.06, 0.07), "hoof", cloven=True)

    r = s["tail_root"]
    b.part("tail", "tail", merge(
        path([(r + V((0, 0.02, 0.05)), 0.035, 0.035),
              (r, 0.04, 0.04),
              (r + V((0, -0.10, -0.06)), 0.03, 0.03),
              (r + V((0, -0.60, -0.07)), 0.018, 0.018),
              (r + V((0, -0.72, -0.06)), 0.02, 0.02)], RIGHT, sides=6),
        path([(r + V((0, -0.70, -0.06)), 0.03, 0.03),
              (r + V((0, -0.80, -0.06)), 0.045, 0.04),
              (r + V((0, -0.92, -0.05)), 0.02, 0.02)], RIGHT, sides=6)),
        lambda c: "black" if c.y < r.y - 0.66 else "white")
    b.save()


# -- the Gotland sheep ---------------------------------------------------

def sheep():
    palette = {
        "fleece": (0.56, 0.55, 0.53),
        "face": (0.09, 0.08, 0.08),
        "hoof": (0.15, 0.14, 0.13),
        "eye": (0.25, 0.18, 0.06),
    }
    finish = {
        "fleece": dict(roughness=0.95, sheen=0.6),
        "face": dict(roughness=0.7, sheen=0.2),
        "hoof": dict(roughness=0.5),
        "eye": dict(roughness=0.1, coat=1.0),
    }
    poll = V((0, 0.82, 0.48))
    tip = V((0, 0.63, 0.70))
    s = dict(withers=V((0, 0.52, 0.30)), poll=poll,
             shoulder=V((0.10, 0.56, 0.28)), carpus=V((0.10, 0.22, 0.29)),
             fore_hoof=V((0.10, 0, 0.30)),
             hip=V((0.11, 0.58, -0.30)), hock=V((0.11, 0.24, -0.38)),
             hind_hoof=V((0.11, 0, -0.355)),
             tail_root=V((0, 0.72, -0.46)), muzzle=tip + V((0, -0.02, 0.0)))
    b = Beast("sheep", palette, finish)
    frame(b, s)

    def curls(p):
        n = noise(p, 26.0, 11) - 0.5
        m = noise(p, 11.0, 5) - 0.5
        c = V((p.x, 0.62, 0))
        out = (p - c)
        out.z = 0
        if out.length < 1e-6:
            return p
        return p + out.normalized() * (0.05 * n + 0.025 * m)

    fleece = "fleece"

    b.part("body", "fleece", barrel([
        (-0.50, 0.63, 0.08, 0.08, 0.08),
        (-0.46, 0.64, 0.19, 0.17, 0.20),
        (-0.34, 0.64, 0.25, 0.20, 0.25),
        (-0.12, 0.62, 0.27, 0.21, 0.27),
        (0.10, 0.62, 0.26, 0.21, 0.26),
        (0.28, 0.62, 0.22, 0.20, 0.24),
        (0.38, 0.61, 0.15, 0.15, 0.18),
        (0.44, 0.60, 0.07, 0.08, 0.09),
    ], sides=28, shape=curls, rings=22), fleece, levels=1)

    def neck_curls(p):
        return p + V((0, 0, 0)) * 0 + (p - V((0, p.y, p.z))) * 0.0 + \
            V((0, 0.025 * (noise(p, 22.0, 4) - 0.5), 0))

    b.part("neck", "neck", path([(V((0, 0.60, 0.24)), 0.13, 0.17),
                                 (V((0, 0.72, 0.38)), 0.11, 0.13),
                                 (V((0, 0.80, 0.46)), 0.075, 0.085)],
                                sides=14, shape=neck_curls), fleece)
    head = [(poll + V((0, 0.0, -0.03)), 0.05, 0.05),
            (poll, 0.06, 0.07),
            (along(poll, tip, 0.30), 0.062, 0.072),
            (along(poll, tip, 0.70), 0.044, 0.05),
            (along(poll, tip, 0.92), 0.036, 0.038),
            (tip, 0.025, 0.026)]
    b.part("head", "face", path(head, sides=10), "face", levels=2)
    b.part("head", "topknot", blob(poll + V((0, 0.045, -0.01)),
                                   (0.055, 0.04, 0.06), 8, 5,
                                   shape=lambda u, p: p * (1 + 0.15 * (noise(p, 40, 1) - 0.5))),
           "fleece")
    b.part("head", "ears", both(ear(poll + V((0.05, 0.0, 0.0)),
                                    V((1, 0.1, -0.1)), 0.10, 0.03,
                                    thick=0.008)), "face")
    eyes(b, along(poll, tip, 0.30) + V((0, 0.025, 0)), 0.055, r=0.011)

    x = s["shoulder"].x
    leg(b, "fore_upper", [(0.58, 0.28, 0.06, 0.07), (0.40, 0.29, 0.05, 0.06),
                          (0.30, 0.29, 0.028, 0.032), (0.22, 0.29, 0.026, 0.03)],
        "face", x, sides=8)
    leg(b, "fore_lower", [(0.24, 0.29, 0.027, 0.03), (0.12, 0.29, 0.02, 0.022),
                          (0.05, 0.295, 0.022, 0.024)], "face", x, sides=8)
    hoof(b, "fore_lower", V((x, 0.045, 0.297)), V((x, 0.0, 0.31)),
         (0.022, 0.024), (0.028, 0.034), "hoof", cloven=True)
    x = s["hip"].x
    leg(b, "hind_upper", [(0.60, -0.30, 0.08, 0.11), (0.42, -0.28, 0.065, 0.09),
                          (0.32, -0.34, 0.032, 0.04), (0.24, -0.38, 0.026, 0.034)],
        "face", x, sides=8)
    leg(b, "hind_lower", [(0.26, -0.38, 0.027, 0.034), (0.12, -0.36, 0.02, 0.022),
                          (0.05, -0.355, 0.022, 0.024)], "face", x, sides=8)
    hoof(b, "hind_lower", V((x, 0.045, -0.355)), V((x, 0.0, -0.345)),
         (0.022, 0.024), (0.028, 0.034), "hoof", cloven=True)

    r = s["tail_root"]
    b.part("tail", "tail", path([(r + V((0, 0.01, 0.04)), 0.04, 0.04),
                                 (r + V((0, -0.05, -0.04)), 0.05, 0.045),
                                 (r + V((0, -0.15, -0.06)), 0.03, 0.03)],
                                RIGHT, sides=8), "fleece")
    b.save()


# -- the reindeer ----------------------------------------------------------

def antler(points, radius, tines=(), sides=6):
    """A beam through `points`, tapering from `radius`, with tines, each
    (index along the beam, direction, length)."""
    n = len(points)
    parts = [tube(points, radius, sides=sides)]
    beam = loft([(p, radius * (1 - 0.6 * i / (n - 1)),
                  radius * (1 - 0.6 * i / (n - 1)))
                 for i, p in enumerate(points)],
                RIGHT if abs((points[-1] - points[0]).normalized().x) < 0.9
                else UP, sides=sides)
    parts = [beam]
    for i, d, length in tines:
        a = points[i]
        r = radius * (1 - 0.6 * i / (n - 1)) * 0.75
        parts.append(loft([(a, r, r), (a + d.normalized() * length * 0.6,
                                       r * 0.7, r * 0.7),
                           (a + d.normalized() * length, r * 0.3, r * 0.3)],
                          RIGHT if abs(d.normalized().x) < 0.9 else UP,
                          sides=sides))
    return merge(*parts)


def reindeer():
    palette = {
        "coat": (0.42, 0.36, 0.29),
        "pale": (0.82, 0.79, 0.72),
        "dark": (0.20, 0.16, 0.12),
        "antler": (0.58, 0.50, 0.40),
        "hoof": (0.13, 0.12, 0.11),
        "eye": (0.03, 0.025, 0.02),
    }
    finish = {
        "coat": dict(roughness=0.85, sheen=0.4),
        "pale": dict(roughness=0.9, sheen=0.5),
        "dark": dict(roughness=0.8, sheen=0.3),
        "antler": dict(roughness=0.55),
        "hoof": dict(roughness=0.5),
        "eye": dict(roughness=0.1, coat=1.0),
    }
    poll = V((0, 1.30, 0.76))
    tip = V((0, 1.10, 1.16))
    s = dict(withers=V((0, 0.84, 0.44)), poll=poll,
             shoulder=V((0.14, 0.88, 0.38)), carpus=V((0.14, 0.34, 0.39)),
             fore_hoof=V((0.14, 0, 0.42)),
             hip=V((0.15, 0.94, -0.42)), hock=V((0.15, 0.40, -0.56)),
             hind_hoof=V((0.15, 0, -0.52)),
             tail_root=V((0, 1.06, -0.66)), muzzle=tip + V((0, -0.03, 0.0)))
    b = Beast("reindeer", palette, finish)
    frame(b, s)

    def body_paint(c):
        if c.z < -0.58 and c.y > 0.8:
            return "pale"
        if c.y < 0.70:
            return "pale"
        if c.y < 0.76 + 0.05 * (noise(c, 8, 9) - 0.5):
            return "pale"
        return "coat"

    b.part("body", "trunk", barrel([
        (-0.68, 0.92, 0.08, 0.10, 0.10),
        (-0.62, 0.94, 0.18, 0.18, 0.22),
        (-0.48, 0.95, 0.24, 0.20, 0.27),
        (-0.24, 0.93, 0.26, 0.20, 0.30),
        (0.06, 0.93, 0.27, 0.20, 0.31),
        (0.30, 0.96, 0.24, 0.20, 0.32),
        (0.46, 0.95, 0.20, 0.17, 0.32),
        (0.56, 0.90, 0.12, 0.10, 0.20),
    ]), body_paint, levels=2)

    def shaggy(p):
        # The pale mane hangs shaggy under the neck.
        if p.y < 1.05:
            return p + V((0, -0.04 * noise(p, 18, 2), 0))
        return p

    b.part("neck", "neck", path([(V((0, 0.94, 0.38)), 0.16, 0.26),
                                 (V((0, 1.08, 0.54)), 0.14, 0.25),
                                 (V((0, 1.20, 0.68)), 0.10, 0.16),
                                 (V((0, 1.29, 0.76)), 0.075, 0.11)],
                                sides=12, shape=shaggy),
           lambda c: "pale" if c.y < 1.14 + 0.5 * (c.z - 0.5) +
           0.06 * (noise(c, 9, 4) - 0.5) else "coat", levels=2)
    head = [(poll + V((0, 0.0, -0.04)), 0.06, 0.06),
            (poll, 0.075, 0.085),
            (along(poll, tip, 0.22), 0.08, 0.10),
            (along(poll, tip, 0.60), 0.064, 0.078),
            (along(poll, tip, 0.88), 0.062, 0.07),
            (tip, 0.045, 0.048)]
    b.part("head", "skull", path(head, sides=12),
           lambda c: "dark" if (c - tip).length < 0.13 else "coat", levels=2)
    b.part("head", "ears", both(ear(poll + V((0.06, 0.03, -0.02)),
                                    V((1, 0.5, -0.3)), 0.11, 0.035,
                                    thick=0.01)), "coat")
    eyes(b, along(poll, tip, 0.25) + V((0, 0.035, 0)), 0.07, r=0.014)
    # The antlers sweep back and up, then forward, with a brow tine (the
    # shovel) forward over the face and a few tines off the beam.
    beam = [V((0.05, 1.36, 0.74)), V((0.14, 1.48, 0.64)),
            V((0.24, 1.62, 0.57)), V((0.31, 1.76, 0.60)),
            V((0.32, 1.86, 0.71)), V((0.27, 1.91, 0.83)),
            V((0.21, 1.92, 0.91))]
    one = antler(beam, 0.022, tines=[
        (0, V((-0.15, 0.2, 1)), 0.24),
        (1, V((0.0, 0.45, 1)), 0.20),
        (3, V((0.3, 0.3, -0.8)), 0.12),
        (5, V((0.2, 0.7, 0.3)), 0.10),
        (6, V((-0.2, 0.6, 0.5)), 0.08)])
    b.part("head", "antlers", both(one), "antler")

    x = s["shoulder"].x
    leg(b, "fore_upper", [(0.88, 0.38, 0.06, 0.10), (0.70, 0.40, 0.07, 0.09),
                          (0.50, 0.39, 0.05, 0.06), (0.34, 0.39, 0.042, 0.048)],
        lambda c: "coat" if c.y > 0.62 else "dark", x)
    leg(b, "fore_lower", [(0.36, 0.39, 0.044, 0.048), (0.26, 0.39, 0.034, 0.036),
                          (0.12, 0.40, 0.032, 0.035),
                          (0.07, 0.41, 0.04, 0.042)],
        "dark", x)
    hoof(b, "fore_lower", V((x, 0.06, 0.41)), V((x, 0.0, 0.44)),
         (0.045, 0.045), (0.06, 0.07), "hoof", cloven=True)
    x = s["hip"].x
    leg(b, "hind_upper", [(0.94, -0.42, 0.08, 0.16), (0.74, -0.38, 0.10, 0.14),
                          (0.56, -0.48, 0.06, 0.08), (0.40, -0.57, 0.042, 0.06)],
        lambda c: "coat" if c.y > 0.62 else "dark", x)
    leg(b, "hind_lower", [(0.43, -0.565, 0.044, 0.058), (0.28, -0.54, 0.034, 0.036),
                          (0.12, -0.525, 0.032, 0.035),
                          (0.07, -0.52, 0.04, 0.042)],
        "dark", x)
    hoof(b, "hind_lower", V((x, 0.06, -0.52)), V((x, 0.0, -0.50)),
         (0.045, 0.045), (0.06, 0.07), "hoof", cloven=True)

    r = s["tail_root"]
    b.part("tail", "tail", path([(r + V((0, 0.0, 0.04)), 0.04, 0.03),
                                 (r + V((0, -0.04, -0.04)), 0.045, 0.035),
                                 (r + V((0, -0.13, -0.06)), 0.025, 0.02)],
                                RIGHT, sides=8), "pale")
    b.save()


# -- the moose -------------------------------------------------------------

def palm(base, sides=8):
    """A bull moose's palmate antler on the left: a beam out from the
    skull to a broad, cupped palm with points along its rim."""
    out = V((1, 0.18, -0.12)).normalized()
    up = V((-0.35, 1, 0.1)).normalized()
    fore = out.cross(up).normalized()
    if fore.z < 0:
        fore = -fore
    beam = loft([(base, 0.035, 0.035),
                 (base + out * 0.14, 0.03, 0.03),
                 (base + out * 0.24 + up * 0.04, 0.04, 0.025)],
                UP, sides=sides)
    # The palm: a curved plate spread up and forward and back.
    c = base + out * 0.36 + up * 0.14
    cage = Cage()
    rows, cols = 5, 7
    grid = []
    for i in range(rows):
        row = []
        for j in range(cols):
            u = i / (rows - 1)          # out from the beam
            v = j / (cols - 1) - 0.5    # back to front
            w = 0.12 + 0.34 * u - 0.1 * u * u
            p = (base + out * (0.22 + 0.36 * u) + up * (0.02 + 0.22 * u) +
                 fore * (v * 2 * w) - up * (0.10 * (2 * v) ** 2 * u))
            row.append(p)
        grid.append(row)
    thick = 0.02
    n = up.cross(fore).normalized()
    top, bot = [], []
    for row in grid:
        top.append([])
        bot.append([])
        for p in row:
            cage.verts.append(p + n * thick)
            top[-1].append(len(cage.verts) - 1)
            cage.verts.append(p - n * thick)
            bot[-1].append(len(cage.verts) - 1)
    for i in range(rows - 1):
        for j in range(cols - 1):
            cage.faces.append([top[i][j], top[i][j + 1], top[i + 1][j + 1],
                               top[i + 1][j]])
            cage.faces.append([bot[i][j], bot[i + 1][j], bot[i + 1][j + 1],
                               bot[i][j + 1]])
    edge = ([(0, j) for j in range(cols)] +
            [(i, cols - 1) for i in range(1, rows)] +
            [(rows - 1, j) for j in range(cols - 2, -1, -1)] +
            [(i, 0) for i in range(rows - 2, 0, -1)])
    for (a, b), (c2, d) in zip(edge, edge[1:] + edge[:1]):
        cage.faces.append([top[a][b], bot[a][b], bot[c2][d], top[c2][d]])
    # Orient by the plate's mid-surface: each face's own centre pushed
    # back onto it.
    for k, f in enumerate(cage.faces):
        pts = [cage.verts[i] for i in f]
        nn = V((0, 0, 0))
        for i in range(len(pts)):
            nn += pts[i].cross(pts[(i + 1) % len(pts)])
        centre = sum(pts, V((0, 0, 0))) / len(pts)
        mid = centre - n * (centre - c).dot(n)
        # Rim faces face out from the plate's middle within its plane.
        if abs((centre - mid).dot(n)) < 1e-4:
            ref = mid - (c + out * 0.10 + up * 0.04)
            ref = ref - n * ref.dot(n)
            if nn.dot(ref) < 0:
                cage.faces[k] = list(reversed(f))
        elif nn.dot(centre - mid) < 0:
            cage.faces[k] = list(reversed(f))
    points = []
    for j in range(cols):
        p = grid[-1][j]
        d = (out * 0.6 + up * 0.8 + fore * (j / (cols - 1) - 0.5) * 0.8)
        points.append(loft([(p - d.normalized() * 0.02, 0.022, 0.016),
                            (p + d.normalized() * 0.07, 0.015, 0.011),
                            (p + d.normalized() * 0.12, 0.004, 0.004)],
                           n, sides=6))
    return merge(beam, cage, *points)


def moose():
    palette = {
        "coat": (0.17, 0.12, 0.09),
        "brown": (0.30, 0.22, 0.15),
        "legs": (0.62, 0.58, 0.52),
        "antler": (0.72, 0.65, 0.52),
        "nose": (0.10, 0.08, 0.07),
        "hoof": (0.10, 0.09, 0.08),
        "eye": (0.03, 0.025, 0.02),
    }
    finish = {
        "coat": dict(roughness=0.8, sheen=0.35),
        "brown": dict(roughness=0.8, sheen=0.35),
        "legs": dict(roughness=0.85, sheen=0.3),
        "antler": dict(roughness=0.6),
        "nose": dict(roughness=0.6),
        "hoof": dict(roughness=0.5),
        "eye": dict(roughness=0.1, coat=1.0),
    }
    poll = V((0, 1.86, 1.12))
    tip = V((0, 1.36, 1.70))
    s = dict(withers=V((0, 1.32, 0.70)), poll=poll,
             shoulder=V((0.20, 1.50, 0.60)), carpus=V((0.20, 0.60, 0.62)),
             fore_hoof=V((0.20, 0, 0.65)),
             hip=V((0.21, 1.50, -0.62)), hock=V((0.21, 0.68, -0.84)),
             hind_hoof=V((0.21, 0, -0.78)),
             tail_root=V((0, 1.66, -0.96)), muzzle=tip + V((0, -0.06, -0.02)))
    b = Beast("moose", palette, finish)
    frame(b, s)

    b.part("body", "trunk", barrel([
        (-1.00, 1.48, 0.08, 0.10, 0.12),
        (-0.94, 1.50, 0.22, 0.20, 0.26),
        (-0.78, 1.50, 0.30, 0.24, 0.34),
        (-0.50, 1.47, 0.35, 0.26, 0.42),
        (-0.15, 1.47, 0.38, 0.30, 0.45),
        (0.18, 1.52, 0.37, 0.38, 0.46),
        (0.42, 1.55, 0.32, 0.40, 0.46),
        (0.64, 1.50, 0.25, 0.33, 0.44),
        (0.80, 1.42, 0.16, 0.22, 0.32),
        (0.88, 1.38, 0.08, 0.10, 0.15),
    ]), lambda c: "brown" if c.y > 1.75 and c.z > -0.2 else "coat", levels=2)
    b.part("neck", "neck", path([(V((0, 1.50, 0.64)), 0.20, 0.36),
                                 (V((0, 1.62, 0.84)), 0.17, 0.30),
                                 (V((0, 1.76, 1.00)), 0.13, 0.22),
                                 (V((0, 1.84, 1.10)), 0.10, 0.17)], sides=12),
           lambda c: "brown" if c.y > 1.78 else "coat", levels=2)
    # A long head with the overhanging, bulbous muzzle.
    head = [(poll + V((0, 0.0, -0.05)), 0.075, 0.08),
            (poll, 0.09, 0.11),
            (along(poll, tip, 0.18), 0.11, 0.15),
            (along(poll, tip, 0.45), 0.085, 0.115),
            (along(poll, tip, 0.72), 0.08, 0.11),
            (along(poll, tip, 0.90) + V((0, -0.01, 0)), 0.095, 0.125),
            (tip + V((0, -0.03, 0)), 0.07, 0.08)]
    b.part("head", "skull", path(head, sides=12),
           lambda c: "nose" if (c - tip).length < 0.1 else "brown", levels=2)
    # The bell hangs from the throat.
    throat = along(poll, tip, 0.22) + V((0, -0.14, -0.06))
    b.part("head", "bell", path([(throat + V((0, 0.06, 0.0)), 0.04, 0.06),
                                 (throat + V((0, -0.08, -0.02)), 0.03, 0.05),
                                 (throat + V((0, -0.24, -0.04)), 0.025, 0.035),
                                 (throat + V((0, -0.30, -0.04)), 0.01, 0.01)],
                                RIGHT, sides=8), "coat")
    b.part("head", "ears", both(ear(poll + V((0.07, 0.05, -0.04)),
                                    V((1, 0.35, -0.4)), 0.22, 0.06,
                                    thick=0.014)), "brown")
    eyes(b, along(poll, tip, 0.25) + V((0, 0.06, 0)), 0.085, r=0.016)
    b.part("head", "antlers", both(palm(poll + V((0.07, 0.08, 0.0)))),
           "antler")

    x = s["shoulder"].x
    leg(b, "fore_upper", [(1.46, 0.60, 0.07, 0.13), (1.20, 0.63, 0.095, 0.13),
                          (0.95, 0.62, 0.075, 0.095), (0.75, 0.62, 0.062, 0.072),
                          (0.60, 0.62, 0.056, 0.064)],
        lambda c: "coat" if c.y > 1.0 else "legs", x)
    leg(b, "fore_lower", [(0.63, 0.62, 0.058, 0.066), (0.50, 0.62, 0.046, 0.052),
                          (0.22, 0.625, 0.042, 0.047),
                          (0.12, 0.635, 0.048, 0.054)], "legs", x)
    hoof(b, "fore_lower", V((x, 0.10, 0.64)), V((x, 0.0, 0.67)),
         (0.05, 0.055), (0.07, 0.085), "hoof", cloven=True)
    x = s["hip"].x
    leg(b, "hind_upper", [(1.50, -0.60, 0.10, 0.22), (1.20, -0.55, 0.12, 0.19),
                          (0.98, -0.66, 0.09, 0.12), (0.80, -0.76, 0.065, 0.09),
                          (0.67, -0.85, 0.056, 0.08)],
        lambda c: "coat" if c.y > 1.05 else "legs", x)
    leg(b, "hind_lower", [(0.71, -0.85, 0.056, 0.076), (0.55, -0.81, 0.046, 0.052),
                          (0.22, -0.785, 0.042, 0.047),
                          (0.12, -0.78, 0.048, 0.054)], "legs", x)
    hoof(b, "hind_lower", V((x, 0.10, -0.78)), V((x, 0.0, -0.76)),
         (0.05, 0.055), (0.07, 0.085), "hoof", cloven=True)

    r = s["tail_root"]
    b.part("tail", "tail", path([(r + V((0, 0.0, 0.04)), 0.04, 0.03),
                                 (r + V((0, -0.06, -0.03)), 0.045, 0.035),
                                 (r + V((0, -0.14, -0.04)), 0.02, 0.02)],
                                RIGHT, sides=8), "coat")
    b.save()


# -- the roe deer ----------------------------------------------------------

def roe_deer():
    palette = {
        "coat": (0.46, 0.35, 0.25),
        "rump": (0.92, 0.90, 0.85),
        "nose": (0.05, 0.045, 0.04),
        "chin": (0.88, 0.86, 0.80),
        "antler": (0.40, 0.32, 0.24),
        "tip": (0.85, 0.80, 0.70),
        "hoof": (0.10, 0.09, 0.08),
        "eye": (0.03, 0.025, 0.02),
    }
    finish = {
        "coat": dict(roughness=0.75, sheen=0.4),
        "rump": dict(roughness=0.85, sheen=0.4),
        "nose": dict(roughness=0.3, coat=0.5),
        "chin": dict(roughness=0.8),
        "antler": dict(roughness=0.8),
        "tip": dict(roughness=0.5),
        "hoof": dict(roughness=0.5),
        "eye": dict(roughness=0.1, coat=1.0),
    }
    poll = V((0, 1.02, 0.50))
    tip = V((0, 0.90, 0.72))
    s = dict(withers=V((0, 0.58, 0.34)), poll=poll,
             shoulder=V((0.08, 0.64, 0.28)), carpus=V((0.08, 0.27, 0.285)),
             fore_hoof=V((0.08, 0, 0.30)),
             hip=V((0.085, 0.72, -0.30)), hock=V((0.085, 0.32, -0.41)),
             hind_hoof=V((0.085, 0, -0.38)),
             tail_root=V((0, 0.84, -0.45)), muzzle=tip + V((0, -0.02, 0.0)))
    b = Beast("roe_deer", palette, finish)
    frame(b, s)

    def body_paint(c):
        if (c.z < -0.40 and abs(c.x) < 0.12 + 0.02 * (c.y - 0.7) / 0.1
                and 0.60 < c.y < 0.88):
            return "rump"
        return "coat"

    b.part("body", "trunk", barrel([
        (-0.47, 0.72, 0.06, 0.08, 0.08),
        (-0.43, 0.74, 0.13, 0.13, 0.15),
        (-0.33, 0.75, 0.16, 0.14, 0.18),
        (-0.12, 0.73, 0.17, 0.13, 0.20),
        (0.10, 0.72, 0.17, 0.13, 0.21),
        (0.26, 0.72, 0.15, 0.12, 0.21),
        (0.38, 0.69, 0.11, 0.10, 0.17),
        (0.44, 0.67, 0.06, 0.06, 0.09),
    ], sides=16), body_paint, levels=2)
    b.part("neck", "neck", path([(V((0, 0.70, 0.28)), 0.09, 0.13),
                                 (V((0, 0.82, 0.38)), 0.07, 0.10),
                                 (V((0, 0.94, 0.46)), 0.056, 0.075),
                                 (V((0, 1.01, 0.50)), 0.05, 0.06)], sides=10),
           "coat", levels=2)
    head = [(poll + V((0, 0.0, -0.03)), 0.04, 0.04),
            (poll, 0.048, 0.052),
            (along(poll, tip, 0.22), 0.055, 0.06),
            (along(poll, tip, 0.60), 0.04, 0.045),
            (along(poll, tip, 0.88), 0.03, 0.034),
            (tip, 0.022, 0.024)]

    def head_paint(c):
        if (c - tip).length < 0.045:
            return "nose"
        if (c - tip).length < 0.08 and c.y < tip.y + 0.01:
            return "chin"
        return "coat"

    b.part("head", "skull", path(head, sides=10), head_paint, levels=2)
    b.part("head", "ears", both(ear(poll + V((0.035, 0.03, -0.01)),
                                    V((0.7, 1, -0.3)), 0.13, 0.045,
                                    thick=0.008)), "coat")
    eyes(b, along(poll, tip, 0.28) + V((0, 0.025, 0)), 0.048, r=0.012)
    beam = [V((0.03, 1.07, 0.50)), V((0.035, 1.14, 0.50)),
            V((0.04, 1.22, 0.49)), V((0.035, 1.28, 0.47))]
    one = antler(beam, 0.012, tines=[(1, V((0, 0.5, 1)), 0.06),
                                     (2, V((0, 0.4, -1)), 0.05)], sides=5)
    b.part("head", "antlers", both(one),
           lambda c: "tip" if c.y > 1.25 else "antler", levels=1)

    x = s["shoulder"].x
    leg(b, "fore_upper", [(0.66, 0.28, 0.05, 0.065), (0.50, 0.29, 0.042, 0.052),
                          (0.36, 0.285, 0.026, 0.032), (0.27, 0.285, 0.022, 0.026)],
        "coat", x, sides=8)
    leg(b, "fore_lower", [(0.29, 0.285, 0.022, 0.026), (0.12, 0.29, 0.016, 0.019),
                          (0.05, 0.295, 0.018, 0.02)], "coat", x, sides=8)
    hoof(b, "fore_lower", V((x, 0.04, 0.297)), V((x, 0.0, 0.31)),
         (0.017, 0.019), (0.022, 0.03), "hoof", cloven=True)
    x = s["hip"].x
    leg(b, "hind_upper", [(0.74, -0.30, 0.075, 0.10), (0.56, -0.28, 0.06, 0.085),
                          (0.42, -0.36, 0.03, 0.04), (0.32, -0.41, 0.022, 0.03)],
        "coat", x, sides=8)
    leg(b, "hind_lower", [(0.34, -0.41, 0.022, 0.03), (0.12, -0.385, 0.016, 0.019),
                          (0.05, -0.38, 0.018, 0.02)], "coat", x, sides=8)
    hoof(b, "hind_lower", V((x, 0.04, -0.38)), V((x, 0.0, -0.37)),
         (0.017, 0.019), (0.022, 0.03), "hoof", cloven=True)

    r = s["tail_root"]
    b.part("tail", "tail", blob(r + V((0, -0.02, -0.01)), (0.03, 0.03, 0.02),
                                6, 4), "rump")
    b.save()


SPECIES = {"horse": horse, "cow": cow, "sheep": sheep, "reindeer": reindeer,
           "moose": moose, "roe_deer": roe_deer}

if __name__ == "__main__":
    names = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    for name in names or SPECIES:
        SPECIES[name]()
