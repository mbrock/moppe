#include <moppe/game/game_session.hh>
#include <moppe/game/snow.hh>
#include <moppe/gfx/signal.hh>

#include <algorithm>
#include <cmath>

namespace moppe::game {
  namespace {
    // Presses one actor's tracks into `snow`, returning the distance stepped
    // (zero when not yet far enough to stamp).
    meters_t stamp_snow_step (SnowPack& snow,
                              SnowTrail& trail,
                              Doing mode,
                              const position_t& at,
                              direction_t heading,
                              bool contact,
                              proportion_t cover) {
      if (!contact || cover < 0.42f * proportion[one]) {
        trail.last.reset ();
        return meters_t {};
      }
      const auto driven = driven_machine (mode);
      const bool foot = mode == Doing::on_foot ();
      const bool pogo = mode == Doing::pogoing ();
      const auto distance =
        trail.last ? length (plan (at) - plan (*trail.last)) : meters_t {};
      if (!trail.last || trail.mode != mode || distance > 4.0f * u::m) {
        trail.mode = mode;
        trail.last = at;
        trail.heading = heading;
        return meters_t {};
      }
      if (distance < (foot ? 0.65f : pogo ? 0.35f : 0.06f) * u::m)
        return meters_t {};
      const auto offset =
        [] (position_t p, direction_t way, meters_t side, meters_t front) {
          const auto f = plan (direction_value (way));
          return plan (p) + plan_displacement (f * meters_value (front) +
                                               Vec2 (f.z (), -f.x ()) *
                                                 meters_value (side));
        };
      const auto stamp = [&] (meters_t side,
                              meters_t front,
                              meters_t width,
                              meters_t depth,
                              bool belt = false) {
        const auto start = offset (*trail.last, trail.heading, side, front);
        const auto end = offset (at, heading, side, front);
        snow.press (foot ? offset (at, heading, side, front - 0.15f * u::m)
                         : start,
                    end,
                    width,
                    depth * cover.numerical_value_in (one),
                    belt ? trail.distance : -1.0f * u::m);
      };
      const bool sled = driven == Machine::snowmobile;
      if (foot) {
        trail.left = !trail.left;
        stamp ((trail.left ? 0.12f : -0.12f) * u::m,
               0.12f * u::m,
               0.085f * u::m,
               0.075f * u::m);
      } else if (pogo) {
        stamp (meters_t {}, meters_t {}, 0.09f * u::m, 0.11f * u::m);
      } else if (sled) {
        for (const int sign : { -1, 1 })
          stamp (
            sign * 0.48f * u::m, 0.90f * u::m, 0.085f * u::m, 0.095f * u::m);
        stamp (meters_t {}, -1.03f * u::m, 0.25f * u::m, 0.165f * u::m, true);
      } else if (driven &&
                 (*driven == Machine::caddy || *driven == Machine::jeep ||
                  *driven == Machine::tractor)) {
        const meters_t track =
          *driven == Machine::tractor ? mov::Tractor::rear_track
          : *driven == Machine::jeep  ? mov::jeep_rig.rear_track
                                      : mov::caddy_rig.rear_track;
        for (const int sign : { -1, 1 })
          stamp (sign * track, -1.0f * u::m, 0.14f * u::m, 0.11f * u::m);
      } else if (driven) {
        stamp (meters_t {}, -0.65f * u::m, 0.065f * u::m, 0.09f * u::m);
      }
      trail.distance += distance;
      trail.heading = heading;
      trail.last = at;
      return distance;
    }
  }

  void press_snow_tracks (SnowPack& snow,
                          SnowTrail& trail,
                          Doing mode,
                          const position_t& at,
                          direction_t heading,
                          bool contact,
                          proportion_t cover) {
    (void)stamp_snow_step (snow, trail, mode, at, heading, contact, cover);
  }

  void disturb_snow (GameSession& session, proportion_t cover) {
    auto& state = session.logic ();
    const auto mode = state.m_doing;
    const auto driven = driven_machine (mode);
    const bool foot = mode == Doing::on_foot ();
    const bool pogo = mode == Doing::pogoing ();
    const bool contact =
      foot ? session.walker ().grounded ()
      : pogo
        ? session.pogo ().grounded ()
        : driven && machine_kind (*driven).medium == Medium::land &&
            visit (session.fleet (), *driven, [] (Machine, const auto& body) {
              if constexpr (requires { body.grounded (); })
                return body.grounded ();
              else
                return false;
            });
    const auto at = session.subject_position ();
    const auto heading = session.subject_heading ();
    SnowTrail trail {
      .last = state.m_snow_last,
      .heading = state.m_snow_heading,
      .mode = state.m_snow_mode,
      .distance = state.m_snow_distance,
      .left = state.m_snow_left,
    };
    const meters_t distance =
      stamp_snow_step (state.m_snow, trail, mode, at, heading, contact, cover);
    state.m_snow_last = trail.last;
    state.m_snow_heading = trail.heading;
    state.m_snow_mode = trail.mode;
    state.m_snow_distance = trail.distance;
    state.m_snow_left = trail.left;
    if (!(distance > meters_t {}))
      return;
    const bool sled = driven == Machine::snowmobile;

    const speed_t speed = foot ? session.walker ().state ().stride_speed
                               : speed_t (session.subject_speed_kmh ());
    if (speed > (foot ? 2.6f : 1.5f) * u::m / u::s) {
      const auto vigour =
        std::clamp (speed_value (speed) / 14.0f, 0.12f, 1.0f) *
        cover.numerical_value_in (one);
      const direction_t left =
        unit_direction (cross (Vec3 (0, 1, 0), direction_value (heading)));
      const displacement_t back =
        along (heading, (sled ? -1.10f : -0.55f) * u::m);
      const position_t source = at + quantity_cast<isq::position_vector> (back);
      const auto side =
        (int (meters_value (state.m_snow_distance) * 8.0f) & 1) ? -1.0f : 1.0f;
      Dust::Style mist;
      mist.powder = true;
      mist.size = (0.09f + (sled ? 0.30f : 0.22f) * vigour) * u::m;
      mist.lifetime = (0.45f + 0.50f * vigour) * u::s;
      mist.spread = (0.55f + 0.85f * vigour) * one;
      mist.downward_acceleration =
        3.2f * isq::acceleration[u::m / pow<2> (u::s)];
      const velocity_t plume =
        velocity ((direction_value (left) * side * (sled ? 4.2f : 1.7f) -
                   direction_value (heading) * (sled ? 2.6f : 1.0f)) *
                    vigour +
                  Vec3 (0, 0.65f + (sled ? 3.2f : 2.5f) * vigour, 0));
      // The turbulent air behind the tunnel fans fine powder outward, leaving
      // the twin ski ruts and chevron belt track sharp behind the sled.
      const velocity_t wake =
        sled ? session.fleet ().snowmobile.velocity () * 0.35f : velocity_t {};
      session.dust ().emit (source,
                            plume + wake,
                            sled ? 3 : 1,
                            DisplayColor (1.0f, 1.0f, 1.0f),
                            mist);
      if (sled) {
        mist.size = 0.048f * u::m;
        mist.lifetime = 0.75f * u::s;
        mist.flake = true;
        mist.spread = (0.85f + 1.35f * vigour) * one;
        mist.downward_acceleration =
          9.0f * isq::acceleration[u::m / pow<2> (u::s)];
        session.dust ().emit (
          source, plume * 1.55f, 6, DisplayColor (1.0f, 1.0f, 1.0f), mist);
      }
    }
  }

  SnowPack::Key SnowPack::tile_at (plan_position_t at) {
    const auto index = [] (meters_t x) {
      return int (std::floor ((x / (tile * spacing)).numerical_value_in (one)));
    };
    return { index (x_of (at)), index (z_of (at)) };
  }

  void SnowPack::press (plan_position_t from,
                        plan_position_t to,
                        meters_t radius,
                        meters_t sink,
                        meters_t belt_phase) {
    // Work in texels from here to the byte written into the height tile.
    const Vec2 a = plan_value (from) / meters_value (spacing);
    const Vec2 b = plan_value (to) / meters_value (spacing);
    const Vec2 delta = b - a;
    const auto len = std::sqrt (length2 (delta));
    const Vec2 axis = len > 0 ? delta / len : Vec2 (0, 1);
    const auto width = (radius / spacing).numerical_value_in (one);
    const auto reach = width + 1.6f;
    ++m_revision;
    for (int z = int (std::floor (std::min (a.z (), b.z ()) - reach));
         z < std::max (a.z (), b.z ()) + reach;
         ++z)
      for (int x = int (std::floor (std::min (a.x (), b.x ()) - reach));
           x < std::max (a.x (), b.x ()) + reach;
           ++x) {
        const Vec2 off = Vec2 (x + 0.5f, z + 0.5f) - a;
        const auto along = std::clamp (dot (off, axis), 0.0f, len);
        const auto d = std::sqrt (length2 (off - axis * along));
        if (d >= reach)
          continue;
        // A hollow with rolled lips. Track cleats bite slightly deeper than
        // the belt between them at a wavelength the texel grid resolves
        // cleanly without temporal aliasing.
        const auto rib =
          belt_phase < meters_t {}
            ? 1.0f
            : 0.82f +
                0.18f * std::cos (meters_value (belt_phase + along * spacing) *
                                    18.8f +
                                  d * 0.45f);
        const auto hollow =
          rib * (1.0f - smoothstep (0.50f * width, width + 0.55f, d));
        const auto lip =
          0.56f *
          (1.0f - smoothstep (0.0f, 1.35f, std::abs (d - width - 0.55f)));
        const int displacement = std::lround (
          (sink / depth).numerical_value_in (one) * 127 * (lip - hollow));
        if (!displacement)
          continue;
        const Key key { int (std::floor (double (x) / tile)),
                        int (std::floor (double (z) / tile)) };
        auto& page = m_tiles[key];
        page.touched = m_revision;
        auto& h = page.height[(z - key[1] * tile) * tile + x - key[0] * tile];
        const auto level =
          std::uint8_t (std::clamp (128 + displacement, 0, 255));
        if (displacement < 0)
          h = std::min (h, level);
        else if (h >= 120)
          h = std::max (h, level);
      }
    while (m_tiles.size () > 512)
      m_tiles.erase (std::ranges::min_element (
        m_tiles, {}, [] (const auto& page) { return page.second.touched; }));
  }

  bool SnowPack::read (Key origin, std::vector<std::uint8_t>& image) const {
    bool any = false;
    for (int z = 0; z < window; ++z)
      for (int x = 0; x < window; ++x) {
        const auto found = m_tiles.find ({ origin[0] + x, origin[1] + z });
        if (found == m_tiles.end ())
          continue;
        if (!any)
          image.assign (pixels * pixels, 128);
        any = true;
        for (int row = 0; row < tile; ++row)
          std::copy_n (found->second.height.begin () + row * tile,
                       tile,
                       image.begin () + (z * tile + row) * pixels + x * tile);
      }
    return any;
  }
}
