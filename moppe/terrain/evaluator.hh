#ifndef MOPPE_TERRAIN_EVALUATOR_HH
#define MOPPE_TERRAIN_EVALUATOR_HH

#include <moppe/terrain/field.hh>

#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>
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

    const Domain2D& domain () const noexcept {
      return m_domain;
    }
    std::span<const float> values () const noexcept {
      return m_values;
    }

    float at (std::size_t x, std::size_t y) const;
    float min_value () const;
    float max_value () const;

  private:
    Domain2D m_domain;
    std::vector<float> m_values;
  };

  // A sampled scalar field whose values retain their semantic reference.
  // Storage remains a compact float vector; sample() reconstructs a quantity
  // only at the domain boundary.
  template <auto R>
    requires mp_units::Reference<std::remove_const_t<decltype (R)>>
  class Raster {
  public:
    static constexpr auto reference = R;

    explicit Raster (ScalarRaster raster) : m_raster (std::move (raster)) {}

    const ScalarRaster& untyped () const noexcept {
      return m_raster;
    }
    const Domain2D& domain () const noexcept {
      return m_raster.domain ();
    }
    std::span<const float> values () const noexcept {
      return m_raster.values ();
    }
    mp_units::quantity<R, float> sample (std::size_t x, std::size_t y) const {
      return m_raster.at (x, y) * R;
    }

  private:
    ScalarRaster m_raster;
  };

  using NormalizedRaster = Raster<normalized_sample[mp_units::one]>;
  using RelativeElevationRaster = Raster<relative_elevation[mp_units::one]>;

  // Materializes a pointwise field over a concrete sampling domain.  CPU,
  // Metal, and future portable GPU implementations share this value-level
  // boundary; only the lowering and execution strategy differs.
  class FieldEvaluator {
  public:
    virtual ~FieldEvaluator () = default;

    virtual ScalarRaster evaluate (const ScalarField& field,
                                   const Domain2D& domain) const = 0;
  };

  // A materialization barrier: remaps the sampled minimum and maximum to
  // zero and one.  Constant rasters become zero, matching HeightMap's
  // existing normalization semantics.
  ScalarRaster normalize (const ScalarRaster& raster);

  template <auto QS>
  Raster<QS[mp_units::one]> materialize (const FieldEvaluator& evaluator,
                                         const Field<QS>& field,
                                         const Domain2D& domain) {
    return Raster<QS[mp_units::one]> (
      evaluator.evaluate (field.untyped (), domain));
  }

  template <auto R>
  NormalizedRaster normalize (const Raster<R>& raster) {
    return NormalizedRaster (normalize (raster.untyped ()));
  }
}

#endif
