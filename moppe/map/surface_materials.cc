#include <moppe/map/surface.hh>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace moppe::map {
  namespace {
    void require_surface_size (std::span<const float> input,
                               const SurfaceSections& sections,
                               const char* message) {
      if (input.size () != sections.size ())
        throw std::invalid_argument (message);
    }

    float robust_positive_scale (std::span<const float> values) {
      std::vector<float> positive;
      positive.reserve (values.size () / 8);
      for (float value : values)
        if (value > 0.0f)
          positive.push_back (value);
      if (positive.empty ())
        return 1.0f;
      const std::size_t rank = positive.size () * 49 / 50;
      std::nth_element (
        positive.begin (), positive.begin () + rank, positive.end ());
      return std::max (positive[rank], 1e-6f);
    }
  }

  void Surface::materialize_moisture (std::span<const float> moisture) {
    SurfaceSections& values = mutable_sections ();
    require_surface_size (
      moisture, values, "Moisture needs one value per surface sample");
    auto& column = spatial::get<surface_moisture> (values);
    for (std::size_t offset = 0; offset < values.size (); ++offset)
      column[offset] =
        std::clamp (moisture[offset], 0.0f, 1.0f) * surface_moisture[one];
    m_materialized.moisture = true;
  }

  void
  Surface::materialize_waterline_distance (std::span<const float> distance) {
    SurfaceSections& values = mutable_sections ();
    require_surface_size (
      distance,
      values,
      "Waterline distance needs one value per surface sample");
    auto& column = spatial::get<waterline_distance> (values);
    for (std::size_t offset = 0; offset < values.size (); ++offset)
      column[offset] =
        std::max (distance[offset], 0.0f) * waterline_distance[u::m];
    m_materialized.waterline = true;
  }

  void Surface::derive_geology_materials (std::span<const float> eroded,
                                          std::span<const float> deposited) {
    SurfaceSections& values = mutable_sections ();
    require_surface_size (
      eroded, values, "Erosion ledger needs one value per surface sample");
    require_surface_size (deposited,
                          values,
                          "Deposition ledger needs one value per surface "
                          "sample");
    const float eroded_scale = robust_positive_scale (eroded);
    const float deposited_scale = robust_positive_scale (deposited);
    auto& exposure = spatial::get<erosion_exposure> (values);
    auto& cover = spatial::get<deposition_cover> (values);
    for (std::size_t offset = 0; offset < values.size (); ++offset) {
      exposure[offset] =
        std::clamp (eroded[offset] / eroded_scale, 0.0f, 1.0f) *
        erosion_exposure[one];
      cover[offset] =
        std::clamp (deposited[offset] / deposited_scale, 0.0f, 1.0f) *
        deposition_cover[one];
    }
    m_materialized.geology = true;
  }
}
