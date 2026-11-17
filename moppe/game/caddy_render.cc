#include <moppe/game/caddy_model.hh>
#include <moppe/game/caddy_render.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/game/sling.hh>
#include <moppe/gfx/mat4.hh>

#include <algorithm>
#include <cmath>

namespace moppe::game {
  void render_caddy (render::Renderer& r,
                     render::DrawList& dl,
                     const CarPose& caddy,
                     JeepRiders riders,
                     uint64_t motion_base) {
    namespace m = caddy_model;
    // Some three and a half turns of the steering wheel from lock to lock;
    // sat more upright than in the jeep.
    static const CarModel model {
      .steering_ratio = 4.6f * scale_factor[one],
      .steering_hub = displacement (m::steering_hub),
      .steering_column = displacement (m::steering_column),
      .grip_left = displacement (m::grip_left),
      .grip_right = displacement (m::grip_right),
      .seat_driver = displacement (m::seat_driver),
      .seat_passenger = displacement (m::seat_passenger),
      .pedal_left = displacement (m::pedal_left),
      .pedal_right = displacement (m::pedal_right),
      .footrest_left = displacement (m::footrest_left),
      .footrest_right = displacement (m::footrest_right),
      .wheels = { displacement (m::wheel_fl),
                  displacement (m::wheel_fr),
                  displacement (m::wheel_rl),
                  displacement (m::wheel_rr) },
      .lean = -0.10f * u::rad,
      .sole_tilt = 0.45f * grade[one],
      .knee_ahead = 0.6f * scale_factor[one],
      .knee_out = 0.15f * scale_factor[one],
    };
    static const CarMeshes meshes {
      .body = { { model_mesh::bake (r, m::body) },
                { model_mesh::bake (r, m::hardtop) },
                { model_mesh::bake (r, m::tailgate),
                  displacement (m::tailgate_hinge) },
                { model_mesh::bake (r, m::seats) },
                { model_mesh::bake (r, m::column) } },
      .steering_wheel = model_mesh::bake (r, m::steering_wheel),
      .wheel = model_mesh::bake (r, m::wheel),
      .glass = { { model_mesh::bake (r, m::glass) } },
    };
    render_car (r, dl, caddy, riders, motion_base, model, meshes);
  }

  namespace {
    // A rope as a square tube in a few straight pieces, `thick` from its
    // axis to each face, hanging `sag` at its middle.
    void draw_line (render::DrawList& dl,
                    const position_t& from,
                    const position_t& to,
                    meters_t thick,
                    meters_t sag) {
      constexpr int pieces = 10;
      const displacement_t line = quantity_cast<isq::displacement> (to - from);
      const meters_t span = length (displacement_value (line)) * u::m;
      if (span < 1e-3f * u::m)
        return;
      const direction_t axis = unit_direction (displacement_value (line) *
                                               (1.0f / meters_value (span)));
      // Square to the axis and level, unless the line hangs plumb.
      const Vec3 across = cross (direction_value (axis), Vec3 (0, 1, 0));
      const direction_t side = unit_direction (
        length2 (across) > 1e-6f ? normalized (across) : Vec3 (1, 0, 0));
      const direction_t over =
        unit_direction (cross (direction_value (side), direction_value (axis)));
      const auto at = [&] (int piece) {
        const proportion_t t =
          static_cast<float> (piece) / pieces * proportion[one];
        const meters_t drop = sag * 4.0f * t * (1.0f * proportion[one] - t);
        return from +
               quantity_cast<isq::position_vector> (displacement (
                 displacement_value (line) * t.numerical_value_in (one))) -
               quantity_cast<isq::position_vector> (
                 displacement (Vec3 (0, meters_value (drop), 0)));
      };
      const direction_t faces[4] = { side, over, -side, -over };
      for (int piece = 0; piece < pieces; ++piece) {
        const position_t a = at (piece), b = at (piece + 1);
        for (int f = 0; f < 4; ++f) {
          const auto out = quantity_cast<isq::position_vector> (
                       along (faces[f], thick)),
                     next = quantity_cast<isq::position_vector> (
                       along (faces[(f + 1) % 4], thick));
          dl.normal (
            unit_direction (normalized (direction_value (faces[f]) +
                                        direction_value (faces[(f + 1) % 4]))));
          dl.vertex (a + out);
          dl.vertex (b + out);
          dl.vertex (b + next);
          dl.vertex (a + out);
          dl.vertex (b + next);
          dl.vertex (a + next);
        }
      }
    }
  }

  void render_sling (render::DrawList& dl, const SlingPose& sling) {
    // How thick the line and the bridle's legs are, from axis to face.
    const meters_t line_thick = 0.035f * u::m;
    const meters_t leg_thick = 0.025f * u::m;
    render::DrawState solid;
    solid.cull = false;
    dl.state (solid);
    dl.lit (true);
    dl.fogged (true);
    dl.begin (render::Prim::Triangles);
    // An orange lifting line, and the bridle's darker legs.
    dl.color (DisplayColor (0.86f, 0.42f, 0.10f));
    const meters_t span =
      length (displacement_value (
        quantity_cast<isq::displacement> (sling.hook - sling.ring))) *
      u::m;
    const meters_t slack = std::max (0.0f * u::m, sling_line - span);
    draw_line (dl,
               sling.hook,
               sling.ring,
               line_thick,
               sling.taut ? 0.0f * u::m
                          : std::min (0.5f * slack + 0.2f * u::m, 3.0f * u::m));
    dl.color (DisplayColor (0.30f, 0.30f, 0.28f));
    for (const position_t& leg : sling.legs)
      draw_line (dl, sling.ring, leg, leg_thick, 0.0f * u::m);
    dl.end ();
    dl.state (render::DrawState ());
  }
}
