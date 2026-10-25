"""Builds models/hiker.blend: the walking figure, modelled and rigged.

He is dressed and proportioned after a photogrammetry scan of Krisjanis
(measured, not imported): his shirt, shorts, sandals, cap, and beard.

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
# the linear equivalent; the exporter converts back. He is dressed as the
# scan of Krisjanis has him: a pale blue button-down shirt, its sleeves
# long, denim shorts, sandals, and a navy cap over sandy hair and a short
# beard. The names the green outfit repaints (game/figure.cc) stay.
PALETTE = {
    "jacket": (0.50, 0.62, 0.78),
    "jacket_dark": (0.40, 0.51, 0.67),
    "button": (0.90, 0.90, 0.86),
    "denim": (0.22, 0.33, 0.52),
    "denim_dark": (0.16, 0.25, 0.41),
    "sandal": (0.16, 0.15, 0.14),
    "sole": (0.62, 0.58, 0.50),
    "cap": (0.17, 0.21, 0.32),
    "cap_dark": (0.12, 0.15, 0.24),
    "skin": (0.88, 0.68, 0.55),
    "nose": (0.88, 0.62, 0.52),
    "hair": (0.62, 0.47, 0.31),
    "eye": (0.06, 0.05, 0.05),
    "glint": (1.0, 1.0, 1.0),
    "pack": (0.40, 0.42, 0.25),
    "pack_dark": (0.30, 0.32, 0.19),
    "strap": (0.22, 0.17, 0.12),
    "buckle": (0.66, 0.63, 0.57),
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

    # Bare feet in sandals: a slim foot on a pale sole, a band over the
    # toes, and a strap round the ankle and under the heel.
    for s, tag in TAGS:
        j = SIDES[s]
        a = j["ankle"]
        x = a.x
        foot, shin = f"foot.{tag}", f"shin.{tag}"
        part(f"bare_foot.{tag}", loft([
            (V((x, 0.05, -0.085)), 0.032, 0.028),
            (V((x, 0.06, -0.055)), 0.042, 0.042),
            (V((x, 0.06, 0.01)), 0.046, 0.038),
            (V((x, 0.045, 0.1)), 0.05, 0.026),
            (V((x, 0.038, 0.17)), 0.046, 0.019),
            (V((x, 0.036, 0.2)), 0.03, 0.014),
        ], RIGHT, sides=8, turn=math.pi / 8), "skin", [foot])
        part(f"sandal_sole.{tag}",
             slab(V((x, 0.012, 0.058)), (0.054, 0.012, 0.15)), "sole",
             [foot], bevel=0.01)
        part(f"toe_strap.{tag}", loft([
            (V((x, 0.043, 0.105)), 0.056, 0.03),
            (V((x, 0.043, 0.13)), 0.054, 0.028),
        ], RIGHT, sides=8), "sandal", [foot], levels=1)
        part(f"instep_strap.{tag}", loft([
            (V((x, 0.057, 0.02)), 0.05, 0.043),
            (V((x, 0.057, 0.042)), 0.05, 0.042),
        ], RIGHT, sides=8), "sandal", [foot], levels=1)
        part(f"ankle_strap.{tag}", loft([
            (a + V((0, -0.03, -0.012)), 0.048, 0.054),
            (a + V((0, -0.012, -0.012)), 0.047, 0.053),
        ], RIGHT, sides=8), "sandal", [foot, shin], levels=1)
        # The bare shin and calf, from the ankle up under the shorts.
        knee = j["knee"]
        part(f"calf.{tag}", loft([
            (a + V((0, -0.035, -0.008)), 0.036, 0.04),
            (a + V((0, 0.06, -0.005)), 0.038, 0.042),
            (knee + (a - knee) * 0.55, 0.053, 0.063),
            (knee + (a - knee) * 0.25, 0.054, 0.062),
            (knee + V((0, 0.0, 0.005)), 0.05, 0.052),
            (knee + V((0, 0.09, 0.0)), 0.058, 0.06),
        ], RIGHT, sides=8), "skin",
            [f"thigh.{tag}", shin, foot], sharpness=8)

    # Denim shorts: one piece over the seat and down both legs to just
    # above the knee, with a turned-up hem.
    part("seat", loft([
        (PELVIS + V((0, -0.11, 0.0)), 0.09, 0.07),
        (PELVIS + V((0, -0.06, 0.0)), 0.155, 0.11),
        (PELVIS + V((0, 0.02, -0.005)), 0.165, 0.118),
        (PELVIS + V((0, 0.08, 0.0)), 0.16, 0.112),
    ], RIGHT, sides=10), "denim",
        ["pelvis", "thigh.L", "thigh.R"], sharpness=4)
    for s, tag in TAGS:
        j = SIDES[s]
        hip, knee = j["hip"], j["knee"]
        part(f"short_leg.{tag}", loft([
            (hip + V((0, 0.04, 0)), 0.088, 0.095),
            (hip + (knee - hip) * 0.4, 0.08, 0.085),
            (knee + V((0, 0.07, 0.005)), 0.07, 0.074),
        ], RIGHT, sides=8), "denim",
            ["pelvis", f"thigh.{tag}"], sharpness=8)
        part(f"short_hem.{tag}", loft([
            (knee + V((0, 0.055, 0.005)), 0.073, 0.077),
            (knee + V((0, 0.075, 0.005)), 0.075, 0.079),
            (knee + V((0, 0.095, 0.005)), 0.072, 0.076),
        ], RIGHT, sides=8), "denim_dark", [f"thigh.{tag}"], levels=1)

    # The shirt: a button-down worn out over the shorts, slim, with a
    # placket of white buttons, two flapped chest pockets, a collar, and
    # long sleeves to cuffs at the wrist.
    part("jacket", loft([
        (PELVIS + V((0, -0.06, 0.0)), 0.18, 0.128),
        (PELVIS + V((0, -0.03, 0.0)), 0.182, 0.13),
        (PELVIS + V((0, 0.04, 0.0)), 0.175, 0.124),
        (WAIST + V((0, 0.08, 0.0)), 0.168, 0.118),
        (CHEST + V((0, 0.0, 0.0)), 0.182, 0.125),
        (CHEST + V((0, 0.1, -0.005)), 0.19, 0.122),
        (NECK + V((0, -0.04, -0.01)), 0.15, 0.1),
        (NECK + V((0, -0.01, -0.01)), 0.07, 0.06),
    ], RIGHT, sides=12, turn=math.pi / 12), "jacket",
        ["pelvis", "spine", "chest"], sharpness=4)
    part("hem", loft([
        (PELVIS + V((0, -0.07, 0.0)), 0.181, 0.129),
        (PELVIS + V((0, -0.055, 0.0)), 0.186, 0.133),
        (PELVIS + V((0, -0.04, 0.0)), 0.183, 0.131),
    ], RIGHT, sides=12, turn=math.pi / 12), "jacket_dark",
        ["pelvis"], levels=1)
    part("collar", loft([
        (NECK + V((0, -0.035, -0.008)), 0.08, 0.07),
        (NECK + V((0, 0.0, -0.01)), 0.07, 0.064),
        (NECK + V((0, 0.02, -0.012)), 0.075, 0.068),
    ], RIGHT, sides=10), "jacket_dark", ["chest"], levels=1)
    part("placket", slab(CHEST + V((0, -0.13, 0.124)), (0.013, 0.3, 0.005)),
         "jacket_dark", ["pelvis", "spine", "chest"], levels=1, sharpness=4)
    buttons = []
    for k in range(7):
        y = PELVIS.y - 0.02 + k * (NECK.y - 0.06 - PELVIS.y + 0.02) / 6
        depth = 0.128 if y > CHEST.y - 0.05 else 0.124
        buttons.append((V((0, y, depth + 0.004)), y))
    for k, (at, y) in enumerate(buttons):
        bone = "chest" if y > CHEST.y else "spine" if y > WAIST.y \
            else "pelvis"
        part(f"button.{k}", blob(at, (0.006, 0.006, 0.003), 6, 4),
             "button", [bone], levels=0)
    for s, tag in TAGS:
        pocket = CHEST + V((0.085 * s, 0.05, 0.123))
        part(f"pocket.{tag}", slab(pocket, (0.045, 0.05, 0.006)),
             "jacket", ["chest"], levels=0, bevel=0.006)
        part(f"pocket_flap.{tag}",
             slab(pocket + V((0, 0.045, 0.004)), (0.047, 0.014, 0.006)),
             "jacket_dark", ["chest"], levels=0, bevel=0.004)

    for s, tag in TAGS:
        j = SIDES[s]
        sh, el, wr = j["shoulder"], j["elbow"], j["wrist"]
        along = (wr - el).normalized()
        upper, fore, hand = f"upper_arm.{tag}", f"forearm.{tag}", \
            f"hand.{tag}"
        part(f"sleeve.{tag}", loft([
            (sh + V((-0.07 * s, 0.03, 0)), 0.06, 0.065),
            (sh + V((0, 0.0, 0)), 0.068, 0.07),
            (sh + (el - sh) * 0.5, 0.058, 0.06),
            (el, 0.052, 0.054),
            (el + (wr - el) * 0.55, 0.047, 0.049),
            (wr - along * 0.03, 0.044, 0.046),
        ], FORWARD, sides=8), "jacket", ["chest", upper, fore],
            sharpness=8)
        part(f"cuff.{tag}", loft([
            (wr - along * 0.05, 0.046, 0.048),
            (wr - along * 0.025, 0.048, 0.05),
            (wr - along * 0.005, 0.045, 0.047),
        ], FORWARD, sides=8), "jacket_dark", [fore], levels=1)
        # Bare hands: a palm and fingers together, and a thumb forward.
        part(f"palm.{tag}", loft([
            (wr - along * 0.01, 0.036, 0.024),
            (wr + along * 0.045, 0.046, 0.022),
            (wr + along * 0.1, 0.042, 0.018),
            (wr + along * 0.13, 0.026, 0.012),
        ], FORWARD, sides=8), "skin", [hand])
        thumb = wr + along * 0.035 + FORWARD * 0.03 - RIGHT * (0.01 * s)
        part(f"thumb.{tag}", loft([
            (thumb - FORWARD * 0.015, 0.014, 0.013),
            (thumb + along * 0.03 + FORWARD * 0.008, 0.012, 0.012),
            (thumb + along * 0.048 + FORWARD * 0.01, 0.007, 0.007),
        ], FORWARD, sides=6), "skin", [hand])

    # Head: his own proportions rather than a gnome's -- a longer face
    # narrowing to the jaw, a straight nose, sandy hair at the sides and
    # nape, a short beard and moustache, and a navy cap with a short bill.
    head = NECK + V((0, 0.14, 0.015))

    def skull(unit, p):
        if unit.y < 0:
            p.x *= 1.0 - 0.22 * -unit.y
            p.z *= 1.0 - 0.05 * -unit.y
        return p

    part("neck", loft([
        (NECK + V((0, -0.04, 0)), 0.052, 0.05),
        (head + V((0, -0.05, -0.02)), 0.05, 0.048),
    ], RIGHT, sides=8), "skin", ["chest", "head"], levels=1)
    part("skull", blob(head, (0.083, 0.115, 0.1), slices=14, stacks=9,
                       shape=skull), "skin", ["head"])
    for s, tag in TAGS:
        eye = head + V((0.031 * s, 0.018, 0.088))
        part(f"eye.{tag}", blob(eye, (0.01, 0.008, 0.006), 8, 5), "eye",
             ["head"], levels=1)
        part(f"glint.{tag}", blob(eye + V((0.003 * s, 0.003, 0.005)),
                                  (0.002, 0.002, 0.0015), 6, 4), "glint",
             ["head"], levels=1)
        part(f"brow.{tag}", loft([
            (eye + V((-0.012 * s, 0.017, 0.004)), 0.006, 0.004),
            (eye + V((0.014 * s, 0.019, 0.0)), 0.006, 0.004),
        ], UP, sides=6), "hair", ["head"], levels=1)
        part(f"ear.{tag}", blob(head + V((0.083 * s, 0.0, -0.005)),
                                (0.013, 0.028, 0.02), 8, 5), "skin",
             ["head"], levels=1)
        part(f"moustache.{tag}", loft([
            (head + V((0.0, -0.045, 0.098)), 0.012, 0.007),
            (head + V((0.022 * s, -0.048, 0.093)), 0.013, 0.008),
            (head + V((0.04 * s, -0.062, 0.078)), 0.007, 0.006),
        ], UP, sides=6), "hair", ["head"], levels=1)
    part("nose", blob(head + V((0, -0.008, 0.1)), (0.014, 0.026, 0.016),
                      8, 6), "nose", ["head"], levels=1)

    def beard_shape(unit, p):
        if p.z < -0.03:
            p.z = -0.03 + (p.z + 0.03) * 0.2
        return p

    part("beard", blob(head + V((0, -0.068, 0.03)), (0.072, 0.052, 0.07),
                       slices=10, stacks=6, shape=beard_shape), "hair",
         ["head"])
    part("hair", blob(head + V((0, 0.0, -0.035)), (0.088, 0.085, 0.075)),
         "hair", ["head"], levels=1)

    part("cap", loft([
        (head + V((0, 0.035, -0.008)), 0.09, 0.106),
        (head + V((0, 0.07, -0.01)), 0.088, 0.103),
        (head + V((0, 0.1, -0.015)), 0.072, 0.085),
        (head + V((0, 0.12, -0.02)), 0.04, 0.047),
        (head + V((0, 0.125, -0.02)), 0.012, 0.012),
    ], RIGHT, sides=14), "cap", ["head"])
    part("cap_band", loft([
        (head + V((0, 0.03, -0.008)), 0.091, 0.107),
        (head + V((0, 0.045, -0.008)), 0.092, 0.108),
    ], RIGHT, sides=14), "cap_dark", ["head"], levels=1)
    dip = math.radians(14)
    bill_fwd = V((0, -math.sin(dip), math.cos(dip)))
    bill_up = V((0, math.cos(dip), math.sin(dip)))
    part("brim", slab(head + V((0, 0.036, 0.118)), (0.072, 0.005, 0.042),
                      axes=(RIGHT, bill_up, bill_fwd)), "cap_dark",
         ["head"], levels=1, bevel=0.004)

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
