#include <moppe/mov/box3d_vec.hh>
#include <moppe/mov/glider.hh>

#include <algorithm>
#include <cmath>

namespace moppe::mov {
  namespace {
    // The speed at which a loading of one carries the pilot alone, and the
    // speeds the bar can ask the wing to settle at. A heavier load flies
    // the same loadings faster, by the root of the weight.
    constexpr airspeed_t reference_speed = 15.0f * airspeed[u::m / u::s];
    constexpr airspeed_t trim_speed = 16.0f * airspeed[u::m / u::s];
    constexpr airspeed_t minimum_speed = 10.5f * airspeed[u::m / u::s];
    constexpr airspeed_t maximum_speed = 28.0f * airspeed[u::m / u::s];
    constexpr airspeed_t speed_range = 8.0f * airspeed[u::m / u::s];
    constexpr airspeed_t stall_speed = 9.5f * airspeed[u::m / u::s];
    constexpr airspeed_t least_speed = 1.0f * airspeed[u::m / u::s];

    // One airspeed as a share of another.
    constexpr float share (airspeed_t speed, airspeed_t of) {
      return (speed / of).numerical_value_in (one);
    }

    constexpr float stall_loading = share (reference_speed, stall_speed) *
                                    share (reference_speed, stall_speed);
    constexpr float least_loading = 0.2f;
    // The drag polar, in the lift's own units: parasitic drag, drag induced
    // by lift, and the separated flow of a wing pushed toward its stall.
    // The first two give a best glide of sixteen to one at the reference
    // speed; the third is the cost of flying near the stall.
    constexpr float parasitic_drag = 1.0f / 32.0f;
    constexpr float induced_drag = 1.0f / 32.0f;
    constexpr float separation_onset = 1.8f;
    constexpr float separation_drag = 0.5f;
    // A flaring pilot swings upright and the wing mushes: an air brake.
    constexpr float flare_drag = 0.25f;
    // A flare rounds the path out to the shallowest of descents and holds
    // it there.
    constexpr float flare_climb = -0.02f;
    constexpr float flare_hold = 4.0f;
    // A Trail 90 hanging in the airstream is no fairing.
    constexpr float bike_drag = 0.02f;
    // Rider and wing are the unloaded reference mass. Carrying the 175 kg
    // Trail 90 and its rider raises total weight to roughly 2.5 times
    // that baseline.
    constexpr float loaded_weight_ratio = 2.5f;
    constexpr radians_t full_bank = 0.8378f * u::rad; // 48 degrees
    // How strongly the pilot holds the flight path against the phugoid,
    // the slow swapping of speed and height a fixed bar would fall into:
    // about half of critical damping.
    constexpr float path_hold = 1.4f;

    // A persistent, readable prevailing wind. The wing flies in it, it
    // slackens toward the ground, and its encounter with windward slopes
    // makes the ridge lift.
    const velocity_t wind_velocity = velocity (Vec3 (3.2f, 0, 1.4f));
    const Vec3 wind_direction = normalized (velocity_value (wind_velocity));
    constexpr meters_t wind_gradient_height = 25.0f * u::m;
    // Ridge lift at its strongest, into the steepest windward slope, and
    // the height over the ground it has faded out by.
    constexpr rate_of_climb_t ridge_lift_peak =
      8.0f * rate_of_climb_speed[u::m / u::s];
    constexpr meters_t ridge_band = 110.0f * u::m;
    constexpr float surface_wind_share = 0.3f;

    float drag_of (float loading, bool bike) {
      const float separated = std::max (0.0f, loading - separation_onset);
      return parasitic_drag + (bike ? bike_drag : 0.0f) +
             induced_drag * loading * loading +
             separation_drag * separated * separated;
    }

    // The loading that carries the weight in level flight at an airspeed
    // given for the pilot alone.
    float loading_at (airspeed_t unloaded_speed) {
      const float ratio =
        share (reference_speed, std::max (unloaded_speed, least_speed));
      return std::clamp (ratio * ratio, least_loading, stall_loading);
    }
  }

  Glider::Glider (const map::SurfaceGeometry& surface) : m_surface (surface) {}

  void Glider::launch (position_t position,
                       velocity_t inherited_velocity,
                       const Vec3& heading,
                       bool bike_attached) {
    m_position = position;
    m_velocity = inherited_velocity;
    m_bike_attached = bike_attached;
    const Vec3 inherited = velocity_value (inherited_velocity);
    const Vec3 horizontal (inherited[0], 0, inherited[2]);
    const float inherited_speed = length (horizontal);

    m_heading = heading;
    m_heading[1] = 0;
    if (inherited_speed > 2.0f)
      m_heading = horizontal / inherited_speed;
    else if (length2 (m_heading) > 0.0001f)
      normalize (m_heading);
    else
      m_heading = Vec3 (0, 0, 1);

    m_airspeed = length (inherited) * moppe::airspeed[u::m / u::s];
    m_vertical_speed = inherited[1] * rate_of_climb_speed[u::m / u::s];
    m_air_mass_lift = 0.0f * rate_of_climb_speed[u::m / u::s];
    m_loading = loading_at (trim_speed) * scale_factor[one];
    m_bank = 0.0f * u::rad;
    m_turn = {};
    m_speed_control = {};
    m_flare = false;
    m_landed = false;
  }

  // The weight the wing carries, against the pilot alone's.
  scale_factor_t Glider::load () const {
    return (m_bike_attached ? loaded_weight_ratio : 1.0f) * scale_factor[one];
  }

  rate_of_climb_t Glider::polar_sink (airspeed_t airspeed) {
    // Steady flight: lift carries the weight, and the glide slope is the
    // drag over the lift.
    const float v = airspeed.numerical_value_in (u::m / u::s);
    const float loading = loading_at (airspeed);
    const float slope = drag_of (loading, false) / loading;
    return -v * slope / std::sqrt (1.0f + slope * slope) *
           rate_of_climb_speed[u::m / u::s];
  }

  glide_ratio_t Glider::glide_ratio_at (airspeed_t airspeed) {
    return quantity_cast<glide_ratio> (airspeed / -polar_sink (airspeed));
  }

  meters_t Glider::height_above_ground () const {
    // Over a bridge, the deck: not the river under it.
    const Vec3& p = position_value (m_position);
    return (p[1] - ground_height (m_surface, p[0], p[2])) * u::m;
  }

  rate_of_climb_t Glider::ridge_lift (meters_t agl) const {
    const Vec3 n = normalized (
      spatial::sample<terrain::terrain_normal> (m_surface, m_position)
        .numerical_value_in (one));

    // -n.xz is the uphill gradient direction.  Wind into that gradient
    // rises; the useful band fades above the terrain instead of becoming an
    // infinite elevator over every windward mountain.
    const float upslope =
      std::max (0.0f, -(n[0] * wind_direction[0] + n[2] * wind_direction[2]));
    const float height_fade = std::clamp (
      1.0f -
        (std::max (0.0f * u::m, agl) / ridge_band).numerical_value_in (one),
      0.0f,
      1.0f);
    return ridge_lift_peak * upslope * height_fade;
  }

  // The air's own motion where the wing is, with the lift last read.
  velocity_t Glider::air_velocity () const {
    const float share = std::clamp (
      (height_above_ground () / wind_gradient_height).numerical_value_in (one),
      surface_wind_share,
      1.0f);
    velocity_t air = wind_velocity * share;
    velocity_value (air)[1] = m_air_mass_lift.numerical_value_in (u::m / u::s);
    return air;
  }

  Vec3 Glider::up () const {
    return Quaternion::rotate (Vec3 (0, 1, 0), m_heading, -m_bank);
  }

  Vec3 Glider::nose () const {
    const Vec3 through = velocity_value (m_velocity - air_velocity ());
    Vec3 path = m_heading;
    if (length2 (through) > 4.0f) {
      path = normalized (through);
      // A wing falling out of the sky still hangs nose-forward.
      path[1] = std::clamp<float> (path[1], -0.75f, 0.75f);
      const float flat =
        std::sqrt (std::max (0.0f, 1.0f - float (path[1] * path[1])));
      path = m_heading * flat + Vec3 (0, path[1], 0);
    }
    // The bar pushed out raises the nose to the air.
    const float attack = 0.09f * (m_loading.numerical_value_in (one) - 1.0f);
    return normalized (path + Vec3 (0, 1, 0) * attack);
  }

  acceleration_t
  Glider::fly (seconds_t dt, position_t position, velocity_t velocity) {
    m_position = position;
    m_velocity = velocity;

    const float turn = scalar_value (m_turn);
    const radians_t bank_target = turn * full_bank;
    m_bank += (bank_target - m_bank) * smoothing_alpha (3.2f / u::s, dt);
    const float bank = radians_value (m_bank);

    m_air_mass_lift = ridge_lift (height_above_ground ());
    const Vec3 through = velocity_value (m_velocity - air_velocity ());
    const float speed = length (through);
    m_airspeed = speed * moppe::airspeed[u::m / u::s];
    const float against_reference =
      share (reference_speed, std::max (m_airspeed, least_speed));
    m_vertical_speed =
      float (velocity_value (m_velocity)[1]) * rate_of_climb_speed[u::m / u::s];

    const Vec3 flat (through[0], 0, through[2]);
    if (length2 (flat) > 4.0f)
      m_heading = normalized (flat);
    const Vec3 path = speed > 1.0f ? through / speed : m_heading;

    // The bar. The stick picks the airspeed the wing should settle at; a
    // banked wing is pushed out to carry the turn; and the pilot leans
    // against a path steeper or shallower than that speed's own glide, so
    // a dive is neither frozen nor left to porpoise. A flare rounds the
    // path out level and holds it there, pushing further out as the speed
    // bleeds away, until the wing has no more to give and settles.
    const float weight = load ().numerical_value_in (one);
    const float climb = std::asin (std::clamp<float> (path[1], -1.0f, 1.0f));
    float wanted;
    if (m_flare) {
      const float level = weight * against_reference * against_reference;
      wanted = level * std::clamp (
                         1.0f - flare_hold * (climb - flare_climb), 0.4f, 1.8f);
    } else {
      const airspeed_t asked =
        std::clamp (trim_speed + scalar_value (m_speed_control) * speed_range,
                    minimum_speed,
                    maximum_speed);
      const float settled = loading_at (asked);
      const float glide =
        -std::atan (drag_of (settled, m_bike_attached) / settled);
      const float hold =
        std::clamp (1.0f - path_hold * (climb - glide), 0.4f, 1.8f);
      wanted = settled * hold;
    }
    wanted = std::clamp (
      wanted / std::max (0.6f, std::cos (bank)), least_loading, stall_loading);
    const damping_t bar = m_flare ? 5.0f / u::s : 4.0f / u::s;
    m_loading +=
      (wanted * scale_factor[one] - m_loading) * smoothing_alpha (bar, dt);
    const float loading = m_loading.numerical_value_in (one);

    // Lift acts across the path through the air, in the plane the bank
    // tilts; drag acts back along it.
    const Vec3 world_up (0, 1, 0);
    Vec3 across = world_up - path * dot (world_up, path);
    if (length2 (across) < 1e-6f)
      across = world_up - m_heading * dot (world_up, m_heading);
    normalize (across);
    const Vec3 lift = Quaternion::rotate (across, path, -m_bank);
    const metres_per_second_squared_t pressure =
      standard_gravity * share (m_airspeed, reference_speed) *
      share (m_airspeed, reference_speed) / weight;
    const float drag =
      drag_of (loading, m_bike_attached) + (m_flare ? flare_drag : 0.0f);
    return quantity_cast<isq::acceleration> (lift * (pressure * loading) -
                                             path * (pressure * drag));
  }

  void Glider::follow (position_t position, velocity_t velocity) {
    m_position = position;
    m_velocity = velocity;
    m_vertical_speed =
      float (velocity_value (velocity)[1]) * rate_of_climb_speed[u::m / u::s];
  }

  bool Glider::update (seconds_t dt) {
    if (m_landed)
      return true;

    const acceleration_t weight =
      Vec3 (0, standard_gravity.numerical_value_in (u::m / pow<2> (u::s)), 0) *
      isq::acceleration[u::m / pow<2> (u::s)];
    const acceleration_t air = fly (dt, m_position, m_velocity);
    m_velocity += quantity_cast<isq::velocity> ((air - weight) * dt);
    m_position += quantity_cast<isq::position_vector> (m_velocity * dt);

    const meters_t clearance =
      pilot_clearance +
      (m_bike_attached ? bike_hang + 0.25f * u::m : 0.0f * u::m);
    const meters_t sunk = clearance - height_above_ground ();
    if (sunk >= 0.0f * u::m) {
      position_value (m_position)[1] += meters_value (sunk);
      m_landed = true;
    }
    return m_landed;
  }

  Glider::State Glider::state () const {
    return { m_position,      m_velocity, unit_direction (m_heading),
             m_bank,          m_airspeed, m_vertical_speed,
             m_air_mass_lift, m_loading,  m_turn,
             m_speed_control, m_flare,    m_bike_attached,
             m_landed };
  }

  void Glider::restore (const State& state) {
    m_position = state.position;
    m_velocity = state.velocity;
    m_heading = direction_value (state.heading);
    m_bank = state.bank;
    m_airspeed = state.airspeed;
    m_vertical_speed = state.vertical_speed;
    m_air_mass_lift = state.air_mass_lift;
    m_loading = state.loading;
    m_turn = state.turn;
    m_speed_control = state.speed_control;
    m_flare = state.flare;
    m_bike_attached = state.bike_attached;
    m_landed = state.landed;
  }
}
