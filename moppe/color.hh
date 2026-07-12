#ifndef MOPPE_COLOR_HH
#define MOPPE_COLOR_HH

#include <algorithm>
#include <cstddef>

namespace moppe {
  // An authored, display-referred RGB triplet.  This is not light, a
  // reflectance spectrum, or a spatial vector: it is the late rendering input
  // that will eventually drive a display's three primaries.  Keeping the type
  // explicit prevents geometric algebra from leaking into color code while
  // leaving room for spectral materials and observer transforms upstream.
  struct DisplayColor {
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;

    constexpr DisplayColor () = default;
    constexpr DisplayColor (float red, float green, float blue)
        : red (red), green (green), blue (blue) {}

    constexpr float& operator[] (std::size_t index) {
      return index == 0 ? red : index == 1 ? green : blue;
    }
    constexpr const float& operator[] (std::size_t index) const {
      return index == 0 ? red : index == 1 ? green : blue;
    }

    constexpr bool operator== (const DisplayColor&) const = default;
  };

  // These operations describe existing art direction, not physical light
  // transport.  Naming them keeps the chosen display-space approximation
  // visible at call sites until material and spectral models replace it.
  constexpr DisplayColor
  mix_display (DisplayColor from, DisplayColor to, float amount) {
    return DisplayColor (from.red + (to.red - from.red) * amount,
                         from.green + (to.green - from.green) * amount,
                         from.blue + (to.blue - from.blue) * amount);
  }

  constexpr DisplayColor scale_display (DisplayColor color, float amount) {
    return DisplayColor (
      color.red * amount, color.green * amount, color.blue * amount);
  }

  constexpr DisplayColor clamp_display (DisplayColor color) {
    return DisplayColor (std::clamp (color.red, 0.0f, 1.0f),
                         std::clamp (color.green, 0.0f, 1.0f),
                         std::clamp (color.blue, 0.0f, 1.0f));
  }
}

#endif
