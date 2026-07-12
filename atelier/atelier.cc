#include "atelier/atelier.hh"

#include <array>
#include <cmath>

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
  using AngularVelocity =
    quantity<isq::angular_velocity[si::radian / si::second], float>;

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
    Displacement displacement (float x, float y, float z) {
      return isq::displacement (Cartesian { x, y, z } * m);
    }

    PositionVector position_vector (float x, float y, float z) {
      return isq::position_vector (Cartesian { x, y, z } * m);
    }

    GpuVector gpu_vector (Displacement value) {
      const Cartesian& v = value.numerical_value_in (m);
      return { v[0], v[1], v[2] };
    }

    Matrix perspective (Angle vertical_fov,
                        float aspect,
                        Length near_plane,
                        Length far_plane) {
      const float fov = vertical_fov.numerical_value_in (si::radian);
      const float near_m = near_plane.numerical_value_in (m);
      const float far_m = far_plane.numerical_value_in (m);
      const float y = 1.0f / std::tan (fov * 0.5f);
      const float x = y / aspect;
      const float z = far_m / (near_m - far_m);
      return { simd_make_float4 (x, 0, 0, 0),
               simd_make_float4 (0, y, 0, 0),
               simd_make_float4 (0, 0, z, -1),
               simd_make_float4 (0, 0, near_m * z, 0) };
    }

    Matrix look_at (PositionVector eye_position,
                    PositionVector target_position,
                    Cartesian up) {
      const Cartesian& eye_value = eye_position.numerical_value_in (m);
      const Cartesian& target_value = target_position.numerical_value_in (m);
      const GpuVector eye = { eye_value[0], eye_value[1], eye_value[2] };
      const GpuVector target = { target_value[0],
                                 target_value[1],
                                 target_value[2] };
      const GpuVector gpu_up = { up[0], up[1], up[2] };
      const GpuVector z = simd_normalize (eye - target);
      const GpuVector x = simd_normalize (simd_cross (gpu_up, z));
      const GpuVector y = simd_cross (z, x);
      return { simd_make_float4 (x.x, y.x, z.x, 0),
               simd_make_float4 (x.y, y.y, z.y, 0),
               simd_make_float4 (x.z, y.z, z.z, 0),
               simd_make_float4 (-simd_dot (x, eye),
                                 -simd_dot (y, eye),
                                 -simd_dot (z, eye),
                                 1) };
    }

    void append_rod (std::vector<Displacement>& mesh,
                     Displacement start,
                     Displacement end,
                     QuantityOf<wire_radius> auto radius) {
      const Cartesian direction = (end - start).numerical_value_in (m).unit ();
      const Cartesian reference =
        std::abs (scalar_product (direction, Cartesian { 0, 1, 0 })) > 0.9f
          ? Cartesian { 1, 0, 0 }
          : Cartesian { 0, 1, 0 };
      const float radius_m = radius.numerical_value_in (m);
      const Displacement u = isq::displacement (
        radius_m * vector_product (direction, reference).unit () * m);
      const Cartesian u_value = u.numerical_value_in (m);
      const Displacement v = isq::displacement (
        radius_m * vector_product (direction, u_value).unit () * m);
      const std::array<Displacement, 8> corners { start - u - v, start + u - v,
                                                  start + u + v, start - u + v,
                                                  end - u - v,   end + u - v,
                                                  end + u + v,   end - u + v };
      constexpr std::array<unsigned, 36> triangles {
        0, 1, 5, 0, 5, 4, 1, 2, 6, 1, 6, 5, 2, 3, 7, 2, 7, 6,
        3, 0, 4, 3, 4, 7, 0, 3, 2, 0, 2, 1, 4, 5, 6, 4, 6, 7
      };
      for (const unsigned index : triangles)
        mesh.push_back (corners[index]);
    }
  }

  std::vector<GpuVector> coin_wire_mesh () {
    constexpr int sides = 6;
    constexpr float pi = 3.14159265358979323846f;
    const auto radius = coin_radius (1.0f * m);
    const auto half_thickness = coin_half_thickness (0.16f * m);
    const auto edge_radius = wire_radius (0.025f * m);
    std::array<Displacement, sides> upper;
    std::array<Displacement, sides> lower;
    for (int i = 0; i < sides; ++i) {
      const float angle = 2.0f * pi * static_cast<float> (i) / sides;
      const float x = radius.numerical_value_in (m) * std::cos (angle);
      const float z = radius.numerical_value_in (m) * std::sin (angle);
      const float y = half_thickness.numerical_value_in (m);
      upper[i] = displacement (x, y, z);
      lower[i] = displacement (x, -y, z);
    }

    std::vector<Displacement> edges;
    edges.reserve (sides * 6);
    for (int i = 0; i < sides; ++i) {
      const int next = (i + 1) % sides;
      edges.push_back (upper[i]);
      edges.push_back (upper[next]);
      edges.push_back (lower[i]);
      edges.push_back (lower[next]);
      edges.push_back (upper[i]);
      edges.push_back (lower[i]);
    }

    std::vector<Displacement> mesh;
    mesh.reserve (edges.size () * 18);
    for (std::size_t i = 0; i < edges.size (); i += 2)
      append_rod (mesh, edges[i], edges[i + 1], edge_radius);

    std::vector<GpuVector> gpu_mesh;
    gpu_mesh.reserve (mesh.size ());
    for (const Displacement vertex : mesh)
      gpu_mesh.push_back (gpu_vector (vertex));
    return gpu_mesh;
  }

  std::string_view shader_source () {
    return R"(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
  float4x4 world_to_clip;
};

vertex float4 atelier_vertex(const device float3* positions [[buffer(0)]],
                             constant Uniforms& uniforms [[buffer(1)]],
                             uint id [[vertex_id]]) {
  return uniforms.world_to_clip * float4(positions[id], 1.0);
}

fragment half4 atelier_fragment() {
  return half4(0.94h, 0.72h, 0.27h, 1.0h);
}
)";
  }

  Frame frame (float elapsed_seconds, float aspect) {
    const Duration elapsed = elapsed_seconds * si::second;
    const auto orbit_velocity =
      camera_orbit_velocity (0.32f * si::radian / si::second);
    const Angle orbit_angle = isq::angular_measure (orbit_velocity * elapsed);
    const float angle = orbit_angle.numerical_value_in (si::radian);
    const auto orbit_radius = camera_orbit_radius (3.0f * m);
    const auto eye_height = camera_height (1.55f * m);
    const float orbit_m = orbit_radius.numerical_value_in (m);
    const PositionVector eye =
      position_vector (orbit_m * std::sin (angle),
                       eye_height.numerical_value_in (m),
                       orbit_m * std::cos (angle));
    const auto field_of_view = camera_field_of_view (0.78f * si::radian);
    const auto near_plane = near_clip_distance (0.05f * m);
    const auto far_plane = far_clip_distance (50.0f * m);
    const Matrix projection =
      perspective (field_of_view, aspect, near_plane, far_plane);
    const Matrix view = look_at (eye, position_vector (0, 0, 0), { 0, 1, 0 });
    return { .uniforms = { .world_to_clip = simd_mul (projection, view) } };
  }
}
