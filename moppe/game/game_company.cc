// The other players (companionship.hh) as this game meets them: each
// frame's exchange, and making room beside senior peers when first met.

#include <moppe/game/game.hh>

#include <cmath>

namespace moppe {
  namespace game {
    std::uint32_t MoppeGame::this_world () const {
      return m_company_world_tag;
    }

    WorldSummary MoppeGame::this_world_summary () const {
      const meters_t extent = extent_value (recipe ().extent ())[0] * u::m;
      return {
        .tag = m_company_world_tag,
        .seed = static_cast<std::uint32_t> (recipe ().seed ().value),
        .resolution = static_cast<std::uint16_t> (recipe ().resolution ()),
        .extent_hm = static_cast<std::uint16_t> (
          std::lround (meters_value (extent) / 100.0f)),
      };
    }

    void MoppeGame::make_room (std::span<const CompanionSnapshot> newly_met) {
      if (newly_met.empty () || logic ().m_doing != Doing::on_foot () ||
          !m_company.link)
        return;

      const play_time_t my_play = logic ().m_total_time;
      const std::uint64_t my_id = m_company.link->player ();
      const plan_extent_t period = world_ground ().period ();
      constexpr duration_t seniority_margin = 5.0 * si::second;

      // Symmetric seniority: a player who has been in the world clearly
      // longer keeps their spot; simultaneous launches tie-break by player ID
      // so exactly one player stays and each newcomer steps into a distinct
      // lateral rank.
      int senior_count = 0;
      bool trailhead_occupied = false;
      std::vector<position_t> peer_bikes;
      std::vector<position_t> peer_helicopters;
      std::vector<position_t> peer_boats;
      std::vector<position_t> peer_watercraft;
      const position_t bike_pos = session ().bike ().position ();
      const plan_displacement_t from_start =
        terrain::torus_offset (plan (bike_pos), plan (m_initial_spawn), period);
      if (dot (from_start, from_start) > (30.0f * u::m) * (30.0f * u::m))
        return;

      const auto consider_peer = [&] (const CompanionSnapshot& raw) {
        CompanionSnapshot peer = raw;
        rebase_snapshot (peer, bike_pos, period);
        const bool they_are_senior =
          peer.play_time > my_play + seniority_margin ||
          (abs (peer.play_time - my_play) <= seniority_margin &&
           peer.player < my_id);
        const plan_displacement_t peer_from_start = terrain::torus_offset (
          plan (peer.fleet.bike.position), plan (m_initial_spawn), period);
        const bool peer_at_trailhead = dot (peer_from_start, peer_from_start) <=
                                       (30.0f * u::m) * (30.0f * u::m);
        if (they_are_senior && peer_at_trailhead) {
          ++senior_count;
          trailhead_occupied = true;
        }
        peer_bikes.push_back (peer.fleet.bike.position);
        peer_helicopters.push_back (peer.fleet.helicopter.position);
        peer_boats.push_back (peer.fleet.boat.position);
        peer_watercraft.push_back (peer.fleet.boat.position);
        peer_watercraft.push_back (peer.fleet.jet_ski.position);
      };

      for (const RemotePeer& peer : m_company.peers)
        if (peer.snapshot)
          consider_peer (*peer.snapshot);
      for (const CompanionSnapshot& arrived : newly_met) {
        const bool already_counted =
          std::ranges::any_of (m_company.peers, [&] (const RemotePeer& p) {
            return p.player == arrived.player && p.snapshot.has_value ();
          });
        if (!already_counted)
          consider_peer (arrived);
      }

      if (senior_count == 0 || !trailhead_occupied)
        return;

      mov::Vehicle& bike = session ().bike ();
      Vec3 heading = direction_value (bike.forward ());
      heading[1] = 0.0f;
      heading =
        length2 (heading) > 1e-6f ? normalized (heading) : Vec3 (0, 0, 1);
      const Vec3 side (heading[2], 0, -heading[0]);
      const bool beside =
        length2 (position_value (session ().walker ().position ()) -
                 position_value (bike.position ())) < 6.0f * 6.0f;

      // Pick the first unoccupied lateral rank relative to m_initial_spawn
      // (-7m, +7m, -14m, +14m...) so arriving peers never step onto one
      // another even when they hear of senior peers across separate frames.
      Vec3 place = position_value (m_initial_spawn);
      for (int slot = senior_count; slot < senior_count + 16; ++slot) {
        const float dir = (slot % 2 == 1) ? -1.0f : 1.0f;
        const meters_t shift =
          dir * static_cast<float> ((slot + 1) / 2) * 7.0f * u::m;
        const Vec3 candidate =
          position_value (m_initial_spawn) + side * meters_value (shift);
        const bool clear =
          std::ranges::all_of (peer_bikes, [&] (const position_t& other) {
            const plan_displacement_t d = terrain::torus_offset (
              plan (position (candidate)), plan (other), period);
            return dot (d, d) > (5.0f * u::m) * (5.0f * u::m);
          });
        if (clear) {
          place = candidate;
          break;
        }
      }
      place[1] = terrain::surface_elevation_value (
                   world_ground ().elevation (plan (position (place)))) +
                 1.2f;
      bike.reset (place);
      bike.set_heading (heading);
      Vec3 aside = place - position_value (m_initial_spawn);
      aside[1] = 0.0f;
      m_parking.set_aside (moppe::displacement (aside));
      m_spawn_position = position (place);
      m_parking.park_jeep (parking_lot ());
      m_parking.park_tractor (parking_lot ());
      m_parking.park_mountain_bike (parking_lot ());
      m_parking.park_long_john (parking_lot ());
      m_parking.park_helicopter (parking_lot (), peer_helicopters);
      if (logic ().m_doing != Doing::driving (Machine::boat))
        m_parking.park_boat (parking_lot (), false, peer_boats);
      if (logic ().m_doing != Doing::driving (Machine::jet_ski))
        m_parking.park_jet_ski (parking_lot (), false, peer_watercraft);
      m_parking.park_caddy (parking_lot ());
      m_parking.park_snowmobile (parking_lot ());
      if (!beside)
        return;
      session ().walker ().spawn (moppe::position (place + side * 1.8f),
                                  unit_direction (heading));
      logic ().m_fp_eye = session ().walker ().eye_position ();
      m_opening.reaim (session (), surface ());
    }

    void MoppeGame::keep_company (seconds_t interval) {
      if (!m_company.link)
        return;
      const WorldSummary world = this_world_summary ();
      const std::vector<CompanionSnapshot> met =
        m_company.hear (m_mushrooms, world);
      if (!met.empty ())
        make_room (met);
      m_company.follow (
        session (), world, world_ground (), m_sky.day ().turning, interval);
    }

    void MoppeGame::connect_multiplayer (const std::string& target) {
      if (m_session && passenger_in (logic ().m_doing)) {
        session ().set_mode (Doing::on_foot ());
        logic ().m_ride_peer = 0;
      }
      m_company.seek (target);
      if (m_company.link && m_ready)
        m_company.link->set_world (this_world_summary ());
    }

    void MoppeGame::disconnect_multiplayer () {
      if (m_session && passenger_in (logic ().m_doing)) {
        session ().set_mode (Doing::on_foot ());
        logic ().m_ride_peer = 0;
      }
      m_company.disconnect ();
    }

    void MoppeGame::close_multiplayer_menu () {
      if (m_heads_up.multiplayer_editing) {
        m_heads_up.multiplayer_editing = false;
        platform::set_text_input (false);
      }
      m_heads_up.multiplayer_open = false;
    }
  }
}
