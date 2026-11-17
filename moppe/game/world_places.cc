// The places at the ends of the roads and what lives there: farms and their
// barns, the animals and poultry, eggs in the nest boxes, and the
// sanctuaries' beacons (world_places.hh).

#include <moppe/game/camp.hh>
#include <moppe/game/farm_model.hh>
#include <moppe/game/world_places.hh>
#include <moppe/profile.hh>
#include <moppe/terrain/readings.hh>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace moppe::game {
  void WorldPlaces::plan (const WorldGround& ground,
                          GameSession& session,
                          std::uint32_t world_seed,
                          std::span<const std::string> names) {
    MOPPE_PROFILE_ZONE ("startup.plan_places");
    m_seed = world_seed;
    layout = {};
    layout = plan_places (ground.generated.roads (),
                          ground.place_ground (),
                          static_cast<std::uint32_t> (world_seed) ^ 0x91ace5u);
    // The bus's stops, kept clear of trees and rocks.
    bus = plan_bus_line (ground.generated.roads (),
                         names,
                         layout,
                         ground.place_ground (),
                         world_seed ^ 0xb05u);
    for (const BusStop& stop : bus.stops ())
      layout.clearings.push_back ({ stop.shelter.value_or (stop.sign),
                                    stop.shelter ? 4.0f * u::m : 2.0f * u::m });
    session.logic ().m_beacons.assign (layout.sanctuaries.size (), Beacon {});
    // Each barn lived in has its row of nest boxes, with the eggs laid
    // in them before the walker came.
    nests.clear ();
    for (const Building* barn : kept_barns ()) {
      const Mat4 frame = building_frame (*barn);
      const Vec3 out = frame.transform_vector (Vec3 (1, 0, 0));
      for (const Vec3& nest : { farm_model::barn_nest_0,
                                farm_model::barn_nest_1,
                                farm_model::barn_nest_2,
                                farm_model::barn_nest_3 })
        nests.push_back ({ .at = moppe::position (
                             frame.transform_point (nest + Vec3 (0, 0.03f, 0))),
                           .out = unit_direction (out) });
    }
    session.logic ().m_nests =
      first_eggs (nests.size (), session.logic ().m_day_hours, egg_seed ());
    // The farmhouses and the barns can be walked into; the camera
    // keeps inside.
    std::vector<ChaseCamera::Room> rooms;
    for (const Farm& farm : layout.farms)
      for (const Building& b : farm.buildings) {
        if (farm.ruined || b.kind == Building::Kind::mill)
          continue;
        Vec3 f = direction_value (b.forward);
        f[1] = 0.0f;
        f = normalized (f);
        const bool barn = b.kind == Building::Kind::barn;
        const meters_t half_x =
          barn ? barn_inside_half_x - 0.1f * u::m : 3.38f * u::m;
        const meters_t half_z =
          barn ? barn_inside_half_z - 0.1f * u::m : 2.38f * u::m;
        const meters_t floor = barn ? 0.3f * u::m : 0.6f * u::m;
        const meters_t ceiling = barn ? 3.4f * u::m : 2.96f * u::m;
        rooms.push_back ({ .centre = b.at,
                           .x = unit_direction (Vec3 (f[2], 0.0f, -f[0])),
                           .half_x = half_x,
                           .half_z = half_z,
                           .floor = floor,
                           .ceiling = ceiling });
      }
    session.set_rooms (std::move (rooms), layout.period);
    session.logic ().m_their_beacons.assign (session.logic ().m_beacons.size (),
                                             Beacon {});
    session.logic ().m_beacon_burn.assign (session.logic ().m_beacons.size (),
                                           0.0f * proportion[one]);
  }

  void WorldPlaces::build (render::Renderer* renderer,
                           const WorldGround& ground) {
    MOPPE_PROFILE_ZONE ("startup.build_places");
    const PlaceGround placed = ground.place_ground ();
    place_camps (
      layout,
      ground.generated.roads (),
      placed,
      [&ground] (const position_t& near) -> std::optional<position_t> {
        const std::vector<mov::Trunk> trunks =
          ground.trunks.gather (near, 90.0f * u::m);
        const std::optional<HammockSite> site = nearest_hammock_site (
          trunks, near, ground.elevation_field (), 0.6f * u::m);
        if (!site)
          return std::nullopt;
        const position_t middle = hammock_middle (*site);
        const Vec3& m = position_value (middle);
        const Vec3& n = position_value (near);
        if (std::hypot (m[0] - n[0], m[2] - n[2]) * u::m > 80.0f * u::m)
          return std::nullopt;
        return middle;
      });
    if (!renderer)
      return;
    landscape.rebuild (*renderer, layout, placed);
    bus_view.rebuild (*renderer, bus, layout.period);
  }

  std::vector<const Building*> WorldPlaces::kept_barns () const {
    std::vector<const Building*> out;
    for (const Farm& farm : layout.farms)
      if (farm.kept && !farm.ruined)
        for (const Building& b : farm.buildings)
          if (b.kind == Building::Kind::barn)
            out.push_back (&b);
    return out;
  }

  std::vector<Stall> WorldPlaces::barn_stalls () const {
    std::vector<Stall> stalls;
    std::uint32_t n = 0;
    for (const Building* barn : kept_barns ()) {
      const Mat4 frame = building_frame (*barn);
      const Vec3 back = frame.transform_vector (Vec3 (0, 0, -1));
      const Vec3 side = frame.transform_vector (Vec3 (1, 0, 0));
      const radians_t to_manger = std::atan2 (back[0], back[2]) * u::rad;
      const radians_t along_x = std::atan2 (side[0], side[2]) * u::rad;
      const auto at = [&] (const Vec3& point, float z) {
        return frame.transform_point (Vec3 (point[0], point[1], z));
      };
      const Vec3 stall_points[4] = { farm_model::barn_stall_0,
                                     farm_model::barn_stall_1,
                                     farm_model::barn_stall_2,
                                     farm_model::barn_stall_3 };
      for (int k = 0; k < 4; ++k)
        stalls.push_back ({ .species = k < 2 ? Species::horse : Species::cow,
                            .at = moppe::position (
                              at (stall_points[k], k < 2 ? -1.95f : -2.1f)),
                            .heading = to_manger });
      const Vec3 lo = farm_model::barn_pen_low;
      const Vec3 hi = farm_model::barn_pen_high;
      const Vec3 middle = (lo + hi) * 0.5f;
      stalls.push_back ({ .species = Species::pig,
                          .at = moppe::position (at (middle, middle[2])),
                          .heading = along_x,
                          .half_along = 0.5f * std::abs (hi[0] - lo[0]) * u::m,
                          .half_across = 0.5f * std::abs (hi[2] - lo[2]) * u::m,
                          .count = 2 + static_cast<int> (++n % 2) });
    }
    return stalls;
  }

  AnimalGround WorldPlaces::animal_ground (const WorldGround& ground) const {
    return {
      .ground =
        [ground, this] (plan_position_t p) {
          const terrain::SurfaceElevation height = ground.elevation (p);
          if (const std::optional<terrain::SurfaceElevation> floor =
                floor_at (layout, p))
            return std::max (*floor, height);
          return height;
        },
      .dry =
        [ground] (plan_position_t p) {
          return ground.still_water (p) < ground.elevation (p) + 0.05f * u::m;
        },
      .pasture =
        [ground] (plan_position_t p) {
          const float x = plan_value (p).x (), z = plan_value (p).z ();
          const auto read = [&] (auto quantity) {
            return spatial::sample<quantity> (
                     ground.generated.readings (),
                     moppe::position (Vec3 (x, 0.0f, z)))
              .numerical_value_in (one);
          };
          return Pasture {
            .grazed = read (map::grazing_pressure) * proportion[one],
            .forest = read (map::forest_cover) * proportion[one],
            // The fjäll's dwarf birch and fell birch, and its lichen
            // heath, but not the lichen of the dry pine forest below.
            .fell =
              std::min (1.0f,
                        1.5f * read (map::dwarf_birch_abundance) +
                          read (map::mountain_birch_abundance) +
                          read (map::lichen_abundance) *
                            smoothstep (
                              0.05f, 0.2f, read (map::dwarf_birch_abundance))) *
              proportion[one],
            .bog = read (map::sphagnum_abundance) * proportion[one],
          };
        },
      .clear =
        [ground] (const position_t& at_q, meters_t radius_q) {
          const Vec3& at = position_value (at_q);
          const float radius = meters_value (radius_q);
          const mov::TrunkContact contact =
            ground.trunks.collide (position (at + Vec3 (0, radius + 0.2f, 0)),
                                   position (at + Vec3 (0, radius + 0.8f, 0)),
                                   radius * u::m);
          Vec3 out = at;
          if (contact.hit) {
            out[0] += displacement_value (contact.push)[0];
            out[2] += displacement_value (contact.push)[2];
          }
          return moppe::position (out);
        },
      .period = ground.period (),
    };
  }

  void WorldPlaces::let_out_animals (const WorldGround& ground,
                                     GameSession& session,
                                     const position_t& home_base,
                                     const position_t& spawn) const {
    MOPPE_PROFILE_ZONE ("startup.let_out_animals");
    session.logic ().m_last_subject.reset ();
    const terrain::RoadNetwork& roads = ground.generated.roads ();
    const auto lived_in = [&roads] (std::size_t place) {
      return place < roads.places.size () &&
             roads.places[place].abandoned <= age_t::zero ();
    };
    std::vector<Fold> folds;
    for (const Farm& farm : layout.farms)
      if (farm.kept)
        for (const Building& b : farm.buildings)
          if (b.kind == Building::Kind::house)
            folds.push_back ({ .kind = Fold::Kind::farm, .at = b.at });
    if (layout.landing && lived_in (layout.landing->place))
      folds.push_back (
        { .kind = Fold::Kind::landing, .at = layout.landing->jetty.root });
    for (const Sanctuary& s : layout.sanctuaries)
      if (s.kept)
        folds.push_back ({ .kind = Fold::Kind::sanctuary, .at = s.centre });
    folds.push_back ({ .kind = Fold::Kind::trailhead, .at = home_base });
    const AnimalGround animals = animal_ground (ground);
    session.animals ().rebuild (plan_animals (animals,
                                              folds,
                                              home_base,
                                              animals.period,
                                              m_seed ^ 0xa417a15u,
                                              barn_stalls ()));
    std::vector<Coop> coops;
    for (const Building* barn : kept_barns ())
      coops.push_back ({ .frame = building_frame (*barn) });
    std::vector<Bird> birds = plan_birds (coops, egg_seed ());
    for (Bird& b : birds)
      b.position = terrain::at_elevation (
        moppe::plan (b.position), animals.ground (moppe::plan (b.position)));
    std::cerr << "moppe: animals: " << birds.size () << " hens, roosters,"
              << " and chicks in " << coops.size () << " barns, "
              << nests.size () << " nest boxes" << std::endl;
    session.hens ().rebuild (std::move (coops), std::move (birds));
    // And where the barn nearest the spawn keeps them, to aim at.
    const Building* barn = nullptr;
    for (const Building* b : kept_barns ()) {
      const displacement_t to_b = b->at - spawn;
      if (!barn) {
        barn = b;
        continue;
      }
      const displacement_t to_barn = barn->at - spawn;
      if (dot (to_b, to_b) < dot (to_barn, to_barn))
        barn = b;
    }
    if (barn) {
      const Mat4 frame = building_frame (*barn);
      const Vec3 nest = frame.transform_point (farm_model::barn_nest_1);
      const Vec3 door = frame.transform_point (farm_model::barn_doorway);
      std::cerr << "moppe: animals: the nearest barn's nest boxes at "
                << nest[0] << ' ' << nest[1] << ' ' << nest[2]
                << ", its doors at " << door[0] << ' ' << door[1] << ' '
                << door[2] << " facing " << direction_value (barn->forward)[0]
                << ' ' << direction_value (barn->forward)[2] << std::endl;
    }
    // Say where the nearest herd of each kind is.
    for (int s = 0; s < species_count; ++s) {
      const Animal* nearest = nullptr;
      meters_t best = 1e9f * u::m;
      const Vec3& from = position_value (spawn);
      for (const Animal& a : session.animals ().animals ()) {
        const meters_t d =
          std::hypot (position_value (a.position)[0] - from[0],
                      position_value (a.position)[2] - from[2]) *
          u::m;
        if (static_cast<int> (a.species) == s && d < best) {
          best = d;
          nearest = &a;
        }
      }
      if (nearest)
        std::cerr << "moppe: animals: the nearest "
                  << traits (nearest->species).plural << ' '
                  << std::lround (meters_value (best))
                  << " m from the spawn, at "
                  << std::lround (position_value (nearest->position)[0]) << ' '
                  << std::lround (position_value (nearest->position)[2])
                  << std::endl;
    }
  }

  void WorldPlaces::draw (render::Renderer& r,
                          render::DrawList& dl,
                          const FrameView& frame,
                          const GameLogicState& logic) {
    const position_t& camera = frame.camera.position;
    landscape.draw (r, camera, frame.lighting.time, 0x10000);
    landscape.draw_beacons (dl, camera, logic.m_beacon_burn);
    const Camp& camp = logic.m_camp;
    constexpr meters_t near = 200.0f * u::m;
    constexpr square_meters_t same2 = 0.04f * u::m * u::m;
    for (const position_t& ring : layout.hearths) {
      const displacement_t from_eye = ring - camera;
      if (dot (from_eye, from_eye) > near * near)
        continue;
      const displacement_t from_camp = camp.hearth - ring;
      if (hearth_laid (camp) && dot (from_camp, from_camp) < same2)
        continue;
      draw_hearth (dl, ring, 0.0f * proportion[one]);
    }
  }

  // Squared distances, as length2 measures them.
  namespace {
    square_meters_t world_places_far2 (const position_t& a,
                                       const position_t& b) {
      const displacement_t d = a - b;
      return dot (d, d);
    }
  }

  void WorldPlaces::light_beacons (FrameView& frame,
                                   std::span<const proportion_t> burn) const {
    for (std::size_t i = 0; i < layout.sanctuaries.size () && i < burn.size ();
         ++i) {
      if (burn[i] <= 0.01f * proportion[one])
        continue;
      const position_t lamp =
        position (position_value (layout.sanctuaries[i].hearth) +
                  Vec3 (0.0f, 0.7f, 0.0f));
      FrameLighting& lighting = frame.lighting;
      const position_t& camera = frame.camera.position;
      if (lighting.lamp_reach > 0.0f * u::m &&
          world_places_far2 (lighting.lamp_position, camera) <=
            world_places_far2 (lamp, camera))
        continue;
      lighting.lamp_position = lamp;
      lighting.lamp_reach = 1.8f * fire_light_reach;
      lighting.lamp_color = scale_display (
        fire_light_color (burn[i], lighting.time),
        1.4f * (1.0f - 0.25f * lighting.daylight.numerical_value_in (one)) *
          scale_factor[one]);
    }
  }

  void WorldPlaces::light_barn (FrameView& frame) const {
    FrameLighting& lighting = frame.lighting;
    const proportion_t daylight = lighting.daylight;
    const seconds_t time = lighting.time;
    const proportion_t dark =
      (1.0f - smoothstep (0.15f, 0.55f, daylight.numerical_value_in (one))) *
      proportion[one];
    const position_t& eye = frame.camera.position;
    constexpr meters_t near = 30.0f * u::m;
    const Building* nearest = nullptr;
    for (const Building* barn : kept_barns ())
      if (world_places_far2 (barn->at, eye) < near * near &&
          (!nearest || world_places_far2 (barn->at, eye) <
                         world_places_far2 (nearest->at, eye)))
        nearest = barn;
    if (!nearest)
      return;
    const Mat4 barn = building_frame (*nearest);
    const bool lantern = dark > 0.5f * proportion[one];
    // The barn's frame and the model's points are floats (farm_model).
    const position_t at = position (barn.transform_point (
      lantern ? farm_model::barn_lantern - Vec3 (0.0f, 0.2f, 0.0f)
              : Vec3 (0.0f, 2.4f, meters_value (barn_front) - 0.9f)));
    if (lighting.lamp_reach > 0.0f * u::m &&
        world_places_far2 (lighting.lamp_position, eye) <=
          world_places_far2 (at, eye))
      return;
    lighting.lamp_position = at;
    if (lantern) {
      const scale_factor_t flicker =
        (1.0f + 0.03f * std::sin (seconds_value (time) * 7.3f) +
         0.02f * std::sin (seconds_value (time) * 12.9f + 1.7f)) *
        scale_factor[one];
      lighting.lamp_reach = 11.0f * u::m;
      lighting.lamp_color =
        scale_display (DisplayColor (1.30f, 0.95f, 0.55f),
                       dark.numerical_value_in (one) *
                         flicker.numerical_value_in (one) * scale_factor[one]);
    } else {
      // The day's light, warmed by the boards it comes in over.
      lighting.lamp_reach = 13.0f * u::m;
      lighting.lamp_color =
        scale_display (DisplayColor (1.0f, 0.93f, 0.80f),
                       0.9f * (1.0f - dark.numerical_value_in (one)) *
                         daylight.numerical_value_in (one) * scale_factor[one]);
    }
  }
}
