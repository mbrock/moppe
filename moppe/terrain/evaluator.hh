#ifndef MOPPE_TERRAIN_EVALUATOR_HH
#define MOPPE_TERRAIN_EVALUATOR_HH

#include <moppe/terrain/field.hh>

#include <cstddef>
#include <span>
#include <vector>

namespace moppe::terrain {
  struct Domain2D {
    std::size_t width;
    std::size_t height;
    float min_x = 0.0f;
    float max_x = 1.0f;
    float min_y = 0.0f;
    float max_y = 1.0f;
  };

  class ScalarRaster {
  public:
    ScalarRaster (Domain2D domain, std::vector<float> values);

    const Domain2D& domain () const noexcept { return m_domain; }
    std::span<const float> values () const noexcept { return m_values; }

    float at (std::size_t x, std::size_t y) const;
    float min_value () const;
    float max_value () const;

  private:
    Domain2D m_domain;
    std::vector<float> m_values;
  };

  // Materializes a pointwise field over a concrete sampling domain.  CPU,
  // Metal, and future portable GPU implementations share this value-level
  // boundary; only the lowering and execution strategy differs.
  class FieldEvaluator {
  public:
    virtual ~FieldEvaluator () = default;

    virtual ScalarRaster evaluate
      (const ScalarField& field, const Domain2D& domain) const = 0;
  };

  // A materialization barrier: remaps the sampled minimum and maximum to
  // zero and one.  Constant rasters become zero, matching HeightMap's
  // existing normalization semantics.
  ScalarRaster normalize (const ScalarRaster& raster);
}

#endif
