#include <moppe/game/places.hh>

#include <moppe/game/cairn.hh>
#include <moppe/game/camp.hh>
#include <moppe/game/farm_model.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/game/place_kind.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/gfx/signal.hh>
#include <moppe/terrain/torus.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace moppe::game {
  namespace {
    // The engine is a unity build: these helpers keep to a namespace of
    // their own.
    namespace places_parts {
      constexpr radians_t tau = 6.2831853f * u::rad;
      const direction_t up = unit_direction (Vec3 (0.0f, 1.0f, 0.0f));
      constexpr proportion_t whole = 1.0f * proportion[one];

      // A small deterministic stream, so each place is built the same way
      // on every launch of a world.
      class Random {
      public:
        explicit Random (std::uint32_t seed)
            : m_state (seed * 2654435761u + 0x6d2b79f5u) {
          if (m_state == 0)
            m_state = 1;
        }

        float unit () {
          m_state ^= m_state << 13;
          m_state ^= m_state >> 17;
          m_state ^= m_state << 5;
          return static_cast<float> (m_state & 0xffffffu) / 16777215.0f;
        }
        // Even odds between `low` and `high`, of whatever they measure.
        template <typename Value>
        Value between (Value low, Value high) {
          return low + (high - low) * unit ();
        }
        // A roll to compare with a chance.
        probability_t chance () {
          return unit () * probability[one];
        }

      private:
        std::uint32_t m_state;
      };

      direction_t level (Vec3 v) {
        v[1] = 0.0f;
        return unit_direction (length2 (v) > 1e-10f ? normalized (v)
                                                    : Vec3 (0, 0, 1));
      }
      direction_t level (const direction_t& way) {
        return level (direction_value (way));
      }
      direction_t level (const displacement_t& way) {
        return level (displacement_value (way));
      }
      direction_t level (const plan_displacement_t& way) {
        return level (lift (plan_value (way)));
      }

      // The level direction at `angle` round from +x toward +z.
      direction_t bearing (radians_t angle) {
        return unit_direction (Vec3 (std::cos (radians_value (angle)),
                                     0.0f,
                                     std::sin (radians_value (angle))));
      }

      // `at` moved `distance` along `way`.
      position_t
      ahead (const position_t& at, const direction_t& way, meters_t distance) {
        return at + quantity_cast<isq::position_vector> (along (way, distance));
      }

      // From `from` to `to`.
      displacement_t offset (const position_t& from, const position_t& to) {
        return displacement (position_value (to) - position_value (from));
      }

      // How far across the map, measured with std::hypot.
      meters_t flat_length (const plan_displacement_t& d) {
        return std::hypot (static_cast<float> (plan_value (d).x ()),
                           static_cast<float> (plan_value (d).z ())) *
               u::m;
      }

      meters_t flat_distance (const position_t& a, const position_t& b) {
        return flat_length (plan (a) - plan (b));
      }

      square_meters_t squared (const plan_displacement_t& d) {
        return x_of (d) * x_of (d) + z_of (d) * z_of (d);
      }

      direction_t left_of (const direction_t& forward) {
        const Vec3& f = direction_value (forward);
        return unit_direction (Vec3 (f[2], 0.0f, -f[0]));
      }

      terrain::SurfaceElevation elevation_of (const position_t& at) {
        return terrain::surface_elevation_point (position_value (at)[1] * u::m);
      }

      // `at`, standing at height zero, raised up to `height`.
      position_t lifted (const position_t& at,
                         terrain::SurfaceElevation height) {
        return ahead (at, up, terrain::surface_elevation_value (height) * u::m);
      }

      position_t on_ground (const PlaceGround& g, const plan_position_t& at) {
        return terrain::at_elevation (at, g.ground (at));
      }

      // How long ago a place was left, as the road network rolled it.
      age_t abandoned (const terrain::RoadPlace& place) {
        return place.abandoned;
      }

      float years_value (age_t age) {
        return age.numerical_value_in (astronomy::Julian_year);
      }

      // How deep the still water stands over the ground at a point.
      terrain::ElevationChange depth_at (const PlaceGround& g,
                                         const plan_position_t& at) {
        return g.water (at) - g.ground (at);
      }

      speed_t speed_at (const PlaceGround& g, const plan_position_t& at) {
        const terrain::WaterVelocity c = g.current (at);
        return std::hypot (
                 static_cast<float> (c.numerical_value_ref_in (u::m / u::s)[0]),
                 static_cast<float> (
                   c.numerical_value_ref_in (u::m / u::s)[2])) *
               (u::m / u::s);
      }

      // How far the ground rises and falls within `radius` of a point.
      meters_t
      relief (const PlaceGround& g, const position_t& at, meters_t radius) {
        terrain::SurfaceElevation low =
          terrain::surface_elevation_point (1e30f * u::m);
        terrain::SurfaceElevation high =
          terrain::surface_elevation_point (-1e30f * u::m);
        for (int ring = 1; ring <= 2; ++ring)
          for (int i = 0; i < 12; ++i) {
            const position_t p =
              ahead (at, bearing (tau * i / 12.0f), radius * ring / 2);
            const terrain::SurfaceElevation h = g.ground (plan (p));
            low = std::min (low, h);
            high = std::max (high, h);
          }
        return quantity_cast<isq::height> (high - low);
      }

      // Dry, well above any water, all the way round to `radius`.
      bool dry_round (const PlaceGround& g,
                      const position_t& at,
                      meters_t radius,
                      meters_t margin) {
        for (int ring = 0; ring <= 2; ++ring)
          for (int i = 0; i < (ring ? 12 : 1); ++i) {
            const position_t p =
              ahead (at, bearing (tau * i / 12.0f), radius * ring / 2);
            if (depth_at (g, plan (p)) > -margin)
              return false;
          }
        return true;
      }

      // How far a point lies from the nearest road's line, and that road.
      terrain::RoadSample road_near (const PlaceGround& g,
                                     const position_t& at) {
        if (!g.roads || g.roads->empty ())
          return {};
        return g.roads->nearest (plan (at));
      }

      // How far a road sample lies off its line, either side.
      meters_t off_road (const terrain::RoadSample& road) {
        return std::abs (meters_value (road.across)) * u::m;
      }

      // The road's line across the ground.
      direction_t road_direction (const terrain::RoadSample& road) {
        return unit_direction (lift (plan_value (road.direction), 0.0f));
      }

      // How high a kept sanctuary's fire platform stands.
      constexpr meters_t platform_height = 0.62f * u::m;

      // How much a place's stones have tumbled after so many years left.
      proportion_t ruin_after (age_t left) {
        const float years = years_value (left);
        return (years <= 0.0f
                  ? 0.0f
                  : 0.3f + 0.7f * smoothstep (100.0f, 1400.0f, years)) *
               proportion[one];
      }
    }
  }

  // -- planning ------------------------------------------------------

  namespace {
    namespace places_parts {
      // A jetty run out from the land at `from` along `out` over water
      // whose surface is `water`: its deck stands `freeboard` over it,
      // its root where the ground comes up to the deck, and its head
      // where the water under it is `wanted` deep, within `most` metres.
      std::optional<Jetty> lay_jetty (const PlaceGround& g,
                                      const position_t& from,
                                      const direction_t& out,
                                      terrain::SurfaceElevation water,
                                      meters_t freeboard,
                                      meters_t wanted,
                                      meters_t least,
                                      meters_t most) {
        Jetty jetty;
        jetty.out = out;
        jetty.deck = water + freeboard;
        // Back up onto the land until the ground stands at the deck, and
        // then out again to where it falls under it.
        position_t at = from;
        for (int i = 0; i < 80 && g.ground (plan (at)) < jetty.deck; ++i)
          at -= quantity_cast<isq::position_vector> (along (out, 0.5f * u::m));
        for (int i = 0;
             i < 200 && g.ground (plan (at)) > jetty.deck - 0.1f * u::m;
             ++i)
          at += quantity_cast<isq::position_vector> (along (out, 0.25f * u::m));
        // It must start on the land.
        if (g.ground (plan (at) - plan (along (out, 0.5f * u::m))) <
            jetty.deck - 0.3f * u::m)
          return std::nullopt;
        jetty.root = terrain::at_elevation (plan (at), jetty.deck);
        // Out to deep water, which must stay water all the way.
        // Over the strand to the water's edge, and then over water all the
        // way.
        meters_t length = 0.0f * u::m;
        bool wet = false;
        for (; length <= most; length += 0.5f * u::m) {
          const position_t p = ahead (at, out, length);
          const terrain::ElevationChange depth = water - g.ground (plan (p));
          wet = wet || depth > 0.0f * u::m;
          if ((wet && depth < -0.1f * u::m) || (!wet && length > 8.0f * u::m))
            return std::nullopt;
          if (length >= least && depth >= wanted)
            break;
        }
        if (length > most)
          return std::nullopt;
        // Its sides must be water too, for a boat to lie alongside.
        const direction_t side = left_of (out);
        for (const meters_t s : { -2.0f * u::m, 2.0f * u::m }) {
          const position_t p =
            ahead (ahead (at, out, length - 2.0f * u::m), side, s);
          if (water - g.ground (plan (p)) < 0.5f * wanted)
            return std::nullopt;
        }
        jetty.length = length;
        return jetty;
      }

      // The way out to the deepest water from a point on the shore: the
      // bearing along which it deepens soonest and stays water.
      std::optional<direction_t> seaward (const PlaceGround& g,
                                          const position_t& shore,
                                          terrain::SurfaceElevation water,
                                          bool want_sea) {
        terrain::ElevationChange best = -1e30f * u::m;
        std::optional<direction_t> found;
        for (int i = 0; i < 48; ++i) {
          const direction_t out = bearing (tau * i / 48.0f);
          terrain::ElevationChange score = 0.0f * u::m;
          bool through = true;
          for (const meters_t reach :
               { 8.0f * u::m, 16.0f * u::m, 26.0f * u::m, 40.0f * u::m }) {
            const position_t p = ahead (shore, out, reach);
            const terrain::ElevationChange depth = water - g.ground (plan (p));
            if (reach >= 16.0f * u::m && depth < 0.3f * u::m)
              through = false;
            if (want_sea && reach >= 16.0f * u::m && !g.sea (plan (p)))
              through = false;
            score += std::min<terrain::ElevationChange> (depth, 4.0f * u::m);
          }
          if (through && score > best) {
            best = score;
            found = out;
          }
        }
        return found;
      }

      std::optional<Landing> plan_landing (const PlaceGround& g,
                                           const terrain::RoadPlace& place,
                                           std::size_t index,
                                           terrain::SurfaceElevation sea) {
        // The shore within a stone's throw of where the road ends.
        const position_t centre = on_ground (g, place.at);
        std::optional<Landing> best;
        meters_t best_score = 1e30f * u::m;
        // Out to water deep enough for any boat, or on a shallow shore to
        // what the skiff needs.
        for (const auto [wanted, most] :
             { std::pair (2.3f * u::m, 34.0f * u::m),
               std::pair (1.2f * u::m, 45.0f * u::m) }) {
          if (best)
            break;
          for (meters_t r = 0.0f * u::m; r <= 60.0f * u::m; r += 4.0f * u::m)
            for (int k = 0; k < (r > 0.0f * u::m ? 12 : 1); ++k) {
              const position_t shore =
                ahead (centre, bearing (tau * k / 12.0f), r);
              // Dry, just above the water.
              const terrain::ElevationChange above =
                g.ground (plan (shore)) - sea;
              if (above < 0.0f * u::m || above > 2.5f * u::m)
                continue;
              const std::optional<direction_t> out =
                seaward (g, shore, sea, true);
              if (!out)
                continue;
              const std::optional<Jetty> jetty = lay_jetty (
                g, shore, *out, sea, 1.0f * u::m, wanted, 12.0f * u::m, most);
              if (!jetty)
                continue;
              const meters_t score = jetty->length + 0.4f * r;
              if (score >= best_score)
                continue;
              best_score = score;
              Landing landing;
              landing.place = index;
              landing.jetty = *jetty;
              best = landing;
            }
        }
        if (!best)
          return std::nullopt;
        Jetty& jetty = best->jetty;
        const age_t left = abandoned (place);
        jetty.decay =
          (years_value (left) > 0.0f
             ? 0.25f + 0.35f * smoothstep (15.0f, 150.0f, years_value (left))
             : 0.0f) *
          proportion[one];
        // The skiff lies alongside near the head, on the deeper side, its
        // bow to the land, tied to the post there.
        const direction_t side = left_of (jetty.out);
        const position_t head =
          ahead (jetty.root, jetty.out, jetty.length - 3.2f * u::m);
        const auto deep = [&] (const direction_t& way) {
          const position_t p = ahead (head, way, 2.0f * u::m);
          return sea - g.ground (plan (p));
        };
        const direction_t deeper = deep (side) >= deep (-side) ? side : -side;
        best->berth = terrain::at_elevation (
          plan (ahead (head, deeper, jetty.half_width + 0.85f * u::m)), sea);
        best->berth_forward = -jetty.out;
        best->bollard =
          ahead (ahead (jetty.root, jetty.out, jetty.length - 0.9f * u::m),
                 deeper,
                 jetty.half_width - 0.12f * u::m);
        return best;
      }

      // The enclosure stands on the top, or the shoulder the planner
      // chose; its gap opens on the footpath coming up.
      Sanctuary plan_sanctuary (const PlaceGround& g,
                                const terrain::RoadPlace& place,
                                std::size_t index,
                                Random& random) {
        Sanctuary s;
        s.place = index;
        s.gate = unit_direction (Vec3 (1, 0, 0));
        // The highest ground near the planned site.
        position_t top = on_ground (g, place.at);
        for (meters_t r = 1.0f * u::m; r <= 10.0f * u::m; r += 1.0f * u::m)
          for (int k = 0; k < 16; ++k) {
            const position_t p =
              ahead (on_ground (g, place.at), bearing (tau * k / 16), r);
            const position_t at = on_ground (g, plan (p));
            if (elevation_of (at) > elevation_of (top))
              top = at;
          }
        s.centre = top;
        s.wall_radius = random.between (6.6f * u::m, 8.2f * u::m);
        s.platform_radius = random.between (1.35f * u::m, 1.6f * u::m);
        // The footpath's last stretch: where a ring round the wall meets
        // the path, else the gentlest way down.
        meters_t nearest = 1e30f * u::m;
        terrain::ElevationChange gentlest = 1e30f * u::m;
        direction_t gentle = unit_direction (Vec3 (1, 0, 0));
        for (int k = 0; k < 48; ++k) {
          const direction_t way = bearing (tau * k / 48.0f);
          const position_t p = ahead (top, way, s.wall_radius + 5.0f * u::m);
          const terrain::ElevationChange fall =
            elevation_of (top) - g.ground (plan (p));
          if (fall < gentlest) {
            gentlest = fall;
            gentle = way;
          }
          const terrain::RoadSample road = road_near (g, p);
          if (road.found && road.kind == terrain::Road::Kind::Footpath &&
              off_road (road) < nearest) {
            nearest = off_road (road);
            s.gate = way;
          }
        }
        if (nearest > 6.0f * u::m)
          s.gate = gentle;
        // The platform's top stands over the highest ground under it.
        terrain::SurfaceElevation under = elevation_of (top);
        for (int k = 0; k < 12; ++k) {
          const position_t p =
            ahead (top, bearing (tau * k / 12.0f), s.platform_radius);
          under = std::max (under, g.ground (plan (p)));
        }
        const age_t left = abandoned (place);
        s.kept = years_value (left) <= 0.0f;
        s.ruin = ruin_after (left);
        s.hearth = terrain::at_elevation (
          plan (top), under + platform_height * (whole - 0.5f * s.ruin));
        return s;
      }

      // A building's site: level, dry, open of the road, within `low` to
      // `high` of `near`, the levellest such nearest the middle.
      std::optional<position_t>
      building_site (const PlaceGround& g,
                     const position_t& near,
                     meters_t low,
                     meters_t high,
                     meters_t room,
                     meters_t flat,
                     const std::vector<position_t>& taken) {
        std::optional<position_t> best;
        meters_t best_score = 1e30f * u::m;
        for (meters_t r = low; r <= high; r += 2.0f * u::m) {
          const int around = std::max (
            8,
            static_cast<int> (radians_value (tau) * meters_value (r) / 3.0f));
          for (int k = 0; k < around; ++k) {
            const position_t at =
              on_ground (g, plan (ahead (near, bearing (tau * k / around), r)));
            const meters_t rough = relief (g, at, room);
            if (rough > flat ||
                !dry_round (g, at, room + 6.0f * u::m, 1.0f * u::m))
              continue;
            const terrain::RoadSample road = road_near (g, at);
            if (road.found && off_road (road) < room + 4.0f * u::m)
              continue;
            bool clear = true;
            for (const position_t& other : taken)
              clear = clear && flat_distance (other, at) > room + 7.0f * u::m;
            if (!clear)
              continue;
            const meters_t score = rough * 30.0f + (r - low) * 0.5f;
            if (score < best_score) {
              best_score = score;
              best = at;
            }
          }
        }
        return best;
      }

      // Raises a house or a barn until its floor stands clear of the
      // highest ground under its footprint, its plinth or footing reaching
      // down to the rest.
      position_t raised (const PlaceGround& g,
                         position_t at,
                         const direction_t& forward,
                         Building::Kind kind) {
        const bool barn = kind == Building::Kind::barn;
        const meters_t hx = barn ? 6.1f * u::m : 3.7f * u::m,
                       hz = barn ? 4.1f * u::m : 2.7f * u::m;
        // How far under the floor the highest ground may come.
        const meters_t under = barn ? 0.27f * u::m : 0.5f * u::m;
        const direction_t f = level (forward), l = left_of (f);
        terrain::SurfaceElevation high = elevation_of (at);
        for (int i = -2; i <= 2; ++i)
          for (int j = -2; j <= 2; ++j) {
            const position_t p =
              ahead (ahead (at, l, hx * i / 2.0f), f, hz * j / 2.0f);
            high = std::max (high, g.ground (plan (p)));
          }
        position_value (at)[1] = terrain::surface_elevation_value (
          std::max (elevation_of (at), high - under));
        return at;
      }

      // The mill stands on the bank of the stream leaving the lake, its
      // wheel over the water a little below the outlet; the house and
      // the barn on level ground near by, facing the road.
      std::optional<Farm> plan_farm (const PlaceGround& g,
                                     const terrain::RoadPlace& place,
                                     std::size_t index,
                                     std::vector<Jetty>& stages) {
        const position_t outlet = on_ground (g, place.at);
        // The stream: running water near the outlet, and the way it
        // runs.
        terrain::WaterVelocity flow =
          Vec3 (0, 0, 0) * terrain::water_velocity[u::m / u::s];
        position_t stream = outlet;
        speed_t fastest = 0.0f * (u::m / u::s);
        for (meters_t r = 4.0f * u::m; r <= 50.0f * u::m; r += 2.0f * u::m)
          for (int k = 0; k < 24; ++k) {
            const position_t p = ahead (outlet, bearing (tau * k / 24.0f), r);
            if (depth_at (g, plan (p)) < 0.15f * u::m)
              continue;
            const speed_t speed = speed_at (g, plan (p));
            if (speed > 0.2f * (u::m / u::s))
              flow += g.current (plan (p)) *
                      (1.0f / (1.0f + 0.1f * meters_value (r)));
            if (speed > fastest && r > 8.0f * u::m) {
              fastest = speed;
              stream = p;
            }
          }
        Farm farm;
        farm.place = index;
        const age_t left = abandoned (place);
        farm.kept = years_value (left) <= 0.0f;
        farm.ruined = years_value (left) > 45.0f;
        std::vector<position_t> taken;
        if (fastest > 0.2f * (u::m / u::s)) {
          const direction_t down =
            level (flow.numerical_value_ref_in (u::m / u::s));
          // Down the stream a little way, out to its bank on whichever
          // side is drier and levellest.
          for (meters_t d = 10.0f * u::m; d <= 40.0f * u::m && taken.empty ();
               d += 3.0f * u::m) {
            const position_t water = ahead (outlet, down, d);
            if (depth_at (g, plan (water)) < 0.1f * u::m ||
                speed_at (g, plan (water)) < 0.15f * (u::m / u::s))
              continue;
            for (const direction_t& across :
                 { left_of (down), -left_of (down) }) {
              meters_t bank = 0.0f * u::m;
              while (bank < 12.0f * u::m &&
                     depth_at (g, plan (ahead (water, across, bank))) >
                       -0.25f * u::m)
                bank += 0.5f * u::m;
              if (bank >= 12.0f * u::m)
                continue;
              // The wheel stands in the water's edge; the house's middle
              // 3.2 m back from it.
              const position_t at =
                on_ground (g, plan (ahead (water, across, bank + 3.0f * u::m)));
              if (relief (g, at, 3.0f * u::m) > 1.6f * u::m)
                continue;
              const terrain::RoadSample road = road_near (g, at);
              if (road.found && off_road (road) < 5.0f * u::m)
                continue;
              // The wheel is on the model's -x: turned so that it faces
              // the stream.
              farm.buildings.push_back (
                { Building::Kind::mill,
                  at,
                  level (
                    cross (direction_value (up), -direction_value (across))) });
              taken.push_back (at);
              break;
            }
          }
        }
        // The house and the barn, facing the nearest road.
        const auto facing_road = [&] (const position_t& at) {
          const terrain::RoadSample road = road_near (g, at);
          if (!road.found)
            return level (offset (at, outlet));
          const direction_t dir = road_direction (road);
          // Toward the line: against the side it lies on.
          return level (road.across > 0.0f * u::m ? -left_of (dir)
                                                  : left_of (dir));
        };
        if (const std::optional<position_t> house = building_site (g,
                                                                   outlet,
                                                                   20.0f * u::m,
                                                                   70.0f * u::m,
                                                                   5.0f * u::m,
                                                                   0.9f * u::m,
                                                                   taken)) {
          const direction_t facing = facing_road (*house);
          farm.buildings.push_back (
            { Building::Kind::house,
              raised (g, *house, facing, Building::Kind::house),
              facing });
          taken.push_back (*house);
          if (const std::optional<position_t> barn =
                building_site (g,
                               *house,
                               16.0f * u::m,
                               32.0f * u::m,
                               7.0f * u::m,
                               0.9f * u::m,
                               taken)) {
            // The barn's doors to the house across the yard.
            const direction_t to_house = level (offset (*barn, *house));
            farm.buildings.push_back (
              { Building::Kind::barn,
                raised (g, *barn, to_house, Building::Kind::barn),
                to_house });
          }
        }
        if (farm.buildings.empty ())
          return std::nullopt;

        // A landing stage on the lake, clear of the outlet's pull, to fish
        // from.
        const terrain::SurfaceElevation lake = [&] {
          for (meters_t r = 6.0f * u::m; r <= 30.0f * u::m; r += 2.0f * u::m)
            for (int k = 0; k < 24; ++k) {
              const position_t p = ahead (outlet, bearing (tau * k / 24.0f), r);
              if (depth_at (g, plan (p)) > 0.6f * u::m &&
                  speed_at (g, plan (p)) < 0.05f * (u::m / u::s))
                return g.water (plan (p));
            }
          return g.water (plan (outlet));
        }();
        std::optional<Jetty> stage;
        meters_t best = 1e30f * u::m;
        for (meters_t r = 14.0f * u::m; r <= 70.0f * u::m && !stage;
             r += 4.0f * u::m)
          for (int k = 0; k < 24; ++k) {
            const position_t shore =
              ahead (outlet, bearing (tau * k / 24.0f), r);
            const terrain::ElevationChange d = depth_at (g, plan (shore));
            if (d > 0.1f * u::m || d < -0.8f * u::m)
              continue;
            const std::optional<direction_t> out =
              seaward (g, shore, lake, false);
            if (!out)
              continue;
            const position_t tip = ahead (shore, *out, 10.0f * u::m);
            if (speed_at (g, plan (tip)) > 0.06f * (u::m / u::s) ||
                abs (g.water (plan (tip)) - lake) > 0.15f * u::m)
              continue;
            std::optional<Jetty> laid = lay_jetty (g,
                                                   shore,
                                                   *out,
                                                   lake,
                                                   0.45f * u::m,
                                                   1.0f * u::m,
                                                   4.0f * u::m,
                                                   11.0f * u::m);
            if (!laid)
              continue;
            bool clear = true;
            for (const position_t& other : taken)
              clear = clear && flat_distance (other, laid->root) > 9.0f * u::m;
            if (clear && laid->length < best) {
              best = laid->length;
              laid->half_width = 0.75f * u::m;
              laid->decay = (farm.ruined ? 0.35f : 0.0f) * proportion[one];
              stage = laid;
            }
          }
        if (stage)
          stages.push_back (*stage);
        return farm;
      }

      // Farmsteads along the roads still in use: of stations every 80 m
      // along them, the levellest, driest, most open ground a little off
      // the road on either side, taken best first while it keeps well
      // apart from every other place -- and one, first of all, a short
      // walk from the trailhead.
      void plan_homesteads (PlacesPlan& plan,
                            const terrain::RoadNetwork& network,
                            const PlaceGround& g) {
        std::vector<position_t> others;
        std::optional<position_t> trailhead;
        for (const terrain::RoadPlace& place : network.places) {
          others.push_back (on_ground (g, place.at));
          if (place.kind == terrain::RoadPlace::Kind::Trailhead)
            trailhead = others.back ();
        }
        for (const Farm& farm : plan.farms)
          for (const Building& b : farm.buildings)
            others.push_back (b.at);
        const meters_t period_x = plan_value (g.period).x () * u::m;
        const meters_t period_z = plan_value (g.period).z () * u::m;
        const auto wrap = [&] (meters_t v, meters_t period) {
          return period > 0.0f * u::m
                   ? v - period *
                           std::floor (meters_value (v) / meters_value (period))
                   : v;
        };
        const auto apart = [&] (const position_t& a, const position_t& b) {
          return flat_length (terrain::minimum_image (
            moppe::plan (a) - moppe::plan (b), g.period));
        };
        struct Candidate {
          position_t at;
          meters_t score;
        };
        std::vector<Candidate> candidates;
        // Not up on the fells: below most of the way from the sea to the
        // highest sanctuary.
        terrain::SurfaceElevation ceiling =
          terrain::surface_elevation_point (1e30f * u::m);
        terrain::SurfaceElevation summit =
          terrain::surface_elevation_point (-1e30f * u::m);
        for (const terrain::RoadPlace& place : network.places)
          if (place.kind == terrain::RoadPlace::Kind::Sanctuary)
            summit = std::max (summit, place.height);
        if (summit > g.sea_level)
          ceiling = g.sea_level + 0.72f * (summit - g.sea_level);
        const auto consider = [&] (const plan_position_t& near,
                                   meters_t penalty) {
          const position_t at =
            on_ground (g,
                       plan_position (wrap (x_of (near), period_x),
                                      wrap (z_of (near), period_z)));
          if (elevation_of (at) > ceiling ||
              depth_at (g, moppe::plan (at)) > -1.0f * u::m)
            return;
          const meters_t rough = relief (g, at, 9.0f * u::m);
          if (rough > 1.8f * u::m)
            return;
          const map::ForestCover trees = g.forest
                                           ? g.forest (moppe::plan (at))
                                           : 0.0f * map::forest_cover[one];
          candidates.push_back ({ at, rough + 1.5f * trees * u::m + penalty });
        };
        // Beside the cart roads, old and new, and the footpaths in use.
        for (const terrain::Road& road : network.roads) {
          if ((road.kind != terrain::Road::Kind::Road &&
               years_value (road.abandoned) > 0.0f) ||
              road.points.size () < 2)
            continue;
          const meters_t penalty =
            (road.kind == terrain::Road::Kind::Road ? 0.0f * u::m
                                                    : 0.4f * u::m) +
            (years_value (road.abandoned) > 0.0f ? 0.5f * u::m : 0.0f * u::m);
          meters_t run = 30.0f * u::m;
          for (std::size_t i = 1; i < road.points.size (); ++i) {
            const plan_position_t a = road.points[i - 1];
            const plan_position_t b = road.points[i];
            const meters_t step = flat_length (a - b);
            run += step;
            if (run < 60.0f * u::m || step < 1e-3f * u::m)
              continue;
            run = 0.0f * u::m;
            const direction_t across = left_of (level (b - a));
            for (const meters_t off :
                 { -50.0f * u::m, -28.0f * u::m, 28.0f * u::m, 50.0f * u::m })
              consider (
                moppe::plan (ahead (at_height (b, 0.0f * u::m), across, off)),
                penalty);
          }
        }
        // And anywhere else open and level enough to farm.
        if (period_x > 0.0f * u::m && period_z > 0.0f * u::m)
          for (meters_t z = 80.0f * u::m; z < period_z; z += 160.0f * u::m)
            for (meters_t x = 80.0f * u::m; x < period_x; x += 160.0f * u::m)
              consider (plan_position (x, z), 1.0f * u::m);
        std::ranges::sort (candidates, {}, &Candidate::score);
        // The one by the trailhead goes first.
        if (trailhead)
          std::ranges::stable_partition (candidates, [&] (const Candidate& c) {
            const meters_t d = apart (c.at, *trailhead);
            return d > 160.0f * u::m && d < 480.0f * u::m;
          });
        const square_meters_t area = std::max (period_x, 1000.0f * u::m) *
                                     std::max (period_z, 1000.0f * u::m);
        const std::size_t most = std::clamp<std::size_t> (
          static_cast<std::size_t> (
            (area / (1.8e6f * u::m2)).numerical_value_in (one)),
          2,
          24);
        std::size_t made = 0;
        for (const Candidate& c : candidates) {
          if (made >= most)
            break;
          bool clear = true;
          for (const position_t& other : others)
            clear = clear && apart (other, c.at) > 380.0f * u::m;
          if (trailhead)
            clear = clear && apart (c.at, *trailhead) > 160.0f * u::m;
          if (!clear)
            continue;
          const std::vector<position_t> none;
          const std::optional<position_t> house = building_site (
            g, c.at, 0.0f * u::m, 14.0f * u::m, 5.0f * u::m, 0.8f * u::m, none);
          if (!house)
            continue;
          const std::optional<position_t> barn = building_site (g,
                                                                *house,
                                                                17.0f * u::m,
                                                                30.0f * u::m,
                                                                7.0f * u::m,
                                                                0.8f * u::m,
                                                                { *house });
          if (!barn)
            continue;
          Farm farm;
          farm.homestead = true;
          farm.place = network.places.size ();
          const terrain::RoadSample road = road_near (g, *house);
          direction_t facing = level (offset (*house, c.at));
          if (road.found)
            facing = level (road.across > 0.0f * u::m
                              ? -left_of (road_direction (road))
                              : left_of (road_direction (road)));
          farm.buildings.push_back (
            { Building::Kind::house,
              raised (g, *house, facing, Building::Kind::house),
              facing });
          const direction_t to_house = level (offset (*barn, *house));
          farm.buildings.push_back (
            { Building::Kind::barn,
              raised (g, *barn, to_house, Building::Kind::barn),
              to_house });
          plan.farms.push_back (std::move (farm));
          others.push_back (*house);
          ++made;
        }
      }

      // Kept clear of trees and loose rock: round each building, the
      // sanctuary's enclosure, and the jetty's root.
      void clear_round (PlacesPlan& plan) {
        if (plan.landing)
          plan.clearings.push_back ({ plan.landing->jetty.root, 7.0f * u::m });
        for (const Sanctuary& s : plan.sanctuaries)
          plan.clearings.push_back ({ s.centre, s.wall_radius + 2.5f * u::m });
        for (const Farm& farm : plan.farms)
          for (const Building& b : farm.buildings)
            plan.clearings.push_back (
              { b.at,
                b.kind == Building::Kind::mill    ? 7.5f * u::m
                : b.kind == Building::Kind::house ? 9.5f * u::m
                                                  : 11.5f * u::m });
        for (const Jetty& stage : plan.stages)
          plan.clearings.push_back ({ stage.root, 4.0f * u::m });
      }
    }
  }

  PlacesPlan plan_places (const terrain::RoadNetwork& network,
                          const PlaceGround& ground,
                          std::uint32_t seed) {
    using namespace places_parts;
    PlacesPlan plan;
    plan.period = ground.period;
    const terrain::SurfaceElevation sea = ground.sea_level;
    std::vector<std::size_t> sanctuary_of (network.places.size (),
                                           network.places.size ());
    for (std::size_t i = 0; i < network.places.size (); ++i) {
      const terrain::RoadPlace& place = network.places[i];
      Random random (seed ^ static_cast<std::uint32_t> (i * 0x9e3779b9u));
      using Kind = terrain::RoadPlace::Kind;
      switch (place.kind) {
      case Kind::Trailhead:
        break;
      case Kind::Landing:
        if (!plan.landing)
          plan.landing = plan_landing (ground, place, i, sea);
        break;
      case Kind::LakeOutlet:
        if (std::optional<Farm> farm =
              plan_farm (ground, place, i, plan.stages))
          plan.farms.push_back (std::move (*farm));
        break;
      case Kind::Sanctuary:
        sanctuary_of[i] = plan.sanctuaries.size ();
        plan.sanctuaries.push_back (plan_sanctuary (ground, place, i, random));
        break;
      }
      std::cerr << "moppe: places: " << place_kind (place.kind).what << " at "
                << plan_value (place.at).x () << ' '
                << plan_value (place.at).z () << ", "
                << terrain::surface_elevation_value (place.height)
                << " m, founded " << years_value (place.founded)
                << " years ago";
      if (years_value (place.abandoned) > 0.0f)
        std::cerr << ", left " << years_value (place.abandoned) << " years ago";
      std::cerr << '\n';
    }
    for (const auto& [a, b] : network.sightlines) {
      if (a >= sanctuary_of.size () || b >= sanctuary_of.size () ||
          sanctuary_of[a] >= plan.sanctuaries.size () ||
          sanctuary_of[b] >= plan.sanctuaries.size ())
        continue;
      plan.sanctuaries[sanctuary_of[a]].seen_from.push_back (sanctuary_of[b]);
      plan.sanctuaries[sanctuary_of[b]].seen_from.push_back (sanctuary_of[a]);
    }
    if (plan.landing) {
      const Jetty& j = plan.landing->jetty;
      std::cerr << "moppe: places: the jetty runs " << meters_value (j.length)
                << " m out from " << position_value (j.root)[0] << ' '
                << position_value (j.root)[1] << ' '
                << position_value (j.root)[2] << " toward "
                << direction_value (j.out)[0] << ' '
                << direction_value (j.out)[2] << '\n';
    }
    for (const Sanctuary& s : plan.sanctuaries)
      std::cerr << "moppe: places: an enclosure at "
                << position_value (s.centre)[0] << ' '
                << position_value (s.centre)[1] << ' '
                << position_value (s.centre)[2] << ", its gate toward "
                << direction_value (s.gate)[0] << ' '
                << direction_value (s.gate)[2] << ", "
                << (s.kept ? "kept" : "ruined") << '\n';
    for (const Farm& farm : plan.farms)
      for (const Building& b : farm.buildings)
        std::cerr << "moppe: places: a "
                  << (b.kind == Building::Kind::mill    ? "mill"
                      : b.kind == Building::Kind::house ? "house"
                                                        : "barn")
                  << " at " << position_value (b.at)[0] << ' '
                  << position_value (b.at)[1] << ' ' << position_value (b.at)[2]
                  << '\n';
    plan_homesteads (plan, network, ground);
    for (const Farm& farm : plan.farms)
      if (farm.homestead)
        std::cerr << "moppe: places: a farmstead at "
                  << position_value (farm.buildings[0].at)[0] << ' '
                  << position_value (farm.buildings[0].at)[1] << ' '
                  << position_value (farm.buildings[0].at)[2]
                  << ", its barn at "
                  << position_value (farm.buildings[1].at)[0] << ' '
                  << position_value (farm.buildings[1].at)[2] << '\n';
    clear_round (plan);
    std::cerr << "moppe: places: " << (plan.landing ? "a jetty, " : "")
              << plan.sanctuaries.size () << " sanctuaries, "
              << network.sightlines.size () << " sightlines, "
              << plan.farms.size () << " farms, " << plan.stages.size ()
              << " landing stages" << std::endl;
    return plan;
  }

  bool cleared (const PlacesPlan& plan, const position_t& at) {
    using namespace places_parts;
    for (const PlacesPlan::Clearing& c : plan.clearings) {
      const plan_displacement_t d = terrain::minimum_image (
        moppe::plan (at) - moppe::plan (c.centre), plan.period);
      if (squared (d) < c.radius * c.radius)
        return true;
    }
    return false;
  }

  void place_camps (
    PlacesPlan& plan,
    const terrain::RoadNetwork& network,
    const PlaceGround& ground,
    const std::function<std::optional<position_t> (const position_t&)>&
      hammock_near) {
    using namespace places_parts;
    plan.hearths.clear ();
    for (const terrain::RoadPlace& place : network.places) {
      if (place.kind == terrain::RoadPlace::Kind::Trailhead)
        continue;
      const position_t at = on_ground (ground, place.at);
      const std::optional<position_t> middle = hammock_near (at);
      if (!middle)
        continue;
      // The ring a few steps off the hammock, on level dry ground.
      for (int k = 0; k < 12; ++k) {
        const position_t p = on_ground (
          ground,
          moppe::plan (ahead (*middle, bearing (tau * k / 12), 3.0f * u::m)));
        if (relief (ground, p, 0.7f * u::m) > 0.3f * u::m ||
            !dry_round (ground, p, 1.0f * u::m, 0.4f * u::m))
          continue;
        const terrain::RoadSample road = road_near (ground, p);
        if (road.found && off_road (road) < 4.0f * u::m)
          continue;
        plan.hearths.push_back (p);
        std::cerr << "moppe: places: a camp at " << position_value (p)[0] << ' '
                  << position_value (p)[2] << ", "
                  << std::hypot (
                       position_value (p)[0] - plan_value (place.at).x (),
                       position_value (p)[2] - plan_value (place.at).z ())
                  << " m from the place" << std::endl;
        break;
      }
    }
  }

  std::optional<std::size_t> sanctuary_at_hand (const PlacesPlan& plan,
                                                const position_t& feet) {
    using namespace places_parts;
    for (std::size_t i = 0; i < plan.sanctuaries.size (); ++i) {
      const Sanctuary& s = plan.sanctuaries[i];
      const plan_displacement_t d = terrain::minimum_image (
        moppe::plan (feet) - moppe::plan (s.hearth), plan.period);
      const meters_t reach = s.platform_radius + 1.6f * u::m;
      if (squared (d) < reach * reach)
        return i;
    }
    return std::nullopt;
  }

  std::optional<position_t> hearth_at_hand (const PlacesPlan& plan,
                                            const position_t& feet) {
    using namespace places_parts;
    for (const position_t& hearth : plan.hearths) {
      const plan_displacement_t d = terrain::minimum_image (
        moppe::plan (feet) - moppe::plan (hearth), plan.period);
      if (squared (d) < (2.2f * u::m) * (2.2f * u::m))
        return at_height (moppe::plan (feet) - d,
                          position_value (hearth)[1] * u::m);
    }
    return std::nullopt;
  }

  proportion_t beacon_burn (play_time_t lit, play_time_t out, play_time_t now) {
    using namespace places_parts;
    if (lit > out)
      return smoothstep (0.0f, 6.0f, seconds_between (now, lit)) *
             proportion[one];
    return (1.0f - smoothstep (0.0f, 2.5f, seconds_between (now, out))) *
           smoothstep (0.0f, 6.0f, seconds_between (out, lit)) *
           proportion[one];
  }
}

namespace moppe::game {
  Mat4 building_frame (const Building& b) {
    using namespace places_parts;
    const direction_t forward = level (b.forward);
    return Mat4::translation (position_value (b.at)) *
           Mat4::basis (direction_value (left_of (forward)),
                        direction_value (up),
                        direction_value (forward));
  }

  std::optional<terrain::SurfaceElevation>
  floor_at (const PlacesPlan& plan, const plan_position_t& at) {
    using namespace places_parts;
    for (const Farm& farm : plan.farms) {
      if (farm.ruined)
        continue;
      for (const Building& b : farm.buildings) {
        if (b.kind == Building::Kind::mill)
          continue;
        const plan_displacement_t d =
          terrain::minimum_image (at - moppe::plan (b.at), plan.period);
        if (squared (d) > 64.0f * u::m2)
          continue;
        const direction_t f = level (b.forward), l = left_of (f);
        const meters_t lx =
          x_of (d) * direction_value (l)[0] + z_of (d) * direction_value (l)[2];
        const meters_t lz =
          x_of (d) * direction_value (f)[0] + z_of (d) * direction_value (f)[2];
        if (b.kind == Building::Kind::house) {
          if (abs (lx) < 3.38f * u::m && abs (lz) < 2.38f * u::m)
            return elevation_of (b.at) + 0.64f * u::m;
          continue;
        }
        const meters_t floor = farm_model::barn_floor[1] * u::m;
        if (abs (lx) < barn_inside_half_x && abs (lz) < barn_front)
          return elevation_of (b.at) + floor;
        if (abs (lx) < barn_door_half + 0.25f * u::m && lz >= barn_front &&
            lz < barn_front + barn_ramp_length)
          return elevation_of (b.at) + floor - 0.04f * u::m -
                 (floor + 0.08f * u::m) * (lz - barn_front) / barn_ramp_length;
      }
    }
    return std::nullopt;
  }

  namespace {
    namespace places_parts {
      // One unit box, centred at `centre`, its axes `x`, `y`, `z` scaled
      // to `size`.
      void box (render::DrawList& dl,
                const position_t& centre,
                const direction_t& x,
                const direction_t& y,
                const direction_t& z,
                const spatial_extent_t& size) {
        dl.push ();
        dl.mult (Mat4::basis (direction_value (x),
                              direction_value (y),
                              direction_value (z),
                              position_value (centre)));
        dl.scale (extent_value (size));
        dl.cube (1.0f);
        dl.pop ();
      }

      direction_t across_of (const direction_t& a, const direction_t& b) {
        return unit_direction (
          cross (direction_value (a), direction_value (b)));
      }

      void timber (render::DrawList& dl, Random& random, proportion_t tar) {
        const proportion_t grey =
          random.between (0.36f, 0.46f) * (whole - 0.55f * tar);
        dl.color ((grey * 1.02f).numerical_value_in (one),
                  (grey * 0.93f).numerical_value_in (one),
                  (grey * 0.82f).numerical_value_in (one));
      }

      // Planks across on two stringers, on pairs of posts, from the root
      // out; decay drops and tilts planks and leans posts.
      void build_jetty (render::DrawList& dl,
                        const Jetty& jetty,
                        const PlaceGround& g,
                        std::uint32_t seed,
                        std::vector<mov::Trunk>& colliders) {
        Random random (seed);
        const direction_t out = jetty.out;
        const direction_t side = left_of (out);
        const meters_t hw = jetty.half_width;
        // Posts in pairs every 2.4 m, sunk into the bed.
        for (meters_t d = 0.6f * u::m; d <= jetty.length; d += 2.4f * u::m)
          for (const direction_t& across : { -side, side }) {
            const position_t top = terrain::at_elevation (
              plan (
                ahead (ahead (jetty.root, out, d), across, hw - 0.06f * u::m)),
              jetty.deck - 0.05f * u::m);
            const terrain::SurfaceElevation bed =
              g.ground (plan (top)) - 0.6f * u::m;
            // Leaning by a level offset to the unit up.
            const direction_t axis = unit_direction (
              normalized (direction_value (up) +
                          direction_value (side) *
                            (jetty.decay * random.between (-0.08f, 0.08f))
                              .numerical_value_in (one)));
            const meters_t tall =
              quantity_cast<isq::height> (elevation_of (top) - bed) +
              0.15f * u::m;
            timber (dl, random, 0.8f * proportion[one]);
            box (dl,
                 ahead (top, axis, -(tall * 0.5f - 0.15f * u::m)),
                 side,
                 axis,
                 across_of (side, axis),
                 spatial_extent_in_metres (
                   Vec3 (0.16f, meters_value (tall), 0.16f)));
          }
        // The stringers.
        for (const direction_t& across : { -side, side }) {
          timber (dl, random, 0.6f * proportion[one]);
          box (dl,
               ahead (ahead (ahead (jetty.root, out, jetty.length * 0.5f),
                             across,
                             hw - 0.12f * u::m),
                      up,
                      -0.13f * u::m),
               side,
               up,
               out,
               spatial_extent_in_metres (
                 Vec3 (0.1f, 0.16f, meters_value (jetty.length))));
        }
        // The planks.
        for (meters_t d = 0.1f * u::m; d < jetty.length; d += 0.21f * u::m) {
          if (random.chance () < jetty.decay * 0.45f)
            continue;
          // Tilted by a level offset to the unit up.
          const direction_t across = unit_direction (
            normalized (direction_value (side) +
                        direction_value (up) *
                          (jetty.decay * random.between (-0.06f, 0.06f))
                            .numerical_value_in (one)));
          timber (dl, random, 0.0f * proportion[one]);
          box (dl,
               ahead (ahead (ahead (jetty.root, out, d), up, -0.025f * u::m),
                      side,
                      random.between (-0.03f * u::m, 0.03f * u::m)),
               across,
               across_of (out, across),
               out,
               spatial_extent_in_metres (
                 Vec3 (meters_value (2.0f * hw + random.between (-0.04f * u::m,
                                                                 0.06f * u::m)),
                       0.05f,
                       0.18f)));
        }
        // The deck to stand on: capsules lying along it, their tops at the
        // planks'.
        constexpr meters_t radius = 0.3f * u::m;
        for (const meters_t s : { -0.55f * hw, 0.0f * hw, 0.55f * hw }) {
          mov::Trunk deck;
          deck.root =
            ahead (ahead (ahead (jetty.root, side, s), out, -0.2f * u::m),
                   up,
                   -radius);
          deck.axis = out;
          deck.height = jetty.length + 0.2f * u::m;
          deck.radius = radius;
          colliders.push_back (deck);
        }
      }

      // A ring of fieldstones round `centre`, `courses` high, with a gap
      // `gap` metres wide toward `gate`; `ruin` tumbles the upper courses
      // off it.
      void build_wall (render::DrawList& dl,
                       const Sanctuary& s,
                       const PlaceGround& g,
                       std::uint32_t seed,
                       std::vector<mov::Trunk>& colliders) {
        Random random (seed);
        const position_t centre = at_height (plan (s.centre), 0.0f * u::m);
        const int courses = s.ruin > 0.0f * proportion[one]
                              ? (s.ruin > 0.7f * proportion[one] ? 1 : 2)
                              : 3;
        const meters_t r = s.wall_radius;
        const radians_t gate =
          std::atan2 (static_cast<float> (direction_value (s.gate)[2]),
                      static_cast<float> (direction_value (s.gate)[0])) *
          u::rad;
        const radians_t half_gap = 1.1f / meters_value (r) * u::rad;
        const auto in_gap = [&] (radians_t a) {
          const radians_t d =
            std::remainder (radians_value (a - gate), radians_value (tau)) *
            u::rad;
          return abs (d) < half_gap;
        };
        const int around =
          static_cast<int> (radians_value (tau) * meters_value (r) / 0.62f);
        const meters_t course_h = 0.27f * u::m;
        for (int k = 0; k < courses; ++k)
          for (int i = 0; i < around; ++i) {
            const radians_t a =
              tau * (i + 0.5f * (k % 2) + random.between (-0.15f, 0.15f)) /
              around;
            if (in_gap (a))
              continue;
            const direction_t way = bearing (a);
            const position_t at = ahead (
              centre, way, r + random.between (-0.08f * u::m, 0.08f * u::m));
            const terrain::SurfaceElevation ground = g.ground (plan (at));
            const meters_t radius =
              random.between (0.32f * u::m, 0.42f * u::m) * (1.0f - 0.12f * k);
            const meters_t thick =
              k == 0 ? course_h + 0.1f * u::m
                     : random.between (0.2f * u::m, 0.28f * u::m);
            build_fieldstone (dl,
                              seed * 31u + k * 977u + i,
                              lifted (at, ground - 0.1f * u::m + course_h * k),
                              radius,
                              thick,
                              a +
                                random.between (-0.4f * u::rad, 0.4f * u::rad),
                              Vec3 (random.between (-0.08f, 0.08f),
                                    0.0f,
                                    random.between (-0.08f, 0.08f)));
          }
        // What has tumbled lies about the foot of it, inside and out.
        const int fallen =
          static_cast<int> ((around * 1.4f * s.ruin).numerical_value_in (one));
        for (int i = 0; i < fallen; ++i) {
          const radians_t a = random.between (0.0f * u::rad, tau);
          if (in_gap (a))
            continue;
          const meters_t off = random.between (-1.6f * u::m, 1.6f * u::m);
          const position_t at = ahead (centre, bearing (a), r + off);
          build_fieldstone (dl,
                            seed * 53u + i,
                            lifted (at, g.ground (plan (at)) - 0.08f * u::m),
                            random.between (0.2f * u::m, 0.36f * u::m),
                            random.between (0.15f * u::m, 0.24f * u::m),
                            random.between (0.0f * u::rad, tau),
                            Vec3 (random.between (-0.3f, 0.3f),
                                  0.0f,
                                  random.between (-0.3f, 0.3f)));
        }
        // Two taller stones stand either side of the way in.
        if (s.ruin < 0.7f * proportion[one])
          for (const int side : { -1, 1 }) {
            const position_t at =
              ahead (centre, bearing (gate + side * half_gap * 1.25f), r);
            build_fieldstone (dl,
                              seed * 7u + (side > 0 ? 1u : 2u),
                              lifted (at, g.ground (plan (at)) - 0.15f * u::m),
                              0.36f * u::m,
                              1.25f * u::m * (whole - 0.4f * s.ruin),
                              gate,
                              Vec3 ());
          }
        // A kept wall stops a body, which must go round by the gap or
        // jump it; a tumbled one is rubble to step over.
        const meters_t tall =
          courses >= 3 ? 1.35f * u::m : course_h * courses + 0.05f * u::m;
        const int posts =
          static_cast<int> (radians_value (tau) * meters_value (r) / 0.9f);
        for (int i = 0; i < posts; ++i) {
          const radians_t a = tau * (i + 0.5f) / posts;
          if (in_gap (a))
            continue;
          mov::Trunk stone;
          const position_t at = ahead (centre, bearing (a), r);
          stone.root = terrain::at_elevation (
            plan (at), g.ground (plan (at)) - 0.1f * u::m);
          stone.axis = up;
          stone.height = std::max (0.05f * u::m, tall - 0.35f * u::m);
          stone.radius = 0.35f * u::m;
          colliders.push_back (stone);
        }
      }

      // The fire platform: a drum of coursed stones, its top laid with
      // flat slabs blackened by the fires that were lit on it, and the
      // footpath's last stretch, paved, from it out through the gate.
      void build_platform (render::DrawList& dl,
                           const Sanctuary& s,
                           const PlaceGround& g,
                           std::uint32_t seed,
                           std::vector<mov::Trunk>& colliders) {
        Random random (seed);
        const terrain::SurfaceElevation top = elevation_of (s.hearth);
        const meters_t R = s.platform_radius;
        const position_t middle = at_height (plan (s.hearth), 0.0f * u::m);
        const int around =
          static_cast<int> (radians_value (tau) * meters_value (R) / 0.5f);
        for (int k = 0; k < 3; ++k) {
          const terrain::SurfaceElevation bottom = top - 0.22f * u::m * (3 - k);
          for (int i = 0; i < around; ++i) {
            const radians_t a =
              tau * (i + 0.5f * (k % 2) + random.between (-0.12f, 0.12f)) /
              around;
            const position_t at = ahead (middle, bearing (a), R - 0.25f * u::m);
            const terrain::SurfaceElevation ground = g.ground (plan (at));
            // The lowest course reaches down to the ground, wherever
            // that is under it.
            const terrain::SurfaceElevation from =
              k == 0 ? std::min (bottom, ground - 0.1f * u::m) : bottom;
            if (top - 0.22f * u::m * (2 - k) < ground - 0.05f * u::m ||
                random.chance () < s.ruin * (0.15f + 0.2f * k))
              continue;
            build_fieldstone (
              dl,
              seed * 17u + k * 401u + i,
              terrain::at_elevation (plan (at), from),
              0.34f * u::m,
              quantity_cast<isq::height> (top - 0.22f * u::m * (2 - k) - from +
                                          0.02f * u::m),
              a + random.between (-0.3f * u::rad, 0.3f * u::rad),
              Vec3 (random.between (-0.05f, 0.05f),
                    0.0f,
                    random.between (-0.05f, 0.05f)));
          }
        }
        // The slabs over the top.
        for (int ring = 0; ring < 2; ++ring) {
          const int count = ring == 0 ? 1 : 6;
          for (int i = 0; i < count; ++i) {
            const radians_t a =
              tau * (i + random.between (-0.1f, 0.1f)) / count;
            if (random.chance () < 0.6f * s.ruin)
              continue;
            const position_t at =
              ahead (ahead (middle, bearing (a), ring * (R * 0.55f)),
                     bearing (random.between (0.0f * u::rad, tau)),
                     0.25f * u::m * s.ruin);
            build_fieldstone (
              dl,
              seed * 29u + ring * 61u + i,
              terrain::at_elevation (plan (at), top - 0.12f * u::m),
              ring == 0 ? 0.55f * u::m : 0.5f * u::m,
              0.14f * u::m,
              random.between (0.0f * u::rad, tau),
              Vec3 (random.between (-0.03f, 0.03f),
                    0.0f,
                    random.between (-0.03f, 0.03f)));
          }
        }
        // What has fallen off it lies round its foot.
        for (int i = 0;
             i < static_cast<int> ((14 * s.ruin).numerical_value_in (one));
             ++i) {
          const position_t at =
            ahead (middle,
                   bearing (random.between (0.0f * u::rad, tau)),
                   R + random.between (0.1f * u::m, 1.4f * u::m));
          build_fieldstone (dl,
                            seed * 71u + i,
                            lifted (at, g.ground (plan (at)) - 0.06f * u::m),
                            random.between (0.2f * u::m, 0.34f * u::m),
                            random.between (0.14f * u::m, 0.22f * u::m),
                            random.between (0.0f * u::rad, tau),
                            Vec3 (random.between (-0.3f, 0.3f),
                                  0.0f,
                                  random.between (-0.3f, 0.3f)));
        }
        // Blackened where the fires stood.
        dl.begin (render::Prim::Triangles);
        dl.color (0.08f, 0.07f, 0.06f);
        dl.normal (direction_value (up));
        constexpr int rim = 16;
        for (int i = 0; i < rim; ++i) {
          const meters_t jitter0 =
            (0.75f + 0.2f * std::sin (i * 2.7f + seed)) * u::m;
          const meters_t jitter1 =
            (0.75f + 0.2f * std::sin ((i + 1) % rim * 2.7f + seed)) * u::m;
          dl.vertex (position_value (
            terrain::at_elevation (plan (middle), top + 0.035f * u::m)));
          dl.vertex (position_value (
            lifted (ahead (middle, bearing (tau * (i + 1) / rim), jitter1),
                    top + 0.035f * u::m)));
          dl.vertex (position_value (
            lifted (ahead (middle, bearing (tau * i / rim), jitter0),
                    top + 0.035f * u::m)));
        }
        dl.end ();
        // It stands up out of the summit, too high to step onto but solid
        // to jump up on: upright capsules packed under its top.
        constexpr meters_t post = 0.35f * u::m;
        for (meters_t x = -R + post; x <= R - post + 0.01f * u::m;
             x += 0.6f * u::m)
          for (meters_t z = -R + post; z <= R - post + 0.01f * u::m;
               z += 0.6f * u::m) {
            if (x * x + z * z > (R - post) * (R - post))
              continue;
            const plan_position_t foot_at =
              plan (middle) + plan_displacement (x, z);
            const terrain::SurfaceElevation foot =
              g.ground (foot_at) - 0.2f * u::m;
            mov::Trunk drum;
            drum.root = terrain::at_elevation (foot_at, foot);
            drum.axis = up;
            drum.radius = post;
            drum.height = std::max<meters_t> (
              0.01f * u::m, quantity_cast<isq::height> (top - post - foot));
            colliders.push_back (drum);
          }

        // Flat stones laid along the way in from the platform out through
        // the gate and on down the last of the path.
        const direction_t gate = s.gate;
        const direction_t side = left_of (gate);
        for (meters_t d = R + 0.6f * u::m; d < s.wall_radius + 14.0f * u::m;
             d += 0.85f * u::m) {
          if (d > s.wall_radius + 2.0f * u::m &&
              random.chance () < 0.25f * proportion[one] + s.ruin * 0.3f)
            continue;
          for (const meters_t w : { -0.38f * u::m, 0.38f * u::m }) {
            if (random.chance () < 0.15f * probability[one])
              continue;
            const position_t at =
              ahead (ahead (middle, gate, d),
                     side,
                     w + random.between (-0.08f * u::m, 0.08f * u::m));
            build_fieldstone (
              dl,
              seed * 41u + static_cast<std::uint32_t> (meters_value (d) * 10) +
                (w > 0.0f * u::m ? 1u : 0u),
              lifted (at, g.ground (plan (at)) - 0.06f * u::m),
              random.between (0.3f * u::m, 0.4f * u::m),
              0.1f * u::m,
              random.between (0.0f * u::rad, tau),
              Vec3 ());
          }
        }
      }

      // A point in a building's frame (tools/figure/farm.py's metres).
      position_t model_point (meters_t x, meters_t y, meters_t z) {
        return position (
          Vec3 (meters_value (x), meters_value (y), meters_value (z)));
      }

      // The barn (tools/figure/farm.py) is walked into up its ramp and
      // through its doorway: its walls, the open doors' leaves, the
      // stalls' partitions and the pen's walls, the posts, the nest
      // shelf, and the churns and tub are rails and posts; its floor and
      // its ramp are capsules lying under the boards and the stone, their
      // tops where the feet go. A barn lived in has a beast in each stall,
      // which stands in the way like one.
      void barn_colliders (const Mat4& frame,
                           bool kept,
                           std::vector<mov::Trunk>& colliders) {
        const auto lying =
          [&] (const position_t& a, const position_t& b, meters_t radius) {
            mov::Trunk t;
            t.root = position (frame.transform_point (position_value (a)));
            const displacement_t to =
              displacement (frame.transform_point (position_value (b)) -
                            position_value (t.root));
            t.height = length (displacement_value (to)) * u::m;
            t.axis = unit_direction (displacement_value (to) /
                                     std::max (meters_value (t.height), 1e-3f));
            t.radius = radius;
            colliders.push_back (t);
          };
        const auto upright =
          [&] (meters_t x, meters_t z, meters_t top, meters_t radius) {
            lying (
              model_point (x, 0.0f * u::m, z), model_point (x, top, z), radius);
          };
        const meters_t fl = farm_model::barn_floor[1] * u::m;
        const meters_t ix = barn_inside_half_x, iz = barn_inside_half_z;
        const meters_t door = barn_door_half;
        constexpr meters_t wall = 0.1f * u::m;
        const meters_t wx = ix + 0.07f * u::m, wz = iz + 0.07f * u::m;
        for (meters_t y = fl + 0.3f * u::m; y < 3.3f * u::m;
             y += 0.35f * u::m) {
          lying (model_point (-wx, y, -wz), model_point (wx, y, -wz), wall);
          for (const meters_t x : { -wx, wx })
            lying (model_point (x, y, -wz), model_point (x, y, wz), wall);
          lying (
            model_point (-wx, y, wz), model_point (-door - wall, y, wz), wall);
          lying (
            model_point (door + wall, y, wz), model_point (wx, y, wz), wall);
          // The open leaves, square to the wall either side.
          for (const int s : { -1, 1 })
            lying (model_point (
                     s * (door + 0.17f * u::m), y, barn_front + 0.1f * u::m),
                   model_point (
                     s * (door + 0.17f * u::m), y, barn_front + 1.75f * u::m),
                   0.06f * u::m);
        }
        constexpr meters_t deck = 0.3f * u::m;
        for (meters_t z = -iz + 0.25f * u::m; z <= barn_front + 0.01f * u::m;
             z += 0.4f * u::m) {
          const meters_t half = z > iz ? door : ix - 0.25f * u::m;
          lying (model_point (-half, fl - deck, z),
                 model_point (half, fl - deck, z),
                 deck);
        }
        // The ramp: a stair of capsules whose tops follow its slope.
        for (meters_t z = barn_front + 0.2f * u::m;
             z < barn_front + barn_ramp_length;
             z += 0.3f * u::m) {
          const meters_t top =
            fl - 0.04f * u::m -
            (fl + 0.08f * u::m) * (z - barn_front) / barn_ramp_length;
          lying (model_point (-door - 0.2f * u::m, top - deck, z),
                 model_point (door + 0.2f * u::m, top - deck, z),
                 deck);
        }
        // The stalls' partitions and the pen's walls (farm.py's
        // BARN_STALLS and BARN_PEN), and the posts at their fronts.
        constexpr std::array<std::array<meters_t, 3>, 4> partitions { {
          { 4.06f * u::m, -0.8f * u::m, 1.4f * u::m },
          { 2.26f * u::m, -1.4f * u::m, 1.4f * u::m },
          { 0.91f * u::m, -1.4f * u::m, 1.2f * u::m },
          { -0.44f * u::m, -1.4f * u::m, 1.2f * u::m },
        } };
        for (const auto& [x, front, tall] : partitions) {
          for (meters_t y = fl + 0.35f * u::m; y < fl + tall; y += 0.35f * u::m)
            lying (
              model_point (x, y, -iz), model_point (x, y, front), 0.05f * u::m);
          upright (x, front, 3.4f * u::m, 0.09f * u::m);
        }
        for (meters_t y = fl + 0.3f * u::m; y < fl + 0.95f * u::m;
             y += 0.3f * u::m) {
          lying (model_point (-0.44f * u::m, y, -1.2f * u::m),
                 model_point (-3.4f * u::m, y, -1.2f * u::m),
                 0.05f * u::m);
          lying (model_point (-3.4f * u::m, y, -iz),
                 model_point (-3.4f * u::m, y, -1.2f * u::m),
                 0.05f * u::m);
        }
        // The nest shelf, the churns, and the tub.
        lying (
          model_point (-ix + 0.45f * u::m, fl + 0.3f * u::m, -3.85f * u::m),
          model_point (-ix + 0.45f * u::m, fl + 0.3f * u::m, -1.6f * u::m),
          0.08f * u::m);
        lying (
          model_point (-ix + 0.45f * u::m, fl + 0.75f * u::m, -3.85f * u::m),
          model_point (-ix + 0.45f * u::m, fl + 0.75f * u::m, -1.6f * u::m),
          0.08f * u::m);
        upright (-5.1f * u::m, 3.38f * u::m, fl + 0.6f * u::m, 0.3f * u::m);
        upright (-5.2f * u::m, 2.4f * u::m, fl + 0.4f * u::m, 0.36f * u::m);
        // The wheelbarrow, the feed bin, and the milking stool
        // (farm.py's barn_things).
        lying (model_point (3.15f * u::m, fl + 0.35f * u::m, 2.75f * u::m),
               model_point (1.9f * u::m, fl + 0.35f * u::m, 2.75f * u::m),
               0.3f * u::m);
        lying (model_point (-5.53f * u::m, fl + 0.35f * u::m, 0.0f * u::m),
               model_point (-5.53f * u::m, fl + 0.35f * u::m, 0.7f * u::m),
               0.32f * u::m);
        upright (1.25f * u::m, -0.7f * u::m, fl + 0.4f * u::m, 0.17f * u::m);
        // The beasts at their mangers: a body lying down each stall.
        struct Stall {
          meters_t x0, x1, front;
          bool horse;
        };
        if (kept)
          for (const auto& [x0, x1, front, horse] : std::array<Stall, 4> { {
                 { 5.86f * u::m, 4.06f * u::m, -0.8f * u::m, true },
                 { 4.06f * u::m, 2.26f * u::m, -0.8f * u::m, true },
                 { 2.26f * u::m, 0.91f * u::m, -1.4f * u::m, false },
                 { 0.91f * u::m, -0.44f * u::m, -1.4f * u::m, false },
               } }) {
            const meters_t x = 0.5f * (x0 + x1);
            lying (model_point (x, fl + 1.0f * u::m, -3.0f * u::m),
                   model_point (x, fl + 1.0f * u::m, front - 0.35f * u::m),
                   horse ? 0.4f * u::m : 0.36f * u::m);
          }
      }

      // A building's walls stop bodies: capsules round inside its
      // footprint.
      void building_colliders (const Building& b,
                               bool ruined,
                               bool kept,
                               std::vector<mov::Trunk>& colliders) {
        meters_t hx = 2.2f * u::m, hz = 2.5f * u::m, tall = 3.4f * u::m;
        if (b.kind == Building::Kind::house)
          hx = 3.6f * u::m, hz = 2.6f * u::m, tall = 3.0f * u::m;
        else if (b.kind == Building::Kind::barn)
          hx = 4.5f * u::m, hz = 3.0f * u::m, tall = 3.8f * u::m;
        if (ruined)
          tall = 0.4f * u::m;
        const Mat4 frame = building_frame (b);
        if (b.kind == Building::Kind::house && !ruined) {
          // The house can be walked into (tools/figure/farm.py): its walls
          // are rails lying along their lines, the doorway open between
          // x -1.35 and -0.45 at the front, and its floor, the step before
          // the door, and the threshold are capsules lying under the
          // boards, their tops where the feet go.
          const auto lying = [&] (const position_t& a,
                                  const position_t& b,
                                  meters_t radius) {
            mov::Trunk t;
            t.root = position (frame.transform_point (position_value (a)));
            const displacement_t to =
              displacement (frame.transform_point (position_value (b)) -
                            position_value (t.root));
            t.height = length (displacement_value (to)) * u::m;
            t.axis = unit_direction (displacement_value (to) /
                                     std::max (meters_value (t.height), 1e-3f));
            t.radius = radius;
            colliders.push_back (t);
          };
          constexpr meters_t wall = 0.1f * u::m;
          for (meters_t y = 0.85f * u::m; y < 3.0f * u::m; y += 0.35f * u::m) {
            lying (model_point (-3.44f * u::m, y, -2.44f * u::m),
                   model_point (3.44f * u::m, y, -2.44f * u::m),
                   wall);
            for (const meters_t x : { -3.44f * u::m, 3.44f * u::m })
              lying (model_point (x, y, -2.44f * u::m),
                     model_point (x, y, 2.44f * u::m),
                     wall);
            lying (model_point (-3.44f * u::m, y, 2.44f * u::m),
                   model_point (-1.35f * u::m - wall, y, 2.44f * u::m),
                   wall);
            lying (model_point (-0.45f * u::m + wall, y, 2.44f * u::m),
                   model_point (3.44f * u::m, y, 2.44f * u::m),
                   wall);
          }
          constexpr meters_t deck = 0.3f * u::m;
          for (meters_t z = -2.1f * u::m; z <= 2.15f * u::m; z += 0.4f * u::m)
            lying (model_point (-3.1f * u::m, 0.64f * u::m - deck, z),
                   model_point (3.1f * u::m, 0.64f * u::m - deck, z),
                   deck);
          lying (model_point (-1.3f * u::m, 0.64f * u::m - deck, 2.6f * u::m),
                 model_point (-0.5f * u::m, 0.64f * u::m - deck, 2.6f * u::m),
                 deck);
          lying (model_point (-1.4f * u::m, 0.42f * u::m - deck, 3.0f * u::m),
                 model_point (-0.4f * u::m, 0.42f * u::m - deck, 3.0f * u::m),
                 deck);
          lying (model_point (-1.45f * u::m, 0.2f * u::m - deck, 3.45f * u::m),
                 model_point (-0.35f * u::m, 0.2f * u::m - deck, 3.45f * u::m),
                 deck);
          // The chimney stack standing in the room.
          lying (model_point (0.6f * u::m, 0.6f * u::m, -0.3f * u::m),
                 model_point (0.6f * u::m, 3.0f * u::m, -0.3f * u::m),
                 0.45f * u::m);
          return;
        }
        if (b.kind == Building::Kind::barn && !ruined) {
          barn_colliders (frame, kept, colliders);
          return;
        }
        constexpr meters_t radius = 0.55f * u::m;
        const auto post = [&] (meters_t x, meters_t z) {
          mov::Trunk wall;
          wall.root = position (frame.transform_point (
            position_value (model_point (x, -0.5f * u::m, z))));
          wall.axis = up;
          wall.height = tall;
          wall.radius = radius;
          colliders.push_back (wall);
        };
        for (meters_t x = -hx + radius; x <= hx - radius + 0.01f * u::m;
             x += 0.9f * u::m)
          for (const meters_t z : { -hz + radius, hz - radius })
            post (x, z);
        for (meters_t z = -hz + radius + 0.9f * u::m; z < hz - radius;
             z += 0.9f * u::m)
          for (const meters_t x : { -hx + radius, hx - radius })
            post (x, z);
      }

      // A building's stone base or its timber, at tier 0 (near), 1, or 2
      // (far); a base has no middle tier of its own.
      const model_mesh::Mesh&
      building_mesh (Building::Kind kind, bool base, int tier) {
        using namespace farm_model;
        switch (kind) {
        case Building::Kind::mill:
          if (base)
            return tier == 0 ? mill_base : mill_base_far;
          return tier == 0 ? mill : tier == 1 ? mill_mid : mill_far;
        case Building::Kind::house:
          if (base)
            return tier == 0 ? house_base : house_base_far;
          return tier == 0 ? house : tier == 1 ? house_mid : house_far;
        case Building::Kind::barn:
          break;
        }
        if (base)
          return tier == 0 ? barn_base : barn_base_far;
        return tier == 0 ? barn : tier == 1 ? barn_mid : barn_far;
      }

      const model_mesh::Mesh& wheel_mesh (int tier) {
        return tier == 0   ? farm_model::mill_wheel
               : tier == 1 ? farm_model::mill_wheel_mid
                           : farm_model::mill_wheel_far;
      }

      // The finest parts of a building's near tier (battens, glazing
      // bars, coursed stones); its middle tier's are twice that.
      constexpr meters_t building_detail = 0.05f * u::m;

      // How the mill's wheel turns while the mill is kept.
      constexpr radians_per_second_t wheel_turn = -0.9f * u::rad / u::s;

      // How far apart two places are, squared.
      square_meters_t distance2 (const position_t& a, const position_t& b) {
        return length2 (displacement_value (offset (b, a))) * u::m2;
      }
    }
  }

  position_t PlacesLandscape::near_copy (const position_t& at,
                                         const position_t& camera) const {
    position_t out = at;
    const plan_displacement_t gap = plan (camera) - plan (at);
    position_value (out)[0] += terrain::whole_periods (
      plan_value (gap).x (), plan_value (m_period).x ());
    position_value (out)[2] += terrain::whole_periods (
      plan_value (gap).z (), plan_value (m_period).z ());
    return out;
  }

  void PlacesLandscape::rebuild (render::Renderer& renderer,
                                 const PlacesPlan& plan,
                                 const PlaceGround& ground) {
    using namespace places_parts;
    m_pieces.clear ();
    m_colliders.clear ();
    m_beacons.clear ();
    m_period = plan.period;
    const auto bake =
      [&] (const position_t& origin, meters_t reach, const auto& record) {
        render::DrawList dl;
        render::DrawState solid;
        solid.cull = false;
        dl.state (solid);
        dl.lit (true);
        dl.fogged (true);
        record (dl);
        dl.state (render::DrawState ());
        if (dl.empty ())
          return;
        Piece piece;
        piece.mesh = renderer.create_mesh (dl);
        piece.origin = origin;
        piece.reach = reach;
        m_pieces.push_back (std::move (piece));
      };
    std::uint32_t seed = 0x51ac7u;
    if (plan.landing) {
      const Jetty& jetty = plan.landing->jetty;
      bake (jetty.root, 2500.0f * u::m, [&] (render::DrawList& dl) {
        build_jetty (dl, jetty, ground, seed++, m_colliders);
        // The post the painter is tied to.
        Random post (seed);
        timber (dl, post, 0.7f * proportion[one]);
        box (dl,
             ahead (plan.landing->bollard, up, 0.25f * u::m),
             left_of (jetty.out),
             up,
             jetty.out,
             spatial_extent_in_metres (Vec3 (0.18f, 0.6f, 0.18f)));
      });
    }
    for (const Jetty& stage : plan.stages)
      bake (stage.root, 1800.0f * u::m, [&] (render::DrawList& dl) {
        build_jetty (dl, stage, ground, seed++, m_colliders);
      });
    for (const Sanctuary& s : plan.sanctuaries) {
      m_beacons.push_back (s.hearth);
      bake (s.centre, 4000.0f * u::m, [&] (render::DrawList& dl) {
        build_wall (dl, s, ground, seed++, m_colliders);
        build_platform (dl, s, ground, seed++, m_colliders);
      });
    }
    const auto record_lit = [] (render::DrawList& dl) {
      render::DrawState solid;
      solid.cull = false;
      dl.state (solid);
      dl.lit (true);
      dl.fogged (true);
    };
    for (const Farm& farm : plan.farms)
      for (const Building& b : farm.buildings) {
        const bool roofless = farm.ruined;
        const Mat4 frame = building_frame (b);
        std::array<render::MeshPtr, 3> tiers;
        for (int t = 0; t < 3; ++t) {
          render::DrawList dl;
          record_lit (dl);
          dl.push ();
          dl.mult (frame);
          model_mesh::record (dl, building_mesh (b.kind, true, t));
          if (!roofless)
            model_mesh::record (dl, building_mesh (b.kind, false, t));
          dl.pop ();
          dl.state (render::DrawState ());
          tiers[t] = renderer.create_mesh (dl);
        }
        Piece piece;
        piece.mesh = std::move (tiers[0]);
        piece.coarser = { std::move (tiers[1]), std::move (tiers[2]) };
        piece.detail = building_detail;
        piece.origin = b.at;
        piece.reach = 3000.0f * u::m;
        m_pieces.push_back (std::move (piece));
        building_colliders (b, roofless, farm.kept, m_colliders);
        if (b.kind == Building::Kind::mill && !roofless) {
          std::array<render::MeshPtr, 3> wheels;
          for (int t = 0; t < 3; ++t) {
            render::DrawList dl;
            dl.lit (true);
            dl.fogged (true);
            model_mesh::record (dl, wheel_mesh (t));
            wheels[t] = renderer.create_mesh (dl);
          }
          Piece wheel;
          wheel.mesh = std::move (wheels[0]);
          wheel.coarser = { std::move (wheels[1]), std::move (wheels[2]) };
          wheel.detail = building_detail;
          wheel.origin =
            position (frame.transform_point (farm_model::mill_axle));
          wheel.reach = 1500.0f * u::m;
          wheel.wheel = farm.kept;
          wheel.turning = farm.kept;
          wheel.axis = unit_direction (frame.transform_vector (Vec3 (1, 0, 0)));
          wheel.frame = frame;
          m_pieces.push_back (std::move (wheel));
        }
      }
  }

  const render::Mesh& PlacesLandscape::tier (const Piece& piece,
                                             meters_t distance,
                                             float focal) const {
    // A part `detail` across spans detail * focal / distance pixels; the
    // near tier stands while its finest span a pixel, the middle while
    // its own (twice as coarse) do.
    if (piece.detail <= 0.0f * u::m || !piece.coarser[0] || focal <= 0.0f)
      return *piece.mesh;
    const magnitude_t pixels =
      piece.detail * focal / std::max (distance, 0.5f * u::m);
    if (pixels >= 1.0f)
      return *piece.mesh;
    return 2.0f * pixels >= 1.0f ? *piece.coarser[0] : *piece.coarser[1];
  }

  void PlacesLandscape::draw (render::Renderer& renderer,
                              const position_t& camera,
                              seconds_t time,
                              std::uint64_t motion_base) const {
    using namespace places_parts;
    std::uint64_t id = motion_base;
    const float focal = renderer.scene_focal_pixels ();
    for (const Piece& piece : m_pieces) {
      ++id;
      const position_t at = near_copy (piece.origin, camera);
      if (distance2 (at, camera) > piece.reach * piece.reach)
        continue;
      const displacement_t shift = offset (piece.origin, at);
      const render::Mesh& mesh = tier (
        piece, length (displacement_value (offset (camera, at))) * u::m, focal);
      if (piece.frame) {
        // The wheel, in its own frame about the axle, turning.
        const radians_t angle =
          piece.turning ? wheel_turn * time : 0.4f * u::rad;
        Mat4 frame = *piece.frame;
        const Mat4 model = Mat4::translation (displacement_value (shift)) *
                           frame * Mat4::translation (farm_model::mill_axle) *
                           Mat4::rotation (angle, Vec3 (1, 0, 0));
        renderer.draw_mesh (mesh, model, id);
        continue;
      }
      renderer.draw_mesh (
        mesh, Mat4::translation (displacement_value (shift)), id);
    }
  }

  void PlacesLandscape::cast_shadows (render::Renderer& renderer,
                                      const position_t& camera) const {
    using namespace places_parts;
    for (const Piece& piece : m_pieces) {
      const position_t at = near_copy (piece.origin, camera);
      if (piece.frame ||
          distance2 (at, camera) > (160.0f * u::m) * (160.0f * u::m))
        continue;
      // The sun's near texels are some eight centimetres: a building
      // shades the ground by its massing alone.
      renderer.draw_mesh (
        piece.coarser[1] ? *piece.coarser[1] : *piece.mesh,
        Mat4::translation (displacement_value (offset (piece.origin, at))));
    }
  }

  void
  PlacesLandscape::draw_beacons (render::DrawList& dl,
                                 const position_t& camera,
                                 std::span<const proportion_t> burn) const {
    using namespace places_parts;
    for (std::size_t i = 0; i < m_beacons.size (); ++i) {
      const position_t at = near_copy (m_beacons[i], camera);
      if (distance2 (at, camera) > (300.0f * u::m) * (300.0f * u::m))
        continue;
      draw_hearth (dl, at, i < burn.size () ? burn[i] : 0.0f * proportion[one]);
    }
  }

  void PlacesLandscape::draw_beacon_fires (render::Renderer& renderer,
                                           const position_t& camera,
                                           std::span<const proportion_t> burn,
                                           proportion_t daylight,
                                           seconds_t time) const {
    for (std::size_t i = 0; i < m_beacons.size () && i < burn.size (); ++i)
      if (burn[i] > 0.01f * proportion[one])
        draw_beacon (renderer,
                     near_copy (m_beacons[i], camera),
                     camera,
                     burn[i],
                     daylight,
                     time);
  }
}
