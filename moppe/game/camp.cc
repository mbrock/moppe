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

    const direction_t east = unit_direction (Vec3 (1, 0, 0));
    const direction_t north = unit_direction (Vec3 (0, 0, 1));

    displacement_t flat (displacement_t v) {
      v.numerical_value_ref_in (u::m)[1] = 0.0f;
      return v;
    }

    template <typename Q>
    Q mix (Q a, Q b, proportion_t t) {
      return a + (b - a) * t.numerical_value_in (one);
    }

    // A length, a share, or an offset scaled by shares, which leave it
    // what it was (docs/typed-quantities.md).
    template <Quantity Q>
    meters_t as_length (const Q& q) {
      return quantity_cast<isq::length> (q);
    }
    template <Quantity Q>
    proportion_t as_share (const Q& q) {
      return quantity_cast<proportion> (q);
    }
    template <Quantity Q>
    displacement_t as_offset (const Q& q) {
      return quantity_cast<isq::displacement> (q);
    }
    // What moves a place by an offset, or by a way times a length.
    template <Quantity Q>
    auto shift (const Q& d) {
      return quantity_cast<isq::position_vector> (d);
    }

    // The dot product of two offsets, as an area.
    square_meters_t dot_m2 (const displacement_t& a, const displacement_t& b) {
      return dot (displacement_value (a), displacement_value (b)) * u::m2;
    }

    // How far a straight offset reaches.
    meters_t length_of (const displacement_t& d) {
      return length (displacement_value (d)) * u::m;
    }

    // A place's east-west lane, and its height.
    meters_t east_of (const position_t& p) {
      return position_value (p)[0] * u::m;
    }
    meters_t height_of (const position_t& p) {
      return position_value (p)[1] * u::m;
    }

    // Halfway between two places.
    position_t halfway (const position_t& a, const position_t& b) {
      return position ((position_value (a) + position_value (b)) * 0.5f);
    }

    // The way `v` points; it must have some length.
    template <Quantity Q>
    direction_t unit_of (const Q& v) {
      return unit_direction (normalized (v.numerical_value_ref_in (Q::unit)));
    }

    // `v` with its part along `way` taken out.
    template <Quantity Q>
    Q square_off (const Q& v, const direction_t& way) {
      const Vec3& raw = v.numerical_value_ref_in (Q::unit);
      return Q (raw - direction_value (way) * dot (raw, direction_value (way)),
                Q::reference);
    }

    // The level way round the up axis `a` turns from east toward south.
    direction_t compass (radians_t a) {
      return unit_direction (Vec3 (libm::cos (a), 0.0f, libm::sin (a)));
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
        return hang[0] + shift (run * t.numerical_value_in (one)) +
               shift (down * as_length (4.0f * sag * t * (whole - t)));
      }
      direction_t tangent (proportion_t t) const {
        return unit_of (
          run + as_offset (down * as_length (4.0f * sag * (whole - 2.0f * t))));
      }
      // The way a body lying at `t` faces: square to the line, upward.
      direction_t normal (proportion_t t) const {
        return unit_of (square_off (up, tangent (t)));
      }
      // The cloth: `lengthwise` along it from foot to head, `across` from
      // -1 to 1 across its half width. Its edges run
      // straighter than its middle, so they rise.
      position_t cloth (proportion_t lengthwise,
                        span_coordinate_t across_at) const {
        const float across = across_at.numerical_value_in (one);
        const proportion_t t =
          cloth_from + as_share ((cloth_to - cloth_from) * lengthwise);
        const proportion_t bell =
          libm::sin (pi * lengthwise.numerical_value_in (one)) *
          proportion[one];
        return centre (t) +
               shift (side *
                      (half_width *
                       std::pow (std::max (bell.numerical_value_in (one), 0.0f),
                                 0.55f) *
                       across)) +
               shift (up * as_length (lift * bell * across * across));
      }
    };

    Shape
    shape_of (const HammockSite& site, proportion_t load, radians_t swing) {
      Shape s;
      const direction_t along = hammock_along (site);
      s.hang[0] = site.strap[0] + shift (along * site.girth[0]);
      s.hang[1] = site.strap[1] - shift (along * site.girth[1]);
      s.run = s.hang[1] - s.hang[0];
      const direction_t axis = direction_of (s.run);
      const direction_t up = unit_of (square_off (world_up, axis));
      const direction_t side = quantity_cast<direction> (cross (axis, up));
      s.down = side * libm::sin (swing) - up * libm::cos (swing);
      s.up = -s.down;
      s.side = side * libm::cos (swing) + up * libm::sin (swing);
      const meters_t span = length_of (s.run);
      s.sag = as_length (span * mix (empty_sag, loaded_sag, load));
      s.half_width = mix (0.34f * u::m, 0.54f * u::m, load);
      s.lift = mix (0.08f * u::m, 0.30f * u::m, load);
      return s;
    }

    bool same_tree (const mov::Trunk& a, const mov::Trunk& b) {
      return position_value (a.root)[0] == position_value (b.root)[0] &&
             position_value (a.root)[2] == position_value (b.root)[2];
    }

    bool carries (const mov::Trunk& trunk) {
      const meters_t height = trunk.height;
      const meters_t radius = trunk.radius;
      const meters_t clear = trunk.clear;
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
      const displacement_t between = flat (b.root - a.root);
      const meters_t apart = length_of (between);
      const meters_t span = apart - a.radius - b.radius;
      if (span < hammock_least_span || span > hammock_most_span)
        return false;
      if (abs (height_of (a.root) - height_of (b.root)) > most_step)
        return false;
      const meters_t top = std::max (height_of (a.root), height_of (b.root));
      const meters_t strap = top + seat_height + as_length (loaded_sag * span);
      if (strap - std::min (height_of (a.root), height_of (b.root)) >
          highest_strap)
        return false;
      const direction_t along = unit_direction (displacement_value (between) *
                                                (1.0f / meters_value (apart)));
      // The ground must not rise into the cloth.
      for (int i = 1; i < 6; ++i) {
        const position_t at = a.root + shift (between * (i / 6.0f));
        if (ground (plan (at)) >
            terrain::surface_elevation_point (top + 0.2f * u::m))
          return false;
      }
      for (const mov::Trunk& other : others) {
        if (same_tree (other, a) || same_tree (other, b) ||
            other.height < 0.8f * u::m)
          continue;
        const displacement_t to = flat (other.root - a.root);
        const meters_t reach = as_length (dot (to, along));
        if (reach < 0.0f * u::m || reach > apart)
          continue;
        const meters_t off = length_of (to - as_offset (along * reach));
        if (off < clear_way + other.radius)
          return false;
      }
      // The head lies toward the higher root, looking out over the feet.
      const bool swap = height_of (a.root) > height_of (b.root);
      const mov::Trunk& foot = swap ? b : a;
      const mov::Trunk& head = swap ? a : b;
      // Each strap goes round its trunk where the trunk, leaning as it
      // does, passes the strap's height; a trunk is a little slimmer there
      // than at its foot.
      const auto up_trunk = [strap] (const mov::Trunk& trunk) {
        return trunk.root +
               shift (
                 trunk.axis *
                 ((strap - height_of (trunk.root)) /
                  std::max (0.5f, float (direction_value (trunk.axis)[1]))));
      };
      site.strap[0] = up_trunk (foot);
      site.strap[1] = up_trunk (head);
      site.girth[0] = 0.9f * foot.radius;
      site.girth[1] = 0.9f * head.radius;
      return true;
    }

    // A round bar from `a` to `b`, as triangles.
    void tube (render::DrawList& dl,
               const position_t& a,
               const position_t& b,
               meters_t radius,
               int sides) {
      const direction_t axis = direction_of (displacement_t (b - a));
      const auto level = cross (axis, world_up);
      const direction_t rim_x = length2 (level.numerical_value_in (one)) < 1e-6f
                                  ? unit_of (cross (axis, east))
                                  : unit_of (level);
      const direction_t rim_y = quantity_cast<direction> (cross (axis, rim_x));
      const auto put = [&] (const position_t& p, const direction_t& n) {
        dl.normal (n);
        dl.vertex (p + shift (n * radius));
      };
      for (int i = 0; i < sides; ++i) {
        const radians_t t0 = tau * i / sides;
        const radians_t t1 = tau * (i + 1) / sides;
        const direction_t n0 = rim_x * libm::cos (t0) + rim_y * libm::sin (t0);
        const direction_t n1 = rim_x * libm::cos (t1) + rim_y * libm::sin (t1);
        put (a, n0);
        put (b, n0);
        put (b, n1);
        put (a, n0);
        put (b, n1);
        put (a, n1);
        // The ends, closed.
        dl.normal (-axis);
        dl.vertex (a);
        dl.vertex (a + shift (n1 * radius));
        dl.vertex (a + shift (n0 * radius));
        dl.normal (axis);
        dl.vertex (b);
        dl.vertex (b + shift (n0 * radius));
        dl.vertex (b + shift (n1 * radius));
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
      const direction_t view = direction_of (displacement_t (camera - centre));
      const auto level = cross (axis, view);
      const direction_t side = length2 (level.numerical_value_in (one)) < 1e-8f
                                 ? unit_of (cross (east, view))
                                 : unit_of (level);
      const direction_t up = quantity_cast<direction> (cross (view, side));
      const proportion_t end_on =
        std::fabs (dot (axis, view).numerical_value_in (one)) * proportion[one];
      const meters_t along =
        half_length + as_length ((half_width - half_length) * end_on * end_on);
      const auto l = shift (up * along);
      const auto b = shift (side * half_width);
      dl.color (colour, alpha);
      dl.uv (0, 0);
      dl.vertex (centre - l - b);
      dl.uv (1, 0);
      dl.vertex (centre - l + b);
      dl.uv (1, 1);
      dl.vertex (centre + l + b);
      dl.uv (0, 1);
      dl.vertex (centre + l - b);
    }
  }

  position_t hammock_middle (const HammockSite& site) {
    using namespace camp_parts;
    return halfway (site.strap[0], site.strap[1]);
  }

  direction_t hammock_along (const HammockSite& site) {
    using namespace camp_parts;
    const displacement_t between = flat (site.strap[1] - site.strap[0]);
    return dot_m2 (between, between) > 1e-6f * u::m2 ? unit_of (between)
                                                     : north;
  }

  direction_t hammock_across (const HammockSite& site) {
    using namespace camp_parts;
    return quantity_cast<direction> (cross (hammock_along (site), world_up));
  }

  meters_t hammock_distance (const HammockSite& site, const position_t& feet) {
    using namespace camp_parts;
    const displacement_t between = flat (site.strap[1] - site.strap[0]);
    const square_meters_t apart2 =
      std::max (dot_m2 (between, between), 1e-6f * u::m2);
    const displacement_t to = flat (feet - site.strap[0]);
    const proportion_t t = std::clamp<proportion_t> (
      dot_m2 (to, between) / apart2, 0.0f * proportion[one], whole);
    return length_of (to - as_offset (between * t));
  }

  proportion_t hammock_load (const Camp& camp, play_time_t now) {
    using namespace camp_parts;
    // Getting in or out takes this long.
    constexpr seconds_t settling = 0.8f * u::s;
    const seconds_t since = seconds_between (now, camp.rest_time);
    const proportion_t eased = band (0.0f * u::s, settling, since);
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
        const displacement_t between = flat (b.root - a.root);
        const square_meters_t apart2 = dot_m2 (between, between);
        if (apart2 < 1.0f * u::m2 || apart2 > 49.0f * u::m2)
          continue;
        // Standing between them: along the middle of the line, and near it.
        const displacement_t to = flat (feet - a.root);
        const proportion_t t = dot_m2 (to, between) / apart2;
        if (t < 0.12f * proportion[one] || t > 0.88f * proportion[one])
          continue;
        const meters_t off = length_of (to - as_offset (between * t));
        if (off > 1.5f * u::m)
          continue;
        HammockSite site;
        if (!fits (a, b, trunks, ground, site))
          continue;
        const meters_t score =
          off +
          0.5f *
            std::fabs ((t - 0.5f * proportion[one]).numerical_value_in (one)) *
            sqrt (apart2);
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
      return east_of (a.root) < east_of (b.root);
    });
    constexpr meters_t window = hammock_most_span + 2.0f * most_girth;
    // The crown standing over a place: each tree round it by the depth of
    // its crown over its distance, found by a walk through the order from
    // where the place's x falls.
    const auto overhead = [&] (const position_t& middle) {
      scale_factor_t crown = 0.0f * scale_factor[one];
      const auto from = std::ranges::lower_bound (
        sorted, east_of (middle) - open_sky, {}, [] (const mov::Trunk& trunk) {
          return east_of (trunk.root);
        });
      for (auto trunk = from;
           trunk != sorted.end () &&
           east_of (trunk->root) <= east_of (middle) + open_sky;
           ++trunk)
        if (const meters_t away = length_of (flat (trunk->root - middle));
            trunk->height > 3.0f * u::m && away < open_sky)
          crown += quantity_cast<scale_factor> ((trunk->height - trunk->clear) /
                                                (away + 2.0f * u::m));
      return crown;
    };
    std::optional<HammockSite> best;
    meters_t best_distance = std::numeric_limits<float>::infinity () * u::m;
    std::size_t first = 0;
    for (std::size_t i = 0; i < sorted.size (); ++i) {
      const mov::Trunk& a = sorted[i];
      if (!carries (a))
        continue;
      while (east_of (sorted[first].root) < east_of (a.root) - window)
        ++first;
      std::size_t last = i;
      while (last + 1 < sorted.size () &&
             east_of (sorted[last + 1].root) <= east_of (a.root) + window)
        ++last;
      const std::span<const mov::Trunk> around (sorted.data () + first,
                                                last - first + 1);
      for (std::size_t j = i + 1; j <= last; ++j) {
        const mov::Trunk& b = sorted[j];
        const position_t middle = halfway (a.root, b.root);
        meters_t distance = length_of (flat (middle - near));
        if (distance >= best_distance)
          continue;
        HammockSite site;
        if (!fits (a, b, around, ground, site))
          continue;
        if (shade > 0.0f * u::m)
          distance += as_length (shade * overhead (middle));
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
      return east_of (a.root) < east_of (b.root);
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
      while (east_of (sorted[first].root) < east_of (a.root) - window)
        ++first;
      std::size_t last = i;
      while (last + 1 < sorted.size () &&
             east_of (sorted[last + 1].root) <= east_of (a.root) + window)
        ++last;
      const std::span<const mov::Trunk> around (sorted.data () + first,
                                                last - first + 1);
      for (std::size_t j = i + 1; j <= last; ++j) {
        const position_t middle = halfway (a.root, sorted[j].root);
        const meters_t distance = length_of (flat (middle - near));
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
        at (std::min (i + 1, along), j) - at (std::max (i - 1, 0), j);
      const displacement_t dv =
        at (i, std::min (j + 1, across)) - at (i, std::max (j - 1, 0));
      const auto square = cross (du, dv);
      if (length2 (square.numerical_value_in (u::m2)) < 1e-10f)
        return s.up;
      const direction_t n = unit_of (square);
      return dot (n, s.up).numerical_value_in (one) < 0.0f ? -n : n;
    };
    for (int j = 0; j < across; ++j) {
      dl.color (j == 0 || j == across - 1   ? hem
                : j == 2 || j == across - 3 ? stripe
                                            : canvas);
      for (int i = 0; i < along; ++i) {
        const auto put = [&] (int a, int b) {
          dl.normal (normal_at (a, b));
          dl.vertex (at (a, b));
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
        const direction_t n0 = compass (tau * i / sides);
        const direction_t n1 = compass (tau * (i + 1) / sides);
        const position_t low =
          site.strap[end] - shift (moppe::along (world_up, 0.03f * u::m));
        const position_t high =
          site.strap[end] + shift (moppe::along (world_up, 0.03f * u::m));
        const auto put = [&] (const position_t& p, const direction_t& n) {
          dl.normal (n);
          dl.vertex (p + shift (n * radius));
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
    const meters_t span = length_of (s.run);
    // The hips lie a little toward the foot from the lowest point, so the
    // whole body is cradled.
    const proportion_t at = 0.46f * proportion[one];
    const direction_t facing = s.normal (at + 0.04f * proportion[one]);
    const direction_t headward = s.tangent (at + 0.06f * proportion[one]);
    const direction_t right = unit_of (cross (headward, facing));
    const meters_t breath =
      0.006f * u::m * std::sin (seconds_value (time) * 1.3f);

    HoldPose hold;
    hold.facing = facing;
    hold.up = headward;
    // High enough in the cloth that the pack on their back stays in it.
    hold.pelvis = s.centre (at) + shift (along (s.normal (at), 0.21f * u::m));
    hold.lean = 0.16f * u::rad;
    // The head rests back; it turns only part of the way after the eyes.
    hold.gaze = direction_value (unit_of (facing + unit_of (gaze) * 0.7f));
    hold.head_up = direction_value (s.tangent (at + 0.22f * proportion[one]));
    const proportion_t cross_ankles = 0.86f * u::m / span;
    for (int i = 0; i < 2; ++i) {
      const float side = i == 0 ? -1.0f : 1.0f;
      hold.wrist[i] = hold.pelvis + shift (along (headward, 0.25f * u::m)) +
                      shift (along (facing, 0.13f * u::m + breath)) +
                      shift (along (right, side * 0.035f * u::m));
      hold.elbow_pole[i] = direction_value (right * side - facing * 0.4f);
      const proportion_t foot = at - cross_ankles;
      hold.ankle[i] =
        s.centre (foot) +
        shift (along (s.normal (foot), (0.10f + 0.05f * i) * u::m)) +
        shift (along (s.side, side * 0.03f * u::m));
      hold.knee_pole[i] = direction_value (facing + right * (side * 0.2f));
    }
    // The feet fall slack, toes up and a little away.
    const direction_t at_feet = s.normal (at - cross_ankles);
    const direction_t to_head = s.tangent (at - cross_ankles);
    hold.sole = unit_of (at_feet * 0.82f - to_head * 0.57f);
    hold.instep = unit_of (to_head * 0.82f + at_feet * 0.57f);
    return pose_holding (hold);
  }

  position_t resting_eye (const HammockSite& site, radians_t swing) {
    using namespace camp_parts;
    const Shape s = shape_of (site, whole, swing);
    const AvatarSkeleton k = pose_resting (site, swing, s.up, 0.0f * u::s);
    return k.head +
           shift (along (s.normal (0.6f * proportion[one]), 0.09f * u::m));
  }

  direction_t
  resting_gaze (const HammockSite& site, radians_t yaw, inclination_t pitch) {
    using namespace camp_parts;
    // Yaw turns from the foot of the hammock; a sleeper looks along
    // their own length and up.
    const direction_t ahead = -hammock_along (site);
    const direction_t right =
      quantity_cast<direction> (cross (ahead, world_up));
    return (ahead * libm::cos (yaw) + right * libm::sin (yaw)) *
             libm::cos (pitch) +
           world_up * libm::sin (pitch);
  }

  proportion_t fire_burn (const Camp& camp, play_time_t now) {
    using namespace camp_parts;
    // How long a fire takes to catch, and to die once put out.
    constexpr seconds_t catching = 3.5f * u::s;
    constexpr seconds_t dying = 1.5f * u::s;
    const seconds_t since = seconds_between (now, camp.fire_time);
    if (camp.fire)
      return 0.12f * proportion[one] +
             0.88f * band (0.0f * u::s, catching, since);
    return since < dying ? whole - band (0.0f * u::s, dying, since)
                         : 0.0f * proportion[one];
  }

  position_t fire_light_position (const Camp& camp) {
    using namespace camp_parts;
    return camp.hearth + shift (along (world_up, 0.55f * u::m));
  }

  DisplayColor fire_light_color (proportion_t burn, seconds_t time) {
    using namespace camp_parts;
    const float t = seconds_value (time);
    const proportion_t flicker = (0.80f + 0.10f * std::sin (t * 11.3f) +
                                  0.06f * std::sin (t * 23.7f + 1.3f) +
                                  0.04f * std::sin (t * 5.1f + 0.4f)) *
                                 proportion[one];
    // A display colour: the renderer's light is its 2.2 power.
    return scale_display (
      DisplayColor (1.50f, 1.04f, 0.56f),
      std::pow (std::clamp (as_share (burn * flicker).numerical_value_in (one),
                            0.0f,
                            1.0f),
                1.0f / 2.2f) *
        scale_factor[one]);
  }

  doneness_t pan_cooked (const Pan& pan, play_time_t now) {
    // Fried in nine seconds, kept until forty, burnt fourteen after.
    constexpr seconds_t frying = 9.0f * u::s;
    constexpr seconds_t keeping = 40.0f * u::s;
    constexpr seconds_t burning = 14.0f * u::s;
    const seconds_t age = seconds_between (now, pan.since);
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
    const displacement_t toward = flat (feet - hearth);
    const direction_t way =
      dot_m2 (toward, toward) > 1e-6f * u::m2 ? unit_of (toward) : north;
    Pan pan;
    pan.on = true;
    pan.place = hearth + shift (along (way, 0.36f * u::m)) +
                shift (along (world_up, 0.17f * u::m));
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
    render::DrawState solid;
    solid.cull = false;
    dl.state (solid);
    dl.lit (true);
    dl.fogged (true);
    dl.begin (render::Prim::Triangles);
    const auto round = [&] (int i, meters_t r, meters_t height) {
      return place + shift (along (compass (tau * i / rim), r)) +
             shift (along (world_up, height));
    };
    constexpr meters_t floor = 0.0f * u::m;
    // The floor, glistening a little, and the wall round it.
    dl.color (0.11f, 0.10f, 0.10f);
    dl.normal (world_up);
    for (int i = 0; i < rim; ++i) {
      dl.vertex (place);
      dl.vertex (round (i + 1, radius, floor));
      dl.vertex (round (i, radius, floor));
    }
    dl.color (0.07f, 0.07f, 0.075f);
    for (int i = 0; i < rim; ++i) {
      const radians_t a = tau * (i + 0.5f) / rim;
      dl.normal (
        unit_of (displacement (Vec3 (libm::cos (a), 0.4f, libm::sin (a)))));
      dl.vertex (round (i, radius, floor));
      dl.vertex (round (i + 1, radius, floor));
      dl.vertex (round (i + 1, radius * 1.1f, wall));
      dl.vertex (round (i, radius, floor));
      dl.vertex (round (i + 1, radius * 1.1f, wall));
      dl.vertex (round (i, radius * 1.1f, wall));
    }
    const direction_t out = compass (yaw);
    tube (dl,
          place + shift (along (out, radius * 1.05f)) +
            shift (along (world_up, wall)),
          place + shift (along (out, radius + 0.27f * u::m)) +
            shift (along (world_up, wall + 0.035f * u::m)),
          0.014f * u::m,
          6);

    // A fish lies across the pan, square to the handle, and the
    // mushrooms in what room it leaves.
    const Vec3& out_v = direction_value (out);
    const direction_t across =
      unit_direction (Vec3 (-out_v[2], 0.0f, out_v[0]));
    if (fish) {
      const meters_t drawn = 1.5f * fish->length;
      const scale_factor_t fit =
        std::min (1.0f * scale_factor[one],
                  quantity_cast<scale_factor> (2.0f * radius * 1.15f / drawn));
      draw_fish (dl,
                 *fish,
                 place -
                   shift (along (across, as_length (0.5f * drawn * fit))) -
                   shift (along (out, 0.03f * u::m)) +
                   shift (along (world_up, 0.035f * u::m)),
                 across,
                 out * -1.0f,
                 fit,
                 0.0f * wriggle[one],
                 cooked);
    }
    // Eggs broken into the middle, the mushrooms round them.
    for (int i = 0; i < eggs; ++i) {
      const radians_t around = tau * (i + 0.25f) / eggs + yaw;
      position_t at = place + shift (along (world_up, 0.004f * u::m));
      if (fish)
        at += shift (along (out, 0.09f * u::m) +
                     along (across, 0.1f * (i % 2 ? 1.0f : -1.0f) * u::m));
      else if (eggs > 1)
        at += shift (along (compass (around), 0.065f * u::m));
      draw_fried_egg (dl,
                      at,
                      around + 2.0f * u::rad,
                      cooked,
                      static_cast<std::uint32_t> (i * 7919 + 13));
    }
    for (std::size_t i = 0; i < mushrooms.size (); ++i) {
      MushroomSite lying = mushrooms[i];
      const radians_t around = tau * (i + 0.5f) / mushrooms.size () +
                               hash_lane (lying.seed, 31) * u::rad;
      const meters_t out_from_middle = eggs > 0                ? 0.125f * u::m
                                       : mushrooms.size () > 1 ? 0.07f * u::m
                                                               : 0.0f * u::m;
      lying.base = place + shift (along (compass (around), out_from_middle)) +
                   shift (along (world_up, 0.03f * u::m));
      if (fish)
        lying.base += shift (
          along (out, 0.09f * u::m) +
          along (across, 0.11f * ((i + eggs) % 2 ? 1.0f : -1.0f) * u::m));
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
    const auto seed = static_cast<std::uint32_t> (
      std::lround (centre[0] * 13.0f) * 7919 + std::lround (centre[2] * 13.0f));
    const auto up_by = [] (meters_t height) {
      return shift (along (world_up, height));
    };
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
      dl.translate (hearth + shift (along (compass (around), 0.43f * u::m)) +
                    up_by (size * 0.45f));
      dl.rotate (tau * hash_lane (seed, 70 + i), 0, 1, 0);
      dl.scale (1.25f, 0.72f, 0.95f);
      dl.sphere (size, 7, 5);
      dl.pop ();
    }

    dl.begin (render::Prim::Triangles);
    // The bed of ash.
    constexpr int rim = 14;
    dl.color (0.10f, 0.095f, 0.09f);
    dl.normal (world_up);
    const position_t bed = hearth + up_by (0.02f * u::m);
    for (int i = 0; i < rim; ++i) {
      const radians_t a0 = tau * i / rim;
      const radians_t a1 = tau * (i + 1) / rim;
      dl.vertex (bed);
      dl.vertex (bed + shift (along (compass (a1), 0.40f * u::m)));
      dl.vertex (bed + shift (along (compass (a0), 0.40f * u::m)));
    }
    // Split logs leaned together over it, and two laid across the bottom;
    // they char as the fire takes.
    const scale_factor_t charred =
      (1.0f - 0.55f * burn.numerical_value_in (one)) * scale_factor[one];
    constexpr int logs = 5;
    for (int i = 0; i < logs; ++i) {
      const radians_t around =
        tau * (i + 0.3f * hash_lane (seed, 90 + i)) / logs;
      const direction_t out = compass (around);
      const proportion_t bark = (0.20f + 0.08f * hash_lane (seed, 110 + i)) *
                                charred.numerical_value_in (one) *
                                proportion[one];
      dl.color (bark.numerical_value_in (one),
                (bark * 0.74f).numerical_value_in (one),
                (bark * 0.52f).numerical_value_in (one));
      tube (dl,
            hearth + shift (along (out, 0.29f * u::m)) + up_by (0.05f * u::m),
            hearth - shift (along (out, 0.07f * u::m)) + up_by (0.40f * u::m),
            0.042f * u::m,
            6);
    }
    dl.color ((0.17f * charred).numerical_value_in (one),
              (0.12f * charred).numerical_value_in (one),
              (0.085f * charred).numerical_value_in (one));
    tube (dl,
          hearth + shift (displacement (Vec3 (-0.26f, 0.06f, -0.10f))),
          hearth + shift (displacement (Vec3 (0.26f, 0.06f, 0.08f))),
          0.05f * u::m,
          6);
    tube (dl,
          hearth + shift (displacement (Vec3 (-0.08f, 0.07f, 0.25f))),
          hearth + shift (displacement (Vec3 (0.10f, 0.07f, -0.25f))),
          0.05f * u::m,
          6);
    dl.end ();

    // Embers under the logs glow by their own light.
    if (burn > 0.03f * proportion[one]) {
      dl.lit (false);
      dl.begin (render::Prim::Triangles);
      dl.color (burn.numerical_value_in (one),
                as_share (0.36f * burn * burn).numerical_value_in (one),
                as_share (0.07f * burn * burn).numerical_value_in (one));
      dl.normal (world_up);
      const position_t embers = hearth + up_by (0.035f * u::m);
      for (int i = 0; i < rim; ++i) {
        const radians_t a0 = tau * i / rim;
        const radians_t a1 = tau * (i + 1) / rim;
        dl.vertex (embers);
        dl.vertex (embers + shift (along (compass (a1), 0.22f * u::m)));
        dl.vertex (embers + shift (along (compass (a0), 0.22f * u::m)));
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
    const float clock = seconds_value (time);
    const auto up_by = [] (meters_t height) {
      return shift (along (world_up, height));
    };
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
                  hearth + up_by (0.45f * u::m),
                  world_up,
                  1.0f * u::m,
                  1.0f * u::m,
                  DisplayColor (1.0f, 0.42f, 0.12f),
                  0.15f * burn);
    flame_sprite (dl,
                  camera,
                  hearth + up_by (0.6f * u::m),
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
        along (compass (around), (tongue == 0 ? 0.0f : 0.11f) * u::m);
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
        const position_t middle = hearth + shift (foot * (1.0f - 0.7f * tv)) +
                                  up_by (0.12f * u::m + tv * height) +
                                  shift (lean * tv);
        const DisplayColor colour =
          t < 0.30f * proportion[one]   ? DisplayColor (1.0f, 0.88f, 0.55f)
          : t < 0.65f * proportion[one] ? DisplayColor (1.0f, 0.55f, 0.16f)
                                        : DisplayColor (0.95f, 0.26f, 0.07f);
        const proportion_t fade = as_share ((whole - t) * (whole - t));
        // The lean, doubled, tips the blob off the upright.
        flame_sprite (
          dl,
          camera,
          middle,
          unit_direction (normalized (direction_value (world_up) +
                                      displacement_value (lean) * 2.0f)),
          0.8f * height / blobs + 0.06f * u::m,
          (0.035f + 0.11f * (1.0f - 0.7f * tv)) * u::m,
          colour,
          as_share ((0.22f * proportion[one] + 0.50f * fade) * burn));
      }
    }
    // The white heart of it.
    for (int i = 0; i < 2; ++i)
      flame_sprite (dl,
                    camera,
                    hearth + up_by ((0.14f + 0.10f * i) * u::m),
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
    const float clock = seconds_value (time);
    const auto up_by = [] (meters_t height) {
      return shift (along (world_up, height));
    };
    const meters_t far = length_of (camera - hearth);
    // Near, the stack burns as several fires together.
    if (far < 220.0f * u::m) {
      draw_fire (renderer, hearth, camera, burn, time);
      for (int i = 0; i < 3; ++i) {
        const radians_t around = tau * i / 3.0f + 0.5f * u::rad;
        draw_fire (renderer,
                   hearth + shift (along (compass (around), 0.32f * u::m)),
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
      const meters_t rise = 2.0f * u::m + as_length (170.0f * u::m * t);
      // Each puff wanders off the column's line by its own swing, a way
      // of less than unit length.
      const direction_t wander = unit_direction (
        Vec3 (std::sin (i * 2.3f + clock * 0.1f), 0.0f, std::cos (i * 1.7f)));
      const position_t middle =
        hearth + up_by (rise) +
        shift (along (
          wind, as_length (rise * (0.25f * proportion[one] + 0.9f * t)))) +
        shift (along (wander, as_length (2.0f * t * rise * 0.05f)));
      const meters_t size = 1.0f * u::m + 22.0f * u::m * std::sqrt (tv);
      const proportion_t alpha =
        as_share (burn * (0.1f * proportion[one] + 0.4f * daylight) *
                  smoothstep (0.0f, 0.06f, tv) * (whole - t) * (whole - t));
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
      const proportion_t grow = band (40.0f * u::m, 400.0f * u::m, far);
      const meters_t size = 1.5f * u::m + 0.0045f * far;
      flame_sprite (dl,
                    camera,
                    hearth + up_by (1.2f * u::m + 0.002f * far),
                    world_up,
                    size * 1.5f,
                    size,
                    DisplayColor (1.0f, 0.55f, 0.18f),
                    as_share (burn * flicker * dark * grow * 0.9f));
      flame_sprite (dl,
                    camera,
                    hearth + up_by (1.5f * u::m + 0.002f * far),
                    world_up,
                    size * 4.0f,
                    size * 4.0f,
                    DisplayColor (1.0f, 0.38f, 0.10f),
                    as_share (burn * flicker * dark * grow * 0.18f));
      dl.end ();
    }
    dl.set_texture (nullptr);
    dl.state (render::DrawState ());
    renderer.draw_list (dl, 0x3100);
  }
}
