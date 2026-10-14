#include <moppe/game/companion.hh>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>

namespace moppe::game {
  namespace {
    // "MOPC", then the version of everything after it.
    constexpr std::uint32_t wire_magic = 0x43504f4dU;
    constexpr std::uint8_t wire_version = 4;

    constexpr std::uint8_t has_glider = 1 << 0;
    constexpr std::uint8_t has_walker = 1 << 1;
    constexpr std::uint8_t is_hung = 1 << 2;
    constexpr std::uint8_t has_hearth = 1 << 3;
    constexpr std::uint8_t glider_carries_bike = 1 << 4;
    constexpr std::uint8_t walker_grounded = 1 << 5;
    constexpr std::uint8_t has_pan = 1 << 6;

    // Farther than this between two snapshots was not travelled: a
    // restart, or a new world's spawn.
    constexpr float leap_metres = 40.0f;
    // Past the newest snapshot the last known motion is carried on,
    // dying away, over about this much time's worth of travel.
    constexpr double carry_seconds = 0.2;
    // The playback clock is drawn toward where it should be at this
    // rate a second, learns a sender whose clock runs fast or slow at
    // this one, and beyond this many seconds out simply jumps.
    constexpr double chase_gain = 2.0;
    constexpr double rate_gain = 0.5;
    constexpr double resync_seconds = 0.5;
    // How much of each arrival's reading of the two clocks' difference
    // is believed, and how fast a long gap between arrivals is
    // forgotten.
    constexpr double offset_ease = 0.1;
    constexpr double gap_ease = 0.01;
    constexpr double usual_gap = 0.05;
    constexpr double longest_gap = 0.3;
    // What late word shows was drawn wrong is put right over this long,
    // unless it is farther than a slide would cover.
    constexpr double slip_seconds = 0.15;
    constexpr float slip_metres = 3.0f;
    constexpr std::size_t track_length = 32;

    class Writer {
    public:
      explicit Writer (std::vector<std::uint8_t>& bytes) : m_bytes (bytes) {}

      template <typename T>
      void put (T value) {
        std::uint8_t raw[sizeof (T)];
        std::memcpy (raw, &value, sizeof (T));
        if constexpr (std::endian::native == std::endian::big)
          std::reverse (raw, raw + sizeof (T));
        m_bytes.insert (m_bytes.end (), raw, raw + sizeof (T));
      }

      void put (const Vec3& value) {
        for (std::size_t i = 0; i < 3; ++i)
          put (static_cast<float> (value[i]));
      }

    private:
      std::vector<std::uint8_t>& m_bytes;
    };

    // Reads what Writer wrote. Running off the end or meeting a number
    // that is not one spoils the whole reading.
    class Reader {
    public:
      explicit Reader (std::span<const std::uint8_t> bytes) : m_bytes (bytes) {}

      template <typename T>
      T get () {
        T value {};
        if (m_bytes.size () - m_at < sizeof (T)) {
          m_good = false;
          m_at = m_bytes.size ();
          return value;
        }
        std::uint8_t raw[sizeof (T)];
        std::memcpy (raw, m_bytes.data () + m_at, sizeof (T));
        m_at += sizeof (T);
        if constexpr (std::endian::native == std::endian::big)
          std::reverse (raw, raw + sizeof (T));
        std::memcpy (&value, raw, sizeof (T));
        return value;
      }

      float number () {
        const float value = get<float> ();
        if (!std::isfinite (value))
          m_good = false;
        return m_good ? value : 0.0f;
      }

      double long_number () {
        const double value = get<double> ();
        if (!std::isfinite (value))
          m_good = false;
        return m_good ? value : 0.0;
      }

      Vec3 point () {
        const float x = number ();
        const float y = number ();
        const float z = number ();
        return Vec3 (x, y, z);
      }

      Vec3 direction (const Vec3& otherwise) {
        const Vec3 value = point ();
        const float size = length (value);
        if (size < 1e-3f || size > 1e3f)
          return otherwise;
        return value * (1.0f / size);
      }

      bool good () const {
        return m_good;
      }
      bool finished () const {
        return m_good && m_at == m_bytes.size ();
      }

    private:
      std::span<const std::uint8_t> m_bytes;
      std::size_t m_at = 0;
      bool m_good = true;
    };

    float mix (float a, float b, float t) {
      return a + (b - a) * t;
    }

    Vec3 mix (const Vec3& a, const Vec3& b, float t) {
      return a + (b - a) * t;
    }

    Vec3 mix_direction (const Vec3& a, const Vec3& b, float t) {
      const Vec3 between = mix (a, b, std::clamp (t, 0.0f, 1.0f));
      return length2 (between) > 1e-6f ? normalized (between) : b;
    }

    // Between two fractions of a cycle, the short way round.
    float mix_phase (float a, float b, float t) {
      float step = b - a;
      step -= std::round (step);
      const float phase = a + step * t;
      return phase - std::floor (phase);
    }

    // Between two angles, the short way round.
    float mix_angle (float a, float b, float t) {
      return a + std::remainder (b - a, 6.2831853f) * t;
    }

    bool leapt (const Vec3& a, const Vec3& b) {
      return length2 (b - a) > leap_metres * leap_metres;
    }

    // The curve from `p1` (at zero) to `p2` (at one) that leaves with
    // velocity `m1` and arrives with `m2`.
    Vec3 curve (
      const Vec3& p1, const Vec3& p2, const Vec3& m1, const Vec3& m2, float s) {
      const float s2 = s * s;
      const float s3 = s2 * s;
      return p1 * (2 * s3 - 3 * s2 + 1) + m1 * (s3 - 2 * s2 + s) +
             p2 * (3 * s2 - 2 * s3) + m2 * (s3 - s2);
    }

    // Where something is partway from `a` to `b`: on the curve that
    // passes each of them the way its neighbours lie, so that it does
    // not turn a corner at every snapshot; and beyond them, straight on.
    // `place` finds the thing in a snapshot, or nothing where it has
    // none.
    template <typename Place>
    Vec3 travel (const CompanionSnapshot* before,
                 const CompanionSnapshot& a,
                 const CompanionSnapshot& b,
                 const CompanionSnapshot* after,
                 float t,
                 Place place) {
      const Vec3& p1 = *place (a);
      const Vec3& p2 = *place (b);
      const double span = b.sent - a.sent;
      if (t <= 0.0f || t >= 1.0f || !(span > 0.0))
        return mix (p1, p2, t);
      Vec3 m1 = p2 - p1;
      Vec3 m2 = m1;
      if (const Vec3* p0 = before ? place (*before) : nullptr;
          p0 && before->sent < a.sent && !leapt (*p0, p1))
        m1 = (p2 - *p0) * static_cast<float> (span / (b.sent - before->sent));
      if (const Vec3* p3 = after ? place (*after) : nullptr;
          p3 && after->sent > b.sent && !leapt (p2, *p3))
        m2 = (*p3 - p1) * static_cast<float> (span / (after->sent - a.sent));
      return curve (p1, p2, m1, m2, t);
    }
  }

  std::uint32_t companion_pick_window_start (std::size_t picked,
                                             std::uint32_t sequence) {
    if (picked <= companion_pick_window)
      return 0;
    const std::size_t latest = picked - companion_pick_window;
    if (sequence % 2 == 0)
      return static_cast<std::uint32_t> (latest);
    // The walking window overlaps its neighbours by half, and ends where
    // the latest begins.
    const std::size_t stride = companion_pick_window / 2;
    const std::size_t stops = latest / stride + 1;
    return static_cast<std::uint32_t> (
      std::min (latest, ((sequence / 2) % stops) * stride));
  }

  std::uint32_t world_tag (std::uint32_t seed, int resolution, int profile) {
    std::uint32_t tag = 2166136261U;
    for (const std::uint32_t part : { seed,
                                      static_cast<std::uint32_t> (resolution),
                                      static_cast<std::uint32_t> (profile) })
      for (int shift = 0; shift < 32; shift += 8) {
        tag ^= (part >> shift) & 0xffU;
        tag *= 16777619U;
      }
    return tag;
  }

  std::vector<std::uint8_t> encode (const CompanionSnapshot& snapshot) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve (512);
    Writer out (bytes);
    out.put (wire_magic);
    out.put (wire_version);
    out.put (snapshot.player);
    out.put (snapshot.sequence);
    out.put (snapshot.sent);
    out.put (snapshot.world);
    out.put (snapshot.day_hours);
    out.put (static_cast<std::uint8_t> (snapshot.mode));
    std::uint8_t flags = 0;
    if (snapshot.glider)
      flags |= has_glider;
    if (snapshot.glider && snapshot.glider->bike_attached)
      flags |= glider_carries_bike;
    if (snapshot.walker)
      flags |= has_walker;
    if (snapshot.walker && snapshot.walker->grounded)
      flags |= walker_grounded;
    if (snapshot.hung)
      flags |= is_hung;
    if (snapshot.hearth_laid)
      flags |= has_hearth;
    if (snapshot.pan_on)
      flags |= has_pan;
    out.put (flags);

    const VehiclePose& bike = snapshot.bike;
    out.put (bike.position);
    out.put (bike.render_orientation);
    out.put (bike.render_normal);
    out.put (bike.rear_wheel_drop);
    out.put (bike.front_wheel_drop);
    out.put (bike.lean_radians);
    out.put (bike.wheel_spin_radians);
    out.put (bike.fork_radians);
    out.put (bike.thrust);
    out.put (bike.boost_level);
    out.put (bike.boost_drive);

    const JeepPose& jeep = snapshot.jeep;
    out.put (jeep.position);
    out.put (jeep.forward);
    out.put (jeep.up);
    for (const float drop : jeep.wheel_drop)
      out.put (drop);
    out.put (jeep.steer_radians);
    out.put (jeep.wheel_spin_radians);
    out.put (snapshot.jeep_velocity);

    const HelicopterPose& heli = snapshot.helicopter;
    out.put (heli.position);
    out.put (heli.forward);
    out.put (heli.up);
    out.put (heli.rotor_radians);
    out.put (heli.tail_radians);

    if (snapshot.glider) {
      out.put (snapshot.glider->position);
      out.put (snapshot.glider->heading);
      out.put (snapshot.glider->bank_radians);
    }
    if (snapshot.walker) {
      const WalkerPose& walker = *snapshot.walker;
      out.put (walker.position);
      out.put (walker.heading);
      out.put (walker.velocity);
      out.put (walker.ground_normal);
      out.put (walker.stride_phase);
      out.put (walker.stride_speed);
      out.put (walker.airborne_seconds);
      out.put (walker.vertical_speed);
      out.put (walker.landing_dip);
      out.put (walker.look_pitch);
    }
    if (snapshot.hung) {
      for (int end = 0; end < 2; ++end) {
        out.put (snapshot.hammock.strap[end]);
        out.put (snapshot.hammock.girth[end]);
      }
      out.put (snapshot.hammock_load);
      out.put (snapshot.swing);
      out.put (snapshot.gaze_yaw);
      out.put (snapshot.gaze_pitch);
    }
    if (snapshot.hearth_laid) {
      out.put (snapshot.hearth);
      out.put (snapshot.fire_burn);
    }

    if (snapshot.pan_on) {
      out.put (snapshot.pan_place);
      out.put (snapshot.pan_yaw);
      out.put (snapshot.pan_cooked);
      out.put (snapshot.pan_count);
      for (const std::uint32_t mushroom : snapshot.pan_mushrooms)
        out.put (mushroom);
    }

    const std::size_t carried =
      std::min (snapshot.picks.size (), companion_pick_window);
    out.put (snapshot.pick_used);
    out.put (snapshot.pick_count);
    out.put (snapshot.picks_from);
    out.put (static_cast<std::uint8_t> (carried));
    for (std::size_t i = 0; i < carried; ++i)
      out.put (snapshot.picks[i]);
    return bytes;
  }

  std::optional<CompanionSnapshot>
  decode (std::span<const std::uint8_t> bytes) {
    Reader in (bytes);
    if (in.get<std::uint32_t> () != wire_magic ||
        in.get<std::uint8_t> () != wire_version)
      return std::nullopt;
    CompanionSnapshot snapshot;
    snapshot.player = in.get<std::uint64_t> ();
    snapshot.sequence = in.get<std::uint32_t> ();
    snapshot.sent = in.long_number ();
    snapshot.world = in.get<std::uint32_t> ();
    snapshot.day_hours = in.long_number ();
    const std::uint8_t mode = in.get<std::uint8_t> ();
    if (mode > M_HELICOPTER)
      return std::nullopt;
    snapshot.mode = static_cast<Mode> (mode);
    const std::uint8_t flags = in.get<std::uint8_t> ();

    VehiclePose& bike = snapshot.bike;
    bike.position = in.point ();
    bike.render_orientation = in.direction (Vec3 (0, 0, 1));
    bike.render_normal = in.direction (Vec3 (0, 1, 0));
    bike.rear_wheel_drop = in.number ();
    bike.front_wheel_drop = in.number ();
    bike.lean_radians = in.number ();
    bike.wheel_spin_radians = in.number ();
    bike.fork_radians = in.number ();
    bike.thrust = in.number ();
    bike.boost_level = in.number ();
    bike.boost_drive = in.number ();

    JeepPose& jeep = snapshot.jeep;
    jeep.position = in.point ();
    jeep.forward = in.direction (Vec3 (0, 0, 1));
    jeep.up = in.direction (Vec3 (0, 1, 0));
    for (float& drop : jeep.wheel_drop)
      drop = std::clamp (in.number (), -0.5f, 0.5f);
    jeep.steer_radians = in.number ();
    jeep.wheel_spin_radians = in.number ();
    snapshot.jeep_velocity = in.point ();

    HelicopterPose& heli = snapshot.helicopter;
    heli.position = in.point ();
    heli.forward = in.direction (Vec3 (0, 0, 1));
    heli.up = in.direction (Vec3 (0, 1, 0));
    heli.rotor_radians = in.number ();
    heli.tail_radians = in.number ();

    if (flags & has_glider) {
      GliderPose glider;
      glider.position = in.point ();
      glider.heading = in.direction (Vec3 (0, 0, 1));
      glider.bank_radians = in.number ();
      glider.bike_attached = (flags & glider_carries_bike) != 0;
      snapshot.glider = glider;
    }
    if (flags & has_walker) {
      WalkerPose walker;
      walker.position = in.point ();
      walker.heading = in.direction (Vec3 (0, 0, 1));
      walker.velocity = in.point ();
      walker.ground_normal = in.direction (Vec3 (0, 1, 0));
      walker.stride_phase = in.number ();
      walker.stride_speed = in.number ();
      walker.airborne_seconds = in.number ();
      walker.vertical_speed = in.number ();
      walker.landing_dip = in.number ();
      walker.look_pitch = in.number ();
      walker.grounded = (flags & walker_grounded) != 0;
      snapshot.walker = walker;
    }
    if (flags & is_hung) {
      snapshot.hung = true;
      for (int end = 0; end < 2; ++end) {
        snapshot.hammock.strap[end] = in.point ();
        snapshot.hammock.girth[end] = in.number ();
      }
      snapshot.hammock_load = std::clamp (in.number (), 0.0f, 1.0f);
      snapshot.swing = in.number ();
      snapshot.gaze_yaw = in.number ();
      snapshot.gaze_pitch = in.number ();
      // A hammock is slung between two different trees.
      if (length2 (snapshot.hammock.strap[1] - snapshot.hammock.strap[0]) <
          0.25f)
        return std::nullopt;
    }
    if (flags & has_hearth) {
      snapshot.hearth_laid = true;
      snapshot.hearth = in.point ();
      snapshot.fire_burn = std::clamp (in.number (), 0.0f, 1.0f);
    }

    if (flags & has_pan) {
      snapshot.pan_on = true;
      snapshot.pan_place = in.point ();
      snapshot.pan_yaw = in.number ();
      snapshot.pan_cooked = std::clamp (in.number (), 0.0f, 2.0f);
      snapshot.pan_count = in.get<std::uint8_t> ();
      if (snapshot.pan_count > Pan::room)
        return std::nullopt;
      for (std::uint32_t& mushroom : snapshot.pan_mushrooms)
        mushroom = in.get<std::uint32_t> ();
    }

    snapshot.pick_used = in.get<std::uint32_t> ();
    snapshot.pick_count = in.get<std::uint32_t> ();
    snapshot.picks_from = in.get<std::uint32_t> ();
    const std::size_t carried = in.get<std::uint8_t> ();
    if (carried > companion_pick_window)
      return std::nullopt;
    snapshot.picks.reserve (carried);
    for (std::size_t i = 0; i < carried; ++i)
      snapshot.picks.push_back (in.get<std::uint32_t> ());
    if (!in.finished ())
      return std::nullopt;
    return snapshot;
  }

  CompanionSnapshot
  blend (const CompanionSnapshot& a, const CompanionSnapshot& b, float t) {
    return blend (nullptr, a, b, nullptr, t);
  }

  CompanionSnapshot blend (const CompanionSnapshot* before,
                           const CompanionSnapshot& a,
                           const CompanionSnapshot& b,
                           const CompanionSnapshot* after,
                           float t) {
    CompanionSnapshot result = b;
    if (leapt (a.bike.position, b.bike.position))
      return result;
    // Only what travels or turns is carried on past `b`; the rest
    // holds as last told.
    const float within = std::clamp (t, 0.0f, 1.0f);

    VehiclePose& bike = result.bike;
    bike.position =
      travel (before, a, b, after, t, [] (const CompanionSnapshot& each) {
        return &each.bike.position;
      });
    bike.render_orientation =
      mix_direction (a.bike.render_orientation, b.bike.render_orientation, t);
    bike.render_normal =
      mix_direction (a.bike.render_normal, b.bike.render_normal, t);
    bike.rear_wheel_drop =
      mix (a.bike.rear_wheel_drop, b.bike.rear_wheel_drop, within);
    bike.front_wheel_drop =
      mix (a.bike.front_wheel_drop, b.bike.front_wheel_drop, within);
    bike.lean_radians = mix (a.bike.lean_radians, b.bike.lean_radians, within);
    bike.wheel_spin_radians =
      mix (a.bike.wheel_spin_radians, b.bike.wheel_spin_radians, t);
    bike.fork_radians = mix (a.bike.fork_radians, b.bike.fork_radians, within);

    if (!leapt (a.jeep.position, b.jeep.position)) {
      JeepPose& jeep = result.jeep;
      jeep.position =
        travel (before, a, b, after, t, [] (const CompanionSnapshot& each) {
          return &each.jeep.position;
        });
      jeep.forward = mix_direction (a.jeep.forward, b.jeep.forward, t);
      jeep.up = mix_direction (a.jeep.up, b.jeep.up, t);
      for (std::size_t i = 0; i < jeep.wheel_drop.size (); ++i)
        jeep.wheel_drop[i] =
          mix (a.jeep.wheel_drop[i], b.jeep.wheel_drop[i], within);
      jeep.steer_radians =
        mix (a.jeep.steer_radians, b.jeep.steer_radians, within);
      jeep.wheel_spin_radians =
        mix_angle (a.jeep.wheel_spin_radians, b.jeep.wheel_spin_radians, t);
      result.jeep_velocity = mix (a.jeep_velocity, b.jeep_velocity, within);
    }
    if (!leapt (a.helicopter.position, b.helicopter.position)) {
      HelicopterPose& heli = result.helicopter;
      heli.position =
        travel (before, a, b, after, t, [] (const CompanionSnapshot& each) {
          return &each.helicopter.position;
        });
      heli.forward =
        mix_direction (a.helicopter.forward, b.helicopter.forward, t);
      heli.up = mix_direction (a.helicopter.up, b.helicopter.up, t);
      heli.rotor_radians =
        mix_angle (a.helicopter.rotor_radians, b.helicopter.rotor_radians, t);
      heli.tail_radians =
        mix_angle (a.helicopter.tail_radians, b.helicopter.tail_radians, t);
    }
    if (a.glider && b.glider &&
        !leapt (a.glider->position, b.glider->position)) {
      GliderPose& glider = *result.glider;
      glider.position =
        travel (before, a, b, after, t, [] (const CompanionSnapshot& each) {
          return each.glider ? &each.glider->position : nullptr;
        });
      glider.heading = mix_direction (a.glider->heading, b.glider->heading, t);
      glider.bank_radians =
        mix (a.glider->bank_radians, b.glider->bank_radians, within);
    }
    if (a.walker && b.walker &&
        !leapt (a.walker->position, b.walker->position)) {
      WalkerPose& walker = *result.walker;
      walker.position =
        travel (before, a, b, after, t, [] (const CompanionSnapshot& each) {
          return each.walker ? &each.walker->position : nullptr;
        });
      walker.heading = mix_direction (a.walker->heading, b.walker->heading, t);
      walker.ground_normal =
        mix_direction (a.walker->ground_normal, b.walker->ground_normal, t);
      walker.stride_phase =
        mix_phase (a.walker->stride_phase, b.walker->stride_phase, t);
      walker.landing_dip =
        mix (a.walker->landing_dip, b.walker->landing_dip, within);
      walker.look_pitch =
        mix (a.walker->look_pitch, b.walker->look_pitch, within);
    }
    if (a.hung && b.hung) {
      result.hammock_load = mix (a.hammock_load, b.hammock_load, within);
      result.swing = mix (a.swing, b.swing, within);
      result.gaze_yaw = mix (a.gaze_yaw, b.gaze_yaw, within);
      result.gaze_pitch = mix (a.gaze_pitch, b.gaze_pitch, within);
    }
    if (a.hearth_laid && b.hearth_laid)
      result.fire_burn = mix (a.fire_burn, b.fire_burn, within);
    return result;
  }

  void CompanionTrack::push (CompanionSnapshot snapshot, double now) {
    if (!m_snapshots.empty () &&
        (snapshot.player != m_snapshots.back ().player || !present (now)))
      clear ();
    const double offset = snapshot.sent - now;
    if (m_snapshots.empty ())
      m_offset = offset;
    else if (snapshot.sent <= m_snapshots.back ().sent)
      return;
    else {
      // Each arrival is early or late by the network's whim and by
      // where in a frame it was read, so no one of them is believed
      // much -- unless the clocks have plainly parted.
      if (std::abs (offset - m_offset) > resync_seconds)
        m_offset = offset;
      else
        m_offset += (offset - m_offset) * offset_ease;
      // The longest wait of late is what the delay must cover.
      const double gap = std::min (now - m_heard, longest_gap);
      if (gap > m_gap)
        m_gap = gap;
      else if (gap > 1e-4)
        m_gap += (gap - m_gap) * gap_ease;
    }
    m_heard = now;
    m_snapshots.push_back (std::move (snapshot));
    ++m_fresh;
    while (m_snapshots.size () > track_length)
      m_snapshots.pop_front ();
  }

  bool CompanionTrack::present (double now) const {
    return !m_snapshots.empty () && now - m_heard < patience;
  }

  double CompanionTrack::delay () const {
    return std::clamp (1.5 * m_gap + 0.01, 0.05, 0.3);
  }

  CompanionSnapshot CompanionTrack::look (std::size_t count, double at) const {
    if (count == 1 || at <= m_snapshots.front ().sent)
      return m_snapshots.front ();
    for (std::size_t i = 1; i < count; ++i) {
      const CompanionSnapshot& a = m_snapshots[i - 1];
      const CompanionSnapshot& b = m_snapshots[i];
      if (at > b.sent)
        continue;
      return blend (i >= 2 ? &m_snapshots[i - 2] : nullptr,
                    a,
                    b,
                    i + 1 < count ? &m_snapshots[i + 1] : nullptr,
                    static_cast<float> ((at - a.sent) / (b.sent - a.sent)));
    }
    // Past the newest: on the way the last two were going, slowing to a
    // stand rather than stopping dead.
    const CompanionSnapshot& a = m_snapshots[count - 2];
    const CompanionSnapshot& b = m_snapshots[count - 1];
    const double reach =
      carry_seconds * (1.0 - std::exp (-(at - b.sent) / carry_seconds));
    return blend (a, b, static_cast<float> (1.0 + reach / (b.sent - a.sent)));
  }

  std::optional<CompanionSnapshot> CompanionTrack::advance (double now,
                                                            double elapsed) {
    if (!present (now)) {
      m_playing = false;
      return std::nullopt;
    }
    elapsed = std::clamp (elapsed, 0.0, 0.25);
    // The moment to draw moves on with this machine's frames, which are
    // even, and is only drawn toward what the arrivals say it should
    // be, which is not.
    const double target = now + m_offset - delay ();
    m_play += elapsed * m_rate;
    const double ahead = target - m_play;
    if (!m_playing || std::abs (ahead) > resync_seconds) {
      m_playing = true;
      m_play = target;
      m_rate = 1.0;
      m_slip = {};
    } else {
      m_play += std::clamp (ahead * (1.0 - std::exp (-chase_gain * elapsed)),
                            -0.5 * elapsed,
                            0.5 * elapsed);
      m_rate = std::clamp (m_rate + ahead * rate_gain * elapsed, 0.5, 2.0);
    }

    CompanionSnapshot seen = look (m_snapshots.size (), m_play);
    const auto places = [] (CompanionSnapshot& each) {
      return std::array<Vec3*, 4> {
        &each.bike.position,
        &each.jeep.position,
        each.walker ? &each.walker->position : nullptr,
        each.glider ? &each.glider->position : nullptr
      };
    };
    // Word that came late shows where they really were while this
    // machine was guessing. The difference is not jumped but kept as a
    // slip that dwindles away.
    if (m_fresh > 0 && m_fresh < m_snapshots.size ()) {
      const std::size_t had = m_snapshots.size () - m_fresh;
      if (m_play > m_snapshots[had - 1].sent) {
        CompanionSnapshot guessed = look (had, m_play);
        const std::array<Vec3*, 4> was = places (guessed);
        const std::array<Vec3*, 4> is = places (seen);
        for (std::size_t i = 0; i < m_slip.size (); ++i)
          if (was[i] && is[i])
            m_slip[i] += *was[i] - *is[i];
      }
    }
    m_fresh = 0;
    const float keep = static_cast<float> (std::exp (-elapsed / slip_seconds));
    const std::array<Vec3*, 4> shown = places (seen);
    for (std::size_t i = 0; i < m_slip.size (); ++i) {
      m_slip[i] = length2 (m_slip[i]) > slip_metres * slip_metres
                    ? Vec3 ()
                    : m_slip[i] * keep;
      if (shown[i])
        *shown[i] += m_slip[i];
    }
    return seen;
  }

  void CompanionTrack::clear () {
    m_snapshots.clear ();
    m_heard = -1e9;
    m_gap = usual_gap;
    m_fresh = 0;
    m_playing = false;
    m_rate = 1.0;
    m_slip = {};
  }
}
