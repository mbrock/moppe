"""Builds models/hiker.blend: the walking figure, meshed and rigged.

Run headless:

    Blender -b --factory-startup --python tools/figure/build.py

The figure is authored here in the game's frame (x right, y up, z forward,
metres, feet on y = 0) and converted to Blender's (z up, facing -y) on the
way in. Every part is a handful of big flat facets, like the trees: blocky
boots and mittens, a puffy jacket, a pointed cap, a rucksack with a bedroll.

The armature's joints sit exactly where game/avatar.cc puts them in its
rest pose, with the lengths of avatar_size, so the game's pose solver drives
the rig directly. Each bone's local Z (its roll) points along the reference
vector the game uses to orient that bone: forward for the trunk, head, and
feet, the body's right for the limbs. Skinning is rigid -- every vertex
belongs to one bone -- and the parts overlap at the joints to hide seams.

Once built, the .blend is the source: edit it by hand and re-export with
tools/figure/export.py. Rebuilding from this script discards hand edits.
"""

import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "models", "hiker.blend")


def blender(g):
    """Game (x right, y up, z forward) to Blender (z up, facing -y)."""
    return Vector((g[0], -g[2], g[1]))


V = Vector

# Joints of the rest pose, in the game's frame. Left is side -1 (game -x).
THIGH, SHIN = 0.45, 0.43
UPPER_ARM, FOREARM = 0.29, 0.27

PELVIS = V((0, 0.96, 0))
WAIST = PELVIS + V((0, 0.10, 0))
CHEST = WAIST + V((0, 0.27, 0))
NECK = WAIST + V((0, 0.47, 0))
HEAD_UP = 0.15


def side_joints(s):
    hip = PELVIS + V((0.10 * s, 0, 0))
    ankle = V((0.11 * s, 0.08, 0))
    knee_drop = V((0.005 * s, -1.0, 0)).normalized()
    knee = hip + knee_drop * THIGH
    # The shin reaches the ankle; nudge the ankle to keep its length exact.
    ankle = knee + (ankle - knee).normalized() * SHIN
    shoulder = NECK + V((0.22 * s, -0.07, 0))
    spread = 0.12
    arm = V((math.sin(spread) * s, -math.cos(spread), 0))
    elbow = shoulder + arm * UPPER_ARM
    fold = 0.22
    forearm = arm * math.cos(fold) + V((0, 0, 1)) * math.sin(fold)
    wrist = elbow + forearm * FOREARM
    return dict(hip=hip, knee=knee, ankle=ankle, shoulder=shoulder,
                elbow=elbow, wrist=wrist)


SIDES = {-1: side_joints(-1), 1: side_joints(1)}

FORWARD = V((0, 0, 1))
RIGHT = V((1, 0, 0))
UP = V((0, 1, 0))

# Bone name, parent, head, tail, roll reference -- all in the game frame.
# Blender's .L is the character's own left, which is game side -1 here.
BONES = [
    ("pelvis", None, PELVIS, WAIST, FORWARD),
    ("spine", "pelvis", WAIST, CHEST, FORWARD),
    ("chest", "spine", CHEST, NECK, FORWARD),
    ("head", "chest", NECK, NECK + UP * HEAD_UP, FORWARD),
]
for s, tag in ((-1, "L"), (1, "R")):
    j = SIDES[s]
    sole = FORWARD
    BONES += [
        (f"thigh.{tag}", "pelvis", j["hip"], j["knee"], RIGHT),
        (f"shin.{tag}", f"thigh.{tag}", j["knee"], j["ankle"], RIGHT),
        (f"foot.{tag}", f"shin.{tag}", j["ankle"], j["ankle"] + sole * 0.15,
         UP),
        (f"upper_arm.{tag}", "chest", j["shoulder"], j["elbow"], RIGHT),
        (f"forearm.{tag}", f"upper_arm.{tag}", j["elbow"], j["wrist"], RIGHT),
        (f"hand.{tag}", f"forearm.{tag}", j["wrist"],
         j["wrist"] + (j["wrist"] - j["elbow"]).normalized() * 0.09, RIGHT),
    ]


# Palette, display-referred like the game's DisplayColor. Materials store
# the linear equivalent; the exporter converts back.
PALETTE = {
    "jacket": (0.18, 0.35, 0.72),
    "jacket_dark": (0.10, 0.24, 0.55),
    "pants": (0.30, 0.25, 0.21),
    "boot": (0.42, 0.24, 0.12),
    "sole": (0.09, 0.08, 0.07),
    "sock": (0.88, 0.83, 0.70),
    "cap": (0.74, 0.20, 0.11),
    "cap_dark": (0.55, 0.14, 0.08),
    "wool": (0.93, 0.90, 0.82),
    "mitten": (0.90, 0.64, 0.16),
    "skin": (0.88, 0.64, 0.50),
    "beard": (0.46, 0.28, 0.15),
    "pack": (0.38, 0.40, 0.23),
    "pack_dark": (0.27, 0.29, 0.17),
    "strap": (0.17, 0.13, 0.10),
    "bedroll": (0.72, 0.33, 0.13),
}


def to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


class Figure:
    """Accumulates faceted parts, each owned by one bone and one colour."""

    def __init__(self):
        self.verts = []
        self.faces = []
        self.face_material = []
        self.vert_bone = []
        self.materials = list(PALETTE)

    def add(self, bone, colour, verts, faces):
        base = len(self.verts)
        self.verts += [blender(v) for v in verts]
        self.vert_bone += [bone] * len(verts)
        index = self.materials.index(colour)
        for f in faces:
            self.faces.append([base + i for i in f])
            self.face_material.append(index)

    def mark(self):
        return len(self.verts)

    def scale_since(self, mark, origin, factor):
        """Scales the parts added since `mark` about `origin`."""
        o = blender(origin)
        for i in range(mark, len(self.verts)):
            self.verts[i] = o + (self.verts[i] - o) * factor

    # Shapes, all in the game frame.

    def hexahedron(self, bone, colour, corners):
        """Eight corners: bottom ring then top ring, each counter-clockwise
        seen from above, starting back-left."""
        faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5),
                 (2, 3, 7, 6), (3, 0, 4, 7)]
        self.add(bone, colour, corners, faces)

    def box(self, bone, colour, centre, half, top_scale=(1, 1), shift=(0, 0)):
        """A box, its top optionally scaled (x, z) and shifted (x, z)."""
        c, (hx, hy, hz) = centre, half
        sx, sz = top_scale
        dx, dz = shift
        bottom = [V((-hx, -hy, -hz)), V((hx, -hy, -hz)), V((hx, -hy, hz)),
                  V((-hx, -hy, hz))]
        top = [V((x * sx + dx, hy, z * sz + dz)) for x, _, z in bottom]
        self.hexahedron(bone, colour, [c + p for p in bottom + top])

    def prism(self, bone, colour, a, b, ref, ra, rb, sides=6, turn=0.0,
              cap_a=True, cap_b=True):
        """A faceted tapered prism from a to b; ra, rb are (width, depth)
        radii, width measured along ref squared to the axis."""
        axis = (b - a).normalized()
        u = (ref - axis * ref.dot(axis)).normalized()
        v = axis.cross(u)
        ring = []
        for end, (w, d) in ((a, ra), (b, rb)):
            for i in range(sides):
                t = turn + 2 * math.pi * i / sides
                ring.append(end + u * (w * math.cos(t)) + v * (d * math.sin(t)))
        n = sides
        faces = [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
        if cap_a:
            faces.append(tuple(reversed(range(n))))
        if cap_b:
            faces.append(tuple(range(n, 2 * n)))
        # Orient every face outward from the axis midpoint.
        self.add(bone, colour, ring, faces)
        self._orient_last(len(faces), (a + b) * 0.5)

    def gem(self, bone, colour, centre, radii, slices=7, stacks=5,
            lat_from=-0.5, lat_to=0.5, cap=False):
        """A low-poly ellipsoid, or a band of one between two latitudes
        (fractions of pi), closed with a flat cap at the bottom if asked."""
        rx, ry, rz = radii
        verts, faces = [], []
        rows = []
        for i in range(stacks + 1):
            lat = math.pi * (lat_from + (lat_to - lat_from) * i / stacks)
            if abs(abs(lat) - math.pi / 2) < 1e-6:
                verts.append(centre + V((0, ry * math.sin(lat), 0)))
                rows.append([len(verts) - 1] * slices)
                continue
            row = []
            for j in range(slices):
                lon = 2 * math.pi * j / slices
                verts.append(centre + V((rx * math.cos(lat) * math.cos(lon),
                                         ry * math.sin(lat),
                                         rz * math.cos(lat) * math.sin(lon))))
                row.append(len(verts) - 1)
            rows.append(row)
        for i in range(stacks):
            for j in range(slices):
                k = (j + 1) % slices
                quad = [rows[i][j], rows[i][k], rows[i + 1][k], rows[i + 1][j]]
                unique = []
                for q in quad:
                    if q not in unique:
                        unique.append(q)
                if len(unique) >= 3:
                    faces.append(tuple(unique))
        if cap and len(set(rows[0])) > 1:
            faces.append(tuple(rows[0]))
        self.add(bone, colour, verts, faces)
        self._orient_last(len(faces), centre)

    def cone(self, bone, colour, base, tip, ref, radii, sides=7, turn=0.0):
        axis = (tip - base).normalized()
        u = (ref - axis * ref.dot(axis)).normalized()
        v = axis.cross(u)
        w, d = radii
        verts = [base + u * (w * math.cos(turn + 2 * math.pi * i / sides)) +
                 v * (d * math.sin(turn + 2 * math.pi * i / sides))
                 for i in range(sides)] + [tip]
        faces = [(i, (i + 1) % sides, sides) for i in range(sides)]
        faces.append(tuple(reversed(range(sides))))
        self.add(bone, colour, verts, faces)
        self._orient_last(len(faces), base + (tip - base) * 0.3)

    def _orient_last(self, count, inside):
        """Winds the last `count` faces counter-clockwise seen from outside."""
        inside_b = blender(inside)
        for k in range(len(self.faces) - count, len(self.faces)):
            f = self.faces[k]
            p = [self.verts[i] for i in f]
            n = (p[1] - p[0]).cross(p[2] - p[0])
            centre = sum(p, Vector()) / len(p)
            if n.dot(centre - inside_b) < 0:
                self.faces[k] = list(reversed(f))


def model(fig):
    for s, tag in ((-1, "L"), (1, "R")):
        j = SIDES[s]
        x = j["ankle"].x
        foot, shin, thigh = f"foot.{tag}", f"shin.{tag}", f"thigh.{tag}"

        # Boots: a thick lug sole, a leather upper whose toe slopes down,
        # and a short shaft with a rolled wool sock over its top.
        fig.box(foot, "sole", V((x, 0.018, 0.055)), (0.072, 0.018, 0.16))
        fig.box(foot, "boot", V((x, 0.075, 0.035)), (0.066, 0.04, 0.14),
                top_scale=(0.92, 0.62), shift=(0, -0.045))
        fig.prism(shin, "boot", j["ankle"] + V((0, -0.03, -0.01)),
                  j["ankle"] + V((0, 0.12, -0.005)), RIGHT,
                  (0.07, 0.078), (0.072, 0.074), sides=6)
        fig.prism(shin, "sock", j["ankle"] + V((0, 0.105, -0.005)),
                  j["ankle"] + V((0, 0.155, -0.005)), RIGHT,
                  (0.08, 0.082), (0.077, 0.079), sides=7)

        # Trousers: shin, a faceted knee, and a thigh that widens to the hip.
        fig.prism(shin, "pants", j["ankle"] + V((0, 0.14, 0)), j["knee"],
                  RIGHT, (0.074, 0.076), (0.08, 0.082), sides=6)
        fig.gem(shin, "pants", j["knee"] + V((0, 0, 0.005)),
                (0.086, 0.088, 0.088), slices=6, stacks=4)
        fig.prism(thigh, "pants", j["knee"], j["hip"],
                  RIGHT, (0.086, 0.088), (0.096, 0.096), sides=6)

    # Seat of the trousers under the jacket's flared hem.
    fig.box("pelvis", "pants", PELVIS + V((0, 0.0, -0.005)),
            (0.155, 0.075, 0.105))
    fig.prism("pelvis", "jacket", PELVIS + V((0, -0.08, 0)),
              PELVIS + V((0, 0.10, 0)), RIGHT,
              (0.235, 0.165), (0.2, 0.142), sides=8, turn=math.pi / 8)
    fig.prism("pelvis", "jacket_dark", PELVIS + V((0, -0.095, 0)),
              PELVIS + V((0, -0.05, 0)), RIGHT,
              (0.242, 0.17), (0.238, 0.167), sides=8, turn=math.pi / 8)

    # Puffy jacket: a belly that swells to the chest, then narrows to the
    # collar; a dark zip down the front.
    fig.prism("spine", "jacket", WAIST + V((0, -0.03, 0)), CHEST, RIGHT,
              (0.18, 0.13), (0.205, 0.14), sides=8, turn=math.pi / 8)
    fig.prism("chest", "jacket", CHEST, NECK + V((0, -0.025, 0)), RIGHT,
              (0.205, 0.14), (0.15, 0.11), sides=8, turn=math.pi / 8)
    fig.box("spine", "jacket_dark", WAIST + V((0, 0.12, 0.137)),
            (0.012, 0.16, 0.012))
    fig.box("pelvis", "jacket_dark", PELVIS + V((0, 0.0, 0.15)),
            (0.012, 0.09, 0.012))

    # Scarf, wound thick, with one tail hanging over the chest.
    fig.prism("chest", "mitten", NECK + V((0, -0.06, 0.0)),
              NECK + V((0, 0.035, 0.01)), RIGHT,
              (0.13, 0.115), (0.105, 0.10), sides=7)

    # Head: a faceted egg, a big nose, a square beard, and a pointed cap
    # that leans back like a spruce top, with a turned-up wool brim.
    head = NECK + UP * HEAD_UP + FORWARD * 0.015
    head_mark = fig.mark()
    fig.gem("head", "skin", head, (0.1, 0.118, 0.106), slices=7, stacks=5)
    fig.cone("head", "skin", head + V((0, -0.005, 0.098)),
             head + V((0, -0.035, 0.145)), UP, (0.022, 0.03), sides=4,
             turn=math.pi / 4)
    fig.box("head", "beard", head + V((0, -0.082, 0.072)),
            (0.072, 0.045, 0.042), top_scale=(1.08, 1.1), shift=(0, -0.004))
    fig.box("head", "beard", head + V((0, -0.135, 0.092)),
            (0.05, 0.025, 0.026), top_scale=(1.4, 1.5), shift=(0, -0.008))
    fig.prism("head", "cap_dark", head + V((0, 0.025, -0.005)),
              head + V((0, 0.075, -0.01)), RIGHT,
              (0.118, 0.124), (0.116, 0.122), sides=8, turn=math.pi / 8)
    fig.cone("head", "cap", head + V((0, 0.07, -0.01)),
             head + V((0, 0.29, -0.11)), RIGHT, (0.108, 0.114), sides=8,
             turn=math.pi / 8)
    # The knitted crown hugs the skull under the point.
    fig.gem("head", "cap", head + V((0, 0.0, -0.003)),
            (0.108, 0.13, 0.114), slices=8, stacks=3, lat_from=0.1,
            lat_to=0.5, cap=True)
    fig.gem("head", "cap_dark", head + V((0, 0.29, -0.115)),
            (0.032, 0.032, 0.032), slices=5, stacks=3)
    for s in (-1, 1):
        fig.box("head", "sole", head + V((0.042 * s, 0.012, 0.094)),
                (0.013, 0.017, 0.012))
    # A toy's head: a fifth larger, grown up from the neck.
    fig.scale_since(head_mark, NECK + V((0, 0.03, 0)), 1.22)

    # Arms: a padded shoulder, sleeves with a dark cuff, and mittens with
    # a thumb.
    for s, tag in ((-1, "L"), (1, "R")):
        j = SIDES[s]
        upper, fore, hand = f"upper_arm.{tag}", f"forearm.{tag}", f"hand.{tag}"
        fig.gem(upper, "jacket", j["shoulder"] + V((-0.01 * s, 0.0, 0)),
                (0.098, 0.092, 0.098), slices=6, stacks=4)
        fig.prism(upper, "jacket", j["shoulder"], j["elbow"], FORWARD,
                  (0.08, 0.082), (0.07, 0.072), sides=6)
        fig.gem(fore, "jacket", j["elbow"], (0.072, 0.072, 0.072),
                slices=6, stacks=4)
        fig.prism(fore, "jacket", j["elbow"], j["wrist"] - (j["wrist"] - j[
            "elbow"]).normalized() * 0.035, FORWARD, (0.068, 0.07),
                  (0.06, 0.062), sides=6)
        cuff_a = j["wrist"] - (j["wrist"] - j["elbow"]).normalized() * 0.05
        fig.prism(fore, "jacket_dark", cuff_a, j["wrist"] + (
            j["wrist"] - j["elbow"]).normalized() * 0.005, FORWARD,
                  (0.072, 0.074), (0.07, 0.072), sides=6)
        along = (j["wrist"] - j["elbow"]).normalized()
        palm = j["wrist"] + along * 0.065
        fig.gem(hand, "mitten", palm, (0.058, 0.085, 0.065), slices=6,
                stacks=4)
        fig.gem(hand, "mitten", palm + V((-0.03 * s, 0.03, 0.045)),
                (0.022, 0.035, 0.022), slices=5, stacks=3)

    # Rucksack: a tapered canvas body, a darker lid and pocket, a bedroll
    # strapped across the top, and straps over both shoulders.
    back = CHEST + V((0, -0.02, -0.215))
    fig.box("chest", "pack", back, (0.16, 0.2, 0.09), top_scale=(0.88, 0.8))
    fig.box("chest", "pack_dark", back + V((0, 0.17, 0.005)),
            (0.15, 0.05, 0.09), top_scale=(1.0, 0.9), shift=(0, -0.01))
    fig.box("chest", "pack_dark", back + V((0, -0.08, -0.105)),
            (0.12, 0.085, 0.035), top_scale=(0.95, 0.8))
    roll = back + V((0, -0.27, -0.01))
    fig.prism("chest", "bedroll", roll + V((-0.17, 0, 0)),
              roll + V((0.17, 0, 0)), UP, (0.07, 0.07), (0.07, 0.07),
              sides=8)
    for s in (-1, 1):
        fig.prism("chest", "strap", back + V((0.09 * s, 0.22, -0.098)),
                  back + V((0.09 * s, 0.02, -0.11)), RIGHT,
                  (0.022, 0.008), (0.022, 0.008), sides=4, turn=math.pi / 4)
        top = NECK + V((0.105 * s, -0.04, 0.0))
        fig.prism("chest", "strap", top + V((0, 0.0, -0.12)),
                  top + V((0, 0.02, 0.04)), RIGHT, (0.03, 0.012),
                  (0.03, 0.012), sides=4, turn=math.pi / 4)
        fig.prism("chest", "strap", top + V((0, 0.02, 0.04)),
                  CHEST + V((0.11 * s, -0.04, 0.145)), RIGHT,
                  (0.03, 0.012), (0.03, 0.012), sides=4, turn=math.pi / 4)
        fig.prism("chest", "strap", roll + V((0.1 * s, 0, 0)),
                  roll + V((0.125 * s, 0, 0)), UP, (0.076, 0.076),
                  (0.076, 0.076), sides=8)


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene

    arm_data = bpy.data.armatures.new("hiker_rig")
    rig = bpy.data.objects.new("hiker_rig", arm_data)
    scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    for name, parent, head, tail, ref in BONES:
        b = arm_data.edit_bones.new(name)
        b.head = blender(head)
        b.tail = blender(tail)
        b.align_roll(blender(ref))
        if parent:
            b.parent = arm_data.edit_bones[parent]
    bpy.ops.object.mode_set(mode="OBJECT")
    arm_data.display_type = "STICK"
    rig.show_in_front = True

    fig = Figure()
    model(fig)
    mesh = bpy.data.meshes.new("hiker")
    mesh.from_pydata([tuple(v) for v in fig.verts], [], fig.faces)
    mesh.update()
    for name in fig.materials:
        mat = bpy.data.materials.new(name)
        lin = tuple(to_linear(c) for c in PALETTE[name])
        mat.diffuse_color = lin + (1.0,)
        mat.roughness = 0.9
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        if bsdf:
            bsdf.inputs["Base Color"].default_value = lin + (1.0,)
            bsdf.inputs["Roughness"].default_value = 0.9
        mesh.materials.append(mat)
    mesh.polygons.foreach_set("material_index", fig.face_material)
    for p in mesh.polygons:
        p.use_smooth = False

    body = bpy.data.objects.new("hiker", mesh)
    scene.collection.objects.link(body)
    body.parent = rig
    for name, *_ in BONES:
        body.vertex_groups.new(name=name)
    for i, bone in enumerate(fig.vert_bone):
        body.vertex_groups[bone].add([i], 1.0, "REPLACE")
    mod = body.modifiers.new("rig", "ARMATURE")
    mod.object = rig

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
    print(f"wrote {OUT}: {len(mesh.vertices)} vertices, "
          f"{sum(len(p.vertices) - 2 for p in mesh.polygons)} triangles, "
          f"{len(BONES)} bones")


build()
