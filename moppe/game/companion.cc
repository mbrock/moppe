#include <moppe/game/companion.hh>

#include <moppe/game/bus.hh>
#include <moppe/game/game_session.hh>
#include <moppe/mov/boat.hh>
#include <moppe/mov/car.hh>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <tuple>
#include <utility>

namespace moppe::game {
  namespace {
    // "MOPC", then the version of everything after it.
    constexpr std::uint32_t wire_magic = 0x43504f4dU;
    constexpr std::uint8_t wire_version = 18;

    constexpr std::uint8_t has_glider = 1 << 0;
    constexpr std::uint8_t has_walker = 1 << 1;
    constexpr std::uint8_t is_hung = 1 << 2;
    constexpr std::uint8_t has_hearth = 1 << 3;
    constexpr std::uint8_t glider_carries_bike = 1 << 4;
    constexpr std::uint8_t walker_grounded = 1 << 5;
    constexpr std::uint8_t has_pan = 1 << 6;

    // What the player is doing, one byte on the wire: the bike, on
    // foot, the glider, the jeep, riding along in the jeep, then the
    // helicopter, the tractor, the Caddy, the mountainbike, the boat,
    // the Long John, the jet ski, on the bus, and riding along in each
    // other machine.
    constexpr Doing doing_bytes[] = {
      Doing::driving (Machine::bike),
      Doing::on_foot (),
      Doing::gliding (),
      Doing::driving (Machine::jeep),
      Doing::riding_along (Machine::jeep),
      Doing::driving (Machine::helicopter),
      Doing::driving (Machine::tractor),
      Doing::driving (Machine::caddy),
      Doing::driving (Machine::mountain_bike),
      Doing::driving (Machine::boat),
      Doing::driving (Machine::long_john),
      Doing::driving (Machine::jet_ski),
      Doing::on_the_bus (),
      Doing::riding_along (Machine::bike),
      Doing::riding_along (Machine::tractor),
      Doing::riding_along (Machine::caddy),
      Doing::riding_along (Machine::helicopter),
      Doing::riding_along (Machine::mountain_bike),
      Doing::riding_along (Machine::boat),
      Doing::riding_along (Machine::long_john),
      Doing::riding_along (Machine::jet_ski),
    };

    std::uint8_t doing_byte (Doing doing) {
      for (std::size_t i = 0; i < std::size (doing_bytes); ++i)
        if (doing_bytes[i] == doing)
          return static_cast<std::uint8_t> (i);
      return 0;
    }

    std::optional<Doing> doing_of (std::uint8_t byte) {
      if (byte >= std::size (doing_bytes))
        return std::nullopt;
      return doing_bytes[byte];
    }

    // Farther than this between two snapshots was not travelled: a
    // restart, or a new world's spawn.
    constexpr meters_t leap_metres = 40.0f * si::metre;
    // Past the newest snapshot the last known motion is carried on,
    // dying away, over about this much time's worth of travel.
    constexpr duration_t carry_seconds = 0.2 * si::second;
    // The playback clock is drawn toward where it should be at this
    // rate a second, learns a sender whose clock runs fast or slow at
    // this one (its rate moving by this much a second for every second
    // it is out), and beyond this far out simply jumps.
    constexpr quantity<one / si::second, double> chase_gain = 2.0 / si::second;
    constexpr quantity<one / pow<2> (si::second), double> rate_gain =
      0.5 / pow<2> (si::second);
    constexpr duration_t resync_seconds = 0.5 * si::second;
    // How much of each arrival's reading of the two clocks' difference
    // is believed, and how fast a long gap between arrivals is
    // forgotten.
    using ease_t = quantity<blend_weight[one], double>;
    constexpr ease_t offset_ease = 0.1 * blend_weight[one];
    constexpr ease_t gap_ease = 0.01 * blend_weight[one];
    constexpr duration_t usual_gap = 0.05 * si::second;
    constexpr duration_t longest_gap = 0.3 * si::second;
    // What late word shows was drawn wrong is put right over this long,
    // unless it is farther than a slide would cover.
    constexpr duration_t slip_seconds = 0.15 * si::second;
    constexpr meters_t slip_metres = 3.0f * si::metre;
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

    // Partway from `a` to `b`: a number, a vector, or a quantity.
    template <class Value>
    Value mix (const Value& a, const Value& b, blend_weight_t t) {
      return a + (b - a) * t.numerical_value_in (one);
    }

    Vec3 mix_direction (const Vec3& a, const Vec3& b, blend_weight_t t) {
      const Vec3 between = mix (
        a,
        b,
        std::clamp (t, 0.0f * blend_weight[one], 1.0f * blend_weight[one]));
      return length2 (between) > 1e-6f ? normalized (between) : b;
    }

    // Between two fractions of a cycle, the short way round. (The poses
    // tell their phases and angles as plain numbers, frame_view.hh.)
    float mix_phase (float a, float b, blend_weight_t t) {
      float step = b - a;
      step -= std::round (step);
      const float phase = a + step * t.numerical_value_in (one);
      return phase - std::floor (phase);
    }

    // Between two angles in radians, the short way round.
    float mix_angle (float a, float b, blend_weight_t t) {
      return a +
             std::remainder (b - a, 6.2831853f) * t.numerical_value_in (one);
    }

    bool leapt (const Vec3& a, const Vec3& b) {
      const square_meters_t apart = length2 (b - a) * (si::metre * si::metre);
      return apart > leap_metres * leap_metres;
    }

    // The curve from `p1` (at zero) to `p2` (at one) that leaves with
    // velocity `m1` and arrives with `m2`.
    Vec3 curve (const Vec3& p1,
                const Vec3& p2,
                const Vec3& m1,
                const Vec3& m2,
                blend_weight_t along) {
      const float s = along.numerical_value_in (one);
      const float s2 = s * s;
      const float s3 = s2 * s;
      return p1 * (2 * s3 - 3 * s2 + 1) + m1 * (s3 - 2 * s2 + s) +
             p2 * (3 * s2 - 2 * s3) + m2 * (s3 - s2);
    }

    // Each machine's pose on the wire, in the fleet's order. A
    // pedalled bike sends its crank where the motocross sends its lean,
    // throttle, and jets.
    void put (Writer& out, Machine machine, const VehiclePose& bike) {
      out.put (bike.position);
      out.put (bike.render_orientation);
      out.put (bike.render_normal);
      out.put (bike.rear_wheel_drop);
      out.put (bike.front_wheel_drop);
      out.put (bike.wheel_spin_radians);
      out.put (bike.fork_radians);
      if (machine_kind (machine).pedalled) {
        out.put (bike.crank_radians);
        return;
      }
      out.put (bike.lean_radians);
      out.put (bike.thrust);
      out.put (bike.boost_level);
      out.put (bike.boost_drive);
      out.put (bike.rider_lean);
    }

    void put (Writer& out, Machine, const CarPose& car) {
      out.put (car.position);
      out.put (car.forward);
      out.put (car.up);
      for (const float drop : car.wheel_drop)
        out.put (drop);
      out.put (car.steer_radians);
      out.put (car.wheel_spin_radians);
      out.put (car.velocity);
    }

    void put (Writer& out, Machine, const HelicopterPose& heli) {
      out.put (heli.position);
      out.put (heli.forward);
      out.put (heli.up);
      out.put (heli.rotor_radians);
      out.put (heli.tail_radians);
    }

    void put (Writer& out, Machine, const TractorPose& tractor) {
      out.put (tractor.position);
      out.put (tractor.forward);
      out.put (tractor.up);
      for (const float drop : tractor.wheel_drop)
        out.put (drop);
      out.put (tractor.steer_radians);
      out.put (tractor.front_spin_radians);
      out.put (tractor.rear_spin_radians);
      out.put (tractor.trailer_position);
      out.put (tractor.trailer_forward);
      out.put (tractor.trailer_up);
      for (const float drop : tractor.trailer_wheel_drop)
        out.put (drop);
      out.put (tractor.trailer_spin_radians);
    }

    void put (Writer& out, Machine, const BoatPose& boat) {
      out.put (boat.position);
      out.put (boat.forward);
      out.put (boat.up);
      out.put (boat.swing_radians);
      out.put (boat.tilt_radians);
      out.put (boat.propeller_radians);
      out.put (boat.rpm);
      out.put (boat.velocity);
    }

    void put (Writer& out, Machine, const JetSkiPose& ski) {
      out.put (ski.position);
      out.put (ski.forward);
      out.put (ski.up);
      out.put (ski.steer_radians);
      out.put (ski.lean_radians);
      out.put (ski.rpm);
      out.put (ski.velocity);
    }

    void get (Reader& in, Machine machine, VehiclePose& bike) {
      bike.position = in.point ();
      bike.render_orientation = in.direction (Vec3 (0, 0, 1));
      bike.render_normal = in.direction (Vec3 (0, 1, 0));
      bike.rear_wheel_drop = in.number ();
      bike.front_wheel_drop = in.number ();
      bike.wheel_spin_radians = in.number ();
      bike.fork_radians = in.number ();
      if (machine_kind (machine).pedalled) {
        bike.rear_wheel_drop = std::clamp (bike.rear_wheel_drop, -0.2f, 0.2f);
        bike.front_wheel_drop = std::clamp (bike.front_wheel_drop, -0.2f, 0.2f);
        bike.crank_radians = in.number ();
        return;
      }
      bike.lean_radians = in.number ();
      bike.thrust = in.number ();
      bike.boost_level = in.number ();
      bike.boost_drive = in.number ();
      bike.rider_lean = in.number ();
    }

    void get (Reader& in, Machine, CarPose& car) {
      car.position = in.point ();
      car.forward = in.direction (Vec3 (0, 0, 1));
      car.up = in.direction (Vec3 (0, 1, 0));
      for (float& drop : car.wheel_drop)
        drop = std::clamp (in.number (), -0.5f, 0.5f);
      car.steer_radians = in.number ();
      car.wheel_spin_radians = in.number ();
      car.velocity = in.point ();
    }

    void get (Reader& in, Machine, HelicopterPose& heli) {
      heli.position = in.point ();
      heli.forward = in.direction (Vec3 (0, 0, 1));
      heli.up = in.direction (Vec3 (0, 1, 0));
      heli.rotor_radians = in.number ();
      heli.tail_radians = in.number ();
    }

    void get (Reader& in, Machine, TractorPose& tractor) {
      tractor.position = in.point ();
      tractor.forward = in.direction (Vec3 (0, 0, 1));
      tractor.up = in.direction (Vec3 (0, 1, 0));
      for (float& drop : tractor.wheel_drop)
        drop = std::clamp (in.number (), -0.5f, 0.5f);
      tractor.steer_radians = in.number ();
      tractor.front_spin_radians = in.number ();
      tractor.rear_spin_radians = in.number ();
      tractor.trailer_position = in.point ();
      tractor.trailer_forward = in.direction (Vec3 (0, 0, 1));
      tractor.trailer_up = in.direction (Vec3 (0, 1, 0));
      for (float& drop : tractor.trailer_wheel_drop)
        drop = std::clamp (in.number (), -0.5f, 0.5f);
      tractor.trailer_spin_radians = in.number ();
    }

    void get (Reader& in, Machine, BoatPose& boat) {
      boat.position = in.point ();
      boat.forward = in.direction (Vec3 (0, 0, 1));
      boat.up = in.direction (Vec3 (0, 1, 0));
      boat.swing_radians = in.number ();
      boat.tilt_radians = in.number ();
      boat.propeller_radians = in.number ();
      boat.rpm = in.number ();
      boat.velocity = in.point ();
    }

    void get (Reader& in, Machine, JetSkiPose& ski) {
      ski.position = in.point ();
      ski.forward = in.direction (Vec3 (0, 0, 1));
      ski.up = in.direction (Vec3 (0, 1, 0));
      ski.steer_radians = in.number ();
      ski.lean_radians = in.number ();
      ski.rpm = in.number ();
      ski.velocity = in.point ();
    }

    // When a blend falls, between snapshots `a` and `b` (at `t` from
    // zero to one), and the snapshots either side of them, if any.
    struct Moments {
      const CompanionSnapshot* before;
      const CompanionSnapshot& a;
      const CompanionSnapshot& b;
      const CompanionSnapshot* after;
      blend_weight_t t;
      // `t` held between the two.
      blend_weight_t within;
    };

    // Where something is partway from `p1` to `p2`: on the curve that
    // passes each of them the way its neighbours `p0` and `p3` lie (when
    // there are any), so that it does not turn a corner at every
    // snapshot; and beyond them, straight on.
    Vec3 travel (const Moments& w,
                 const Vec3* p0,
                 const Vec3& p1,
                 const Vec3& p2,
                 const Vec3* p3) {
      const blend_weight_t t = w.t;
      const duration_t span = w.b.sent - w.a.sent;
      if (t <= 0.0f * blend_weight[one] || t >= 1.0f * blend_weight[one] ||
          !(span > 0.0 * si::second))
        return mix (p1, p2, t);
      Vec3 m1 = p2 - p1;
      Vec3 m2 = m1;
      if (p0 && w.before->sent < w.a.sent && !leapt (*p0, p1))
        m1 = (p2 - *p0) *
             static_cast<float> (
               (span / (w.b.sent - w.before->sent)).numerical_value_in (one));
      if (p3 && w.after->sent > w.b.sent && !leapt (p2, *p3))
        m2 = (*p3 - p1) *
             static_cast<float> (
               (span / (w.after->sent - w.a.sent)).numerical_value_in (one));
      return curve (p1, p2, m1, m2, t);
    }

    // A pose blended from `p1` to `p2`, knowing `p0` and `p3` either
    // side when the moments have them. Only what travels or turns is
    // carried on past `p2`; the rest holds as last told.
    template <class Pose>
    struct Around {
      const Pose* p0;
      const Pose& p1;
      const Pose& p2;
      const Pose* p3;

      Vec3 travel (const Moments& w, Vec3 Pose::* place) const {
        return game::travel (w,
                             p0 ? &(p0->*place) : nullptr,
                             p1.*place,
                             p2.*place,
                             p3 ? &(p3->*place) : nullptr);
      }
    };

    void mix (Machine machine,
              VehiclePose& out,
              const Around<VehiclePose>& by,
              const Moments& w) {
      const VehiclePose& from = by.p1;
      const VehiclePose& to = by.p2;
      out.position = by.travel (w, &VehiclePose::position);
      out.render_orientation =
        mix_direction (from.render_orientation, to.render_orientation, w.t);
      out.render_normal =
        mix_direction (from.render_normal, to.render_normal, w.t);
      out.rear_wheel_drop =
        mix (from.rear_wheel_drop, to.rear_wheel_drop, w.within);
      out.front_wheel_drop =
        mix (from.front_wheel_drop, to.front_wheel_drop, w.within);
      out.fork_radians = mix (from.fork_radians, to.fork_radians, w.within);
      if (machine_kind (machine).pedalled) {
        out.wheel_spin_radians =
          mix_angle (from.wheel_spin_radians, to.wheel_spin_radians, w.t);
        out.crank_radians =
          mix_angle (from.crank_radians, to.crank_radians, w.t);
        return;
      }
      out.lean_radians = mix (from.lean_radians, to.lean_radians, w.within);
      out.wheel_spin_radians =
        mix (from.wheel_spin_radians, to.wheel_spin_radians, w.t);
      out.rider_lean = mix (from.rider_lean, to.rider_lean, w.within);
    }

    void
    mix (Machine, CarPose& out, const Around<CarPose>& by, const Moments& w) {
      const CarPose& from = by.p1;
      const CarPose& to = by.p2;
      out.position = by.travel (w, &CarPose::position);
      out.forward = mix_direction (from.forward, to.forward, w.t);
      out.up = mix_direction (from.up, to.up, w.t);
      for (std::size_t i = 0; i < out.wheel_drop.size (); ++i)
        out.wheel_drop[i] =
          mix (from.wheel_drop[i], to.wheel_drop[i], w.within);
      out.steer_radians = mix (from.steer_radians, to.steer_radians, w.within);
      out.wheel_spin_radians =
        mix_angle (from.wheel_spin_radians, to.wheel_spin_radians, w.t);
      out.velocity = mix (from.velocity, to.velocity, w.within);
    }

    void mix (Machine,
              HelicopterPose& out,
              const Around<HelicopterPose>& by,
              const Moments& w) {
      const HelicopterPose& from = by.p1;
      const HelicopterPose& to = by.p2;
      out.position = by.travel (w, &HelicopterPose::position);
      out.forward = mix_direction (from.forward, to.forward, w.t);
      out.up = mix_direction (from.up, to.up, w.t);
      out.rotor_radians = mix_angle (from.rotor_radians, to.rotor_radians, w.t);
      out.tail_radians = mix_angle (from.tail_radians, to.tail_radians, w.t);
    }

    void mix (Machine,
              TractorPose& out,
              const Around<TractorPose>& by,
              const Moments& w) {
      const TractorPose& from = by.p1;
      const TractorPose& to = by.p2;
      out.position = by.travel (w, &TractorPose::position);
      out.forward = mix_direction (from.forward, to.forward, w.t);
      out.up = mix_direction (from.up, to.up, w.t);
      for (std::size_t i = 0; i < out.wheel_drop.size (); ++i)
        out.wheel_drop[i] =
          mix (from.wheel_drop[i], to.wheel_drop[i], w.within);
      out.steer_radians = mix (from.steer_radians, to.steer_radians, w.within);
      out.front_spin_radians =
        mix_angle (from.front_spin_radians, to.front_spin_radians, w.t);
      out.rear_spin_radians =
        mix_angle (from.rear_spin_radians, to.rear_spin_radians, w.t);
      out.trailer_position = by.travel (w, &TractorPose::trailer_position);
      out.trailer_forward =
        mix_direction (from.trailer_forward, to.trailer_forward, w.t);
      out.trailer_up = mix_direction (from.trailer_up, to.trailer_up, w.t);
      for (std::size_t i = 0; i < out.trailer_wheel_drop.size (); ++i)
        out.trailer_wheel_drop[i] =
          mix (from.trailer_wheel_drop[i], to.trailer_wheel_drop[i], w.within);
      out.trailer_spin_radians =
        mix_angle (from.trailer_spin_radians, to.trailer_spin_radians, w.t);
    }

    void
    mix (Machine, BoatPose& out, const Around<BoatPose>& by, const Moments& w) {
      const BoatPose& from = by.p1;
      const BoatPose& to = by.p2;
      out.position = by.travel (w, &BoatPose::position);
      out.forward = mix_direction (from.forward, to.forward, w.t);
      out.up = mix_direction (from.up, to.up, w.t);
      out.swing_radians = mix (from.swing_radians, to.swing_radians, w.within);
      out.tilt_radians = mix (from.tilt_radians, to.tilt_radians, w.within);
      out.propeller_radians =
        mix_angle (from.propeller_radians, to.propeller_radians, w.t);
      out.rpm = mix (from.rpm, to.rpm, w.within);
      out.velocity = mix (from.velocity, to.velocity, w.within);
    }

    void mix (Machine,
              JetSkiPose& out,
              const Around<JetSkiPose>& by,
              const Moments& w) {
      const JetSkiPose& from = by.p1;
      const JetSkiPose& to = by.p2;
      out.position = by.travel (w, &JetSkiPose::position);
      out.forward = mix_direction (from.forward, to.forward, w.t);
      out.up = mix_direction (from.up, to.up, w.t);
      out.steer_radians = mix (from.steer_radians, to.steer_radians, w.within);
      out.lean_radians = mix (from.lean_radians, to.lean_radians, w.within);
      out.rpm = mix (from.rpm, to.rpm, w.within);
      out.velocity = mix (from.velocity, to.velocity, w.within);
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

  std::uint32_t world_tag (std::string_view identity) {
    std::uint32_t tag = 2166136261U;
    for (const char c : identity) {
      tag ^= static_cast<std::uint8_t> (c);
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
    out.put (seconds_on_link (snapshot.sent));
    out.put (snapshot.world);
    out.put (hours_since_midnight (snapshot.day_hours));
    out.put (seconds_into_play (snapshot.bus_clock));
    out.put (doing_byte (snapshot.mode));
    out.put (snapshot.bus);
    out.put (snapshot.bus_seat);
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

    for_each_machine (snapshot.fleet, [&] (Machine m, const auto& pose) {
      put (out, m, pose);
    });

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
      out.put (walker.look_aside);
      out.put (walker.turn_speed);
    }
    if (snapshot.hung) {
      for (int end = 0; end < 2; ++end) {
        out.put (position_value (snapshot.hammock.strap[end]));
        out.put (meters_value (snapshot.hammock.girth[end]));
      }
      out.put (snapshot.hammock_load.numerical_value_in (one));
      out.put (radians_value (snapshot.swing));
      out.put (radians_value (snapshot.gaze_yaw));
      out.put (snapshot.gaze_pitch.numerical_value_in (si::radian));
    }
    if (snapshot.hearth_laid) {
      out.put (position_value (snapshot.hearth));
      out.put (snapshot.fire_burn.numerical_value_in (one));
    }
    out.put (snapshot.beacons);

    if (snapshot.pan_on) {
      out.put (position_value (snapshot.pan_place));
      out.put (snapshot.pan_yaw.numerical_value_in (si::radian));
      out.put (snapshot.pan_cooked.numerical_value_in (one));
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
    snapshot.sent = link_time (in.long_number ());
    snapshot.world = in.get<std::uint32_t> ();
    snapshot.day_hours = clock_time (in.long_number ());
    snapshot.bus_clock = play_time (in.long_number ());
    const std::optional<Doing> mode = doing_of (in.get<std::uint8_t> ());
    if (!mode)
      return std::nullopt;
    snapshot.mode = *mode;
    snapshot.bus = in.get<std::uint8_t> ();
    snapshot.bus_seat = in.get<std::uint8_t> ();
    if (snapshot.bus_seat > 1)
      return std::nullopt;
    const std::uint8_t flags = in.get<std::uint8_t> ();

    for_each_machine (snapshot.fleet,
                      [&] (Machine m, auto& pose) { get (in, m, pose); });

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
      walker.look_aside = in.number ();
      walker.turn_speed = in.number ();
      walker.grounded = (flags & walker_grounded) != 0;
      snapshot.walker = walker;
    }
    if (flags & is_hung) {
      snapshot.hung = true;
      for (int end = 0; end < 2; ++end) {
        snapshot.hammock.strap[end] = position (in.point ());
        snapshot.hammock.girth[end] = in.number () * u::m;
      }
      snapshot.hammock_load =
        std::clamp (in.number (), 0.0f, 1.0f) * proportion[one];
      snapshot.swing = in.number () * si::radian;
      snapshot.gaze_yaw = in.number () * si::radian;
      snapshot.gaze_pitch = in.number () * inclination[si::radian];
      // A hammock is slung between two different trees.
      if (length2 (position_value (snapshot.hammock.strap[1]) -
                   position_value (snapshot.hammock.strap[0])) < 0.25f)
        return std::nullopt;
    }
    if (flags & has_hearth) {
      snapshot.hearth_laid = true;
      snapshot.hearth = position (in.point ());
      snapshot.fire_burn =
        std::clamp (in.number (), 0.0f, 1.0f) * proportion[one];
    }
    snapshot.beacons = in.get<std::uint32_t> ();

    if (flags & has_pan) {
      snapshot.pan_on = true;
      snapshot.pan_place = position (in.point ());
      snapshot.pan_yaw = in.number () * heading[si::radian];
      snapshot.pan_cooked =
        std::clamp (in.number (), 0.0f, 2.0f) * proportion[one];
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

  CompanionSnapshot blend (const CompanionSnapshot& a,
                           const CompanionSnapshot& b,
                           blend_weight_t t) {
    return blend (nullptr, a, b, nullptr, t);
  }

  CompanionSnapshot blend (const CompanionSnapshot* before,
                           const CompanionSnapshot& a,
                           const CompanionSnapshot& b,
                           const CompanionSnapshot* after,
                           blend_weight_t t) {
    CompanionSnapshot result = b;
    // The buses' clock runs on evenly, past `b` too.
    result.bus_clock =
      play_time (std::lerp (seconds_into_play (a.bus_clock),
                            seconds_into_play (b.bus_clock),
                            double (t.numerical_value_in (one))));
    if (leapt (a.fleet.bike.position, b.fleet.bike.position))
      return result;
    const Moments w {
      before, a,
      b,      after,
      t,      std::clamp (t, 0.0f * blend_weight[one], 1.0f * blend_weight[one])
    };
    const blend_weight_t within = w.within;
    // Each machine moves on unless it has leapt.
    for_each_machine_across (
      [&] (Machine m,
           auto& out,
           const auto& p0,
           const auto& p1,
           const auto& p2,
           const auto& p3) {
        if (leapt (p1.position, p2.position))
          return;
        mix (m,
             out,
             Around<std::remove_cvref_t<decltype (out)>> {
               before ? &p0 : nullptr, p1, p2, after ? &p3 : nullptr },
             w);
      },
      result.fleet,
      (before ? *before : a).fleet,
      a.fleet,
      b.fleet,
      (after ? *after : b).fleet);
    if (a.glider && b.glider &&
        !leapt (a.glider->position, b.glider->position)) {
      GliderPose& glider = *result.glider;
      glider.position =
        travel (w,
                before && before->glider ? &before->glider->position : nullptr,
                a.glider->position,
                b.glider->position,
                after && after->glider ? &after->glider->position : nullptr);
      glider.heading = mix_direction (a.glider->heading, b.glider->heading, t);
      glider.bank_radians =
        mix (a.glider->bank_radians, b.glider->bank_radians, within);
    }
    if (a.walker && b.walker &&
        !leapt (a.walker->position, b.walker->position)) {
      WalkerPose& walker = *result.walker;
      walker.position =
        travel (w,
                before && before->walker ? &before->walker->position : nullptr,
                a.walker->position,
                b.walker->position,
                after && after->walker ? &after->walker->position : nullptr);
      walker.heading = mix_direction (a.walker->heading, b.walker->heading, t);
      walker.ground_normal =
        mix_direction (a.walker->ground_normal, b.walker->ground_normal, t);
      walker.stride_phase =
        mix_phase (a.walker->stride_phase, b.walker->stride_phase, t);
      walker.landing_dip =
        mix (a.walker->landing_dip, b.walker->landing_dip, within);
      walker.look_pitch =
        mix (a.walker->look_pitch, b.walker->look_pitch, within);
      walker.look_aside =
        mix (a.walker->look_aside, b.walker->look_aside, within);
      walker.turn_speed =
        mix (a.walker->turn_speed, b.walker->turn_speed, within);
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

  void CompanionTrack::push (CompanionSnapshot snapshot, link_time_t now) {
    if (!m_snapshots.empty () &&
        (snapshot.player != m_snapshots.back ().player || !present (now)))
      clear ();
    const duration_t offset = snapshot.sent - now;
    if (m_snapshots.empty ())
      m_offset = offset;
    else if (snapshot.sent <= m_snapshots.back ().sent)
      return;
    else {
      // Each arrival is early or late by the network's whim and by
      // where in a frame it was read, so no one of them is believed
      // much -- unless the clocks have plainly parted.
      if (abs (offset - m_offset) > resync_seconds)
        m_offset = offset;
      else
        m_offset += (offset - m_offset) * offset_ease.numerical_value_in (one);
      // The longest wait of late is what the delay must cover.
      const duration_t gap = std::min (now - m_heard, longest_gap);
      if (gap > m_gap)
        m_gap = gap;
      else if (gap > 1e-4 * si::second)
        m_gap += (gap - m_gap) * gap_ease.numerical_value_in (one);
    }
    m_heard = now;
    m_snapshots.push_back (std::move (snapshot));
    ++m_fresh;
    while (m_snapshots.size () > track_length)
      m_snapshots.pop_front ();
  }

  bool CompanionTrack::present (link_time_t now) const {
    return !m_snapshots.empty () && now - m_heard < patience;
  }

  duration_t CompanionTrack::delay () const {
    return std::clamp (
      1.5 * m_gap + 0.01 * si::second, 0.05 * si::second, 0.3 * si::second);
  }

  CompanionSnapshot CompanionTrack::look (std::size_t count,
                                          link_time_t at) const {
    if (count == 1 || at <= m_snapshots.front ().sent)
      return m_snapshots.front ();
    for (std::size_t i = 1; i < count; ++i) {
      const CompanionSnapshot& a = m_snapshots[i - 1];
      const CompanionSnapshot& b = m_snapshots[i];
      if (at > b.sent)
        continue;
      return blend (
        i >= 2 ? &m_snapshots[i - 2] : nullptr,
        a,
        b,
        i + 1 < count ? &m_snapshots[i + 1] : nullptr,
        static_cast<float> (
          ((at - a.sent) / (b.sent - a.sent)).numerical_value_in (one)) *
          blend_weight[one]);
    }
    // Past the newest: on the way the last two were going, slowing to a
    // stand rather than stopping dead.
    const CompanionSnapshot& a = m_snapshots[count - 2];
    const CompanionSnapshot& b = m_snapshots[count - 1];
    const duration_t reach =
      carry_seconds *
      (1.0 -
       std::exp ((-(at - b.sent) / carry_seconds).numerical_value_in (one)));
    return blend (
      a,
      b,
      static_cast<float> (
        1.0 + (reach / (b.sent - a.sent)).numerical_value_in (one)) *
        blend_weight[one]);
  }

  std::optional<CompanionSnapshot>
  CompanionTrack::advance (link_time_t now, duration_t elapsed) {
    if (!present (now)) {
      m_playing = false;
      return std::nullopt;
    }
    elapsed = std::clamp (elapsed, 0.0 * si::second, 0.25 * si::second);
    // The moment to draw moves on with this machine's frames, which are
    // even, and is only drawn toward what the arrivals say it should
    // be, which is not.
    const link_time_t target = now + m_offset - delay ();
    m_play += elapsed * m_rate.numerical_value_in (one);
    const duration_t ahead = target - m_play;
    if (!m_playing || abs (ahead) > resync_seconds) {
      m_playing = true;
      m_play = target;
      m_rate = 1.0 * scale_factor[one];
      m_slip = {};
    } else {
      m_play += std::clamp (
        ahead *
          (1.0 - std::exp ((-chase_gain * elapsed).numerical_value_in (one))),
        -0.5 * elapsed,
        0.5 * elapsed);
      m_rate = std::clamp (m_rate + ahead * rate_gain * elapsed,
                           0.5 * scale_factor[one],
                           2.0 * scale_factor[one]);
    }

    CompanionSnapshot seen = look (m_snapshots.size (), m_play);
    const auto places = [] (CompanionSnapshot& each) {
      return std::array<Vec3*, 4> {
        &each.fleet.bike.position,
        &each.fleet.jeep.position,
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
            m_slip[i] += displacement (*was[i] - *is[i]);
      }
    }
    m_fresh = 0;
    // What of the slip is kept through this frame.
    const proportion_t keep =
      static_cast<float> (
        std::exp ((-elapsed / slip_seconds).numerical_value_in (one))) *
      proportion[one];
    const std::array<Vec3*, 4> shown = places (seen);
    for (std::size_t i = 0; i < m_slip.size (); ++i) {
      const square_meters_t slid =
        length2 (displacement_value (m_slip[i])) * (si::metre * si::metre);
      m_slip[i] = slid > slip_metres * slip_metres
                    ? displacement (Vec3 ())
                    : m_slip[i] * keep.numerical_value_in (one);
      if (shown[i])
        *shown[i] += displacement_value (m_slip[i]);
    }
    return seen;
  }

  void CompanionTrack::clear () {
    m_snapshots.clear ();
    m_heard = link_time (-1e9);
    m_gap = usual_gap;
    m_fresh = 0;
    m_playing = false;
    m_rate = 1.0 * scale_factor[one];
    m_slip = {};
  }

  namespace {
    // How a machine as told lies: its way forward and its up. The bikes
    // tell theirs as drawn.
    template <class Pose>
    std::pair<Vec3, Vec3> lie_of (const Pose& pose) {
      if constexpr (requires { pose.render_orientation; })
        return { pose.render_orientation, pose.render_normal };
      else
        return { pose.forward, pose.up };
    }
  }

  position_t passenger_seat (const CompanionSnapshot& theirs, Machine machine) {
    const Vec3 seat =
      machine_kind (machine).passenger_seat.value_or (Vec3 (0, 1, 0));
    return position (
      visit (theirs.fleet, machine, [&] (Machine, const auto& pose) {
        const auto [forward, up] = lie_of (pose);
        const Vec3 left = normalized (cross (up, forward));
        return pose.position + left * seat[0] + up * seat[1] +
               forward * seat[2];
      }));
  }

  void keep_seat (GameLogicState& state, const CompanionSnapshot& theirs) {
    const Machine machine = state.m_doing.machine;
    state.m_seat_position = passenger_seat (theirs, machine);
    visit (theirs.fleet, machine, [&] (Machine, const auto& pose) {
      const auto [forward, up] = lie_of (pose);
      state.m_seat_heading = unit_direction (forward);
      state.m_seat_up = unit_direction (up);
      if constexpr (requires { pose.velocity; })
        state.m_seat_velocity = velocity (pose.velocity);
      else
        state.m_seat_velocity = velocity (Vec3 ());
    });
  }

  std::optional<Machine>
  seat_within_reach (const GameSession& session,
                     const CompanionSnapshot* companion) {
    if (!companion || session.logic ().m_doing != Doing::on_foot ())
      return std::nullopt;
    const Vec3 feet = session.walker ().position ();
    // Anything of one's own nearer wins.
    constexpr auto square_metre = si::metre * si::metre;
    square_meters_t nearest = 1e30f * square_metre;
    for_each_machine (session.fleet (), [&] (Machine, const auto& body) {
      nearest =
        std::min (nearest, length2 (feet - body.position ()) * square_metre);
    });
    constexpr meters_t reach = 4.0f * si::metre;
    std::optional<Machine> found;
    for (int i = 0; i < machine_count; ++i) {
      const Machine m = static_cast<Machine> (i);
      const MachineKind& kind = machine_kind (m);
      if (!kind.passenger_seat || (kind.medium == Medium::water &&
                                   companion->mode != Doing::driving (m)))
        continue;
      const square_meters_t d =
        length2 (feet - position_value (passenger_seat (*companion, m))) *
        square_metre;
      if (d < reach * reach && d < nearest) {
        nearest = d;
        found = m;
      }
    }
    return found;
  }

  void take_seat (GameSession& session,
                  const CompanionSnapshot& theirs,
                  Machine machine) {
    session.clear_controls ();
    session.set_mode (Doing::riding_along (machine));
    keep_seat (session.logic (), theirs);
  }

  void keep_bus_time (GameLogicState& state,
                      const CompanionSnapshot& theirs,
                      duration_t behind,
                      std::uint64_t player) {
    const duration_t gap = theirs.bus_clock + behind - bus_time (state);
    if (gap > 1.0 * si::second)
      state.m_bus_lead += gap;
    else if (player < theirs.player)
      state.m_bus_lead += 0.05 * gap;
  }
}
