#include <moppe/map/surface_readings.hh>

#include <algorithm>
#include <vector>

namespace moppe::map {
  namespace {
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

  GeologyMaterials analyze_geology_materials (const SurfaceGeometry& geometry) {
    const auto& eroded = spatial::get<eroded_surface_material> (geometry);
    const auto& deposited = spatial::get<deposited_surface_material> (geometry);
    const float eroded_scale = robust_positive_scale (eroded);
    const float deposited_scale = robust_positive_scale (deposited);
    GeologyMaterials values (geometry.domain ());
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
    return values;
  }
}
