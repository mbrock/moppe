#include <moppe/game/camp.hh>

#include <moppe/game/fishing.hh>
#include <moppe/game/hens.hh>
#include <moppe/game/sprites.hh>
#include <moppe/gfx/signal.hh>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace moppe::game {
  // Named, not anonymous: the game builds as one unit, where another
  // file's helpers of the same name would collide.
  namespace camp_parts {
    constexpr radians_t tau = 6.2831853f * u::rad;
    constexpr radians_t pi = 3.14159265f * u::rad;
    const direction_t world_up = unit_direction (Vec3 (0, 1, 0));
    constexpr proportion_t whole = 1.0f * proportion[one];
    using span_coordinate_t = quantity<span_coordinate[one], float>;

    // A loaded hammock's lowest point sits this far above the higher of
    // its trees' roots, and sags by this much of its span; empty it hangs
    // shallower. Its cloth takes the middle of the span and lines the rest.
    constexpr meters_t seat_height = 0.60f * u::m;
    constexpr proportion_t loaded_sag = 0.20f * proportion[one];
    constexpr proportion_t empty_sag = 0.12f * proportion[one];
    constexpr proportion_t cloth_from = 0.15f * proportion[one];
    constexpr proportion_t cloth_to = 0.85f * proportion[one];

    // What a tree must be to carry a strap, and how level its roots must
    // be with its partner's.
    constexpr meters_t least_height = 5.0f * u::m;
    constexpr meters_t least_girth = 0.07f * u::m;
    constexpr meters_t most_girth = 0.6f * u::m;
    constexpr meters_t most_step = 0.7f * u::m;
    constexpr meters_t highest_strap = 2.5f * u::m;
    // Its branches must start above the strap and the head of whoever
    // sits up in the hammock; and a tree that is mostly crown -- a spruce
    // -- hangs its boughs wide and low, so it must be bare much higher.
    constexpr meters_t least_clear = 2.4f * u::m;
    constexpr meters_t least_clear_under_deep_crown = 4.5f * u::m;
    constexpr proportion_t deep_crown = 0.6f * proportion[one];
    // Nothing else may stand this near the line between them.
    constexpr meters_t clear_way = 0.55f * u::m;

    displacement_t flat (displacement_t v) {
      v.numerical_value_ref_in (u::m)[1] = 0.0f;
      return v;
    }

    template <typename Q>
    Q mix (Q a, Q b, proportion_t t) {
      return a + (b - a) * t;
    }

    // The hung hammock's geometry: a parabola between the two points where
    // its lines leave the bark, swung about the line between them, with
    // the cloth spread across it and gathered to a point at each end.
    struct Shape {
      position_t hang[2];
      displacement_t run;
      direction_t down, side, up;
      meters_t sag = 0.0f * u::m;
      meters_t half_width = 0.0f * u::m;
      meters_t lift = 0.0f * u::m;

      // A point on the sagging centre line, 0 at the foot's tree to 1 at
      // the head's, and the line's direction there, headward.
      position_t centre (proportion_t t) const {
        return position (position_value (hang[0]) +
                         displacement_value (run) * t.numerical_value_in (one) +
                         direction_value (down) *
                           meters_value (4.0f * sag * t * (whole - t)));
      }
      direction_t tangent (proportion_t t) const {
        return unit_direction (
          normalized (displacement_value (run) +
                      direction_value (down) *
                        meters_value (4.0f * sag * (whole - 2.0f * t))));
      }
      // The way a body lying at `t` faces: square to the line, upward.
      direction_t normal (proportion_t t) const {
        const direction_t along = tangent (t);
        return unit_direction (
          normalized (direction_value (up) -
                      direction_value (along) *
                        dot (direction_value (up), direction_value (along))));
      }
      // The cloth: `lengthwise` along it from foot to head, `across` from
      // -1 to 1 across its half width. Its edges run
      // straighter than its middle, so they rise.
      position_t cloth (proportion_t lengthwise,
                        span_coordinate_t across_at) const {
        const float across = across_at.numerical_value_in (one);
        const proportion_t t =
          cloth_from + (cloth_to - cloth_from) * lengthwise;
        const proportion_t bell =
          std::sin (radians_value (pi * lengthwise)) * proportion[one];
        return position (
          position_value (centre (t)) +
          direction_value (side) *
            meters_value (
              half_width *
              std::pow (std::max (bell.numerical_value_in (one), 0.0f), 0.55f) *
              across) +
          direction_value (up) * meters_value (lift * bell * across * across));
      }
    };

    Shape
    shape_of (const HammockSite& site, proportion_t load, radians_t swing) {
      Shape s;
      const direction_t along = hammock_along (site);
      s.hang[0] =
        position (position_value (site.strap[0]) +
                  direction_value (along) * meters_value (site.girth[0]));
      s.hang[1] =
        position (position_value (site.strap[1]) -
                  direction_value (along) * meters_value (site.girth[1]));
      s.run =
        displacement (position_value (s.hang[1]) - position_value (s.hang[0]));
      const direction_t axis = direction_of (s.run);
      const direction_t up = unit_direction (
        normalized (direction_value (world_up) -
                    direction_value (axis) * dot (direction_value (world_up),
                                                  direction_value (axis))));
      const direction_t side =
        unit_direction (cross (direction_value (axis), direction_value (up)));
      s.down = unit_direction (
        direction_value (side) * std::sin (radians_value (swing)) -
        direction_value (up) * std::cos (radians_value (swing)));
      s.up = unit_direction (-direction_value (s.down));
      s.side = unit_direction (
        direction_value (side) * std::cos (radians_value (swing)) +
        direction_value (up) * std::sin (radians_value (swing)));
      const meters_t span = length (displacement_value (s.run)) * u::m;
      s.sag = span * mix (empty_sag, loaded_sag, load);
      s.half_width = mix (0.34f * u::m, 0.54f * u::m, load);
      s.lift = mix (0.08f * u::m, 0.30f * u::m, load);
      return s;
    }

    bool same_tree (const mov::Trunk& a, const mov::Trunk& b) {
      return a.root[0] == b.root[0] && a.root[2] == b.root[2];
    }

    bool carries (const mov::Trunk& trunk) {
      const meters_t height = trunk.height * u::m;
      const meters_t radius = trunk.radius * u::m;
      const meters_t clear = trunk.clear * u::m;
      if (height < least_height || radius < least_girth || radius > most_girth)
        return false;
      const bool deep = height - clear > deep_crown * height;
      return clear >= (deep ? least_clear_under_deep_crown : least_clear);
    }

    // Whether a hammock hangs between `a` and `b`, with `others` the trees
    // that might stand in its way; fills `site` if so.
    bool fits (const mov::Trunk& a,
               const mov::Trunk& b,
               std::span<const mov::Trunk> others,
               const terrain::ElevationField& ground,
               HammockSite& site) {
      if (!carries (a) || !carries (b))
        return false;
      const displacement_t between = flat (displacement (b.root - a.root));
      const meters_t apart = length (displacement_value (between)) * u::m;
      const meters_t span = apart - a.radius * u::m - b.radius * u::m;
      if (span < hammock_least_span || span > hammock_most_span)
        return false;
      if (std::fabs (a.root[1] - b.root[1]) * u::m > most_step)
        return false;
      const meters_t top = std::max (a.root[1], b.root[1]) * u::m;
      const meters_t strap = top + seat_height + loaded_sag * span;
      if (strap - std::min (a.root[1], b.root[1]) * u::m > highest_strap)
        return false;
      const direction_t along = unit_direction (displacement_value (between) *
                                                (1.0f / meters_value (apart)));
      // The ground must not rise into the cloth.
      for (int i = 1; i < 6; ++i) {
        const position_t at =
          position (a.root + displacement_value (between) * (i / 6.0f));
        if (ground (plan (at)) >
            terrain::surface_elevation_point (top + 0.2f * u::m))
          return false;
      }
      for (const mov::Trunk& other : others) {
        if (same_tree (other, a) || same_tree (other, b) ||
            other.height * u::m < 0.8f * u::m)
          continue;
        const displacement_t to = flat (displacement (other.root - a.root));
        const meters_t reach =
          dot (displacement_value (to), direction_value (along)) * u::m;
        if (reach < 0.0f * u::m || reach > apart)
          continue;
        const meters_t off =
          length (displacement_value (to) -
                  direction_value (along) * meters_value (reach)) *
          u::m;
        if (off < clear_way + other.radius * u::m)
          return false;
      }
      // The head lies toward the higher root, looking out over the feet.
      const bool swap = a.root[1] > b.root[1];
      const mov::Trunk& foot = swap ? b : a;
      const mov::Trunk& head = swap ? a : b;
      // Each strap goes round its trunk where the trunk, leaning as it
      // does, passes the strap's height; a trunk is a little slimmer there
      // than at its foot.
      const auto up_trunk = [strap] (const mov::Trunk& trunk) {
        return trunk.root +
               trunk.axis *
                 meters_value ((strap - trunk.root[1] * u::m) /
                               std::max (0.5f, float (trunk.axis[1])));
      };
      site.strap[0] = position (up_trunk (foot));
      site.strap[1] = position (up_trunk (head));
      site.girth[0] = 0.9f * foot.radius * u::m;
      site.girth[1] = 0.9f * head.radius * u::m;
      return true;
    }

    // A round bar from `a` to `b`, as triangles.
    void tube (render::DrawList& dl,
               const position_t& a,
               const position_t& b,
               meters_t radius,
               int sides) {
      const direction_t axis =
        direction_of (displacement (position_value (b) - position_value (a)));
      const Vec3& axis_v = direction_value (axis);
      const direction_t rim_x = unit_direction (
        normalized (length2 (cross (axis_v, direction_value (world_up))) < 1e-6f
                      ? cross (axis_v, Vec3 (1, 0, 0))
                      : cross (axis_v, direction_value (world_up))));
      const direction_t rim_y =
        unit_direction (cross (axis_v, direction_value (rim_x)));
      const auto put = [&] (const position_t& p, const direction_t& n) {
        dl.normal (direction_value (n));
        dl.vertex (position_value (p) +
                   direction_value (n) * meters_value (radius));
      };
      for (int i = 0; i < sides; ++i) {
        const radians_t t0 = tau * i / sides;
        const radians_t t1 = tau * (i + 1) / sides;
        const direction_t n0 = unit_direction (
          direction_value (rim_x) * std::cos (radians_value (t0)) +
          direction_value (rim_y) * std::sin (radians_value (t0)));
        const direction_t n1 = unit_direction (
          direction_value (rim_x) * std::cos (radians_value (t1)) +
          direction_value (rim_y) * std::sin (radians_value (t1)));
        put (a, n0);
        put (b, n0);
        put (b, n1);
        put (a, n0);
        put (b, n1);
        put (a, n1);
        // The ends, closed.
        dl.normal (-axis_v);
        dl.vertex (position_value (a));
        dl.vertex (position_value (a) +
                   direction_value (n1) * meters_value (radius));
        dl.vertex (position_value (a) +
                   direction_value (n0) * meters_value (radius));
        dl.normal (axis_v);
        dl.vertex (position_value (b));
        dl.vertex (position_value (b) +
                   direction_value (n0) * meters_value (radius));
        dl.vertex (position_value (b) +
                   direction_value (n1) * meters_value (radius));
      }
    }

    // A soft sprite facing the camera, stretched along `axis`; seen end-on
    // it rounds out.
    void flame_sprite (render::DrawList& dl,
                       const position_t& camera,
                       const position_t& centre,
                       const direction_t& axis,
                       meters_t half_length,
                       meters_t half_width,
                       DisplayColor colour,
                       proportion_t alpha) {
      const direction_t view = direction_of (
        displacement (position_value (camera) - position_value (centre)));
      const Vec3& axis_v = direction_value (axis);
      const Vec3& view_v = direction_value (view);
      const direction_t side =
        unit_direction (normalized (length2 (cross (axis_v, view_v)) < 1e-8f
                                      ? cross (Vec3 (1, 0, 0), view_v)
                                      : cross (axis_v, view_v)));
      const direction_t up =
        unit_direction (cross (view_v, direction_value (side)));
      const proportion_t end_on =
        std::fabs (dot (axis_v, view_v)) * proportion[one];
      const meters_t along =
        half_length + (half_width - half_length) * end_on * end_on;
      const displacement_t lengthwise =
        displacement (direction_value (up) * meters_value (along));
      const displacement_t breadthwise =
        displacement (direction_value (side) * meters_value (half_width));
      const Vec3& at = position_value (centre);
      const Vec3& l = displacement_value (lengthwise);
      const Vec3& b = displacement_value (breadthwise);
      dl.color (colour, alpha.numerical_value_in (one));
      dl.uv (0, 0);
      dl.vertex (at - l - b);
      dl.uv (1, 0);
      dl.vertex (at - l + b);
      dl.uv (1, 1);
      dl.vertex (at + l + b);
      dl.uv (0, 1);
      dl.vertex (at + l - b);
    }
  }

  position_t hammock_middle (const HammockSite& site) {
    using namespace camp_parts;
    return position (
      (position_value (site.strap[0]) + position_value (site.strap[1])) * 0.5f);
  }

  direction_t hammock_along (const HammockSite& site) {
    using namespace camp_parts;
    const displacement_t between = flat (displacement (
      position_value (site.strap[1]) - position_value (site.strap[0])));
    return unit_direction (length2 (displacement_value (between)) > 1e-6f
                             ? normalized (displacement_value (between))
                             : Vec3 (0, 0, 1));
  }

  direction_t hammock_across (const HammockSite& site) {
    using namespace camp_parts;
    return unit_direction (cross (direction_value (hammock_along (site)),
                                  direction_value (world_up)));
  }

  meters_t hammock_distance (const HammockSite& site, const position_t& feet) {
    using namespace camp_parts;
    const displacement_t between = flat (displacement (
      position_value (site.strap[1]) - position_value (site.strap[0])));
    const square_meters_t apart2 =
      std::max (length2 (displacement_value (between)), 1e-6f) * u::m2;
    const displacement_t to = flat (
      displacement (position_value (feet) - position_value (site.strap[0])));
    const proportion_t t = std::clamp<proportion_t> (
      dot (displacement_value (to), displacement_value (between)) * u::m2 /
        apart2,
      0.0f * proportion[one],
      whole);
    return length (displacement_value (to) -
                   displacement_value (between) * t.numerical_value_in (one)) *
           u::m;
  }

  proportion_t hammock_load (const Camp& camp, play_time_t now) {
    using namespace camp_parts;
    // Getting in or out takes this long.
    constexpr seconds_t settling = 0.8f * u::s;
    const seconds_t since = seconds_between (now, camp.rest_time) * u::s;
    const proportion_t eased =
      smoothstep (0.0f, seconds_value (settling), seconds_value (since)) *
      proportion[one];
    return camp.resting ? eased : whole - eased;
  }

  std::optional<HammockSite>
  hammock_site_at (std::span<const mov::Trunk> trunks,
                   const position_t& feet,
                   const terrain::ElevationField& ground) {
    using namespace camp_parts;
    std::optional<HammockSite> best;
    meters_t best_score = std::numeric_limits<float>::infinity () * u::m;
    for (std::size_t i = 0; i < trunks.size (); ++i)
      for (std::size_t j = i + 1; j < trunks.size (); ++j) {
        const mov::Trunk& a = trunks[i];
        const mov::Trunk& b = trunks[j];
        const displacement_t between = flat (displacement (b.root - a.root));
        const square_meters_t apart2 =
          length2 (displacement_value (between)) * u::m2;
        if (apart2 < 1.0f * u::m2 || apart2 > 49.0f * u::m2)
          continue;
        // Standing between them: along the middle of the line, and near it.
        const displacement_t to =
          flat (displacement (position_value (feet) - a.root));
        const proportion_t t =
          dot (displacement_value (to), displacement_value (between)) * u::m2 /
          apart2;
        if (t < 0.12f * proportion[one] || t > 0.88f * proportion[one])
          continue;
        const meters_t off =
          length (displacement_value (to) -
                  displacement_value (between) * t.numerical_value_in (one)) *
          u::m;
        if (off > 1.5f * u::m)
          continue;
        HammockSite site;
        if (!fits (a, b, trunks, ground, site))
          continue;
        const meters_t score =
          off +
          0.5f *
            std::fabs ((t - 0.5f * proportion[one]).numerical_value_in (one)) *
            std::sqrt (apart2.numerical_value_in (u::m2)) * u::m;
        if (score < best_score) {
          best_score = score;
          best = site;
        }
      }
    return best;
  }

  std::optional<HammockSite>
  nearest_hammock_site (std::span<const mov::Trunk> trunks,
                        const position_t& near,
                        const terrain::ElevationField& ground,
                        meters_t shade) {
    using namespace camp_parts;
    // Sorted along x, a tree's possible partners are its neighbours in the
    // order.
    std::vector<mov::Trunk> sorted (trunks.begin (), trunks.end ());
    std::ranges::sort (sorted, [] (const mov::Trunk& a, const mov::Trunk& b) {
      return a.root[0] < b.root[0];
    });
    constexpr meters_t window = hammock_most_span + 2.0f * most_girth;
    // The crown standing over a place: each tree round it by the depth of
    // its crown over its distance, found by a walk through the order from
    // where the place's x falls.
    const auto overhead = [&] (const position_t& middle) {
      scale_factor_t crown = 0.0f * scale_factor[one];
      const auto from = std::ranges::lower_bound (
        sorted,
        position_value (middle)[0] - meters_value (open_sky),
        {},
        [] (const mov::Trunk& trunk) {
          return static_cast<float> (trunk.root[0]);
        });
      for (auto trunk = from; trunk != sorted.end () &&
                              trunk->root[0] <= position_value (middle)[0] +
                                                  meters_value (open_sky);
           ++trunk)
        if (const meters_t away =
              length (displacement_value (
                flat (displacement (trunk->root - position_value (middle))))) *
              u::m;
            trunk->height > 3.0f && away < open_sky)
          crown += quantity_cast<scale_factor> ((trunk->height - trunk->clear) *
                                                u::m / (away + 2.0f * u::m));
      return crown;
    };
    std::optional<HammockSite> best;
    meters_t best_distance = std::numeric_limits<float>::infinity () * u::m;
    std::size_t first = 0;
    for (std::size_t i = 0; i < sorted.size (); ++i) {
      const mov::Trunk& a = sorted[i];
      if (!carries (a))
        continue;
      while (sorted[first].root[0] < a.root[0] - meters_value (window))
        ++first;
      std::size_t last = i;
      while (last + 1 < sorted.size () &&
             sorted[last + 1].root[0] <= a.root[0] + meters_value (window))
        ++last;
      const std::span<const mov::Trunk> around (sorted.data () + first,
                                                last - first + 1);
      for (std::size_t j = i + 1; j <= last; ++j) {
        const mov::Trunk& b = sorted[j];
        const position_t middle = position ((a.root + b.root) * 0.5f);
        meters_t distance =
          length (displacement_value (flat (
            displacement (position_value (middle) - position_value (near))))) *
          u::m;
        if (distance >= best_distance)
          continue;
        HammockSite site;
        if (!fits (a, b, around, ground, site))
          continue;
        if (shade > 0.0f * u::m)
          distance += shade * overhead (middle).numerical_value_in (one);
        if (distance >= best_distance)
          continue;
        best_distance = distance;
        best = site;
      }
    }
    return best;
  }

  std::vector<HammockSite>
  hammock_sites_near (std::span<const mov::Trunk> trunks,
                      const position_t& near,
                      meters_t within,
                      const terrain::ElevationField& ground) {
    using namespace camp_parts;
    std::vector<mov::Trunk> sorted (trunks.begin (), trunks.end ());
    std::ranges::sort (sorted, [] (const mov::Trunk& a, const mov::Trunk& b) {
      return a.root[0] < b.root[0];
    });
    constexpr meters_t window = hammock_most_span + 2.0f * most_girth;
    struct Found {
      meters_t distance;
      std::size_t a, b;
      HammockSite site;
    };
    std::vector<Found> found;
    std::size_t first = 0;
    for (std::size_t i = 0; i < sorted.size (); ++i) {
      const mov::Trunk& a = sorted[i];
      if (!carries (a))
        continue;
      while (sorted[first].root[0] < a.root[0] - meters_value (window))
        ++first;
      std::size_t last = i;
      while (last + 1 < sorted.size () &&
             sorted[last + 1].root[0] <= a.root[0] + meters_value (window))
        ++last;
      const std::span<const mov::Trunk> around (sorted.data () + first,
                                                last - first + 1);
      for (std::size_t j = i + 1; j <= last; ++j) {
        const position_t middle = position ((a.root + sorted[j].root) * 0.5f);
        const meters_t distance =
          length (displacement_value (flat (
            displacement (position_value (middle) - position_value (near))))) *
          u::m;
        HammockSite site;
        if (distance > within || !fits (a, sorted[j], around, ground, site))
          continue;
        found.push_back ({ distance, i, j, site });
      }
    }
    std::ranges::sort (found, {}, &Found::distance);
    std::vector<bool> used (sorted.size ());
    std::vector<HammockSite> sites;
    for (const Found& f : found) {
      if (used[f.a] || used[f.b])
        continue;
      used[f.a] = used[f.b] = true;
      sites.push_back (f.site);
    }
    return sites;
  }

  void draw_hammock (render::DrawList& dl,
                     const HammockSite& site,
                     proportion_t load,
                     radians_t swing) {
    using namespace camp_parts;
    const Shape s = shape_of (site, load, swing);
    render::DrawState both_sides;
    both_sides.cull = false;
    dl.state (both_sides);
    dl.lit (true);
    dl.fogged (true);
    dl.begin (render::Prim::Triangles);

    // The cloth: madder canvas with two unbleached stripes and a darker
    // hem.
    constexpr int along = 24;
    constexpr int across = 10;
    const DisplayColor canvas (0.70f, 0.27f, 0.16f);
    const DisplayColor stripe (0.88f, 0.80f, 0.62f);
    const DisplayColor hem (0.46f, 0.17f, 0.11f);
    const auto at = [&] (int i, int j) {
      return s.cloth (static_cast<float> (i) / along * proportion[one],
                      (2.0f * j / across - 1.0f) * span_coordinate[one]);
    };
    const auto normal_at = [&] (int i, int j) {
      const displacement_t du =
        displacement (position_value (at (std::min (i + 1, along), j)) -
                      position_value (at (std::max (i - 1, 0), j)));
      const displacement_t dv =
        displacement (position_value (at (i, std::min (j + 1, across))) -
                      position_value (at (i, std::max (j - 1, 0))));
      if (length2 (cross (displacement_value (du), displacement_value (dv))) <
          1e-10f)
        return s.up;
      const direction_t n = unit_direction (
        normalized (cross (displacement_value (du), displacement_value (dv))));
      return dot (direction_value (n), direction_value (s.up)) < 0.0f
               ? unit_direction (-direction_value (n))
               : n;
    };
    for (int j = 0; j < across; ++j) {
      dl.color (j == 0 || j == across - 1   ? hem
                : j == 2 || j == across - 3 ? stripe
                                            : canvas);
      for (int i = 0; i < along; ++i) {
        const auto put = [&] (int a, int b) {
          dl.normal (direction_value (normal_at (a, b)));
          dl.vertex (position_value (at (a, b)));
        };
        put (i, j);
        put (i + 1, j);
        put (i + 1, j + 1);
        put (i, j);
        put (i + 1, j + 1);
        put (i, j + 1);
      }
    }

    // The lines from the gathered ends to the bark, and the webbing round
    // each trunk.
    constexpr meters_t line = 0.011f * u::m;
    dl.color (0.78f, 0.70f, 0.52f);
    tube (dl,
          s.hang[0],
          s.cloth (0.0f * proportion[one], 0.0f * span_coordinate[one]),
          line,
          5);
    tube (dl, s.cloth (whole, 0.0f * span_coordinate[one]), s.hang[1], line, 5);
    dl.color (0.17f, 0.16f, 0.15f);
    for (int end = 0; end < 2; ++end) {
      constexpr int sides = 14;
      const meters_t radius = site.girth[end] + 0.008f * u::m;
      for (int i = 0; i < sides; ++i) {
        const radians_t a0 = tau * i / sides;
        const radians_t a1 = tau * (i + 1) / sides;
        const direction_t n0 = unit_direction (Vec3 (
          std::cos (radians_value (a0)), 0.0f, std::sin (radians_value (a0))));
        const direction_t n1 = unit_direction (Vec3 (
          std::cos (radians_value (a1)), 0.0f, std::sin (radians_value (a1))));
        const position_t low = position (position_value (site.strap[end]) -
                                         direction_value (world_up) * 0.03f);
        const position_t high = position (position_value (site.strap[end]) +
                                          direction_value (world_up) * 0.03f);
        const auto put = [&] (const position_t& p, const direction_t& n) {
          dl.normal (direction_value (n));
          dl.vertex (position_value (p) +
                     direction_value (n) * meters_value (radius));
        };
        put (low, n0);
        put (high, n0);
        put (high, n1);
        put (low, n0);
        put (high, n1);
        put (low, n1);
      }
    }
    dl.end ();
    dl.state (render::DrawState ());
  }

  AvatarSkeleton pose_resting (const HammockSite& site,
                               radians_t swing,
                               const direction_t& gaze,
                               seconds_t time) {
    using namespace camp_parts;
    const Shape s = shape_of (site, whole, swing);
    const meters_t span = length (displacement_value (s.run)) * u::m;
    // The hips lie a little toward the foot from the lowest point, so the
    // whole body is cradled.
    const proportion_t at = 0.46f * proportion[one];
    const direction_t facing = s.normal (at + 0.04f * proportion[one]);
    const direction_t headward = s.tangent (at + 0.06f * proportion[one]);
    const direction_t right = unit_direction (normalized (
      cross (direction_value (headward), direction_value (facing))));
    const meters_t breath =
      0.006f * u::m * std::sin (seconds_value (time) * 1.3f);

    HoldPose hold;
    hold.facing = direction_value (facing);
    hold.up = direction_value (headward);
    // High enough in the cloth that the pack on their back stays in it.
    hold.pelvis =
      position_value (s.centre (at)) + direction_value (s.normal (at)) * 0.21f;
    hold.lean = 0.16f;
    // The head rests back; it turns only part of the way after the eyes.
    hold.gaze = normalized (direction_value (facing) +
                            normalized (direction_value (gaze)) * 0.7f);
    hold.head_up = direction_value (s.tangent (at + 0.22f * proportion[one]));
    const proportion_t cross_ankles = 0.86f * u::m / span;
    for (int i = 0; i < 2; ++i) {
      const float side = i == 0 ? -1.0f : 1.0f;
      hold.wrist[i] =
        hold.pelvis + direction_value (headward) * 0.25f +
        direction_value (facing) * meters_value (0.13f * u::m + breath) +
        direction_value (right) * (side * 0.035f);
      hold.elbow_pole[i] =
        direction_value (right) * side - direction_value (facing) * 0.4f;
      const proportion_t foot = at - cross_ankles;
      hold.ankle[i] = position_value (s.centre (foot)) +
                      direction_value (s.normal (foot)) * (0.10f + 0.05f * i) +
                      direction_value (s.side) * (side * 0.03f);
      hold.knee_pole[i] =
        direction_value (facing) + direction_value (right) * (side * 0.2f);
    }
    // The feet fall slack, toes up and a little away.
    const direction_t at_feet = s.normal (at - cross_ankles);
    const direction_t to_head = s.tangent (at - cross_ankles);
    hold.sole = normalized (direction_value (at_feet) * 0.82f -
                            direction_value (to_head) * 0.57f);
    hold.instep = normalized (direction_value (to_head) * 0.82f +
                              direction_value (at_feet) * 0.57f);
    return pose_holding (hold);
  }

  position_t resting_eye (const HammockSite& site, radians_t swing) {
    using namespace camp_parts;
    const Shape s = shape_of (site, whole, swing);
    const AvatarSkeleton k = pose_resting (site, swing, s.up, 0.0f * u::s);
    return position (
      k.head + direction_value (s.normal (0.6f * proportion[one])) * 0.09f);
  }

  direction_t
  resting_gaze (const HammockSite& site, radians_t yaw, inclination_t pitch) {
    using namespace camp_parts;
    // Yaw turns from the foot of the hammock; a sleeper looks along
    // their own length and up.
    const direction_t ahead =
      unit_direction (-direction_value (hammock_along (site)));
    const direction_t right = unit_direction (
      cross (direction_value (ahead), direction_value (world_up)));
    return unit_direction (
      (direction_value (ahead) * std::cos (radians_value (yaw)) +
       direction_value (right) * std::sin (radians_value (yaw))) *
        std::cos (radians_value (pitch)) +
      direction_value (world_up) * std::sin (radians_value (pitch)));
  }

  proportion_t fire_burn (const Camp& camp, play_time_t now) {
    using namespace camp_parts;
    // How long a fire takes to catch, and to die once put out.
    constexpr seconds_t catching = 3.5f * u::s;
    constexpr seconds_t dying = 1.5f * u::s;
    const seconds_t since = seconds_between (now, camp.fire_time) * u::s;
    if (camp.fire)
      return (0.12f + 0.88f * smoothstep (0.0f,
                                          seconds_value (catching),
                                          seconds_value (since))) *
             proportion[one];
    return since < dying ? whole - smoothstep (0.0f,
                                               seconds_value (dying),
                                               seconds_value (since)) *
                                     proportion[one]
                         : 0.0f * proportion[one];
  }

  position_t fire_light_position (const Camp& camp) {
    using namespace camp_parts;
    return position (position_value (camp.hearth) +
                     direction_value (world_up) * 0.55f);
  }

  DisplayColor fire_light_color (proportion_t burn, seconds_t time) {
    const float t = seconds_value (time);
    const proportion_t flicker = (0.80f + 0.10f * std::sin (t * 11.3f) +
                                  0.06f * std::sin (t * 23.7f + 1.3f) +
                                  0.04f * std::sin (t * 5.1f + 0.4f)) *
                                 proportion[one];
    // A display colour: the renderer's light is its 2.2 power.
    return scale_display (
      DisplayColor (1.50f, 1.04f, 0.56f),
      std::pow (
        std::clamp ((burn * flicker).numerical_value_in (one), 0.0f, 1.0f),
        1.0f / 2.2f) *
        scale_factor[one]);
  }

  doneness_t pan_cooked (const Pan& pan, play_time_t now) {
    // Fried in nine seconds, kept until forty, burnt fourteen after.
    constexpr seconds_t frying = 9.0f * u::s;
    constexpr seconds_t keeping = 40.0f * u::s;
    constexpr seconds_t burning = 14.0f * u::s;
    const seconds_t age = seconds_between (now, pan.since) * u::s;
    if (!pan.on || age <= seconds_t {})
      return 0.0f * doneness[one];
    if (age < frying)
      return quantity_cast<doneness> (age / frying);
    if (age < keeping)
      return 1.0f * doneness[one];
    return std::min (2.0f * doneness[one],
                     1.0f * doneness[one] +
                       quantity_cast<doneness> ((age - keeping) / burning));
  }

  Pan pan_set_on (const position_t& hearth, const position_t& feet) {
    using namespace camp_parts;
    const displacement_t toward =
      flat (displacement (position_value (feet) - position_value (hearth)));
    const direction_t way =
      unit_direction (length2 (displacement_value (toward)) > 1e-6f
                        ? normalized (displacement_value (toward))
                        : Vec3 (0, 0, 1));
    Pan pan;
    pan.on = true;
    pan.place =
      position (position_value (hearth) + direction_value (way) * 0.36f +
                direction_value (world_up) * 0.17f);
    pan.yaw = std::atan2 (direction_value (way)[2], direction_value (way)[0]) *
              heading[u::rad];
    return pan;
  }

  void draw_pan (render::DrawList& dl,
                 const position_t& place,
                 radians_t yaw,
                 std::span<const MushroomSite> mushrooms,
                 doneness_t cooked,
                 const Fish* fish,
                 int eggs) {
    using namespace camp_parts;
    constexpr meters_t radius = 0.17f * u::m;
    constexpr meters_t wall = 0.045f * u::m;
    constexpr int rim = 14;
    const Vec3& centre = position_value (place);
    const Vec3& up = direction_value (world_up);
    render::DrawState solid;
    solid.cull = false;
    dl.state (solid);
    dl.lit (true);
    dl.fogged (true);
    dl.begin (render::Prim::Triangles);
    const auto round = [&] (int i, meters_t r, meters_t height) {
      const radians_t a = tau * i / rim;
      return position (centre +
                       Vec3 (std::cos (radians_value (a)),
                             0.0f,
                             std::sin (radians_value (a))) *
                         meters_value (r) +
                       up * meters_value (height));
    };
    const auto vertex = [&] (const position_t& p) {
      dl.vertex (position_value (p));
    };
    constexpr meters_t floor = 0.0f * u::m;
    // The floor, glistening a little, and the wall round it.
    dl.color (0.11f, 0.10f, 0.10f);
    dl.normal (up);
    for (int i = 0; i < rim; ++i) {
      dl.vertex (centre);
      vertex (round (i + 1, radius, floor));
      vertex (round (i, radius, floor));
    }
    dl.color (0.07f, 0.07f, 0.075f);
    for (int i = 0; i < rim; ++i) {
      const radians_t a = tau * (i + 0.5f) / rim;
      dl.normal (normalized (Vec3 (
        std::cos (radians_value (a)), 0.4f, std::sin (radians_value (a)))));
      vertex (round (i, radius, floor));
      vertex (round (i + 1, radius, floor));
      vertex (round (i + 1, radius * 1.1f, wall));
      vertex (round (i, radius, floor));
      vertex (round (i + 1, radius * 1.1f, wall));
      vertex (round (i, radius * 1.1f, wall));
    }
    const direction_t out = unit_direction (Vec3 (
      std::cos (radians_value (yaw)), 0.0f, std::sin (radians_value (yaw))));
    const Vec3& out_v = direction_value (out);
    tube (dl,
          position (centre + out_v * meters_value (radius * 1.05f) +
                    up * meters_value (wall)),
          position (centre + out_v * meters_value (radius + 0.27f * u::m) +
                    up * meters_value (wall + 0.035f * u::m)),
          0.014f * u::m,
          6);

    // A fish lies across the pan, square to the handle, and the
    // mushrooms in what room it leaves.
    const direction_t across =
      unit_direction (Vec3 (-out_v[2], 0.0f, out_v[0]));
    const Vec3& across_v = direction_value (across);
    if (fish) {
      const meters_t drawn = 1.5f * fish->length;
      const scale_factor_t fit =
        std::min (1.0f * scale_factor[one],
                  quantity_cast<scale_factor> (2.0f * radius * 1.15f / drawn));
      draw_fish (dl,
                 *fish,
                 centre -
                   across_v * (0.5f * drawn * fit).numerical_value_in (u::m) -
                   out_v * 0.03f + up * 0.035f,
                 across_v,
                 out_v * -1.0f,
                 fit.numerical_value_in (one),
                 0.0f,
                 cooked.numerical_value_in (one));
    }
    // Eggs broken into the middle, the mushrooms round them.
    for (int i = 0; i < eggs; ++i) {
      const radians_t around = tau * (i + 0.25f) / eggs + yaw;
      position_t at = position (centre + up * 0.004f);
      if (fish)
        position_value (at) +=
          out_v * 0.09f + across_v * (0.1f * (i % 2 ? 1.0f : -1.0f));
      else if (eggs > 1)
        position_value (at) += Vec3 (std::cos (radians_value (around)),
                                     0.0f,
                                     std::sin (radians_value (around))) *
                               0.065f;
      draw_fried_egg (dl,
                      position_value (at),
                      radians_value (around + 2.0f * u::rad),
                      cooked.numerical_value_in (one),
                      static_cast<std::uint32_t> (i * 7919 + 13));
    }
    for (std::size_t i = 0; i < mushrooms.size (); ++i) {
      MushroomSite lying = mushrooms[i];
      const radians_t around = tau * (i + 0.5f) / mushrooms.size () +
                               hash_lane (lying.seed, 31) * u::rad;
      const meters_t out_from_middle = eggs > 0                ? 0.125f * u::m
                                       : mushrooms.size () > 1 ? 0.07f * u::m
                                                               : 0.0f * u::m;
      lying.base = position (centre +
                             Vec3 (std::cos (radians_value (around)),
                                   0.0f,
                                   std::sin (radians_value (around))) *
                               meters_value (out_from_middle) +
                             up * 0.03f);
      if (fish)
        position_value (lying.base) +=
          out_v * 0.09f + across_v * (0.11f * ((i + eggs) % 2 ? 1.0f : -1.0f));
      lying.scale = 0.5f * mushrooms[i].scale;
      lying.lean = 1.3f * inclination[u::rad];
      lying.lean_toward =
        radians_value (around + 1.9f * u::rad) * heading[u::rad];
      lying.cooked = cooked;
      draw_mushroom (dl, lying);
    }
    dl.end ();
    dl.state (render::DrawState ());
  }

  void draw_hearth (render::DrawList& dl,
                    const position_t& hearth,
                    proportion_t burn) {
    using namespace camp_parts;
    const Vec3& centre = position_value (hearth);
    const Vec3& up = direction_value (world_up);
    const auto seed = static_cast<std::uint32_t> (
      std::lround (centre[0] * 13.0f) * 7919 + std::lround (centre[2] * 13.0f));
    dl.state (render::DrawState ());
    dl.lit (true);
    dl.fogged (true);

    // A ring of fieldstones.
    constexpr int stones = 9;
    for (int i = 0; i < stones; ++i) {
      const radians_t around =
        tau * (i + 0.35f * hash_lane (seed, 10 + i)) / stones;
      const meters_t size = (0.085f + 0.045f * hash_lane (seed, 30 + i)) * u::m;
      const proportion_t grey =
        (0.36f + 0.16f * hash_lane (seed, 50 + i)) * proportion[one];
      dl.color (grey.numerical_value_in (one),
                (grey * 0.97f).numerical_value_in (one),
                (grey * 0.92f).numerical_value_in (one));
      dl.push ();
      dl.translate (centre +
                    Vec3 (std::cos (radians_value (around)),
                          0.0f,
                          std::sin (radians_value (around))) *
                      0.43f +
                    up * meters_value (size * 0.45f));
      dl.rotate (tau * hash_lane (seed, 70 + i), 0, 1, 0);
      dl.scale (1.25f, 0.72f, 0.95f);
      dl.sphere (meters_value (size), 7, 5);
      dl.pop ();
    }

    dl.begin (render::Prim::Triangles);
    // The bed of ash.
    constexpr int rim = 14;
    dl.color (0.10f, 0.095f, 0.09f);
    dl.normal (up);
    const position_t bed = position (centre + up * 0.02f);
    for (int i = 0; i < rim; ++i) {
      const radians_t a0 = tau * i / rim;
      const radians_t a1 = tau * (i + 1) / rim;
      dl.vertex (position_value (bed));
      dl.vertex (position_value (bed) + Vec3 (std::cos (radians_value (a1)),
                                              0.0f,
                                              std::sin (radians_value (a1))) *
                                          0.40f);
      dl.vertex (position_value (bed) + Vec3 (std::cos (radians_value (a0)),
                                              0.0f,
                                              std::sin (radians_value (a0))) *
                                          0.40f);
    }
    // Split logs leaned together over it, and two laid across the bottom;
    // they char as the fire takes.
    const scale_factor_t charred =
      (1.0f - 0.55f * burn.numerical_value_in (one)) * scale_factor[one];
    constexpr int logs = 5;
    for (int i = 0; i < logs; ++i) {
      const radians_t around =
        tau * (i + 0.3f * hash_lane (seed, 90 + i)) / logs;
      const direction_t out =
        unit_direction (Vec3 (std::cos (radians_value (around)),
                              0.0f,
                              std::sin (radians_value (around))));
      const proportion_t bark = (0.20f + 0.08f * hash_lane (seed, 110 + i)) *
                                charred.numerical_value_in (one) *
                                proportion[one];
      dl.color (bark.numerical_value_in (one),
                (bark * 0.74f).numerical_value_in (one),
                (bark * 0.52f).numerical_value_in (one));
      tube (dl,
            position (centre + direction_value (out) * 0.29f + up * 0.05f),
            position (centre - direction_value (out) * 0.07f + up * 0.40f),
            0.042f * u::m,
            6);
    }
    dl.color ((0.17f * charred).numerical_value_in (one),
              (0.12f * charred).numerical_value_in (one),
              (0.085f * charred).numerical_value_in (one));
    tube (dl,
          position (centre + Vec3 (-0.26f, 0.06f, -0.10f)),
          position (centre + Vec3 (0.26f, 0.06f, 0.08f)),
          0.05f * u::m,
          6);
    tube (dl,
          position (centre + Vec3 (-0.08f, 0.07f, 0.25f)),
          position (centre + Vec3 (0.10f, 0.07f, -0.25f)),
          0.05f * u::m,
          6);
    dl.end ();

    // Embers under the logs glow by their own light.
    if (burn > 0.03f * proportion[one]) {
      dl.lit (false);
      dl.begin (render::Prim::Triangles);
      dl.color (burn.numerical_value_in (one),
                (0.36f * burn * burn).numerical_value_in (one),
                (0.07f * burn * burn).numerical_value_in (one));
      dl.normal (up);
      const position_t embers = position (centre + up * 0.035f);
      for (int i = 0; i < rim; ++i) {
        const radians_t a0 = tau * i / rim;
        const radians_t a1 = tau * (i + 1) / rim;
        dl.vertex (position_value (embers));
        dl.vertex (position_value (embers) +
                   Vec3 (std::cos (radians_value (a1)),
                         0.0f,
                         std::sin (radians_value (a1))) *
                     0.22f);
        dl.vertex (position_value (embers) +
                   Vec3 (std::cos (radians_value (a0)),
                         0.0f,
                         std::sin (radians_value (a0))) *
                     0.22f);
      }
      dl.end ();
      dl.lit (true);
    }
  }

  void draw_fire (render::Renderer& renderer,
                  const position_t& hearth,
                  const position_t& camera,
                  proportion_t burn,
                  seconds_t time) {
    using namespace camp_parts;
    if (burn <= 0.01f * proportion[one])
      return;
    const Vec3& centre = position_value (hearth);
    const Vec3& up = direction_value (world_up);
    const float clock = seconds_value (time);
    static const render::TexturePtr disc = make_soft_disc_texture (renderer);
    static render::DrawList dl;
    dl.clear ();
    render::DrawState glow;
    glow.blend = true;
    glow.additive = true;
    glow.depth_write = false;
    glow.cull = false;
    dl.state (glow);
    dl.lit (false);
    dl.fogged (true);
    dl.set_texture (disc.get ());
    dl.begin (render::Prim::Quads);

    // The warm air round the fire, for the bloom to take up.
    flame_sprite (dl,
                  camera,
                  position (centre + up * 0.45f),
                  world_up,
                  1.0f * u::m,
                  1.0f * u::m,
                  DisplayColor (1.0f, 0.42f, 0.12f),
                  0.15f * burn);
    flame_sprite (dl,
                  camera,
                  position (centre + up * 0.6f),
                  world_up,
                  2.3f * u::m,
                  2.3f * u::m,
                  DisplayColor (1.0f, 0.40f, 0.12f),
                  0.045f * burn);

    // Tongues of flame: each a chain of blobs rising from the logs,
    // leaning and licking on its own phase, cooling from pale yellow
    // through orange to a red tip.
    constexpr int tongues = 4;
    constexpr int blobs = 6;
    for (int tongue = 0; tongue < tongues; ++tongue) {
      const radians_t phase = 1.9f * u::rad * tongue;
      const float phase_v = radians_value (phase);
      const radians_t around = tau * tongue / tongues + 0.6f * u::rad;
      const displacement_t foot =
        displacement (Vec3 (std::cos (radians_value (around)),
                            0.0f,
                            std::sin (radians_value (around))) *
                      (tongue == 0 ? 0.0f : 0.11f));
      const scale_factor_t lick =
        (0.80f + 0.20f * std::sin (clock * (6.1f + tongue) + phase_v) *
                   std::sin (clock * 3.3f + 2.0f * phase_v)) *
        scale_factor[one];
      const meters_t height = (tongue == 0 ? 0.95f : 0.62f) * u::m *
                              lick.numerical_value_in (one) *
                              std::pow (burn.numerical_value_in (one), 0.7f);
      for (int i = 0; i < blobs; ++i) {
        const proportion_t t = (i + 0.5f) / blobs * proportion[one];
        const float tv = t.numerical_value_in (one);
        const displacement_t lean = displacement (
          Vec3 (0.07f * std::sin (clock * 4.7f + 3.0f * tv + phase_v),
                0.0f,
                0.07f * std::sin (clock * 5.9f + 2.2f * tv + 1.7f * phase_v)));
        const position_t middle =
          position (centre + displacement_value (foot) * (1.0f - 0.7f * tv) +
                    up * (0.12f + tv * meters_value (height)) +
                    displacement_value (lean) * tv);
        const DisplayColor colour =
          t < 0.30f * proportion[one]   ? DisplayColor (1.0f, 0.88f, 0.55f)
          : t < 0.65f * proportion[one] ? DisplayColor (1.0f, 0.55f, 0.16f)
                                        : DisplayColor (0.95f, 0.26f, 0.07f);
        const proportion_t fade = (whole - t) * (whole - t);
        flame_sprite (
          dl,
          camera,
          middle,
          unit_direction (normalized (up + displacement_value (lean) * 2.0f)),
          0.8f * height / blobs + 0.06f * u::m,
          (0.035f + 0.11f * (1.0f - 0.7f * tv)) * u::m,
          colour,
          (0.22f * proportion[one] + 0.50f * fade) * burn);
      }
    }
    // The white heart of it.
    for (int i = 0; i < 2; ++i)
      flame_sprite (dl,
                    camera,
                    position (centre + up * (0.14f + 0.10f * i)),
                    world_up,
                    0.12f * u::m,
                    0.085f * u::m,
                    DisplayColor (1.0f, 0.94f, 0.78f),
                    (0.75f - 0.25f * i) * burn);
    dl.end ();
    dl.set_texture (nullptr);
    dl.state (render::DrawState ());
    renderer.draw_list (dl, 0x3000);
  }

  void draw_beacon (render::Renderer& renderer,
                    const position_t& hearth,
                    const position_t& camera,
                    proportion_t burn,
                    proportion_t daylight,
                    seconds_t time) {
    using namespace camp_parts;
    if (burn <= 0.01f * proportion[one])
      return;
    const Vec3& centre = position_value (hearth);
    const Vec3& up = direction_value (world_up);
    const float clock = seconds_value (time);
    const meters_t far = length (position_value (camera) - centre) * u::m;
    // Near, the stack burns as several fires together.
    if (far < 220.0f * u::m) {
      draw_fire (renderer, hearth, camera, burn, time);
      for (int i = 0; i < 3; ++i) {
        const radians_t around = tau * i / 3.0f + 0.5f * u::rad;
        draw_fire (renderer,
                   position (centre + Vec3 (std::cos (radians_value (around)),
                                            0.0f,
                                            std::sin (radians_value (around))) *
                                        0.32f),
                   camera,
                   0.85f * burn,
                   time + 1.7f * u::s * i);
      }
    }
    static const render::TexturePtr disc = make_soft_disc_texture (renderer);
    static render::DrawList dl;
    dl.clear ();
    dl.lit (false);
    // Far off it would be lost in the haze; it is what is looked for.
    dl.fogged (false);
    dl.set_texture (disc.get ());

    // The smoke: puffs rising up a column that widens and leans off with
    // the wind, each climbing it and fading as it goes.
    render::DrawState smoke;
    smoke.blend = true;
    smoke.depth_write = false;
    smoke.cull = false;
    dl.state (smoke);
    dl.begin (render::Prim::Quads);
    const direction_t wind =
      unit_direction (normalized (Vec3 (0.35f, 0.0f, 0.12f)));
    const proportion_t shade = 0.08f * proportion[one] + 0.5f * daylight;
    constexpr int puffs = 22;
    for (int i = 0; i < puffs; ++i) {
      const proportion_t t =
        std::fmod (clock * 0.012f + (i + 0.5f) / puffs, 1.0f) * proportion[one];
      const float tv = t.numerical_value_in (one);
      const meters_t rise = 2.0f * u::m + 170.0f * u::m * t;
      const position_t middle = position (
        centre + up * meters_value (rise) +
        direction_value (wind) *
          meters_value (rise * (0.25f * proportion[one] + 0.9f * t)) +
        Vec3 (std::sin (i * 2.3f + clock * 0.1f), 0.0f, std::cos (i * 1.7f)) *
          meters_value (2.0f * t * rise * 0.05f));
      const meters_t size = 1.0f * u::m + 22.0f * u::m * std::sqrt (tv);
      const proportion_t alpha =
        burn * (0.1f * proportion[one] + 0.4f * daylight) *
        smoothstep (0.0f, 0.06f, tv) * (whole - t) * (whole - t);
      flame_sprite (dl,
                    camera,
                    middle,
                    world_up,
                    size,
                    size,
                    DisplayColor ((shade * 1.05f).numerical_value_in (one),
                                  shade.numerical_value_in (one),
                                  (shade * 0.94f).numerical_value_in (one)),
                    alpha);
    }
    dl.end ();

    // The glow: warm light over the flames, grown with distance so that
    // it stays some pixels across from the next summit, and brightest in
    // the dark.
    if (far > 40.0f * u::m) {
      render::DrawState glow;
      glow.blend = true;
      glow.additive = true;
      glow.depth_write = false;
      glow.cull = false;
      dl.state (glow);
      dl.begin (render::Prim::Quads);
      const proportion_t flicker = (0.85f + 0.15f * std::sin (clock * 7.3f) *
                                              std::sin (clock * 3.1f + 0.7f)) *
                                   proportion[one];
      const proportion_t dark = whole - 0.8f * daylight;
      const proportion_t grow =
        smoothstep (40.0f, 400.0f, meters_value (far)) * proportion[one];
      const meters_t size = 1.5f * u::m + 0.0045f * far;
      flame_sprite (
        dl,
        camera,
        position (centre + up * (1.2f + 0.002f * meters_value (far))),
        world_up,
        size * 1.5f,
        size,
        DisplayColor (1.0f, 0.55f, 0.18f),
        burn * flicker * dark * grow * 0.9f);
      flame_sprite (
        dl,
        camera,
        position (centre + up * (1.5f + 0.002f * meters_value (far))),
        world_up,
        size * 4.0f,
        size * 4.0f,
        DisplayColor (1.0f, 0.38f, 0.10f),
        burn * flicker * dark * grow * 0.18f);
      dl.end ();
    }
    dl.set_texture (nullptr);
    dl.state (render::DrawState ());
    renderer.draw_list (dl, 0x3100);
  }
}
