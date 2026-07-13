#pragma once

#include "atelier/matrix.hh"
#include "atelier/space.hh"

// The camera: it rides a slow carousel around the carpet, always
// facing a point just above the carpet's centre.

namespace atelier {
  struct Carousel {
    Length orbit_radius = 11.0f * si::metre;
    Length ride_height = 6.5f * si::metre;
    AngularRate rate = 0.01f * angular::revolution / si::second;
    Angle field_of_view = 42.0f * angular::degree;
    Length near_plane = 0.05f * si::metre;
    Length far_plane = 80.0f * si::metre;
    Point focus = scene + up * (0.9f * si::metre);

    [[nodiscard]] Point eye (Duration t) const {
      return scene + radial (rate * t) * orbit_radius + up * ride_height;
    }

    [[nodiscard]] Matrix world_to_clip (Duration t, Real aspect_ratio) const {
      return simd_mul (
        perspective (field_of_view, aspect_ratio, near_plane, far_plane),
        look_at (eye (t), focus));
    }
  };
}
