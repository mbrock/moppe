#include <moppe/game/places.hh>

#include <moppe/game/cairn.hh>
#include <moppe/game/camp.hh>
#include <moppe/game/farm_model.hh>
#include <moppe/game/model_mesh.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/gfx/signal.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace moppe::game {
  namespace {
    // The engine is a unity build: these helpers keep to a namespace of
    // their own.
    namespace places_parts {
      constexpr float tau = 6.2831853f;
      const Vec3 up (0.0f, 1.0f, 0.0f);

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
        float between (float low, float high) {
          return low + (high - low) * unit ();
        }

      private:
        std::uint32_t m_state;
      };

      Vec3 level (Vec3 v) {
        v[1] = 0.0f;
        return length2 (v) > 1e-10f ? normalized (v) : Vec3 (0, 0, 1);
      }

      // The level direction at `angle` radians round from +x toward +z.
      Vec3 bearing (float angle) {
        return Vec3 (std::cos (angle), 0.0f, std::sin (angle));
      }

      float flat_distance (const Vec3& a, const Vec3& b) {
        return std::hypot (static_cast<float> (a[0] - b[0]),
                           static_cast<float> (a[2] - b[2]));
      }

      Vec3 left_of (const Vec3& forward) {
        return Vec3 (forward[2], 0.0f, -forward[0]);
      }

      Vec3 on_ground (const PlaceGround& g, float x, float z) {
        return Vec3 (x, g.ground (x, z), z);
      }

      // How deep the still water stands over the ground at a point.
      float depth_at (const PlaceGround& g, float x, float z) {
        return g.water (x, z) - g.ground (x, z);
      }

      float speed_at (const PlaceGround& g, float x, float z) {
        const Vec3 c = g.current (x, z);
        return std::hypot (static_cast<float> (c[0]),
                           static_cast<float> (c[2]));
      }

      // How far the ground rises and falls within `radius` of a point.
      float relief (const PlaceGround& g, const Vec3& at, float radius) {
        float low = 1e30f, high = -1e30f;
        for (int ring = 1; ring <= 2; ++ring)
          for (int i = 0; i < 12; ++i) {
            const Vec3 p = at + bearing (tau * i / 12.0f) * (radius * ring / 2);
            const float h = g.ground (p[0], p[2]);
            low = std::min (low, h);
            high = std::max (high, h);
          }
        return high - low;
      }

      // Dry, well above any water, all the way round to `radius`.
      bool dry_round (const PlaceGround& g,
                      const Vec3& at,
                      float radius,
                      float margin) {
        for (int ring = 0; ring <= 2; ++ring)
          for (int i = 0; i < (ring ? 12 : 1); ++i) {
            const Vec3 p = at + bearing (tau * i / 12.0f) * (radius * ring / 2);
            if (depth_at (g, p[0], p[2]) > -margin)
              return false;
          }
        return true;
      }

      // How far a point lies from the nearest road's line, and that road.
      terrain::RoadSample road_near (const PlaceGround& g, const Vec3& at) {
        if (!g.roads || g.roads->empty ())
          return {};
        return g.roads->nearest (at[0], at[2]);
      }

      // How high a kept sanctuary's fire platform stands.
      constexpr float platform_height = 0.62f;

      // How much a place's stones have tumbled after so many years left.
      float ruin_after (float years) {
        return years <= 0.0f
                 ? 0.0f
                 : 0.3f + 0.7f * smoothstep (100.0f, 1400.0f, years);
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
                                      const Vec3& from,
                                      const Vec3& out,
                                      float water,
                                      float freeboard,
                                      float wanted,
                                      float least,
                                      float most) {
        Jetty jetty;
        jetty.out = out;
        jetty.deck = water + freeboard;
        // Back up onto the land until the ground stands at the deck, and
        // then out again to where it falls under it.
        Vec3 at = from;
        for (int i = 0; i < 80 && g.ground (at[0], at[2]) < jetty.deck; ++i)
          at -= out * 0.5f;
        for (int i = 0; i < 200 && g.ground (at[0], at[2]) > jetty.deck - 0.1f;
             ++i)
          at += out * 0.25f;
        // It must start on the land.
        if (g.ground (at[0] - out[0] * 0.5f, at[2] - out[2] * 0.5f) <
            jetty.deck - 0.3f)
          return std::nullopt;
        jetty.root = Vec3 (at[0], jetty.deck, at[2]);
        // Out to deep water, which must stay water all the way.
        // Over the strand to the water's edge, and then over water all the
        // way.
        float length = 0.0f;
        bool wet = false;
        for (; length <= most; length += 0.5f) {
          const Vec3 p = at + out * length;
          const float depth = water - g.ground (p[0], p[2]);
          wet = wet || depth > 0.0f;
          if ((wet && depth < -0.1f) || (!wet && length > 8.0f))
            return std::nullopt;
          if (length >= least && depth >= wanted)
            break;
        }
        if (length > most)
          return std::nullopt;
        // Its sides must be water too, for a boat to lie alongside.
        const Vec3 side = left_of (out);
        for (const float s : { -2.0f, 2.0f }) {
          const Vec3 p = at + out * (length - 2.0f) + side * s;
          if (water - g.ground (p[0], p[2]) < 0.5f * wanted)
            return std::nullopt;
        }
        jetty.length = length;
        return jetty;
      }

      // The way out to the deepest water from a point on the shore: the
      // bearing along which it deepens soonest and stays water.
      std::optional<Vec3> seaward (const PlaceGround& g,
                                   const Vec3& shore,
                                   float water,
                                   bool want_sea) {
        float best = -1e30f;
        std::optional<Vec3> found;
        for (int i = 0; i < 48; ++i) {
          const Vec3 out = bearing (tau * i / 48.0f);
          float score = 0.0f;
          bool through = true;
          for (const float reach : { 8.0f, 16.0f, 26.0f, 40.0f }) {
            const Vec3 p = shore + out * reach;
            const float depth = water - g.ground (p[0], p[2]);
            if (reach >= 16.0f && depth < 0.3f)
              through = false;
            if (want_sea && reach >= 16.0f && !g.sea (p[0], p[2]))
              through = false;
            score += std::min (depth, 4.0f);
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
                                           float sea) {
        // The shore within a stone's throw of where the road ends.
        const Vec3 centre = on_ground (g, place.x_m, place.z_m);
        std::optional<Landing> best;
        float best_score = 1e30f;
        // Out to water deep enough for any boat, or on a shallow shore to
        // what the skiff needs.
        for (const auto [wanted, most] :
             { std::pair (2.3f, 34.0f), std::pair (1.2f, 45.0f) }) {
          if (best)
            break;
          for (float r = 0.0f; r <= 60.0f; r += 4.0f)
            for (int k = 0; k < (r > 0.0f ? 12 : 1); ++k) {
              const Vec3 shore = centre + bearing (tau * k / 12.0f) * r;
              // Dry, just above the water.
              const float above = g.ground (shore[0], shore[2]) - sea;
              if (above < 0.0f || above > 2.5f)
                continue;
              const std::optional<Vec3> out = seaward (g, shore, sea, true);
              if (!out)
                continue;
              const std::optional<Jetty> jetty =
                lay_jetty (g, shore, *out, sea, 1.0f, wanted, 12.0f, most);
              if (!jetty)
                continue;
              const float score = jetty->length + 0.4f * r;
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
        jetty.decay =
          place.abandoned_years_ago > 0.0f
            ? 0.25f +
                0.35f * smoothstep (15.0f, 150.0f, place.abandoned_years_ago)
            : 0.0f;
        // The skiff lies alongside near the head, on the deeper side, its
        // bow to the land, tied to the post there.
        const Vec3 side = left_of (jetty.out);
        const Vec3 head = jetty.root + jetty.out * (jetty.length - 3.2f);
        const auto deep = [&] (float s) {
          const Vec3 p = head + side * (2.0f * s);
          return sea - g.ground (p[0], p[2]);
        };
        const float s = deep (1.0f) >= deep (-1.0f) ? 1.0f : -1.0f;
        best->berth = head + side * (s * (jetty.half_width + 0.85f));
        best->berth[1] = sea;
        best->berth_forward = -jetty.out;
        best->bollard = jetty.root + jetty.out * (jetty.length - 0.9f) +
                        side * (s * (jetty.half_width - 0.12f));
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
        s.gate = Vec3 (1, 0, 0);
        // The highest ground near the planned site.
        Vec3 top = on_ground (g, place.x_m, place.z_m);
        for (float r = 1.0f; r <= 10.0f; r += 1.0f)
          for (int k = 0; k < 16; ++k) {
            const Vec3 p =
              on_ground (g, place.x_m, place.z_m) + bearing (tau * k / 16) * r;
            const Vec3 at = on_ground (g, p[0], p[2]);
            if (at[1] > top[1])
              top = at;
          }
        s.centre = top;
        s.wall_radius = random.between (6.6f, 8.2f);
        s.platform_radius = random.between (1.35f, 1.6f);
        // The footpath's last stretch: where a ring round the wall meets
        // the path, else the gentlest way down.
        float nearest = 1e30f;
        float gentlest = 1e30f;
        Vec3 gentle (1, 0, 0);
        for (int k = 0; k < 48; ++k) {
          const Vec3 way = bearing (tau * k / 48.0f);
          const Vec3 p = top + way * (s.wall_radius + 5.0f);
          const float fall = top[1] - g.ground (p[0], p[2]);
          if (fall < gentlest) {
            gentlest = fall;
            gentle = way;
          }
          const terrain::RoadSample road = road_near (g, p);
          if (road.found && road.kind == terrain::Road::Kind::Footpath &&
              std::abs (road.across) < nearest) {
            nearest = std::abs (road.across);
            s.gate = way;
          }
        }
        if (nearest > 6.0f)
          s.gate = gentle;
        // The platform's top stands over the highest ground under it.
        float under = top[1];
        for (int k = 0; k < 12; ++k) {
          const Vec3 p = top + bearing (tau * k / 12.0f) * s.platform_radius;
          under = std::max (under, g.ground (p[0], p[2]));
        }
        s.kept = place.abandoned_years_ago <= 0.0f;
        s.ruin = ruin_after (place.abandoned_years_ago);
        s.hearth = Vec3 (
          top[0], under + platform_height * (1.0f - 0.5f * s.ruin), top[2]);
        return s;
      }

      // A building's site: level, dry, open of the road, within `low` to
      // `high` metres of `near`, the levellest such nearest the middle.
      std::optional<Vec3> building_site (const PlaceGround& g,
                                         const Vec3& near,
                                         float low,
                                         float high,
                                         float room,
                                         float flat,
                                         const std::vector<Vec3>& taken) {
        std::optional<Vec3> best;
        float best_score = 1e30f;
        for (float r = low; r <= high; r += 2.0f) {
          const int around = std::max (8, static_cast<int> (tau * r / 3.0f));
          for (int k = 0; k < around; ++k) {
            const Vec3 at =
              on_ground (g,
                         near[0] + std::cos (tau * k / around) * r,
                         near[2] + std::sin (tau * k / around) * r);
            const float rough = relief (g, at, room);
            if (rough > flat || !dry_round (g, at, room + 6.0f, 1.0f))
              continue;
            const terrain::RoadSample road = road_near (g, at);
            if (road.found && std::abs (road.across) < room + 4.0f)
              continue;
            bool clear = true;
            for (const Vec3& other : taken)
              clear = clear && flat_distance (other, at) > room + 7.0f;
            if (!clear)
              continue;
            const float score = rough * 30.0f + (r - low) * 0.5f;
            if (score < best_score) {
              best_score = score;
              best = at;
            }
          }
        }
        return best;
      }

      // The mill stands on the bank of the stream leaving the lake, its
      // wheel over the water a little below the outlet; the house and
      // the barn on level ground near by, facing the road.
      std::optional<Farm> plan_farm (const PlaceGround& g,
                                     const terrain::RoadPlace& place,
                                     std::size_t index,
                                     std::vector<Jetty>& stages) {
        const Vec3 outlet = on_ground (g, place.x_m, place.z_m);
        // The stream: running water near the outlet, and the way it
        // runs.
        Vec3 flow (0, 0, 0);
        Vec3 stream = outlet;
        float fastest = 0.0f;
        for (float r = 4.0f; r <= 50.0f; r += 2.0f)
          for (int k = 0; k < 24; ++k) {
            const Vec3 p = outlet + bearing (tau * k / 24.0f) * r;
            if (depth_at (g, p[0], p[2]) < 0.15f)
              continue;
            const float speed = speed_at (g, p[0], p[2]);
            if (speed > 0.2f)
              flow += g.current (p[0], p[2]) * (1.0f / (1.0f + 0.1f * r));
            if (speed > fastest && r > 8.0f) {
              fastest = speed;
              stream = p;
            }
          }
        Farm farm;
        farm.place = index;
        farm.kept = place.abandoned_years_ago <= 0.0f;
        farm.ruined = place.abandoned_years_ago > 45.0f;
        std::vector<Vec3> taken;
        if (fastest > 0.2f) {
          const Vec3 down = level (flow);
          // Down the stream a little way, out to its bank on whichever
          // side is drier and levellest.
          for (float along = 10.0f; along <= 40.0f && taken.empty ();
               along += 3.0f) {
            const Vec3 water = outlet + down * along;
            if (depth_at (g, water[0], water[2]) < 0.1f ||
                speed_at (g, water[0], water[2]) < 0.15f)
              continue;
            for (const float s : { 1.0f, -1.0f }) {
              const Vec3 across = left_of (down) * s;
              float bank = 0.0f;
              while (bank < 12.0f &&
                     depth_at (g,
                               water[0] + across[0] * bank,
                               water[2] + across[2] * bank) > -0.25f)
                bank += 0.5f;
              if (bank >= 12.0f)
                continue;
              // The wheel stands in the water's edge; the house's middle
              // 3.2 m back from it.
              const Vec3 at = on_ground (g,
                                         water[0] + across[0] * (bank + 3.0f),
                                         water[2] + across[2] * (bank + 3.0f));
              if (relief (g, at, 3.0f) > 1.6f)
                continue;
              const terrain::RoadSample road = road_near (g, at);
              if (road.found && std::abs (road.across) < 5.0f)
                continue;
              // The wheel is on the model's -x: turned so that it faces
              // the stream.
              farm.buildings.push_back (
                { Building::Kind::mill, at, level (cross (up, -across)) });
              taken.push_back (at);
              break;
            }
          }
        }
        // The house and the barn, facing the nearest road.
        const auto facing_road = [&] (const Vec3& at) {
          const terrain::RoadSample road = road_near (g, at);
          if (!road.found)
            return level (outlet - at);
          const Vec3 dir (road.direction_x, 0.0f, road.direction_z);
          // Toward the line: against the side it lies on.
          return level (left_of (dir) * (road.across > 0.0f ? -1.0f : 1.0f));
        };
        if (const std::optional<Vec3> house =
              building_site (g, outlet, 20.0f, 70.0f, 5.0f, 0.9f, taken)) {
          farm.buildings.push_back (
            { Building::Kind::house, *house, facing_road (*house) });
          taken.push_back (*house);
          if (const std::optional<Vec3> barn =
                building_site (g, *house, 13.0f, 30.0f, 5.0f, 1.0f, taken)) {
            // The barn's end to the house.
            farm.buildings.push_back (
              { Building::Kind::barn, *barn, level (*house - *barn) });
          }
        }
        if (farm.buildings.empty ())
          return std::nullopt;

        // A landing stage on the lake, clear of the outlet's pull, to fish
        // from.
        const float lake = [&] {
          for (float r = 6.0f; r <= 30.0f; r += 2.0f)
            for (int k = 0; k < 24; ++k) {
              const Vec3 p = outlet + bearing (tau * k / 24.0f) * r;
              if (depth_at (g, p[0], p[2]) > 0.6f &&
                  speed_at (g, p[0], p[2]) < 0.05f)
                return g.water (p[0], p[2]);
            }
          return g.water (outlet[0], outlet[2]);
        }();
        std::optional<Jetty> stage;
        float best = 1e30f;
        for (float r = 14.0f; r <= 70.0f && !stage; r += 4.0f)
          for (int k = 0; k < 24; ++k) {
            const Vec3 shore = outlet + bearing (tau * k / 24.0f) * r;
            const float d = depth_at (g, shore[0], shore[2]);
            if (d > 0.1f || d < -0.8f)
              continue;
            const std::optional<Vec3> out = seaward (g, shore, lake, false);
            if (!out)
              continue;
            const Vec3 tip = shore + *out * 10.0f;
            if (speed_at (g, tip[0], tip[2]) > 0.06f ||
                std::abs (g.water (tip[0], tip[2]) - lake) > 0.15f)
              continue;
            std::optional<Jetty> laid =
              lay_jetty (g, shore, *out, lake, 0.45f, 1.0f, 4.0f, 11.0f);
            if (!laid)
              continue;
            bool clear = true;
            for (const Vec3& other : taken)
              clear = clear && flat_distance (other, laid->root) > 9.0f;
            if (clear && laid->length < best) {
              best = laid->length;
              laid->half_width = 0.75f;
              laid->decay = farm.ruined ? 0.35f : 0.0f;
              stage = laid;
            }
          }
        if (stage)
          stages.push_back (*stage);
        return farm;
      }

      // Kept clear of trees and loose rock: round each building, the
      // sanctuary's enclosure, and the jetty's root.
      void clear_round (PlacesPlan& plan) {
        if (plan.landing)
          plan.clearings.push_back ({ plan.landing->jetty.root, 7.0f });
        for (const Sanctuary& s : plan.sanctuaries)
          plan.clearings.push_back ({ s.centre, s.wall_radius + 2.5f });
        for (const Farm& farm : plan.farms)
          for (const Building& b : farm.buildings)
            plan.clearings.push_back (
              { b.at, b.kind == Building::Kind::mill ? 7.5f : 9.5f });
        for (const Jetty& stage : plan.stages)
          plan.clearings.push_back ({ stage.root, 4.0f });
      }
    }
  }

  PlacesPlan plan_places (const terrain::RoadNetwork& network,
                          const PlaceGround& ground,
                          std::uint32_t seed) {
    using namespace places_parts;
    PlacesPlan plan;
    plan.period_x = ground.period_x;
    plan.period_z = ground.period_z;
    const float sea = ground.sea_level;
    std::vector<std::size_t> sanctuary_of (network.places.size (),
                                           network.places.size ());
    for (std::size_t i = 0; i < network.places.size (); ++i) {
      const terrain::RoadPlace& place = network.places[i];
      Random random (seed ^ static_cast<std::uint32_t> (i * 0x9e3779b9u));
      using Kind = terrain::RoadPlace::Kind;
      const char* what = "the trailhead";
      switch (place.kind) {
      case Kind::Trailhead:
        break;
      case Kind::Landing:
        what = "a landing";
        if (!plan.landing)
          plan.landing = plan_landing (ground, place, i, sea);
        break;
      case Kind::LakeOutlet:
        what = "a lake outlet";
        if (std::optional<Farm> farm =
              plan_farm (ground, place, i, plan.stages))
          plan.farms.push_back (std::move (*farm));
        break;
      case Kind::Sanctuary:
        what = "a sanctuary";
        sanctuary_of[i] = plan.sanctuaries.size ();
        plan.sanctuaries.push_back (plan_sanctuary (ground, place, i, random));
        break;
      }
      std::cerr << "moppe: places: " << what << " at " << place.x_m << ' '
                << place.z_m << ", " << place.height_m << " m, founded "
                << place.founded_years_ago << " years ago";
      if (place.abandoned_years_ago > 0.0f)
        std::cerr << ", left " << place.abandoned_years_ago << " years ago";
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
      std::cerr << "moppe: places: the jetty runs " << j.length
                << " m out from " << j.root[0] << ' ' << j.root[1] << ' '
                << j.root[2] << " toward " << j.out[0] << ' ' << j.out[2]
                << '\n';
    }
    for (const Sanctuary& s : plan.sanctuaries)
      std::cerr << "moppe: places: an enclosure at " << s.centre[0] << ' '
                << s.centre[1] << ' ' << s.centre[2] << ", its gate toward "
                << s.gate[0] << ' ' << s.gate[2] << ", "
                << (s.kept ? "kept" : "ruined") << '\n';
    for (const Farm& farm : plan.farms)
      for (const Building& b : farm.buildings)
        std::cerr << "moppe: places: a "
                  << (b.kind == Building::Kind::mill    ? "mill"
                      : b.kind == Building::Kind::house ? "house"
                                                        : "barn")
                  << " at " << b.at[0] << ' ' << b.at[1] << ' ' << b.at[2]
                  << '\n';
    clear_round (plan);
    std::cerr << "moppe: places: " << (plan.landing ? "a jetty, " : "")
              << plan.sanctuaries.size () << " sanctuaries, "
              << network.sightlines.size () << " sightlines, "
              << plan.farms.size () << " farms, " << plan.stages.size ()
              << " landing stages" << std::endl;
    return plan;
  }

  bool cleared (const PlacesPlan& plan, const Vec3& at) {
    using namespace places_parts;
    for (const PlacesPlan::Clearing& c : plan.clearings) {
      float dx = at[0] - c.centre[0];
      float dz = at[2] - c.centre[2];
      if (plan.period_x > 0.0f)
        dx -= plan.period_x * std::round (dx / plan.period_x);
      if (plan.period_z > 0.0f)
        dz -= plan.period_z * std::round (dz / plan.period_z);
      if (dx * dx + dz * dz < c.radius * c.radius)
        return true;
    }
    return false;
  }

  void place_camps (
    PlacesPlan& plan,
    const terrain::RoadNetwork& network,
    const PlaceGround& ground,
    const std::function<std::optional<Vec3> (const Vec3&)>& hammock_near) {
    using namespace places_parts;
    plan.hearths.clear ();
    for (const terrain::RoadPlace& place : network.places) {
      if (place.kind == terrain::RoadPlace::Kind::Trailhead)
        continue;
      const Vec3 at = on_ground (ground, place.x_m, place.z_m);
      const std::optional<Vec3> middle = hammock_near (at);
      if (!middle)
        continue;
      // The ring a few steps off the hammock, on level dry ground.
      for (int k = 0; k < 12; ++k) {
        const Vec3 p =
          on_ground (ground,
                     (*middle)[0] + std::cos (tau * k / 12) * 3.0f,
                     (*middle)[2] + std::sin (tau * k / 12) * 3.0f);
        if (relief (ground, p, 0.7f) > 0.3f ||
            !dry_round (ground, p, 1.0f, 0.4f))
          continue;
        const terrain::RoadSample road = road_near (ground, p);
        if (road.found && std::abs (road.across) < 4.0f)
          continue;
        plan.hearths.push_back (p);
        std::cerr << "moppe: places: a camp at " << p[0] << ' ' << p[2] << ", "
                  << std::hypot (p[0] - place.x_m, p[2] - place.z_m)
                  << " m from the place" << std::endl;
        break;
      }
    }
  }

  std::optional<std::size_t> sanctuary_at_hand (const PlacesPlan& plan,
                                                const Vec3& feet) {
    for (std::size_t i = 0; i < plan.sanctuaries.size (); ++i) {
      const Sanctuary& s = plan.sanctuaries[i];
      float dx = feet[0] - s.hearth[0];
      float dz = feet[2] - s.hearth[2];
      if (plan.period_x > 0.0f)
        dx -= plan.period_x * std::round (dx / plan.period_x);
      if (plan.period_z > 0.0f)
        dz -= plan.period_z * std::round (dz / plan.period_z);
      const float reach = s.platform_radius + 1.6f;
      if (dx * dx + dz * dz < reach * reach)
        return i;
    }
    return std::nullopt;
  }

  std::optional<Vec3> hearth_at_hand (const PlacesPlan& plan,
                                      const Vec3& feet) {
    for (const Vec3& hearth : plan.hearths) {
      float dx = feet[0] - hearth[0];
      float dz = feet[2] - hearth[2];
      if (plan.period_x > 0.0f)
        dx -= plan.period_x * std::round (dx / plan.period_x);
      if (plan.period_z > 0.0f)
        dz -= plan.period_z * std::round (dz / plan.period_z);
      if (dx * dx + dz * dz < 2.2f * 2.2f)
        return Vec3 (feet[0] - dx, hearth[1], feet[2] - dz);
    }
    return std::nullopt;
  }

  float beacon_burn (double lit, double out, double now) {
    using namespace places_parts;
    if (lit > out)
      return smoothstep (0.0f, 6.0f, static_cast<float> (now - lit));
    return (1.0f - smoothstep (0.0f, 2.5f, static_cast<float> (now - out))) *
           smoothstep (0.0f, 6.0f, static_cast<float> (out - lit));
  }
}

namespace moppe::game {
  namespace {
    namespace places_parts {
      // One unit box, centred at `centre`, its axes `x`, `y`, `z` scaled
      // to `size`.
      void box (render::DrawList& dl,
                const Vec3& centre,
                const Vec3& x,
                const Vec3& y,
                const Vec3& z,
                const Vec3& size) {
        dl.push ();
        dl.mult (Mat4::basis (x, y, z, centre));
        dl.scale (size);
        dl.cube (1.0f);
        dl.pop ();
      }

      void timber (render::DrawList& dl, Random& random, float tar) {
        const float grey = random.between (0.36f, 0.46f) * (1.0f - 0.55f * tar);
        dl.color (grey * 1.02f, grey * 0.93f, grey * 0.82f);
      }

      // Planks across on two stringers, on pairs of posts, from the root
      // out; decay drops and tilts planks and leans posts.
      void build_jetty (render::DrawList& dl,
                        const Jetty& jetty,
                        const PlaceGround& g,
                        std::uint32_t seed,
                        std::vector<mov::Trunk>& colliders) {
        Random random (seed);
        const Vec3 out = jetty.out;
        const Vec3 side = left_of (out);
        const float hw = jetty.half_width;
        // Posts in pairs every 2.4 m, sunk into the bed.
        for (float along = 0.6f; along <= jetty.length; along += 2.4f)
          for (const float s : { -1.0f, 1.0f }) {
            Vec3 top = jetty.root + out * along + side * (s * (hw - 0.06f));
            top[1] = jetty.deck - 0.05f;
            const float bed = g.ground (top[0], top[2]) - 0.6f;
            const float lean = jetty.decay * random.between (-0.08f, 0.08f);
            const Vec3 axis = normalized (up + side * lean);
            const float tall = top[1] - bed + 0.15f;
            timber (dl, random, 0.8f);
            box (dl,
                 top - axis * (tall * 0.5f - 0.15f),
                 side,
                 axis,
                 cross (side, axis),
                 Vec3 (0.16f, tall, 0.16f));
          }
        // The stringers.
        for (const float s : { -1.0f, 1.0f }) {
          timber (dl, random, 0.6f);
          box (dl,
               jetty.root + out * (jetty.length * 0.5f) +
                 side * (s * (hw - 0.12f)) + up * (-0.13f),
               side,
               up,
               out,
               Vec3 (0.1f, 0.16f, jetty.length));
        }
        // The planks.
        for (float along = 0.1f; along < jetty.length; along += 0.21f) {
          if (random.unit () < jetty.decay * 0.45f)
            continue;
          const float sag = jetty.decay * random.between (-0.06f, 0.06f);
          const Vec3 across = normalized (side + up * sag);
          timber (dl, random, 0.0f);
          box (dl,
               jetty.root + out * along + up * -0.025f +
                 side * random.between (-0.03f, 0.03f),
               across,
               cross (out, across),
               out,
               Vec3 (2.0f * hw + random.between (-0.04f, 0.06f), 0.05f, 0.18f));
        }
        // The deck to stand on: capsules lying along it, their tops at the
        // planks'.
        constexpr float radius = 0.3f;
        for (const float s : { -0.55f, 0.0f, 0.55f }) {
          mov::Trunk deck;
          deck.root = jetty.root + side * (s * hw) - out * 0.2f + up * -radius;
          deck.axis = out;
          deck.height = jetty.length + 0.2f;
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
        const Vec3 centre (s.centre[0], 0.0f, s.centre[2]);
        const int courses = s.ruin > 0.0f ? (s.ruin > 0.7f ? 1 : 2) : 3;
        const float r = s.wall_radius;
        const float gate = std::atan2 (static_cast<float> (s.gate[2]),
                                       static_cast<float> (s.gate[0]));
        const float half_gap = 1.1f / r;
        const auto in_gap = [&] (float a) {
          float d = std::remainder (a - gate, tau);
          return std::abs (d) < half_gap;
        };
        const int around = static_cast<int> (tau * r / 0.62f);
        const float course_h = 0.27f;
        for (int k = 0; k < courses; ++k)
          for (int i = 0; i < around; ++i) {
            const float a =
              tau * (i + 0.5f * (k % 2) + random.between (-0.15f, 0.15f)) /
              around;
            if (in_gap (a))
              continue;
            const Vec3 way = bearing (a);
            const Vec3 at = centre + way * (r + random.between (-0.08f, 0.08f));
            const float ground = g.ground (at[0], at[2]);
            const float radius =
              random.between (0.32f, 0.42f) * (1.0f - 0.12f * k);
            const float thick =
              k == 0 ? course_h + 0.1f : random.between (0.2f, 0.28f);
            build_fieldstone (dl,
                              seed * 31u + k * 977u + i,
                              at + up * (ground - 0.1f + course_h * k),
                              radius,
                              thick,
                              a + random.between (-0.4f, 0.4f),
                              Vec3 (random.between (-0.08f, 0.08f),
                                    0.0f,
                                    random.between (-0.08f, 0.08f)));
          }
        // What has tumbled lies about the foot of it, inside and out.
        const int fallen = static_cast<int> (around * 1.4f * s.ruin);
        for (int i = 0; i < fallen; ++i) {
          const float a = random.between (0.0f, tau);
          if (in_gap (a))
            continue;
          const float off = random.between (-1.6f, 1.6f);
          const Vec3 at = centre + bearing (a) * (r + off);
          build_fieldstone (dl,
                            seed * 53u + i,
                            at + up * (g.ground (at[0], at[2]) - 0.08f),
                            random.between (0.2f, 0.36f),
                            random.between (0.15f, 0.24f),
                            random.between (0.0f, tau),
                            Vec3 (random.between (-0.3f, 0.3f),
                                  0.0f,
                                  random.between (-0.3f, 0.3f)));
        }
        // Two taller stones stand either side of the way in.
        if (s.ruin < 0.7f)
          for (const float side : { -1.0f, 1.0f }) {
            const Vec3 at =
              centre + bearing (gate + side * half_gap * 1.25f) * r;
            build_fieldstone (dl,
                              seed * 7u + (side > 0 ? 1u : 2u),
                              at + up * (g.ground (at[0], at[2]) - 0.15f),
                              0.36f,
                              1.25f * (1.0f - 0.4f * s.ruin),
                              gate,
                              Vec3 ());
          }
        // A kept wall stops a body, which must go round by the gap or
        // jump it; a tumbled one is rubble to step over.
        const float tall = courses >= 3 ? 1.35f : course_h * courses + 0.05f;
        const int posts = static_cast<int> (tau * r / 0.9f);
        for (int i = 0; i < posts; ++i) {
          const float a = tau * (i + 0.5f) / posts;
          if (in_gap (a))
            continue;
          mov::Trunk stone;
          const Vec3 at = centre + bearing (a) * r;
          stone.root = Vec3 (at[0], g.ground (at[0], at[2]) - 0.1f, at[2]);
          stone.axis = up;
          stone.height = std::max (0.05f, tall - 0.35f);
          stone.radius = 0.35f;
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
        const float top = s.hearth[1];
        const float R = s.platform_radius;
        const Vec3 middle (s.hearth[0], 0.0f, s.hearth[2]);
        const int around = static_cast<int> (tau * R / 0.5f);
        for (int k = 0; k < 3; ++k) {
          const float bottom = top - 0.22f * (3 - k);
          for (int i = 0; i < around; ++i) {
            const float a =
              tau * (i + 0.5f * (k % 2) + random.between (-0.12f, 0.12f)) /
              around;
            const Vec3 at = middle + bearing (a) * (R - 0.25f);
            const float ground = g.ground (at[0], at[2]);
            // The lowest course reaches down to the ground, wherever
            // that is under it.
            const float from =
              k == 0 ? std::min (bottom, ground - 0.1f) : bottom;
            if (top - 0.22f * (2 - k) < ground - 0.05f ||
                random.unit () < s.ruin * (0.15f + 0.2f * k))
              continue;
            build_fieldstone (dl,
                              seed * 17u + k * 401u + i,
                              Vec3 (at[0], from, at[2]),
                              0.34f,
                              top - 0.22f * (2 - k) - from + 0.02f,
                              a + random.between (-0.3f, 0.3f),
                              Vec3 (random.between (-0.05f, 0.05f),
                                    0.0f,
                                    random.between (-0.05f, 0.05f)));
          }
        }
        // The slabs over the top.
        for (int ring = 0; ring < 2; ++ring) {
          const int count = ring == 0 ? 1 : 6;
          for (int i = 0; i < count; ++i) {
            const float a = tau * (i + random.between (-0.1f, 0.1f)) / count;
            if (random.unit () < 0.6f * s.ruin)
              continue;
            const Vec3 at =
              middle + bearing (a) * (ring * (R * 0.55f)) +
              bearing (random.between (0.0f, tau)) * (0.25f * s.ruin);
            build_fieldstone (dl,
                              seed * 29u + ring * 61u + i,
                              Vec3 (at[0], top - 0.12f, at[2]),
                              ring == 0 ? 0.55f : 0.5f,
                              0.14f,
                              random.between (0.0f, tau),
                              Vec3 (random.between (-0.03f, 0.03f),
                                    0.0f,
                                    random.between (-0.03f, 0.03f)));
          }
        }
        // What has fallen off it lies round its foot.
        for (int i = 0; i < static_cast<int> (14 * s.ruin); ++i) {
          const Vec3 at = middle + bearing (random.between (0.0f, tau)) *
                                     (R + random.between (0.1f, 1.4f));
          build_fieldstone (dl,
                            seed * 71u + i,
                            at + up * (g.ground (at[0], at[2]) - 0.06f),
                            random.between (0.2f, 0.34f),
                            random.between (0.14f, 0.22f),
                            random.between (0.0f, tau),
                            Vec3 (random.between (-0.3f, 0.3f),
                                  0.0f,
                                  random.between (-0.3f, 0.3f)));
        }
        // Blackened where the fires stood.
        dl.begin (render::Prim::Triangles);
        dl.color (0.08f, 0.07f, 0.06f);
        dl.normal (up);
        constexpr int rim = 16;
        for (int i = 0; i < rim; ++i) {
          const float jitter0 = 0.75f + 0.2f * std::sin (i * 2.7f + seed);
          const float jitter1 =
            0.75f + 0.2f * std::sin ((i + 1) % rim * 2.7f + seed);
          dl.vertex (Vec3 (middle[0], top + 0.035f, middle[2]));
          dl.vertex (middle + bearing (tau * (i + 1) / rim) * jitter1 +
                     up * (top + 0.035f));
          dl.vertex (middle + bearing (tau * i / rim) * jitter0 +
                     up * (top + 0.035f));
        }
        dl.end ();
        // It stands up out of the summit, too high to step onto but solid
        // to jump up on: upright capsules packed under its top.
        constexpr float post = 0.35f;
        for (float x = -R + post; x <= R - post + 0.01f; x += 0.6f)
          for (float z = -R + post; z <= R - post + 0.01f; z += 0.6f) {
            if (x * x + z * z > (R - post) * (R - post))
              continue;
            mov::Trunk drum;
            drum.root = Vec3 (middle[0] + x,
                              g.ground (middle[0] + x, middle[2] + z) - 0.2f,
                              middle[2] + z);
            drum.axis = up;
            drum.radius = post;
            drum.height = std::max (0.01f, top - post - drum.root[1]);
            colliders.push_back (drum);
          }

        // Flat stones laid along the way in from the platform out through
        // the gate and on down the last of the path.
        const Vec3 gate = s.gate;
        const Vec3 side = left_of (gate);
        for (float along = R + 0.6f; along < s.wall_radius + 14.0f;
             along += 0.85f) {
          if (along > s.wall_radius + 2.0f &&
              random.unit () < 0.25f + s.ruin * 0.3f)
            continue;
          for (const float w : { -0.38f, 0.38f }) {
            if (random.unit () < 0.15f)
              continue;
            const Vec3 at = middle + gate * along +
                            side * (w + random.between (-0.08f, 0.08f));
            build_fieldstone (dl,
                              seed * 41u +
                                static_cast<std::uint32_t> (along * 10) +
                                (w > 0 ? 1u : 0u),
                              at + up * (g.ground (at[0], at[2]) - 0.06f),
                              random.between (0.3f, 0.4f),
                              0.1f,
                              random.between (0.0f, tau),
                              Vec3 ());
          }
        }
      }

      Mat4 building_frame (const Building& b) {
        const Vec3 forward = level (b.forward);
        return Mat4::translation (b.at) *
               Mat4::basis (left_of (forward), up, forward);
      }

      // A building's walls stop bodies: capsules round inside its
      // footprint.
      void building_colliders (const Building& b,
                               bool ruined,
                               std::vector<mov::Trunk>& colliders) {
        float hx = 2.2f, hz = 2.5f, tall = 3.4f;
        if (b.kind == Building::Kind::house)
          hx = 3.6f, hz = 2.6f, tall = 3.0f;
        else if (b.kind == Building::Kind::barn)
          hx = 4.5f, hz = 3.0f, tall = 3.8f;
        if (ruined)
          tall = 0.4f;
        const Mat4 frame = building_frame (b);
        constexpr float radius = 0.55f;
        const auto post = [&] (float x, float z) {
          mov::Trunk wall;
          wall.root = frame.transform_point (Vec3 (x, -0.5f, z));
          wall.axis = up;
          wall.height = tall;
          wall.radius = radius;
          colliders.push_back (wall);
        };
        for (float x = -hx + radius; x <= hx - radius + 0.01f; x += 0.9f)
          for (const float z : { -hz + radius, hz - radius })
            post (x, z);
        for (float z = -hz + radius + 0.9f; z < hz - radius; z += 0.9f)
          for (const float x : { -hx + radius, hx - radius })
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
      // bars, coursed stones), metres; its middle tier's are twice that.
      constexpr float building_detail = 0.05f;
    }
  }

  Vec3 PlacesLandscape::near_copy (const Vec3& at, const Vec3& camera) const {
    Vec3 out = at;
    if (m_period_x > 0.0f)
      out[0] += m_period_x * std::round ((camera[0] - at[0]) / m_period_x);
    if (m_period_z > 0.0f)
      out[2] += m_period_z * std::round ((camera[2] - at[2]) / m_period_z);
    return out;
  }

  void PlacesLandscape::rebuild (render::Renderer& renderer,
                                 const PlacesPlan& plan,
                                 const PlaceGround& ground) {
    using namespace places_parts;
    m_pieces.clear ();
    m_colliders.clear ();
    m_beacons.clear ();
    m_period_x = plan.period_x;
    m_period_z = plan.period_z;
    const auto bake =
      [&] (const Vec3& origin, float reach, const auto& record) {
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
      bake (jetty.root, 2500.0f, [&] (render::DrawList& dl) {
        build_jetty (dl, jetty, ground, seed++, m_colliders);
        // The post the painter is tied to.
        Random post (seed);
        timber (dl, post, 0.7f);
        box (dl,
             plan.landing->bollard + up * 0.25f,
             left_of (jetty.out),
             up,
             jetty.out,
             Vec3 (0.18f, 0.6f, 0.18f));
      });
    }
    for (const Jetty& stage : plan.stages)
      bake (stage.root, 1800.0f, [&] (render::DrawList& dl) {
        build_jetty (dl, stage, ground, seed++, m_colliders);
      });
    for (const Sanctuary& s : plan.sanctuaries) {
      m_beacons.push_back (s.hearth);
      bake (s.centre, 4000.0f, [&] (render::DrawList& dl) {
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
        piece.reach = 3000.0f;
        m_pieces.push_back (std::move (piece));
        building_colliders (b, roofless, m_colliders);
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
          wheel.origin = frame.transform_point (farm_model::mill_axle);
          wheel.reach = 1500.0f;
          wheel.wheel = farm.kept;
          wheel.turning = farm.kept;
          wheel.axis = frame.transform_vector (Vec3 (1, 0, 0));
          wheel.frame = frame;
          m_pieces.push_back (std::move (wheel));
        }
      }
  }

  const render::Mesh& PlacesLandscape::tier (const Piece& piece,
                                             float distance,
                                             float focal) const {
    // A part `detail` metres across spans detail * focal / distance
    // pixels; the near tier stands while its finest span a pixel, the
    // middle while its own (twice as coarse) do.
    if (piece.detail <= 0.0f || !piece.coarser[0] || focal <= 0.0f)
      return *piece.mesh;
    const float pixels = piece.detail * focal / std::max (distance, 0.5f);
    if (pixels >= 1.0f)
      return *piece.mesh;
    return 2.0f * pixels >= 1.0f ? *piece.coarser[0] : *piece.coarser[1];
  }

  void PlacesLandscape::draw (render::Renderer& renderer,
                              const Vec3& camera,
                              float time,
                              std::uint64_t motion_base) const {
    std::uint64_t id = motion_base;
    const float focal = renderer.scene_focal_pixels ();
    for (const Piece& piece : m_pieces) {
      ++id;
      const Vec3 at = near_copy (piece.origin, camera);
      if (length2 (at - camera) > piece.reach * piece.reach)
        continue;
      const Vec3 shift = at - piece.origin;
      const render::Mesh& mesh = tier (piece, length (at - camera), focal);
      if (piece.frame) {
        // The wheel, in its own frame about the axle, turning.
        const float angle = piece.turning ? -0.9f * time : 0.4f;
        Mat4 frame = *piece.frame;
        const Mat4 model = Mat4::translation (shift) * frame *
                           Mat4::translation (farm_model::mill_axle) *
                           Mat4::rotation (angle * u::rad, Vec3 (1, 0, 0));
        renderer.draw_mesh (mesh, model, id);
        continue;
      }
      renderer.draw_mesh (mesh, Mat4::translation (shift), id);
    }
  }

  void PlacesLandscape::cast_shadows (render::Renderer& renderer,
                                      const Vec3& camera) const {
    for (const Piece& piece : m_pieces) {
      const Vec3 at = near_copy (piece.origin, camera);
      if (piece.frame || length2 (at - camera) > 160.0f * 160.0f)
        continue;
      // The sun's near texels are some eight centimetres: a building
      // shades the ground by its massing alone.
      renderer.draw_mesh (piece.coarser[1] ? *piece.coarser[1] : *piece.mesh,
                          Mat4::translation (at - piece.origin));
    }
  }

  void PlacesLandscape::draw_beacons (render::DrawList& dl,
                                      const Vec3& camera,
                                      const std::vector<float>& burn) const {
    for (std::size_t i = 0; i < m_beacons.size (); ++i) {
      const Vec3 at = near_copy (m_beacons[i], camera);
      if (length2 (at - camera) > 300.0f * 300.0f)
        continue;
      draw_hearth (dl, at, i < burn.size () ? burn[i] : 0.0f);
    }
  }

  void PlacesLandscape::draw_beacon_fires (render::Renderer& renderer,
                                           const Vec3& camera,
                                           const std::vector<float>& burn,
                                           float daylight,
                                           float time) const {
    for (std::size_t i = 0; i < m_beacons.size () && i < burn.size (); ++i)
      if (burn[i] > 0.01f)
        draw_beacon (renderer,
                     near_copy (m_beacons[i], camera),
                     camera,
                     burn[i],
                     daylight,
                     time);
  }
}
