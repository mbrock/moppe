#include "atelier/atelier.hh"

#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

#include <mp-units/framework.h>
#include <mp-units/systems/isq.h>
#include <mp-units/systems/si.h>
#include <mp-units/utility/cartesian_vector.h>

namespace atelier {
  using namespace mp_units;
  using namespace mp_units::si::unit_symbols;

  using Matrix = simd_float4x4;
  using Cartesian = mp_units::utility::cartesian_vector<float, 3>;
  using Displacement = quantity<isq::displacement[si::metre], Cartesian>;
  using PositionVector = quantity<isq::position_vector[si::metre], Cartesian>;
  using Length = quantity<isq::length[si::metre], float>;
  using Duration = quantity<isq::duration[si::second], float>;
  using Angle = quantity<isq::angular_measure[si::radian], float>;

  inline constexpr struct coin_radius : quantity_spec<isq::radius> {
  } coin_radius;
  inline constexpr struct wire_radius : quantity_spec<isq::radius> {
  } wire_radius;
  inline constexpr struct coin_half_thickness : quantity_spec<isq::height> {
  } coin_half_thickness;
  inline constexpr struct camera_orbit_radius : quantity_spec<isq::radius> {
  } camera_orbit_radius;
  inline constexpr struct camera_height : quantity_spec<isq::height> {
  } camera_height;
  inline constexpr struct camera_field_of_view
      : quantity_spec<isq::angular_measure> {
  } camera_field_of_view;
  inline constexpr struct camera_orbit_velocity
      : quantity_spec<isq::angular_velocity> {
  } camera_orbit_velocity;
  inline constexpr struct near_clip_distance : quantity_spec<isq::distance> {
  } near_clip_distance;
  inline constexpr struct far_clip_distance : quantity_spec<isq::distance> {
  } far_clip_distance;

  namespace {
    constexpr int coin_sides = 6;
    constexpr float pi = 3.14159265358979323846f;

    struct Segment {
      Displacement start;
      Displacement end;
    };

    Displacement displacement (float x, float y, float z) {
      return isq::displacement (Cartesian { x, y, z } * m);
    }

    PositionVector position (float x, float y, float z) {
      return isq::position_vector (Cartesian { x, y, z } * m);
    }

    VertexPosition vertex_position (Displacement displacement) {
      const Cartesian& value = displacement.numerical_value_in (m);
      return { value[0], value[1], value[2] };
    }

    std::array<Segment, coin_sides * 3> coin_edges () {
      const auto radius = coin_radius (1.0f * m);
      const auto half_thickness = coin_half_thickness (0.16f * m);
      std::array<Displacement, coin_sides> upper;
      std::array<Displacement, coin_sides> lower;

      for (int side = 0; side < coin_sides; ++side) {
        const float angle = 2.0f * pi * side / coin_sides;
        const float x = radius.numerical_value_in (m) * std::cos (angle);
        const float z = radius.numerical_value_in (m) * std::sin (angle);
        const float y = half_thickness.numerical_value_in (m);
        upper[side] = displacement (x, y, z);
        lower[side] = displacement (x, -y, z);
      }

      std::array<Segment, coin_sides * 3> edges;
      for (int side = 0; side < coin_sides; ++side) {
        const int next = (side + 1) % coin_sides;
        edges[side * 3] = { upper[side], upper[next] };
        edges[side * 3 + 1] = { lower[side], lower[next] };
        edges[side * 3 + 2] = { upper[side], lower[side] };
      }
      return edges;
    }

    class WireMeshBuilder {
    public:
      explicit WireMeshBuilder (QuantityOf<wire_radius> auto radius)
          : m_radius_metres (radius.numerical_value_in (m)) {
        m_vertices.reserve (coin_sides * 3 * indices.size ());
      }

      void append (Segment segment) {
        const Cartesian direction =
          (segment.end - segment.start).numerical_value_in (m).unit ();
        const Cartesian reference =
          std::abs (scalar_product (direction, Cartesian { 0, 1, 0 })) > 0.9f
            ? Cartesian { 1, 0, 0 }
            : Cartesian { 0, 1, 0 };
        const Displacement u = isq::displacement (
          m_radius_metres * vector_product (direction, reference).unit () * m);
        const Cartesian u_value = u.numerical_value_in (m);
        const Displacement v = isq::displacement (
          m_radius_metres * vector_product (direction, u_value).unit () * m);

        const std::array<Displacement, 8> corners {
          segment.start - u - v, segment.start + u - v, segment.start + u + v,
          segment.start - u + v, segment.end - u - v,   segment.end + u - v,
          segment.end + u + v,   segment.end - u + v,
        };
        for (const unsigned index : indices)
          m_vertices.push_back (vertex_position (corners[index]));
      }

      std::vector<VertexPosition> finish () && {
        return std::move (m_vertices);
      }

    private:
      static constexpr std::array<unsigned, 36> indices {
        0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6,
        3, 0, 4, 3, 4, 7, 0, 3, 2, 0, 2, 1, 4, 5, 6, 4, 6, 7,
      };

      float m_radius_metres;
      std::vector<VertexPosition> m_vertices;
    };

    Matrix perspective (Angle vertical_fov,
                        float aspect_ratio,
                        Length near_plane,
                        Length far_plane) {
      const float fov = vertical_fov.numerical_value_in (si::radian);
      const float near_m = near_plane.numerical_value_in (m);
      const float far_m = far_plane.numerical_value_in (m);
      const float y = 1.0f / std::tan (fov * 0.5f);
      const float x = y / aspect_ratio;
      const float z = far_m / (near_m - far_m);
      return { simd_make_float4 (x, 0, 0, 0),
               simd_make_float4 (0, y, 0, 0),
               simd_make_float4 (0, 0, z, -1),
               simd_make_float4 (0, 0, near_m * z, 0) };
    }

    Matrix look_at (PositionVector eye, PositionVector target, Cartesian up) {
      const Cartesian& eye_value = eye.numerical_value_in (m);
      const Cartesian& target_value = target.numerical_value_in (m);
      const VertexPosition gpu_eye { eye_value[0], eye_value[1], eye_value[2] };
      const VertexPosition gpu_target { target_value[0],
                                        target_value[1],
                                        target_value[2] };
      const VertexPosition gpu_up { up[0], up[1], up[2] };
      const VertexPosition z = simd_normalize (gpu_eye - gpu_target);
      const VertexPosition x = simd_normalize (simd_cross (gpu_up, z));
      const VertexPosition y = simd_cross (z, x);
      return { simd_make_float4 (x.x, y.x, z.x, 0),
               simd_make_float4 (x.y, y.y, z.y, 0),
               simd_make_float4 (x.z, y.z, z.z, 0),
               simd_make_float4 (-simd_dot (x, gpu_eye),
                                 -simd_dot (y, gpu_eye),
                                 -simd_dot (z, gpu_eye),
                                 1) };
    }

    class OrbitingCamera {
    public:
      Matrix world_to_clip (Duration elapsed, Viewport viewport) const {
        const auto orbit_velocity =
          camera_orbit_velocity (0.32f * si::radian / si::second);
        const Angle orbit_angle =
          isq::angular_measure (orbit_velocity * elapsed);
        const float angle = orbit_angle.numerical_value_in (si::radian);

        const auto orbit_radius = camera_orbit_radius (3.0f * m);
        const auto eye_height = camera_height (1.55f * m);
        const float radius = orbit_radius.numerical_value_in (m);
        const PositionVector eye = position (radius * std::sin (angle),
                                             eye_height.numerical_value_in (m),
                                             radius * std::cos (angle));

        const Matrix projection =
          perspective (camera_field_of_view (0.78f * si::radian),
                       viewport.aspect_ratio (),
                       near_clip_distance (0.05f * m),
                       far_clip_distance (50.0f * m));
        return simd_mul (projection,
                         look_at (eye, position (0, 0, 0), { 0, 1, 0 }));
      }
    };
  }

  bool Viewport::is_empty () const {
    return width == 0 || height == 0;
  }

  float Viewport::aspect_ratio () const {
    if (is_empty ())
      throw std::invalid_argument ("An empty viewport has no aspect ratio");
    return static_cast<float> (width) / static_cast<float> (height);
  }

  std::vector<VertexPosition> make_coin_wireframe () {
    WireMeshBuilder mesh (wire_radius (0.025f * m));
    for (const Segment edge : coin_edges ())
      mesh.append (edge);
    return std::move (mesh).finish ();
  }

  Frame make_frame (std::chrono::steady_clock::duration elapsed,
                    Viewport viewport) {
    const float seconds = std::chrono::duration<float> (elapsed).count ();
    const OrbitingCamera camera;
    return {
      .uniforms = {
        .world_to_clip = camera.world_to_clip (seconds * si::second, viewport),
      },
    };
  }
}
