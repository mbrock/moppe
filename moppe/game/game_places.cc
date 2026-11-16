// The places at the ends of the roads and what lives there: farms and their
// barns, the animals and poultry, eggs in the nest boxes, and the
// sanctuaries' beacons.

#include <moppe/game/game.hh>

namespace moppe {
  namespace game {
    PlaceGround MoppeGame::place_ground () const {
      const terrain::WaterSheets& sheets = generated_world ().water_surface ();
      const float sea = world ().water_level.numerical_value_in (u::m);
      const auto level = [this, &sheets] (float x, float z) {
        return still_water_at (world (), surface (), &sheets, Vec3 (x, 0.0f, z))
          .numerical_value_in (u::m);
      };
      return {
        .ground =
          [this] (float x, float z) {
            return ground_height (Vec3 (x, 0.0f, z));
          },
        .water = level,
        .sea = [level, sea] (float x,
                             float z) { return level (x, z) <= sea + 0.05f; },
        .current =
          [&sheets] (float x, float z) {
            return spatial::sample<terrain::water_velocity> (
                     sheets, moppe::position (Vec3 (x, 0.0f, z)))
              .numerical_value_in (u::m / u::s);
          },
        .forest =
          [this] (float x, float z) {
            return spatial::sample<map::forest_cover> (
                     surface_readings (), moppe::position (Vec3 (x, 0.0f, z)))
              .numerical_value_in (one);
          },
        .roads = &generated_world ().road_surface (),
        .sea_level = sea,
        .period_x = surface ().domain ().period_x ().numerical_value_in (u::m),
        .period_z = surface ().domain ().period_z ().numerical_value_in (u::m),
      };
    }

    void MoppeGame::plan_world_places () {
      MOPPE_PROFILE_ZONE ("startup.plan_places");
      m_places = {};
      m_places = plan_places (
        generated_world ().roads (),
        place_ground (),
        static_cast<std::uint32_t> (recipe ().seed ().value) ^ 0x91ace5u);
      logic ().m_beacons.assign (m_places.sanctuaries.size (), Beacon {});
      // Each barn lived in has its row of nest boxes, with the eggs laid
      // in them before the walker came.
      m_nest_sites.clear ();
      for (const Building* barn : kept_barns ()) {
        const Mat4 frame = building_frame (*barn);
        const Vec3 out = frame.transform_vector (Vec3 (1, 0, 0));
        for (const Vec3& nest : { farm_model::barn_nest_0,
                                  farm_model::barn_nest_1,
                                  farm_model::barn_nest_2,
                                  farm_model::barn_nest_3 })
          m_nest_sites.push_back (
            { .at = frame.transform_point (nest + Vec3 (0, 0.03f, 0)),
              .out = out });
      }
      logic ().m_nests =
        first_eggs (m_nest_sites.size (), logic ().m_day_hours, egg_seed ());
      // The farmhouses and the barns can be walked into; the camera
      // keeps inside.
      std::vector<ChaseCamera::Room> rooms;
      for (const Farm& farm : m_places.farms)
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
      session ().set_rooms (
        std::move (rooms), m_places.period_x, m_places.period_z);
      tend_beacons ();
    }

    void MoppeGame::build_world_places () {
      MOPPE_PROFILE_ZONE ("startup.build_places");
      const PlaceGround ground = place_ground ();
      place_camps (
        m_places,
        generated_world ().roads (),
        ground,
        [this] (const Vec3& near) -> std::optional<Vec3> {
          const std::vector<mov::Trunk> trunks =
            m_trunk_field.gather (near, 90.0f);
          const std::optional<HammockSite> site =
            nearest_hammock_site (trunks, near, ground_reader (), 0.6f);
          if (!site)
            return std::nullopt;
          const Vec3 middle = hammock_middle (*site);
          if (std::hypot (middle[0] - near[0], middle[2] - near[2]) > 80.0f)
            return std::nullopt;
          return middle;
        });
      if (!m_renderer)
        return;
      m_places_landscape.rebuild (*m_renderer, m_places, ground);
      if (!m_places_landscape.colliders ().empty ()) {
        std::vector<mov::Trunk> obstacles = m_forest.trunks ();
        obstacles.insert (obstacles.end (),
                          m_boulders.colliders ().begin (),
                          m_boulders.colliders ().end ());
        obstacles.insert (obstacles.end (),
                          m_places_landscape.colliders ().begin (),
                          m_places_landscape.colliders ().end ());
        const Vec3 period = extent_value (generated_world ().forest ().period);
        m_trunk_field.set_trunks (std::move (obstacles), period[0], period[2]);
      }
    }

    std::vector<const Building*> MoppeGame::kept_barns () const {
      std::vector<const Building*> out;
      for (const Farm& farm : m_places.farms)
        if (farm.kept && !farm.ruined)
          for (const Building& b : farm.buildings)
            if (b.kind == Building::Kind::barn)
              out.push_back (&b);
      return out;
    }

    std::uint32_t MoppeGame::egg_seed () const {
      return static_cast<std::uint32_t> (recipe ().seed ().value) ^ 0xe665u;
    }

    std::vector<Stall> MoppeGame::barn_stalls () const {
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
          stalls.push_back (
            { .species = k < 2 ? Species::horse : Species::cow,
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

    AnimalGround MoppeGame::animal_ground () const {
      const terrain::WaterSheets* sheets = &generated_world ().water_surface ();
      return {
        .ground =
          [this] (float x, float z) {
            const float ground = ground_height (Vec3 (x, 0.0f, z));
            if (const std::optional<float> floor = floor_at (m_places, x, z))
              return std::max (*floor, ground);
            return ground;
          },
        .dry =
          [this, sheets] (float x, float z) {
            const Vec3 at (x, 0.0f, z);
            return still_water_at (world (), surface (), sheets, at)
                     .numerical_value_in (u::m) < ground_height (at) + 0.05f;
          },
        .pasture =
          [this] (float x, float z) {
            const auto read = [&] (auto quantity) {
              return spatial::sample<quantity> (
                       surface_readings (), moppe::position (Vec3 (x, 0.0f, z)))
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
                    smoothstep (
                      0.05f, 0.2f, read (map::dwarf_birch_abundance))),
              .bog = read (map::sphagnum_abundance),
            };
          },
        .clear =
          [this] (const Vec3& at, float radius) {
            const mov::TrunkContact contact =
              m_trunk_field.collide (at + Vec3 (0, radius + 0.2f, 0),
                                     at + Vec3 (0, radius + 0.8f, 0),
                                     radius);
            Vec3 out = at;
            if (contact.hit) {
              out[0] += contact.push[0];
              out[2] += contact.push[2];
            }
            return out;
          },
        .period_x = surface ().domain ().period_x ().numerical_value_in (u::m),
        .period_z = surface ().domain ().period_z ().numerical_value_in (u::m),
      };
    }

    void MoppeGame::let_out_animals () {
      MOPPE_PROFILE_ZONE ("startup.let_out_animals");
      m_last_subject.reset ();
      const terrain::RoadNetwork& roads = generated_world ().roads ();
      const auto lived_in = [&roads] (std::size_t place) {
        return place < roads.places.size () &&
               roads.places[place].abandoned_years_ago <= 0.0f;
      };
      std::vector<Fold> folds;
      for (const Farm& farm : m_places.farms)
        if (farm.kept)
          for (const Building& b : farm.buildings)
            if (b.kind == Building::Kind::house)
              folds.push_back ({ .kind = Fold::Kind::farm, .at = b.at });
      if (m_places.landing && lived_in (m_places.landing->place))
        folds.push_back (
          { .kind = Fold::Kind::landing, .at = m_places.landing->jetty.root });
      for (const Sanctuary& s : m_places.sanctuaries)
        if (s.kept)
          folds.push_back ({ .kind = Fold::Kind::sanctuary, .at = s.centre });
      folds.push_back (
        { .kind = Fold::Kind::trailhead, .at = m_home_base_position });
      const AnimalGround ground = animal_ground ();
      m_animals.rebuild (plan_animals (
        ground,
        folds,
        m_home_base_position,
        ground.period_x,
        ground.period_z,
        static_cast<std::uint32_t> (recipe ().seed ().value) ^ 0xa417a15u,
        barn_stalls ()));
      std::vector<Coop> coops;
      for (const Building* barn : kept_barns ())
        coops.push_back ({ .frame = building_frame (*barn) });
      std::vector<Bird> birds = plan_birds (coops, egg_seed ());
      for (Bird& b : birds)
        b.position[1] = ground.ground (b.position[0], b.position[2]);
      std::cerr << "moppe: animals: " << birds.size () << " hens, roosters,"
                << " and chicks in " << coops.size () << " barns, "
                << m_nest_sites.size () << " nest boxes" << std::endl;
      m_hens.rebuild (std::move (coops), std::move (birds));
      // And where the barn nearest the spawn keeps them, to aim at.
      const Building* barn = nullptr;
      for (const Building* b : kept_barns ())
        if (!barn || length2 (b->at - m_spawn_position) <
                       length2 (barn->at - m_spawn_position))
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
        for (const Animal& a : m_animals.animals ()) {
          const float d = std::hypot (a.position[0] - m_spawn_position[0],
                                      a.position[2] - m_spawn_position[2]);
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

    void MoppeGame::tend_animals (seconds_t interval) {
      const float dt = seconds_value (interval);
      if (m_animals.animals ().empty () || dt <= 0.0f)
        return;
      std::vector<Presence> presences;
      const Vec3 subject = session ().subject_position ();
      Vec3 moving {};
      if (m_last_subject)
        moving = (subject - *m_last_subject) * (1.0f / dt);
      m_last_subject = subject;
      if (length2 (moving) > 60.0f * 60.0f)
        moving = Vec3 ();
      presences.push_back ({ .position = subject,
                             .velocity = moving,
                             .machine = logic ().m_mode != M_FOOT });
      if (m_companion && m_companion->walker)
        presences.push_back ({ .position = m_companion->walker->position,
                               .velocity = m_companion->walker->velocity });
      const AnimalGround ground = animal_ground ();
      m_animals.step (ground,
                      presences,
                      session ().camera ().position (),
                      700.0f * u::m,
                      interval);
      m_hens.step (ground,
                   presences,
                   session ().camera ().position (),
                   160.0f * u::m,
                   static_cast<float> (std::fmod (logic ().m_day_hours, 24.0)) *
                     si::hour,
                   interval);
      lay_eggs (logic ().m_nests, logic ().m_day_hours, egg_seed ());
    }

    std::optional<std::size_t> MoppeGame::beacon_at_hand () const {
      if (logic ().m_mode != M_FOOT || logic ().m_camp.resting)
        return std::nullopt;
      return sanctuary_at_hand (m_places, session ().walker ().position ());
    }

    void MoppeGame::tend_beacons () {
      const double now = logic ().m_total_time;
      std::vector<Beacon>& beacons = logic ().m_beacons;
      m_their_beacons.resize (beacons.size ());
      m_beacon_burn.resize (beacons.size ());
      for (std::size_t i = 0; i < beacons.size (); ++i) {
        Beacon& theirs = m_their_beacons[i];
        const bool lit =
          m_companion && i < 32 && (m_companion->beacons >> i & 1u);
        if (lit != theirs.burning ()) {
          (lit ? theirs.lit : theirs.out) = now;
          std::cerr << "moppe: places: the other player "
                    << (lit ? "lit" : "put out") << " the beacon on sanctuary "
                    << i << std::endl;
        }
        m_beacon_burn[i] =
          std::max (beacon_burn (beacons[i].lit, beacons[i].out, now),
                    beacon_burn (theirs.lit, theirs.out, now));
      }
    }

    bool MoppeGame::their_beacon (std::size_t i) const {
      return i < m_their_beacons.size () && m_their_beacons[i].burning ();
    }

    std::optional<std::size_t> MoppeGame::egg_at_hand () const {
      if (logic ().m_mode != M_FOOT || logic ().m_camp.resting)
        return std::nullopt;
      return nest_within_reach (m_nest_sites,
                                logic ().m_nests,
                                session ().walker ().position (),
                                session ().walker ().heading ());
    }

    void MoppeGame::draw_places (render::Renderer& r, const FrameView& frame) {
      const Vec3& camera = frame.camera.position;
      m_places_landscape.draw (r, camera, frame.lighting.time, 0x10000);
      m_places_landscape.draw_beacons (m_world_dl, camera, m_beacon_burn);
      const Camp& camp = logic ().m_camp;
      for (const Vec3& ring : m_places.hearths) {
        if (length2 (ring - camera) > 200.0f * 200.0f)
          continue;
        if (camp.fire_time > -50.0 && length2 (camp.hearth - ring) < 0.04f)
          continue;
        draw_hearth (m_world_dl, ring, 0.0f);
      }
    }

    void MoppeGame::light_beacons (FrameView& frame) const {
      for (std::size_t i = 0;
           i < m_places.sanctuaries.size () && i < m_beacon_burn.size ();
           ++i) {
        if (m_beacon_burn[i] <= 0.01f)
          continue;
        const Vec3 lamp =
          m_places.sanctuaries[i].hearth + Vec3 (0.0f, 0.7f, 0.0f);
        FrameLighting& lighting = frame.lighting;
        if (lighting.lamp_reach > 0.0f &&
            length2 (lighting.lamp_position - frame.camera.position) <=
              length2 (lamp - frame.camera.position))
          continue;
        lighting.lamp_position = lamp;
        lighting.lamp_reach = 1.8f * meters_value (fire_light_reach);
        lighting.lamp_color =
          scale_display (fire_light_color (m_beacon_burn[i], lighting.time),
                         1.4f * (1.0f - 0.25f * lighting.daylight));
      }
    }

    void MoppeGame::light_barn (FrameView& frame) const {
      FrameLighting& lighting = frame.lighting;
      const float dark = 1.0f - smoothstep (0.15f, 0.55f, lighting.daylight);
      const Vec3 eye = frame.camera.position;
      const Building* nearest = nullptr;
      for (const Building* barn : kept_barns ())
        if (length2 (barn->at - eye) < 30.0f * 30.0f &&
            (!nearest ||
             length2 (barn->at - eye) < length2 (nearest->at - eye)))
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
}
