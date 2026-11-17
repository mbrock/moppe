#include <moppe/game/boat_render.hh>
#include <moppe/game/caddy_render.hh>
#include <moppe/game/fleet_render.hh>
#include <moppe/game/helicopter_render.hh>
#include <moppe/game/jeep_render.hh>
#include <moppe/game/jet_ski_render.hh>
#include <moppe/game/long_john_render.hh>
#include <moppe/game/mountain_bike_render.hh>
#include <moppe/game/tractor_render.hh>
#include <moppe/game/vehicle_render.hh>

namespace moppe::game {
  namespace {
    // Kept in a namespace of its own: the engine is a unity build.
    namespace fleet_drawing {
      // Each machine's motion is told apart from every other's, this
      // player's and the other's, by Machine.
      constexpr std::uint64_t motion_bases[2][machine_count] = {
        { 0x1000,
          0x5000,
          0xA000,
          0xC000,
          0x8000,
          0xD000,
          0xF000,
          0xD100,
          0xD200 },
        { 0x3000,
          0x6000,
          0x9000,
          0xB000,
          0x7000,
          0xE000,
          0xF800,
          0xE100,
          0xE200 },
      };

      // The trailer's shadow, behind the tractor's own.
      constexpr meters_t trailer_shadow_radius = 2.4f * u::m;

      void draw (const FleetScene& s,
                 Machine machine,
                 const VehiclePose& pose,
                 const Riders& who,
                 std::uint64_t base) {
        switch (machine) {
        case Machine::bike:
          // Seen through its rider's eyes, there is no one to draw.
          if (who.from_the_seat)
            return;
          render_vehicle (s.r,
                          s.dl,
                          pose,
                          who.driver.has_value (),
                          base,
                          who.driver.value_or (Outfit::blue_jacket));
          return;
        case Machine::mountain_bike:
          render_mountain_bike (s.r, s.dl, pose, who.driver, base);
          return;
        case Machine::long_john:
          render_long_john (s.r, s.dl, pose, who.driver, base);
          return;
        default:
          return;
        }
      }

      void draw (const FleetScene& s,
                 Machine machine,
                 const CarPose& pose,
                 const Riders& who,
                 std::uint64_t base) {
        const JeepRiders riders {
          .driver = who.driver.has_value (),
          .passenger = who.passenger.has_value (),
          .driver_outfit = who.driver.value_or (Outfit::blue_jacket),
          .passenger_outfit = who.passenger.value_or (Outfit::blue_jacket),
        };
        if (machine == Machine::caddy)
          render_caddy (s.r, s.dl, pose, riders, base);
        else
          render_jeep (s.r, s.dl, pose, riders, base);
      }

      // Its pilot and passenger sit behind tinted glass, not drawn.
      void draw (const FleetScene& s,
                 Machine,
                 const HelicopterPose& pose,
                 const Riders&,
                 std::uint64_t base) {
        render_helicopter (s.r, pose, base);
      }

      void draw (const FleetScene& s,
                 Machine,
                 const TractorPose& pose,
                 const Riders& who,
                 std::uint64_t base) {
        render_tractor (s.r, s.dl, pose, who.driver, base);
      }

      void draw (const FleetScene& s,
                 Machine,
                 const BoatPose& pose,
                 const Riders& who,
                 std::uint64_t base) {
        render_boat (
          s.r, s.dl, pose, who.driver, base, s.rod, s.time, who.passenger);
      }

      void draw (const FleetScene& s,
                 Machine,
                 const JetSkiPose& pose,
                 const Riders& who,
                 std::uint64_t base) {
        render_jet_ski (s.r, s.dl, pose, who.driver, base, who.passenger);
      }
    }
  }

  using namespace fleet_drawing;

  void draw_fleet (const FleetScene& scene,
                   const FleetPoses& fleet,
                   const FleetRiders& riders,
                   bool theirs) {
    for_each_machine (fleet, [&] (Machine m, const auto& pose) {
      const std::size_t i = static_cast<std::size_t> (m);
      if (scene.blob) {
        const meters_t radius = machine_kind (m).shadow_radius;
        if (radius > 0.0f * u::m)
          scene.blob->draw (scene.dl, *scene.surface, pose.position, radius);
        if constexpr (requires { pose.trailer_position; })
          scene.blob->draw (scene.dl,
                            *scene.surface,
                            pose.trailer_position,
                            trailer_shadow_radius);
      }
      draw (scene, m, pose, riders[i], motion_bases[theirs][i]);
    });
  }

  void cast_fleet (render::Renderer& r,
                   render::DrawList& dl,
                   const FleetPoses& fleet) {
    const FleetScene scene { .r = r, .dl = dl };
    for_each_machine (fleet, [&] (Machine m, const auto& pose) {
      draw (scene, m, pose, Riders {}, 0);
    });
  }
}
