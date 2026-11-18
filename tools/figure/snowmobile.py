"""Build models/snowmobile.blend: Ripa 440, a two-up Swedish trail sled.

Life size, +z forward, +y up, +x rider's left. The origin is on snow.
The skis turn on their kingpins and the bars on the column. One cleat is
exported at its own origin; the other 39 are linked preview copies. Run
the exported cleat around belt_frame(s), with s in metres of travel.
"""

import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(__file__))
from shapes import (Cage, FORWARD, RIGHT, UP, V, assembly, blender,  # noqa: E402
                    loft, material, merge, part, slab, tube)

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '../..'))
bpy.ops.wm.read_factory_settings(use_empty=True)
coll = bpy.data.collections.new('Ripa 440')
bpy.context.scene.collection.children.link(coll)

amber = material('Hjortron enamel', (.96, .425, .095), coat=.5, roughness=.29)
ivory = material('Birch white', (.99, .90, .68), coat=.4)
navy = material('Midnight blue', (.085, .17, .225), coat=.42, roughness=.34)
blue = material('Fjord blue', (.15, .43, .51), coat=.4)
silver = material('Brushed aluminium', (.70, .73, .73), metallic=.68)
chrome = material('Bright steel', (.83, .84, .81), metallic=.83, roughness=.26)
rubber = material('Rubber', (.115, .135, .14), roughness=.91)
vinyl = material('Ink blue vinyl', (.12, .18, .205), sheen=.2, roughness=.5)
rib = material('Seat pleats', (.17, .23, .245), sheen=.18)
lens = material('Headlamp glass', (1, .91, .71), emission=.75, coat=.9)
red = material('Cherry lens', (.75, .065, .05), coat=.75, emission=.15)
glass = material('Smoked acrylic', (.29, .49, .55), transmission=.68,
                 roughness=.14, coat=.4, tint=(.71, .86, .88))
glass.node_tree.nodes['Principled BSDF'].inputs['Alpha'].default_value = .30


def group(name, p=(0, 0, 0), point=None):
    return assembly(name, coll, V(p), mesh=name, point=point)


def mark(name, p):
    assembly(name, coll, V(p), point=name)


def solid(g, name, cage, mat, bevel=0):
    return part(g, name, cage, mat, levels=0, smooth=False, bevel=bevel)


def box(g, name, c, half, mat, bevel=0):
    return solid(g, name, slab(V(c), half), mat, bevel)


def rail(g, name, pts, r, mat, ref=RIGHT):
    return part(g, name, tube([V(p) for p in pts], r, ref=ref, sides=8),
                mat, levels=0)


def shell(stations):
    """Pressed cross sections: z, lower and upper edges, half beam."""
    cage = Cage()
    for z, low, high, w in stations:
        d = high - low
        for x, h in ((-.83, 0), (.83, 0), (1, .14), (1, .74),
                     (.84, .96), (.58, 1), (-.58, 1), (-.84, .96),
                     (-1, .74), (-1, .14)):
            cage.verts.append(V((x * w, low + h * d, z)))
    n = 10
    for a in range(0, len(cage.verts) - n, n):
        for j in range(n):
            k = (j + 1) % n
            cage.faces.append([a + j, a + k, a + n + k, a + n + j])
    cage.faces += [list(reversed(range(n))),
                   list(range(len(cage.verts) - n, len(cage.verts)))]
    return cage


body = group('body')
hood = [(.06, .39, .67, .264), (.19, .38, .82, .375),
        (.33, .37, .88, .395), (.55, .36, .85, .395),
        (.89, .35, .76, .373), (1.18, .38, .665, .321),
        (1.305, .43, .60, .254), (1.33, .47, .565, .21)]
solid(body, 'Pressed engine bonnet', shell(hood), amber, .009)
solid(body, 'Deep belly pan', shell([(.01, .28, .46, .26),
      (.3, .19, .46, .38), (.9, .22, .45, .372), (1.20, .30, .47, .316),
      (1.33, .425, .51, .245)]), navy, .012)
solid(body, 'Track tunnel', shell([(-1.36, .39, .47, .278),
      (-1.22, .355, .48, .285), (.35, .35, .50, .285)]), silver, .008)


def at_z(z):
    for a, b in zip(hood, hood[1:]):
        if z <= b[0]:
            t = (z - a[0]) / (b[0] - a[0])
            return [p + (q - p) * t for p, q in zip(a[1:], b[1:])]


def flank(z, h, side, offset=.0025):
    low, high, w = at_z(z)
    return V((side * (w + offset), low + h * (high - low), z))


def stripe(side, mat, h0, h1):
    c = Cage()
    for z in [.19, .33, .55, .89, 1.18, 1.285]:
        for h in (h0, h1):
            c.verts.append(flank(z, h, side))
    c.faces = [[i, i + 1, i + 3, i + 2] for i in range(0, len(c.verts) - 2, 2)]
    c.orient(lambda p: V((0, p.y, p.z)))
    solid(body, 'Bonnet coachline', c, mat)


def badge(side):
    # Bend the actual letter outlines to the stamped side, so the badge
    # survives the texture-free mesh exporter and reads on both sides.
    font = bpy.data.curves.new('Badge lettering', 'FONT')
    font.font = bpy.data.fonts.load(os.path.join(ROOT, 'fonts',
                                    'Geomini-SemiBold.ttf'), check_existing=True)
    font.body = 'RIPA 440'
    font.align_x = 'CENTER'
    font.size = .082
    font.space_character = 1.08
    font.resolution_u = 3
    o = bpy.data.objects.new('Badge pattern', font)
    coll.objects.link(o)
    deps = bpy.context.evaluated_depsgraph_get()
    mesh = bpy.data.meshes.new_from_object(o.evaluated_get(deps))
    c = Cage()
    for v in mesh.vertices:
        z = .57 - side * v.co.x
        p = flank(z, .414, side, .0045)
        p.y += v.co.y
        c.verts.append(p)
    c.faces = [list(f.vertices) for f in mesh.polygons]
    c.orient(lambda p: V((0, p.y, p.z)))
    solid(body, 'Ripa 440 bonnet script', c, ivory)
    bpy.data.objects.remove(o, do_unlink=True)
    bpy.data.meshes.remove(mesh)
    bpy.data.curves.remove(font)


for side in (-1, 1):
    for mat, h0, h1 in ((ivory, .27, .286), (blue, .286, .337),
                        (navy, .343, .66), (ivory, .66, .69)):
        stripe(side, mat, h0, h1)
    badge(side)
    for i in range(5):
        z = .91 + .043 * i
        p = flank(z, .51, side, .007)
        rail(body, 'Cooling louvre', [p + V((0, -.031, .012)),
             p + V((0, .033, -.012))], .009, rubber)

# Pressed intake slots with a bright leading lip. The centre carries a
# maker's lozenge with a white ptarmigan in flight (ripa in Swedish).
for s in (-1, 1):
    for j in range(5):
        z = .62 + j * .044
        h = at_z(z)[1]
        x = s * .132
        box(body, 'Intake slot', (x, h + .005, z),
            (.056, .006, .012), navy, .005)
        rail(body, 'Stamped lip', [(x - .057, h + .006, z + .012),
             (x + .057, h + .006, z + .012)], .003, silver, UP)
for mat, outline, lift in [
        (navy, [(0, .795), (-.083, .936), (0, 1.126), (.083, .936)], .003),
        (ivory, [(-.059, .915), (-.014, .944), (-.006, .984), (.004, .990),
                 (.013, .979), (.019, .951), (.061, .926), (.025, .928),
                 (.043, .907), (.009, .924), (-.009, .897), (-.011, .926)], .006)]:
    c = Cage()
    c.verts = [V((x, at_z(z)[1] + lift, z)) for x, z in outline]
    c.faces = [list(range(len(outline)))]
    c.orient(lambda p: p - UP)
    solid(body, 'Ptarmigan medallion', c, mat)

# A single inset lamp across the brow, with vertical flutes in its lens.
box(body, 'Headlamp binnacle', (0, .624, 1.221), (.195, .083, .076), navy, .025)
box(body, 'Bright bezel', (0, .623, 1.292), (.171, .062, .012), chrome, .009)
box(body, 'Fluted headlamp', (0, .624, 1.305), (.16, .052, .008), lens, .008)
for i in range(-7, 8):
    box(body, 'Lens prism', (i * .021, .624, 1.314), (.0016, .045, .0018), ivory)
rail(body, 'Wraparound brush bow', [(-.37, .39, .88), (-.34, .383, 1.19),
     (-.258, .418, 1.36), (-.18, .425, 1.395), (.18, .425, 1.395),
     (.258, .418, 1.36), (.34, .383, 1.19), (.37, .39, .88)], .024, chrome, UP)

# Two-up vinyl bench, raised at the pillion. Pleats follow the crown.
seat = [(-.979, .49, .64, .148), (-.916, .481, .754, .207),
        (-.79, .48, .77, .213), (-.50, .48, .755, .207), (-.37, .48, .696, .197),
        (-.18, .48, .695, .18), (.04, .48, .716, .156), (.16, .50, .734, .118)]
solid(body, 'Saddle', shell(seat), vinyl, .021)
for z in (-.85, -.79, -.73, -.67, -.61, -.55, -.29, -.23, -.17, -.11, -.05, .01):
    a, b = next((a, b) for a, b in zip(seat, seat[1:]) if a[0] <= z <= b[0])
    t = (z - a[0]) / (b[0] - a[0])
    _, lo, hi, w = [p + t * (q - p) for p, q in zip(a, b)]
    rail(body, 'Heat-welded pleat', [(s * w, hi - h * (hi - lo), z) for s, h in
         [(-.93, .16), (-.84, .04), (-.58, 0), (.58, 0), (.84, .04), (.93, .16)]],
         .003, rib, FORWARD)

# A birch-slat luggage deck with grab rails above the tail lamp.
for i in range(-2, 3):
    box(body, 'Ash luggage slat', (i * .093, .496, -1.17),
        (.039, .014, .185), ivory, .009)
box(body, 'Tail lamp plinth', (0, .435, -1.361), (.119, .039, .018), rubber, .008)
box(body, 'Tail lamp', (0, .438, -1.381), (.106, .028, .012), red, .007)
box(body, 'Flexible snow flap', (0, .304, -1.355), (.25, .10, .01), rubber, .008)
for s in (1, -1):
    rail(body, 'Cargo and pillion rail', [(s * .244, .47, -1.32),
         (s * .25, .55, -1.34), (s * .25, .587, -1.29), (s * .25, .605, -.99),
         (s * .242, .648, -.89), (s * .235, .654, -.61), (s * .224, .51, -.53)],
         .013, chrome)
    box(body, 'Running board', (s * .332, .353, -.42),
        (.082, .015, .84), silver, .012)
    for i in range(16):
        box(body, 'Pressed foot grip', (s * .344, .371, -1.19 + i * .095),
            (.056, .006, .009), rubber, .003)

# Bowed, swept-back windscreen with a narrow polished edge.
wind = group('glass')
c = Cage()
rim = []
base = []
for j in range(17):
    x = (j - 8) / 8
    z = .47 - .234 * x * x
    lo, hi, w = at_z(z)
    edge = max(0, (abs(.31 * x) / w - .58) / .26)
    p = V((.31 * x, hi - .04 * edge * (hi - lo) + .006, z))
    q = V((.338 * x, 1.172 - .18 * x**4, .312 - .196 * x * x))
    base.append(p)
    rim.append(q)
    c.verts.extend([p, q, p + FORWARD * .004, q + FORWARD * .004])
for i in range(0, 64, 4):
    c.faces.extend([[i, i + 1, i + 5, i + 4], [i + 6, i + 7, i + 3, i + 2],
                    [i + 1, i + 3, i + 7, i + 5], [i + 4, i + 6, i + 2, i]])
c.faces.extend([[0, 2, 3, 1], [64, 65, 67, 66]])
solid(wind, 'One-piece windscreen', c, glass)
rail(body, 'Screen bead', [base[0]] + rim + [base[-1]], .007, chrome, FORWARD)
rail(body, 'Screen gasket', base, .012, navy, UP)

# The speedometer tilts back towards the rider, below the screen.
centre = V((0, .85, .198))
normal = V((0, .72, -.69)).normalized()
vertical = RIGHT.cross(normal)
solid(body, 'Instrument pod', loft([(centre - normal * .045, .084, .075),
      (centre, .074, .067)], RIGHT, sides=24), navy)
solid(body, 'Speedometer', loft([(centre, .056, .056),
      (centre + normal * .004, .056, .056)], RIGHT, sides=24), ivory)
base = centre + normal * .006
for j in range(11):
    a = math.radians(-122 + 24.4 * j)
    ray = vertical * math.cos(a) + RIGHT * math.sin(a)
    rail(
        body, 'Scale division', [
            base + ray * .042, base + ray * .05], .0017, navy, normal)
rail(body, 'Needle', [base + normal * .002, base +
     normal * .002 - .035 * RIGHT], .0025, red)

column = V((0, .765, .24))
bars = group('bars', column, 'column')
top = V((0, 1.005, .143))
mark('column_top', top)
rail(bars, 'Raked steering post', [(0, 0, 0), top - column], .024, navy)
line = [(-.327, 1.035, .01), (-.222, 1.031, .045), (-.145, 1.005, .125),
        (-.08, 1.005, .143), (.08, 1.005, .143), (.145, 1.005, .125),
        (.222, 1.031, .045), (.327, 1.035, .01)]
rail(bars, 'Handlebar', [V(p) - column for p in line], .014, chrome, UP)
box(bars, 'Clamp pad', top - column, (.059, .03, .029), vinyl, .008)
for s, side in [(1, 'left'), (-1, 'right')]:
    grip = V((s * .287, 1.034, .024))
    rail(bars, 'Twist grip', [V((s * .234, 1.031, .041)) - column,
                              V((s * .338, 1.035, .006)) - column], .022, rubber, UP)
    mark('grip_' + side, grip)
    mark('foot_' + side, (s * .338, .377, -.008))
    mark('passenger_foot_' + side, (s * .338, .377, -.576))
    mark('passenger_grip_' + side, (s * .235, .654, -.72))
rail(bars, 'Brake lever', [V(p) - column for p in [(.22, 1.033, .06),
     (.226, 1.026, .093), (.309, 1.021, .081)]], .007, chrome, UP)
mark('seat', (0, .695, -.198))
mark('passenger_seat', (0, .763, -.681))

# Pressed steel runners with curled tips and slender leaf springs.
for s, side in [(1, 'left'), (-1, 'right')]:
    pivot = V((s * .48, .197, .90))
    ski = group('ski_' + side, pivot, 'ski_pivot_' + side)
    stations = [(-.55, .018, .045, .048), (-.49, .006, .034, .082),
                (-.25, 0, .031, .086), (.29, 0, .031, .085),
                (.49, .026, .058, .083), (.65, .092, .125, .07),
                (.74, .19, .217, .051), (.775, .246, .264, .028)]
    solid(ski, 'Rolled ski', shell([(z, a - pivot.y, b - pivot.y, w)
          for z, a, b, w in stations]), navy, .005)
    rail(ski, 'Tip bow', [(0, -.164, .28), (0, -.109, .39), (0, .06, .57),
         (0, .104, .662), (0, .09, .748), (0, .059, .771)], .014, amber)
    rail(ski, 'Spring leaf', [(0, -.15, -.35), (0, -.096, -.26), (0, -.045, -.09),
         (0, -.044, .09), (0, -.093, .24), (0, -.146, .32)], .019, silver)
    box(ski, 'Ski saddle', (0, -.066, 0), (.04, .062, .067), navy, .008)
    rail(ski, 'Kingpin', [(0, -.081, 0), (0, .045, 0)], .026, chrome)
    rail(body, 'Spindle housing', [
         pivot, pivot + V((-s * .028, .135, -.018))], .038, navy)
    for z in (.58, 1.065):
        rail(
            body, 'Wishbone', [
                (s * .285, .29, z), pivot + UP * .059], .019, silver)
    rail(
        body, 'Front damper', [
            pivot + UP * .051, (s * .32, .446, .79)], .027, amber)

# The belt is a hollow racetrack around two drums, not a solid block.
rear, front, height, radius = -1.03, .27, .17, .16
span = front - rear
length = 2 * (span + math.pi * radius)


def belt_frame(s):
    """Centre and pitch about +x; local +y faces into belt at bottom."""
    s %= length
    if s < span:
        return V((0, height - radius, rear + s)), 0
    s -= span
    if s < math.pi * radius:
        a = s / radius
        return V((0, height - radius * math.cos(a),
                 front + radius * math.sin(a))), -a
    s -= math.pi * radius
    if s < span:
        return V((0, height + radius, front - s)), -math.pi
    a = math.pi + (s - span) / radius
    return V((0, height - radius * math.cos(a),
             rear + radius * math.sin(a))), -a


track = group('track')
c = Cage()
for i in range(80):
    p, a = belt_frame(i * length / 80)
    inward = V((0, math.cos(a), math.sin(a)))
    for x, d in ((-.236, 0), (.236, 0), (.236, .02), (-.236, .02)):
        c.verts.append(p + x * RIGHT + d * inward)
for i in range(80):
    for j in range(4):
        k = (j + 1) % 4
        n = (i + 1) % 80 * 4
        c.faces.append([i * 4 + j, i * 4 + k, n + k, n + j])
solid(track, 'Endless rubber belt', c, rubber)

for z, r in [(rear, .136), (-.715, .101), (-.398, .101),
             (-.081, .101), (front, .136)]:
    y = .030 + r
    for s in (-1, 1):
        p = V((s * .178, y, z))
        solid(track, 'Bogie wheel', loft([(p - s * .022 * RIGHT, r, r),
              (p + s * .022 * RIGHT, r, r)], UP, sides=20), navy)
        solid(track, 'Wheel boss', loft([(p + s * .023 * RIGHT, .047, .047),
              (p + s * .031 * RIGHT, .037, .037)], UP, sides=10), silver)
for s in (-1, 1):
    rail(track, 'Skid rail', [(s * .194, .14, rear), (s * .194, .106, -.78),
         (s * .194, .106, .11), (s * .194, .165, front)], .019, silver)
    for z in (-.79, -.12):
        rail(track, 'Bogie arm', [(s * .194, .112, z), (s * .17, .25, z + .18),
             (s * .25, .25, z + .18), (s * .259, .38, z + .18)], .018, navy)

origin, _ = belt_frame(0)
cleat = group('cleat', origin, 'cleat_origin')
lug = solid(cleat, 'Split-chevron grouser', merge(*[
    slab(V((s * .122, -.005, 0)), (.118, .005, .013),
         (V((s, .0, .14)).normalized(), UP, V((-.14 * s, 0, 1)).normalized()))
    for s in (-1, 1)]), rubber)
for i in range(1, 40):
    p, a = belt_frame(i * length / 40)
    o = lug.copy()
    o.parent = None
    coll.objects.link(o)
    o.location = blender(p)
    o.rotation_euler.x = a

bpy.context.preferences.filepaths.save_version = 0
out = os.path.join(ROOT, 'models', 'snowmobile.blend')
bpy.ops.wm.save_as_mainfile(filepath=out, compress=True)
