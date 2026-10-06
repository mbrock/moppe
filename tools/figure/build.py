"""Builds models/hiker.blend: the walking figure, modelled and rigged.

Run headless (or `make figure-model`):

    Blender -b --factory-startup --python tools/figure/build.py

Each part is its own object: a coarse cage -- mostly lofts, rings of
control points swept along a path -- smoothed by a Subdivision Surface
modifier, shaded smooth, weighted to the rig, and deformed by an Armature
modifier. Nothing is applied, so every part stays editable by hand in
Blender; tools/figure/export.py evaluates the modifiers when it exports.

Parts are authored in the game's frame (x right, y up, z forward, metres,
feet on y = 0) and converted to Blender's (z up, facing -y) on the way in.
The armature's joints sit where game/avatar.cc puts them at rest, with the
lengths of avatar_size, so the game's pose solver drives the rig directly.
Each bone's local Z (its roll) points along the reference the game uses to
orient that bone: forward for the trunk, head, and feet, the body's right
for the limbs.

Skin weights are inverse powers of the distance from each cage vertex to
the bones a part may follow, so a sleeve bends smoothly at the elbow and
the jacket twists along the spine. Rebuilding from this script discards
hand edits to the .blend.
"""

import math
import os
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(__file__))
from shapes import blender, loft, blob, slab, material  # noqa: E402
from shapes import segment_distance  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "models", "hiker.blend")

V = Vector


# Joints of the rest pose, in the game's frame. Left is side -1 (game -x).
THIGH, SHIN = 0.45, 0.43
UPPER_ARM, FOREARM = 0.29, 0.27

PELVIS = V((0, 0.96, 0))
WAIST = PELVIS + V((0, 0.10, 0))
CHEST = WAIST + V((0, 0.27, 0))
NECK = WAIST + V((0, 0.47, 0))
HEAD_UP = 0.15

FORWARD = V((0, 0, 1))
RIGHT = V((1, 0, 0))
UP = V((0, 1, 0))


def side_joints(s):
    hip = PELVIS + V((0.10 * s, 0, 0))
    knee = hip + V((0.005 * s, -1.0, 0)).normalized() * THIGH
    ankle = knee + (V((0.11 * s, 0.08, 0)) - knee).normalized() * SHIN
    shoulder = NECK + V((0.22 * s, -0.07, 0))
    spread = 0.12
    arm = V((math.sin(spread) * s, -math.cos(spread), 0))
    elbow = shoulder + arm * UPPER_ARM
    fold = 0.22
    forearm = arm * math.cos(fold) + FORWARD * math.sin(fold)
    wrist = elbow + forearm * FOREARM
    return dict(hip=hip, knee=knee, ankle=ankle, shoulder=shoulder,
                elbow=elbow, wrist=wrist)


SIDES = {-1: side_joints(-1), 1: side_joints(1)}
TAGS = ((-1, "L"), (1, "R"))

# Bone name, parent, head, tail, roll reference -- all in the game frame.
# Blender's .L is the character's own left, which is game side -1 here.
BONES = [
    ("pelvis", None, PELVIS, WAIST, FORWARD),
    ("spine", "pelvis", WAIST, CHEST, FORWARD),
    ("chest", "spine", CHEST, NECK, FORWARD),
    ("head", "chest", NECK, NECK + UP * HEAD_UP, FORWARD),
]
for s, tag in TAGS:
    j = SIDES[s]
    BONES += [
        (f"thigh.{tag}", "pelvis", j["hip"], j["knee"], RIGHT),
        (f"shin.{tag}", f"thigh.{tag}", j["knee"], j["ankle"], RIGHT),
        (f"foot.{tag}", f"shin.{tag}", j["ankle"],
         j["ankle"] + FORWARD * 0.15, UP),
        (f"upper_arm.{tag}", "chest", j["shoulder"], j["elbow"], RIGHT),
        (f"forearm.{tag}", f"upper_arm.{tag}", j["elbow"], j["wrist"], RIGHT),
        (f"hand.{tag}", f"forearm.{tag}", j["wrist"],
         j["wrist"] + (j["wrist"] - j["elbow"]).normalized() * 0.09, RIGHT),
    ]
SEGMENTS = {name: (head, tail) for name, _, head, tail, _ in BONES}

# Palette, display-referred like the game's DisplayColor. Materials store
# the linear equivalent; the exporter converts back.
PALETTE = {
    "jacket": (0.20, 0.38, 0.72),
    "jacket_dark": (0.13, 0.25, 0.50),
    "pants": (0.33, 0.28, 0.23),
    "boot": (0.45, 0.27, 0.14),
    "sole": (0.12, 0.10, 0.09),
    "sock": (0.90, 0.86, 0.75),
    "cap": (0.76, 0.22, 0.14),
    "cap_dark": (0.60, 0.16, 0.10),
    "scarf": (0.90, 0.66, 0.20),
    "mitten": (0.86, 0.60, 0.18),
    "skin": (0.92, 0.70, 0.56),
    "nose": (0.92, 0.60, 0.50),
    "hair": (0.48, 0.30, 0.17),
    "eye": (0.06, 0.05, 0.05),
    "glint": (1.0, 1.0, 1.0),
    "pack": (0.40, 0.42, 0.25),
    "pack_dark": (0.30, 0.32, 0.19),
    "strap": (0.22, 0.17, 0.12),
    "buckle": (0.75, 0.72, 0.65),
    "bedroll": (0.72, 0.34, 0.15),
}


class Builder:
    def __init__(self, rig, collection):
        self.rig = rig
        self.collection = collection
        self.materials = {name: material(name, colour)
                          for name, colour in PALETTE.items()}

    def part(self, name, cage, colour, bones, levels=1, bevel=0.0,
             sharpness=6.0):
        """Makes `cage` an object of one colour, weighted to `bones`."""
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata([tuple(blender(v)) for v in cage.verts], [],
                         cage.faces)
        mesh.update()
        mesh.materials.append(self.materials[colour])
        for p in mesh.polygons:
            p.use_smooth = True
        obj = bpy.data.objects.new(name, mesh)
        self.collection.objects.link(obj)
        obj.parent = self.rig
        if bevel > 0:
            mod = obj.modifiers.new("bevel", "BEVEL")
            mod.width = bevel
            mod.segments = 1
            mod.limit_method = "NONE"
        if levels > 0:
            mod = obj.modifiers.new("smooth", "SUBSURF")
            mod.levels = levels
            mod.render_levels = levels
        mod = obj.modifiers.new("rig", "ARMATURE")
        mod.object = self.rig

        groups = {b: obj.vertex_groups.new(name=b) for b in bones}
        for i, v in enumerate(cage.verts):
            if len(bones) == 1:
                groups[bones[0]].add([i], 1.0, "REPLACE")
                continue
            d = {b: max(segment_distance(v, *SEGMENTS[b]), 1e-4)
                 for b in bones}
            near = min(d.values())
            w = {b: (near / d[b]) ** sharpness for b in bones}
            total = sum(w.values())
            for b, x in w.items():
                if x / total > 0.02:
                    groups[b].add([i], x / total, "REPLACE")
        return obj


def model(build):
    part = build.part

    # Boots: a rounded leather foot that swells over the instep, a thick
    # sole, a short shaft up the shin, and a rolled wool sock.
    for s, tag in TAGS:
        j = SIDES[s]
        a = j["ankle"]
        x = a.x
        foot, shin = f"foot.{tag}", f"shin.{tag}"
        part(f"boot.{tag}", loft([
            (V((x, 0.07, -0.09)), 0.05, 0.04),
            (V((x, 0.08, -0.065)), 0.064, 0.065),
            (V((x, 0.085, -0.01)), 0.07, 0.075),
            (V((x, 0.07, 0.07)), 0.07, 0.058),
            (V((x, 0.055, 0.15)), 0.066, 0.045),
            (V((x, 0.05, 0.2)), 0.05, 0.035),
            (V((x, 0.05, 0.215)), 0.03, 0.02),
        ], RIGHT, sides=8, turn=math.pi / 8), "boot", [foot])
        part(f"sole.{tag}", slab(V((x, 0.024, 0.062)), (0.064, 0.024, 0.148)),
             "sole", [foot], bevel=0.016)
        part(f"shaft.{tag}", loft([
            (a + V((0, -0.02, -0.01)), 0.072, 0.078),
            (a + V((0, 0.06, -0.005)), 0.07, 0.074),
            (a + V((0, 0.13, -0.003)), 0.072, 0.074),
        ], RIGHT, sides=8), "boot", [shin, foot], sharpness=10)
        part(f"sock.{tag}", loft([
            (a + V((0, 0.105, -0.003)), 0.08, 0.082),
            (a + V((0, 0.13, -0.003)), 0.086, 0.088),
            (a + V((0, 0.155, -0.003)), 0.08, 0.082),
        ], RIGHT, sides=10), "sock", [shin], levels=1)

    # Trousers: one piece over the seat and down both legs, loose at the
    # knee, gathered into the socks.
    part("seat", loft([
        (PELVIS + V((0, -0.11, 0.0)), 0.1, 0.07),
        (PELVIS + V((0, -0.06, 0.0)), 0.17, 0.11),
        (PELVIS + V((0, 0.02, -0.005)), 0.18, 0.12),
        (PELVIS + V((0, 0.12, 0.0)), 0.16, 0.11),
    ], RIGHT, sides=10), "pants",
        ["pelvis", "thigh.L", "thigh.R"], sharpness=4)
    for s, tag in TAGS:
        j = SIDES[s]
        hip, knee, ankle = j["hip"], j["knee"], j["ankle"]
        part(f"trouser.{tag}", loft([
            (hip + V((0, 0.05, 0)), 0.1, 0.105),
            (hip + (knee - hip) * 0.35, 0.098, 0.1),
            (knee + V((0, 0.07, 0.005)), 0.08, 0.085),
            (knee + V((0, -0.02, 0.01)), 0.078, 0.084),
            (knee + (ankle - knee) * 0.5, 0.072, 0.076),
            (ankle + V((0, 0.13, 0)), 0.068, 0.072),
        ], RIGHT, sides=8), "pants",
            ["pelvis", f"thigh.{tag}", f"shin.{tag}"], sharpness=8)

    # Jacket: a padded body from a flared hem to a high collar, and sleeves
    # that start inside the shoulder and end in a dark cuff.
    part("jacket", loft([
        (PELVIS + V((0, -0.135, 0.0)), 0.205, 0.15),
        (PELVIS + V((0, -0.1, 0.0)), 0.225, 0.162),
        (PELVIS + V((0, 0.02, 0.0)), 0.21, 0.152),
        (WAIST + V((0, 0.08, 0.0)), 0.205, 0.148),
        (CHEST + V((0, 0.0, 0.0)), 0.225, 0.155),
        (CHEST + V((0, 0.1, -0.005)), 0.22, 0.145),
        (NECK + V((0, -0.035, -0.01)), 0.17, 0.115),
        (NECK + V((0, -0.005, -0.01)), 0.1, 0.085),
    ], RIGHT, sides=12, turn=math.pi / 12), "jacket",
        ["pelvis", "spine", "chest"], sharpness=4)
    part("hem", loft([
        (PELVIS + V((0, -0.15, 0.0)), 0.214, 0.158),
        (PELVIS + V((0, -0.12, 0.0)), 0.24, 0.174),
        (PELVIS + V((0, -0.085, 0.0)), 0.222, 0.162),
    ], RIGHT, sides=12, turn=math.pi / 12), "jacket_dark",
        ["pelvis"], levels=1)
    part("zip", slab(CHEST + V((0, -0.1, 0.152)), (0.01, 0.3, 0.008)),
         "jacket_dark", ["pelvis", "spine", "chest"], levels=1, sharpness=4)
    part("scarf", loft([
        (NECK + V((0, -0.07, 0.005)), 0.12, 0.11),
        (NECK + V((0, -0.02, 0.01)), 0.135, 0.125),
        (NECK + V((0, 0.035, 0.015)), 0.11, 0.1),
    ], RIGHT, sides=10), "scarf", ["chest"])
    part("scarf_tail", loft([
        (NECK + V((0.07, -0.04, 0.1)), 0.04, 0.015),
        (NECK + V((0.085, -0.13, 0.135)), 0.045, 0.014),
        (NECK + V((0.09, -0.21, 0.145)), 0.045, 0.014),
    ], RIGHT, sides=6), "scarf", ["chest"], levels=1)

    for s, tag in TAGS:
        j = SIDES[s]
        sh, el, wr = j["shoulder"], j["elbow"], j["wrist"]
        along = (wr - el).normalized()
        upper, fore, hand = f"upper_arm.{tag}", f"forearm.{tag}", \
            f"hand.{tag}"
        part(f"sleeve.{tag}", loft([
            (sh + V((-0.07 * s, 0.03, 0)), 0.07, 0.075),
            (sh + V((0, 0.0, 0)), 0.088, 0.09),
            (sh + (el - sh) * 0.5, 0.075, 0.078),
            (el, 0.068, 0.07),
            (el + (wr - el) * 0.55, 0.062, 0.064),
            (wr - along * 0.03, 0.06, 0.062),
        ], FORWARD, sides=8), "jacket", ["chest", upper, fore],
            sharpness=8)
        part(f"cuff.{tag}", loft([
            (wr - along * 0.05, 0.064, 0.066),
            (wr - along * 0.02, 0.07, 0.072),
            (wr + along * 0.005, 0.064, 0.066),
        ], FORWARD, sides=8), "jacket_dark", [fore], levels=1)
        # Mittens: a flat paddle, palm to the body, and a thumb forward.
        part(f"mitten.{tag}", loft([
            (wr - along * 0.01, 0.048, 0.042),
            (wr + along * 0.045, 0.058, 0.04),
            (wr + along * 0.1, 0.054, 0.036),
            (wr + along * 0.135, 0.034, 0.022),
        ], FORWARD, sides=8), "mitten", [hand])
        thumb = wr + along * 0.04 + FORWARD * 0.045 - RIGHT * (0.012 * s)
        part(f"thumb.{tag}", loft([
            (thumb - FORWARD * 0.02, 0.02, 0.018),
            (thumb + along * 0.03 + FORWARD * 0.01, 0.018, 0.017),
            (thumb + along * 0.05 + FORWARD * 0.012, 0.01, 0.01),
        ], FORWARD, sides=6), "mitten", [hand])

    # Head: a rounded skull narrowing to the jaw, button eyes with a
    # glint, a round nose, ears, a full beard and moustache, and hair at
    # the nape under a long knitted cap that flops back.
    head = NECK + V((0, 0.17, 0.02))

    def skull(unit, p):
        if unit.y < 0:
            p.x *= 1.0 - 0.18 * -unit.y
            p.z *= 1.0 - 0.06 * -unit.y
        return p

    part("neck", loft([
        (NECK + V((0, -0.04, 0)), 0.058, 0.056),
        (head + V((0, -0.06, -0.02)), 0.055, 0.052),
    ], RIGHT, sides=8), "skin", ["chest", "head"], levels=1)
    part("skull", blob(head, (0.118, 0.135, 0.125), slices=14, stacks=9,
                       shape=skull), "skin", ["head"])
    for s, tag in TAGS:
        eye = head + V((0.045 * s, 0.012, 0.112))
        part(f"eye.{tag}", blob(eye, (0.015, 0.022, 0.012), 8, 5), "eye",
             ["head"], levels=1)
        part(f"glint.{tag}", blob(eye + V((0.004 * s, 0.008, 0.01)),
                                  (0.004, 0.005, 0.003), 6, 4), "glint",
             ["head"], levels=1)
        part(f"ear.{tag}", blob(head + V((0.115 * s, -0.005, -0.005)),
                                (0.018, 0.032, 0.026), 8, 5), "skin",
             ["head"], levels=1)
        part(f"moustache.{tag}", loft([
            (head + V((0.0, -0.045, 0.13)), 0.018, 0.016),
            (head + V((0.035 * s, -0.05, 0.125)), 0.022, 0.018),
            (head + V((0.065 * s, -0.068, 0.104)), 0.012, 0.012),
        ], UP, sides=6), "hair", ["head"], levels=1)
    part("nose", blob(head + V((0, -0.018, 0.135)), (0.026, 0.024, 0.026),
                      8, 6), "nose", ["head"], levels=1)

    def beard_shape(unit, p):
        if p.z < -0.02:
            p.z = -0.02 + (p.z + 0.02) * 0.2
        if unit.y < 0:
            p.z += 0.03 * -unit.y
        return p

    part("beard", blob(head + V((0, -0.085, 0.055)), (0.105, 0.09, 0.08),
                       slices=10, stacks=6, shape=beard_shape), "hair",
         ["head"])
    part("hair", blob(head + V((0, -0.02, -0.06)), (0.11, 0.085, 0.07)),
         "hair", ["head"], levels=1)

    part("brim", loft([
        (head + V((0, 0.035, -0.005)), 0.128, 0.134),
        (head + V((0, 0.06, -0.006)), 0.136, 0.142),
        (head + V((0, 0.088, -0.008)), 0.128, 0.134),
    ], RIGHT, sides=12), "cap_dark", ["head"])
    part("cap", loft([
        (head + V((0, 0.06, -0.006)), 0.124, 0.13),
        (head + V((0, 0.13, -0.02)), 0.11, 0.115),
        (head + V((0, 0.2, -0.06)), 0.078, 0.08),
        (head + V((0, 0.24, -0.12)), 0.048, 0.05),
        (head + V((0, 0.245, -0.18)), 0.026, 0.026),
        (head + V((0, 0.22, -0.22)), 0.012, 0.012),
    ], RIGHT, sides=14), "cap", ["head"])
    part("pompom", blob(head + V((0, 0.21, -0.235)), (0.035, 0.035, 0.035),
                        8, 6), "scarf", ["head"], levels=1)

    # Rucksack: a rounded canvas body, a lid, a front pocket, side
    # pockets, a bedroll strapped underneath, and straps over the
    # shoulders with pale buckles.
    back = CHEST + V((0, -0.03, -0.22))
    part("pack", slab(back, (0.155, 0.19, 0.085)), "pack", ["chest"],
         bevel=0.03)
    part("lid", slab(back + V((0, 0.18, 0.0)), (0.16, 0.045, 0.095)),
         "pack_dark", ["chest"], bevel=0.02)
    part("pocket", slab(back + V((0, -0.06, -0.095)), (0.11, 0.085, 0.03)),
         "pack_dark", ["chest"], bevel=0.015)
    for s, tag in TAGS:
        part(f"side_pocket.{tag}",
             slab(back + V((0.165 * s, -0.07, 0.0)), (0.03, 0.08, 0.06)),
             "pack_dark", ["chest"], bevel=0.012)
    roll = back + V((0, -0.255, -0.01))
    part("bedroll", loft([
        (roll + V((-0.18, 0, 0)), 0.05, 0.05),
        (roll + V((-0.17, 0, 0)), 0.065, 0.065),
        (roll + V((0.17, 0, 0)), 0.065, 0.065),
        (roll + V((0.18, 0, 0)), 0.05, 0.05),
    ], UP, sides=10), "bedroll", ["chest"])
    for s, tag in TAGS:
        part(f"roll_strap.{tag}", loft([
            (roll + V((0.1 * s, 0, 0)), 0.071, 0.071),
            (roll + V((0.125 * s, 0, 0)), 0.071, 0.071),
        ], UP, sides=10), "strap", ["chest"], levels=1)
        top = NECK + V((0.1 * s, -0.035, 0.0))
        part(f"strap.{tag}", loft([
            (back + V((0.095 * s, 0.14, 0.09)), 0.032, 0.008),
            (top + V((0, 0.0, -0.08)), 0.032, 0.008),
            (top + V((0.005 * s, 0.025, 0.0)), 0.032, 0.008),
            (top + V((0.01 * s, 0.0, 0.08)), 0.032, 0.008),
            (CHEST + V((0.115 * s, 0.03, 0.16)), 0.032, 0.008),
            (CHEST + V((0.13 * s, -0.12, 0.165)), 0.03, 0.008),
            (CHEST + V((0.17 * s, -0.2, 0.12)), 0.028, 0.008),
        ], RIGHT, sides=6), "strap", ["chest", "spine"], levels=1,
             sharpness=4)
        part(f"buckle.{tag}",
             slab(CHEST + V((0.125 * s, -0.04, 0.17)),
                  (0.03, 0.02, 0.008)), "buckle", ["chest"], levels=0,
             bevel=0.004)


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

    parts = bpy.data.collections.new("hiker")
    scene.collection.children.link(parts)
    model(Builder(rig, parts))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
    print(f"wrote {OUT}: {len(parts.objects)} parts, {len(BONES)} bones")


build()
