#include <moppe/game/hud.hh>
#include <moppe/render/renderer.hh>

#include <moppe/environment.hh>
#include <moppe/terrain/torus.hh>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace moppe {
  namespace game {
    using render::DrawList;
    using render::OutlineContour;
    using render::OutlinePoint;
    using render::SlugGlyph;
    using render::TextAlign;
    using render::TextStyle;

    namespace {
      constexpr float PI = 3.14159265f;

      // The keys each prompt names follow the platform's usual input: the
      // keyboard on the desktop, the controller layout on Apple TV, and no
      // key at all on a touch screen, where the prompt states the
      // situation instead.
#if defined(__APPLE__) && TARGET_OS_TV
      constexpr const char* deploy_key = "A";
      constexpr const char* mount_key = "B";
      constexpr const char* restart_key = "A";
      constexpr const char* ride_key = "Y";
      constexpr const char* dive_key = "LT";
      constexpr const char* hammock_key = "LB";
      constexpr const char* fire_key = "RB";
      constexpr const char* map_key = "";
#elif defined(__APPLE__) && TARGET_OS_IPHONE
      constexpr const char* deploy_key = "";
      constexpr const char* mount_key = "";
      constexpr const char* restart_key = "";
      constexpr const char* ride_key = "";
      constexpr const char* dive_key = "";
      constexpr const char* hammock_key = "";
      constexpr const char* fire_key = "";
      constexpr const char* map_key = "";
#else
      constexpr const char* deploy_key = "E";
      constexpr const char* mount_key = "F";
      constexpr const char* restart_key = "R";
      constexpr const char* ride_key = "Space";
      constexpr const char* dive_key = "Shift";
      constexpr const char* hammock_key = "R";
      constexpr const char* fire_key = "T";
      constexpr const char* map_key = "M";
#endif

      // The gauge: a hairline arc of radius gauge_radius, open at the
      // bottom, with a notch riding it from zero at lower left to full
      // scale at lower right.
      constexpr points_t gauge_radius = 30.0f * u::pt;
      constexpr points_t gauge_margin = 30.0f * u::pt;
      // A host's own buttons (platform::offer_actions) have the lower
      // right corner, under the thumb; the gauge and the basket's tally
      // then stand this far in from it.
      constexpr points_t thumb_room = 92.0f * u::pt;
      constexpr radians_t gauge_start = (225.0f * PI / 180.0f) * u::rad;
      constexpr radians_t gauge_sweep = (270.0f * PI / 180.0f) * u::rad;
      constexpr kilometres_per_hour_t gauge_full_scale =
        160.0f * (u::km / u::h);
      constexpr int reserve_dashes = 12;

      // The off-white every element is set in, black, and white.
      constexpr DisplayColor ink (0.96f, 0.96f, 0.96f);
      constexpr DisplayColor black (0.0f, 0.0f, 0.0f);
      constexpr DisplayColor white (1.0f, 1.0f, 1.0f);

      // The chart: its radius as a share of the frame's shorter side,
      // within bounds, its margin, and the bezel's outer radius in units
      // of it.
      constexpr proportion_t chart_share = 0.115f * proportion[one];
      constexpr points_t chart_least = 54.0f * u::pt;
      constexpr points_t chart_most = 88.0f * u::pt;
      constexpr points_t chart_margin = 24.0f * u::pt;
      constexpr scale_factor_t chart_bezel = 1.20f * scale_factor[one];
      // The ground it shows round the subject: a walk's worth, widening
      // with speed, and wider still from the air.
      constexpr meters_t chart_reach_foot = 220.0f * u::m;
      constexpr meters_t chart_reach_fast = 600.0f * u::m;
      constexpr meters_t chart_reach_air = 800.0f * u::m;

      // Faded in and out, and how faint a thing is to be drawn at all.
      constexpr opacity_t opaque = 1.0f * opacity[one];
      constexpr opacity_t transparent = 0.0f * opacity[one];
      constexpr opacity_t unseen = 0.002f * opacity[one];

      // Sets a text style's colour (TextStyle is a boundary of floats).
      void tint (TextStyle& style, DisplayColor colour) {
        style.red = colour.red;
        style.green = colour.green;
        style.blue = colour.blue;
      }

      // The chart's scale: points on the screen a metre of ground.
      using points_per_metre_t = quantity<display_point / si::metre, float>;

      struct ChartPlacement {
        points_t radius, outer, x, y;
      };

      ChartPlacement
      chart_placement (points_t width, points_t height, bool aside) {
        const points_t share =
          points_t (chart_share * std::min (width, height));
        const points_t r = std::clamp (share, chart_least, chart_most);
        const points_t outer = quantity_cast<display_length> (chart_bezel * r);
        return { r,
                 outer,
                 aside ? width - chart_margin - outer : chart_margin + outer,
                 aside ? chart_margin + outer + 16.0f * u::pt
                       : height - chart_margin - outer };
      }

      // Clamps a dimensionless reading (a proportion, an opacity) to 0..1.
      template <typename Q>
      Q clamp01 (Q value) {
        return std::clamp (
          value, Q (0.0f * Q::reference), Q (1.0f * Q::reference));
      }

      // Exponential approach, frame-rate independent: `rate` is the
      // fraction of the remaining distance closed per second's e-fold.
      template <typename Q>
      Q approach (Q value, Q target, damping_t rate, seconds_t dt) {
        return target + (value - target) *
                          std::exp ((-rate * dt).numerical_value_in (one));
      }

      // Smoothstep of a dimensionless reading clamped to 0..1.
      template <typename Q>
      Q ease (Q value) {
        const float t = clamp01 (value).numerical_value_in (one);
        return t * t * (3.0f - 2.0f * t) * Q::reference;
      }

      // How far a word said `age` ago shows: in over `rise`, held until
      // `hold`, and out over `fall`.
      opacity_t
      said (seconds_t age, seconds_t rise, seconds_t hold, seconds_t fall) {
        return clamp01 (opaque - (age - hold) / fall) * clamp01 (age / rise);
      }

      // Builds closed outlines from lines and circular arcs.  Every curve
      // starts exactly where the last ended, and the closing curve is
      // snapped onto the first point, so Slug's closed-contour invariant
      // holds without trusting trigonometry to round-trip.
      //
      // The outlines here and below are glyph outlines (render::
      // OutlinePoint, Slug's boundary of floats) in the shape's own unit,
      // which place_shape's scale maps to points; their angles are typed.
      class Pen {
      public:
        explicit Pen (OutlinePoint start) : m_start (start), m_at (start) {}

        void line (OutlinePoint to) {
          m_contour.push_back (render::outline_line (m_at, to));
          m_at = to;
        }

        // A circular arc about (cx, cy) from the current point, whose angle
        // is `from`, through `sweep`.  Quadratic segments of at most 30
        // degrees stay within 0.06% of the radius.
        void arc (
          float cx, float cy, float radius, radians_t from, radians_t sweep) {
          const int segments = std::max (
            1,
            (int)std::ceil (std::fabs (radians_value (sweep)) / (PI / 6.0f)));
          const radians_t step = sweep / segments;
          const float reach = radius / std::cos (radians_value (0.5f * step));
          for (int i = 0; i < segments; ++i) {
            const float mid = radians_value (from + (i + 0.5f) * step);
            const float end = radians_value (from + (i + 1) * step);
            const OutlinePoint control { cx + reach * std::cos (mid),
                                         cy + reach * std::sin (mid) };
            const OutlinePoint to { cx + radius * std::cos (end),
                                    cy + radius * std::sin (end) };
            m_contour.push_back ({ m_at, control, to });
            m_at = to;
          }
        }

        OutlineContour close () {
          const float gap = std::hypot (m_at.x - m_start.x, m_at.y - m_start.y);
          if (gap > 1e-4f || m_contour.empty ()) {
            line (m_start);
          } else {
            render::OutlineCurve& last = m_contour.back ();
            if (last.p2.x == last.p3.x && last.p2.y == last.p3.y)
              last.p2 = m_start;
            last.p3 = m_start;
          }
          return std::move (m_contour);
        }

      private:
        OutlinePoint m_start;
        OutlinePoint m_at;
        OutlineContour m_contour;
      };

      OutlinePoint polar (float radius, radians_t angle) {
        return { radius * std::cos (radians_value (angle)),
                 radius * std::sin (radians_value (angle)) };
      }

      constexpr radians_t half_turn = PI * u::rad;
      constexpr radians_t full_turn = (2.0f * PI) * u::rad;

      // A stroke of half-width `half` along the circle of radius `radius`,
      // from angle `from` through `sweep`, with round caps.
      OutlineContour
      stroked_arc (float radius, float half, radians_t from, radians_t sweep) {
        const radians_t turn = sweep >= 0.0f * u::rad ? half_turn : -half_turn;
        const radians_t to = from + sweep;
        Pen pen (polar (radius + half, from));
        pen.arc (0, 0, radius + half, from, sweep);
        const OutlinePoint end = polar (radius, to);
        pen.arc (end.x, end.y, half, to, turn);
        pen.arc (0, 0, radius - half, to, -sweep);
        const OutlinePoint start = polar (radius, from);
        pen.arc (start.x, start.y, half, from + half_turn, turn);
        return pen.close ();
      }

      OutlineContour disc (float radius) {
        Pen pen ({ radius, 0.0f });
        pen.arc (0, 0, radius, 0.0f * u::rad, full_turn);
        return pen.close ();
      }

      // A circle wound counterclockwise, or clockwise for a hole.
      OutlineContour circle (float radius, bool clockwise) {
        Pen pen ({ radius, 0.0f });
        pen.arc (
          0, 0, radius, 0.0f * u::rad, clockwise ? -full_turn : full_turn);
        return pen.close ();
      }

      // Side-on silhouettes, 24 by 18 points. The bicycle's open diamond,
      // the motorbike's tank, the tractor's big rear wheel, and the
      // helicopter's rotor must read without words or colour.
      std::vector<OutlineContour> vehicle_outline (unsigned vehicle) {
        std::vector<OutlineContour> outlines;
        const auto polygon = [&] (std::initializer_list<OutlinePoint> points) {
          Pen pen (*points.begin ());
          for (auto point = points.begin () + 1; point != points.end ();
               ++point)
            pen.line (*point);
          outlines.push_back (pen.close ());
        };
        const auto stroke = [&] (OutlinePoint a, OutlinePoint b, float width) {
          const float n = std::hypot (b.x - a.x, b.y - a.y);
          const float x = (b.y - a.y) * 0.5f * width / n;
          const float y = (a.x - b.x) * 0.5f * width / n;
          polygon ({ { a.x + x, a.y + y },
                     { b.x + x, b.y + y },
                     { b.x - x, b.y - y },
                     { a.x - x, a.y - y } });
        };
        const auto wheel = [&] (float x, float y, float radius, float width) {
          for (bool hole : { false, true }) {
            OutlineContour contour = circle (radius - (hole ? width : 0), hole);
            for (auto& curve : contour)
              for (OutlinePoint* p : { &curve.p1, &curve.p2, &curve.p3 }) {
                p->x += x;
                p->y += y;
              }
            outlines.push_back (std::move (contour));
          }
        };
        using Mark = HudState::Waymark;
        switch (vehicle) {
        case Mark::Bike:
          wheel (-7, -4, 4, 1.5f);
          wheel (7, -4, 4, 1.5f);
          stroke ({ -7, -4 }, { -2, 0 }, 1.8f);
          stroke ({ 7, -4 }, { 3, 5 }, 1.8f);
          stroke ({ 3, 5 }, { 6, 5 }, 1.7f);
          polygon ({ { -6, 0 }, { 1, -1 }, { 4, 2 }, { 1, 4 }, { -3, 3 } });
          stroke ({ -8, 4 }, { -2, 4 }, 2.2f);
          break;
        case Mark::MountainBike:
          wheel (-7, -4, 4, 1.3f);
          wheel (7, -4, 4, 1.3f);
          stroke ({ -7, -4 }, { -3, 3 }, 1.5f);
          stroke ({ -3, 3 }, { 0, -4 }, 1.5f);
          stroke ({ 0, -4 }, { -7, -4 }, 1.5f);
          stroke ({ -3, 3 }, { 4, 3 }, 1.5f);
          stroke ({ 4, 3 }, { 0, -4 }, 1.5f);
          stroke ({ 7, -4 }, { 3, 6 }, 1.5f);
          stroke ({ 3, 6 }, { 6, 6 }, 1.5f);
          stroke ({ -5, 5 }, { -1, 5 }, 1.8f);
          stroke ({ -3, 5 }, { -3, 3 }, 1.5f);
          break;
        case Mark::Jeep:
          wheel (-7, -5, 3.2f, 1.8f);
          wheel (7, -5, 3.2f, 1.8f);
          polygon ({ { -11, -3 },
                     { 11, -3 },
                     { 11, 1 },
                     { 5, 2 },
                     { 3, 7 },
                     { -9, 7 },
                     { -9, 1 },
                     { -11, 1 } });
          // Windows wind against the body, leaving two clear openings.
          polygon ({ { -7, 2 }, { -7, 5 }, { -3, 5 }, { -3, 2 } });
          polygon ({ { -1, 2 }, { -1, 5 }, { 2, 5 }, { 3, 2 } });
          break;
        case Mark::Caddy:
          wheel (-7, -5, 3, 1.6f);
          wheel (7, -5, 3, 1.6f);
          polygon ({ { -12, -3 },
                     { 12, -3 },
                     { 12, 0 },
                     { 8, 1 },
                     { 5, 6 },
                     { -1, 6 },
                     { -2, 0 },
                     { -12, 0 } });
          polygon ({ { 0, 1 }, { 1, 4 }, { 4, 4 }, { 6, 1 } });
          break;
        case Mark::Tractor:
          wheel (-6, -3, 5.3f, 2.7f);
          wheel (8, -5, 3.2f, 1.8f);
          polygon ({ { -2, -3 },
                     { 10, -3 },
                     { 10, 2 },
                     { 0, 2 },
                     { -1, 0 },
                     { -3, 0 } });
          stroke ({ 6, 2 }, { 6, 7 }, 2);
          stroke ({ -5, 3 }, { -1, 3 }, 2);
          stroke ({ -1, 1 }, { 1, 5 }, 1.5f);
          stroke ({ -1, 5 }, { 3, 5 }, 1.5f);
          break;
        case Mark::Helicopter:
          polygon ({ { -4, -2 },
                     { 7, -2 },
                     { 10, 0 },
                     { 8, 3 },
                     { 4, 5 },
                     { -2, 4 } });
          polygon ({ { 3, 0 }, { 3, 3 }, { 5, 3 }, { 7, 0 } });
          stroke ({ -3, 1 }, { -11, 3 }, 2);
          stroke ({ -11, 1 }, { -11, 6 }, 1.7f);
          stroke ({ 0, 4 }, { 0, 7 }, 1.5f);
          stroke ({ -10, 7 }, { 11, 7 }, 1.5f);
          stroke ({ -2, -2 }, { -3, -5 }, 1.5f);
          stroke ({ 5, -2 }, { 6, -5 }, 1.5f);
          stroke ({ -6, -5 }, { 10, -5 }, 1.7f);
          break;
        case Mark::LongJohn:
          // The big wheel behind, the box low between, the small wheel
          // ahead, and the saddle and bars over the back.
          wheel (-9, -3, 4, 1.3f);
          wheel (10, -4, 3, 1.2f);
          polygon ({ { -3, -6 }, { 7, -6 }, { 8, -1 }, { -3, -1 } });
          stroke ({ -9, -3 }, { -3, -3 }, 1.5f);
          stroke ({ -4, -3 }, { -7, 4 }, 1.5f);
          stroke ({ -10, 5 }, { -5, 5 }, 1.8f);
          stroke ({ -2, -1 }, { -2, 5 }, 1.5f);
          stroke ({ -3, 6 }, { 1, 6 }, 1.5f);
          stroke ({ 8, -1 }, { 10, -4 }, 1.4f);
          break;
        case Mark::Boat:
          polygon ({ { -9, 0 }, { 12, 2 }, { 8, -5 }, { -6, -5 } });
          stroke ({ -9, 2 }, { 12, 4 }, 1.4f);
          polygon ({ { -7, 3 }, { -2, 3 }, { -2, 7 }, { -7, 7 } });
          stroke ({ -5, 4 }, { -11, -7 }, 1.4f);
          break;
        case Mark::Snowmobile:
          polygon ({ { -10, 0 },
                     { 11, 1 },
                     { 5, 6 },
                     { 1, 6 },
                     { -1, 3 },
                     { -9, 3 } });
          stroke ({ -8, -2 }, { 1, -2 }, 4.0f);
          stroke ({ 6, 0 }, { 7, -4 }, 1.6f);
          stroke ({ 3, -4 }, { 12, -4 }, 1.6f);
          stroke ({ 12, -4 }, { 14, -2 }, 1.5f);
          break;
        case Mark::JetSki:
          // A short deep hull with a raked bow, the cowl rising to the
          // bars, and the saddle behind it.
          polygon ({ { -10, 0 }, { 11, 1 }, { 6, -5 }, { -8, -5 } });
          polygon ({ { -1, 1 }, { 8, 1 }, { 2, 6 }, { 0, 6 } });
          stroke ({ -9, 2.5f }, { -1, 2.5f }, 2.6f);
          stroke ({ 1, 6 }, { -1, 9 }, 1.5f);
          stroke ({ -3, 9 }, { 1, 9 }, 1.5f);
          break;
        }
        return outlines;
      }

      // A radial capsule from `inner` to `outer` along +x, `half` wide.
      OutlineContour capsule (float inner, float outer, float half) {
        Pen pen ({ inner, -half });
        pen.line ({ outer, -half });
        pen.arc (outer, 0, half, (-0.5f * PI) * u::rad, half_turn);
        pen.line ({ inner, half });
        pen.arc (inner, 0, half, (0.5f * PI) * u::rad, half_turn);
        return pen.close ();
      }

      // A corner of the chart's picture: where it falls on the screen and
      // where on the sheet, in periods of it (so that the integers are its
      // seams). Floats, as the DrawList's vertices and texture coordinates
      // they become are: the polygons below are cut and laid down at that
      // boundary.
      struct ChartCorner {
        float x, y, u, v;
      };

      struct ChartPolygon {
        std::array<ChartCorner, 12> corners;
        int count = 0;
      };

      // Splits a convex polygon where u (or v) crosses `seam`.
      void split_polygon (const ChartPolygon& whole,
                          bool along_u,
                          float seam,
                          ChartPolygon& below,
                          ChartPolygon& above) {
        below.count = above.count = 0;
        const auto key = [&] (const ChartCorner& c) {
          return along_u ? c.u : c.v;
        };
        for (int i = 0; i < whole.count; ++i) {
          const ChartCorner& a = whole.corners[i];
          const ChartCorner& b = whole.corners[(i + 1) % whole.count];
          const float ka = key (a) - seam, kb = key (b) - seam;
          if (ka <= 0.0f)
            below.corners[below.count++] = a;
          if (ka >= 0.0f)
            above.corners[above.count++] = a;
          if ((ka < 0.0f && kb > 0.0f) || (ka > 0.0f && kb < 0.0f)) {
            const float t = ka / (ka - kb);
            const ChartCorner cut { a.x + (b.x - a.x) * t,
                                    a.y + (b.y - a.y) * t,
                                    a.u + (b.u - a.u) * t,
                                    a.v + (b.v - a.v) * t };
            below.corners[below.count++] = cut;
            above.corners[above.count++] = cut;
          }
        }
      }

      // Lays a polygon of the picture down, cut at the sheet's seams so
      // that each piece samples one copy of it: the world repeats, but
      // the HUD's sampler clamps.
      void chart_polygon (DrawList& dl, const ChartPolygon& polygon) {
        if (polygon.count < 3)
          return;
        float low_u = polygon.corners[0].u, high_u = low_u;
        float low_v = polygon.corners[0].v, high_v = low_v;
        for (int i = 1; i < polygon.count; ++i) {
          low_u = std::min (low_u, polygon.corners[i].u);
          high_u = std::max (high_u, polygon.corners[i].u);
          low_v = std::min (low_v, polygon.corners[i].v);
          high_v = std::max (high_v, polygon.corners[i].v);
        }
        ChartPolygon below, above;
        if (std::floor (low_u) + 1.0f < high_u) {
          split_polygon (
            polygon, true, std::floor (low_u) + 1.0f, below, above);
          chart_polygon (dl, below);
          chart_polygon (dl, above);
          return;
        }
        if (std::floor (low_v) + 1.0f < high_v) {
          split_polygon (
            polygon, false, std::floor (low_v) + 1.0f, below, above);
          chart_polygon (dl, below);
          chart_polygon (dl, above);
          return;
        }
        const float tile_u = std::floor (0.5f * (low_u + high_u));
        const float tile_v = std::floor (0.5f * (low_v + high_v));
        for (int i = 1; i + 1 < polygon.count; ++i)
          for (const int k : { 0, i, i + 1 }) {
            const ChartCorner& c = polygon.corners[k];
            dl.uv (c.u - tile_u, c.v - tile_v);
            dl.vertex (c.x, c.y);
          }
      }

      // A rounded rectangle centred on the origin, wound counterclockwise
      // or, for a hole, clockwise.
      OutlineContour
      rounded_rect (float width, float height, float corner, bool clockwise) {
        const float x = 0.5f * width - corner;
        const float y = 0.5f * height - corner;
        const radians_t quarter = (0.5f * PI) * u::rad;
        if (!clockwise) {
          Pen pen ({ x + corner, -y });
          pen.line ({ x + corner, y });
          pen.arc (x, y, corner, 0.0f * u::rad, quarter);
          pen.line ({ -x, y + corner });
          pen.arc (-x, y, corner, quarter, quarter);
          pen.line ({ -x - corner, -y });
          pen.arc (-x, -y, corner, 2.0f * quarter, quarter);
          pen.line ({ x, -y - corner });
          pen.arc (x, -y, corner, 3.0f * quarter, quarter);
          return pen.close ();
        }
        Pen pen ({ x + corner, -y });
        pen.arc (x, -y, corner, 0.0f * u::rad, -quarter);
        pen.line ({ -x, -y - corner });
        pen.arc (-x, -y, corner, -quarter, -quarter);
        pen.line ({ -x - corner, y });
        pen.arc (-x, y, corner, -2.0f * quarter, -quarter);
        pen.line ({ x, y + corner });
        pen.arc (x, y, corner, -3.0f * quarter, -quarter);
        return pen.close ();
      }

      void hud_state (DrawList& dl) {
        render::DrawState s;
        s.blend = true;
        s.depth_test = false;
        s.depth_write = false;
        s.cull = false;
        dl.state (s);
        dl.lit (false);
        dl.fogged (false);
      }
    }

    std::vector<unsigned>
    group_waymarks (std::span<const HudState::Waymark> marks,
                    const std::function<HudBox (unsigned)>& box,
                    std::span<const unsigned> before) {
      std::vector<unsigned> groups;
      for (unsigned i = 0; i < marks.size () && i < 32; ++i)
        if (marks[i].alpha > unseen)
          groups.push_back (1u << i);
      const auto who = [&] (unsigned group) {
        unsigned members = 0;
        for (unsigned i = 0; i < marks.size (); ++i)
          if (group & (1u << i))
            members |= marks[i].who;
        return members;
      };
      // A gap that closes to a few points joins two markers; once
      // joined, they part only when a wider one opens.
      const auto gap = [&] (unsigned a, unsigned b) {
        const unsigned p = who (a), q = who (b);
        return std::ranges::any_of (
                 before, [&] (unsigned g) { return (g & p) && (g & q); })
                 ? 30.0f * u::pt
                 : 6.0f * u::pt;
      };
      for (bool joined = true; joined;) {
        joined = false;
        for (std::size_t a = 0; a < groups.size () && !joined; ++a)
          for (std::size_t b = a + 1; b < groups.size () && !joined; ++b) {
            if (marks[std::countr_zero (groups[a])].in_view !=
                marks[std::countr_zero (groups[b])].in_view)
              continue;
            const HudBox p = box (groups[a]), q = box (groups[b]);
            const points_t g = gap (groups[a], groups[b]);
            if (p[0] < q[2] + g && q[0] < p[2] + g && p[1] < q[3] + g &&
                q[1] < p[3] + g) {
              groups[a] |= groups[b];
              groups.erase (groups.begin () + static_cast<long> (b));
              joined = true;
            }
          }
      }
      return groups;
    }

    // The gauge and prompt outlines, in units of the gauge radius (gauge
    // shapes) or of the prompt's em (keycaps).
    struct Hud::Shape {
      SlugGlyph track;
      SlugGlyph notch;
      SlugGlyph dash;
      SlugGlyph arrow;
      SlugGlyph shade;
      SlugGlyph key;
      SlugGlyph wide_key;
      std::array<SlugGlyph, machine_count> vehicles;
      // The chart's, in units of its radius: the bezel round it, the
      // hairline rim over the picture's edge, and the bezel's ticks.
      SlugGlyph bezel;
      SlugGlyph rim;
      SlugGlyph tick;
      SlugGlyph major_tick;
    };

    Hud::~Hud () = default;

    Hud::Hud () : m_diagnostics (false) {
      if (const char* mode = moppe::environment ("MOPPE_HUD")) {
        m_diagnostics = std::string (mode) == "debug";
        m_hidden = std::string (mode) == "none";
      }
    }

    void Hud::load (render::Renderer& renderer) {
      try {
        m_font = render::Font::load (renderer, "fonts/Geomini-Regular.ttf");
      } catch (const std::exception& error) {
        std::cerr << "moppe: HUD font unavailable: " << error.what () << '\n';
        m_font.reset ();
      }

      render::SlugAtlasBuilder builder;
      m_shape = std::make_unique<Shape> ();
      const auto add = [&] (std::vector<OutlineContour> contours) {
        return builder.add (contours);
      };
      // In units of the gauge's radius.
      const float hairline =
        (0.55f * u::pt / gauge_radius).numerical_value_in (one);
      m_shape->track =
        add ({ stroked_arc (1.0f, hairline, gauge_start, -gauge_sweep) });
      {
        // A short radial capsule across the track, pointing along +x and
        // rotated into place when drawn.
        const float half =
          (1.15f * u::pt / gauge_radius).numerical_value_in (one);
        const float inner = 0.89f, outer = 1.14f;
        Pen pen ({ inner, -half });
        pen.line ({ outer, -half });
        pen.arc (outer, 0, half, (-0.5f * PI) * u::rad, half_turn);
        pen.line ({ inner, half });
        pen.arc (inner, 0, half, (0.5f * PI) * u::rad, half_turn);
        m_shape->notch = add ({ pen.close () });
      }
      {
        const radians_t slot = gauge_sweep / reserve_dashes;
        const radians_t sweep = slot - (7.0f * PI / 180.0f) * u::rad;
        const float half =
          (0.45f * u::pt / gauge_radius).numerical_value_in (one);
        m_shape->dash =
          add ({ stroked_arc (0.8f, half, -0.5f * sweep, sweep) });
      }
      {
        // A waymark's arrowhead, pointing along +x, notched behind.
        Pen pen ({ 1.0f, 0.0f });
        pen.line ({ -0.62f, 0.78f });
        pen.line ({ -0.22f, 0.0f });
        pen.line ({ -0.62f, -0.78f });
        m_shape->arrow = add ({ pen.close () });
      }
      m_shape->shade = add ({ disc (1.0f) });
      m_shape->key = add ({ rounded_rect (1.55f, 1.55f, 0.32f, false),
                            rounded_rect (1.43f, 1.43f, 0.26f, true) });
      m_shape->wide_key = add ({ rounded_rect (3.4f, 1.55f, 0.32f, false),
                                 rounded_rect (3.28f, 1.43f, 0.26f, true) });
      m_shape->bezel =
        add ({ circle (chart_bezel.numerical_value_in (one), false),
               circle (0.995f, true) });
      m_shape->rim = add ({ circle (1.013f, false), circle (0.990f, true) });
      m_shape->tick = add ({ capsule (1.045f, 1.085f, 0.0085f) });
      m_shape->major_tick = add ({ capsule (1.035f, 1.125f, 0.011f) });
      for (unsigned i = 0; i < m_shape->vehicles.size (); ++i)
        m_shape->vehicles[i] = add (vehicle_outline (1u << i));
      m_shapes = renderer.create_glyph_set (builder.data ());
    }

    void Hud::place_shape (render::TextList& text,
                           const SlugGlyph& shape,
                           points_t cx,
                           points_t cy,
                           points_t scale,
                           radians_t angle,
                           DisplayColor colour,
                           opacity_t alpha,
                           points_t softness) const {
      if (!m_shapes || shape.empty () || alpha <= unseen)
        return;
      // Shapes are authored y-up; the HUD is y-down, so a counterclockwise
      // turn on screen negates the y components.
      const float c = std::cos (radians_value (angle));
      const float s = std::sin (radians_value (angle));
      const float size = points_value (scale);
      render::GlyphQuad quad {};
      quad.origin[0] = points_value (cx);
      quad.origin[1] = points_value (cy);
      quad.origin[3] = points_value (softness);
      quad.axis_x[0] = size * c;
      quad.axis_x[1] = -size * s;
      quad.axis_y[0] = -size * s;
      quad.axis_y[1] = -size * c;
      quad.bounds[0] = shape.min_x;
      quad.bounds[1] = shape.min_y;
      quad.bounds[2] = shape.max_x;
      quad.bounds[3] = shape.max_y;
      quad.color[0] = colour.red;
      quad.color[1] = colour.green;
      quad.color[2] = colour.blue;
      quad.color[3] = opacity_value (alpha);
      quad.glyph[0] = shape.band_offset;
      quad.glyph[1] = shape.horizontal_bands;
      quad.glyph[2] = shape.vertical_bands;
      text.add (m_shapes, quad);
    }

    void Hud::draw_shaded (render::TextList& text,
                           points_t x,
                           points_t y,
                           const std::string& line,
                           const TextStyle& style,
                           TextAlign align) const {
      if (!m_font || style.alpha <= opacity_value (unseen))
        return;
      // The style's fields and the font's coordinates are floats, the
      // text layout's boundary.
      TextStyle shade = style;
      tint (shade, black);
      shade.alpha = 0.32f * style.alpha;
      shade.softness = std::max (3.0f, 0.22f * style.size);
      m_font->draw (text,
                    points_value (x),
                    points_value (y) + 0.04f * style.size,
                    line,
                    shade,
                    align);
      m_font->draw (
        text, points_value (x), points_value (y), line, style, align);
    }

    void Hud::draw_prompt (render::TextList& text,
                           const std::string& key,
                           const std::string& action,
                           opacity_t alpha,
                           points_t width,
                           points_t height) const {
      if (!m_font || alpha <= unseen)
        return;
      const TextStyle words = prompt_words (alpha);
      draw_keyed (text,
                  0.5f * (width - keyed_width (key, action, words)),
                  height - 54.0f * u::pt,
                  key,
                  action,
                  words,
                  alpha);
    }

    TextStyle Hud::prompt_words (opacity_t alpha) const {
      TextStyle words;
      words.size = 15.0f;
      tint (words, ink);
      words.alpha = opacity_value (0.92f * alpha);
      words.tracking = 0.02f;
      return words;
    }

    points_t Hud::keyed_width (const std::string& key,
                               const std::string& action,
                               const TextStyle& words) const {
      if (!m_font)
        return 0.0f * u::pt;
      const points_t em = words.size * u::pt;
      const points_t key_width =
        key.empty () ? 0.0f * u::pt : (key.size () > 1 ? 3.4f : 1.55f) * em;
      const points_t gap = key.empty () ? 0.0f * u::pt : 0.6f * em;
      return key_width + gap + m_font->measure (action, words) * u::pt;
    }

    void Hud::draw_keyed (render::TextList& text,
                          points_t x,
                          points_t baseline,
                          const std::string& key,
                          const std::string& action,
                          const TextStyle& words,
                          opacity_t alpha) const {
      if (!m_font || alpha <= unseen)
        return;
      TextStyle cap = words;
      cap.size = (11.5f / 15.0f) * words.size;
      cap.tracking = 0.04f;
      cap.alpha = opacity_value (0.86f * alpha);

      const bool has_key = !key.empty ();
      const bool wide = key.size () > 1;
      const points_t key_em = words.size * u::pt;
      const points_t key_width =
        has_key ? (wide ? 3.4f : 1.55f) * key_em : 0.0f * u::pt;
      const points_t gap = has_key ? 0.6f * key_em : 0.0f * u::pt;

      if (has_key) {
        // The keycap sits on the text's optical centre line.
        const points_t cx = x + 0.5f * key_width;
        const points_t cy =
          baseline - 0.5f * m_font->cap_height (words.size) * u::pt;
        const SlugGlyph& outline = wide ? m_shape->wide_key : m_shape->key;
        place_shape (text,
                     outline,
                     cx,
                     cy,
                     key_em,
                     0.0f * u::rad,
                     black,
                     0.28f * alpha,
                     6.0f * u::pt);
        place_shape (
          text, outline, cx, cy, key_em, 0.0f * u::rad, ink, 0.7f * alpha);
        draw_shaded (text,
                     cx,
                     cy + 0.5f * m_font->cap_height (cap.size) * u::pt,
                     key,
                     cap,
                     TextAlign::Center);
        x += key_width + gap;
      }
      draw_shaded (text, x, baseline, action, words);
    }

    void Hud::draw_gauge (render::TextList& text,
                          const HudState& st,
                          seconds_t dt,
                          points_t width,
                          points_t height) {
      const bool instrument = !st.on_foot;
      m_gauge_alpha = approach (
        m_gauge_alpha, instrument ? opaque : transparent, 5.0f / u::s, dt);

      // The reserve shows only while it is spent or refilling, and lingers
      // a moment after it is full again.
      const proportion_t charge = clamp01 (st.boost_ready);
      const bool spending =
        instrument && !st.gliding && charge < 0.995f * proportion[one];
      m_reserve_hold =
        spending ? 1.2f * u::s : std::max (0.0f * u::s, m_reserve_hold - dt);
      m_reserve_alpha =
        approach (m_reserve_alpha,
                  m_reserve_hold > 0.0f * u::s ? opaque : transparent,
                  4.0f / u::s,
                  dt);

      const opacity_t alpha = ease (m_gauge_alpha);
      if (alpha <= unseen || !m_font)
        return;

      const points_t r = gauge_radius;
      const points_t cx =
        width - gauge_margin - r - (m_host_offers ? thumb_room : 0.0f * u::pt);
      const points_t cy = height - gauge_margin - r;

      // A broad, faint shade under the instrument keeps it legible over
      // snowfields without drawing a panel.
      place_shape (text,
                   m_shape->shade,
                   cx,
                   cy + 2.0f * u::pt,
                   1.25f * r,
                   0.0f * u::rad,
                   black,
                   0.20f * alpha,
                   60.0f * u::pt);

      place_shape (
        text, m_shape->track, cx, cy, r, 0.0f * u::rad, ink, 0.30f * alpha);

      const kilometres_per_hour_t kmh =
        std::max (0.0f * (u::km / u::h), st.speed);
      const proportion_t fraction = clamp01 (kmh / gauge_full_scale);
      const radians_t angle = gauge_start - radians_t (gauge_sweep * fraction);
      place_shape (text,
                   m_shape->notch,
                   cx,
                   cy,
                   r,
                   angle,
                   black,
                   0.35f * alpha,
                   4.0f * u::pt);
      place_shape (text, m_shape->notch, cx, cy, r, angle, ink, 0.95f * alpha);

      if (m_reserve_alpha > unseen) {
        const radians_t slot = gauge_sweep / reserve_dashes;
        const proportion_t filled = charge * reserve_dashes;
        for (int i = 0; i < reserve_dashes; ++i) {
          const proportion_t lit = clamp01 (filled - i * proportion[one]);
          const opacity_t a = quantity_cast<opacity> (
            (0.07f * proportion[one] + 0.36f * lit) * m_reserve_alpha * alpha);
          place_shape (text,
                       m_shape->dash,
                       cx,
                       cy,
                       r,
                       gauge_start - (i + 0.5f) * slot,
                       ink,
                       a);
        }
      }

      TextStyle numeral;
      numeral.size = 25.0f;
      tint (numeral, ink);
      numeral.alpha = opacity_value (0.94f * alpha);
      numeral.tabular_figures = true;
      const std::string speed = std::to_string (
        (int)std::lround (kmh.numerical_value_in (u::km / u::h)));
      draw_shaded (text,
                   cx,
                   cy + 0.5f * m_font->cap_height (numeral.size) * u::pt,
                   speed,
                   numeral,
                   TextAlign::Center);

      TextStyle unit;
      unit.size = 8.5f;
      tint (unit, ink);
      unit.alpha = opacity_value (0.55f * alpha);
      unit.tracking = 0.08f;
      draw_shaded (text, cx, cy + 0.80f * r, "km/h", unit, TextAlign::Center);

      if (st.gliding) {
        char climb[24];
        const float climbing = speed_value (st.vertical_speed);
        std::snprintf (climb,
                       sizeof climb,
                       "%+.1f m/s",
                       std::fabs (climbing) < 0.05f ? 0.0f : climbing);
        TextStyle vario = unit;
        vario.size = 10.0f;
        vario.alpha = opacity_value (0.75f * alpha);
        vario.tracking = 0.02f;
        vario.tabular_figures = true;
        draw_shaded (
          text, cx, cy - r - 12.0f * u::pt, climb, vario, TextAlign::Center);
      }

      // A wheelie counts its metres over the gauge as it goes, and the
      // one just ridden lingers there a moment with what it scored.
      constexpr meters_t counted = 3.0f * u::m;
      const bool wheeling = st.wheelie >= counted;
      const meters_t ridden = wheeling ? st.wheelie : st.last_wheelie;
      const proportion_t linger =
        wheeling ? 1.0f * proportion[one]
                 : clamp01 (1.0f * proportion[one] -
                            (st.wheelie_age - 1.6f * u::s) / (0.6f * u::s)) *
                     (st.last_wheelie >= counted ? 1.0f : 0.0f);
      if (!st.gliding && linger > 0.002f * proportion[one]) {
        char line[40];
        if (!wheeling && st.last_wheelie_points > 0)
          std::snprintf (line,
                         sizeof line,
                         "wheelie %.0f m  +%d",
                         meters_value (ridden),
                         st.last_wheelie_points);
        else
          std::snprintf (
            line, sizeof line, "wheelie %.0f m", meters_value (ridden));
        TextStyle tally = unit;
        tally.size = 10.0f;
        tally.alpha =
          opacity_value (quantity_cast<opacity> (0.8f * alpha * ease (linger)));
        tally.tracking = 0.02f;
        tally.tabular_figures = true;
        draw_shaded (
          text, cx, cy - r - 12.0f * u::pt, line, tally, TextAlign::Center);
      }
    }

    void Hud::draw_basket (render::TextList& text,
                           const HudState& st,
                           seconds_t dt,
                           points_t width,
                           points_t height) {
      if (!m_font)
        return;
      // The tally takes the gauge's corner while walking with a basket that
      // has something in it.
      const bool showing =
        st.on_foot && (st.basket_total > 0 || st.fish_total > 0);
      m_basket_alpha = approach (
        m_basket_alpha, showing ? opaque : transparent, 4.0f / u::s, dt);
      const opacity_t alpha = ease (m_basket_alpha);
      // Where the words of a moment stand, risen as they come in.
      const auto cheered = [&] (opacity_t shown) {
        return height - 96.0f * u::pt -
               points_t (10.0f * u::pt * (opaque - ease (shown)));
      };
      // A pick is celebrated for a moment just above the prompt.
      const opacity_t fresh =
        said (st.reached_age, 0.12f * u::s, 1.4f * u::s, 0.8f * u::s);
      if (fresh > unseen && st.reached_name) {
        TextStyle cheer;
        cheer.size = st.reached_refused ? 15.0f : 21.0f;
        tint (cheer, ink);
        cheer.alpha = opacity_value (0.95f * ease (fresh));
        cheer.tracking = 0.02f;
        const std::string name (st.reached_name);
        const std::string line =
          st.reached_refused ? "Not that one -- the " + name + " is poisonous!"
                             : (name == "egg" ? "An " : "A ") + name + "!";
        draw_shaded (
          text, 0.5f * width, cheered (fresh), line, cheer, TextAlign::Center);
      }
      // A word on supper, where a pick is celebrated.
      const opacity_t sated =
        said (st.meal_age, 0.25f * u::s, 3.2f * u::s, 1.0f * u::s);
      if (sated > unseen && st.meal) {
        TextStyle word;
        word.size = 19.0f;
        tint (word, ink);
        word.alpha = opacity_value (0.95f * ease (sated));
        word.tracking = 0.02f;
        draw_shaded (text,
                     0.5f * width,
                     cheered (sated),
                     st.meal,
                     word,
                     TextAlign::Center);
      }
      // A fish landed, or lost, where a pick is celebrated.
      const opacity_t caught =
        said (st.catch_age, 0.2f * u::s, 3.0f * u::s, 1.0f * u::s);
      if (caught > unseen && st.catch_note) {
        TextStyle word;
        word.size = 21.0f;
        tint (word, ink);
        word.alpha = opacity_value (0.95f * ease (caught));
        word.tracking = 0.02f;
        draw_shaded (text,
                     0.5f * width,
                     cheered (caught),
                     st.catch_note,
                     word,
                     TextAlign::Center);
      }
      // News of the other player, small at the top of the frame.
      const opacity_t told =
        said (st.notice_age, 0.4f * u::s, 4.0f * u::s, 1.2f * u::s);
      if (told > unseen && st.notice) {
        TextStyle news;
        news.size = 15.0f;
        tint (news, ink);
        news.alpha = opacity_value (0.9f * ease (told));
        news.tracking = 0.04f;
        draw_shaded (text,
                     0.5f * width,
                     54.0f * u::pt,
                     st.notice,
                     news,
                     TextAlign::Center);
      }
      if (alpha <= unseen)
        return;
      const points_t cx = width - gauge_margin - gauge_radius -
                          (m_host_offers ? thumb_room : 0.0f * u::pt);
      const points_t cy = height - gauge_margin - gauge_radius;
      TextStyle numeral;
      numeral.size = 25.0f;
      tint (numeral, ink);
      numeral.alpha = opacity_value (0.94f * alpha);
      numeral.tabular_figures = true;
      draw_shaded (
        text,
        cx,
        cy + 0.5f * m_font->cap_height (numeral.size) * u::pt,
        std::to_string (st.basket_total > 0 ? st.basket_total : st.fish_total),
        numeral,
        TextAlign::Center);
      TextStyle unit;
      unit.size = 8.5f;
      tint (unit, ink);
      unit.alpha = opacity_value (0.55f * alpha);
      unit.tracking = 0.08f;
      draw_shaded (text,
                   cx,
                   cy + 0.80f * gauge_radius,
                   st.basket_total > 0 ? "in the basket" : "fish caught",
                   unit,
                   TextAlign::Center);
      constexpr points_t leading = 11.0f * u::pt;
      points_t below = leading;
      if (st.basket_eggs > 0 && st.basket_eggs < st.basket_total) {
        draw_shaded (text,
                     cx,
                     cy + 0.80f * gauge_radius + below,
                     std::to_string (st.basket_eggs) +
                       (st.basket_eggs > 1 ? " of them eggs" : " an egg"),
                     unit,
                     TextAlign::Center);
        below += leading;
      }
      if (st.basket_total > 0 && st.fish_total > 0)
        draw_shaded (text,
                     cx,
                     cy + 0.80f * gauge_radius + below,
                     "and " + std::to_string (st.fish_total) + " fish",
                     unit,
                     TextAlign::Center);
    }

    // Someone lying in the hammock watches the hours go by: the time of
    // day, small, over the prompt.
    void Hud::draw_clock (render::TextList& text,
                          const HudState& st,
                          seconds_t dt,
                          points_t width,
                          points_t height) {
      m_clock_alpha = approach (
        m_clock_alpha, st.resting ? opaque : transparent, 1.5f / u::s, dt);
      const opacity_t alpha = ease (m_clock_alpha);
      if (!m_font || alpha <= unseen)
        return;
      const int minutes = static_cast<int> (std::floor (
                            st.clock.numerical_value_in (u::h) * 60.0f)) %
                          (24 * 60);
      char reading[8];
      std::snprintf (
        reading, sizeof reading, "%02d:%02d", minutes / 60, minutes % 60);
      TextStyle hour;
      hour.size = 17.0f;
      tint (hour, ink);
      hour.alpha = opacity_value (0.72f * alpha);
      hour.tracking = 0.06f;
      hour.tabular_figures = true;
      draw_shaded (text,
                   0.5f * width,
                   height - 92.0f * u::pt,
                   reading,
                   hour,
                   TextAlign::Center);
    }

    void Hud::draw_waymarks (render::TextList& text,
                             const HudState& st,
                             seconds_t dt,
                             points_t width,
                             points_t height) {
      if (!m_font)
        return;
      using Mark = HudState::Waymark;
      std::vector<Mark> marks (st.waymarks.begin (),
                               st.waymarks.begin () + st.waymark_count);

      // A distance as the eye wants it: to five metres, or a tenth of a
      // kilometre; and a group's machines as their nearest, or as the
      // span from nearest to furthest where that is wide.
      static constexpr meters_t kilometre = 1000.0f * u::m;
      const auto distance = [] (meters_t metres, bool unit) {
        char words[32];
        const float m = meters_value (metres);
        if (metres >= kilometre)
          std::snprintf (
            words, sizeof words, unit ? "%.1f km" : "%.1f", m / 1000.0f);
        else
          std::snprintf (words,
                         sizeof words,
                         unit ? "%d m" : "%d",
                         static_cast<int> (std::round (m / 5.0f) * 5.0f));
        return std::string (words);
      };
      const auto span = [&] (meters_t near, meters_t far) {
        const std::string first = distance (near, true);
        const std::string last = distance (far, true);
        if (far <= 1.12f * near + 10.0f * u::m || first == last)
          return first;
        if ((near >= kilometre) == (far >= kilometre))
          return distance (near, false) + "\u2013" + last;
        return first + " \u2013 " + last;
      };

      TextStyle named;
      named.size = 11.0f;
      named.tracking = 0.04f;
      named.tabular_figures = true;
      TextStyle reading = named;
      reading.size = 12.0f;
      tint (reading, ink);
      constexpr points_t line = 15.0f * u::pt;  // a line of words
      constexpr points_t pitch = 28.0f * u::pt; // between silhouettes
      constexpr points_t tier = 22.0f * u::pt;  // a row of them
      constexpr points_t edge = 12.0f * u::pt;  // kept from the frame's

      // The chart, its heading and map key own their corner.
      const bool aside = m_diagnostics || m_host_offers;
      const bool charted = st.chart.picture && m_chart_alpha > unseen;
      const auto chart = chart_placement (width, height, aside);
      const HudBox corner { chart.x - chart.outer - 8.0f * u::pt,
                            chart.y - chart.outer - 36.0f * u::pt,
                            chart.x + chart.outer + 65.0f * u::pt,
                            chart.y + chart.outer + 12.0f * u::pt };

      // One marker for a group of marks: a pointer, and beside it a
      // block of the members' names and silhouettes. In view, the
      // pointer comes down onto the group and the block stands over it;
      // at the frame's edge the pointer points out and the block lies
      // inward of it.
      struct Arrangement {
        points_t x = 0.0f * u::pt, y = 0.0f * u::pt;
        radians_t angle = 0.0f * u::rad;
        points_t size = 0.0f * u::pt;
        DisplayColor colour = ink;
        opacity_t alpha = transparent;
        bool in_view = false;
        TextAlign align = TextAlign::Center;
        std::vector<const Mark*> names;
        unsigned vehicles = 0;
        int count = 0, rows = 0, columns = 0;
        std::string machines;
        HudBox block {}, box {};
      };
      const auto arrange = [&] (unsigned group) {
        Arrangement a;
        // The sum of the members' pointing directions.
        magnitude_t across = 0.0f * one, upward = 0.0f * one;
        meters_t near = 1e9f * u::m, far = 0.0f * u::m;
        int members = 0;
        const Mark* first = nullptr;
        for (unsigned i = 0; i < marks.size (); ++i) {
          if (!(group & (1u << i)))
            continue;
          const Mark& mark = marks[i];
          a.x += mark.x;
          a.y += mark.y;
          across += std::cos (radians_value (mark.angle)) * one;
          upward += std::sin (radians_value (mark.angle)) * one;
          a.alpha = std::max (a.alpha, mark.alpha);
          a.in_view = mark.in_view;
          ++members;
          if (mark.vehicles ()) {
            a.vehicles |= mark.vehicles ();
            near = std::min (near, mark.metres);
            far = std::max (far, mark.metres);
          } else {
            a.names.push_back (&mark);
          }
          // The pointer takes the colour of the one that matters most:
          // the other player, then the place, then the machines.
          const auto rank = [] (const Mark& m) {
            return m.who & Mark::Companion ? 0 : m.who & Mark::Place ? 1 : 2;
          };
          if (!first || rank (mark) < rank (*first))
            first = &mark;
        }
        if (!members)
          return a;
        std::ranges::stable_sort (a.names, {}, [] (const Mark* m) {
          return m->who & Mark::Companion ? 0 : 1;
        });
        a.x /= static_cast<float> (members);
        a.y /= static_cast<float> (members);
        a.angle = a.in_view ? (-0.5f * PI) * u::rad
                            : std::atan2 (upward.numerical_value_in (one),
                                          across.numerical_value_in (one)) *
                                u::rad;
        a.size = a.in_view ? 5.5f * u::pt : 7.0f * u::pt;
        a.colour = first->colour;
        // Quieter over the thing itself than out at the edge.
        a.alpha *= a.vehicles ? 0.94f : a.in_view ? 0.75f : 0.85f;
        a.count = std::popcount (a.vehicles);
        a.rows = a.count ? (a.count + 3) / 4 : 0;
        a.columns = a.count ? (a.count + a.rows - 1) / a.rows : 0;
        if (a.count)
          a.machines = span (near, far);

        points_t w = a.columns * pitch, h = a.rows * tier;
        if (a.count)
          h += line;
        for (const Mark* m : a.names) {
          w = std::max (w,
                        m_font->measure (std::string (m->name) + "  " +
                                           distance (m->metres, true),
                                         named) *
                          u::pt);
          h += line;
        }
        if (a.count)
          w = std::max (w, m_font->measure (a.machines, reading) * u::pt);

        // The pointer's direction on the screen (y up): a unit vector,
        // which places the block.
        const float c = std::cos (radians_value (a.angle));
        const float s = std::sin (radians_value (a.angle));
        points_t left, top;
        if (a.in_view) {
          left = a.x - 0.5f * w;
          top = a.y - 9.0f * u::pt - h;
        } else if (std::fabs (c) > 0.35f) {
          a.align = c > 0.0f ? TextAlign::Right : TextAlign::Left;
          left = c > 0.0f ? a.x - 13.0f * u::pt - w : a.x + 13.0f * u::pt;
          top = a.y - 0.5f * h;
        } else {
          left = a.x - 0.5f * w;
          top = s > 0.0f ? a.y + 12.0f * u::pt : a.y - 12.0f * u::pt - h;
        }
        left = std::clamp (left, edge, std::max (edge, width - edge - w));
        top = std::clamp (top, edge, std::max (edge, height - edge - h));
        // A marker at the edge that would cover the chart's corner slides
        // along the edge past it.
        if (charted && !a.in_view) {
          const points_t low = std::min (top, a.y - a.size);
          const points_t high = std::max (top + h, a.y + a.size);
          if (std::min (left, a.x - a.size) < corner[2] &&
              std::max (left + w, a.x + a.size) > corner[0] &&
              low < corner[3] && high > corner[1]) {
            const points_t shift = aside ? corner[3] + 6.0f * u::pt - low
                                         : corner[1] - 6.0f * u::pt - high;
            a.y += shift;
            top += shift;
          }
        }
        a.block = { left, top, left + w, top + h };
        a.box = { std::min (left, a.x - a.size),
                  std::min (top, a.y - a.size),
                  std::max (left + w, a.x + a.size),
                  std::max (top + h, a.y + a.size) };
        return a;
      };

      // Group them, and fade each group's marker in and the markers of
      // groups that have come apart (or together) out.
      std::vector<unsigned> before;
      for (const Marker& marker : m_markers)
        if (marker.live)
          before.push_back (marker.who);
      const auto who = [&] (unsigned group) {
        unsigned members = 0;
        for (unsigned i = 0; i < marks.size (); ++i)
          if (group & (1u << i))
            members |= marks[i].who;
        return members;
      };
      std::vector<unsigned> live;
      for (unsigned group : group_waymarks (
             marks, [&] (unsigned g) { return arrange (g).box; }, before))
        live.push_back (who (group));
      for (unsigned members : live)
        if (std::ranges::none_of (
              m_markers, [&] (const Marker& m) { return m.who == members; })) {
          // A group that has gained or lost a member takes over the
          // marker of the group it was, as far faded in, rather than
          // fading in under its fading copy: the same name is never
          // drawn twice.
          opacity_t alpha = m_markers.empty () ? opaque : transparent;
          std::erase_if (m_markers, [&] (const Marker& m) {
            if (!(m.who & members) || std::ranges::count (live, m.who))
              return false;
            alpha = std::max (alpha, m.alpha);
            return true;
          });
          m_markers.push_back ({ members, alpha, true });
        }
      for (Marker& marker : m_markers) {
        marker.live = std::ranges::count (live, marker.who) > 0;
        marker.alpha = approach (
          marker.alpha, marker.live ? opaque : transparent, 9.0f / u::s, dt);
      }
      std::erase_if (m_markers, [] (const Marker& m) {
        return m.alpha < 0.01f * opacity[one] && !m.live;
      });

      for (const Marker& marker : m_markers) {
        unsigned group = 0;
        for (unsigned i = 0; i < marks.size (); ++i)
          if ((marks[i].who & marker.who) && marks[i].alpha > unseen)
            group |= 1u << i;
        if (!group)
          continue;
        const Arrangement a = arrange (group);
        const opacity_t alpha =
          quantity_cast<opacity> (a.alpha * ease (marker.alpha));
        if (alpha <= unseen)
          continue;
        place_shape (text,
                     m_shape->arrow,
                     a.x,
                     a.y,
                     a.size + 1.5f * u::pt,
                     a.angle,
                     black,
                     0.4f * alpha,
                     4.0f * u::pt);
        place_shape (
          text, m_shape->arrow, a.x, a.y, a.size, a.angle, a.colour, alpha);

        // The block's lines, set flush toward the pointer.
        const points_t x = a.align == TextAlign::Left ? a.block[0]
                           : a.align == TextAlign::Right
                             ? a.block[2]
                             : 0.5f * (a.block[0] + a.block[2]);
        points_t y = a.block[1];
        for (const Mark* m : a.names) {
          TextStyle words = named;
          tint (words, m->colour);
          words.alpha = opacity_value (alpha);
          draw_shaded (text,
                       x,
                       y + 11.0f * u::pt,
                       std::string (m->name) + "  " +
                         distance (m->metres, true),
                       words,
                       a.align);
          y += line;
        }
        if (!a.count)
          continue;
        // At most four silhouettes a row, balanced: five makes 3 + 2,
        // six 3 + 3, and seven 4 + 3.
        const points_t row_width = a.columns * pitch;
        int icon = 0;
        for (unsigned v = 0; v < m_shape->vehicles.size (); ++v) {
          if (!(a.vehicles & (1u << v)))
            continue;
          const int row = icon / a.columns;
          const int in_row = std::min (a.columns, a.count - row * a.columns);
          const points_t middle =
            a.align == TextAlign::Left    ? x + 0.5f * row_width
            : a.align == TextAlign::Right ? x - 0.5f * row_width
                                          : x;
          const points_t ix =
            middle + (icon % a.columns - 0.5f * (in_row - 1)) * pitch;
          const points_t iy = y + 0.5f * tier + row * tier;
          const auto& shape = m_shape->vehicles[v];
          // A soft shade keeps the small silhouettes legible over sky,
          // without a dark outline round every wheel and spoke.
          place_shape (text,
                       shape,
                       ix,
                       iy + 0.5f * u::pt,
                       0.9f * u::pt,
                       0.0f * u::rad,
                       black,
                       0.5f * alpha,
                       2.0f * u::pt);
          place_shape (
            text, shape, ix, iy, 0.9f * u::pt, 0.0f * u::rad, ink, alpha);
          ++icon;
        }
        y += a.rows * tier;
        TextStyle words = reading;
        words.alpha = opacity_value (alpha);
        draw_shaded (text, x, y + 11.5f * u::pt, a.machines, words, a.align);
      }
    }

    // The chart in the corner: the map sheet's picture turned so that the
    // way the camera looks is up, under a hairline rim and a smoked bezel
    // whose ticks and letters keep to the world's quarters. The heading
    // stands over it, the subject is an arrow in its middle, and what is
    // worth finding is a dot on it -- or on its rim, pointing the way,
    // when it is further off than the chart reaches.
    void Hud::draw_chart (DrawList& dl,
                          render::TextList& text,
                          const HudState& st,
                          seconds_t dt,
                          points_t width,
                          points_t height) {
      const HudState::Chart& chart = st.chart;
      const meters_t period_x = plan_value (chart.period).x () * u::m;
      const meters_t period_z = plan_value (chart.period).z () * u::m;
      const bool wanted = chart.picture && !st.resting &&
                          period_x > 0.0f * u::m && period_z > 0.0f * u::m &&
                          std::min (width, height) >= 240.0f * u::pt;
      m_chart_alpha = approach (
        m_chart_alpha, wanted ? opaque : transparent, 3.0f / u::s, dt);
      const proportion_t pace = clamp01 ((st.speed - 15.0f * (u::km / u::h)) /
                                         (70.0f * (u::km / u::h)));
      const meters_t reach =
        st.gliding || st.flying ? chart_reach_air
        : st.on_foot
          ? chart_reach_foot
          : meters_t (chart_reach_foot +
                      (chart_reach_fast - chart_reach_foot) * ease (pace));
      m_chart_reach = m_chart_reach > 0.0f * u::m
                        ? approach (m_chart_reach, reach, 0.8f / u::s, dt)
                        : reach;
      const opacity_t alpha = ease (m_chart_alpha);
      if (alpha <= unseen || !chart.picture || !m_font)
        return;

      // The lower left, balancing the gauge; or the upper right where the
      // diagnostics' trail map, or a touch screen's thumb, has that corner.
      const bool aside = m_diagnostics || m_host_offers;
      const auto placement = chart_placement (width, height, aside);
      const points_t r = placement.radius, outer = placement.outer;
      const points_t cx = placement.x, cy = placement.y;
      const points_per_metre_t scale = r / m_chart_reach;

      // The way ahead is up, and the screen's right is the ground's to
      // the right of it.
      plan_direction_t ahead = plan_unit_direction (Vec2 (0.0f, -1.0f));
      if (const magnitude_t n = std::hypot (plan_value (chart.ahead).x (),
                                            plan_value (chart.ahead).z ()) *
                                one;
          n > 1e-4f * one)
        ahead = plan_unit_direction (plan_value (chart.ahead) /
                                     n.numerical_value_in (one));
      const plan_direction_t right = plan_unit_direction (
        Vec2 (-plan_value (ahead).z (), plan_value (ahead).x ()));
      // A stretch of ground from the subject to the screen, from the
      // chart's middle (y down).
      struct OnScreen {
        points_t x, y;
      };
      const auto on_screen = [&] (const plan_displacement_t& d) {
        return OnScreen { (x_of (d) * plan_value (right).x () +
                           z_of (d) * plan_value (right).z ()) *
                            scale,
                          -(x_of (d) * plan_value (ahead).x () +
                            z_of (d) * plan_value (ahead).z ()) *
                            scale };
      };

      // The picture, as rings of cells cut at the sheet's seams.
      hud_state (dl);
      const proportion_t light =
        0.62f * proportion[one] + 0.38f * clamp01 (chart.daylight);
      dl.set_texture (chart.picture);
      dl.color (light.numerical_value_in (one) *
                  (0.90f + 0.10f * chart.daylight.numerical_value_in (one)),
                light.numerical_value_in (one) *
                  (0.94f + 0.06f * chart.daylight.numerical_value_in (one)),
                light.numerical_value_in (one),
                opacity_value (alpha));
      dl.begin (render::Prim::Triangles);
      constexpr int rings = 6;
      constexpr int sectors = 96;
      const points_t edge = 1.004f * r;
      // A sector's bound, counterclockwise from the right.
      const auto sector_angle = [] (int sector) {
        return (2.0f * PI * sector / sectors) * u::rad;
      };
      // Where a point of the chart falls on the screen, and on the sheet.
      const auto corner = [&] (points_t radius, radians_t angle) {
        const points_t sx = radius * std::cos (radians_value (angle));
        const points_t sy = radius * std::sin (radians_value (angle));
        const meters_t dx =
          (plan_value (right).x () * sx - plan_value (ahead).x () * sy) / scale;
        const meters_t dz =
          (plan_value (right).z () * sx - plan_value (ahead).z () * sy) / scale;
        return ChartCorner {
          points_value (cx + sx),
          points_value (cy + sy),
          ((x_of (chart.subject) + dx) / period_x).numerical_value_in (one),
          ((z_of (chart.subject) + dz) / period_z).numerical_value_in (one)
        };
      };
      for (int ring = 0; ring < rings; ++ring) {
        const points_t inner = edge * ring / rings;
        const points_t outer_ring = edge * (ring + 1) / rings;
        for (int sector = 0; sector < sectors; ++sector) {
          const radians_t a0 = sector_angle (sector);
          const radians_t a1 = sector_angle (sector + 1);
          ChartPolygon cell;
          if (ring == 0) {
            cell.corners[cell.count++] = corner (0.0f * u::pt, 0.0f * u::rad);
          } else {
            cell.corners[cell.count++] = corner (inner, a0);
          }
          cell.corners[cell.count++] = corner (outer_ring, a0);
          cell.corners[cell.count++] = corner (outer_ring, a1);
          if (ring > 0)
            cell.corners[cell.count++] = corner (inner, a1);
          chart_polygon (dl, cell);
        }
      }
      dl.end ();
      dl.set_texture (nullptr);

      // A lens's falloff toward the rim, so the paper sits under glass.
      dl.begin (render::Prim::Triangles);
      for (int sector = 0; sector < sectors; ++sector) {
        const radians_t a0 = sector_angle (sector);
        const radians_t a1 = sector_angle (sector + 1);
        const points_t inner = 0.70f * r;
        // The sectors' bounding directions: the vertices' floats.
        const float c0 = std::cos (radians_value (a0));
        const float s0 = std::sin (radians_value (a0));
        const float c1 = std::cos (radians_value (a1));
        const float s1 = std::sin (radians_value (a1));
        const auto at =
          [&] (points_t radius, float c, float s, opacity_t shade) {
            dl.color (0.06f,
                      0.05f,
                      0.04f,
                      opacity_value (quantity_cast<opacity> (shade * alpha)));
            dl.vertex (points_value (cx + radius * c),
                       points_value (cy + radius * s));
          };
        constexpr opacity_t rim = 0.20f * opacity[one];
        at (inner, c0, s0, transparent);
        at (edge, c0, s0, rim);
        at (edge, c1, s1, rim);
        at (inner, c0, s0, transparent);
        at (edge, c1, s1, rim);
        at (inner, c1, s1, transparent);
      }
      dl.end ();
      dl.state (render::DrawState ());
      dl.lit (true);
      dl.fogged (true);
      dl.color (1, 1, 1, 1);

      // The instrument: a soft shade under it, the bezel, and the rim over
      // the picture's edge.
      place_shape (text,
                   m_shape->shade,
                   cx,
                   cy + 2.0f * u::pt,
                   1.08f * outer,
                   0.0f * u::rad,
                   black,
                   0.24f * alpha,
                   40.0f * u::pt);
      place_shape (text,
                   m_shape->bezel,
                   cx,
                   cy,
                   r,
                   0.0f * u::rad,
                   DisplayColor (0.035f, 0.040f, 0.050f),
                   0.58f * alpha);
      place_shape (
        text, m_shape->rim, cx, cy, r, 0.0f * u::rad, ink, 0.88f * alpha);

      // A bearing's direction on the screen, as place_shape turns:
      // counterclockwise from the right.
      constexpr meters_t pace_of_ground = 1.0f * u::m;
      const auto bearing_angle = [&] (degrees_t bearing) {
        const radians_t b =
          (bearing.numerical_value_in (u::deg) * PI / 180.0f) * u::rad;
        const OnScreen way = on_screen (
          along (plan_unit_direction (Vec2 (std::sin (radians_value (b)),
                                            -std::cos (radians_value (b)))),
                 pace_of_ground));
        return std::atan2 (-points_value (way.y), points_value (way.x)) *
               u::rad;
      };
      for (int degrees = 10; degrees < 360; degrees += 10) {
        if (degrees % 90 == 0)
          continue;
        const bool major = degrees % 30 == 0;
        place_shape (text,
                     major ? m_shape->major_tick : m_shape->tick,
                     cx,
                     cy,
                     r,
                     bearing_angle (static_cast<float> (degrees) * u::deg),
                     ink,
                     (major ? 0.85f : 0.45f) * alpha);
      }
      TextStyle quarter;
      quarter.size =
        points_value (std::clamp (0.135f * r, 9.0f * u::pt, 12.5f * u::pt));
      quarter.tracking = 0.02f;
      const char* const quarters[] = { "N", "E", "S", "W" };
      for (int q = 0; q < 4; ++q) {
        const radians_t angle = bearing_angle ((90.0f * q) * u::deg);
        const points_t middle = quantity_cast<display_length> (
          0.5f * (1.0f * scale_factor[one] + chart_bezel) * r);
        const bool north = q == 0;
        tint (quarter, north ? DisplayColor (1.00f, 0.42f, 0.33f) : ink);
        quarter.alpha = opacity_value ((north ? 1.0f : 0.82f) * alpha);
        m_font->draw (
          text,
          points_value (cx + middle * std::cos (radians_value (angle))),
          points_value (cy - middle * std::sin (radians_value (angle)) +
                        0.5f * m_font->cap_height (quarter.size) * u::pt),
          quarters[q],
          quarter,
          TextAlign::Center);
      }

      // The heading, read off a fixed mark over the bezel.
      const degrees_t heading =
        std::fmod (
          std::atan2 (plan_value (ahead).x (), -plan_value (ahead).z ()) *
              180.0f / PI +
            360.0f,
          360.0f) *
        u::deg;
      place_shape (text,
                   m_shape->arrow,
                   cx,
                   cy - outer - 3.0f * u::pt,
                   4.0f * u::pt,
                   (-0.5f * PI) * u::rad,
                   ink,
                   0.92f * alpha);
      char reading[8];
      std::snprintf (
        reading,
        sizeof reading,
        "%03d",
        static_cast<int> (std::lround (heading.numerical_value_in (u::deg))) %
          360);
      TextStyle figure;
      figure.size = 11.5f;
      tint (figure, ink);
      figure.alpha = opacity_value (0.9f * alpha);
      figure.tracking = 0.06f;
      figure.tabular_figures = true;
      draw_shaded (text,
                   cx,
                   cy - outer - 11.0f * u::pt,
                   reading,
                   figure,
                   TextAlign::Center);

      // What there is to find on it, pinned to the rim when off it.
      for (int i = 0; i < chart.pin_count; ++i) {
        const HudState::Chart::Pin& pin = chart.pins[i];
        OnScreen s = on_screen (
          terrain::torus_offset (chart.subject, pin.position, chart.period));
        const points_t far = 0.86f * r;
        const points_t off =
          std::hypot (points_value (s.x), points_value (s.y)) * u::pt;
        const bool beyond = off > far;
        using Kind = HudState::Chart::Pin::Kind;
        using Place = terrain::RoadPlace::Kind;
        const bool trailhead =
          pin.kind == Kind::Place && pin.place == Place::Trailhead;
        const bool place = pin.kind == Kind::Place && !trailhead;
        if (pin.kind == Kind::Bus || pin.kind == Kind::BusStop) {
          // The buses and their stops show only on the chart: a bus as
          // a red dot ringed in cream, a stop as a little yellow one.
          if (off > 0.95f * r)
            continue;
          const bool bus = pin.kind == Kind::Bus;
          const points_t size = bus ? 3.6f * u::pt : 2.0f * u::pt;
          const points_t x = cx + s.x, y = cy + s.y;
          place_shape (text,
                       m_shape->shade,
                       x,
                       y + 0.5f * u::pt,
                       size + 2.0f * u::pt,
                       0.0f * u::rad,
                       black,
                       0.30f * alpha,
                       3.0f * u::pt);
          place_shape (text,
                       m_shape->shade,
                       x,
                       y,
                       size + 1.2f * u::pt,
                       0.0f * u::rad,
                       bus ? DisplayColor (0.92f, 0.87f, 0.72f)
                           : DisplayColor (0.06f, 0.42f, 0.22f),
                       alpha);
          place_shape (
            text, m_shape->shade, x, y, size, 0.0f * u::rad, pin.colour, alpha);
          continue;
        }
        if (place) {
          // Named places stay on the chart only while they are on it:
          // a peak as a triangle, the landing and an outlet as a ring.
          if (off > 0.95f * r)
            continue;
          const points_t x = cx + s.x, y = cy + s.y;
          const bool peak = pin.place == Place::Sanctuary;
          const auto& shape = peak ? m_shape->arrow : m_shape->shade;
          const radians_t angle = peak ? (0.5f * PI) * u::rad : 0.0f * u::rad;
          const points_t size =
            pin.place == Place::LakeOutlet ? 3.0f * u::pt : 3.8f * u::pt;
          place_shape (text,
                       shape,
                       x,
                       y + 0.6f * u::pt,
                       size + 2.4f * u::pt,
                       angle,
                       black,
                       0.30f * alpha,
                       3.0f * u::pt);
          place_shape (
            text, shape, x, y, size + 1.3f * u::pt, angle, white, alpha);
          place_shape (text, shape, x, y, size, angle, pin.colour, alpha);
          if (!peak)
            place_shape (text,
                         m_shape->shade,
                         x,
                         y,
                         0.45f * size,
                         0.0f * u::rad,
                         white,
                         alpha);
          continue;
        }
        if (beyond) {
          s.x *= (far / off).numerical_value_in (one);
          s.y *= (far / off).numerical_value_in (one);
        }
        points_t size = pin.kind == Kind::Companion ? 4.4f * u::pt
                        : trailhead                 ? 4.2f * u::pt
                                                    : 3.4f * u::pt;
        opacity_t a = alpha;
        if (beyond) {
          size *= 1.3f;
          a *= 0.8f;
        }
        const points_t x = cx + s.x, y = cy + s.y;
        const auto& shape = beyond ? m_shape->arrow : m_shape->shade;
        const radians_t angle =
          beyond ? std::atan2 (-points_value (s.y), points_value (s.x)) * u::rad
                 : 0.0f * u::rad;
        place_shape (text,
                     shape,
                     x,
                     y + 0.6f * u::pt,
                     size + 2.4f * u::pt,
                     angle,
                     black,
                     0.30f * a,
                     3.0f * u::pt);
        place_shape (text, shape, x, y, size + 1.4f * u::pt, angle, white, a);
        place_shape (text, shape, x, y, size, angle, pin.colour, a);
        if (trailhead && !beyond)
          place_shape (
            text, m_shape->shade, x, y, 0.45f * size, 0.0f * u::rad, white, a);
      }

      // The subject, facing the way they face.
      const OnScreen f = on_screen (along (chart.facing, pace_of_ground));
      const radians_t facing =
        std::hypot (points_value (f.x), points_value (f.y)) > 1e-6f
          ? std::atan2 (-points_value (f.y), points_value (f.x)) * u::rad
          : (0.5f * PI) * u::rad;
      place_shape (text,
                   m_shape->arrow,
                   cx,
                   cy + 0.5f * u::pt,
                   9.5f * u::pt,
                   facing,
                   black,
                   0.35f * alpha,
                   4.0f * u::pt);
      place_shape (
        text, m_shape->arrow, cx, cy, 8.6f * u::pt, facing, white, alpha);
      place_shape (text,
                   m_shape->arrow,
                   cx,
                   cy,
                   6.4f * u::pt,
                   facing,
                   DisplayColor (0.80f, 0.16f, 0.10f),
                   alpha);

      // The key that unfolds the whole map, small against the bezel's
      // lower outer side.
      if (*map_key && !m_host_offers) {
        TextStyle words = prompt_words (0.8f * alpha);
        words.size = 10.5f;
        const points_t run = keyed_width (map_key, "Map", words);
        const points_t diagonal = 0.72f * outer;
        const points_t x = aside ? cx - diagonal - 4.0f * u::pt - run
                                 : cx + diagonal + 4.0f * u::pt;
        draw_keyed (text,
                    x,
                    cy + diagonal +
                      0.5f * m_font->cap_height (words.size) * u::pt,
                    map_key,
                    "Map",
                    words,
                    0.8f * alpha);
      }
    }

    void Hud::draw_prompts (render::TextList& text,
                            const HudState& st,
                            seconds_t dt,
                            points_t width,
                            points_t height) {
      std::string key, action;
      if (st.prompt_keyed) {
        // The key a button is pressed with here, if there is one.
        const char* keyed = "";
        if (st.prompt_key)
          switch (*st.prompt_key) {
          case Button::use:
            keyed = deploy_key;
            break;
          case Button::mount:
            keyed = mount_key;
            break;
          case Button::hammock:
            keyed = hammock_key;
            break;
          case Button::fire:
            keyed = fire_key;
            break;
          }
        key = keyed;
        action = *keyed ? st.prompt_keyed : st.prompt_unkeyed;
      } else if (st.swimming && *ride_key) {
        key = st.submerged ? ride_key : dive_key;
        action = st.submerged ? "Swim up" : "Dive";
      } else if (st.thumb_hint && st.thumb && *ride_key) {
        key = ride_key;
        action = st.thumb;
      } else if (st.wheelie_hint && *hammock_key) {
        key = hammock_key;
        action = "Lean back for a wheelie";
      }
      // The prompt's action leads the offers, then what needs no prompt.
      offer (key, action);
      if (st.thumb)
        offer ("Space", st.thumb);
      if (st.flying)
        offer ("Shift", "Descend");
      if (st.swimming)
        offer ("Shift", "Dive");
      if (st.skids)
        offer ("Shift", "Skid");
      if (st.wheelies)
        offer (hammock_key, "Wheelie");
      if (st.dismount)
        offer (mount_key, st.dismount);
      if (st.can_take_down)
        offer (hammock_key, "Take down the hammock");
      if (st.can_douse_fire)
        offer (fire_key, "Put out the fire");
      else if (st.can_light_fire)
        offer (fire_key, "Light a fire");

      const bool wanted = !action.empty ();
      if (wanted) {
        // A different prompt replaces the old one only once it has faded.
        if (m_prompt_alpha < 0.05f * opacity[one] ||
            action == m_prompt_action) {
          m_prompt_key = key;
          m_prompt_action = action;
        }
      }
      const bool showing = wanted && action == m_prompt_action;
      m_prompt_alpha = approach (m_prompt_alpha,
                                 showing ? opaque : transparent,
                                 showing ? 7.0f / u::s : 5.0f / u::s,
                                 dt);
      // A host with a button for it shows a keyed prompt itself.
      if (!m_host_offers || m_prompt_key.empty ())
        draw_prompt (text,
                     m_prompt_key,
                     m_prompt_action,
                     ease (m_prompt_alpha),
                     width,
                     height);
    }

    void Hud::offer (const std::string& key, const std::string& action) {
      const platform::Key code = platform::key_named (key);
      if (code == platform::Key::Unknown || action.empty ())
        return;
      // One button a key: the first to ask for it has it.
      for (const platform::Offer& made : m_offers)
        if (made.key == code)
          return;
      m_offers.push_back ({ code, action });
    }

    void Hud::publish_offers () {
      m_host_offers = platform::offer_actions (m_offers);
      m_offers.clear ();
    }

    void Hud::draw_diagnostics (render::DrawList& dl,
                                render::TextList& text,
                                const HudState& st,
                                points_t width,
                                points_t height) {
      (void)width;
      (void)height;
      // Diagnostics are for reading, not for looks: a dark card keeps them
      // legible against any sky.
      hud_state (dl);
      dl.color (0.015f, 0.02f, 0.03f, 0.55f);
      dl.begin (render::Prim::Quads);
      dl.vertex (8.0f, 8.0f);
      dl.vertex (268.0f, 8.0f);
      dl.vertex (268.0f, 153.0f);
      dl.vertex (8.0f, 153.0f);
      dl.end ();
      dl.state (render::DrawState ());
      dl.lit (true);
      dl.fogged (true);
      dl.color (1, 1, 1, 1);
      TextStyle style;
      style.size = 11.0f;
      tint (style, ink);
      style.alpha = 0.85f;
      style.tabular_figures = true;
      // The readings are set as text: their numbers leave the types here.
      const float frame_ms =
        seconds_value (std::max (st.frame_time, 1e-4f * u::s)) * 1000.0f;
      const degrees_t heading =
        std::fmod (st.heading.numerical_value_in (si::radian) * 180.0f / PI +
                     360.0f,
                   360.0f) *
        u::deg;
      char lines[9][96];
      std::snprintf (lines[0],
                     sizeof lines[0],
                     "%.0f fps   %.1f ms",
                     m_fps.numerical_value_in (u::Hz),
                     frame_ms);
      std::snprintf (lines[1],
                     sizeof lines[1],
                     "%.0f km/h   heading %03.0f°",
                     st.speed.numerical_value_in (u::km / u::h),
                     heading.numerical_value_in (u::deg));
      std::snprintf (lines[2],
                     sizeof lines[2],
                     "air %.1f s   whip %.0f°",
                     seconds_value (st.airtime),
                     st.spin.numerical_value_in (u::deg));
      std::snprintf (lines[3],
                     sizeof lines[3],
                     "last %s %.1f s   %.0f°   +%d",
                     st.landed_clean ? "clean" : "landing",
                     seconds_value (st.landed_airtime),
                     st.landed_spin.numerical_value_in (u::deg),
                     st.landed_points);
      std::snprintf (lines[4],
                     sizeof lines[4],
                     "wheelie %.0f m   last %.0f m +%d   best %.0f m",
                     meters_value (st.wheelie),
                     meters_value (st.last_wheelie),
                     st.last_wheelie_points,
                     meters_value (st.best_wheelie));
      std::snprintf (lines[5], sizeof lines[5], "score %d", st.score);
      std::snprintf (lines[6],
                     sizeof lines[6],
                     "health %.0f%%   lives %d   stars %d",
                     clamp01 (st.health).numerical_value_in (one) * 100.0f,
                     st.lives,
                     st.stars);
      std::snprintf (lines[7],
                     sizeof lines[7],
                     "odometer %.2f km   boost %.0f%%",
                     meters_value (st.odometer) / 1000.0f,
                     clamp01 (st.boost_ready).numerical_value_in (one) *
                       100.0f);
      std::snprintf (lines[8],
                     sizeof lines[8],
                     "climb %+.1f m/s",
                     speed_value (st.vertical_speed));
      points_t y = 24.0f * u::pt;
      for (const char* line : lines) {
        draw_shaded (text, 16.0f * u::pt, y, line, style);
        y += 15.0f * u::pt;
      }
    }

    void Hud::draw (render::DrawList& dl,
                    render::TextList& text,
                    const HudState& st,
                    points_t width,
                    points_t height) {
      const seconds_t dt = std::clamp (st.frame_time, 0.0f * u::s, 0.1f * u::s);
      const frequency_t instant_fps =
        std::min (240.0f * u::Hz,
                  frequency_t (1.0f / std::max (st.frame_time, 0.001f * u::s)));
      m_fps =
        m_fps > 0.0f * u::Hz ? m_fps * 0.9f + instant_fps * 0.1f : instant_fps;
      if (m_hidden)
        return;

      draw_chart (dl, text, st, dt, width, height);
      draw_waymarks (text, st, dt, width, height);
      draw_gauge (text, st, dt, width, height);
      draw_basket (text, st, dt, width, height);
      draw_clock (text, st, dt, width, height);
      draw_prompts (text, st, dt, width, height);
      if (m_diagnostics)
        draw_diagnostics (dl, text, st, width, height);
    }

    void Hud::draw_game_over (render::DrawList& dl,
                              render::TextList& text,
                              points_t width,
                              points_t height) {
      // The veil's vertices and the lettering's places: the DrawList's and
      // the font's floats.
      const float w = points_value (width);
      const float h = points_value (height);
      hud_state (dl);
      dl.color (0.02f, 0.02f, 0.025f, 1.0f);
      dl.begin (render::Prim::Quads);
      dl.vertex (0, 0);
      dl.vertex (w, 0);
      dl.vertex (w, h);
      dl.vertex (0, h);
      dl.end ();
      dl.state (render::DrawState ());
      dl.lit (true);
      dl.fogged (true);
      dl.color (1, 1, 1, 1);
      if (!m_font)
        return;

      TextStyle title;
      title.size = 34.0f;
      tint (title, DisplayColor (0.92f, 0.88f, 0.84f));
      title.alpha = 0.95f;
      m_font->draw (
        text, 0.5f * w, 0.5f * h - 18.0f, "Sorry.", title, TextAlign::Center);
      TextStyle line = title;
      line.size = 19.0f;
      line.alpha = 0.7f;
      m_font->draw (text,
                    0.5f * w,
                    0.5f * h + 16.0f,
                    "You are in great pain.",
                    line,
                    TextAlign::Center);
      offer (restart_key, "Ride again");
      if (!m_host_offers)
        draw_prompt (
          text, restart_key, "Ride again", 0.8f * opacity[one], width, height);
    }

    void Hud::draw_quit_question (render::DrawList& dl,
                                  render::TextList& text,
                                  points_t width,
                                  points_t height) const {
      // The veil's vertices: the DrawList's floats.
      const float w = points_value (width);
      const float h = points_value (height);
      hud_state (dl);
      dl.color (0.02f, 0.02f, 0.025f, 0.55f);
      dl.begin (render::Prim::Quads);
      dl.vertex (0, 0);
      dl.vertex (w, 0);
      dl.vertex (w, h);
      dl.vertex (0, h);
      dl.end ();
      dl.state (render::DrawState ());
      dl.lit (true);
      dl.fogged (true);
      dl.color (1, 1, 1, 1);
      if (!m_font)
        return;

      TextStyle title = prompt_words (opaque);
      title.size = 30.0f;
      draw_shaded (text,
                   0.5f * width,
                   0.5f * height - 14.0f * u::pt,
                   "Quit Moppe?",
                   title,
                   TextAlign::Center);
      // Esc again quits; any other key goes back to the game.
      constexpr opacity_t clear = 0.9f * opacity[one];
      const TextStyle words = prompt_words (clear);
      const points_t space = 2.0f * words.size * u::pt;
      const points_t quit = keyed_width ("Esc", "Quit", words);
      const points_t stay = keyed_width ("Space", "Keep playing", words);
      const points_t x = 0.5f * (width - quit - space - stay);
      const points_t baseline = 0.5f * height + 30.0f * u::pt;
      draw_keyed (text, x, baseline, "Esc", "Quit", words, clear);
      draw_keyed (text,
                  x + quit + space,
                  baseline,
                  "Space",
                  "Keep playing",
                  words,
                  clear);
    }

    void Hud::draw_ride_prompt (render::TextList& text,
                                opacity_t alpha,
                                points_t width,
                                points_t height) {
      offer (ride_key, "Begin");
      if (*ride_key && !m_host_offers && !m_hidden)
        draw_prompt (text, ride_key, "Begin", alpha, width, height);
    }

    void Hud::draw_cheat_menu (render::DrawList& dl,
                               render::TextList& text,
                               points_t width,
                               points_t height,
                               Machine chosen) const {
      const float w = points_value (width);
      const float h = points_value (height);
      hud_state (dl);
      dl.color (0.02f, 0.02f, 0.025f, 0.45f);
      dl.begin (render::Prim::Quads);
      dl.vertex (0, 0);
      dl.vertex (w, 0);
      dl.vertex (w, h);
      dl.vertex (0, h);
      dl.end ();
      dl.state (render::DrawState ());
      dl.lit (true);
      dl.fogged (true);
      dl.color (1, 1, 1, 1);
      if (!m_font)
        return;

      constexpr points_t line = 26.0f * u::pt;
      const points_t top =
        0.5f * (height - static_cast<float> (machine_count) * line);
      TextStyle title = prompt_words (opaque);
      title.size = 26.0f;
      draw_shaded (text,
                   0.5f * width,
                   top - 24.0f * u::pt,
                   "Take the wheel of",
                   title,
                   TextAlign::Center);
      for (int i = 0; i < machine_count; ++i) {
        const Machine machine = static_cast<Machine> (i);
        // "the jeep" becomes "Jeep".
        std::string name = machine_kind (machine).name;
        if (name.starts_with ("the "))
          name.erase (0, 4);
        if (!name.empty ())
          name[0] = static_cast<char> (std::toupper (name[0]));
        const bool on = machine == chosen;
        TextStyle words = prompt_words ((on ? 1.0f : 0.5f) * opacity[one]);
        words.size = on ? 19.0f : 17.0f;
        draw_shaded (text,
                     0.5f * width,
                     top + (static_cast<float> (i) + 1.0f) * line,
                     on ? "\u2014  " + name + "  \u2014" : name,
                     words,
                     TextAlign::Center);
      }
      constexpr opacity_t clear = 0.9f * opacity[one];
      const TextStyle words = prompt_words (clear);
      const points_t space = 2.0f * words.size * u::pt;
      const points_t choose = keyed_width ("W/S", "Choose", words);
      const points_t take = keyed_width ("Space", "Take it", words);
      const points_t close = keyed_width ("Esc", "Close", words);
      const points_t x = 0.5f * (width - choose - take - close - 2.0f * space);
      const points_t baseline =
        top + (static_cast<float> (machine_count) + 2.5f) * line;
      draw_keyed (text, x, baseline, "W/S", "Choose", words, clear);
      draw_keyed (
        text, x + choose + space, baseline, "Space", "Take it", words, clear);
      draw_keyed (text,
                  x + choose + take + 2.0f * space,
                  baseline,
                  "Esc",
                  "Close",
                  words,
                  clear);
    }

    void Hud::draw_title_card (render::TextList& text,
                               bool title,
                               const std::string& line,
                               opacity_t alpha,
                               proportion_t y,
                               points_t width,
                               points_t height) const {
      if (!m_font || alpha <= unseen)
        return;
      constexpr proportion_t unplaced = 0.0f * proportion[one];
      TextStyle style;
      tint (style, ink);
      if (title) {
        // The name alone, large and widely spaced, at the frame's optical
        // centre.
        style.size = points_value (
          std::clamp (0.105f * height, 30.0f * u::pt, 140.0f * u::pt));
        style.tracking = 0.14f;
        style.alpha = opacity_value (0.94f * alpha);
        const points_t baseline =
          points_t ((y >= unplaced ? y : 0.47f * proportion[one]) * height) +
          0.5f * m_font->x_height (style.size) * u::pt;
        draw_shaded (
          text, 0.5f * width, baseline, line, style, TextAlign::Center);
      } else {
        // A credit, small, in the lower third.
        style.size = points_value (
          std::clamp (0.027f * height, 13.0f * u::pt, 40.0f * u::pt));
        style.tracking = 0.09f;
        style.alpha = opacity_value (0.9f * alpha);
        draw_shaded (
          text,
          0.5f * width,
          points_t ((y >= unplaced ? y : 0.74f * proportion[one]) * height),
          line,
          style,
          TextAlign::Center);
      }
    }
  }
}
