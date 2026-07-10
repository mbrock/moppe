#ifndef MOPPE_TERRAIN_CPU_EVALUATOR_HH
#define MOPPE_TERRAIN_CPU_EVALUATOR_HH

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

  // The first evaluator backend.  It lowers the expression DAG into a
  // topologically ordered scalar program, then runs that program at each
  // sample.  Later source backends can traverse the same node variants.
  class CpuEvaluator {
  public:
    ScalarRaster evaluate (const ScalarField& field,
			   const Domain2D& domain) const;
  };
}

#endif
