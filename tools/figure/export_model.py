"""Exports a rigid model's assemblies and points to Arrow bundles and C++.

    nix run .#blender -- -b models/bike.blend --python \\
      tools/figure/export_model.py -- bike_model

writes compressed Arrow IPC bundles under data/models/bike/
({vertices,triangles,paints[,film]}.arrows) using moppe_export + pyarrow,
and a tiny header-only moppe/game/bike_model.hh in namespace
moppe::game::bike_model carrying the named points and assembly slices.

Every empty with a `moppe_mesh` property becomes a model_mesh::Assembly of
that name: its children's evaluated meshes (subdivision and bevels
applied) in the empty's own frame, converted to the game's (x right, y up,
z forward), with per-corner normals so smooth and hard-edged parts both
shade as modelled, and a palette entry per material: its viewport colour,
unlit when the material's `moppe_unlit` is set, and the rest of its
Principled BSDF (metallic, roughness, clear coat, transmission, index of
refraction, sheen, emission) for the renderer to shade by. A mesh with a
"wear" corner attribute and a "film" UV map (a weathered film,
tools/figure/boat.py) writes film.arrows over the same vertex domain.
Every empty with a `moppe_point` property becomes a Vec3 of its location
in model space.
"""

import os
import sys

import bpy
import moppe_export
import numpy as np
import pyarrow as pa

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


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


def export_mesh(empty, graph, palette):
    to_local = empty.matrix_world.inverted()
    vertices, index, triangles = [], {}, []
    for obj in sorted(empty.children, key=lambda o: o.name):
        if obj.type != "MESH":
            continue
        evaluated = obj.evaluated_get(graph)
        mesh = evaluated.to_mesh()
        matrix = to_local @ obj.matrix_world
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
            corners = []
            for loop in t.loops:
                v = mesh.loops[loop].vertex_index
                p = game(matrix @ mesh.vertices[v].co)
                n = game((turn @ normals[loop].vector).normalized())
                film = None
                if worn is not None and flat is not None:
                    film = (round(worn.data[loop].value, 3),
                            tuple(round(x, 4) for x in flat.data[loop].uv))
                k = (obj.name, v, tuple(round(x, 3) for x in n), film)
                if k not in index:
                    index[k] = len(vertices)
                    vertices.append((p, n, film))
                corners.append(index[k])
            triangles.append((*corners, palette.index(key)))
        evaluated.to_mesh_clear()
    if len(vertices) > 65535:
        raise SystemExit(f"{empty.name}: too many vertices")
    return vertices, triangles


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

    all_positions, all_normals, all_film_uvs, all_wear = [], [], [], []
    all_corners, all_paints = [], []
    slices = []
    has_film = False

    for name, vertices, triangles in meshes:
        first_vertex = len(all_positions)
        first_triangle = len(all_corners)
        for p, n, film in vertices:
            all_positions.append(p)
            all_normals.append(n)
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
    write_arrows(os.path.join(out_dir, "vertices.arrows"),
                 moppe_export.vertices(positions, normals))

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
    counts = ", ".join(f"{n} {nt}" for n, _, _, _, nt in slices)
    print(f"wrote {out_dir} ({total_bytes} bytes) + {out_hh}: "
          f"triangles {counts}; {len(points)} points")


if __name__ == "__main__":
    main()
