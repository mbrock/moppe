// Python extension built for Blender's Python that turns raw mesh arrays
// into Moppe's typed spatial::Bundles and hands them straight to pyarrow
// over the Arrow C Data Interface (PyCapsule protocol).

#include <moppe/game/model_bundle.hh>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace nb = nanobind;

namespace {
  using namespace moppe;
  namespace mm = moppe::game::model_mesh;

  template <typename T, std::ptrdiff_t... Dims>
  using Array =
    nb::ndarray<const T, nb::shape<Dims...>, nb::c_contig, nb::device::cpu>;

  struct ExportedBundle {
    spatial::detail::UniqueSchema schema;
    spatial::detail::UniqueArray array;

    nb::tuple arrow_c_array (nb::object /*requested_schema*/) {
      if (!schema->release || !array->release)
        throw std::runtime_error ("exported bundle already consumed");

      auto* out_schema = new ArrowSchema {};
      auto* out_array = new ArrowArray {};
      ArrowSchemaMove (schema.get (), out_schema);
      ArrowArrayMove (array.get (), out_array);

      nb::capsule schema_cap (
        out_schema, "arrow_schema", [] (void* p) noexcept {
          auto* ptr = static_cast<ArrowSchema*> (p);
          if (ptr->release)
            ptr->release (ptr);
          delete ptr;
        });
      nb::capsule array_cap (out_array, "arrow_array", [] (void* p) noexcept {
        auto* ptr = static_cast<ArrowArray*> (p);
        if (ptr->release)
          ptr->release (ptr);
        delete ptr;
      });
      return nb::make_tuple (std::move (schema_cap), std::move (array_cap));
    }
  };

  template <typename Bundle>
  ExportedBundle export_to_python (const Bundle& bundle,
                                   spatial::FloatStorage floats) {
    ExportedBundle out;
    if (!spatial::export_bundle (
          bundle, *out.schema.get (), *out.array.get (), floats))
      throw std::runtime_error ("failed to export spatial bundle to Arrow");
    return out;
  }

  ExportedBundle export_vertices (Array<float, -1, 3> positions,
                                  Array<float, -1, 3> normals) {
    const std::size_t n = positions.shape (0);
    if (normals.shape (0) != n)
      throw std::invalid_argument ("positions and normals lengths differ");

    mm::VertexBundle bundle (mm::VertexDomain { n });
    auto& pos_col = spatial::get<mm::vertex_position> (bundle);
    auto& norm_col = spatial::get<mm::vertex_normal> (bundle);
    const float* p = positions.data ();
    const float* no = normals.data ();
    for (std::size_t i = 0; i < n; ++i) {
      pos_col[i] =
        mm::VertexPosition (Vec3 (p[3 * i], p[3 * i + 1], p[3 * i + 2]),
                            mm::VertexPosition::reference);
      norm_col[i] =
        mm::VertexNormal (Vec3 (no[3 * i], no[3 * i + 1], no[3 * i + 2]),
                          mm::VertexNormal::reference);
    }
    (void)spatial::quantize_fixed16 (pos_col);
    (void)spatial::quantize_fixed16 (norm_col);
    return export_to_python (bundle, spatial::FloatStorage::fixed_point);
  }

  ExportedBundle export_film (Array<float, -1, 2> uvs, Array<float, -1> wear) {
    const std::size_t n = uvs.shape (0);
    if (wear.shape (0) != n)
      throw std::invalid_argument ("film uvs and wear lengths differ");

    mm::FilmBundle bundle (mm::VertexDomain { n });
    auto& uv_col = spatial::get<mm::film_coordinates> (bundle);
    auto& wear_col = spatial::get<mm::surface_wear> (bundle);
    const float* u = uvs.data ();
    const float* w = wear.data ();
    for (std::size_t i = 0; i < n; ++i) {
      uv_col[i] = mm::FilmCoordinates (Vec2 (u[2 * i], u[2 * i + 1]),
                                       mm::FilmCoordinates::reference);
      wear_col[i] = mm::SurfaceWear (w[i], mm::SurfaceWear::reference);
    }
    (void)spatial::quantize_fixed16 (uv_col);
    (void)spatial::quantize_fixed16 (wear_col);
    return export_to_python (bundle, spatial::FloatStorage::fixed_point);
  }

  ExportedBundle export_triangles (Array<std::uint16_t, -1, 3> corners,
                                   Array<std::uint8_t, -1> paints) {
    const std::size_t n = corners.shape (0);
    if (paints.shape (0) != n)
      throw std::invalid_argument ("corners and paints lengths differ");

    mm::TriangleBundle bundle (mm::TriangleDomain { n });
    auto& ca = spatial::get<mm::corner_a> (bundle);
    auto& cb = spatial::get<mm::corner_b> (bundle);
    auto& cc = spatial::get<mm::corner_c> (bundle);
    auto& pi = spatial::get<mm::paint_index> (bundle);
    const std::uint16_t* c = corners.data ();
    const std::uint8_t* p = paints.data ();
    for (std::size_t i = 0; i < n; ++i) {
      ca[i] = mm::CornerA (c[3 * i], mm::CornerA::reference);
      cb[i] = mm::CornerB (c[3 * i + 1], mm::CornerB::reference);
      cc[i] = mm::CornerC (c[3 * i + 2], mm::CornerC::reference);
      pi[i] = mm::PaintIndex (p[i], mm::PaintIndex::reference);
    }
    return export_to_python (bundle, spatial::FloatStorage::plain);
  }

  ExportedBundle export_paints (Array<float, -1, 3> colours,
                                Array<std::uint8_t, -1> unlit,
                                Array<float, -1> metallic,
                                Array<float, -1> roughness,
                                Array<float, -1> coat,
                                Array<float, -1> coat_roughness,
                                Array<float, -1> transmission,
                                Array<float, -1> ior,
                                Array<float, -1> sheen,
                                Array<float, -1> emission) {
    const std::size_t n = colours.shape (0);
    if (unlit.shape (0) != n || metallic.shape (0) != n ||
        roughness.shape (0) != n || coat.shape (0) != n ||
        coat_roughness.shape (0) != n || transmission.shape (0) != n ||
        ior.shape (0) != n || sheen.shape (0) != n || emission.shape (0) != n)
      throw std::invalid_argument ("paint column lengths differ");

    mm::PaintBundle bundle (mm::PaintDomain { n });
    auto& col_rgb = spatial::get<mm::paint_colour> (bundle);
    auto& col_unlit = spatial::get<mm::paint_unlit> (bundle);
    auto& col_met = spatial::get<mm::paint_metallic> (bundle);
    auto& col_rough = spatial::get<mm::paint_roughness> (bundle);
    auto& col_coat = spatial::get<mm::paint_coat> (bundle);
    auto& col_cr = spatial::get<mm::paint_coat_roughness> (bundle);
    auto& col_trans = spatial::get<mm::paint_transmission> (bundle);
    auto& col_ior = spatial::get<mm::paint_ior> (bundle);
    auto& col_sheen = spatial::get<mm::paint_sheen> (bundle);
    auto& col_emit = spatial::get<mm::paint_emission> (bundle);

    const float* c = colours.data ();
    const std::uint8_t* u = unlit.data ();
    const float* met = metallic.data ();
    const float* rough = roughness.data ();
    const float* ct = coat.data ();
    const float* cr = coat_roughness.data ();
    const float* tr = transmission.data ();
    const float* ir = ior.data ();
    const float* sh = sheen.data ();
    const float* em = emission.data ();

    for (std::size_t i = 0; i < n; ++i) {
      col_rgb[i] = mm::PaintColour (Vec3 (c[3 * i], c[3 * i + 1], c[3 * i + 2]),
                                    mm::PaintColour::reference);
      col_unlit[i] = mm::PaintUnlit (u[i], mm::PaintUnlit::reference);
      col_met[i] = mm::PaintMetallic (met[i], mm::PaintMetallic::reference);
      col_rough[i] =
        mm::PaintRoughness (rough[i], mm::PaintRoughness::reference);
      col_coat[i] = mm::PaintCoat (ct[i], mm::PaintCoat::reference);
      col_cr[i] =
        mm::PaintCoatRoughness (cr[i], mm::PaintCoatRoughness::reference);
      col_trans[i] =
        mm::PaintTransmission (tr[i], mm::PaintTransmission::reference);
      col_ior[i] = mm::PaintIor (ir[i], mm::PaintIor::reference);
      col_sheen[i] = mm::PaintSheen (sh[i], mm::PaintSheen::reference);
      col_emit[i] = mm::PaintEmission (em[i], mm::PaintEmission::reference);
    }
    return export_to_python (bundle, spatial::FloatStorage::plain);
  }
}

NB_MODULE (moppe_export, m) {
  m.doc () = "Exports rigid Moppe models to Arrow bundles via C Data Interface";

  nb::class_<ExportedBundle> (m, "ExportedBundle")
    .def ("__arrow_c_array__",
          &ExportedBundle::arrow_c_array,
          nb::arg ("requested_schema") = nb::none ());

  m.def (
    "vertices", &export_vertices, nb::arg ("positions"), nb::arg ("normals"));
  m.def ("film", &export_film, nb::arg ("uvs"), nb::arg ("wear"));
  m.def (
    "triangles", &export_triangles, nb::arg ("corners"), nb::arg ("paints"));
  m.def ("paints",
         &export_paints,
         nb::arg ("colours"),
         nb::arg ("unlit"),
         nb::arg ("metallic"),
         nb::arg ("roughness"),
         nb::arg ("coat"),
         nb::arg ("coat_roughness"),
         nb::arg ("transmission"),
         nb::arg ("ior"),
         nb::arg ("sheen"),
         nb::arg ("emission"));
}
