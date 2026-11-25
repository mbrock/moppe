"""Exports a rigid model's assemblies and points to Arrow bundles and C++.

    nix run .#blender -- -b models/bike.blend --python \\
      tools/figure/export_model.py -- bike_model

writes compressed Arrow IPC bundles under data/models/bike/
({vertices,triangles,paints[,film]}.arrows) using moppe_export + pyarrow,
and a tiny header-only moppe/game/bike_model.hh in namespace
moppe::game::bike_model carrying the named points and assembly slices.

Every empty with a `moppe_mesh` property becomes a model_mesh::Mesh of
that name: its children's evaluated meshes (subdivision and bevels
applied) in the empty's own frame, converted to the game's (x right, y up,
z forward), with per-corner normals so smooth and hard-edged parts both
shade as modelled, baked per-vertex ambient occlusion from deterministic
cosine-weighted hemisphere raycasting, and a palette entry per material:
its viewport colour, unlit when the material's `moppe_unlit` is set, and
the rest of its Principled BSDF (metallic, roughness, clear coat,
transmission, index of refraction, sheen, emission) for the renderer to
shade by. A mesh with a "wear" corner attribute and a "film" UV map (a
weathered film, tools/figure/boat.py) writes film.arrows over the same
vertex domain. Every empty with a `moppe_point` property becomes a Vec3 of
its location in model space.
"""

import math
import os
import sys

import bpy
from mathutils.bvhtree import BVHTree
import moppe_export
import numpy as np
import pyarrow as pa

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

# Static, symmetric model-space assemblies that may occlude one another
# during AO baking (chassis + seats + cage, or a building + its foundation).
# Spinning, articulated, removable, or single-instance mirrored parts
# (wheels, rotors, doors, hardtop, animal limbs) occlude only against
# themselves so they never carry asymmetric or rotating shadows.
STATIC_ASSEMBLIES = {
    "body", "chassis", "frame", "hull", "cage", "seats", "glider",
    "barn", "barn_base", "barn_mid", "barn_far", "barn_base_far",
    "house", "house_base", "house_mid", "house_far", "house_base_far",
    "mill", "mill_base", "mill_mid", "mill_far", "mill_base_far",
}

AO_RAYS = 64
_GOLDEN_ANGLE = math.pi * (3.0 - math.sqrt(5.0))
_HEMISPHERE = tuple(
    (math.sqrt((i + 0.5) / AO_RAYS) * math.cos(i * _GOLDEN_ANGLE),
     math.sqrt((i + 0.5) / AO_RAYS) * math.sin(i * _GOLDEN_ANGLE),
     math.sqrt(1.0 - (i + 0.5) / AO_RAYS))
    for i in range(AO_RAYS)
)


def game(v):
    return (v[0], v[2], -v[1])


def to_display(c):
    c = max(0.0, min(1.0, c))
    return c * 12.92 if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def f(x):
    s = f"{x:.4f}".rstrip("0")
    s = s + "0" if s.endswith(".") else s
    return ("0.0" if s in ("-0.0", "0.0") else s) + "f"


# Principled BSDF inputs written beside each paint's colour, with the value
# used when the material has no Principled BSDF.
SURFACE = (("metallic", "Metallic", 0.0),
           ("roughness", "Roughness", 0.8),
           ("coat", "Coat Weight", 0.0),
           ("coat_roughness", "Coat Roughness", 0.03),
           ("transmission", "Transmission Weight", 0.0),
           ("ior", "IOR", 1.5),
           ("sheen", "Sheen Weight", 0.0),
           ("emission", "Emission Strength", 0.0))
TRANSMISSION_INDEX = 4


def surface(mat):
    tree = mat.node_tree
    bsdf = next((n for n in tree.nodes if n.type == "BSDF_PRINCIPLED"),
                None) if tree else None
    values = []
    for _, socket, default in SURFACE:
        value = default
        if bsdf is not None and socket in bsdf.inputs:
            value = bsdf.inputs[socket].default_value
        elif socket == "Metallic":
            value = mat.metallic
        elif socket == "Roughness":
            value = mat.roughness
        values.append(round(float(value), 4))
    return tuple(values)


def is_glass_key(paint_key):
    _, _, unlit, props = paint_key
    return props[TRANSMISSION_INDEX] > 0.5 and not unlit


def export_mesh(empty, graph, palette):
    to_local = empty.matrix_world.inverted()
    vertices, index, triangles = [], {}, []
    world_vertices, opaque_tris = [], []
    vertex_opaque = []
    for obj in sorted(empty.children, key=lambda o: o.name):
        if obj.type != "MESH":
            continue
        evaluated = obj.evaluated_get(graph)
        mesh = evaluated.to_mesh()
        world_m = obj.matrix_world
        world_turn = world_m.to_3x3().inverted().transposed()
        matrix = to_local @ world_m
        turn = matrix.to_3x3().inverted().transposed()
        mesh.calc_loop_triangles()
        normals = mesh.corner_normals
        worn = mesh.attributes.get("wear")
        flat = mesh.uv_layers.get("film")
        for t in mesh.loop_triangles:
            mat = mesh.materials[t.material_index]
            key = (mat.name,
                   tuple(round(to_display(c), 4)
                         for c in mat.diffuse_color[:3]),
                   bool(mat.get("moppe_unlit", False)), surface(mat))
            if key not in palette:
                palette.append(key)
            glass = is_glass_key(key)
            corners = []
            for loop in t.loops:
                v = mesh.loops[loop].vertex_index
                co = mesh.vertices[v].co
                p = game(matrix @ co)
                n = game((turn @ normals[loop].vector).normalized())
                wp = world_m @ co
                wn = (world_turn @ normals[loop].vector).normalized()
                film = None
                if worn is not None and flat is not None:
                    film = (round(worn.data[loop].value, 3),
                            tuple(round(x, 4) for x in flat.data[loop].uv))
                k = (obj.name, v, tuple(round(x, 3) for x in n), film)
                if k not in index:
                    index[k] = len(vertices)
                    vertices.append((p, n, film))
                    world_vertices.append(((wp.x, wp.y, wp.z),
                                           (wn.x, wn.y, wn.z)))
                    vertex_opaque.append(not glass)
                elif not glass:
                    vertex_opaque[index[k]] = True
                corners.append(index[k])
            triangles.append((*corners, palette.index(key)))
            if not glass:
                opaque_tris.append(tuple(corners))
        evaluated.to_mesh_clear()
    if len(vertices) > 65535:
        raise SystemExit(f"{empty.name}: too many vertices")
    return vertices, triangles, world_vertices, opaque_tris, vertex_opaque


def build_bvh(groups):
    verts, polys = [], []
    for world_vertices, opaque_tris in groups:
        if not opaque_tris:
            continue
        base = len(verts)
        verts.extend(wp for wp, _ in world_vertices)
        polys.extend((base + a, base + b, base + c) for a, b, c in opaque_tris)
    if not polys:
        return None
    return BVHTree.FromPolygons(verts, polys)


def bake_ao(world_vertices, triangles, vertex_opaque, bvh):
    if bvh is None or not world_vertices:
        return [1.0] * len(world_vertices)
    xs = [wp[0] for wp, _ in world_vertices]
    ys = [wp[1] for wp, _ in world_vertices]
    zs = [wp[2] for wp, _ in world_vertices]
    diag = math.sqrt((max(xs) - min(xs)) ** 2
                     + (max(ys) - min(ys)) ** 2
                     + (max(zs) - min(zs)) ** 2)
    max_dist = max(0.35, min(1.5, diag * 0.35))
    eps = max(0.0015, max_dist * 0.0025)
    inv_max = 1.0 / max_dist
    inv_rays = 1.0 / AO_RAYS
    ray_cast = bvh.ray_cast

    # Pull each vertex's sample point slightly onto its incident faces (up to
    # 8 cm on long coarse planks, capped at 25% of edge length on fine meshes)
    # projected onto the tangent plane. This keeps corners of long boxes
    # (trailer planks, barn walls) from sampling inside overlapping posts.
    n_verts = len(world_vertices)
    pull_x = [0.0] * n_verts
    pull_y = [0.0] * n_verts
    pull_z = [0.0] * n_verts
    pull_w = [0.0] * n_verts
    for a, b, c, _ in triangles:
        pa = world_vertices[a][0]
        pb = world_vertices[b][0]
        pc = world_vertices[c][0]
        cx = (pa[0] + pb[0] + pc[0]) / 3.0
        cy = (pa[1] + pb[1] + pc[1]) / 3.0
        cz = (pa[2] + pb[2] + pc[2]) / 3.0
        ux, uy, uz = pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]
        vx, vy, vz = pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]
        area = math.sqrt((uy * vz - uz * vy) ** 2
                         + (uz * vx - ux * vz) ** 2
                         + (ux * vy - uy * vx) ** 2)
        if area <= 1e-10:
            continue
        for idx, p in ((a, pa), (b, pb), (c, pc)):
            dx, dy, dz = cx - p[0], cy - p[1], cz - p[2]
            d = math.sqrt(dx * dx + dy * dy + dz * dz)
            if d > 1e-6:
                s = min(0.25, 0.08 / d) * area
                pull_x[idx] += dx * s
                pull_y[idx] += dy * s
                pull_z[idx] += dz * s
                pull_w[idx] += area

    out = []
    for idx, ((wp, wn), opaque) in enumerate(zip(world_vertices, vertex_opaque)):
        if not opaque:
            out.append(1.0)
            continue
        nx, ny, nz = wn
        px, py, pz = wp
        w = pull_w[idx]
        if w > 0.0:
            ix = pull_x[idx] / w
            iy = pull_y[idx] / w
            iz = pull_z[idx] / w
            dot_n = ix * nx + iy * ny + iz * nz
            px += ix - dot_n * nx
            py += iy - dot_n * ny
            pz += iz - dot_n * nz
        # Branchless orthonormal basis around the shading normal (Duff 2017).
        sign = 1.0 if nz >= 0.0 else -1.0
        a = -1.0 / (sign + nz)
        b = nx * ny * a
        tx = 1.0 + sign * nx * nx * a
        ty = sign * b
        tz = -sign * nx
        bx = b
        by = sign + ny * ny * a
        bz = -ny
        origin = (px + nx * eps, py + ny * eps, pz + nz * eps)
        occ = 0.0
        for lx, ly, lz in _HEMISPHERE:
            dx = tx * lx + bx * ly + nx * lz
            dy = ty * lx + by * ly + ny * lz
            dz = tz * lx + bz * ly + nz * lz
            direction = (dx, dy, dz)
            hit_loc, hit_norm, _, dist = ray_cast(origin, direction, max_dist)
            if dist is not None and dist < 0.08:
                # Vertex slightly embedded in an overlapping part: skip its
                # exit backface and continue from just outside.
                if dx * hit_norm.x + dy * hit_norm.y + dz * hit_norm.z > 0.0:
                    rem = max_dist - dist
                    if rem > eps:
                        next_origin = (hit_loc.x + dx * eps,
                                       hit_loc.y + dy * eps,
                                       hit_loc.z + dz * eps)
                        _, _, _, dist2 = ray_cast(next_origin, direction, rem)
                        dist = (dist + dist2) if dist2 is not None else None
                    else:
                        dist = None
            if dist is not None:
                occ += 1.0 - dist * inv_max
        ao = max(0.0, min(1.0, 1.0 - occ * inv_rays))
        # Snap to 1/256 steps so Fixed16 stores exact 8-bit smoothness and
        # compresses tightly under ZSTD.
        out.append(round(ao * 256.0) / 256.0)
    return out


def write_arrows(path, exported):
    batch = pa.record_batch(exported)
    options = pa.ipc.IpcWriteOptions(
        compression=pa.Codec("zstd", compression_level=19))
    with pa.OSFile(path, "wb") as sink:
        with pa.ipc.new_stream(sink, batch.schema, options=options) as writer:
            writer.write_batch(batch)


def main():
    stem = sys.argv[sys.argv.index("--") + 1]
    model = stem.removesuffix("_model")
    out_hh = os.path.join(ROOT, "moppe", "game", f"{stem}.hh")
    out_dir = os.path.join(ROOT, "data", "models", model)
    blend = os.path.basename(bpy.data.filepath)
    graph = bpy.context.evaluated_depsgraph_get()

    palette, meshes = [], []
    for empty in sorted(bpy.data.objects, key=lambda o: o.name):
        if empty.type == "EMPTY" and "moppe_mesh" in empty:
            meshes.append((empty["moppe_mesh"],
                           *export_mesh(empty, graph, palette)))
    points = sorted((o["moppe_point"], game(o.matrix_world.translation))
                    for o in bpy.data.objects
                    if o.type == "EMPTY" and "moppe_point" in o)

    static_bvh = build_bvh([
        (wv, ot) for name, _, _, wv, ot, _ in meshes
        if name in STATIC_ASSEMBLIES
    ])

    all_positions, all_normals, all_ao = [], [], []
    all_film_uvs, all_wear = [], []
    all_corners, all_paints = [], []
    slices = []
    has_film = False

    for name, vertices, triangles, world_vertices, opaque_tris, vertex_opaque in meshes:
        bvh = (static_bvh if name in STATIC_ASSEMBLIES
               else build_bvh([(world_vertices, opaque_tris)]))
        ao_values = bake_ao(world_vertices, triangles, vertex_opaque, bvh)
        first_vertex = len(all_positions)
        first_triangle = len(all_corners)
        for (p, n, film), ao in zip(vertices, ao_values):
            all_positions.append(p)
            all_normals.append(n)
            all_ao.append(ao)
            if film is not None and film[0] >= 0:
                has_film = True
                all_film_uvs.append(film[1])
                all_wear.append(film[0])
            else:
                all_film_uvs.append((0.0, 0.0))
                all_wear.append(-1.0)
        for a, b, c, paint in triangles:
            all_corners.append((a, b, c))
            all_paints.append(paint)
        slices.append((name, first_vertex, len(vertices),
                       first_triangle, len(triangles)))

    os.makedirs(out_dir, exist_ok=True)

    positions = np.ascontiguousarray(all_positions, dtype=np.float32).reshape(-1, 3)
    normals = np.ascontiguousarray(all_normals, dtype=np.float32).reshape(-1, 3)
    ao = np.ascontiguousarray(all_ao, dtype=np.float32).reshape(-1)
    write_arrows(os.path.join(out_dir, "vertices.arrows"),
                 moppe_export.vertices(positions, normals, ao))

    film_path = os.path.join(out_dir, "film.arrows")
    if has_film:
        uvs = np.ascontiguousarray(all_film_uvs, dtype=np.float32).reshape(-1, 2)
        wear = np.ascontiguousarray(all_wear, dtype=np.float32).reshape(-1)
        write_arrows(film_path, moppe_export.film(uvs, wear))
    elif os.path.exists(film_path):
        os.remove(film_path)

    corners = np.ascontiguousarray(all_corners, dtype=np.uint16).reshape(-1, 3)
    paints = np.ascontiguousarray(all_paints, dtype=np.uint8).reshape(-1)
    write_arrows(os.path.join(out_dir, "triangles.arrows"),
                 moppe_export.triangles(corners, paints))

    colours = np.ascontiguousarray([p[1] for p in palette], dtype=np.float32).reshape(-1, 3)
    unlit = np.ascontiguousarray([1 if p[2] else 0 for p in palette], dtype=np.uint8)
    bsdf = [np.ascontiguousarray([p[3][i] for p in palette], dtype=np.float32)
            for i in range(len(SURFACE))]
    write_arrows(os.path.join(out_dir, "paints.arrows"),
                 moppe_export.paints(colours, unlit, *bsdf))

    guard = f"MOPPE_GAME_{stem.upper()}_HH"
    film_flag = "true" if has_film else "false"
    hh = [
        f"// Generated by tools/figure/export_model.py from models/{blend}.",
        "// Do not edit; edit the model and re-export.",
        "",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <moppe/game/model_mesh.hh>",
        "",
        f"namespace moppe::game::{stem} {{",
        "  // Assemblies, each in its own frame.",
    ]
    for name, fv, nv, ft, nt in slices:
        hh.append(
            f'  inline constexpr model_mesh::Mesh {name} {{ "{model}", '
            f"{fv}u, {nv}u, {ft}u, {nt}u, {film_flag} }};"
        )
    hh += ["", "  // Points in model space."]
    for name, p in points:
        hh.append(f"  inline const Vec3 {name} ({', '.join(f(x) for x in p)});")
    hh += ["}", "", "#endif", ""]

    with open(out_hh, "w") as out:
        out.write("\n".join(hh))

    total_bytes = sum(
        os.path.getsize(os.path.join(out_dir, fn))
        for fn in os.listdir(out_dir) if fn.endswith(".arrows")
    )
    ao_min = float(ao.min()) if len(ao) else 1.0
    ao_mean = float(ao.mean()) if len(ao) else 1.0
    counts = ", ".join(f"{n} {nt}" for n, _, _, _, nt in slices)
    print(f"wrote {out_dir} ({total_bytes} bytes, ao min={ao_min:.2f} "
          f"mean={ao_mean:.2f}) + {out_hh}: triangles {counts}; "
          f"{len(points)} points")


if __name__ == "__main__":
    main()
