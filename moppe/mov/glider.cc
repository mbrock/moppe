#include <moppe/mov/glider.hh>

#include <algorithm>
#include <cmath>

namespace moppe::mov {
  namespace {
    constexpr float gravity = 9.82f;
    constexpr airspeed_t trim_speed = 16.0f * airspeed[u::m / u::s];
    constexpr airspeed_t minimum_speed = 10.0f * airspeed[u::m / u::s];
    constexpr airspeed_t maximum_speed = 28.0f * airspeed[u::m / u::s];

    // A persistent, readable prevailing wind.  Its horizontal velocity is
    // included in ground speed and its encounter with windward slopes makes
    // the first ridge-lift model.
    const Vec3 wind_velocity (3.2f, 0, 1.4f);
    const Vec3 wind_direction = normalized (wind_velocity);
  }

  Glider::Glider (const map::Surface& surface) : m_surface (surface) {}

  void Glider::launch (position_t position,
                       velocity_t inherited_velocity,
                       const Vec3& heading) {
    m_position = position;
    const Vec3 inherited = velocity_value (inherited_velocity);
    Vec3 horizontal (inherited[0], 0, inherited[2]);
    const float inherited_speed = length (horizontal);

    m_heading = heading;
    m_heading[1] = 0;
    if (inherited_speed > 2.0f)
      m_heading = horizontal / inherited_speed;
    else if (length2 (m_heading) > 0.0001f)
      normalize (m_heading);
    else
      m_heading = Vec3 (0, 0, 1);

    m_airspeed = std::clamp (inherited_speed * moppe::airspeed[u::m / u::s],
                             minimum_speed,
                             maximum_speed);
    m_vertical_speed = inherited[1] * rate_of_climb_speed[u::m / u::s];
    m_air_mass_lift = 0.0f * rate_of_climb_speed[u::m / u::s];
    m_velocity = inherited_velocity;
    m_bank = 0.0f * u::rad;
    m_turn = 0.0f;
    m_speed_control = 0.0f;
    m_flare = false;
    m_landed = false;
  }

  rate_of_climb_t Glider::polar_sink (airspeed_t airspeed) {
    // A one-line hang-glider polar: best sink near trim, with a steep
    // low-speed penalty that makes an indefinitely held flare become a stall.
    const float v = airspeed.numerical_value_in (u::m / u::s);
    const float fast = v - 15.0f;
    const float slow = std::max (0.0f, 13.0f - v);
    const float sink = 0.75f + 0.012f * fast * fast + 0.11f * slow * slow;
    return -sink * rate_of_climb_speed[u::m / u::s];
  }

  glide_ratio_t Glider::glide_ratio_at (airspeed_t airspeed) {
    return quantity_cast<glide_ratio> (airspeed / -polar_sink (airspeed));
  }

  rate_of_climb_t Glider::ridge_lift () const {
    const Vec3& p = position_value (m_position);
    const float ground = m_surface.elevation_at (m_position)
                           .quantity_from_zero ()
                           .numerical_value_in (u::m);
    const float agl = std::max (0.0f, p[1] - ground);
    const Vec3 n =
      normalized (m_surface.normal_at (m_position).numerical_value_in (one));

    // -n.xz is the uphill gradient direction.  Wind into that gradient
    // rises; the useful band fades above the terrain instead of becoming an
    // infinite elevator over every windward mountain.
    const float upslope =
      std::max (0.0f, -(n[0] * wind_direction[0] + n[2] * wind_direction[2]));
    const float height_fade = std::clamp (1.0f - agl / 110.0f, 0.0f, 1.0f);
    const float lift = 8.0f * upslope * height_fade;
    return lift * rate_of_climb_speed[u::m / u::s];
  }

  bool Glider::update (seconds_t dt) {
    if (m_landed)
      return true;

    const float dt_s = seconds_value (dt);
    const float turn = scalar_value (m_turn);
    const float speed_input = scalar_value (m_speed_control);
    const radians_t bank_target = turn * 48.0f * u::deg;
    m_bank += (bank_target - m_bank) * smoothing_alpha (3.2f / u::s, dt);

    airspeed_t target_speed =
      trim_speed + speed_input * (8.0f * moppe::airspeed[u::m / u::s]);
    if (m_flare)
      target_speed = minimum_speed;
    target_speed = std::clamp (target_speed, minimum_speed, maximum_speed);
    const damping_t speed_response = m_flare ? 2.8f / u::s : 1.3f / u::s;
    m_airspeed +=
      (target_speed - m_airspeed) * smoothing_alpha (speed_response, dt);

    const float speed = m_airspeed.numerical_value_in (u::m / u::s);
    const float bank = radians_value (m_bank);
    const float yaw_rate = gravity * std::tan (bank) / std::max (speed, 6.0f);
    m_heading =
      Quaternion::rotate (m_heading, Vec3 (0, 1, 0), yaw_rate * dt_s * u::rad);
    m_heading[1] = 0;
    normalize (m_heading);

    m_air_mass_lift = ridge_lift ();
    const float bank_load = 1.0f / std::max (0.45f, std::cos (bank));
    rate_of_climb_t target_vertical =
      m_air_mass_lift + polar_sink (m_airspeed) * bank_load * bank_load;

    // Pulling the bar through a flare converts some excess airspeed into a
    // brief climb.  Once the speed is gone the low-speed side of the polar
    // takes over, so holding Space cannot create lift.
    if (m_flare) {
      const float excess = std::max (0.0f, speed - 11.0f);
      target_vertical += (0.22f * excess) * rate_of_climb_speed[u::m / u::s];
    }
    m_vertical_speed +=
      (target_vertical - m_vertical_speed) * smoothing_alpha (1.8f / u::s, dt);

    Vec3 ground_velocity = m_heading * speed + wind_velocity;
    ground_velocity[1] = m_vertical_speed.numerical_value_in (u::m / u::s);
    m_velocity = moppe::velocity (ground_velocity);
    m_position += quantity_cast<isq::position_vector> (m_velocity * dt);
    bound ();

    Vec3& p = position_value (m_position);
    const float ground = m_surface.elevation_at (m_position)
                           .quantity_from_zero ()
                           .numerical_value_in (u::m);
    if (p[1] <= ground + 0.75f) {
      p[1] = ground + 0.75f;
      m_velocity = moppe::velocity (Vec3 ());
      m_vertical_speed = 0.0f * rate_of_climb_speed[u::m / u::s];
      m_air_mass_lift = 0.0f * rate_of_climb_speed[u::m / u::s];
      m_bank = 0.0f * u::rad;
      m_landed = true;
    }
    return m_landed;
  }

  void Glider::bound () {
    const map::SurfaceDomain& domain = m_surface.atlas ().domain ();
    if (domain.topology () == terrain::Topology::Torus)
      return;
    Vec3& p = position_value (m_position);
    const float margin = 2.0f;
    const float max_x =
      meters_value (domain.maximum_interpolated_x ()) - margin;
    const float max_z =
      meters_value (domain.maximum_interpolated_z ()) - margin;
    if (p[0] < margin) {
      p[0] = margin;
      m_heading[0] = std::abs (m_heading[0]);
    } else if (p[0] > max_x) {
      p[0] = max_x;
      m_heading[0] = -std::abs (m_heading[0]);
    }
    if (p[2] < margin) {
      p[2] = margin;
      m_heading[2] = std::abs (m_heading[2]);
    } else if (p[2] > max_z) {
      p[2] = max_z;
      m_heading[2] = -std::abs (m_heading[2]);
    }
    normalize (m_heading);
  }

  Glider::State Glider::state () const {
    return { m_position,      m_velocity,       m_heading,       m_bank,
             m_airspeed,      m_vertical_speed, m_air_mass_lift, m_turn,
             m_speed_control, m_flare,          m_landed };
  }

  void Glider::restore (const State& state) {
    m_position = state.position;
    m_velocity = state.velocity;
    m_heading = state.heading;
    m_bank = state.bank;
    m_airspeed = state.airspeed;
    m_vertical_speed = state.vertical_speed;
    m_air_mass_lift = state.air_mass_lift;
    m_turn = state.turn;
    m_speed_control = state.speed_control;
    m_flare = state.flare;
    m_landed = state.landed;
  }
}
