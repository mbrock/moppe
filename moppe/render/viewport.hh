#ifndef MOPPE_RENDER_VIEWPORT_HH
#define MOPPE_RENDER_VIEWPORT_HH

#include <moppe/gfx/math.hh>

#include <algorithm>
#include <cstdint>

namespace moppe::render {
  // The play area within the drawable, in pixels. Offscreen targets have
  // its width and height; only presentation uses its inset.
  struct Viewport {
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
  };

  // Limit width, never height: a narrower or portrait surface stays whole.
  // A nonpositive maximum leaves even an ultrawide surface whole.
  inline Viewport fit_viewport (std::uint32_t width,
                                std::uint32_t height,
                                aspect_ratio_t maximum_aspect) {
    const double most =
      double (height) * maximum_aspect.numerical_value_in (one);
    if (height > 0 && maximum_aspect > 0.0f * aspect_ratio[one] &&
        width > most) {
      const auto fitted = std::max (1u, std::uint32_t (most));
      return { (width - fitted) / 2, 0, fitted, height };
    }
    return { 0, 0, width, height };
  }
}

#endif
