"""Builds models/bus.blend: a country bus of the fifties.

    Blender -b --factory-startup --python tools/figure/bus.py

A forward-control "bulldog" bus of the kind Scania-Vabis and Volvo built
for the Swedish country lines in the fifties: eleven metres of rounded
coachwork, two and a half wide and not quite three high, with the driver
beside the engine at the very front, a two-piece windscreen wrapped
round the corners, a destination box in the dome over it, a chromed
grille between round headlamps, a row of rubber-set windows under a drip
rail, and a luggage rack on the back of the roof with a ladder up to
it. It wears the yellow of the Norrland post coaches under a cream
roof, with a navy band and bright strips along the waist, and its door
is on the right, behind the front wheel, as they were rebuilt for
right-hand traffic in 1967: two leaves, each hinged on its own jamb.
Forty-two sit in it on leatherette, and the driver on his own seat over
the front axle.

It is authored life size in the game's frame -- x to the driver's left,
y up, z forward -- with its origin on the road under the middle of the
coachwork, which reaches 5.5 m ahead of it and behind. The coachwork is
one skin lofted from cross-sections along its length, its ends drawn in
to a superellipse in plan and to a dome above, with a lining inside it;
the windows, the door, and the wheel arches are left out of both, and
the edges between them closed.

Assemblies, each exported in its empty's frame by
tools/figure/export_model.py: the body, with the cabin and everything
fixed to it; the glass, to be drawn last; the door's two leaves, each
about the foot of its hinge, opening outward (the fore leaf a quarter
turn about +y, the aft one about -y); one front wheel and one rear pair
(the right-hand ones, about x; the game turns them about for the left);
and the steering wheel, about its column. The empties marked as points
are the four axle ends, the leaves' hinges, the driver's seat, pedals,
and grips, the steering wheel's hub and a point down its column, the
passengers' seats from the front backward and from the right to the
left (seat_01 to seat_42), the ground beside the door, the step inside
it and the aisle above that, the lamps, and the middle of the
destination sign, which is blank for the game to letter (0.92 m by
0.15 m).

Rebuilding discards hand edits to the .blend.
"""

import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(__file__))
from shapes import (V, RIGHT, UP, FORWARD, Cage, assembly, blender,  # noqa
                    blob, loft, material, merge, part, slab, torus, tube)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "models", "bus.blend")

# Key dimensions, metres; heights above the ground.
R = 0.49            # the wheels' radius, on 9.00-20 tyres
TYRE = 0.12         # half a tyre's width
AXLE_F = 4.15       # the front axle, ahead of the origin
AXLE_R = -2.30      # the rear axle
AXLES = (AXLE_F, AXLE_R)
TRACK_F = 1.07      # the centre line to a front wheel's centre
TRACK_R = 0.945     # and to the middle of a rear pair
PAIR = 0.135        # the middle of a pair to each of its tyres
W = 1.25            # half the body's width, at the waist
WE = 1.15           # and at the eaves
ZB = 4.90           # where the front's rounding begins
ZA = -4.88          # and the back's
DF, DR = 0.60, 0.62  # how far each end reaches beyond that
POWER = 2.5         # the ends are superellipses of this power in plan
SKIRT = 0.38        # the body's lower edge
FLOOR = 0.82
STRIPE = 1.33       # the navy band's lower edge
BELT = 1.45         # the window sills, and the band's upper edge
EAVE = 2.33         # the windows' tops
CROWN = 2.85        # the roof's
TURN = 0.05         # how far the sides turn under by the skirt
FULL = 1.05         # the height they turn under from
RAKE_F, RAKE_R = 0.16, 0.10    # the ends' lean back between belt and eave
DOME_F, DOME_R = 0.34, 0.44    # and from the eave to the crown
T = 0.035           # the skin to the lining
ARCH = 0.62         # the wheel arches' radius
HOUSE = 0.61        # the centre line to the wheel housings' inner walls
DOOR = (2.40, 3.45)
# The windows along each side; the door stands in its bay on the right.
BAYS = [(-4.83, -3.695), (-3.625, -2.49), (-2.42, -1.285), (-1.215, -0.08),
        (-0.01, 1.125), (1.195, 2.33), DOOR, (3.52, 4.85)]
CORNER = 0.06       # the windows' corners
SLIDER = 2.04       # the bar under their sliding lights
SCREEN = 0.95       # how far round each front corner the windscreen comes
REAR_WINDOW = 0.90  # and the back window
WELL = 0.77         # the centre line to the step well's inner wall
STEP = 0.52         # the step's height

SEAT = 0.62         # the driver's distance from the centre line
SEAT_Z = 4.45
CUSHION = FLOOR + 0.45      # the passengers' cushions' tops
STEERING_HUB = V((SEAT, 1.66, SEAT_Z + 0.45))
COLUMN = V((0, -0.9, 0.44)).normalized()
STEERING_RADIUS = 0.25

PALETTE = {
    "paint": (0.97, 0.74, 0.09),
    "cream": (0.94, 0.91, 0.80),
    "navy": (0.09, 0.15, 0.36),
    "chrome": (0.82, 0.83, 0.85),
    "steel": (0.60, 0.62, 0.64),
    "black": (0.07, 0.07, 0.075),
    "rubber": (0.045, 0.045, 0.05),
    "tire": (0.07, 0.07, 0.07),
    "under": (0.09, 0.09, 0.09),
    "glass": (0.13, 0.17, 0.19),
    "mirror": (0.78, 0.80, 0.82),
    "lining": (0.76, 0.79, 0.70),
    "floor": (0.33, 0.25, 0.19),
    "seat": (0.47, 0.15, 0.12),
    "dash": (0.11, 0.11, 0.115),
    "leather": (0.50, 0.30, 0.15),
    "canvas": (0.36, 0.42, 0.32),
    "plate": (0.93, 0.93, 0.90),
    "lens": (1.0, 0.95, 0.72),
    "amber": (1.0, 0.55, 0.10),
    "tail_light": (1.0, 0.18, 0.10),
    "sign": (0.98, 0.94, 0.80),
}

LAMPS = ("lens", "amber", "tail_light", "sign")

# Coach enamel under a clear coat, bright trim, leatherette on the seats
# and linoleum underfoot; the lamps glow through their lenses, and the
# sign is lit from behind.
FINISH = {
    "paint": dict(roughness=0.34, coat=0.6),
    "cream": dict(roughness=0.36, coat=0.5),
    "navy": dict(roughness=0.34, coat=0.6),
    "chrome": dict(metallic=1.0, roughness=0.12),
    "steel": dict(metallic=0.85, roughness=0.35),
    "black": dict(roughness=0.55),
    "rubber": dict(roughness=0.85),
    "tire": dict(roughness=0.92),
    "under": dict(roughness=0.9),
    "glass": dict(roughness=0.02, transmission=1.0, ior=1.52,
                  tint=(0.80, 0.90, 0.88)),
    "mirror": dict(metallic=1.0, roughness=0.02),
    "lining": dict(roughness=0.8),
    "floor": dict(roughness=0.6, coat=0.2),
    "seat": dict(roughness=0.5, sheen=0.25),
    "dash": dict(roughness=0.6),
    "leather": dict(roughness=0.55, sheen=0.2),
    "canvas": dict(roughness=0.9, sheen=0.4),
    "plate": dict(roughness=0.4),
    "lens": dict(roughness=0.05, transmission=0.6, emission=0.6),
    "amber": dict(roughness=0.1, transmission=0.5, emission=0.6),
    "tail_light": dict(roughness=0.1, transmission=0.5, emission=0.6),
    "sign": dict(roughness=0.4, emission=0.5),
}

O = V((0, 0, 0))


def newell(points):
    n = V((0, 0, 0))
    for i, p in enumerate(points):
        n += p.cross(points[(i + 1) % len(points)])
    return n


def mean(points):
    return sum(points, V((0, 0, 0))) / len(points)


class Skin:
    """A mesh gathered face by face, each with its material and the way
    it faces. Faces given the same group share their vertices where
    they meet, and so shade smoothly across them; the others are
    flat."""

    def __init__(self, m):
        self.m = m
        self.verts = []
        self.faces = []
        self.mats = []
        self.smooth = []
        self.shared = {}
        self.groups = 0

    def group(self):
        self.groups += 1
        return self.groups

    def vert(self, p, group):
        key = (group,) + tuple(round(c, 5) for c in p)
        if group is None or key not in self.shared:
            self.shared[key] = len(self.verts)
            self.verts.append(p)
        return self.shared[key]

    def face(self, points, mat, toward, group=None):
        pts = []
        for p in points:
            if not pts or (p - pts[-1]).length > 1e-6:
                pts.append(p)
        while len(pts) > 2 and (pts[0] - pts[-1]).length < 1e-6:
            pts.pop()
        if len(pts) < 3 or newell(pts).length < 1e-10:
            return
        if newell(pts).dot(toward) < 0:
            pts.reverse()
        self.faces.append([self.vert(p, group) for p in pts])
        self.mats.append(mat)
        self.smooth.append(group is not None)

    def build(self, asm, name, origin=O):
        names = list(dict.fromkeys(self.mats))
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata([tuple(blender(v - origin)) for v in self.verts],
                         [], self.faces)
        for n in names:
            mesh.materials.append(self.m[n])
        for poly, mat, smooth in zip(mesh.polygons, self.mats, self.smooth):
            poly.material_index = names.index(mat)
            poly.use_smooth = smooth
        mesh.validate()
        mesh.update()
        obj = bpy.data.objects.new(name, mesh)
        asm.users_collection[0].objects.link(obj)
        obj.parent = asm
        return obj


def double(skin, outer, inner, mat, lining, hole, edge, border):
    """A wall between two grids of points: the cells of `outer` facing
    away from `inner`, those of `inner` facing back, and an edge closing
    the wall wherever a cell meets a hole, or the grid's end along the
    axes `border` names. `mat`, `lining`, and `edge` name materials by
    cell."""
    ni, nj = len(outer) - 1, len(outer[0]) - 1
    out_group, in_group = skin.group(), skin.group()

    def solid(i, j):
        return 0 <= i < ni and 0 <= j < nj and not hole(i, j)

    def quad(grid, i, j):
        return [grid[i][j], grid[i + 1][j], grid[i + 1][j + 1],
                grid[i][j + 1]]

    for i in range(ni):
        for j in range(nj):
            if not solid(i, j):
                continue
            qo, qi = quad(outer, i, j), quad(inner, i, j)
            co, ci = mean(qo), mean(qi)
            skin.face(qo, mat(i, j), co - ci, out_group)
            skin.face(qi, lining(i, j), ci - co, in_group)
            for (a, b), p, q in (((i - 1, j), 0, 3), ((i + 1, j), 1, 2),
                                 ((i, j - 1), 0, 1), ((i, j + 1), 3, 2)):
                if solid(a, b):
                    continue
                if not border[0] and not 0 <= a < ni:
                    continue
                if not border[1] and not 0 <= b < nj:
                    continue
                rim = [qo[p], qo[q], qi[q], qi[p]]
                skin.face(rim, edge(i, j), mean(rim) - (co + ci) / 2)


def lathe(profile, segments=24):
    """Turns `profile`, (radius, distance along x) pairs from the axle
    round the outside and back to it, about x."""
    cage = Cage()
    rings = []
    for r, x in profile:
        base = len(cage.verts)
        if r <= 0:
            cage.verts.append(V((x, 0, 0)))
            rings.append([base])
            continue
        for k in range(segments):
            t = 2 * math.pi * k / segments
            cage.verts.append(V((x, r * math.cos(t), r * math.sin(t))))
        rings.append(list(range(base, base + segments)))
    for a, b in zip(rings, rings[1:]):
        for k in range(segments):
            n = (k + 1) % segments
            if len(a) > 1 and len(b) > 1:
                cage.faces.append([a[k], a[n], b[n], b[k]])
            elif len(b) > 1:
                cage.faces.append([a[0], b[n], b[k]])
            elif len(a) > 1:
                cage.faces.append([a[k], a[n], b[0]])
    volume = 0.0
    for f in cage.faces:
        p = [cage.verts[i] for i in f]
        for i in range(1, len(p) - 1):
            volume += p[0].dot(p[i].cross(p[i + 1]))
    if volume < 0:
        cage.faces = [list(reversed(f)) for f in cage.faces]
    return cage


# The body's lines.

def x_low(h):
    """Half the body's width below the belt."""
    k = max(0.0, (FULL - h) / (FULL - SKIRT))
    return W - TURN * k * k


def roof(a, t=0.0):
    """The roof's section, from the eave (0) to the crown (a quarter
    turn), `t` inside the skin."""
    if a >= math.pi / 2 - 1e-9:
        return 0.0, CROWN - t
    return ((WE - t) * math.cos(a) ** 0.8,
            EAVE + (CROWN - EAVE - t) * math.sin(a) ** 0.9)


def roof_y(x):
    a = math.acos(min(1.0, (abs(x) / WE) ** 1.25))
    return roof(a)[1]


def bottom(z):
    """The lower edge of the sides, which the wheel arches cut."""
    for centre in AXLES:
        d = abs(z - centre)
        if d <= ARCH + 1e-6:
            return R + math.sqrt(max(0.0, ARCH * ARCH - d * d))
    return SKIRT


def reach(cx):
    """How far along an end its plan has come where it has narrowed to
    `cx` of the body's width."""
    return (1 - cx ** POWER) ** (1 / POWER)


NL = 5              # the cells between the lower edge and the band
ROOF = [math.radians(a) for a in (8, 18, 30, 42, 54, 66, 78, 90)]


def half_section(low, t):
    """Half a cross-section from the lower edge at `low` to the crown,
    `t` inside the skin: each point's half width, height, and how far
    the front and the back reach at it."""
    cols = []
    for k in range(NL + 1):
        h = low + (STRIPE - low) * k / NL
        x = x_low(h)
        cols.append((x - t, h, DF - (W - x) - t, DR - (W - x) - t))
    cols.append((W - t, BELT, DF - t, DR - t))
    cols.append((WE - t, EAVE, DF - RAKE_F - t, DR - RAKE_R - t))
    for a in ROOF:
        x, y = roof(a, t)
        k = 1 - math.cos(a)
        cols.append((x, y, DF - RAKE_F - DOME_F * k - t,
                     DR - RAKE_R - DOME_R * k - t))
    return cols


HALF = NL + 3 + len(ROOF)       # the points in half a section
J_STRIPE = NL                   # the navy band's cell
J_BAND = NL + 1                 # the windows'


def cell(j):
    """The side (-1 the right) a section's cell lies on, and the cell it
    mirrors on the right."""
    if j < HALF - 1:
        return -1, j
    return 1, 2 * HALF - 3 - j


# The stations along the body: (end, z, cx, reach), from the middle of
# the back to the middle of the front. `end` is -1 on the back's
# rounding, 1 on the front's, and 0 between them.
CAP = (0.995, 0.98, 0.95, 0.90, 0.83, 0.74, 0.62, 0.48, 0.32, 0.16, 0.0)


def stations():
    zs = {ZA, ZB}
    for z0, z1 in BAYS:
        zs |= {z0, z1}
    for centre in AXLES:
        zs |= {round(centre + ARCH * math.cos(math.pi * k / 15), 4)
               for k in range(16)}
        zs |= {round(centre - ARCH - 0.004, 4),
               round(centre + ARCH + 0.004, 4)}
    return ([(-1, ZA, cx, reach(cx)) for cx in reversed(CAP)] +
            [(0, z, 1.0, 0.0) for z in sorted(zs)] +
            [(1, ZB, cx, reach(cx)) for cx in CAP])


STATIONS = stations()


def section(st, t=0.0):
    end, z, cx, far = st
    half = half_section(bottom(z) if end == 0 else SKIRT, t)

    def at(s, col):
        x, y, df, dr = col
        if end == 0:
            return V((s * x, y, z))
        if end > 0:
            return V((s * x * cx, y, ZB + df * far))
        return V((s * x * cx, y, ZA - dr * far))

    return ([at(-1, c) for c in half] +
            [at(1, c) for c in reversed(half[:-1])])


def within(z0, z1, span):
    return span[0] - 1e-6 <= z0 and z1 <= span[1] + 1e-6


def hole(i, j):
    """Whether the skin is left out between stations i and i + 1 and
    points j and j + 1 of their sections."""
    a, b = STATIONS[i], STATIONS[i + 1]
    side, k = cell(j)
    if a[0] == 0 and b[0] == 0:
        if side < 0 and k <= J_BAND and within(a[1], b[1], DOOR):
            return True
        return k == J_BAND and any(
            within(a[1], b[1], bay) for bay in BAYS
            if side > 0 or bay != DOOR)
    if k != J_BAND or a[0] != b[0]:
        return False
    return max(a[2], b[2]) <= (SCREEN if a[0] > 0 else REAR_WINDOW) + 1e-6


# Places on the skin. A post is a vertical line up it: (end, side, z,
# cx), on a side at z or round an end where it has narrowed to cx.

def post(end, s, z=0.0, cx=1.0):
    return (end, s, z, cx)


def face_post(end, x):
    """The post at `x` from the centre line across an end."""
    return (end, -1 if x < 0 else 1, 0.0, abs(x) / W)


def on_skin(p, h, lift=0.0):
    """The skin at height `h` up a post, `lift` out from it."""
    end, s, z, cx = p
    if h <= BELT:
        x = x_low(h)
        shrink = W - x
    else:
        k = min(1.0, (h - BELT) / (EAVE - BELT))
        x = W + (WE - W) * k
        shrink = k * (RAKE_F if end > 0 else RAKE_R)
    if end == 0:
        return V((s * (x + lift), h, z))
    depth = (DF if end > 0 else DR) - shrink
    far = reach(cx)
    n = V((cx ** (POWER - 1) / x, 0, far ** (POWER - 1) / depth))
    n.normalize()
    return V((s * (x * cx + n.x * lift), h,
              (ZB if end > 0 else ZA) + end * (depth * far + n.z * lift)))


def outward(p, h):
    return on_skin(p, h, 1.0) - on_skin(p, h)


def dome(end, s, cx, a, lift=0.0):
    """An end's dome over the eave: `a` up the roof's section where the
    plan has narrowed to `cx`."""
    x, y = roof(a)
    k = 1 - math.cos(a)
    depth = (DF - RAKE_F - DOME_F * k if end > 0
             else DR - RAKE_R - DOME_R * k)
    return V((s * x * cx + 0.0, y + math.sin(a) * lift,
              (ZB if end > 0 else ZA) +
              end * (depth * reach(cx) + math.cos(a) * lift)))


def cap_run(end, limit=1.0):
    """The posts round an end, from its right-hand corner to its left."""
    cxs = [c for c in (1.0,) + CAP if c <= limit + 1e-6]
    return ([post(end, -1, cx=c) for c in cxs] +
            [post(end, 1, cx=c) for c in reversed(cxs[:-1])])


def side_spans(s, low, door=True):
    """The stretches of a side that a line at height `low` runs along,
    between the arches and the door."""
    cuts = []
    if low < R + ARCH + 0.03:
        cuts += [(c - ARCH - 0.02, c + ARCH + 0.02) for c in AXLES]
    if s < 0 and door:
        cuts.append(DOOR)
    spans, z = [], ZA
    for c0, c1 in sorted(cuts):
        spans.append((z, c0))
        z = c1
    return spans + [(z, ZB)]


def ribbon(skin, posts, profile, mat):
    """A strip along `posts`, its section the (height, lift) pairs of
    `profile`."""
    group = skin.group()
    rows = [[on_skin(p, h, lift) for h, lift in profile] for p in posts]
    for a in range(len(rows) - 1):
        out = (outward(posts[a], profile[0][0]) +
               outward(posts[a + 1], profile[0][0]))
        for b in range(len(profile) - 1):
            skin.face([rows[a][b], rows[a + 1][b], rows[a + 1][b + 1],
                       rows[a][b + 1]], mat, out, group)


def line(skin, profile, mat, ends=True, door=True):
    """A ribbon the whole way round the body."""
    low = min(h for h, _ in profile)
    for s in (-1, 1):
        for z0, z1 in side_spans(s, low, door):
            ribbon(skin, [post(0, s, z0), post(0, s, z1)], profile, mat)
    if ends:
        for end in (-1, 1):
            ribbon(skin, cap_run(end), profile, mat)


def bright(h):
    """A half-round bright strip's section."""
    return [(h - 0.011, 0.001), (h, 0.009), (h + 0.011, 0.001)]


# Building.

def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    coll = bpy.data.collections.new("bus")
    scene.collection.children.link(coll)
    m = {name: material(name, c, emit=name in LAMPS, **FINISH[name])
         for name, c in PALETTE.items()}

    b = assembly("body", coll, O, mesh="body")
    g = assembly("glass", coll, O, mesh="glass")
    skin, panes = Skin(m), Skin(m)
    shell(skin)
    windows(skin, panes)
    floor(skin)
    housings(skin)
    line(skin, bright(STRIPE), "chrome")
    line(skin, bright(BELT - 0.035), "chrome")
    line(skin, bright(0.72), "chrome", ends=False)
    line(skin, [(EAVE + 0.016, 0.002), (EAVE + 0.03, 0.014),
                (EAVE + 0.044, 0.002)], "cream", door=False)
    front(coll, b, skin, m)
    back(coll, b, m)
    skin.build(b, "shell")
    panes.build(g, "panes")
    arches(b, m)
    roof_fittings(b, m)
    cabin(coll, b, m)

    doors(coll, m)
    wheels(coll, m)
    steering(coll, m)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
    print(f"wrote {OUT}: {len(coll.objects)} objects")


def shell(skin):
    """The skin and its lining."""
    outer = [section(st) for st in STATIONS]
    inner = [section(st, T) for st in STATIONS]

    def paint(i, j):
        k = cell(j)[1]
        return "paint" if k < J_STRIPE else "navy" if k == J_STRIPE \
            else "cream"

    def edge(i, j):
        return "paint" if cell(j)[1] <= J_STRIPE else "cream"

    double(skin, outer, inner, paint, lambda i, j: "lining", hole, edge,
           border=(False, True))


def corners(z0, z1, h0, h1, r, n=4):
    """Each corner of a window with its quarter round, as (z, height)
    pairs, the rounds following one another about the window."""
    out = []
    for zc, hc, dz, dh, back in ((z0, h0, 1, 1, True),
                                 (z1, h0, -1, 1, False),
                                 (z1, h1, -1, -1, True),
                                 (z0, h1, 1, -1, False)):
        arc = []
        for k in range(n + 1):
            t = 0.5 * math.pi * k / n
            arc.append((zc + dz * r * (1 - math.sin(t)),
                        hc + dh * r * (1 - math.cos(t))))
        out.append(((zc, hc), list(reversed(arc)) if back else arc))
    return out


def windows(skin, panes):
    """The side windows: their corners rounded in the skin and the
    lining, a rubber round each, and its pane; then the windscreen and
    the back window, which follow the ends' curve."""
    def at(s, zh, inset=0.0):
        return on_skin(post(0, s, zh[0]), zh[1], -inset)

    gasket = 0.012
    for s in (-1, 1):
        for z0, z1 in BAYS:
            if s < 0 and (z0, z1) == DOOR:
                continue
            rounds = corners(z0, z1, BELT, EAVE, CORNER)
            middle = at(s, ((z0 + z1) / 2, (BELT + EAVE) / 2), T / 2)
            for corner, arc in rounds:
                for inset, mat, way in ((0.0, "cream", s),
                                        (T, "lining", -s)):
                    for a, b in zip(arc, arc[1:]):
                        skin.face([at(s, corner, inset), at(s, a, inset),
                                   at(s, b, inset)], mat, V((way, 0, 0)))
                group = skin.group()
                for a, b in zip(arc, arc[1:]):
                    rim = [at(s, a), at(s, b), at(s, b, T), at(s, a, T)]
                    skin.face(rim, "cream", middle - mean(rim), group)
            loop = [p for _, arc in rounds for p in arc]
            wide = [p for _, arc in corners(z0 - gasket, z1 + gasket,
                                            BELT - gasket, EAVE + gasket,
                                            CORNER + gasket)
                    for p in arc]
            for k in range(len(loop)):
                n = (k + 1) % len(loop)
                skin.face([at(s, loop[k], -0.004), at(s, loop[n], -0.004),
                           at(s, wide[n], -0.004), at(s, wide[k], -0.004)],
                          "rubber", V((s, 0, 0)))
            panes.face([at(s, p, 0.012) for p in loop], "glass",
                       V((s, 0, 0)))
            # The bar under the sliding lights.
            if z1 - z0 > 0.9:
                skin.face([at(s, (z0, SLIDER - 0.011), 0.006),
                           at(s, (z1, SLIDER - 0.011), 0.006),
                           at(s, (z1, SLIDER + 0.011), 0.006),
                           at(s, (z0, SLIDER + 0.011), 0.006)], "steel",
                          V((s, 0, 0)))

    group = panes.group()
    for i in range(len(STATIONS) - 1):
        if STATIONS[i][0] == 0 or STATIONS[i + 1][0] == 0:
            continue
        for j in (J_BAND, 2 * HALF - 3 - J_BAND):
            if not hole(i, j):
                continue
            rows = [section(STATIONS[k], 0.012) for k in (i, i + 1)]
            pane = [rows[0][j], rows[1][j], rows[1][j + 1], rows[0][j + 1]]
            out = (section(STATIONS[i])[j] -
                   section(STATIONS[i], T)[j])
            panes.face(pane, "glass", out, group)


def outline(h, lift):
    """The body's plan at height `h`, from the right-hand front corner
    across the front and back down the left to the right-hand back
    corner."""
    return ([on_skin(p, h, lift) for p in cap_run(1)] +
            [on_skin(p, h, lift) for p in reversed(cap_run(-1))])


def floor(skin):
    """The floor, the step well inside the door, and the plate under
    it all."""
    xr = x_low(FLOOR) - T
    loop = outline(FLOOR, -T)
    loop += [V((-xr, FLOOR, DOOR[0])), V((-WELL, FLOOR, DOOR[0])),
             V((-WELL, FLOOR, DOOR[1])), V((-xr, FLOOR, DOOR[1]))]
    skin.face(loop, "floor", UP)
    skin.face(outline(SKIRT + 0.03, -0.01), "under", -UP)

    corner = [V((-xr, 0, DOOR[0])), V((-WELL, 0, DOOR[0])),
              V((-WELL, 0, DOOR[1])), V((-xr, 0, DOOR[1]))]
    middle = V((-(xr + WELL) / 2, STEP, (DOOR[0] + DOOR[1]) / 2))
    for a, b in zip(corner, corner[1:]):
        wall = [a + UP * STEP, b + UP * STEP, b + UP * FLOOR,
                a + UP * FLOOR]
        skin.face(wall, "lining", middle - mean(wall))
    tread = [p + UP * STEP for p in corner]
    skin.face(tread, "floor", UP)
    # The step's nosing, under the door's leaves.
    lip = x_low(STEP) - 0.045
    skin.face([V((-xr, STEP, DOOR[0])), V((-lip, STEP, DOOR[0])),
               V((-lip, STEP, DOOR[1])), V((-xr, STEP, DOOR[1]))],
              "steel", UP)
    skin.face([V((-lip, STEP, DOOR[0])), V((-lip, STEP, DOOR[1])),
               V((-lip, SKIRT, DOOR[1])), V((-lip, SKIRT, DOOR[0]))],
              "under", -RIGHT)


def arch_line(centre, n=14, foot=SKIRT):
    """(z, height) round a wheel arch, from its foot ahead of the wheel
    over it to its foot behind."""
    arc = [(centre + ARCH * math.cos(math.pi * k / n),
            R + ARCH * math.sin(math.pi * k / n)) for k in range(n + 1)]
    return [(arc[0][0], foot)] + arc + [(arc[-1][0], foot)]


def housings(skin):
    """The wheel housings: an arch over each wheel and a wall inside
    it, and the same again a little larger, facing the saloon, where
    they stand up through the floor."""
    for centre in AXLES:
        for s in (-1, 1):
            arc = arch_line(centre, foot=SKIRT + 0.03)
            hub = V((s * (W + HOUSE) / 2, R, centre))
            for grow, mat, way in ((0.0, "under", 1), (0.03, "lining", -1)):
                outer = [V((s * (x_low(h) - 0.004 - T * (way < 0)),
                            R + (h - R) * (1 + grow / ARCH),
                            centre + (z - centre) * (1 + grow / ARCH)))
                         for z, h in arc]
                inner = [V((s * (HOUSE - grow), p.y, p.z)) for p in outer]
                group = skin.group()
                for k in range(len(arc) - 1):
                    q = [outer[k], outer[k + 1], inner[k + 1], inner[k]]
                    skin.face(q, mat, (hub - mean(q)) * way, group)
                skin.face(inner, mat, V((s * way, 0, 0)))


def arches(b, m):
    """A rolled lip round each wheel arch."""
    for centre in AXLES:
        for s in (-1, 1):
            path = [V((s * (x_low(h) + 0.003), h, z))
                    for z, h in arch_line(centre)]
            part(b, f"arch_lip{centre}{s}", tube(path, 0.014, ref=RIGHT,
                                                 sides=6),
                 m["paint"], levels=0)


def lamp(b, m, name, p, n, r, mat, rim="chrome"):
    """A round lamp at `p` facing `n`: a bright rim and a domed lens."""
    part(b, f"{name}_rim", loft([(p - n * 0.01, r * 1.2, r * 1.2),
                                 (p + n * 0.03, r * 1.18, r * 1.18),
                                 (p + n * 0.042, r, r)], UP, sides=16),
         m[rim], levels=0)
    part(b, f"{name}_lens", loft([(p + n * 0.036, r, r),
                                  (p + n * 0.056, r * 0.84, r * 0.84),
                                  (p + n * 0.066, r * 0.45, r * 0.45)],
                                 UP, sides=16), m[mat], levels=0)


def glazing_bars(b, m, end, limit, divider):
    """The rubber round an end's window, and the windscreen's post."""
    name = "screen" if end > 0 else "rear_window"
    run = cap_run(end, limit)
    for h in (BELT, EAVE):
        part(b, f"{name}_rubber{h}", tube([on_skin(p, h, 0.004)
                                           for p in run], 0.013, ref=UP,
                                          sides=6), m["rubber"], levels=0)
    for k, p in enumerate((run[0], run[-1])):
        part(b, f"{name}_rubber_side{k}",
             tube([on_skin(p, BELT, 0.004), on_skin(p, EAVE, 0.004)],
                  0.013, sides=6), m["rubber"], levels=0)
    if divider:
        p = face_post(end, 0.0)
        part(b, f"{name}_post", tube([on_skin(p, BELT - 0.01, 0.004),
                                      on_skin(p, EAVE + 0.01, 0.004)],
                                     0.022, sides=8), m["cream"], levels=0)


def bumper(b, m, end):
    name = "front" if end > 0 else "rear"
    part(b, f"bumper_{name}",
         loft([(on_skin(p, 0.50, 0.075), 0.075, 0.032)
               for p in cap_run(end, 0.98)], UP, sides=8),
         m["chrome"])
    for s in (-1, 1):
        p = face_post(end, s * 0.55)
        part(b, f"bumper_iron_{name}{s}",
             tube([on_skin(p, 0.50, -0.02), on_skin(p, 0.50, 0.06)], 0.025,
                  sides=6), m["black"], levels=0)


def front(coll, b, skin, m):
    # The grille: bright bars on a dark panel, in a bright frame.
    posts = cap_run(1, 0.32)
    low, high = 0.76, 1.24
    ribbon(skin, posts, [(low, 0.005), (high, 0.005)], "black")
    for k in range(7):
        h = 0.79 + 0.07 * k
        ribbon(skin, posts, [(h - 0.013, 0.007), (h, 0.019),
                             (h + 0.013, 0.007)], "chrome")
    for h in (low, high):
        part(b, f"grille_frame{h}", tube([on_skin(p, h, 0.012)
                                          for p in posts], 0.015, ref=UP,
                                         sides=6), m["chrome"], levels=0)
    for k in (0, -1):
        part(b, f"grille_side{k}", tube([on_skin(posts[k], low, 0.012),
                                         on_skin(posts[k], high, 0.012)],
                                        0.015, sides=6), m["chrome"],
             levels=0)
    part(b, "badge", blob(on_skin(face_post(1, 0.0), 1.39, 0.006),
                          (0.085, 0.032, 0.012), 10, 6), m["chrome"])

    for s, tag in ((1, "left"), (-1, "right")):
        p = face_post(1, s * 0.84)
        at, n = on_skin(p, 1.0), outward(p, 1.0)
        lamp(b, m, f"headlight{s}", at, n, 0.10, "lens")
        assembly(f"headlight_{tag}", coll, at + n * 0.07,
                 point=f"headlight_{tag}")
        p = face_post(1, s * 0.90)
        lamp(b, m, f"indicator{s}", on_skin(p, 0.80), outward(p, 0.80),
             0.04, "amber")

    bumper(b, m, 1)
    part(b, "plate_front", slab(on_skin(face_post(1, 0.0), 0.645, 0.01),
                                (0.24, 0.055, 0.005)), m["plate"],
         levels=0, bevel=0.003)

    glazing_bars(b, m, 1, SCREEN, True)
    for s in (-1, 1):
        foot = on_skin(face_post(1, s * 0.30), BELT + 0.03, 0.03)
        part(b, f"wiper{s}", tube([foot, on_skin(face_post(1, s * 0.66),
                                                 BELT + 0.52, 0.022)],
                                  0.007, sides=5), m["black"], levels=0)
        part(b, f"wiper_pivot{s}", blob(foot - FORWARD * 0.015,
                                        (0.02, 0.02, 0.02), 8, 5),
             m["chrome"])

    # The destination box in the dome, and its sign.
    box = V((0, 2.56, ZB + DF - 0.24))
    part(b, "destination_box", slab(box, (0.52, 0.11, 0.09)), m["cream"],
         levels=0, bevel=0.02)
    sign = box + FORWARD * 0.092
    part(b, "destination_sign", slab(sign, (0.46, 0.075, 0.003)),
         m["sign"], levels=0)
    assembly("destination", coll, sign + FORWARD * 0.004,
             point="destination")

    # A mirror on an arm at each corner.
    for s in (-1, 1):
        root = on_skin(post(1, s, cx=0.985), 1.82)
        tip = root + V((s * 0.27, 0.05, 0.10))
        part(b, f"mirror_arm{s}", tube([root, tip], 0.011, sides=6),
             m["steel"], levels=0)
        part(b, f"mirror{s}", slab(tip + V((s * 0.02, 0, 0)),
                                   (0.065, 0.11, 0.014)), m["mirror"],
             levels=0, bevel=0.008)
        part(b, f"mirror_back{s}", slab(tip + V((s * 0.02, 0, 0.016)),
                                        (0.07, 0.115, 0.008)), m["black"],
             levels=0, bevel=0.006)


def back(coll, b, m):
    for s, tag in ((1, "left"), (-1, "right")):
        p = face_post(-1, s * 0.92)
        at, n = on_skin(p, 1.02), outward(p, 1.02)
        lamp(b, m, f"tail_light{s}", at, n, 0.06, "tail_light")
        assembly(f"tail_light_{tag}", coll, at + n * 0.07,
                 point=f"tail_light_{tag}")
        lamp(b, m, f"indicator_rear{s}", on_skin(p, 1.19), outward(p, 1.19),
             0.045, "amber")
    bumper(b, m, -1)
    part(b, "plate_rear", slab(on_skin(face_post(-1, 0.0), 0.86, 0.01),
                               (0.24, 0.055, 0.005)), m["plate"],
         levels=0, bevel=0.003)
    glazing_bars(b, m, -1, REAR_WINDOW, False)
    # The engine hatch's handle, and the exhaust under the left corner.
    part(b, "hatch_handle", tube([on_skin(face_post(-1, -0.09), 1.20, 0.03),
                                  on_skin(face_post(-1, 0.09), 1.20, 0.03)],
                                 0.012, sides=6), m["chrome"], levels=0)
    part(b, "exhaust", tube([V((0.62, 0.31, ZA + 0.4)),
                            V((0.62, 0.30, ZA - DR - 0.03))],
                            0.032, sides=8), m["steel"], levels=0)
    for s in (-1, 1):
        part(b, f"mudflap{s}", slab(V((s * TRACK_R, 0.27,
                                       AXLE_R - ARCH - 0.05)),
                                    (0.27, 0.16, 0.008)), m["rubber"],
             levels=0)

    # The ladder up to the rack, right of the middle.
    rails = []
    for x in (-0.40, -0.74):
        p = face_post(-1, x)
        rail = [on_skin(p, h, 0.07)
                for h in (0.74, 0.98, 1.22, BELT, 1.67, 1.89, 2.11, EAVE)]
        for a in (math.radians(20), math.radians(40)):
            rail.append(dome(-1, -1, -x / (WE * math.cos(a) ** 0.8), a,
                             0.07))
        rail.append(V((x, roof_y(x) + 0.07, ZA + 0.12)))
        rails.append(rail)
        part(b, f"ladder_rail{x}", tube(rail, 0.014, ref=RIGHT, sides=6),
             m["steel"], levels=0)
        for k in (0, 9):
            foot = rail[k] - (UP * 0.07 if k else
                              outward(p, 0.74) * 0.07)
            part(b, f"ladder_foot{x}{k}", tube([rail[k], foot], 0.012,
                                               sides=6), m["steel"],
                 levels=0)
    for k in range(10):
        part(b, f"rung{k}", tube([rails[0][k], rails[1][k]], 0.011,
                                 sides=6), m["steel"], levels=0)


def roof_fittings(b, m):
    """Two vents over the saloon, and the rack on the back of the roof
    with what is riding on it."""
    for z in (ZB - 1.5, ZB - 3.6):
        part(b, f"vent{z}", slab(V((0, CROWN + 0.012, z)),
                                 (0.30, 0.03, 0.36)), m["cream"],
             levels=0, bevel=0.012)

    z0, z1, x = ZA + 0.35, ZA + 4.2, 0.76
    high = 0.14

    def hoop(z, lift):
        return [V((u, roof_y(u) + lift, z))
                for u in (-x, -0.5, -0.25, 0.0, 0.25, 0.5, x)]

    bars = []
    for s in (-1, 1):
        bars.append(tube([V((s * x, roof_y(x) + high, z0)),
                          V((s * x, roof_y(x) + high, z1))], 0.014,
                         sides=6))
        for k in range(5):
            z = z0 + (z1 - z0) * k / 4
            bars.append(tube([V((s * x, roof_y(x) - 0.01, z)),
                              V((s * x, roof_y(x) + high, z))], 0.010,
                             sides=6))
    for z in (z0, z1):
        bars.append(tube(hoop(z, high), 0.014, ref=FORWARD, sides=6))
    for k in range(6):
        z = z0 + 0.15 + (z1 - z0 - 0.3) * k / 5
        bars.append(tube(hoop(z, 0.03), 0.012, ref=FORWARD, sides=6))
    part(b, "rack", merge(*bars), m["steel"], levels=0)

    deck = CROWN + 0.045
    part(b, "suitcase", slab(V((-0.18, deck + 0.11, z1 - 0.95)),
                             (0.36, 0.11, 0.24)), m["leather"], levels=0,
         bevel=0.02)
    part(b, "suitcase_small", slab(V((0.22, deck + 0.08, z1 - 1.8)),
                                   (0.20, 0.08, 0.30)), m["seat"],
         levels=0, bevel=0.02)
    part(b, "kitbag", loft([(V((-0.45, deck + 0.13, z0 + 0.9)), 0.06, 0.06),
                            (V((-0.36, deck + 0.14, z0 + 0.9)), 0.14, 0.14),
                            (V((0.30, deck + 0.14, z0 + 0.9)), 0.15, 0.15),
                            (V((0.42, deck + 0.13, z0 + 0.9)), 0.07, 0.07)],
                           UP, sides=8), m["canvas"])
    part(b, "suitcase_handle", tube([V((-0.26, deck + 0.11, z1 - 0.70)),
                                     V((-0.24, deck + 0.11, z1 - 0.67)),
                                     V((-0.12, deck + 0.11, z1 - 0.67)),
                                     V((-0.10, deck + 0.11, z1 - 0.70))],
                                    0.009, ref=UP, sides=5), m["black"],
         levels=0)


def seat(x, z, half):
    """A bench's cushion, back, and leg, `half` its half width."""
    back = (UP - FORWARD * 0.12).normalized()
    out = RIGHT.cross(back)
    return (slab(V((x, CUSHION - 0.05, z)), (half, 0.05, 0.21)),
            slab(V((x, CUSHION + 0.27, z - 0.235)), (half, 0.30, 0.035),
                 axes=(RIGHT, back, out)),
            slab(V((x, FLOOR + 0.18, z - 0.02)), (half - 0.12, 0.18, 0.02)))


def cabin(coll, b, m):
    """The seats, the driver's place, and where everyone sits."""
    rows = [(s, -3.83 + 0.8 * k) for s in (-1, 1) for k in range(8)]
    rows += [(1, 2.57), (1, 3.37), (-1, AXLE_F - 0.2)]
    cushions, backs, legs, rails, places = [], [], [], [], []
    for s, z in rows:
        c, k, leg = seat(s * 0.70, z, 0.43)
        cushions.append(c)
        backs.append(k)
        legs.append(leg)
        rails.append(tube([V((s * 0.29, CUSHION + 0.60, z - 0.275)),
                           V((s * 1.11, CUSHION + 0.60, z - 0.275))],
                          0.012, sides=6))
        places += [V((s * 0.91, CUSHION + 0.02, z - 0.02)),
                   V((s * 0.49, CUSHION + 0.02, z - 0.02))]
    c, k, leg = seat(0.0, ZA + 0.40, 1.12)
    cushions.append(c)
    backs.append(k)
    legs.append(leg)
    places += [V((x, CUSHION + 0.02, ZA + 0.38))
               for x in (-0.86, -0.30, 0.30, 0.86)]

    # The driver's seat, on its sprung pedestal over the wheel.
    top = STEERING_HUB.y - 0.33
    back = (UP - FORWARD * 0.10).normalized()
    cushions.append(slab(V((SEAT, top - 0.05, SEAT_Z)), (0.23, 0.05, 0.22)))
    backs.append(slab(V((SEAT, top + 0.30, SEAT_Z - 0.245)),
                      (0.23, 0.31, 0.035),
                      axes=(RIGHT, back, RIGHT.cross(back))))
    legs.append(slab(V((SEAT, FLOOR + 0.2, SEAT_Z)), (0.09, 0.22, 0.09)))
    part(b, "cushions", merge(*cushions), m["seat"], bevel=0.03)
    part(b, "seat_backs", merge(*backs), m["seat"], bevel=0.025)
    part(b, "seat_legs", merge(*legs), m["black"], levels=0)
    part(b, "seat_rails", merge(*rails), m["chrome"], levels=0)

    places.sort(key=lambda p: (-p.z, p.x))
    for k, p in enumerate(places):
        assembly(f"seat_{k + 1:02d}", coll, p, point=f"seat_{k + 1:02d}")
    assembly("seat_driver", coll, V((SEAT, top + 0.02, SEAT_Z - 0.01)),
             point="seat_driver")

    # The dashboard under the windscreen, the engine's cover beside the
    # driver, and a screen behind them.
    part(b, "dash", slab(V((0.20, 1.33, ZB + 0.31)), (0.74, 0.10, 0.09)),
         m["dash"], bevel=0.03)
    part(b, "binnacle", slab(V((SEAT, 1.46, ZB + 0.27)), (0.17, 0.06, 0.05),
                             axes=(RIGHT, (UP - FORWARD * 0.5).normalized(),
                                   (FORWARD + UP * 0.5).normalized())),
         m["dash"], bevel=0.02)
    part(b, "engine_cover", slab(V((-0.12, FLOOR + 0.24, ZB - 0.03)),
                                 (0.36, 0.25, 0.40)), m["dash"],
         bevel=0.06)
    part(b, "gear_lever", tube([V((0.22, FLOOR + 0.40, ZB - 0.20)),
                                V((0.30, FLOOR + 0.78, ZB - 0.33))], 0.009,
                               sides=6), m["chrome"], levels=0)
    part(b, "gear_knob", blob(V((0.30, FLOOR + 0.80, ZB - 0.33)),
                              (0.028, 0.028, 0.028), 8, 5), m["black"])
    behind = SEAT_Z - 0.46
    part(b, "screen", slab(V((0.75, FLOOR + 0.52, behind)),
                           (0.45, 0.52, 0.012)), m["lining"], levels=0,
         bevel=0.006)
    poles = [tube([V((0.30, FLOOR, behind)),
                   V((0.30, roof_y(0.30) - T - 0.01, behind))], 0.014,
                  sides=6)]
    for z in DOOR:
        poles.append(tube([V((-WELL, FLOOR, z)),
                           V((-WELL, roof_y(WELL) - T - 0.01, z))], 0.014,
                          sides=6))
    part(b, "poles", merge(*poles), m["chrome"], levels=0)

    lean = ((UP - FORWARD * 0.6).normalized(),
            (FORWARD + UP * 0.6).normalized())
    pedals = []
    for s, tag in ((1, "left"), (-1, "right")):
        at = V((SEAT + 0.10 * s, FLOOR + 0.08, SEAT_Z + 0.64))
        assembly(f"pedal_{tag}", coll, at, point=f"pedal_{tag}")
        pedals.append(slab(at + V((0, 0.0, 0.03)), (0.04, 0.05, 0.008),
                           axes=(RIGHT,) + lean))
    part(b, "pedals", merge(*pedals), m["rubber"], levels=0, bevel=0.003)

    middle = (DOOR[0] + DOOR[1]) / 2
    assembly("door", coll, V((-(W + 0.55), 0, middle)), point="door")
    assembly("step", coll, V((-(W - T + WELL) / 2, STEP, middle)),
             point="step")
    assembly("aisle", coll, V((-0.30, FLOOR, middle)), point="aisle")


def doors(coll, m):
    """The door's two leaves, each about its own jamb."""
    gap = 0.006
    middle = (DOOR[0] + DOOR[1]) / 2
    leaf(coll, m, "door_fore", middle + gap / 2, DOOR[1] - gap,
         DOOR[1] - gap)
    leaf(coll, m, "door_aft", DOOR[0] + gap, middle - gap / 2,
         DOOR[0] + gap)


def leaf(coll, m, name, z0, z1, hinge):
    origin = V((-(W - 0.015), 0, hinge))
    asm = assembly(name, coll, origin, mesh=name, point=f"{name}_hinge")
    skin = Skin(m)
    thick = 0.03
    hs = [SKIRT + 0.03, 0.62, 0.85, 1.08, STRIPE, BELT, BELT + 0.06,
          EAVE - 0.075, EAVE - 0.02]
    zs = [z0, z0 + 0.055, z1 - 0.055, z1]
    outer = [[on_skin(post(0, -1, z), h) for h in hs] for z in zs]
    inner = [[on_skin(post(0, -1, z), h, -thick) for h in hs] for z in zs]

    def paint(i, j):
        return "paint" if j < 4 else "navy" if j == 4 else "cream"

    double(skin, outer, inner, paint, lambda i, j: "lining",
           lambda i, j: i == 1 and j == 6, lambda i, j: "rubber",
           border=(True, True))
    for inset, way in ((0.013, -1), (0.017, 1)):
        skin.face([on_skin(post(0, -1, z), h, -inset)
                   for z, h in ((zs[1], hs[6]), (zs[2], hs[6]),
                                (zs[2], hs[7]), (zs[1], hs[7]))],
                  "glass", V((way, 0, 0)))
    for h in (STRIPE, BELT - 0.035):
        ribbon(skin, [post(0, -1, z0), post(0, -1, z1)], bright(h),
               "chrome")
    skin.build(asm, f"{name}_leaf", origin)

    # A push bar across the glass inside, and a kick plate outside.
    bar = [on_skin(post(0, -1, z), 1.78, -thick - 0.03) - origin
           for z in (zs[1], zs[2])]
    ends = [on_skin(post(0, -1, z), 1.78, -thick) - origin
            for z in (zs[1], zs[2])]
    part(asm, f"{name}_bar", tube([ends[0], bar[0], bar[1], ends[1]],
                                  0.011, ref=UP, sides=6), m["chrome"],
         levels=0)
    plate = [on_skin(post(0, -1, z), h, 0.003) - origin
             for z, h in ((zs[1], 0.47), (zs[2], 0.47), (zs[2], 0.66),
                          (zs[1], 0.66))]
    cage = Cage()
    cage.verts = plate
    cage.faces = [[0, 1, 2, 3]]
    cage.orient(lambda p: p + RIGHT)
    part(asm, f"{name}_kick", cage, m["steel"], levels=0, smooth=False)


def tyre(asm, m, name, x):
    part(asm, name, torus(V((x, 0, 0)), RIGHT, UP, R - 0.11, (0.11, TYRE),
                          32, 10), m["tire"])


def nuts(asm, m, radius, x, count=8):
    studs = []
    for k in range(count):
        a = 2 * math.pi * (k + 0.5) / count
        at = V((x, radius * math.cos(a), radius * math.sin(a)))
        studs.append(loft([(at + RIGHT * 0.012, 0.016, 0.016),
                           (at - RIGHT * 0.018, 0.013, 0.013)],
                          V((0, math.cos(a), math.sin(a))), sides=6))
    part(asm, "nuts", merge(*studs), m["steel"], levels=0)


def copy_wheel(coll, first, name, at, point):
    """A wheel that only displays `first`, turned about on the left."""
    other = assembly(name, coll, at, point=point)
    if at.x > 0:
        other.rotation_euler = (0, 0, math.pi)
    for child in first.children:
        dup = child.copy()
        coll.objects.link(dup)
        dup.parent = other


def wheels(coll, m):
    """The right-hand front wheel and the right-hand rear pair export,
    about x with their faces toward -x; the left-hand ones only display
    them."""
    front_wheel = assembly("wheel", coll, V((-TRACK_F, R, AXLE_F)),
                           mesh="wheel", point="wheel_fr")
    tyre(front_wheel, m, "tyre", 0.0)
    part(front_wheel, "disc", lathe([
        (0, 0.08), (0.28, 0.08), (0.29, 0.105), (0.29, -0.105),
        (0.265, -0.11), (0.24, -0.085), (0.20, -0.07), (0.165, -0.085),
        (0.15, -0.14), (0, -0.14)]), m["cream"], levels=0)
    part(front_wheel, "cap", lathe([
        (0, -0.135), (0.125, -0.14), (0.115, -0.175), (0.07, -0.195),
        (0, -0.20)]), m["chrome"], levels=0)
    nuts(front_wheel, m, 0.185, -0.078)
    copy_wheel(coll, front_wheel, "wheel_fl", V((TRACK_F, R, AXLE_F)),
               "wheel_fl")

    rear = assembly("wheel_rear", coll, V((-TRACK_R, R, AXLE_R)),
                    mesh="wheel_rear", point="wheel_rr")
    tyre(rear, m, "tyre_outer", -PAIR)
    tyre(rear, m, "tyre_inner", PAIR)
    part(rear, "disc", lathe([
        (0, 0.24), (0.28, 0.24), (0.29, 0.255), (0.29, -0.255),
        (0.265, -0.26), (0.235, -0.20), (0.20, -0.07), (0.135, -0.05),
        (0.12, -0.12), (0, -0.12)]), m["cream"], levels=0)
    part(rear, "cap", lathe([
        (0, -0.115), (0.10, -0.12), (0.085, -0.15), (0, -0.155)]),
        m["chrome"], levels=0)
    nuts(rear, m, 0.165, -0.052)
    copy_wheel(coll, rear, "wheel_rl", V((TRACK_R, R, AXLE_R)), "wheel_rl")


def steering(coll, m):
    """A big thin-rimmed wheel lying nearly flat, which turns about its
    column."""
    hub = STEERING_HUB
    w = assembly("steering_wheel", coll, hub, mesh="steering_wheel",
                 point="steering_hub")
    radius = STEERING_RADIUS
    part(w, "rim", torus(O, COLUMN, RIGHT, radius, (0.014, 0.014), 32, 8),
         m["black"])
    down = RIGHT.cross(COLUMN).normalized()
    spokes = []
    for a in (0, 120, 240):
        t = math.radians(a + 90)
        d = RIGHT * math.cos(t) + down * math.sin(t)
        spokes.append(tube([COLUMN * 0.05 + d * 0.04, d * (radius - 0.006)],
                           0.009, sides=6))
    part(w, "spokes", merge(*spokes), m["chrome"], levels=0)
    part(w, "boss", loft([(COLUMN * 0.075, 0.05, 0.05),
                          (COLUMN * 0.03, 0.05, 0.05),
                          (COLUMN * 0.015, 0.03, 0.03)], RIGHT, sides=12),
         m["black"], levels=0)

    body = bpy.data.objects["body"]
    part(body, "steering_shaft", tube([hub + COLUMN * 0.07,
                                       hub + COLUMN * 0.92], 0.022,
                                      sides=8), m["black"], levels=0)
    assembly("steering_column", coll, hub + COLUMN, point="steering_column")
    assembly("grip_left", coll, hub + RIGHT * radius, point="grip_left")
    assembly("grip_right", coll, hub - RIGHT * radius, point="grip_right")


if __name__ == "__main__":
    build()
