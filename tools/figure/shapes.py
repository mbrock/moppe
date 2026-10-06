"""Shape helpers shared by the model scripts in tools/figure.

Shapes are built in the game's frame (x right, y up, z forward, metres) as
coarse cages -- lofts of rings swept along a path, ellipsoids, boxes --
for a Subdivision Surface modifier to smooth, and converted to Blender's
frame (z up, facing -y) as they become meshes.
"""

import math

import bpy
from mathutils import Vector

V = Vector
RIGHT = V((1, 0, 0))
UP = V((0, 1, 0))
FORWARD = V((0, 0, 1))


def blender(g):
    """Game (x right, y up, z forward) to Blender (z up, facing -y)."""
    return V((g[0], -g[2], g[1]))


def material(name, colour, emit=False):
    """A material of a display-referred colour, stored linear. An
    emitting material marks surfaces the game draws unlit."""
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    lin = tuple(to_linear(c) for c in colour) + (1.0,)
    mat.diffuse_color = lin
    mat.roughness = 0.8
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = lin
        bsdf.inputs["Roughness"].default_value = 0.8
        if emit:
            bsdf.inputs["Emission Color"].default_value = lin
            bsdf.inputs["Emission Strength"].default_value = 3.0
    mat["moppe_unlit"] = emit
    return mat


def to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def segment_distance(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / ab.length_squared))
    return (p - (a + ab * t)).length


class Cage:
    """A coarse mesh in the game frame, before it becomes an object."""

    def __init__(self):
        self.verts = []
        self.faces = []

    def ring(self, centre, axis, ref, width, depth, sides, turn):
        axis = axis.normalized()
        u = (ref - axis * ref.dot(axis)).normalized()
        v = axis.cross(u)
        base = len(self.verts)
        for i in range(sides):
            t = turn + 2 * math.pi * i / sides
            self.verts.append(centre + u * (width * math.cos(t)) +
                              v * (depth * math.sin(t)))
        return list(range(base, base + sides))

    def orient(self, inside):
        """Winds every face counter-clockwise seen from outside."""
        for k, f in enumerate(self.faces):
            p = [self.verts[i] for i in f]
            n = V((0, 0, 0))
            for i in range(len(p)):
                n += p[i].cross(p[(i + 1) % len(p)])
            centre = sum(p, V((0, 0, 0))) / len(p)
            if n.dot(centre - inside(centre)) < 0:
                self.faces[k] = list(reversed(f))


def loft(sections, ref, sides=8, turn=0.0, close=(True, True)):
    """Sweeps rings through `sections`, each (centre, width, depth): width
    along `ref` squared to the path, depth square to both. The ends are
    closed with flat caps, which the subdivision rounds."""
    cage = Cage()
    rings = []
    n = len(sections)
    for i, (c, w, d) in enumerate(sections):
        prev = sections[max(0, i - 1)][0]
        nxt = sections[min(n - 1, i + 1)][0]
        rings.append(cage.ring(c, nxt - prev, ref, w, d, sides, turn))
    for a, b in zip(rings, rings[1:]):
        for i in range(sides):
            k = (i + 1) % sides
            cage.faces.append([a[i], a[k], b[k], b[i]])
    if close[0]:
        cage.faces.append(list(rings[0]))
    if close[1]:
        cage.faces.append(list(rings[-1]))
    centres = [c for c, _, _ in sections]

    def inside(p):
        best = min(range(len(centres) - 1),
                   key=lambda i: segment_distance(p, centres[i],
                                                  centres[i + 1]))
        a, b = centres[best], centres[best + 1]
        ab = b - a
        t = max(0.0, min(1.0, (p - a).dot(ab) / ab.length_squared))
        return a + ab * t

    cage.orient(inside)
    return cage


def blob(centre, radii, slices=10, stacks=6, shape=None):
    """A UV ellipsoid; `shape` may move each point (given its unit
    direction) before it is placed."""
    cage = Cage()
    rx, ry, rz = radii
    cage.verts.append(centre + V((0, -ry, 0)))
    rows = []
    for i in range(1, stacks):
        lat = -math.pi / 2 + math.pi * i / stacks
        row = []
        for j in range(slices):
            lon = 2 * math.pi * j / slices
            unit = V((math.cos(lat) * math.cos(lon), math.sin(lat),
                      math.cos(lat) * math.sin(lon)))
            p = V((unit.x * rx, unit.y * ry, unit.z * rz))
            if shape:
                p = shape(unit, p)
            cage.verts.append(centre + p)
            row.append(len(cage.verts) - 1)
        rows.append(row)
    cage.verts.append(centre + V((0, ry, 0)))
    top = len(cage.verts) - 1
    for j in range(slices):
        k = (j + 1) % slices
        cage.faces.append([0, rows[0][k], rows[0][j]])
        cage.faces.append([top, rows[-1][j], rows[-1][k]])
        for a, b in zip(rows, rows[1:]):
            cage.faces.append([a[j], a[k], b[k], b[j]])
    cage.orient(lambda p: centre)
    return cage


def slab(centre, half, axes=(RIGHT, UP, FORWARD)):
    """A box, to be rounded by a bevel and subdivision."""
    cage = Cage()
    x, y, z = axes
    hx, hy, hz = half
    for sy in (-1, 1):
        for sx, sz in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            cage.verts.append(centre + x * (hx * sx) + y * (hy * sy) +
                              z * (hz * sz))
    cage.faces = [[0, 1, 2, 3], [4, 5, 6, 7], [0, 1, 5, 4], [1, 2, 6, 5],
                  [2, 3, 7, 6], [3, 0, 4, 7]]
    cage.orient(lambda p: centre)
    return cage




def mesh_object(name, cage, mat, collection, origin=V((0, 0, 0)),
                levels=1, bevel=0.0, smooth=True):
    """Makes `cage` a mesh object whose origin is `origin` (game frame),
    with a bevel and subdivision left live."""
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([tuple(blender(v - origin)) for v in cage.verts], [],
                     cage.faces)
    mesh.update()
    mesh.materials.append(mat)
    for p in mesh.polygons:
        p.use_smooth = smooth
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    obj.location = blender(origin)
    if bevel > 0:
        mod = obj.modifiers.new("bevel", "BEVEL")
        mod.width = bevel
        mod.segments = 1
        mod.limit_method = "NONE"
    if levels > 0:
        mod = obj.modifiers.new("smooth", "SUBSURF")
        mod.levels = levels
        mod.render_levels = levels
    return obj


def torus(centre, axis, ref, radius, section, segments=24, sides=8,
          turn=0.0):
    """A ring of `segments` rings of `sides` around `axis`: `radius` to the
    tube's centre, `section` its (radial, axial) half extents."""
    cage = Cage()
    axis = axis.normalized()
    u = (ref - axis * ref.dot(axis)).normalized()
    v = axis.cross(u)
    rows = []
    for i in range(segments):
        a = 2 * math.pi * i / segments
        out = u * math.cos(a) + v * math.sin(a)
        c = centre + out * radius
        row = []
        for j in range(sides):
            b = turn + 2 * math.pi * j / sides
            cage.verts.append(c + out * (section[0] * math.cos(b)) +
                              axis * (section[1] * math.sin(b)))
            row.append(len(cage.verts) - 1)
        rows.append(row)
    for i in range(segments):
        a, b = rows[i], rows[(i + 1) % segments]
        for j in range(sides):
            k = (j + 1) % sides
            cage.faces.append([a[j], a[k], b[k], b[j]])

    def inside(p):
        q = p - centre
        q = q - axis * q.dot(axis)
        return centre + q.normalized() * radius

    cage.orient(inside)
    return cage


def tube(points, radius, ref=None, sides=6, close=(True, True)):
    """A round tube through `points`."""
    if ref is None:
        d = points[-1] - points[0]
        ref = RIGHT if abs(d.normalized().dot(RIGHT)) < 0.9 else UP
    return loft([(p, radius, radius) for p in points], ref, sides=sides,
                close=close)


def merge(*cages):
    out = Cage()
    for c in cages:
        base = len(out.verts)
        out.verts += c.verts
        out.faces += [[base + i for i in f] for f in c.faces]
    return out


def assembly(name, collection, origin=V((0, 0, 0)), mesh=None, point=None,
             parent=None):
    """An empty that a group of parts hangs from. `mesh` names the game
    mesh its parts export as, in the empty's local frame; `point` names a
    location the game reads."""
    empty = bpy.data.objects.new(name, None)
    empty.empty_display_type = "PLAIN_AXES"
    empty.empty_display_size = 0.05
    collection.objects.link(empty)
    if parent:
        empty.parent = parent
        empty.location = blender(origin) - parent.matrix_world.translation
    else:
        empty.location = blender(origin)
    if mesh:
        empty["moppe_mesh"] = mesh
    if point:
        empty["moppe_point"] = point
    return empty


def part(asm, name, cage, mat, levels=1, bevel=0.0, smooth=True,
         origin=None):
    """A part of the assembly `asm`, its cage given in the game frame
    relative to `origin` (the assembly's own origin by default)."""
    if origin is None:
        origin = V((0, 0, 0))
    obj = mesh_object(name, cage, mat, asm.users_collection[0],
                      origin=origin, levels=levels, bevel=bevel,
                      smooth=smooth)
    obj.parent = asm
    obj.location = blender(origin)
    return obj
