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
                          std::uint32_t world_seed) {
    MOPPE_PROFILE_ZONE ("startup.plan_places");
    m_seed = world_seed;
    layout = {};
    layout = plan_places (ground.generated.roads (),
                          ground.place_ground (),
                          static_cast<std::uint32_t> (world_seed) ^ 0x91ace5u);
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
        nests.push_back (
          { .at = frame.transform_point (nest + Vec3 (0, 0.03f, 0)),
            .out = out });
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
        Vec3 f = b.forward;
        f[1] = 0.0f;
        f = normalized (f);
        const bool barn = b.kind == Building::Kind::barn;
        rooms.push_back ({ .centre = b.at,
                           .x = Vec3 (f[2], 0.0f, -f[0]),
                           .half_x = barn ? barn_inside_half_x - 0.1f : 3.38f,
                           .half_z = barn ? barn_inside_half_z - 0.1f : 2.38f,
                           .floor = barn ? 0.3f : 0.6f,
                           .ceiling = barn ? 3.4f : 2.96f });
      }
    session.set_rooms (std::move (rooms), layout.period_x, layout.period_z);
    session.logic ().m_their_beacons.assign (session.logic ().m_beacons.size (),
                                             Beacon {});
    session.logic ().m_beacon_burn.assign (session.logic ().m_beacons.size (),
                                           0.0f);
  }

  void WorldPlaces::build (render::Renderer* renderer,
                           const WorldGround& ground) {
    MOPPE_PROFILE_ZONE ("startup.build_places");
    const PlaceGround placed = ground.place_ground ();
    place_camps (
      layout,
      ground.generated.roads (),
      placed,
      [&ground] (const Vec3& near) -> std::optional<Vec3> {
        const std::vector<mov::Trunk> trunks =
          ground.trunks.gather (near, 90.0f);
        const std::optional<HammockSite> site =
          nearest_hammock_site (trunks, near, ground.reader (), 0.6f);
        if (!site)
          return std::nullopt;
        const Vec3 middle = hammock_middle (*site);
        if (std::hypot (middle[0] - near[0], middle[2] - near[2]) > 80.0f)
          return std::nullopt;
        return middle;
      });
    if (!renderer)
      return;
    landscape.rebuild (*renderer, layout, placed);
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
      const float to_manger = std::atan2 (back[0], back[2]);
      const float along_x = std::atan2 (side[0], side[2]);
      const auto at = [&] (const Vec3& point, float z) {
        return frame.transform_point (Vec3 (point[0], point[1], z));
      };
      const Vec3 stall_points[4] = { farm_model::barn_stall_0,
                                     farm_model::barn_stall_1,
                                     farm_model::barn_stall_2,
                                     farm_model::barn_stall_3 };
      for (int k = 0; k < 4; ++k)
        stalls.push_back ({ .species = k < 2 ? Species::horse : Species::cow,
                            .at = at (stall_points[k], k < 2 ? -1.95f : -2.1f),
                            .heading = to_manger });
      const Vec3 lo = farm_model::barn_pen_low;
      const Vec3 hi = farm_model::barn_pen_high;
      const Vec3 middle = (lo + hi) * 0.5f;
      stalls.push_back ({ .species = Species::pig,
                          .at = at (middle, middle[2]),
                          .heading = along_x,
                          .half_along = 0.5f * std::abs (hi[0] - lo[0]),
                          .half_across = 0.5f * std::abs (hi[2] - lo[2]),
                          .count = 2 + static_cast<int> (++n % 2) });
    }
    return stalls;
  }

  AnimalGround WorldPlaces::animal_ground (const WorldGround& ground) const {
    return {
      .ground =
        [ground, this] (float x, float z) {
          const float height = ground.height (Vec3 (x, 0.0f, z));
          if (const std::optional<float> floor = floor_at (layout, x, z))
            return std::max (*floor, height);
          return height;
        },
      .dry =
        [ground] (float x, float z) {
          const Vec3 at (x, 0.0f, z);
          return ground.still_level (at) < ground.height (at) + 0.05f;
        },
      .pasture =
        [ground] (float x, float z) {
          const auto read = [&] (auto quantity) {
            return spatial::sample<quantity> (
                     ground.generated.readings (),
                     moppe::position (Vec3 (x, 0.0f, z)))
              .numerical_value_in (one);
          };
          return Pasture {
            .grazed = read (map::grazing_pressure),
            .forest = read (map::forest_cover),
            // The fjäll's dwarf birch and fell birch, and its lichen
            // heath, but not the lichen of the dry pine forest below.
            .fell = std::min (
              1.0f,
              1.5f * read (map::dwarf_birch_abundance) +
                read (map::mountain_birch_abundance) +
                read (map::lichen_abundance) *
                  smoothstep (0.05f, 0.2f, read (map::dwarf_birch_abundance))),
            .bog = read (map::sphagnum_abundance),
          };
        },
      .clear =
        [ground] (const Vec3& at, float radius) {
          const mov::TrunkContact contact =
            ground.trunks.collide (at + Vec3 (0, radius + 0.2f, 0),
                                   at + Vec3 (0, radius + 0.8f, 0),
                                   radius);
          Vec3 out = at;
          if (contact.hit) {
            out[0] += contact.push[0];
            out[2] += contact.push[2];
          }
          return out;
        },
      .period_x = ground.period_x (),
      .period_z = ground.period_z (),
    };
  }

  void WorldPlaces::let_out_animals (const WorldGround& ground,
                                     GameSession& session,
                                     const Vec3& home_base,
                                     const Vec3& spawn) const {
    MOPPE_PROFILE_ZONE ("startup.let_out_animals");
    session.logic ().m_last_subject.reset ();
    const terrain::RoadNetwork& roads = ground.generated.roads ();
    const auto lived_in = [&roads] (std::size_t place) {
      return place < roads.places.size () &&
             roads.places[place].abandoned_years_ago <= 0.0f;
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
                                              animals.period_x,
                                              animals.period_z,
                                              m_seed ^ 0xa417a15u,
                                              barn_stalls ()));
    std::vector<Coop> coops;
    for (const Building* barn : kept_barns ())
      coops.push_back ({ .frame = building_frame (*barn) });
    std::vector<Bird> birds = plan_birds (coops, egg_seed ());
    for (Bird& b : birds)
      b.position[1] = animals.ground (b.position[0], b.position[2]);
    std::cerr << "moppe: animals: " << birds.size () << " hens, roosters,"
              << " and chicks in " << coops.size () << " barns, "
              << nests.size () << " nest boxes" << std::endl;
    session.hens ().rebuild (std::move (coops), std::move (birds));
    // And where the barn nearest the spawn keeps them, to aim at.
    const Building* barn = nullptr;
    for (const Building* b : kept_barns ())
      if (!barn || length2 (b->at - spawn) < length2 (barn->at - spawn))
        barn = b;
    if (barn) {
      const Mat4 frame = building_frame (*barn);
      const Vec3 nest = frame.transform_point (farm_model::barn_nest_1);
      const Vec3 door = frame.transform_point (farm_model::barn_doorway);
      std::cerr << "moppe: animals: the nearest barn's nest boxes at "
                << nest[0] << ' ' << nest[1] << ' ' << nest[2]
                << ", its doors at " << door[0] << ' ' << door[1] << ' '
                << door[2] << " facing " << barn->forward[0] << ' '
                << barn->forward[2] << std::endl;
    }
    // Say where the nearest herd of each kind is.
    for (int s = 0; s < species_count; ++s) {
      const Animal* nearest = nullptr;
      float best = 1e9f;
      for (const Animal& a : session.animals ().animals ()) {
        const float d =
          std::hypot (a.position[0] - spawn[0], a.position[2] - spawn[2]);
        if (static_cast<int> (a.species) == s && d < best) {
          best = d;
          nearest = &a;
        }
      }
      if (nearest)
        std::cerr << "moppe: animals: the nearest "
                  << traits (nearest->species).plural << ' '
                  << std::lround (best) << " m from the spawn, at "
                  << std::lround (nearest->position[0]) << ' '
                  << std::lround (nearest->position[2]) << std::endl;
    }
  }

  void WorldPlaces::draw (render::Renderer& r,
                          render::DrawList& dl,
                          const FrameView& frame,
                          const GameLogicState& logic) {
    const Vec3& camera = frame.camera.position;
    landscape.draw (r, camera, frame.lighting.time, 0x10000);
    landscape.draw_beacons (dl, camera, logic.m_beacon_burn);
    const Camp& camp = logic.m_camp;
    for (const Vec3& ring : layout.hearths) {
      if (length2 (ring - camera) > 200.0f * 200.0f)
        continue;
      if (hearth_laid (camp) && length2 (camp.hearth - ring) < 0.04f)
        continue;
      draw_hearth (dl, ring, 0.0f);
    }
  }

  void WorldPlaces::light_beacons (FrameView& frame,
                                   const std::vector<float>& burn) const {
    for (std::size_t i = 0; i < layout.sanctuaries.size () && i < burn.size ();
         ++i) {
      if (burn[i] <= 0.01f)
        continue;
      const Vec3 lamp = layout.sanctuaries[i].hearth + Vec3 (0.0f, 0.7f, 0.0f);
      FrameLighting& lighting = frame.lighting;
      if (lighting.lamp_reach > 0.0f &&
          length2 (lighting.lamp_position - frame.camera.position) <=
            length2 (lamp - frame.camera.position))
        continue;
      lighting.lamp_position = lamp;
      lighting.lamp_reach = 1.8f * meters_value (fire_light_reach);
      lighting.lamp_color =
        scale_display (fire_light_color (burn[i], lighting.time),
                       1.4f * (1.0f - 0.25f * lighting.daylight));
    }
  }

  void WorldPlaces::light_barn (FrameView& frame) const {
    FrameLighting& lighting = frame.lighting;
    const float dark = 1.0f - smoothstep (0.15f, 0.55f, lighting.daylight);
    const Vec3 eye = frame.camera.position;
    const Building* nearest = nullptr;
    for (const Building* barn : kept_barns ())
      if (length2 (barn->at - eye) < 30.0f * 30.0f &&
          (!nearest || length2 (barn->at - eye) < length2 (nearest->at - eye)))
        nearest = barn;
    if (!nearest)
      return;
    const Mat4 barn = building_frame (*nearest);
    const bool lantern = dark > 0.5f;
    const Vec3 at = barn.transform_point (
      lantern ? farm_model::barn_lantern - Vec3 (0.0f, 0.2f, 0.0f)
              : Vec3 (0.0f, 2.4f, barn_front - 0.9f));
    if (lighting.lamp_reach > 0.0f &&
        length2 (lighting.lamp_position - eye) <= length2 (at - eye))
      return;
    lighting.lamp_position = at;
    if (lantern) {
      const float flicker = 1.0f + 0.03f * std::sin (lighting.time * 7.3f) +
                            0.02f * std::sin (lighting.time * 12.9f + 1.7f);
      lighting.lamp_reach = 11.0f;
      lighting.lamp_color =
        scale_display (DisplayColor (1.30f, 0.95f, 0.55f), dark * flicker);
    } else {
      // The day's light, warmed by the boards it comes in over.
      lighting.lamp_reach = 13.0f;
      lighting.lamp_color =
        scale_display (DisplayColor (1.0f, 0.93f, 0.80f),
                       0.9f * (1.0f - dark) * lighting.daylight);
    }
  }
}
