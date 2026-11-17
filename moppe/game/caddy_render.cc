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
      .steering_ratio = 4.6f,
      .steering_hub = m::steering_hub,
      .steering_column = m::steering_column,
      .grip_left = m::grip_left,
      .grip_right = m::grip_right,
      .seat_driver = m::seat_driver,
      .seat_passenger = m::seat_passenger,
      .pedal_left = m::pedal_left,
      .pedal_right = m::pedal_right,
      .footrest_left = m::footrest_left,
      .footrest_right = m::footrest_right,
      .wheels = { m::wheel_fl, m::wheel_fr, m::wheel_rl, m::wheel_rr },
      .lean = -0.10f,
      .sole_tilt = 0.45f,
      .knee_ahead = 0.6f,
      .knee_out = 0.15f,
    };
    static const CarMeshes meshes {
      .body = { { model_mesh::bake (r, m::body) },
                { model_mesh::bake (r, m::hardtop) },
                { model_mesh::bake (r, m::tailgate), m::tailgate_hinge },
                { model_mesh::bake (r, m::seats) },
                { model_mesh::bake (r, m::column) } },
      .steering_wheel = model_mesh::bake (r, m::steering_wheel),
      .wheel = model_mesh::bake (r, m::wheel),
      .glass = { { model_mesh::bake (r, m::glass) } },
    };
    render_car (r, dl, caddy, riders, motion_base, model, meshes);
  }

  namespace {
    // A rope as a square tube in a few straight pieces, hanging `sag`
    // metres at its middle.
    void draw_line (render::DrawList& dl,
                    const Vec3& from,
                    const Vec3& to,
                    float thick,
                    float sag) {
      constexpr int pieces = 10;
      const Vec3 along = to - from;
      const float span = length (along);
      if (span < 1e-3f)
        return;
      const Vec3 axis = along * (1.0f / span);
      Vec3 side = cross (axis, Vec3 (0, 1, 0));
      side = length2 (side) > 1e-6f ? normalized (side) : Vec3 (1, 0, 0);
      const Vec3 over = cross (side, axis);
      const auto at = [&] (int piece) {
        const float t = static_cast<float> (piece) / pieces;
        return from + along * t - Vec3 (0, sag * 4.0f * t * (1.0f - t), 0);
      };
      const Vec3 faces[4] = { side, over, -side, -over };
      for (int piece = 0; piece < pieces; ++piece) {
        const Vec3 a = at (piece), b = at (piece + 1);
        for (int f = 0; f < 4; ++f) {
          const Vec3 u = faces[f] * thick, v = faces[(f + 1) % 4] * thick;
          dl.normal (normalized (faces[f] + faces[(f + 1) % 4]));
          dl.vertex (a + u);
          dl.vertex (b + u);
          dl.vertex (b + v);
          dl.vertex (a + u);
          dl.vertex (b + v);
          dl.vertex (a + v);
        }
      }
    }
  }

  void render_sling (render::DrawList& dl, const SlingPose& sling) {
    render::DrawState solid;
    solid.cull = false;
    dl.state (solid);
    dl.lit (true);
    dl.fogged (true);
    dl.begin (render::Prim::Triangles);
    // An orange lifting line, and the bridle's darker legs.
    dl.color (DisplayColor (0.86f, 0.42f, 0.10f));
    const float span = length (sling.hook - sling.ring);
    const float slack = std::max (0.0f, meters_value (sling_line) - span);
    draw_line (dl,
               sling.hook,
               sling.ring,
               0.035f,
               sling.taut ? 0.0f : std::min (0.5f * slack + 0.2f, 3.0f));
    dl.color (DisplayColor (0.30f, 0.30f, 0.28f));
    for (const Vec3& leg : sling.legs)
      draw_line (dl, sling.ring, leg, 0.025f, 0.0f);
    dl.end ();
    dl.state (render::DrawState ());
  }
}
