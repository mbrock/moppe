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
    scale_factor_t steering_ratio;
    // Points of the model, as offsets from its origin.
    displacement_t steering_hub, steering_column;
    displacement_t grip_left, grip_right;
    displacement_t seat_driver, seat_passenger;
    displacement_t pedal_left, pedal_right;
    displacement_t footrest_left, footrest_right;
    // Front left, front right, rear left, rear right.
    displacement_t wheels[4];
    // How far back the hiker sits, how far the soles tip up to the pedals
    // (rise over run), and how far the knees point ahead and out, as
    // shares of up.
    radians_t lean;
    grade_t sole_tilt;
    scale_factor_t knee_ahead;
    scale_factor_t knee_out;
  };

  // A car's baked meshes: the parts fixed to its body, each at its hinge
  // when it has one, in the order drawn; the steering wheel about its hub;
  // the right-hand wheel model, turned about for the left; and the
  // see-through parts, drawn last over what stands behind them.
  struct CarMeshes {
    struct Part {
      render::MeshPtr mesh;
      std::optional<displacement_t> hinge;
    };
    std::vector<Part> body;
    render::MeshPtr steering_wheel;
    render::MeshPtr wheel;
    // The rear pair's, when they differ (the bus's twins).
    render::MeshPtr rear_wheel;
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

  // A passenger alone, seated in `model`'s passenger seat of `car`,
  // skinned into `dl`: one more rider than render_car seats.
  void seat_car_passenger (render::DrawList& dl,
                           const CarPose& car,
                           const CarModel& model,
                           Outfit outfit);
}

#endif
