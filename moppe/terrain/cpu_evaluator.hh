#ifndef MOPPE_TERRAIN_CPU_EVALUATOR_HH
#define MOPPE_TERRAIN_CPU_EVALUATOR_HH

#include <moppe/terrain/evaluator.hh>

namespace moppe::terrain {
  // The first evaluator backend.  It lowers the expression DAG into a
  // topologically ordered scalar program, then runs that program at each
  // sample.  Later source backends can traverse the same node variants.
  class CpuEvaluator final: public FieldEvaluator {
  public:
    ScalarRaster evaluate (const ScalarField& field,
			   const Domain2D& domain) const override;
  };
}

#endif
