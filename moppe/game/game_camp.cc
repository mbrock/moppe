// What the walker does by hand, as the HUD and the scripts ask after it
// (the doing is the session's step, surroundings.hh), and the camp's
// hammock, fire, and pan as drawn.

#include <moppe/game/figure.hh>
#include <moppe/game/game.hh>
#include <moppe/game/walker_render.hh>

namespace moppe {
  namespace game {
    Surroundings MoppeGame::surroundings (const AnimalGround* animals) const {
      return {
        .world = &world (),
        .surface = &surface (),
        .water = generated_world ().water_view (),
        .trunks = &m_trunk_field,
        .mushrooms = &m_mushrooms,
        .places = &m_places.layout,
        .bus = &m_places.bus,
        .nests = m_places.nests,
        .egg_seed = m_places.egg_seed (),
        .animal_ground = animals,
        .day_pace = m_sky.day ().pace,
        .day_turning = m_sky.day ().turning,
        .daylight = m_sky.sky ().daylight,
        .companions = m_company.snapshots (),
        .companion_clock = m_company.hours >= clock_time (0.0)
                             ? std::optional (m_company.hours)
                             : std::nullopt,
      };
    }

    HudWorld MoppeGame::hud_world () const {
      return { .session = session (),
               .world = generated_world (),
               .ground = world_ground (),
               .around = surroundings (),
               .company = m_company,
               .mushrooms = m_mushrooms,
               .places = m_places.layout,
               .bus = m_places.bus,
               .place_names = m_place_names,
               .home_base = m_home_base_position,
               .sky = m_sky.sky () };
    }

    ScriptStage MoppeGame::script_stage () {
      return { .session = session (),
               .ground = world_ground (),
               .around = surroundings (),
               .places = m_places,
               .mushrooms = m_mushrooms,
               .spawn = m_spawn_position,
               .cameras = m_capture_camera,
               .renderer = m_renderer };
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
                  camp.pan.yaw.numerical_value_in (u::rad) * u::rad,
                  m_pan_contents,
                  pan_cooked (camp.pan, now),
                  camp.pan.has_fish ? &camp.pan.fish : nullptr,
                  camp.pan.eggs);
      }
    }

    bool MoppeGame::lying_down () const {
      const Camp& camp = logic ().m_camp;
      return camp.hung && logic ().m_doing == Doing::on_foot () &&
             hammock_load (camp, logic ().m_total_time) >
               0.5f * proportion[one];
    }

    void MoppeGame::draw_sleeper (render::DrawList& dl,
                                  bool through_own_eyes,
                                  seconds_t time) {
      const Camp& camp = logic ().m_camp;
      if (!through_own_eyes)
        figure::draw (
          dl,
          pose_resting (
            camp.hammock,
            camp.swing,
            resting_gaze (camp.hammock, camp.gaze_yaw, camp.gaze_pitch),
            time),
          m_company.outfit);
      // The basket waits on the ground under the foot of the hammock.
      const direction_t across = hammock_across (camp.hammock);
      const direction_t way = hammock_along (camp.hammock);
      Vec3 floor = position_value (hammock_middle (camp.hammock)) +
                   direction_value (across) * 0.75f -
                   direction_value (way) * 0.5f;
      floor[1] = terrain::surface_elevation_value (
        world_ground ().elevation (plan (position (floor))));
      render_basket_set_down (dl, position (floor), way, m_basket_contents);
    }
  }
}
