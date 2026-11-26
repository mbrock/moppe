"""Renders one studio portrait of a model .blend, for the README.

    Blender -b models/bike.blend --python tools/figure/beauty.py \\
      -- OUT.png [AZIMUTH [ELEVATION [FLOOR]]]

A three-quarter view (azimuth from the model's front, Blender -y, in
degrees; elevation above the horizon) through an 85 mm lens onto a seamless
curved backdrop, lit by a warm low sun, a soft fill, and a cool rim, in
Cycles with AgX. Renders on a GPU when Cycles finds one (HIP, CUDA,
OptiX), otherwise on every CPU core. MOPPE_BEAUTY_SAMPLES and
MOPPE_BEAUTY_SIZE (WIDTHxHEIGHT) override the defaults. The .blend is not
saved.
"""

import math
import os
import sys

import bpy
from mathutils import Vector

args = sys.argv[sys.argv.index("--") + 1:]
out = args[0]
azimuth = float(args[1]) if len(args) > 1 else -35.0
elevation = float(args[2]) if len(args) > 2 else 12.0
width, height = (int(n) for n in
                 os.environ.get("MOPPE_BEAUTY_SIZE", "1600x1000").split("x"))
samples = int(os.environ.get("MOPPE_BEAUTY_SAMPLES", "256"))
scene = bpy.context.scene
D = math.radians

meshes = [o for o in scene.objects if o.type == "MESH" and not o.hide_render]
corners = [o.matrix_world @ Vector(c) for o in meshes for c in o.bound_box]
lo = Vector([min(c[i] for c in corners) for i in range(3)])
hi = Vector([max(c[i] for c in corners) for i in range(3)])
centre = (lo + hi) / 2
radius = (hi - lo).length / 2
# The floor is under the model's lowest point unless FLOOR names the
# height it rests at (the skiff on its hull, its long-tail's skeg below).
floor = float(args[3]) if len(args) > 3 else lo.z
a, e = D(azimuth), D(elevation)
toward = Vector((math.sin(a) * math.cos(e), -math.cos(a) * math.cos(e),
                 math.sin(e)))


def material(name, colour, roughness):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*colour, 1)
    bsdf.inputs["Roughness"].default_value = roughness
    return m


# The backdrop: a floor that curves up into a wall behind the model, seen
# from the camera's side, so there is no horizon.
reach = radius * 14
bend = radius * 4
profile = [(-reach, 0.0), (bend * 0.2, 0.0)]
for i in range(1, 17):
    t = D(90) * i / 16
    profile.append((bend * 0.2 + bend * math.sin(t),
                    bend * (1 - math.cos(t))))
profile.append((bend * 1.2, reach))
half = reach
verts = [(x, y, z) for x in (-half, half) for y, z in profile]
n = len(profile)
faces = [(i, i + 1, n + i + 1, n + i) for i in range(n - 1)]
mesh = bpy.data.meshes.new("backdrop")
mesh.from_pydata(verts, [], faces)
mesh.shade_smooth()
backdrop = bpy.data.objects.new("backdrop", mesh)
backdrop.data.materials.append(material("backdrop", (0.06, 0.055, 0.05),
                                        0.6))
# Its wall stands away from the camera, square to the view.
backdrop.rotation_euler = (0, 0, math.atan2(toward.y, toward.x) + D(90))
backdrop.location = (centre.x, centre.y, floor)
scene.collection.objects.link(backdrop)


def light(name, kind, energy, colour, direction, distance=0.0, size=0.0,
          angle=0.0):
    data = bpy.data.lights.new(name, kind)
    data.energy = energy
    data.color = colour
    if kind == "SUN":
        data.angle = D(angle)
    else:
        data.shape = "DISK"
        data.size = size
    obj = bpy.data.objects.new(name, data)
    d = direction.normalized()
    obj.location = centre + d * distance
    obj.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
    scene.collection.objects.link(obj)


def around(degrees, up):
    """The direction from the model at an azimuth from the camera's."""
    t = math.atan2(toward.y, toward.x) + D(degrees)
    return Vector((math.cos(t), math.sin(t), up))


light("key", "SUN", 4.0, (1.0, 0.84, 0.66), around(-60, 0.8), angle=4)
light("fill", "AREA", 250 * radius ** 2, (0.8, 0.88, 1.0), around(40, 0.3),
      distance=radius * 5, size=radius * 6)
light("rim", "AREA", 900 * radius ** 2, (0.7, 0.82, 1.0),
      around(165, 0.7), distance=radius * 5, size=radius * 2.5)

world = bpy.data.worlds.new("studio")
world.use_nodes = True
bg = world.node_tree.nodes["Background"]
bg.inputs["Color"].default_value = (0.5, 0.56, 0.66, 1)
bg.inputs["Strength"].default_value = 0.12
scene.world = world

cam_data = bpy.data.cameras.new("cam")
cam_data.lens = 85
cam_data.sensor_fit = "HORIZONTAL"
cam_data.sensor_width = 36
cam = bpy.data.objects.new("cam", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam
scene.render.resolution_x = width
scene.render.resolution_y = height
# Aim at the model's box and back off until its corners, as projected,
# fill most of the frame; then centre the frame on them.
cam.rotation_euler = (-toward).to_track_quat("-Z", "Y").to_euler()
distance = radius * 6
for _ in range(2):
    cam.location = centre + toward * distance
    bpy.context.view_layer.update()
    seen = [cam.matrix_world.inverted() @ c for c in corners]
    xs = [p.x / -p.z for p in seen]
    ys = [p.y / -p.z for p in seen]
    span_x = (max(xs) - min(xs)) / 2 / (18 / 85)
    span_y = (max(ys) - min(ys)) / 2 / (18 / 85 * height / width)
    distance *= max(span_x / 0.84, span_y / 0.8)
cam.location = centre + toward * distance
bpy.context.view_layer.update()
seen = [cam.matrix_world.inverted() @ c for c in corners]
xs = [p.x / -p.z for p in seen]
ys = [p.y / -p.z for p in seen]
cam_data.shift_x = (max(xs) + min(xs)) / 2 / (36 / 85)
cam_data.shift_y = (max(ys) + min(ys)) / 2 / (36 / 85)
cam_data.dof.use_dof = True
cam_data.dof.focus_distance = (cam.location - centre).length
cam_data.dof.aperture_fstop = 8.0
cam_data.clip_end = reach * 4

scene.render.engine = "CYCLES"
scene.cycles.samples = samples
scene.cycles.use_denoising = True
scene.cycles.max_bounces = 8
scene.cycles.transparent_max_bounces = 16
prefs = bpy.context.preferences.addons["cycles"].preferences
scene.cycles.device = "CPU"
for kind in ("OPTIX", "CUDA", "HIP", "ONEAPI", "METAL"):
    try:
        prefs.compute_device_type = kind
    except TypeError:
        continue
    prefs.get_devices()
    gpus = [d for d in prefs.devices if d.type == kind]
    if gpus:
        for d in prefs.devices:
            d.use = d.type == kind
        scene.cycles.device = "GPU"
        break
print(f"beauty: Cycles on {scene.cycles.device}")
scene.render.resolution_percentage = 100
scene.view_settings.view_transform = "AgX"
try:
    scene.view_settings.look = "AgX - Medium High Contrast"
except TypeError:
    pass
scene.render.image_settings.file_format = "PNG"
scene.render.filepath = out
bpy.ops.render.render(write_still=True)
print(f"wrote {out}")
