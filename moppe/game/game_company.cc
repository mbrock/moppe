// The other player (companionship.hh) as this game meets them: each
// frame's exchange, and making room beside them when first met.

#include <moppe/game/game.hh>

namespace moppe {
  namespace game {
    std::uint32_t MoppeGame::this_world () const {
      return world_tag (recipe_cache_identity (recipe ()));
    }

    void MoppeGame::make_room (const CompanionSnapshot& theirs) {
      if (logic ().m_mode != M_FOOT)
        return;
      const double here = m_company.link->now ();
      if (here > theirs.sent ||
          (here == theirs.sent && m_company.link->player () < theirs.player))
        return;
      mov::Vehicle& bike = session ().bike ();
      Vec3 apart = bike.position () - theirs.bike.position;
      apart[1] = 0.0f;
      if (length2 (apart) > 2.5f * 2.5f)
        return;
      Vec3 heading = bike.orientation ();
      heading[1] = 0.0f;
      heading =
        length2 (heading) > 1e-6f ? normalized (heading) : Vec3 (0, 0, 1);
      const Vec3 side (heading[2], 0, -heading[0]);
      const bool beside = length2 (session ().walker ().position () -
                                   bike.position ()) < 6.0f * 6.0f;
      // Far enough that this jeep, parked beyond the bike, clears
      // theirs.
      Vec3 place = bike.position () - side * 7.0f;
      place[1] = world_ground ().height (place) + 1.2f;
      bike.reset (place);
      bike.set_heading (heading);
      Vec3 aside = place - m_spawn_position;
      aside[1] = 0.0f;
      m_parking.set_aside (aside);
      m_spawn_position = place;
      m_parking.park_jeep (parking_lot ());
      m_parking.park_tractor (parking_lot ());
      m_parking.park_mountain_bike (parking_lot ());
      m_parking.park_long_john (parking_lot ());
      m_parking.park_helicopter (parking_lot (), theirs.helicopter.position);
      if (logic ().m_mode != M_BOAT)
        m_parking.park_boat (parking_lot ());
      if (logic ().m_mode != M_JET_SKI)
        m_parking.park_jet_ski (parking_lot ());
      m_parking.park_caddy (parking_lot ());
      if (!beside)
        return;
      session ().walker ().spawn (moppe::position (place + side * 1.8f),
                                  heading);
      logic ().m_fp_eye = session ().walker ().eye_position ();
      m_opening.reaim (session (), surface ());
    }

    void MoppeGame::keep_company (seconds_t interval) {
      if (!m_company.link)
        return;
      if (const std::optional<CompanionSnapshot> met =
            m_company.hear (m_mushrooms, this_world ()))
        make_room (*met);
      m_company.follow (
        session (), this_world (), m_sky.day ().turning, interval);
    }
  }
}
