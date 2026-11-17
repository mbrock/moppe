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
      .steering_ratio = 3.2f,
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
      .lean = -0.14f,
      .sole_tilt = 0.35f,
      .knee_ahead = 0.5f,
      .knee_out = 0.2f,
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
