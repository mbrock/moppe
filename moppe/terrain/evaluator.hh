#ifndef MOPPE_TERRAIN_EVALUATOR_HH
#define MOPPE_TERRAIN_EVALUATOR_HH

#include <moppe/terrain/discretization.hh>

#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace moppe::terrain {
  class ScalarRaster {
  public:
    ScalarRaster (FieldSamplingGrid2D domain, std::vector<float> values);

    const FieldSamplingGrid2D& domain () const noexcept {
      return m_domain;
    }
    std::span<const float> values () const noexcept {
      return m_values;
    }

    float at (std::size_t x, std::size_t y) const;
    float min_value () const;
    float max_value () const;

  private:
    FieldSamplingGrid2D m_domain;
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
    const FieldSamplingGrid2D& domain () const noexcept {
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
  using RelativeUpliftRaster = Raster<relative_uplift[mp_units::one]>;

  // A raster whose field coordinates and physical terrain geometry remain
  // coupled to the samples that materialized them.
  template <auto R>
    requires mp_units::Reference<std::remove_const_t<decltype (R)>>
  class TerrainRaster {
  public:
    TerrainRaster (Raster<R> raster, TerrainDiscretization discretization)
        : m_raster (std::move (raster)),
          m_discretization (std::move (discretization)) {
      if (m_raster.domain () != m_discretization.field_sampling_grid ())
        throw std::invalid_argument (
          "raster domain differs from terrain discretization");
    }

    const Raster<R>& raster () const noexcept {
      return m_raster;
    }
    const TerrainDiscretization& discretization () const noexcept {
      return m_discretization;
    }
    std::span<const float> values () const noexcept {
      return m_raster.values ();
    }
    mp_units::quantity<R, float> sample (std::size_t x, std::size_t y) const {
      return m_raster.sample (x, y);
    }
    mp_units::quantity<R, float> sample (GridPointIndex index) const {
      (void)m_discretization.physical_position (index);
      return m_raster.sample (
        static_cast<std::size_t> (column_number (index.column)),
        static_cast<std::size_t> (row_number (index.row)));
    }
    FieldCoordinate2D field_position (GridPointIndex index) const {
      return m_discretization.field_position (index);
    }
    HorizontalPosition2D physical_position (GridPointIndex index) const {
      return m_discretization.physical_position (index);
    }

  private:
    Raster<R> m_raster;
    TerrainDiscretization m_discretization;
  };

  using TerrainRelativeElevationRaster =
    TerrainRaster<relative_elevation[mp_units::one]>;

  // Materializes a pointwise field over a concrete sampling domain.  CPU,
  // Metal, and future portable GPU implementations share this value-level
  // boundary; only the lowering and execution strategy differs.
  class FieldEvaluator {
  public:
    virtual ~FieldEvaluator () = default;

    virtual ScalarRaster evaluate (const ScalarField& field,
                                   const FieldSamplingGrid2D& domain) const = 0;
  };

  // A materialization barrier: remaps the sampled minimum and maximum to
  // zero and one.  Constant rasters become zero, matching HeightMap's
  // existing normalization semantics.
  ScalarRaster normalize (const ScalarRaster& raster);

  template <auto QS>
  Raster<QS[mp_units::one]> materialize (const FieldEvaluator& evaluator,
                                         const Field<QS>& field,
                                         const FieldSamplingGrid2D& domain) {
    return Raster<QS[mp_units::one]> (
      evaluator.evaluate (field.untyped (), domain));
  }

  template <auto QS>
  TerrainRaster<QS[mp_units::one]>
  materialize (const FieldEvaluator& evaluator,
               const Field<QS>& field,
               const TerrainDiscretization& discretization) {
    return TerrainRaster<QS[mp_units::one]> (
      materialize (evaluator, field, discretization.field_sampling_grid ()),
      discretization);
  }

  template <auto R>
  NormalizedRaster normalize (const Raster<R>& raster) {
    return NormalizedRaster (normalize (raster.untyped ()));
  }
}

#endif
