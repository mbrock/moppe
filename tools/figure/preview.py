"""Renders a lineup of the figure from models/hiker.blend.

    Blender -b models/hiker.blend --python tools/figure/preview.py -- OUT.png

Front, three-quarter, side, and back views in the rest pose, then a stride,
a tucked jump, and a reach overhead (a glider's control bar), all posed
through the rig so the skinning is exercised too. The .blend is not saved.
"""

import math
import sys

import bpy
from mathutils import Euler, Vector

out = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else \
    "/tmp/hiker.png"
scene = bpy.context.scene
rig = bpy.data.objects["hiker_rig"]
parts = [o for o in rig.children if o.type == "MESH"]
D = math.radians

# Rotations about each bone's local Z (the body's right for limbs, so a
# positive angle swings the limb forward... or back; see the poses) and X.
STRIDE = {
    "thigh.L": (0, 0, D(-28)), "shin.L": (0, 0, D(10)),
    "foot.L": (D(12), 0, 0),
    "thigh.R": (0, 0, D(22)), "shin.R": (0, 0, D(38)),
    "foot.R": (D(-10), 0, 0),
    "upper_arm.L": (0, 0, D(20)), "forearm.L": (0, 0, D(-25)),
    "upper_arm.R": (0, 0, D(-28)), "forearm.R": (0, 0, D(-45)),
    "spine": (D(6), 0, 0), "head": (D(-6), 0, 0),
}
JUMP = {
    "thigh.L": (0, 0, D(-80)), "shin.L": (0, 0, D(95)),
    "thigh.R": (0, 0, D(-30)), "shin.R": (0, 0, D(70)),
    "upper_arm.L": (0, 0, D(-60)), "forearm.L": (0, 0, D(-50)),
    "upper_arm.R": (0, 0, D(-60)), "forearm.R": (0, 0, D(-50)),
    "spine": (D(12), 0, 0),
}
REACH = {
    "upper_arm.L": (0, 0, D(-160)), "forearm.L": (0, 0, D(-20)),
    "upper_arm.R": (0, 0, D(-160)), "forearm.R": (0, 0, D(-20)),
    "thigh.L": (0, 0, D(10)), "thigh.R": (0, 0, D(10)),
    "shin.L": (0, 0, D(20)), "shin.R": (0, 0, D(20)),
}

LINEUP = [
    (0, {}), (35, {}), (90, {}), (180, {}),
    (70, STRIDE), (70, JUMP), (35, REACH),
]


def copy(x, angle, pose):
    r = rig.copy()
    scene.collection.objects.link(r)
    for part in parts:
        b = part.copy()
        scene.collection.objects.link(b)
        b.parent = r
        b.modifiers["rig"].object = r
    r.location = (x, 0, 0)
    r.rotation_euler = (0, 0, D(angle))
    r.show_in_front = False
    r.hide_render = True
    for name, angles in pose.items():
        pb = r.pose.bones[name]
        pb.rotation_mode = "XYZ"
        pb.rotation_euler = Euler(angles, "XYZ")
    return r


spacing = 0.85
for i, (angle, pose) in enumerate(LINEUP):
    copy((i - (len(LINEUP) - 1) / 2) * spacing, angle, pose)
for o in [rig] + parts:
    o.hide_render = True
    o.hide_viewport = True

bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, 0))
ground = bpy.context.active_object
grass = bpy.data.materials.new("grass")
grass.diffuse_color = (0.08, 0.2, 0.04, 1)
grass.use_nodes = True
grass.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value \
    = (0.08, 0.2, 0.04, 1)
ground.data.materials.append(grass)

sun_data = bpy.data.lights.new("sun", "SUN")
sun_data.energy = 4.0
sun_data.angle = D(4)
sun = bpy.data.objects.new("sun", sun_data)
sun.rotation_euler = (D(50), D(10), D(-35))
scene.collection.objects.link(sun)

world = bpy.data.worlds.new("sky")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Color"].default_value = \
    (0.45, 0.6, 0.85, 1)
world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.9
scene.world = world

cam_data = bpy.data.cameras.new("cam")
cam_data.type = "ORTHO"
cam_data.ortho_scale = spacing * len(LINEUP) + 0.2
cam = bpy.data.objects.new("cam", cam_data)
cam.location = (0, -12, 1.6)
cam.rotation_euler = (D(87), 0, 0)
scene.collection.objects.link(cam)
scene.camera = cam

scene.render.resolution_x = 2800
scene.render.resolution_y = 1150
scene.render.filepath = out
scene.view_settings.view_transform = "Standard"
try:
    scene.render.engine = "BLENDER_EEVEE"
    scene.eevee.taa_render_samples = 32
except TypeError:
    scene.render.engine = "BLENDER_WORKBENCH"
bpy.ops.render.render(write_still=True)
print(f"wrote {out}")
