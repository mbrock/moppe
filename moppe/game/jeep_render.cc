#include <moppe/game/jeep_model.hh>
#include <moppe/game/jeep_render.hh>
#include <moppe/game/model_mesh.hh>

namespace moppe::game {
  void render_jeep (render::Renderer& r,
                    render::DrawList& dl,
                    const CarPose& jeep,
                    JeepRiders riders,
                    uint64_t motion_base) {
    namespace m = jeep_model;
    static const CarModel model {
      .steering_ratio = 3.2f * scale_factor[one],
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
      .lean = -0.14f * u::rad,
      .sole_tilt = 0.35f * grade[one],
      .knee_ahead = 0.5f * scale_factor[one],
      .knee_out = 0.2f * scale_factor[one],
    };
    static const CarMeshes meshes {
      .body = { { model_mesh::bake (r, m::body) },
                { model_mesh::bake (r, m::cage) },
                { model_mesh::bake (r, m::seats) },
                { model_mesh::bake (r, m::column) } },
      .steering_wheel = model_mesh::bake (r, m::steering_wheel),
      .wheel = model_mesh::bake (r, m::wheel),
    };
    render_car (r, dl, jeep, riders, motion_base, model, meshes);
  }
}
