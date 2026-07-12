#ifndef MOPPE_TERRAIN_CPU_EVALUATOR_HH
#define MOPPE_TERRAIN_CPU_EVALUATOR_HH

#include <moppe/terrain/evaluator.hh>

#include <functional>
#include <utility>

namespace moppe::terrain {
  // The first evaluator backend.  It lowers the expression DAG into a
  // topologically ordered scalar program, then runs that program at each
  // sample.  Later source backends can traverse the same node variants.
  class CpuEvaluator final : public FieldEvaluator {
  public:
    using Progress = std::function<void (std::size_t, std::size_t)>;

    explicit CpuEvaluator (Progress progress = {})
        : m_progress (std::move (progress)) {}

    ScalarRaster evaluate (const ScalarField& field,
                           const RecipeDomain2D& domain) const override;

  private:
    Progress m_progress;
  };
}

#endif
