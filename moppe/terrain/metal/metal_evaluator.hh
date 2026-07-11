#ifndef MOPPE_TERRAIN_METAL_EVALUATOR_HH
#define MOPPE_TERRAIN_METAL_EVALUATOR_HH

#include <moppe/terrain/evaluator.hh>

#include <memory>
#include <string>

namespace moppe::terrain::metal {
  // Metal 4 backend for pointwise ScalarField programs.  It lowers the DAG
  // to an MTLFunctionStitchingGraph, statically links that graph into a fixed
  // compute wrapper, and caches pipelines independently of parameter values.
  class MetalEvaluator final : public FieldEvaluator {
  public:
    explicit MetalEvaluator (const std::string& library_path);
    ~MetalEvaluator () override;

    MetalEvaluator (MetalEvaluator&&) noexcept;
    MetalEvaluator& operator= (MetalEvaluator&&) noexcept;

    MetalEvaluator (const MetalEvaluator&) = delete;
    MetalEvaluator& operator= (const MetalEvaluator&) = delete;

    ScalarRaster evaluate (const ScalarField& field,
                           const Domain2D& domain) const override;

    std::size_t compiled_pipeline_count () const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
  };
}

#endif
