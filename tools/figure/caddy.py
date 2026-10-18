"""Builds models/caddy.blend: a Volkswagen Caddy of the first series (Typ
14, 1982-1992), the pickup Westmoreland and Sarajevo made of the Golf,
here in jade metallic under the high fibreglass hardtop.

    Blender -b --factory-startup --python tools/figure/caddy.py

It is authored life size from the factory's figures -- 4.37 m long, 1.64
m wide, a 2.625 m wheelbase, 1.905 m to the top of the hardtop, 175/70
R13 tyres -- and from a side-on photograph scaled by them. Its frame is
the game's, as the jeep's is: x to the driver's left, y up, z forward,
the origin on the centre line midway between the axles and at their
height. Heights in this script are measured from the ground, which lies
a wheel radius below the origin; `P` places them.

The body is lofted from cross-sections taken every few centimetres along
its length: the nose (bonnet, wings, the front wheel arches) and the tub
behind it, whose section is a U -- the cab's doors and floor, then the
bed's sides, the wheel housings standing in it, and its floor. Each
section is a ring of points whose runs (the crease along the waist, the
rolled lips of the arches, the wing's crest) are kept apart, so that the
crisp edges of Giugiaro's folded-paper Golf stay crisp and the panels
between them shade smoothly. The greenhouse is planar like the car's:
the windows lie in sides that lean in, the windscreen in one raked plane,
the roof is lofted between them, and the C-pillars and the cab's back,
with its window, are a solidified shell lofted round the cab in plan.

Assemblies, each exported in its empty's frame by
tools/figure/export_model.py: the body; the glass; the hardtop; the
tailgate, about its hinge; one wheel (the right front; the game draws it
four times, turned about for the left); the steering wheel, about its
column; the column; and the seats with the pedals. The empties marked as
points are the four axles, the seats (the cushion where the rider sits),
the pedals and the passenger's footrests, the steering wheel's hub, a
point down its column and its rim where the hands hold it, the lamps,
the tailgate's hinge, the tow ball, and a place beside each door to
step in from.

Every material carries the whole of its Principled BSDF -- metallic,
roughness, clear coat, transmission, emission -- which the exporter
writes beside the colour for the renderer to use.

Rebuilding discards hand edits to the .blend.
"""

import math
import os
import sys

import bpy
from mathutils import Vector, geometry

sys.path.insert(0, os.path.dirname(__file__))
from shapes import (V, RIGHT, UP, FORWARD, Cage, assembly, blender,  # noqa
                    blob, loft, merge, part, slab, to_linear, torus, tube)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "models", "caddy.blend")

# Key dimensions, metres; heights above the ground.
R = 0.288           # the wheel's radius, 175/70 R13
AXLE = 1.3125       # the origin to either axle
TRACK_F = 0.695     # the centre line to a front wheel's centre
TRACK_R = 0.685
W = 0.815           # half the body's width, at the waist crease
Z_NOSE = 1.965      # the front of the body, before the bumper
Z_TAIL = -2.215     # the back of the bed
Z_COWL = 0.875      # the windscreen's foot, and the doors' front edge
Z_DOOR = -0.105     # the doors' back edge
Z_CAB = -0.345      # the back of the cab
Z_HEADER = 0.46     # the windscreen's top
H_BELT = 0.955      # the window sills, and the bed's rails
H_EAVE = 1.355      # where the roof meets the leaning sides
H_ROOF = 1.455
H_HEADER = 1.37
H_CAB_FLOOR = 0.36
H_BED = 0.62
H_HARDTOP = 1.905
X_IN = 0.68         # the bed's inner walls, and the door cards
X_HOUSE = 0.50      # the inner faces of the rear wheel housings
RC = 0.10           # the cab's back corners, in plan
SEAT = 0.36         # a front seat's distance from the centre line

FRONT_ARCH = (AXLE, 0.31, 0.35)     # centre, height of centre, radius
REAR_ARCH = (-AXLE, 0.355, 0.37)

STEERING_HUB_H = 0.97
STEERING_HUB_Z = 0.32
COLUMN = V((0, -0.42, 0.91)).normalized()
STEERING_RADIUS = 0.19


def P(x, h, z):
    """The model-space point `h` above the ground."""
    return V((x, h - R, z))


# Materials: a display-referred colour and the rest of a Principled BSDF.
# `unlit` marks surfaces the game draws at full brightness, like lamps.
MATERIALS = {
    "paint": dict(colour=(0.36, 0.62, 0.57), metallic=0.5,
                  roughness=0.30, coat=1.0),
    "hardtop": dict(colour=(0.91, 0.91, 0.88), roughness=0.32, coat=0.6),
    "black": dict(colour=(0.075, 0.075, 0.08), roughness=0.55),
    "rubber": dict(colour=(0.045, 0.045, 0.05), roughness=0.85),
    "seam": dict(colour=(0.06, 0.08, 0.075), roughness=0.9),
    "tyre": dict(colour=(0.075, 0.07, 0.07), roughness=0.92),
    "under": dict(colour=(0.10, 0.10, 0.10), roughness=0.9),
    "glass": dict(colour=(0.13, 0.17, 0.19), roughness=0.02,
                  transmission=1.0, ior=1.52, tint=(0.80, 0.90, 0.88)),
    "chrome": dict(colour=(0.82, 0.83, 0.85), metallic=1.0,
                   roughness=0.12),
    "mirror": dict(colour=(0.78, 0.80, 0.82), metallic=1.0,
                   roughness=0.02),
    "steel": dict(colour=(0.72, 0.73, 0.74), metallic=0.7,
                  roughness=0.35),
    "hub": dict(colour=(0.24, 0.24, 0.25), metallic=0.5, roughness=0.5),
    "drum": dict(colour=(0.20, 0.18, 0.16), metallic=0.6, roughness=0.75),
    "exhaust": dict(colour=(0.33, 0.29, 0.25), metallic=0.6,
                    roughness=0.7),
    "lens": dict(colour=(0.96, 0.95, 0.88), roughness=0.05,
                 transmission=0.6, emission=0.6, unlit=True),
    "amber": dict(colour=(1.0, 0.55, 0.10), roughness=0.1,
                  transmission=0.5, emission=0.6, unlit=True),
    "tail_light": dict(colour=(0.85, 0.10, 0.08), roughness=0.1,
                       transmission=0.5, emission=0.6, unlit=True),
    "trim": dict(colour=(0.13, 0.12, 0.115), roughness=0.7),
    "dash": dict(colour=(0.09, 0.085, 0.085), roughness=0.6),
    "seat": dict(colour=(0.16, 0.145, 0.135), roughness=0.95, sheen=0.5),
    "seat_insert": dict(colour=(0.30, 0.27, 0.24), roughness=0.95,
                        sheen=0.6),
    "headliner": dict(colour=(0.72, 0.70, 0.64), roughness=0.9,
                      sheen=0.3),
    "mat": dict(colour=(0.06, 0.06, 0.06), roughness=0.9),
    "gauge": dict(colour=(0.03, 0.03, 0.035), roughness=0.1),
    "plate": dict(colour=(0.93, 0.93, 0.90), roughness=0.4),
    "ink": dict(colour=(0.04, 0.04, 0.04), roughness=0.5),
}
M = {}


def make_material(name, colour, metallic=0.0, roughness=0.8, coat=0.0,
                  transmission=0.0, ior=1.5, emission=0.0, sheen=0.0,
                  unlit=False, tint=None):
    """`colour` is what the game draws (the viewport colour); `tint`,
    when given, is the BSDF's own, as for glass, which the game shows
    dark but Blender renders clear."""
    mat = bpy.data.materials.new(name)
    lin = tuple(to_linear(c) for c in colour) + (1.0,)
    bsdf_colour = tuple(to_linear(c) for c in (tint or colour)) + (1.0,)
    mat.diffuse_color = lin
    mat.metallic = metallic
    mat.roughness = roughness
    if mat.node_tree is None:
        mat.use_nodes = True
    bsdf = next(n for n in mat.node_tree.nodes
                if n.type == "BSDF_PRINCIPLED")
    inputs = bsdf.inputs
    inputs["Base Color"].default_value = bsdf_colour
    inputs["Metallic"].default_value = metallic
    inputs["Roughness"].default_value = roughness
    inputs["Coat Weight"].default_value = coat
    inputs["Coat Roughness"].default_value = 0.04
    inputs["Transmission Weight"].default_value = transmission
    inputs["IOR"].default_value = ior
    inputs["Sheen Weight"].default_value = sheen
    if emission > 0:
        inputs["Emission Color"].default_value = lin
        inputs["Emission Strength"].default_value = emission
    if transmission >= 0.99:
        mat.use_raytrace_refraction = True
        mat.surface_render_method = "DITHERED"
    mat["moppe_unlit"] = unlit
    return mat


def clamp(x, lo=0.0, hi=1.0):
    return max(lo, min(hi, x))


def smoothstep(e0, e1, x):
    t = clamp((x - e0) / (e1 - e0))
    return t * t * (3 - 2 * t)


def newell(points):
    n = V((0, 0, 0))
    for i, p in enumerate(points):
        n += p.cross(points[(i + 1) % len(points)])
    return n


class Shell:
    """A mesh built point by point, each face with its own material."""

    def __init__(self):
        self.verts = []
        self.faces = []
        self.mats = []
        self.smooth = []

    def mark(self):
        return len(self.faces)

    def add(self, points):
        base = len(self.verts)
        self.verts += points
        return base

    def face(self, idx, mat, smooth=True):
        out = []
        for i in idx:
            if out and (self.verts[out[-1]] - self.verts[i]).length < 1e-7:
                continue
            out.append(i)
        while len(out) > 2 and \
                (self.verts[out[0]] - self.verts[out[-1]]).length < 1e-7:
            out.pop()
        if len(out) < 3:
            return
        if newell([self.verts[i] for i in out]).length < 1e-10:
            return
        self.faces.append(out)
        self.mats.append(mat)
        self.smooth.append(smooth)

    def grid(self, rows, mat, wrap=False, smooth=True):
        """Quads between consecutive rows of points; `mat` may be a
        function of the row and column that names a material, or None to
        leave a hole."""
        n = len(rows[0])
        base = self.add([p for r in rows for p in r])
        for i in range(len(rows) - 1):
            for j in range(n if wrap else n - 1):
                k = (j + 1) % n
                m = mat(i, j) if callable(mat) else mat
                if m:
                    self.face([base + i * n + j, base + i * n + k,
                               base + (i + 1) * n + k, base + (i + 1) * n + j],
                              m, smooth)

    def polygon(self, points, mat, smooth=False):
        base = self.add(list(points))
        self.face(list(range(base, base + len(points))), mat, smooth)

    def mesh(self, verts, faces, mat, smooth=False):
        base = self.add(list(verts))
        for f in faces:
            self.face([base + i for i in f], mat, smooth)

    def orient(self, start, toward):
        """Turns each face since `start` to face `toward(centre)`."""
        for k in range(start, len(self.faces)):
            pts = [self.verts[i] for i in self.faces[k]]
            centre = sum(pts, V((0, 0, 0))) / len(pts)
            if newell(pts).dot(toward(centre)) < 0:
                self.faces[k] = list(reversed(self.faces[k]))

    def orient_closed(self, start):
        """Turns the closed surface made since `start` outward."""
        volume = 0.0
        for f in self.faces[start:]:
            a = self.verts[f[0]]
            for i in range(1, len(f) - 1):
                volume += a.dot(self.verts[f[i]].cross(self.verts[f[i + 1]]))
        if volume < 0:
            for k in range(start, len(self.faces)):
                self.faces[k] = list(reversed(self.faces[k]))

    def build(self, asm, name, origin=V((0, 0, 0)), extra=()):
        names = []
        for m in list(self.mats) + list(extra):
            if m not in names:
                names.append(m)
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata([tuple(blender(v - origin)) for v in self.verts],
                         [], self.faces)
        for m in names:
            mesh.materials.append(M[m])
        for poly, m, s in zip(mesh.polygons, self.mats, self.smooth):
            poly.material_index = names.index(m)
            poly.use_smooth = s
        mesh.validate()
        mesh.update()
        obj = bpy.data.objects.new(name, mesh)
        asm.users_collection[0].objects.link(obj)
        obj.parent = asm
        return obj


# Profiles of the body, as functions of the distance z along it.

def half_width(z):
    w = W
    rn = 0.10
    if z > Z_NOSE - rn:
        d = min(rn, z - (Z_NOSE - rn))
        w -= rn - math.sqrt(rn * rn - d * d)
    rt = 0.05
    if z < Z_TAIL + rt:
        d = min(rt, (Z_TAIL + rt) - z)
        w -= rt - math.sqrt(rt * rt - d * d)
    return w


def crease(z):
    """The waist crease, falling a little toward the nose."""
    return 0.785 - 0.012 * z


def belt(z):
    """The top of the sides: the sills and rails, and forward of the
    windscreen the crest of the wings, falling to the nose."""
    if z <= Z_COWL:
        return H_BELT
    t = (z - Z_COWL) / (Z_NOSE - Z_COWL)
    return H_BELT - 0.155 * (0.8 * t + 0.2 * t * t)


def arch_top(z, arch):
    zc, hc, r = arch
    d = abs(z - zc)
    return hc + math.sqrt(r * r - d * d) if d < r else None


def sill(z):
    if z > AXLE:
        return 0.31 + 0.025 * smoothstep(1.72, 1.94, z)
    if z < -AXLE:
        return 0.40
    return 0.30


def bottom(z):
    """The lower edge of the sides: the sills, and the arches over the
    wheels."""
    h = sill(z)
    for arch in (FRONT_ARCH, REAR_ARCH):
        top = arch_top(z, arch)
        if top is not None:
            h = max(h, top)
    return h


def ramp(z, zc, r1, r2):
    """1 within r1 of zc, falling smoothly to 0 at r2."""
    return 1.0 - smoothstep(r1, r2, abs(z - zc))


def front_well(z):
    return 0.40 * ramp(z, AXLE, 0.36, 0.40)


def rear_house(z):
    return 0.24 * ramp(z, -AXLE, 0.37, 0.41)


def underside(z):
    """The tub's bottom: the cab's floor pan, then the bed's, which drops
    to the rear valance behind the axle."""
    bed = 0.56 - 0.16 * smoothstep(-1.90, -1.98, z)
    return 0.30 + (bed - 0.30) * smoothstep(Z_CAB - 0.005, Z_CAB - 0.02, z)


def floor(z):
    return H_CAB_FLOOR + (H_BED - H_CAB_FLOOR) * \
        smoothstep(Z_CAB - 0.005, Z_CAB - 0.02, z)


def outer(z):
    """The outer skin, bottom to top: the rolled lip under the sill (or
    round the arch), the panel swelling slightly to the crease, and above
    it the shoulder leaning in and rounding over."""
    w, hb, hc, ht = half_width(z), bottom(z), crease(z), belt(z)
    lower = [(w - 0.075, hb), (w - 0.058, hb + 0.004),
             (w - 0.046, hb + 0.013), (w - 0.039, hb + 0.028)]
    h0 = hb + 0.028
    for s in (0.2, 0.4, 0.6, 0.8, 1.0):
        lower.append((w - 0.039 * (1 - s) ** 1.4, h0 + (hc - h0) * s))
    h1 = ht - 0.022
    upper = [(w, hc)]
    for s in (0.25, 0.5, 0.75, 1.0):
        upper.append((w - 0.006 * s - 0.026 * s ** 1.5,
                      hc + (h1 - hc) * s))
    upper += [(w - 0.042, ht - 0.009), (w - 0.052, ht - 0.002),
              (w - 0.064, ht)]
    return lower, upper


OUTER_TAGS = ["lower"] * 8 + ["upper"] * 7


def nose_half(z):
    lower, upper = outer(z)
    w, ht = half_width(z), belt(z)
    well = 0.30 + front_well(z)
    under = [(0.0, 0.30), (0.47, 0.30), (0.47, well)]
    bonnet = [((w - 0.064) * k, ht + 0.022 * (1 - k * k))
              for k in (0.8, 0.6, 0.4, 0.2, 0.0)]
    return under + lower + upper[1:] + bonnet


NOSE_TAGS = ["under", "well", "well"] + OUTER_TAGS + ["bonnet"] * 5


def tub_half(z):
    lower, upper = outer(z)
    ht, hu, hf, hs = belt(z), underside(z), floor(z), rear_house(z)
    under = [(0.0, hu), (X_HOUSE + 0.03, hu), (X_HOUSE + 0.03, hu + hs)]
    rail = [(X_IN + 0.012, ht), (X_IN + 0.0035, ht - 0.0035),
            (X_IN, ht - 0.012)]
    inside = [(X_IN, hf + hs), (X_HOUSE, hf + hs), (X_HOUSE, hf), (0.0, hf)]
    return under + lower + upper[1:] + rail + inside


TUB_TAGS = (["under", "well", "well"] + OUTER_TAGS + ["rail"] * 3 +
            ["inner", "house_top", "house_wall", "floor"])


def mirrored(half, tags):
    ring = list(half) + [(-x, h) for x, h in reversed(half[1:-1])]
    return ring, list(tags) + list(reversed(tags))


def inset(ring, d, weights=None):
    """The closed ring moved inward by d (each point by d times its
    weight)."""
    n = len(ring)
    area = sum(ring[i][0] * ring[(i + 1) % n][1] -
               ring[(i + 1) % n][0] * ring[i][1] for i in range(n))
    sign = 1.0 if area > 0 else -1.0
    vec = [Vector(p) for p in ring]
    out = []
    for i in range(n):
        wt = 1.0 if weights is None else weights[i]
        p = vec[i]
        if wt == 0 or d == 0:
            out.append(tuple(p))
            continue
        j = (i - 1) % n
        while j != i and (vec[j] - p).length < 1e-7:
            j = (j - 1) % n
        k = (i + 1) % n
        while k != i and (vec[k] - p).length < 1e-7:
            k = (k + 1) % n
        t1 = (p - vec[j]).normalized()
        t2 = (vec[k] - p).normalized()
        n1 = Vector((-t1.y, t1.x))
        n2 = Vector((-t2.y, t2.x))
        m = n1 + n2
        m = m.normalized() if m.length > 1e-6 else n1
        c = max(0.35, m.dot(n1))
        q = p + m * (sign * d * wt / c)
        out.append((q.x, q.y))
    return out


def body_loft(shell, zs, ring_at, tags, paint, caps=("under", "under"),
         warp=None):
    """Lofts the rings `ring_at(z)` -- closed lists of (x, h) -- through
    `zs`, each run of segments sharing a tag a band of its own (so the
    edges between runs stay sharp), coloured `paint(tag, z)`, and closes
    the ends."""
    start = shell.mark()
    rings = [ring_at(z) for z in zs]
    n = len(tags)

    def point(x, h, z):
        p = P(x, h, z)
        return warp(p) if warp else p

    s0 = next((i for i in range(n) if tags[i] != tags[i - 1]), 0)
    runs, cur = [], [s0]
    for k in range(1, n):
        i = (s0 + k) % n
        if tags[i] == tags[cur[-1]]:
            cur.append(i)
        else:
            runs.append(cur)
            cur = [i]
    runs.append(cur)
    for run in runs:
        cols = run + [(run[-1] + 1) % n]
        rows = [[point(*ring[c], z) for c in cols]
                for ring, z in zip(rings, zs)]
        tag = tags[run[0]]
        shell.grid(rows, lambda i, j, tag=tag:
                   paint(tag, (zs[i] + zs[i + 1]) / 2))
    # The first cap runs round against the bands' first edges, the last
    # with them, so that the whole stays wound one way.
    for ring, z, mat in ((list(reversed(rings[0])), zs[0], caps[0]),
                         (rings[-1], zs[-1], caps[1])):
        if mat:
            shell.polygon([point(x, h, z) for x, h in ring], mat)
    shell.orient_closed(start)


def samples(z0, z1, step, extra=()):
    n = max(1, math.ceil((z1 - z0) / step))
    zs = [z0 + (z1 - z0) * i / n for i in range(n + 1)]
    zs += [z for z in extra if z0 < z < z1]
    zs.sort()
    out = [zs[0]]
    for z in zs[1:]:
        if z - out[-1] > 4e-5:
            out.append(z)
    return out


def arch_samples(arch, r1, r2):
    zc, _, r = arch
    out = [zc + r * math.cos(math.pi * i / 48) for i in range(49)]
    for s in (-1, 1):
        out += [zc + s * (r - 2e-4), zc + s * (r + 2e-4)]
        out += [zc + s * (r1 + (r2 - r1) * t / 4) for t in range(5)]
    return out


def end_samples(z_end, r, sign, n=10):
    """Samples rounding an end over r, finest at the end itself."""
    return [z_end - sign * (r - r * math.sin(math.pi / 2 * i / n))
            for i in range(n + 1)]


# The greenhouse.

def xs(h):
    """The leaning sides of the greenhouse, which hold the windows."""
    return 0.735 - 0.27 * (h - H_BELT)


SIDE_NORMAL = V((1, 0.27, 0)).normalized()
WS_SLOPE = (Z_COWL - Z_HEADER) / (H_HEADER - H_BELT)
WS_NORMAL = V((0, WS_SLOPE, 1)).normalized()


def zws(h):
    """The windscreen's plane."""
    return Z_COWL - (h - H_BELT) * WS_SLOPE


def zb(h):
    """The cab's back, leaning forward a little."""
    return Z_CAB + 0.05 * (h - H_BELT)


def skin_x(z, h):
    """The outer skin's x at height h, and its slope dx/dh."""
    lower, upper = outer(z)
    prof = lower + upper[1:]
    for (x0, h0), (x1, h1) in zip(prof, prof[1:]):
        if h0 <= h <= h1 and h1 > h0:
            t = (h - h0) / (h1 - h0)
            return x0 + (x1 - x0) * t, (x1 - x0) / (h1 - h0)
    return prof[-1][0], 0.0


def skin_point(side, z, h, lift=0.0025):
    x, slope = skin_x(z, h)
    n = V((1, -slope, 0)).normalized()
    return P(side * (x + n.x * lift), h + n.y * lift, z)


def skin_strip(shell, side, path, width, mat, lift=0.0025):
    """A strip `width` wide along `path`, (z, h) pairs, laid on the
    outer skin: a panel gap, a pinstripe."""
    rows = []
    for i, (z, h) in enumerate(path):
        a = path[max(0, i - 1)]
        b = path[min(len(path) - 1, i + 1)]
        dz, dh = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dz, dh)
        nz, nh = -dh / length * width / 2, dz / length * width / 2
        rows.append([skin_point(side, z + nz, h + nh, lift),
                     skin_point(side, z - nz, h - nh, lift)])
    start = shell.mark()
    shell.grid(rows, mat)
    shell.orient(start, lambda c: V((side, 0, 0)))


def rounded_rect(u0, u1, v0, v1, r, n=5):
    """A rounded rectangle's outline, counter-clockwise in (u, v)."""
    pts = []
    for cu, cv, a0 in ((u1 - r, v0 + r, -90), (u1 - r, v1 - r, 0),
                       (u0 + r, v1 - r, 90), (u0 + r, v0 + r, 180)):
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            pts.append((cu + r * math.cos(a), cv + r * math.sin(a)))
    return pts


def loop_tube(points, radius, sides=6):
    """A tube round the closed loop `points`."""
    pts = list(points) + [points[0], points[1]]
    return tube(pts, radius, sides=sides, close=(False, False))


def sweep(shell, path, profile, mat, up=UP, caps=True, smooth=True):
    """Sweeps the closed `profile` of (out, up) pairs along `path`: out is
    square to the path and to `up`, on the right of the way it goes."""
    start = shell.mark()
    rows = []
    for i, p in enumerate(path):
        a = path[max(0, i - 1)]
        b = path[min(len(path) - 1, i + 1)]
        t = (b - a).normalized()
        out = t.cross(up).normalized()
        u = out.cross(t).normalized()
        rows.append([p + out * o + u * v for o, v in profile])
    shell.grid(rows, mat, wrap=True, smooth=smooth)
    if caps:
        shell.polygon(rows[0], mat)
        shell.polygon(rows[-1], mat)
    shell.orient_closed(start)


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


def text(body, size, depth, condense=1.0):
    """The vertices (text frame: x along the line, y up, z out of the
    face, centred) and faces of `body` set in Blender's own face."""
    curve = bpy.data.curves.new("text", type="FONT")
    curve.body = body
    curve.size = size
    curve.extrude = depth / 2
    curve.align_x = "CENTER"
    curve.align_y = "CENTER"
    obj = bpy.data.objects.new("text", curve)
    bpy.context.scene.collection.objects.link(obj)
    graph = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(graph))
    verts = [V((v.co.x * condense, v.co.y, v.co.z)) for v in mesh.vertices]
    faces = [list(p.vertices) for p in mesh.polygons]
    bpy.data.objects.remove(obj)
    bpy.data.curves.remove(curve)
    bpy.data.meshes.remove(mesh)
    return verts, faces


def lettering(shell, body, size, depth, origin, along, up, mat,
              condense=1.0):
    """Sets `body` at `origin`, reading along `along`, its face toward
    along x up."""
    verts, faces = text(body, size, depth, condense)
    out = along.cross(up)
    shell.mesh([origin + along * v.x + up * v.y + out * (v.z + depth / 2)
                for v in verts], faces, mat)


# Building.

def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    coll = bpy.data.collections.new("caddy")
    scene.collection.children.link(coll)
    for name, spec in MATERIALS.items():
        M[name] = make_material(name, **spec)

    body = assembly("body", coll, V((0, 0, 0)), mesh="body")
    shell = Shell()
    lofted_body(shell)
    greenhouse(shell, body)
    panel_lines(shell)
    lamps_and_grille(shell, body)
    bumpers_and_plates(shell)
    fittings(shell, body)
    underbody(shell, body)
    dashboard(shell, body)
    shell.build(body, "body_shell")

    glass(coll)
    hardtop(coll)
    tailgate(coll)
    wheels(coll)
    seats(coll)
    steering(coll)

    assembly("seat_driver", coll, P(SEAT, 0.57, -0.08), point="seat_driver")
    assembly("seat_passenger", coll, P(-SEAT, 0.57, -0.08),
             point="seat_passenger")
    for s, tag in ((1, "left"), (-1, "right")):
        assembly(f"pedal_{tag}", coll, P(SEAT + 0.095 * s, 0.41, 0.64),
                 point=f"pedal_{tag}")
        assembly(f"footrest_{tag}", coll, P(-SEAT + 0.095 * s, 0.41, 0.66),
                 point=f"footrest_{tag}")
        assembly(f"headlight_{tag}", coll, P(0.555 * s, 0.70, Z_NOSE + 0.03),
                 point=f"headlight_{tag}")
        assembly(f"tail_light_{tag}", coll, P(0.75 * s, 0.72, Z_TAIL - 0.01),
                 point=f"tail_light_{tag}")
        assembly(f"door_{tag}", coll, P(1.15 * s, 0.0, 0.30),
                 point=f"door_{tag}")
    assembly("hitch", coll, P(0, 0.38, -2.43), point="hitch")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
    print(f"wrote {OUT}: {len(coll.objects)} objects")


def lofted_body(shell):
    def paint_nose(tag, z):
        return "under" if tag in ("under", "well") else "paint"

    nose_zs = samples(Z_COWL, Z_NOSE, 0.02,
                      arch_samples(FRONT_ARCH, 0.36, 0.40) +
                      end_samples(Z_NOSE, 0.10, 1, 14) +
                      end_samples(Z_NOSE, 0.025, 1, 8))

    def nose_ring(z):
        ring, _ = mirrored(nose_half(z), NOSE_TAGS)
        re = 0.025
        if z > Z_NOSE - re:
            d = min(re, z - (Z_NOSE - re))
            ring = inset(ring, re - math.sqrt(re * re - d * d))
        return ring

    _, nose_tags = mirrored(nose_half(Z_COWL), NOSE_TAGS)
    body_loft(shell, nose_zs, nose_ring, nose_tags, paint_nose,
         caps=("dash", "paint"))

    def paint_tub(tag, z):
        if tag in ("under", "well"):
            return "under"
        cab = z > Z_CAB - 0.01
        if tag == "inner" and cab:
            return "trim"
        if tag == "floor" and cab:
            return "mat"
        return "paint"

    tub_zs = samples(Z_TAIL, Z_COWL, 0.025,
                     arch_samples(REAR_ARCH, 0.37, 0.41) +
                     end_samples(Z_TAIL, 0.05, -1, 8) +
                     [Z_DOOR, Z_CAB, Z_CAB - 0.005, Z_CAB - 0.01,
                      Z_CAB - 0.015, Z_CAB - 0.02, -1.90, -1.92, -1.94,
                      -1.96, -1.98])
    _, tub_tags = mirrored(tub_half(0), TUB_TAGS)
    body_loft(shell, tub_zs, lambda z: mirrored(tub_half(z), TUB_TAGS)[0],
         tub_tags, paint_tub, caps=("paint", None))

    # The cab's back below the windows, painted toward the bed and
    # trimmed toward the seats.
    for z0, z1, mat in ((Z_CAB, Z_CAB + 0.01, "paint"),
                        (Z_CAB + 0.01, Z_CAB + 0.02, "trim")):
        box(shell, P(-X_IN, H_CAB_FLOOR, z0), P(X_IN, H_BELT, z1), mat)


def box(shell, lo, hi, mat):
    """An axis-aligned box between the corners lo and hi."""
    start = shell.mark()
    c = [V((x, y, z)) for z in (lo.z, hi.z) for y in (lo.y, hi.y)
         for x in (lo.x, hi.x)]
    for f in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6),
              (0, 2, 6, 4), (1, 5, 7, 3)):
        shell.polygon([c[i] for i in f], mat)
    shell.orient_closed(start)


def greenhouse(shell, body):
    # The roof, lofted from the cab's back to past the windscreen and cut
    # off by its plane; its back corners round in plan as the cab's do.
    xe = xs(H_EAVE)
    z_back = zb(H_EAVE)

    def roof_half(z):
        pts = [(0.0, H_EAVE), (xe, H_EAVE)]
        for h in (1.375, 1.395):
            pts.append((xs(h), h))
        xr = xs(1.405)
        pts += [(xr - 0.004, 1.414), (xr - 0.013, 1.426),
                (xr - 0.026, 1.434)]
        xt = xr - 0.034
        pts += [(xt * k, H_ROOF - 0.018 * k * k)
                for k in (1.0, 0.8, 0.6, 0.4, 0.2, 0.0)]
        return pts

    roof_tags = ["headliner"] + ["roof"] * 11

    def roof_ring(z):
        half = roof_half(z)
        if z < z_back + RC:
            d = min(RC, z_back + RC - z)
            shrink = RC - math.sqrt(RC * RC - d * d)
            half = [(x * (xe - shrink) / xe, h) for x, h in half]
        ring, _ = mirrored(half, roof_tags)
        re = 0.045
        if z < z_back + re:
            d = min(re, z_back + re - z)
            weights = [clamp((h - 1.39) / 0.02) for _, h in ring]
            ring = inset(ring, re - math.sqrt(re * re - d * d), weights)
        return ring

    def clip(p):
        z = zws(p.y + R)
        return V((p.x, p.y, min(p.z, z)))

    _, tags = mirrored(roof_half(0), roof_tags)
    roof_zs = samples(z_back, Z_HEADER + 0.04, 0.02,
                      end_samples(z_back, RC, -1, 12) +
                      end_samples(z_back, 0.045, -1, 8))
    body_loft(shell, roof_zs, roof_ring, tags,
         lambda tag, z: "headliner" if tag == "headliner" else "paint",
         caps=("paint", "paint"), warp=clip)

    # The C-pillars and the cab's back with its window: horizontal
    # slices round the back of the cab, made a shell by a Solidify.
    rows_h = [H_BELT - 0.012, H_BELT, 0.985, 1.035, 1.105, 1.175, 1.245,
              1.315, H_EAVE]

    def wrap_path(h):
        x, zback = xs(h), zb(h)
        left = [(x, z) for z in (Z_DOOR, -0.15, -0.19)]
        arc = [(x - RC + RC * math.cos(math.radians(a)),
                zback + RC - RC * math.sin(math.radians(a)))
               for a in (0, 15, 30, 45, 60, 75, 90)]
        back = [(xx, zback) for xx in (0.47, 0.32, 0.16)]
        half = left + arc + back
        mirror = [(-px, pz) for px, pz in reversed(half)]
        return half + [(0.0, zback)] + mirror

    rows = [[P(px, h, pz) for px, pz in wrap_path(h)] for h in rows_h]
    wrap = Shell()

    def window(i, j):
        a, b = rows[i][j], rows[i + 1][j + 1]
        cx, ch = (a.x + b.x) / 2, (a.y + b.y) / 2 + R
        cz = (a.z + b.z) / 2
        if abs(cx) < 0.47 and 1.035 < ch < 1.315 and cz < zb(ch) + 0.01:
            return None
        return "paint"

    wrap.grid(rows, window)
    wrap.orient(0, lambda c: c - V((0, c.y, -0.05)))
    obj = wrap.build(body, "cab_back", extra=("trim",))
    mod = obj.modifiers.new("shell", "SOLIDIFY")
    mod.thickness = 0.02
    mod.offset = -1.0
    mod.use_even_offset = True
    mod.material_offset = 1

    # The A-pillars, along the edge between the windscreen and the sides.
    for s in (1, -1):
        side_n = V((s * SIDE_NORMAL.x, SIDE_NORMAL.y, 0))
        bis = (side_n + WS_NORMAL).normalized()
        across = (side_n - WS_NORMAL).normalized()
        path = [P(s * xs(h), h, zws(h)) + bis * 0.010
                for h in (0.93, 0.98, 1.10, 1.22, 1.33, 1.39)]
        part(body, f"a_pillar{s}", loft_sections(path, 0.034, 0.016,
                                                  across), M["paint"])
        # The drip rail along the roof's edge.
        part(body, f"drip_rail{s}", tube(
            [P(s * (xs(1.388) + 0.004), 1.388, z)
             for z in (zws(1.388) - 0.005, 0.2, -0.05, -0.20)] +
            [P(s * (xs(1.388) + 0.004 - (RC - math.sqrt(RC * RC - d * d))),
               1.388, zb(1.388) + RC - d) for d in (0.03, 0.06, 0.085)],
            0.0045, sides=6), M["paint"], levels=0)
        # Black rubber along the sill under the window.
        part(body, f"sill_seal{s}", tube(
            [P(s * (half_width(z) - 0.069), H_BELT + 0.004, z)
             for z in (Z_DOOR + 0.005, 0.3, 0.6, Z_COWL - 0.01)],
            0.007, sides=6), M["rubber"], levels=0)


def loft_sections(path, width, depth, across):
    return loft([(p, width, depth) for p in path], across, sides=8)


def panel_lines(shell):
    """Panel gaps drawn on the skin, and the fuel filler."""
    for s in (1, -1):
        skin_strip(shell, s, [(Z_COWL + 0.003, h) for h in
                              (0.34, 0.45, 0.6, 0.75, 0.85, 0.94)],
                   0.006, "seam")
        skin_strip(shell, s, [(Z_DOOR, h) for h in
                              (0.34, 0.45, 0.6, 0.75, 0.85, 0.948)],
                   0.006, "seam")
        skin_strip(shell, s, [(z, 0.338) for z in
                              (Z_DOOR - 0.003, 0.2, 0.5, Z_COWL + 0.006)],
                   0.006, "seam")
        # The bonnet's shut line along the wing's crest.
        start = shell.mark()
        zs = samples(Z_COWL + 0.01, Z_NOSE - 0.03, 0.04)
        rows = []
        for z in zs:
            w, ht = half_width(z), belt(z)
            x = w - 0.071
            k = x / (w - 0.064)
            h = ht + 0.022 * (1 - k * k) + 0.002
            rows.append([P(s * (x - 0.003), h, z), P(s * (x + 0.003), h, z)])
        shell.grid(rows, "seam")
        shell.orient(start, lambda c: UP)
        # The black pinstripe under the waist crease, nose to tail.
        skin_strip(shell, s, [(z, crease(z) - 0.011) for z in
                              samples(Z_TAIL + 0.03, Z_NOSE - 0.07, 0.1)],
                   0.0065, "black", 0.002)
        # The stone guard on the leading edge of the rear arch: a black
        # strip round the lip, widest at the bottom.
        zc, hc, r = REAR_ARCH
        rows = []
        for i in range(17):
            a = math.radians(4 + 78 * i / 16)
            width = 0.060 - 0.040 * i / 16
            z = zc + (r + 0.016) * math.cos(a)
            h = hc + (r + 0.016) * math.sin(a)
            z2 = zc + (r + 0.016 + width) * math.cos(a)
            h2 = hc + (r + 0.016 + width) * math.sin(a)
            rows.append([skin_point(s, z, max(h, 0.33), 0.005),
                         skin_point(s, z2, max(h2, 0.33), 0.005)])
        start = shell.mark()
        shell.grid(rows, "black")
        shell.orient(start, lambda c, s=s: V((s, 0, 0)))
    # The fuel filler, on the right behind the door.
    loop = rounded_rect(-0.955, -0.805, 0.635, 0.775, 0.012, 3)
    skin_strip(shell, -1, loop + [loop[0]], 0.005, "seam")


def lamps_and_grille(shell, body):
    zf = Z_NOSE
    # The grille: a black panel across the nose, slats between the lamps.
    box(shell, P(-0.705, 0.605, zf - 0.02), P(0.705, 0.79, zf + 0.006),
        "black")
    for i in range(10):
        h = 0.627 + i * 0.0165
        box(shell, P(-0.445, h - 0.0042, zf), P(0.445, h + 0.0042,
                                                zf + 0.014), "trim")
    for s in (1, -1):
        for i in range(3):
            x = s * (0.668 + i * 0.013)
            box(shell, P(x - 0.0035, 0.63, zf), P(x + 0.0035, 0.77,
                                                  zf + 0.012), "trim")
    # The badge.
    centre = P(0, 0.70, zf + 0.018)
    part(body, "badge_ring", torus(centre, FORWARD, UP, 0.050,
                                   (0.006, 0.005), 32, 6), M["chrome"])
    part(body, "badge_disc", loft([(centre - FORWARD * 0.006, 0.047, 0.047),
                                   (centre, 0.047, 0.047)], UP, sides=24),
         M["black"], levels=0)
    strokes = [[(-0.50, 0.85), (0.0, -0.18), (0.50, 0.85)],
               [(-0.90, 0.42), (-0.46, -0.84), (-0.12, 0.02)],
               [(0.12, 0.02), (0.46, -0.84), (0.90, 0.42)]]
    for k, stroke in enumerate(strokes):
        pts = [centre + V((-u * 0.048, v * 0.048, 0.002)) for u, v in stroke]
        part(body, f"badge_stroke{k}", tube(pts, 0.0045, sides=6),
             M["chrome"], levels=0)
    lettering(shell, "Caddy", 0.026, 0.002, P(-0.33, 0.622, zf + 0.0145),
              RIGHT, UP, "chrome")

    # Round seven-inch lamps in black rubber surrounds.
    for s in (1, -1):
        c = P(0.555 * s, 0.70, zf + 0.012)
        part(body, f"lamp_surround{s}", torus(c, FORWARD, UP, 0.098,
                                              (0.013, 0.010), 32, 6),
             M["rubber"])
        part(body, f"lamp_ring{s}", torus(c + FORWARD * 0.006, FORWARD, UP,
                                          0.089, (0.004, 0.003), 32, 6),
             M["chrome"], levels=0)
        part(body, f"lamp_bowl{s}", loft([
            (c - FORWARD * 0.004, 0.088, 0.088),
            (c - FORWARD * 0.030, 0.060, 0.060),
            (c - FORWARD * 0.045, 0.020, 0.020)], UP, sides=24),
            M["chrome"], levels=0)
        part(body, f"lamp_lens{s}", loft([
            (c - FORWARD * 0.004, 0.089, 0.089),
            (c + FORWARD * 0.010, 0.087, 0.087),
            (c + FORWARD * 0.020, 0.070, 0.070),
            (c + FORWARD * 0.026, 0.040, 0.040),
            (c + FORWARD * 0.028, 0.004, 0.004)], UP, sides=24),
            M["lens"], levels=0)
        # Side repeaters on the wings.
        p = skin_point(s, 0.965, 0.732, 0.004)
        box(shell, p + V((-0.004, -0.009, -0.020)),
            p + V((0.004, 0.009, 0.020)), "amber")
        # Tail lamps on the back of the bed's sides: the indicator above
        # the tail and stop lamp, in a black frame.
        xo = s * 0.752
        box(shell, P(xo - 0.052, 0.578, Z_TAIL - 0.006),
            P(xo + 0.052, 0.862, Z_TAIL + 0.004), "black")
        box(shell, P(xo - 0.045, 0.770, Z_TAIL - 0.014),
            P(xo + 0.045, 0.853, Z_TAIL - 0.004), "amber")
        box(shell, P(xo - 0.045, 0.586, Z_TAIL - 0.014),
            P(xo + 0.045, 0.765, Z_TAIL - 0.004), "tail_light")


def bumpers_and_plates(shell):
    # The black plastic bumpers of the later cars, wrapping round to the
    # wheel arches in front.
    front = []
    for z in (1.70, 1.76, 1.825):
        front.append((0.78, z))
    for a in range(1, 9):
        t = math.radians(90 * a / 9)
        front.append((0.62 + 0.16 * math.cos(t), 1.825 + 0.16 * math.sin(t)))
    for x in (0.62, 0.3, 0.0, -0.3, -0.62):
        front.append((x, 1.985))
    path = [P(-x, 0.505, z) for x, z in front]
    path += [P(x, 0.505, z) for x, z in reversed(front[:-5])]
    profile = [(-0.05, -0.075), (0.03, -0.075), (0.045, -0.066),
               (0.05, -0.045), (0.05, 0.030), (0.046, 0.046),
               (0.036, 0.051), (0.040, 0.060), (0.036, 0.071),
               (0.020, 0.075), (-0.05, 0.075)]
    sweep(shell, path, profile, "black")

    rear = []
    for z in (-2.02, -2.07, -2.115):
        rear.append((0.80, z))
    for a in range(1, 9):
        t = math.radians(90 * a / 9)
        rear.append((0.66 + 0.14 * math.cos(t), -2.115 - 0.14 * math.sin(t)))
    for x in (0.66, 0.3, 0.0, -0.3, -0.66):
        rear.append((x, -2.255))
    path = [P(x, 0.445, z) for x, z in rear]
    path += [P(-x, 0.445, z) for x, z in reversed(rear[:-5])]
    profile = [(-0.04, -0.055), (0.025, -0.055), (0.038, -0.046),
               (0.04, -0.03), (0.04, 0.035), (0.032, 0.05),
               (0.015, 0.055), (-0.04, 0.055)]
    sweep(shell, path, profile, "black")

    # Indicators in the front bumper's corners.
    for s in (1, -1):
        box(shell, P(s * 0.66 - 0.055, 0.508, 2.030),
            P(s * 0.66 + 0.055, 0.548, 2.041), "amber")

    plate(shell, P(0, 0.500, 2.038), RIGHT, FORWARD)
    plate(shell, P(0, 0.445, -2.298), -RIGHT, -FORWARD)


def plate(shell, centre, along, out):
    """A German number plate of the eighties, 520 by 110 mm."""
    up = out.cross(along)
    w, h = 0.26, 0.055
    corners = [centre + along * u + up * v
               for u, v in ((-w, -h), (w, -h), (w, h), (-w, h))]
    start = shell.mark()
    back = [p - out * 0.004 for p in corners]
    shell.polygon(corners, "plate")
    shell.orient(start, lambda c: out)
    start = shell.mark()
    shell.grid([back, corners], "plate", wrap=True)
    shell.orient(start, lambda c: (c - centre) - out * (c - centre).dot(out))
    border = rounded_rect(-w + 0.006, w - 0.006, -h + 0.006, h - 0.006,
                          0.004, 2)
    inner = rounded_rect(-w + 0.010, w - 0.010, -h + 0.010, h - 0.010,
                         0.002, 2)
    start = shell.mark()
    shell.grid([[centre + along * u + up * v + out * 0.0008
                 for u, v in border],
                [centre + along * u + up * v + out * 0.0008
                 for u, v in inner]], "ink", wrap=True)
    shell.orient(start, lambda c: out)
    lettering(shell, "WOB-LV 96", 0.088, 0.0012,
              centre + out * 0.0005 - up * 0.001, along, up, "ink",
              condense=0.86)


def fittings(shell, body):
    for s in (1, -1):
        # Door handle: a black pull in the door's skin, and its lock.
        z0, z1, h = 0.035, 0.150, 0.872
        a, b = skin_point(s, z0, h, 0.004), skin_point(s, z1, h, 0.004)
        part(body, f"handle{s}", tube([a, (a + b) / 2 + V((s * 0.006, 0, 0)),
                                       b], 0.0085, sides=8), M["black"])
        lock = skin_point(s, 0.172, h, 0.002)
        part(body, f"lock{s}", loft([(lock, 0.009, 0.009),
                                     (lock + V((s * 0.005, 0, 0)), 0.008,
                                      0.008)], UP, sides=10), M["chrome"],
             levels=0)
        # Wing mirror at the window's front corner.
        foot = P(s * 0.745, 0.985, 0.80)
        part(body, f"mirror_foot{s}", slab(foot, (0.012, 0.030, 0.045)),
             M["black"], bevel=0.008)
        arm = [foot + V((s * 0.01, 0.01, 0)),
               P(s * 0.84, 1.03, 0.79)]
        part(body, f"mirror_arm{s}", tube(arm, 0.011, sides=8), M["black"])
        head = P(s * 0.885, 1.05, 0.775)
        part(body, f"mirror_head{s}", slab(head, (0.075, 0.050, 0.028)),
             M["black"], bevel=0.012)
        part(body, f"mirror_glass{s}", slab(head - FORWARD * 0.026,
                                            (0.064, 0.040, 0.004)),
             M["mirror"], levels=0, bevel=0.003)

    # The cowl's grille and the wipers parked along it.
    part(body, "cowl", prism([(H_BELT - R - 0.01, Z_COWL - 0.02),
                              (H_BELT - R + 0.012, Z_COWL - 0.012),
                              (H_BELT - R + 0.028, Z_COWL + 0.03),
                              (H_BELT - R + 0.018, Z_COWL + 0.06),
                              (H_BELT - R - 0.01, Z_COWL + 0.06)],
                             -0.70, 0.70), M["black"], levels=0)
    for x in (-0.42, 0.13):
        pivot = P(x, H_BELT + 0.03, Z_COWL + 0.035)
        part(body, f"wiper_pivot{x}", loft([(pivot, 0.012, 0.012),
                                            (pivot + UP * 0.012, 0.010,
                                             0.010)], FORWARD, sides=8),
             M["black"], levels=0)
        h0 = H_BELT + 0.04
        tip = P(x + 0.40, h0 + 0.015, zws(h0 + 0.015) + 0.012)
        part(body, f"wiper_arm{x}", tube([pivot + UP * 0.012,
                                          P(x + 0.06, h0 + 0.004,
                                            zws(h0) + 0.016), tip],
                                         0.006, sides=6), M["black"],
             levels=0)
        blade = [P(x + 0.04, h0 + 0.002, zws(h0 + 0.002) + 0.008),
                 P(x + 0.43, h0 + 0.018, zws(h0 + 0.018) + 0.008)]
        part(body, f"wiper_blade{x}", tube(blade, 0.007, sides=6),
             M["rubber"], levels=0)


def underbody(shell, body):
    # The engine and gearbox between the front wheels, and the shafts.
    box(shell, P(-0.36, 0.20, 1.02), P(0.30, 0.31, 1.72), "under")
    part(body, "sump", slab(P(0.05, 0.22, 1.50), (0.22, 0.04, 0.14)),
         M["black"], bevel=0.02)
    for s in (1, -1):
        part(body, f"driveshaft{s}", tube([P(0.05 * s, 0.288, 1.31),
                                           P(0.62 * s, 0.288, AXLE)],
                                          0.022, sides=8), M["drum"],
             levels=0)
        part(body, f"control_arm{s}", tube([P(0.30 * s, 0.24, 1.10),
                                            P(0.62 * s, 0.25, AXLE - 0.02)],
                                           0.018, sides=6), M["black"],
             levels=0)
    # The floor pan between the axles.
    box(shell, P(-0.52, 0.27, -0.95), P(0.52, 0.305, 1.00), "under")
    # The tank under the bed ahead of the rear axle.
    part(body, "tank", slab(P(0.08, 0.42, -0.70), (0.40, 0.11, 0.22)),
         M["black"], bevel=0.03)
    # The rear axle beam on leaf springs, with its dampers.
    part(body, "axle_beam", tube([P(-0.64, 0.288, -AXLE),
                                  P(0.64, 0.288, -AXLE)], 0.035, sides=8),
         M["under"], levels=0)
    for s in (1, -1):
        x = 0.56 * s
        for k, sag in enumerate((0.0, 0.015, 0.03)):
            path = []
            for i in range(9):
                t = i / 8
                z = -1.86 + (-0.80 + 1.86) * t
                h = 0.335 - 0.085 * math.sin(math.pi * t) - sag * \
                    math.sin(math.pi * t) ** 2
                path.append(P(x, h, z))
            if k:
                path = path[1:-1]
            part(body, f"leaf{s}_{k}", loft([(p, 0.028, 0.006)
                                              for p in path], RIGHT,
                                             sides=4), M["under"], levels=0)
        part(body, f"shackle{s}", slab(P(x, 0.37, -1.86),
                                       (0.02, 0.04, 0.012)), M["black"],
             levels=0)
        part(body, f"damper{s}", tube([P(x - 0.04 * s, 0.30, -AXLE + 0.08),
                                       P(x - 0.10 * s, 0.62, -AXLE + 0.20)],
                                      0.022, sides=8), M["black"], levels=0)
    # The exhaust, down the right side to its silencer and out behind.
    pipe = [P(-0.10, 0.22, 1.60), P(-0.20, 0.235, 1.0),
            P(-0.30, 0.25, 0.2), P(-0.30, 0.26, -0.40)]
    part(body, "exhaust_front", tube(pipe, 0.022, sides=8),
         M["exhaust"], levels=0)
    part(body, "silencer", loft([(P(-0.30, 0.30, -0.40), 0.10, 0.06),
                                 (P(-0.30, 0.30, -0.95), 0.10, 0.06)],
                                RIGHT, sides=10), M["exhaust"])
    part(body, "tail_pipe", tube([P(-0.30, 0.30, -0.95),
                                  P(-0.40, 0.32, -1.85),
                                  P(-0.45, 0.33, -2.24),
                                  P(-0.45, 0.335, -2.29)],
                                 0.021, sides=8), M["exhaust"], levels=0)
    # The spare wheel slung flat under the back of the bed.
    spare = P(0.12, 0.43, -1.96)
    part(body, "spare", torus(spare, UP, RIGHT, 0.21, (0.075, 0.06), 24, 8),
         M["tyre"])
    part(body, "spare_cradle", slab(spare - UP * 0.07, (0.20, 0.008, 0.20)),
         M["under"], levels=0)
    # The tow bar: a bar under the bumper, a neck, and the ball.
    part(body, "towbar_cross", tube([P(-0.45, 0.33, -2.16),
                                     P(0.45, 0.33, -2.16)], 0.025, sides=4),
         M["black"], levels=0)
    part(body, "towbar_neck", tube([P(0, 0.33, -2.16), P(0, 0.33, -2.33),
                                    P(0, 0.34, -2.40), P(0, 0.36, -2.43)],
                                   0.018, sides=8), M["black"])
    part(body, "tow_ball", blob(P(0, 0.38, -2.43), (0.025, 0.025, 0.025),
                                12, 8), M["chrome"])


def dashboard(shell, body):
    zf = 0.56
    part(body, "dash", prism([(0.60 - R, zf + 0.02), (0.90 - R, zf),
                              (0.935 - R, zf + 0.035), (0.950 - R, 0.70),
                              (0.958 - R, Z_COWL - 0.01),
                              (0.60 - R, Z_COWL - 0.01)], -0.68, 0.68),
         M["dash"], levels=0, bevel=0.008)
    hood = P(SEAT, 0.965, 0.60)
    part(body, "binnacle", slab(hood, (0.17, 0.035, 0.065)), M["dash"],
         bevel=0.02)
    for dx in (-0.075, 0.075):
        c = P(SEAT + dx, 0.905, zf - 0.002)
        part(body, f"dial{dx}", loft([(c + FORWARD * 0.004, 0.046, 0.046),
                                      (c, 0.044, 0.044)], UP, sides=16),
             M["gauge"], levels=0)
        part(body, f"dial_ring{dx}", torus(c - FORWARD * 0.002, FORWARD, UP,
                                           0.046, (0.004, 0.004), 20, 4),
             M["steel"], levels=0)
    part(body, "console", slab(P(0, 0.62, 0.53), (0.12, 0.26, 0.05)),
         M["dash"], bevel=0.02)
    part(body, "radio", slab(P(0, 0.78, 0.475), (0.09, 0.025, 0.006)),
         M["black"], levels=0, bevel=0.003)
    for x in (-0.55, -0.16, 0.16, 0.58):
        part(body, f"vent{x}", slab(P(x, 0.87, zf - 0.004),
                                    (0.05, 0.022, 0.005)), M["black"],
             levels=0)
    part(body, "glovebox", slab(P(-0.36, 0.80, zf - 0.002),
                                (0.17, 0.065, 0.004)), M["trim"], levels=0,
         bevel=0.003)
    # The mirror and the visors at the windscreen's top.
    part(body, "rear_view", slab(P(0, 1.30, zws(1.30) - 0.06),
                                 (0.10, 0.03, 0.012)), M["black"],
         bevel=0.01)
    for s in (1, -1):
        part(body, f"visor{s}", slab(P(s * 0.30, 1.335, zws(1.335) - 0.08),
                                     (0.22, 0.008, 0.075)), M["headliner"],
             bevel=0.006)


def glass(coll):
    g = assembly("glass", coll, V((0, 0, 0)), mesh="glass")
    shell = Shell()
    # The windscreen, behind the pillars and under the roof's edge.
    rows = []
    for i in range(7):
        h = 0.945 + (H_EAVE + 0.008 - 0.945) * i / 6
        xe = xs(h) - 0.006
        rows.append([P(xe * (2 * j / 8 - 1), h, zws(h) - 0.005)
                     for j in range(9)])
    start = shell.mark()
    shell.grid(rows, "glass")
    shell.orient(start, lambda c: WS_NORMAL)
    # The door windows, in the leaning sides.
    for s in (1, -1):
        corners = [(Z_DOOR + 0.012, H_BELT - 0.01),
                   (zws(H_BELT - 0.01) - 0.028, H_BELT - 0.01),
                   (zws(H_EAVE - 0.004) - 0.028, H_EAVE - 0.004),
                   (Z_DOOR + 0.012, H_EAVE - 0.004)]
        pts = [P(s * (xs(h) - 0.008), h, z) for z, h in corners]
        start = shell.mark()
        shell.polygon(pts, "glass")
        shell.orient(start, lambda c, s=s: V((s, 0, 0)))
        seal = [P(s * (xs(h) - 0.002), h, z) for z, h in
                ((Z_DOOR + 0.010, H_BELT + 0.005),
                 (Z_DOOR + 0.010, H_EAVE - 0.006),
                 (zws(H_EAVE - 0.006) - 0.030, H_EAVE - 0.006),
                 (zws(H_BELT + 0.005) - 0.030, H_BELT + 0.005))]
        part(g, f"window_seal{s}", tube(seal, 0.0055, sides=6),
             M["rubber"], levels=0)
    # The window in the cab's back, with rounded corners and its rubber.
    outline = rounded_rect(-0.505, 0.505, 1.005, 1.345, 0.06, 5)
    pts = [P(x, h, zb(h) + 0.006) for x, h in outline]
    start = shell.mark()
    shell.polygon(pts, "glass")
    shell.orient(start, lambda c: -FORWARD)
    part(g, "rear_seal", loop_tube([P(x, h, zb(h) - 0.003)
                                    for x, h in outline], 0.008),
         M["rubber"], levels=0)
    part(g, "screen_seal", tube([P(x, H_EAVE - 0.004,
                                   zws(H_EAVE - 0.004) + 0.003)
                                 for x in (-0.60, -0.3, 0.0, 0.3, 0.60)],
                                0.007, sides=6), M["rubber"], levels=0)
    shell.build(g, "glass_panes")


def hardtop(coll):
    """The fibreglass high top: a box on the bed's rails, its upper part
    run forward over the cab's roof nearly to the windscreen's head and
    rounded down to it, and rounded broadly over the back door."""
    ht = assembly("hardtop", coll, V((0, 0, 0)), mesh="hardtop")
    shell = Shell()
    z_rear = Z_TAIL - 0.008
    z_front = 0.36
    nose = (0.17, 0.26)  # the round of the top's front, along and down
    r_back = 0.30        # the top's back edge
    h_cab = H_ROOF + 0.010

    def width(z):
        w = 0.785
        rp = 0.10
        if z > z_front - rp:
            d = min(rp, z - (z_front - rp))
            w -= rp - math.sqrt(rp * rp - d * d)
        rr = 0.03
        if z < z_rear + rr:
            d = min(rr, z_rear + rr - z)
            w -= rr - math.sqrt(rr * rr - d * d)
        return w

    def base(z):
        """The foot: on the bed's rails, then stepping up behind the cab
        to sit over its roof."""
        return 0.962 + (h_cab - 0.962) * smoothstep(-0.47, -0.37, z)

    def top(z):
        if z > z_front - nose[0]:
            t = min(1.0, (z - (z_front - nose[0])) / nose[0])
            return H_HARDTOP - nose[1] + nose[1] * math.sqrt(1 - t * t)
        if z < z_rear + r_back:
            d = min(r_back, z_rear + r_back - z)
            return H_HARDTOP - r_back + math.sqrt(r_back ** 2 - d * d)
        return H_HARDTOP

    def half(z):
        hb, htop, xb = base(z), top(z), width(z)
        span = htop - hb
        xt = xb - 0.035 * span / 0.94
        r = min(0.06, 0.45 * span)
        pts = [(0.0, hb), (xb - 0.006, hb), (xb, hb + 0.006)]
        h_side = htop - r
        for t in (0.33, 0.66, 1.0):
            h = hb + 0.006 + (h_side - hb - 0.006) * t
            pts.append((xb - (xb - xt) * (h - hb) / span, h))
        for a in (22.5, 45.0, 67.5):
            a = math.radians(a)
            pts.append((xt - r + r * math.cos(a), h_side + r * math.sin(a)))
        pts.append((xt - r, htop))
        pts += [((xt - r) * k, htop + 0.010 * (1 - k * k))
                for k in (0.75, 0.5, 0.25, 0.0)]
        return pts

    tags = ["under", "edge"] + ["side"] * 11
    ring_tags = mirrored(half(-1), tags)[1]
    zs = samples(z_rear, z_front, 0.03,
                 [-0.47 + 0.1 * i / 20 for i in range(21)] +
                 end_samples(z_rear, r_back, -1, 16) +
                 end_samples(z_front, nose[0], 1, 16) +
                 end_samples(z_front, 0.10, 1, 8))
    body_loft(shell, zs, lambda z: mirrored(half(z), tags)[0], ring_tags,
              lambda tag, z: "hardtop", caps=("hardtop", "hardtop"))

    def side_point(s, z, h, lift):
        x = width(z) - 0.035 * (h - base(z)) / 0.94
        n = V((1, 0.035 / 0.94, 0)).normalized()
        return P(s * (x + n.x * lift), h + n.y * lift, z)

    # The upper part stands a little proud of the box below it, from the
    # back's rounding to the step behind the cab, and runs on over the
    # cab as the whole of the side there.
    for s in (1, -1):
        outline = rounded_rect(-1.96, -0.40, 1.465, 1.84, 0.05, 5)
        inner = rounded_rect(-1.95, -0.40, 1.475, 1.83, 0.04, 5)
        lo = [side_point(s, z, h, 0.0005) for z, h in outline]
        hi = [side_point(s, z, h, 0.010) for z, h in inner]
        start = shell.mark()
        shell.grid([lo, hi], "hardtop", wrap=True)
        shell.polygon(hi, "hardtop")
        shell.orient(start, lambda c, s=s: V((s, 0.1, 0)))
        # Rubber along the foot, on the bed's rail.
        part(ht, f"foot_seal{s}", tube([P(s * 0.783, 0.966, z) for z in
                                        (z_rear + 0.01, -1.4, -0.47)],
                                       0.010, sides=6), M["rubber"],
             levels=0)
    # The back door, top-hinged under the rounding: its gap, its handle,
    # and its hinges.
    door = rounded_rect(-0.64, 0.64, 1.00, 1.56, 0.05, 4)
    rows = [V((x, h, z_rear - 0.002)) for x, h in door + [door[0]]]
    strip = []
    for i, p in enumerate(rows):
        a, b = rows[max(0, i - 1)], rows[min(len(rows) - 1, i + 1)]
        t = (b - a).normalized()
        n = V((-t.y, t.x, 0)) * 0.003
        strip.append([P(p.x + n.x, p.y + n.y, p.z),
                      P(p.x - n.x, p.y - n.y, p.z)])
    start = shell.mark()
    shell.grid(strip, "seam")
    shell.orient(start, lambda c: -FORWARD)
    part(ht, "door_handle", slab(P(0, 1.10, z_rear - 0.012),
                                 (0.07, 0.018, 0.010)), M["black"],
         bevel=0.006)
    part(ht, "door_lock", loft([(P(0.10, 1.10, z_rear - 0.002), 0.011,
                                 0.011),
                                (P(0.10, 1.10, z_rear - 0.008), 0.010,
                                 0.010)], UP, sides=10), M["chrome"],
         levels=0)
    for x in (-0.40, 0.40):
        part(ht, f"hinge{x}", slab(P(x, 1.575, z_rear - 0.006),
                                   (0.05, 0.010, 0.006)), M["black"],
             levels=0, bevel=0.003)
    shell.build(ht, "hardtop_shell")


def tailgate(coll):
    hinge = P(0, H_BED, Z_TAIL + 0.022)
    tg = assembly("tailgate", coll, hinge, mesh="tailgate",
                  point="tailgate_hinge")
    shell = Shell()
    z_out = Z_TAIL
    part(tg, "gate", slab(P(0, (H_BED + H_BELT) / 2 - 0.002,
                            Z_TAIL + 0.022) - hinge,
                          (X_IN - 0.006, (H_BELT - H_BED) / 2 - 0.002,
                           0.022)), M["paint"], levels=1, bevel=0.006)
    # The top rail and the panel the name stands in.
    part(tg, "rail", slab(P(0, 0.925, z_out - 0.002) - hinge,
                          (X_IN - 0.02, 0.022, 0.004)), M["paint"],
         levels=1, bevel=0.003)
    part(tg, "latch", slab(P(0, 0.925, z_out - 0.008) - hinge,
                           (0.075, 0.012, 0.004)), M["black"], bevel=0.003)
    lettering(shell, "VOLKSWAGEN", 0.082, 0.004,
              P(0, 0.79, z_out) - hinge, -RIGHT, UP, "paint")
    shell.build(tg, "tailgate_letters")


def lathe(shell, profile, mat, segments, axis_out=-RIGHT, turn=0.0):
    """Revolves `profile`, (radius, distance outward) pairs, about the
    wheel's axle."""
    side = UP
    ahead = axis_out.cross(side)
    rows = []
    for r, a in profile:
        rows.append([axis_out * a + side * (r * math.cos(t)) +
                     ahead * (r * math.sin(t))
                     for t in (turn + 2 * math.pi * k / segments
                               for k in range(segments))])
    shell.grid(rows, mat, wrap=True)


def wheels(coll):
    """The right front wheel exports; the other three only display it."""
    first = assembly("wheel", coll, V((-TRACK_F, 0, AXLE)), mesh="wheel",
                     point="wheel_fr")
    shell = Shell()
    out = -RIGHT

    def facing(start, toward):
        shell.orient(start, toward)

    # The tyre, its sidewalls rounding to the tread, and the tread blocks.
    tyre = [(0.166, -0.074), (0.182, -0.083), (0.205, -0.088),
            (0.235, -0.089), (0.260, -0.084), (0.274, -0.075),
            (0.280, -0.062), (0.281, -0.03), (0.281, 0.0), (0.281, 0.03),
            (0.280, 0.062), (0.274, 0.075), (0.260, 0.084),
            (0.235, 0.089), (0.205, 0.088), (0.182, 0.083),
            (0.166, 0.074)]
    start = shell.mark()
    lathe(shell, [(r, -a) for r, a in tyre], "tyre", 64)
    facing(start, lambda c: c - (c - out * c.dot(out)).normalized() * 0.225)
    blocks = 48
    for k in range(blocks):
        for row, (a0, a1, shift) in enumerate(((-0.070, -0.026, 0.0),
                                               (-0.018, 0.018, 0.5),
                                               (0.026, 0.070, 0.0))):
            t0 = 2 * math.pi * (k + shift) / blocks
            t1 = t0 + 2 * math.pi * 0.70 / blocks
            if row != 1:
                t0 += 0.01 * (1 if row == 0 else -1)
            start = shell.mark()
            pts = []
            for r in (0.2795, 0.2875):
                for a in (a0, a1):
                    for t in (t0, t1):
                        pts.append(out * -a + UP * (r * math.cos(t)) +
                                   FORWARD * (r * math.sin(t)))
            # pts index: r(0/1)*4 + a*2 + t
            for f in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1),
                      (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
                shell.polygon([pts[i] for i in f], "tyre")
            shell.orient_closed(start)

    # The steel rim: the flange and barrel, then the dished disc, its
    # ring of eight vents, the raised centre with four nuts, and a cap.
    rim = [(0.160, 0.060), (0.168, 0.070), (0.176, 0.071), (0.179, 0.067),
           (0.174, 0.062), (0.165, 0.060), (0.160, 0.050), (0.150, 0.046),
           (0.138, 0.047), (0.131, 0.049)]
    start = shell.mark()
    lathe(shell, rim, "steel", 64)
    facing(start, lambda c: out + (c - out * c.dot(out)) * 2.0)
    vent_r0, vent_r1 = 0.131, 0.083
    a_vent = lambda r: 0.049 + (r - 0.131) * -0.06  # noqa: E731
    holes = []
    for k in range(8):
        t = 2 * math.pi * (k + 0.5) / 8
        loop = []
        for i in range(16):
            u = 2 * math.pi * i / 16
            rr = 0.1075 + 0.0135 * math.cos(u)
            tt = t + 0.19 * math.sin(u) * (1.0 + 0.15 * math.cos(u))
            loop.append((rr, tt))
        holes.append(loop)
    circles = []
    for r in (vent_r0, vent_r1):
        circles.append([(r, 2 * math.pi * i / 64) for i in range(64)])
    flat = [[Vector((r * math.cos(t), r * math.sin(t), 0)) for r, t in lp]
            for lp in circles + holes]
    tris = geometry.tessellate_polygon(flat)
    allpts = [p for lp in flat for p in lp]

    def disc_point(p, back=0.0):
        r = math.hypot(p.x, p.y)
        return (out * (a_vent(r) - back) + UP * p.x + FORWARD * p.y)

    start = shell.mark()
    shell.mesh([disc_point(p) for p in allpts], tris, "steel", smooth=True)
    facing(start, lambda c: out)
    start = shell.mark()
    shell.mesh([disc_point(p, 0.003) for p in allpts],
               [list(reversed(t)) for t in tris], "steel", smooth=True)
    facing(start, lambda c: -out)
    centre = [(0.083, 0.0515), (0.078, 0.054), (0.070, 0.057),
              (0.062, 0.058), (0.040, 0.059), (0.032, 0.0595),
              (0.030, 0.061), (0.024, 0.063), (0.012, 0.0645),
              (0.0, 0.065)]
    start = shell.mark()
    lathe(shell, centre[:6], "steel", 48)
    facing(start, lambda c: out)
    start = shell.mark()
    lathe(shell, centre[5:], "hub", 24)
    facing(start, lambda c: out)
    for k in range(4):
        t = 2 * math.pi * k / 4 + math.pi / 4
        c = out * 0.058 + UP * (0.050 * math.cos(t)) + \
            FORWARD * (0.050 * math.sin(t))
        part(first, f"nut{k}", loft([(c, 0.0095, 0.0095),
                                     (c + out * 0.009, 0.0095, 0.0095),
                                     (c + out * 0.013, 0.006, 0.006)],
                                    UP, sides=6), M["hub"], levels=0)
    # The drum behind, which the vents show.
    start = shell.mark()
    lathe(shell, [(0.0, 0.030), (0.115, 0.030), (0.118, 0.020),
                  (0.118, -0.050), (0.0, -0.050)], "drum", 32)
    shell.orient_closed(start)
    shell.build(first, "wheel_parts")

    for tag, x, z in (("fl", TRACK_F, AXLE), ("rr", -TRACK_R, -AXLE),
                      ("rl", TRACK_R, -AXLE)):
        other = assembly(f"wheel_{tag}", coll, V((x, 0, z)),
                         point=f"wheel_{tag}")
        if x > 0:
            other.rotation_euler = (0, 0, math.pi)
        for child in first.children:
            dup = child.copy()
            coll.objects.link(dup)
            dup.parent = other


def seats(coll):
    s_asm = assembly("seats", coll, V((0, 0, 0)), mesh="seats")
    back = (UP - FORWARD * 0.12).normalized()
    out = RIGHT.cross(back)
    for s in (-1, 1):
        x = SEAT * s
        part(s_asm, f"cushion{s}", slab(P(x, 0.50, -0.05),
                                        (0.235, 0.05, 0.235)), M["seat"],
             bevel=0.035)
        part(s_asm, f"cushion_insert{s}", slab(P(x, 0.551, -0.05),
                                               (0.13, 0.004, 0.19)),
             M["seat_insert"], bevel=0.003)
        part(s_asm, f"back{s}", slab(P(x, 0.85, -0.22), (0.23, 0.30, 0.055),
                                     axes=(RIGHT, back, out)), M["seat"],
             bevel=0.035)
        part(s_asm, f"back_insert{s}", slab(P(x, 0.86, -0.22) + out * 0.055,
                                            (0.13, 0.22, 0.004),
                                            axes=(RIGHT, back, out)),
             M["seat_insert"], bevel=0.003)
        part(s_asm, f"headrest{s}", slab(P(x, 1.25, -0.275),
                                         (0.13, 0.07, 0.045),
                                         axes=(RIGHT, back, out)),
             M["seat"], bevel=0.03)
        for dx in (-0.07, 0.07):
            part(s_asm, f"post{s}{dx}", tube([P(x + dx, 1.13, -0.26),
                                              P(x + dx, 1.20, -0.268)],
                                             0.006, sides=6), M["chrome"],
                 levels=0)
        part(s_asm, f"seat_base{s}", slab(P(x, 0.41, -0.05),
                                          (0.19, 0.045, 0.20)), M["black"],
             levels=0)
    # The gear lever and the handbrake between the seats.
    part(s_asm, "gear_boot", blob(P(0, 0.40, 0.30), (0.05, 0.03, 0.05), 10,
                                  5), M["black"])
    part(s_asm, "lever", tube([P(0, 0.40, 0.30), P(0, 0.62, 0.26)],
                              0.007, sides=6), M["chrome"], levels=0)
    part(s_asm, "knob", blob(P(0, 0.64, 0.26), (0.022, 0.022, 0.022), 10,
                             6), M["black"])
    part(s_asm, "handbrake", tube([P(0, 0.40, 0.12), P(0, 0.46, -0.08)],
                                  0.012, sides=6), M["black"], levels=0)
    # Clutch, brake, and accelerator.
    for dx, w in ((0.115, 0.035), (0.035, 0.035), (-0.075, 0.025)):
        x = SEAT + dx
        part(s_asm, f"pedal{dx}", slab(P(x, 0.47, 0.68),
                                       (w, 0.035, 0.006),
                                       axes=(RIGHT, (UP - FORWARD * 0.5)
                                             .normalized(),
                                             (FORWARD + UP * 0.5)
                                             .normalized())),
             M["rubber"], levels=0, bevel=0.003)
        part(s_asm, f"pedal_arm{dx}", tube([P(x, 0.49, 0.69),
                                            P(x, 0.66, 0.70)], 0.007,
                                           sides=6), M["black"], levels=0)


def steering(coll):
    """The wheel turns about its column; the column itself stays."""
    hub = P(SEAT, STEERING_HUB_H, STEERING_HUB_Z)
    column = assembly("column", coll, V((0, 0, 0)), mesh="column")
    part(column, "shaft", tube([hub + COLUMN * 0.03, hub + COLUMN * 0.30],
                               0.022, sides=8), M["black"], levels=0)
    part(column, "shroud", loft([(hub + COLUMN * 0.06, 0.05, 0.045),
                                 (hub + COLUMN * 0.22, 0.05, 0.05)],
                                RIGHT, sides=8), M["dash"])

    w = assembly("steering_wheel", coll, hub, mesh="steering_wheel",
                 point="steering_hub")
    o = V((0, 0, 0))
    radius = STEERING_RADIUS
    part(w, "rim", torus(o, COLUMN, RIGHT, radius, (0.016, 0.016), 32, 8),
         M["dash"])
    down = RIGHT.cross(COLUMN).normalized()
    spokes = []
    for a in (12, 168, 68, 112):
        t = math.radians(a)
        d = RIGHT * math.cos(t) + down * math.sin(t)
        spokes.append(tube([COLUMN * 0.035 + d * 0.05,
                            d * (radius - 0.008)], 0.010, sides=6))
    part(w, "spokes", merge(*spokes), M["dash"], levels=0)
    part(w, "pad", slab(COLUMN * 0.05, (0.068, 0.055, 0.025),
                        axes=(RIGHT, down, COLUMN)), M["dash"], bevel=0.02)
    part(w, "pad_badge", loft([(COLUMN * 0.024, 0.018, 0.018),
                               (COLUMN * 0.022, 0.018, 0.018)], RIGHT,
                              sides=16), M["chrome"], levels=0)

    assembly("steering_column", coll, hub + COLUMN, point="steering_column")
    assembly("grip_left", coll, hub + RIGHT * radius, point="grip_left")
    assembly("grip_right", coll, hub - RIGHT * radius, point="grip_right")


if __name__ == "__main__":
    build()
