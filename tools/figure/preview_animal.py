"""Renders a turnaround of an animal's .blend with all four legs.

    Blender -b models/horse.blend --python tools/figure/preview_animal.py \\
      -- OUT.png

An animal's .blend holds the legs of its left side only (the game draws
them on both); this copies them across to the right before rendering the
turnaround of tools/figure/preview_model.py. The .blend is not saved.
"""

import os

import bpy
from mathutils import Matrix

LEGS = ("fore_upper", "fore_lower", "hind_upper", "hind_lower")

bpy.context.view_layer.update()
flip = Matrix.Scale(-1, 4, (1, 0, 0))
for obj in list(bpy.data.objects):
    if obj.type != "MESH" or obj.parent is None:
        continue
    if obj.parent.get("moppe_mesh") not in LEGS:
        continue
    copy = obj.copy()
    copy.data = obj.data.copy()
    copy.parent = None
    obj.users_collection[0].objects.link(copy)
    copy.matrix_world = flip @ obj.matrix_world

exec(open(os.path.join(os.path.dirname(__file__),
                       "preview_model.py")).read())
