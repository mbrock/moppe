#ifndef MOPPE_GAME_CAR_RENDER_HH
#define MOPPE_GAME_CAR_RENDER_HH

#include <moppe/game/frame_view.hh>
#include <moppe/game/outfit.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

#include <optional>
#include <vector>

namespace moppe::game {
  // Who is seen sitting in a car, and what each wears.
  struct JeepRiders {
    bool driver = false;
    bool passenger = false;
    Outfit driver_outfit = Outfit::blue_jacket;
    Outfit passenger_outfit = Outfit::blue_jacket;
  };

  // What a four-wheeled car's drawing reads of its model (the jeep's and
  // the Caddy's, exported from Blender): where the steering wheel turns
  // and is held, where its two front seats are and their occupants' feet
  // rest, where its wheels are, and how its hiker sits.
  struct CarModel {
    // The steering wheel turns this many times as far as the road wheels.
    float steering_ratio;
    Vec3 steering_hub, steering_column;
    Vec3 grip_left, grip_right;
    Vec3 seat_driver, seat_passenger;
    Vec3 pedal_left, pedal_right;
    Vec3 footrest_left, footrest_right;
    // Front left, front right, rear left, rear right.
    Vec3 wheels[4];
    // How far back the hiker sits, how far the soles tip up to the pedals,
    // and how far the knees point ahead and out.
    float lean;
    float sole_tilt;
    float knee_ahead;
    float knee_out;
  };

  // A car's baked meshes: the parts fixed to its body, each at its hinge
  // when it has one, in the order drawn; the steering wheel about its hub;
  // the right-hand wheel model, turned about for the left; and the
  // see-through parts, drawn last over what stands behind them.
  struct CarMeshes {
    struct Part {
      render::MeshPtr mesh;
      std::optional<Vec3> hinge;
    };
    std::vector<Part> body;
    render::MeshPtr steering_wheel;
    render::MeshPtr wheel;
    std::vector<Part> glass;
  };

  // The car drawn through the renderer, each wheel on its spring and the
  // front pair turned with the steering, and its riders, the hiker
  // seated with the driver's hands on the wheel, skinned into `dl`.
  void render_car (render::Renderer& r,
                   render::DrawList& dl,
                   const CarPose& car,
                   JeepRiders riders,
                   uint64_t motion_base,
                   const CarModel& model,
                   const CarMeshes& meshes);
}

#endif
