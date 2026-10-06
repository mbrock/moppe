"""Renders a turnaround of a model .blend into one image.

    Blender -b models/bike.blend --python tools/figure/preview_model.py \\
      -- OUT.png

Four views around the whole model -- three-quarter front, side, front,
and three-quarter rear from above -- each rendered with a sun and sky and
stitched side by side. The .blend is not saved.
"""

import math
import os
import sys

import bpy
import numpy as np
from mathutils import Vector

out = sys.argv[sys.argv.index("--") + 1]
scene = bpy.context.scene
D = math.radians

meshes = [o for o in scene.objects if o.type == "MESH"]
graph = bpy.context.evaluated_depsgraph_get()
corners = [o.matrix_world @ Vector(c) for o in meshes for c in o.bound_box]
lo = Vector([min(c[i] for c in corners) for i in range(3)])
hi = Vector([max(c[i] for c in corners) for i in range(3)])
centre = (lo + hi) / 2
size = max(hi - lo)

sun_data = bpy.data.lights.new("sun", "SUN")
sun_data.energy = 4.0
sun_data.angle = D(4)
sun = bpy.data.objects.new("sun", sun_data)
sun.rotation_euler = (D(45), D(10), D(-35))
scene.collection.objects.link(sun)
world = bpy.data.worlds.new("sky")
world.use_nodes = True
bg = world.node_tree.nodes["Background"]
bg.inputs["Color"].default_value = (0.45, 0.6, 0.85, 1)
bg.inputs["Strength"].default_value = 0.9
scene.world = world

cam_data = bpy.data.cameras.new("cam")
cam_data.type = "ORTHO"
cam_data.ortho_scale = size * 1.12
cam = bpy.data.objects.new("cam", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam

scene.render.resolution_x = 900
scene.render.resolution_y = 900
scene.render.film_transparent = False
scene.view_settings.view_transform = "Standard"
try:
    scene.render.engine = "BLENDER_EEVEE"
    scene.eevee.taa_render_samples = 32
except TypeError:
    scene.render.engine = "BLENDER_WORKBENCH"

# Azimuths are measured from the model's front (Blender -y), elevation up.
VIEWS = [(-40, 15), (-90, 5), (0, 8), (150, 35)]
tiles = []
for i, (azimuth, elevation) in enumerate(VIEWS):
    a, e = D(azimuth), D(elevation)
    direction = Vector((math.sin(a) * math.cos(e), -math.cos(a) *
                        math.cos(e), math.sin(e)))
    cam.location = centre + direction * size * 4
    cam.rotation_euler = (-direction).to_track_quat("-Z", "Y").to_euler()
    path = f"{out}.{i}.png"
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    image = bpy.data.images.load(path)
    w, h = image.size
    pixels = np.array(image.pixels[:], dtype=np.float32).reshape(h, w, 4)
    tiles.append(pixels)
    bpy.data.images.remove(image)
    os.remove(path)

sheet = np.concatenate(tiles, axis=1)
h, w = sheet.shape[:2]
result = bpy.data.images.new("sheet", w, h, alpha=True)
result.pixels[:] = sheet.ravel()
result.filepath_raw = out
result.file_format = "PNG"
result.save()
print(f"wrote {out}")
