#include <moppe/map/surface.hh>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace moppe::map {
  namespace {
    void require_surface_size (std::span<const float> input,
                               const SurfaceDomain& domain,
                               const char* message) {
      if (input.size () != domain.size ())
        throw std::invalid_argument (message);
    }

    template <typename Values>
    float robust_positive_scale (const Values& values) {
      std::vector<float> positive;
      positive.reserve (values.size () / 8);
      for (const auto value : values) {
        const float scalar = value.numerical_value_in (mp_units::one);
        if (scalar > 0.0f)
          positive.push_back (scalar);
      }
      if (positive.empty ())
        return 1.0f;
      const std::size_t rank = positive.size () * 49 / 50;
      std::nth_element (
        positive.begin (), positive.begin () + rank, positive.end ());
      return std::max (positive[rank], 1e-6f);
    }
  }

  void Surface::materialize_moisture (std::span<const float> moisture) {
    SurfaceAtlas& atlas = mutable_atlas ();
    require_surface_size (
      moisture, atlas.domain (), "Moisture needs one value per surface sample");
    SurfaceMoistureSections& values =
      atlas.hydrology ().materialize_moisture ();
    auto& column = spatial::get<surface_moisture> (values);
    for (std::size_t offset = 0; offset < values.size (); ++offset)
      column[offset] =
        std::clamp (moisture[offset], 0.0f, 1.0f) * surface_moisture[one];
  }

  void
  Surface::materialize_waterline_distance (std::span<const float> distance) {
    SurfaceAtlas& atlas = mutable_atlas ();
    require_surface_size (
      distance,
      atlas.domain (),
      "Waterline distance needs one value per surface sample");
    SurfaceWaterlineSections& values =
      atlas.hydrology ().materialize_waterline ();
    auto& column = spatial::get<waterline_distance> (values);
    for (std::size_t offset = 0; offset < values.size (); ++offset)
      column[offset] =
        std::max (distance[offset], 0.0f) * waterline_distance[u::m];
  }

  void Surface::derive_geology_materials () {
    SurfaceAtlas& atlas = mutable_atlas ();
    const auto& eroded = eroded_material ();
    const auto& deposited = deposited_material ();
    const float eroded_scale = robust_positive_scale (eroded);
    const float deposited_scale = robust_positive_scale (deposited);
    SurfaceGeologySections& values = atlas.geology ().materialize_materials ();
    auto& exposure = spatial::get<erosion_exposure> (values);
    auto& cover = spatial::get<deposition_cover> (values);
    for (std::size_t offset = 0; offset < values.size (); ++offset) {
      exposure[offset] =
        std::clamp (eroded[offset].numerical_value_in (mp_units::one) /
                      eroded_scale,
                    0.0f,
                    1.0f) *
        erosion_exposure[one];
      cover[offset] =
        std::clamp (deposited[offset].numerical_value_in (mp_units::one) /
                      deposited_scale,
                    0.0f,
                    1.0f) *
        deposition_cover[one];
    }
  }
}
