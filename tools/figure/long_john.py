"""Builds models/long_john.blend: Krisjanis's Long John cargo bike.

    Blender -b --factory-startup --python tools/figure/long_john.py

His own bike, a one-off: a donor 26-inch rear triangle in worn red paint
(hub motor, disc brake) welded to a raw-steel front of his own -- a tall
hoop carrying the rider's head tube, an X-braced box with a floor, a front
bulkhead of holed plates -- over a 20-inch front wheel 1.894 m ahead,
steered by cables from the bars. tools/figure/long_john_source.py is the
procedural twin made from his snow photos (19 frame points solved across
four cameras, to about a centimetre; its own git history and checks live
with it), kept as it was written. This script runs its geometry, leaves
out the weld beads and the individual chain links (most of its 390k
triangles), coarsens the tubes and tyres, and regroups the parts the
game's way: in the game's frame (x right, y up, z forward) with the
origin midway between the axles at the rear axle's height, as
mov::long_john_rig has it.

The assemblies are the frame (with the box, the seat, the cables, and
the folded stand), the rear wheel and its cog, the front wheel, the fork
turning about the front head tube, the bars turning about the rider's
head tube (the cables tie them 1:1), the crank, and one pedal; the points
are the axles, both steering axes, the bottom bracket, the crank's end,
the saddle, and the grips. Rebuilding discards hand edits to the .blend.
"""

import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(__file__))
from shapes import V, assembly, blender, material  # noqa

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(ROOT, "models", "long_john.blend")

# The source's colours are linear; the palette is display-referred.
PALETTE = {
    "steel": (0.58, 0.575, 0.57),
    "old_paint": (0.52, 0.30, 0.31),
    "ground_steel": (0.74, 0.74, 0.73),
    "red": (0.77, 0.19, 0.19),
    "alloy": (0.92, 0.92, 0.93),
    "dark_steel": (0.35, 0.35, 0.36),
    "black": (0.10, 0.10, 0.11),
    "panel": (0.17, 0.18, 0.19),
    "grip_tape": (0.19, 0.19, 0.19),
    "rubber": (0.12, 0.12, 0.12),
    "saddle": (0.93, 0.81, 0.25),
    "cable": (0.08, 0.08, 0.08),
}

# Raw mild steel brushed and weathered; the donor frame's old red paint,
# mostly scuffed back to steel; ground welds bright; the rest bike parts.
FINISH = {
    "steel": dict(metallic=0.75, roughness=0.5),
    "old_paint": dict(metallic=0.35, roughness=0.6),
    "ground_steel": dict(metallic=0.9, roughness=0.35),
    "red": dict(roughness=0.45, coat=0.3),
    "alloy": dict(metallic=1.0, roughness=0.25),
    "dark_steel": dict(metallic=0.9, roughness=0.4),
    "black": dict(roughness=0.35, coat=0.2),
    "panel": dict(roughness=0.55),
    "grip_tape": dict(roughness=0.95),
    "rubber": dict(roughness=0.85),
    "saddle": dict(roughness=0.45, coat=0.4),
    "cable": dict(roughness=0.4),
}

# The source's materials, by name, to the palette.
MATERIALS = {
    "Raw steel": "steel",
    "Old frame (worn red)": "old_paint",
    "Weld bead": "ground_steel",
    "Ground steel": "ground_steel",
    "Red paint": "red",
    "Aluminium": "alloy",
    "Dark steel": "dark_steel",
    "Black paint": "black",
    "Box panel": "panel",
    "Floor grip": "grip_tape",
    "Rubber": "rubber",
    "Saddle": "saddle",
    "Cable housing": "cable",
}

# Each rig empty of the source to the assembly its parts go in.
ASSEMBLIES = {
    "Bike": "frame",
    "Stand pivot.L": "frame",
    "Stand pivot.R": "frame",
    "Rear wheel pivot": "rear_wheel",
    "Front wheel pivot": "front_wheel",
    "Front steering": "fork",
    "Rider steering": "bars",
    "Crank pivot": "crank",
    "Pedal pivot.R": "pedal",
    "Pedal pivot.L": None,
}


def source():
    """The source's geometry and rig, run as far as its simulation, with
    the right crank down at rest, coarser tyres, and no weld beads."""
    with open(os.path.join(HERE, "long_john_source.py")) as f:
        text = f.read()
    text = text[:text.index("\nSIM = dict(")]
    for old, new in (
            ("CRANK = math.radians(-20)", "CRANK = math.radians(-90)"),
            ("    sm, sn = 96, 20", "    sm, sn = 48, 10"),
            ("add_welds()\nbuild_welds()", "pass"),
            # Fewer sides on the many small round parts.
            ("def cylinder(name, a, b, radius, material, cname, segs=32,",
             "def cylinder(name, a, b, radius, material, cname, segs=12,"),
            ("M_ALU, cname, segs=64)", "M_ALU, cname, segs=24)"),
            ("cname, segs=48)", "cname, segs=20)"),
            ("cap_ends=True, segments=24,", "cap_ends=True, segments=8,"),
            ("segments=8, radius1=0.0017", "segments=4, radius1=0.0017"),
            ("0.0012, 2, 'ANGLE'", "0.0012, 1, 'ANGLE'"),
    ):
        assert old in text, old
        text = text.replace(old, new)
    namespace = {"__file__": os.path.join(HERE, "long_john_source.py"),
                 "__name__": "long_john_source"}
    argv = sys.argv
    sys.argv = [argv[0]]
    try:
        exec(compile(text, "long_john_source.py", "exec"), namespace)
    finally:
        sys.argv = argv
    return namespace


def game_point(p):
    """The source's frame (x forward, y left, z up, origin on the ground
    under the rear axle) to the game's."""
    return V((-p[1], p[2] - ORIGIN[2], p[0] - ORIGIN[0]))


def to_blender(p):
    return blender(game_point(p))


def assembly_of(obj):
    o = obj.parent
    while o is not None:
        if o.name in ASSEMBLIES:
            return ASSEMBLIES[o.name]
        o = o.parent
    return "frame"


def chain(ns):
    """The chain as one loop along its pitch line, flattened to a strap."""
    path = ns["path"][::6]
    cu = bpy.data.curves.new("chain", "CURVE")
    cu.dimensions = "3D"
    sp = cu.splines.new("POLY")
    sp.points.add(len(path) - 1)
    for pt, p in zip(sp.points, path):
        pt.co = (*p, 1)
    sp.use_cyclic_u = True
    cu.bevel_depth = 0.0045
    cu.bevel_resolution = 0
    obj = bpy.data.objects.new("Chain", cu)
    obj.data.materials.append(bpy.data.materials["Dark steel"])
    obj.scale = (1, 0.6, 1)
    bpy.context.scene.collection.objects.link(obj)
    obj.parent = bpy.data.objects["Bike"]
    return obj


def build():
    global ORIGIN
    ns = source()
    AX_R, AX_F = ns["AX_R"], ns["AX_F"]
    ORIGIN = V(((AX_R.x + AX_F.x) / 2, 0, AX_R.z))

    for obj in list(bpy.data.objects):
        if obj.name.startswith("Chain link"):
            bpy.data.objects.remove(obj)
    chain(ns)
    # Every tube meshed with fewer sides and segments.
    for cu in bpy.data.curves:
        cu.bevel_resolution = min(cu.bevel_resolution, 2)
        cu.resolution_u = min(cu.resolution_u, 4)
    bpy.context.view_layer.update()
    graph = bpy.context.evaluated_depsgraph_get()

    # Every part's evaluated mesh, in the game's frame.
    to_game = Matrix(((0, -1, 0, 0), (-1, 0, 0, 0), (0, 0, 1, 0),
                      (0, 0, 0, 1))) @ Matrix.Translation(-ORIGIN)
    groups = {}
    for obj in bpy.data.objects:
        if obj.type not in ("MESH", "CURVE") or obj.hide_render:
            continue
        if obj.name in ("Snow",) or obj.name.endswith(" cutter"):
            continue
        name = assembly_of(obj)
        if name is None:
            continue
        mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(graph))
        mesh.transform(to_game @ obj.matrix_world)
        # The change of frame is a reflection: turn the faces back out.
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bmesh.ops.reverse_faces(bm, faces=bm.faces)
        bm.to_mesh(mesh)
        bm.free()
        groups.setdefault(name, []).append((obj.name, mesh))

    # The source's rig, as points in the game's frame.
    objects = bpy.data.objects
    head = ns["HEAD_DIR"]
    points = {
        "rear_axle": AX_R,
        "front_axle": AX_F,
        "bottom_bracket": ns["BB"],
        "crank_end": objects["Pedal pivot.R"].matrix_world.translation,
        "front_steering_head": ns["FHT_TOP"],
        "front_steering_top": ns["FHT_TOP"] + head * 0.1,
        "bars_steering_head": ns["HT_BOT"],
        "bars_steering_top": ns["HT_BOT"] + head * 0.1,
        "saddle": objects["Saddle"].matrix_world.translation
        + V((-0.02, 0, 0.03)),
        "grip_left": objects["Grip.L"].matrix_world.translation,
        "grip_right": objects["Grip.R"].matrix_world.translation,
    }
    pedal_y = 0.065
    origins = {
        "frame": ORIGIN,
        "rear_wheel": AX_R,
        "front_wheel": AX_F,
        "fork": ns["FHT_TOP"],
        "bars": ns["HT_BOT"],
        "crank": ns["BB"],
        "pedal": points["crank_end"] + V((0, -pedal_y, 0)),
    }
    points = {k: V(v) for k, v in points.items()}

    # Clear the source's scene, keeping only the meshes taken from it.
    scene = bpy.context.scene
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj)
    for c in list(bpy.data.collections):
        bpy.data.collections.remove(c)
    scene.world = None
    coll = bpy.data.collections.new("long_john")
    scene.collection.children.link(coll)
    m = {name: material(name, c, **FINISH[name])
         for name, c in PALETTE.items()}
    count = 0
    for name, parts in sorted(groups.items()):
        at = to_blender(origins[name])
        asm = assembly(name, coll, game_point(origins[name]), mesh=name)
        for part_name, mesh in parts:
            mesh.transform(Matrix.Translation(-at))
            for i, slot in enumerate(mesh.materials):
                mesh.materials[i] = m[MATERIALS[slot.name]]
            obj = bpy.data.objects.new(part_name, mesh)
            coll.objects.link(obj)
            obj.parent = asm
            count += 1
    for name, p in points.items():
        assembly(name, coll, game_point(p), point=name)
    bpy.data.orphans_purge(do_recursive=True)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=OUT, compress=True)
    print(f"wrote {OUT}: {count} parts")


build()
