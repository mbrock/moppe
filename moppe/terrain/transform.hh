#ifndef MOPPE_TERRAIN_TRANSFORM_HH
#define MOPPE_TERRAIN_TRANSFORM_HH

#include <cstddef>
#include <string>
#include <string_view>

namespace moppe::terrain {
  // These two axes describe what an evaluator must observe, without
  // prescribing whether it uses a CPU loop, a GPU kernel, or something
  // else. In more abstract language they distinguish pointwise maps, local
  // context, and operations whose answer depends on the whole terrain.
  enum class SpatialScope { Pointwise, Neighborhood, Global };

  enum class EvaluationOrder { Direct, Reduction, Iterative };

  struct TransformSemantics {
    SpatialScope spatial_scope;
    EvaluationOrder evaluation_order;
  };

  // Terrain owns this small editing vocabulary so the Lab and later tools do
  // not have to rediscover whether a property can be scrubbed or needs a
  // discrete counter.
  enum class ParameterDomain { Continuous, Natural };

  struct TransformDescription {
    std::string_view id;
    std::string_view title;
    TransformSemantics semantics;
  };

  struct TransformProperty {
    std::string label;
    std::string value;
    ParameterDomain domain;
  };
}

#endif
