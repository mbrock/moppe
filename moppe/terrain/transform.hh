#ifndef MOPPE_TERRAIN_TRANSFORM_HH
#define MOPPE_TERRAIN_TRANSFORM_HH

#include <algorithm>
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

  // Editors represent heterogeneous parameters in one normalized UI range,
  // while each concrete transform remains responsible for its own bounds and
  // unit conversion.
  inline float normalized_edit_value (float value, float low, float high) {
    if (high <= low)
      return 0.0f;
    return std::clamp ((value - low) / (high - low), 0.0f, 1.0f);
  }

  inline float edited_value (float normalized, float low, float high) {
    return low + std::clamp (normalized, 0.0f, 1.0f) * (high - low);
  }

  template <typename T>
  bool replace_edit_value (T& target, const T& value) {
    if (target == value)
      return false;
    target = value;
    return true;
  }
}

#endif
