#include <moppe/color.hh>
#include <moppe/game/model_bundle.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/platform/platform.hh>

#include <array>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

namespace moppe::game::model_mesh {
  namespace {
    struct LoadedModel {
      VertexBundle vertices;
      TriangleBundle triangles;
      PaintBundle paints;
      std::optional<FilmBundle> film;
    };

    template <typename BundleType>
    BundleType read_model_bundle (std::string_view model,
                                  std::string_view domain) {
      const std::string relative = "data/models/" + std::string (model) + "/" +
                                   std::string (domain) + ".arrows";
      const std::string path = platform::asset_path (relative);
      std::ifstream input (path, std::ios::binary);
      if (!input)
        throw std::runtime_error ("could not open model bundle: " + path);
      std::optional<BundleType> loaded =
        spatial::read_bundle<BundleType> (input);
      if (!loaded)
        throw std::runtime_error ("invalid model bundle: " + path);
      return std::move (*loaded);
    }

    struct ModelCache {
      std::mutex mutex;
      std::string name;
      std::optional<LoadedModel> model;
    };

    ModelCache& model_cache () {
      static ModelCache cache;
      return cache;
    }

    const LoadedModel& load_model_locked (ModelCache& cache,
                                          std::string_view model,
                                          bool need_film) {
      if (cache.model && cache.name == model &&
          (!need_film || cache.model->film.has_value ()))
        return *cache.model;

      LoadedModel loaded {
        read_model_bundle<VertexBundle> (model, "vertices"),
        read_model_bundle<TriangleBundle> (model, "triangles"),
        read_model_bundle<PaintBundle> (model, "paints"),
        need_film ? std::optional<FilmBundle> (
                      read_model_bundle<FilmBundle> (model, "film"))
                  : std::nullopt,
      };
      if (loaded.film && loaded.film->size () != loaded.vertices.size ())
        throw std::runtime_error (
          "model film domain disagrees with vertices: " + std::string (model));
      cache.name = std::string (model);
      cache.model = std::move (loaded);
      return *cache.model;
    }
  }

  void clear_cache () {
    ModelCache& cache = model_cache ();
    const std::lock_guard lock (cache.mutex);
    cache.name.clear ();
    cache.model.reset ();
  }

  void record (render::DrawList& dl, const Mesh& mesh, bool reversed) {
    ModelCache& cache = model_cache ();
    const std::lock_guard lock (cache.mutex);
    const LoadedModel& loaded =
      load_model_locked (cache, mesh.model, mesh.has_film);

    if (static_cast<std::size_t> (mesh.first_vertex) + mesh.vertex_count >
          loaded.vertices.size () ||
        static_cast<std::size_t> (mesh.first_triangle) + mesh.triangle_count >
          loaded.triangles.size ())
      throw std::out_of_range ("model assembly slice out of bounds: " +
                               std::string (mesh.model));

    const auto& positions = spatial::get<vertex_position> (loaded.vertices);
    const auto& normals = spatial::get<vertex_normal> (loaded.vertices);
    const auto& aos = spatial::get<ambient_occlusion> (loaded.vertices);
    const auto& corner_as = spatial::get<corner_a> (loaded.triangles);
    const auto& corner_bs = spatial::get<corner_b> (loaded.triangles);
    const auto& corner_cs = spatial::get<corner_c> (loaded.triangles);
    const auto& paint_ids = spatial::get<paint_index> (loaded.triangles);
    const auto& colours = spatial::get<paint_colour> (loaded.paints);
    const auto& unlits = spatial::get<paint_unlit> (loaded.paints);
    const auto& metallics = spatial::get<paint_metallic> (loaded.paints);
    const auto& roughnesses = spatial::get<paint_roughness> (loaded.paints);
    const auto& coats = spatial::get<paint_coat> (loaded.paints);
    const auto& coat_roughnesses =
      spatial::get<paint_coat_roughness> (loaded.paints);
    const auto& transmissions =
      spatial::get<paint_transmission> (loaded.paints);
    const auto& sheens = spatial::get<paint_sheen> (loaded.paints);
    const auto* film_uvs =
      mesh.has_film ? &spatial::get<film_coordinates> (*loaded.film) : nullptr;
    const auto* wears =
      mesh.has_film ? &spatial::get<surface_wear> (*loaded.film) : nullptr;

    // What lets light through (glass, a lamp's lens is lit) is drawn
    // blended and leaves the depth to what stands behind it: its colour's
    // alpha is how much of its own tint it puts over that.
    const auto see_through = [&] (std::size_t p) {
      return transmissions[p] > 0.5f * proportion[one] &&
             unlits[p].numerical_value_in (one) == 0;
    };
    const render::DrawState before = dl.state ();
    bool has_glass = false;
    for (std::size_t p = 0; p < loaded.paints.size (); ++p)
      if (see_through (p)) {
        has_glass = true;
        break;
      }

    const proportion_t opaque = 1.0f * proportion[one];
    const std::size_t end_triangle =
      static_cast<std::size_t> (mesh.first_triangle) + mesh.triangle_count;
    for (const bool clear : { false, true }) {
      if (clear && !has_glass)
        break;
      render::DrawState state = before;
      state.glass = clear;
      state.blend = clear || before.blend;
      state.depth_write = !clear && before.depth_write;
      dl.state (state);
      dl.begin (render::Prim::Triangles);
      for (std::size_t t = mesh.first_triangle; t < end_triangle; ++t) {
        const std::size_t p = paint_ids[t].numerical_value_in (one);
        if (p >= loaded.paints.size ())
          throw std::out_of_range ("model triangle paint index out of range");
        if (has_glass && see_through (p) != clear)
          continue;
        dl.lit (unlits[p].numerical_value_in (one) == 0);
        const auto& rgb = colours[p].numerical_value_ref_in (one);
        const proportion_t transmission = transmissions[p];
        dl.color (DisplayColor (rgb[0], rgb[1], rgb[2]),
                  clear ? opaque - 0.7f * transmission : opaque);
        dl.material (metallics[p],
                     roughnesses[p],
                     coats[p],
                     sheens[p],
                     coat_roughnesses[p]);
        const std::array<std::uint16_t, 3> corners {
          corner_as[t].numerical_value_in (one),
          reversed ? corner_cs[t].numerical_value_in (one)
                   : corner_bs[t].numerical_value_in (one),
          reversed ? corner_bs[t].numerical_value_in (one)
                   : corner_cs[t].numerical_value_in (one),
        };
        for (const std::uint16_t local : corners) {
          const std::size_t v =
            static_cast<std::size_t> (mesh.first_vertex) + local;
          dl.normal (unit_direction (reversed
                                       ? -normals[v].numerical_value_in (one)
                                       : normals[v].numerical_value_in (one)));
          dl.occlusion (aos[v]);
          if (film_uvs && wears) {
            const auto& uv = (*film_uvs)[v].numerical_value_ref_in (u::m);
            dl.uv (uv[0], uv[1]);
            dl.worn ((*wears)[v].numerical_value_in (one));
          } else {
            dl.uv (0.0f, 0.0f);
            dl.worn (-1.0f);
          }
          dl.vertex (displacement (positions[v].numerical_value_in (u::m)));
        }
      }
      dl.end ();
    }
    dl.state (before);
    dl.lit (true);
    dl.plain ();
    dl.occlusion (1.0f * proportion[one]);
    dl.uv (0.0f, 0.0f);
    dl.worn (-1.0f);
  }

  render::MeshPtr bake (render::Renderer& r, const Mesh& mesh) {
    render::DrawList dl;
    record (dl, mesh);
    return r.create_mesh (dl);
  }

  Mat4 body_frame (const position_t& position,
                   const direction_t& forward,
                   const direction_t& up) {
    // Normalized again: a pose's forward is only nearly unit.
    const direction_t ahead =
      unit_direction (normalized (direction_value (forward)));
    const direction_t left = unit_direction (
      normalized (cross (direction_value (up), direction_value (ahead))));
    return Mat4::translation (position_value (position)) *
           Mat4::basis (direction_value (left),
                        cross (direction_value (ahead), direction_value (left)),
                        direction_value (ahead));
  }
}
