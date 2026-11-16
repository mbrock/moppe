// What the walker does by hand, as the HUD and the scripts ask after it
// (the doing is the session's step, surroundings.hh), and the camp's
// hammock, fire, and pan as drawn.

#include <moppe/game/game.hh>

namespace moppe {
  namespace game {
    Surroundings MoppeGame::surroundings (const AnimalGround* animals) const {
      return {
        .world = &world (),
        .surface = &surface (),
        .water = &generated_world ().water_surface (),
        .trunks = &m_trunk_field,
        .mushrooms = &m_mushrooms,
        .places = &m_places,
        .nests = m_nest_sites,
        .egg_seed = egg_seed (),
        .animal_ground = animals,
        .day_pace = m_day.pace,
        .day_turning = m_day.turning,
        .daylight = m_sky.daylight,
        .companion = m_companion ? &*m_companion : nullptr,
        .companion_clock = m_companion_hours >= 0.0
                             ? std::optional (clock_time (m_companion_hours))
                             : std::nullopt,
      };
    }

    bool MoppeGame::beside_hammock () const {
      return game::beside_hammock (session ());
    }

    std::optional<Vec3> MoppeGame::hearth_ahead () const {
      return game::hearth_ahead (session (), surroundings ());
    }

    std::optional<Vec3> MoppeGame::fire_at_hand () const {
      return game::fire_at_hand (session (), surroundings ());
    }

    HudState::Cook MoppeGame::supper_at_hand () const {
      return game::supper_at_hand (session (), surroundings ());
    }

    std::optional<Angler> MoppeGame::angler () const {
      return game::angler (session ());
    }

    bool MoppeGame::boat_drifting () const {
      return game::boat_drifting (session (), surroundings ());
    }

    std::optional<FishingWater> MoppeGame::water_ahead () const {
      return game::water_ahead (session (), surroundings ());
    }

    std::optional<std::size_t> MoppeGame::beacon_at_hand () const {
      return game::beacon_at_hand (session (), surroundings ());
    }

    bool MoppeGame::their_beacon (std::size_t i) const {
      return game::their_beacon (logic (), i);
    }

    std::optional<std::size_t> MoppeGame::egg_at_hand () const {
      return game::egg_at_hand (session (), surroundings ());
    }

    void MoppeGame::draw_camp (render::DrawList& dl) {
      const Camp& camp = logic ().m_camp;
      const play_time_t now = logic ().m_total_time;
      if (camp.hung)
        draw_hammock (dl, camp.hammock, hammock_load (camp, now), camp.swing);
      if (hearth_laid (camp))
        draw_hearth (dl, camp.hearth, fire_burn (camp, now));
      if (camp.pan.on) {
        m_pan_contents.clear ();
        for (int i = 0; i < camp.pan.count; ++i)
          if (camp.pan.mushrooms[i] < m_mushrooms.mushroom_count ())
            m_pan_contents.push_back (m_mushrooms.site (camp.pan.mushrooms[i]));
        draw_pan (dl,
                  camp.pan.place,
                  camp.pan.yaw,
                  m_pan_contents,
                  pan_cooked (camp.pan, now),
                  camp.pan.has_fish ? &camp.pan.fish : nullptr,
                  camp.pan.eggs);
      }
    }

    bool MoppeGame::lying_down () const {
      const Camp& camp = logic ().m_camp;
      return camp.hung && logic ().m_mode == M_FOOT &&
             hammock_load (camp, logic ().m_total_time) > 0.5f;
    }

    void MoppeGame::draw_sleeper (render::DrawList& dl,
                                  bool through_own_eyes,
                                  float time) {
      const Camp& camp = logic ().m_camp;
      if (!through_own_eyes)
        figure::draw (
          dl,
          pose_resting (
            camp.hammock,
            camp.swing,
            resting_gaze (camp.hammock, camp.gaze_yaw, camp.gaze_pitch),
            time),
          m_outfit);
      // The basket waits on the ground under the foot of the hammock.
      Vec3 floor = hammock_middle (camp.hammock) +
                   hammock_across (camp.hammock) * 0.75f -
                   hammock_along (camp.hammock) * 0.5f;
      floor[1] = ground_height (floor);
      render_basket_set_down (
        dl, floor, hammock_along (camp.hammock), m_basket_contents);
    }
  }
}
