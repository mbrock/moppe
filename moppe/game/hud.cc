#include <moppe/game/hud.hh>
#include <moppe/render/renderer.hh>

#include <moppe/environment.hh>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

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
      constexpr const char* hammock_key = "LB";
      constexpr const char* fire_key = "RB";
      constexpr const char* map_key = "";
#elif defined(__APPLE__) && TARGET_OS_IPHONE
      constexpr const char* deploy_key = "";
      constexpr const char* mount_key = "";
      constexpr const char* restart_key = "";
      constexpr const char* ride_key = "";
      constexpr const char* hammock_key = "";
      constexpr const char* fire_key = "";
      constexpr const char* map_key = "";
#else
      constexpr const char* deploy_key = "E";
      constexpr const char* mount_key = "F";
      constexpr const char* restart_key = "R";
      constexpr const char* ride_key = "Space";
      constexpr const char* hammock_key = "R";
      constexpr const char* fire_key = "T";
      constexpr const char* map_key = "M";
#endif

      // The gauge: a hairline arc of radius gauge_radius points, open at the
      // bottom, with a notch riding it from zero at lower left to full
      // scale at lower right.
      constexpr float gauge_radius = 30.0f;
      constexpr float gauge_margin = 30.0f;
      // A host's own buttons (platform::offer_actions) have the lower
      // right corner, under the thumb; the gauge and the basket's tally
      // then stand this far in from it.
      constexpr float thumb_room = 92.0f;
      constexpr float gauge_start = 225.0f * PI / 180.0f;
      constexpr float gauge_sweep = 270.0f * PI / 180.0f;
      constexpr float gauge_full_scale_kmh = 160.0f;
      constexpr int reserve_dashes = 12;

      constexpr float ink = 0.96f; // the off-white every element is set in

      // The chart: its radius as a share of the frame's shorter side,
      // within bounds, its margin, and the bezel's outer radius in units
      // of it.
      constexpr float chart_share = 0.115f;
      constexpr float chart_least = 54.0f;
      constexpr float chart_most = 88.0f;
      constexpr float chart_margin = 24.0f;
      constexpr float chart_bezel = 1.20f;
      // The ground it shows round the subject: a walk's worth, widening
      // with speed, and wider still from the air.
      constexpr float chart_reach_foot_m = 220.0f;
      constexpr float chart_reach_fast_m = 600.0f;
      constexpr float chart_reach_air_m = 800.0f;

      struct ChartPlacement {
        float radius, outer, x, y;
      };

      ChartPlacement chart_placement (int width, int height, bool aside) {
        const float r = std::clamp (
          chart_share * std::min (width, height), chart_least, chart_most);
        const float outer = chart_bezel * r;
        return { r,
                 outer,
                 aside ? width - chart_margin - outer : chart_margin + outer,
                 aside ? chart_margin + outer + 16.0f
                       : height - chart_margin - outer };
      }

      float clamp01 (float value) {
        return std::clamp (value, 0.0f, 1.0f);
      }

      // Exponential approach, frame-rate independent: `rate` is the
      // fraction of the remaining distance closed per second's e-fold.
      float approach (float value, float target, float rate, float dt) {
        return target + (value - target) * std::exp (-rate * dt);
      }

      std::string capitalized (const char* name) {
        std::string word (name);
        if (!word.empty () && word[0] >= 'a' && word[0] <= 'z')
          word[0] = static_cast<char> (word[0] - 'a' + 'A');
        return word;
      }

      float ease (float t) {
        t = clamp01 (t);
        return t * t * (3.0f - 2.0f * t);
      }

      // Builds closed outlines from lines and circular arcs.  Every curve
      // starts exactly where the last ended, and the closing curve is
      // snapped onto the first point, so Slug's closed-contour invariant
      // holds without trusting trigonometry to round-trip.
      class Pen {
      public:
        explicit Pen (OutlinePoint start) : m_start (start), m_at (start) {}

        void line (OutlinePoint to) {
          m_contour.push_back (render::outline_line (m_at, to));
          m_at = to;
        }

        // A circular arc about (cx, cy) from the current point, whose angle
        // is `from`, through `sweep` radians.  Quadratic segments of at most
        // 30 degrees stay within 0.06% of the radius.
        void arc (float cx, float cy, float radius, float from, float sweep) {
          const int segments =
            std::max (1, (int)std::ceil (std::fabs (sweep) / (PI / 6.0f)));
          const float step = sweep / segments;
          const float reach = radius / std::cos (0.5f * step);
          for (int i = 0; i < segments; ++i) {
            const float mid = from + (i + 0.5f) * step;
            const float end = from + (i + 1) * step;
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

      OutlinePoint polar (float radius, float angle) {
        return { radius * std::cos (angle), radius * std::sin (angle) };
      }

      // A stroke of half-width `half` along the circle of radius `radius`,
      // from angle `from` through `sweep`, with round caps.
      OutlineContour
      stroked_arc (float radius, float half, float from, float sweep) {
        const float turn = sweep >= 0.0f ? PI : -PI;
        const float to = from + sweep;
        Pen pen (polar (radius + half, from));
        pen.arc (0, 0, radius + half, from, sweep);
        const OutlinePoint end = polar (radius, to);
        pen.arc (end.x, end.y, half, to, turn);
        pen.arc (0, 0, radius - half, to, -sweep);
        const OutlinePoint start = polar (radius, from);
        pen.arc (start.x, start.y, half, from + PI, turn);
        return pen.close ();
      }

      OutlineContour disc (float radius) {
        Pen pen ({ radius, 0.0f });
        pen.arc (0, 0, radius, 0.0f, 2.0f * PI);
        return pen.close ();
      }

      // A circle wound counterclockwise, or clockwise for a hole.
      OutlineContour circle (float radius, bool clockwise) {
        Pen pen ({ radius, 0.0f });
        pen.arc (0, 0, radius, 0.0f, clockwise ? -2.0f * PI : 2.0f * PI);
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
        case Mark::Boat:
          polygon ({ { -9, 0 }, { 12, 2 }, { 8, -5 }, { -6, -5 } });
          stroke ({ -9, 2 }, { 12, 4 }, 1.4f);
          polygon ({ { -7, 3 }, { -2, 3 }, { -2, 7 }, { -7, 7 } });
          stroke ({ -5, 4 }, { -11, -7 }, 1.4f);
          break;
        }
        return outlines;
      }

      // A radial capsule from `inner` to `outer` along +x, `half` wide.
      OutlineContour capsule (float inner, float outer, float half) {
        Pen pen ({ inner, -half });
        pen.line ({ outer, -half });
        pen.arc (outer, 0, half, -0.5f * PI, PI);
        pen.line ({ inner, half });
        pen.arc (inner, 0, half, 0.5f * PI, PI);
        return pen.close ();
      }

      // A corner of the chart's picture: where it falls on the screen and
      // where on the sheet, in periods of it (so that the integers are its
      // seams).
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
        const float quarter = 0.5f * PI;
        if (!clockwise) {
          Pen pen ({ x + corner, -y });
          pen.line ({ x + corner, y });
          pen.arc (x, y, corner, 0.0f, quarter);
          pen.line ({ -x, y + corner });
          pen.arc (-x, y, corner, quarter, quarter);
          pen.line ({ -x - corner, -y });
          pen.arc (-x, -y, corner, 2.0f * quarter, quarter);
          pen.line ({ x, -y - corner });
          pen.arc (x, -y, corner, 3.0f * quarter, quarter);
          return pen.close ();
        }
        Pen pen ({ x + corner, -y });
        pen.arc (x, -y, corner, 0.0f, -quarter);
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

    void bundle_waymarks (HudState& state) {
      const auto marks = state.waymarks;
      const int count = state.waymark_count;
      std::array<int, 8> groups;
      for (int i = 0; i < count; ++i)
        groups[i] = i;
      for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
          const auto& a = marks[i];
          const auto& b = marks[j];
          if (!a.vehicles || !b.vehicles || groups[i] == groups[j] ||
              a.alpha <= 0.002f || b.alpha <= 0.002f ||
              a.in_view != b.in_view || std::fabs (a.x - b.x) > 96.0f ||
              std::fabs (a.y - b.y) > 48.0f)
            continue;
          // A chain of crowded anchors must not join distant vehicles.
          bool nearby = true;
          for (int u = 0; u < count; ++u)
            for (int v = 0; v < count; ++v)
              if (groups[u] == groups[i] && groups[v] == groups[j] &&
                  length2 (marks[u].position - marks[v].position) >
                    80.0f * 80.0f)
                nearby = false;
          if (!nearby)
            continue;
          const int from = std::max (groups[i], groups[j]);
          const int to = std::min (groups[i], groups[j]);
          for (int k = 0; k < count; ++k)
            if (groups[k] == from)
              groups[k] = to;
        }
      }
      state.waymark_count = 0;
      for (int i = 0; i < count; ++i) {
        if (groups[i] != i)
          continue;
        auto mark = marks[i];
        int members = 1;
        float across = std::cos (mark.angle);
        float upward = std::sin (mark.angle);
        for (int j = i + 1; j < count; ++j) {
          if (groups[j] != i)
            continue;
          const auto& other = marks[j];
          mark.vehicles |= other.vehicles;
          mark.x += other.x;
          mark.y += other.y;
          mark.position += other.position;
          mark.metres = std::min (mark.metres, other.metres);
          mark.alpha = std::max (mark.alpha, other.alpha);
          across += std::cos (other.angle);
          upward += std::sin (other.angle);
          ++members;
        }
        if (members > 1) {
          mark.x /= members;
          mark.y /= members;
          mark.position = mark.position / static_cast<float> (members);
          mark.angle = std::atan2 (upward, across);
        }
        state.waymarks[state.waymark_count++] = mark;
      }
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
      std::array<SlugGlyph, 7> vehicles;
      // The chart's, in units of its radius: the bezel round it, the
      // hairline rim over the picture's edge, and the bezel's ticks.
      SlugGlyph bezel;
      SlugGlyph rim;
      SlugGlyph tick;
      SlugGlyph major_tick;
    };

    Hud::Hud ()
        : m_diagnostics (false), m_fps (0), m_gauge_alpha (0),
          m_reserve_alpha (0), m_reserve_hold (0), m_prompt_alpha (0),
          m_basket_alpha (0) {
      if (const char* mode = moppe::environment ("MOPPE_HUD")) {
        m_diagnostics = std::string (mode) == "debug";
        m_hidden = std::string (mode) == "none";
      }
      if (const char* chart = moppe::environment ("MOPPE_CHART"))
        m_chart_hidden = std::string (chart) == "none";
    }

    void Hud::load (render::Renderer& renderer) {
      try {
        m_font = render::Font::load (renderer, "fonts/IosevkaAile-Regular.ttf");
      } catch (const std::exception& error) {
        std::cerr << "moppe: HUD font unavailable: " << error.what () << '\n';
        m_font.reset ();
      }

      render::SlugAtlasBuilder builder;
      m_shape = std::make_unique<Shape> ();
      const auto add = [&] (std::vector<OutlineContour> contours) {
        return builder.add (contours);
      };
      const float hairline = 0.55f / gauge_radius;
      m_shape->track =
        add ({ stroked_arc (1.0f, hairline, gauge_start, -gauge_sweep) });
      {
        // A short radial capsule across the track, pointing along +x and
        // rotated into place when drawn.
        const float half = 1.15f / gauge_radius;
        const float inner = 0.89f, outer = 1.14f;
        Pen pen ({ inner, -half });
        pen.line ({ outer, -half });
        pen.arc (outer, 0, half, -0.5f * PI, PI);
        pen.line ({ inner, half });
        pen.arc (inner, 0, half, 0.5f * PI, PI);
        m_shape->notch = add ({ pen.close () });
      }
      {
        const float slot = gauge_sweep / reserve_dashes;
        const float sweep = slot - 7.0f * PI / 180.0f;
        m_shape->dash = add (
          { stroked_arc (0.8f, 0.45f / gauge_radius, -0.5f * sweep, sweep) });
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
        add ({ circle (chart_bezel, false), circle (0.995f, true) });
      m_shape->rim = add ({ circle (1.013f, false), circle (0.990f, true) });
      m_shape->tick = add ({ capsule (1.045f, 1.085f, 0.0085f) });
      m_shape->major_tick = add ({ capsule (1.035f, 1.125f, 0.011f) });
      for (unsigned i = 0; i < m_shape->vehicles.size (); ++i)
        m_shape->vehicles[i] = add (vehicle_outline (1u << i));
      m_shapes = renderer.create_glyph_set (builder.data ());
    }

    void Hud::place_shape (render::TextList& text,
                           const SlugGlyph& shape,
                           float cx,
                           float cy,
                           float scale,
                           float angle,
                           float red,
                           float green,
                           float blue,
                           float alpha,
                           float softness) const {
      if (!m_shapes || shape.empty () || alpha <= 0.002f)
        return;
      // Shapes are authored y-up; the HUD is y-down, so a counterclockwise
      // turn on screen negates the y components.
      const float c = std::cos (angle), s = std::sin (angle);
      render::GlyphQuad quad {};
      quad.origin[0] = cx;
      quad.origin[1] = cy;
      quad.origin[3] = softness;
      quad.axis_x[0] = scale * c;
      quad.axis_x[1] = -scale * s;
      quad.axis_y[0] = -scale * s;
      quad.axis_y[1] = -scale * c;
      quad.bounds[0] = shape.min_x;
      quad.bounds[1] = shape.min_y;
      quad.bounds[2] = shape.max_x;
      quad.bounds[3] = shape.max_y;
      quad.color[0] = red;
      quad.color[1] = green;
      quad.color[2] = blue;
      quad.color[3] = alpha;
      quad.glyph[0] = shape.band_offset;
      quad.glyph[1] = shape.horizontal_bands;
      quad.glyph[2] = shape.vertical_bands;
      text.add (m_shapes, quad);
    }

    void Hud::draw_shaded (render::TextList& text,
                           float x,
                           float y,
                           const std::string& line,
                           const TextStyle& style,
                           TextAlign align) const {
      if (!m_font || style.alpha <= 0.002f)
        return;
      TextStyle shade = style;
      shade.red = shade.green = shade.blue = 0.0f;
      shade.alpha = 0.32f * style.alpha;
      shade.softness = std::max (3.0f, 0.22f * style.size);
      m_font->draw (text, x, y + 0.04f * style.size, line, shade, align);
      m_font->draw (text, x, y, line, style, align);
    }

    void Hud::draw_prompt (render::TextList& text,
                           const std::string& key,
                           const std::string& action,
                           float alpha,
                           int width_pts,
                           int height_pts) const {
      if (!m_font || alpha <= 0.002f)
        return;
      const TextStyle words = prompt_words (alpha);
      draw_keyed (text,
                  0.5f * (width_pts - keyed_width (key, action, words)),
                  height_pts - 54.0f,
                  key,
                  action,
                  words,
                  alpha);
    }

    TextStyle Hud::prompt_words (float alpha) const {
      TextStyle words;
      words.size = 15.0f;
      words.red = words.green = words.blue = ink;
      words.alpha = 0.92f * alpha;
      words.tracking = 0.02f;
      return words;
    }

    float Hud::keyed_width (const std::string& key,
                            const std::string& action,
                            const TextStyle& words) const {
      if (!m_font)
        return 0.0f;
      const float key_width =
        key.empty () ? 0.0f : (key.size () > 1 ? 3.4f : 1.55f) * words.size;
      const float gap = key.empty () ? 0.0f : 0.6f * words.size;
      return key_width + gap + m_font->measure (action, words);
    }

    void Hud::draw_keyed (render::TextList& text,
                          float x,
                          float baseline,
                          const std::string& key,
                          const std::string& action,
                          const TextStyle& words,
                          float alpha) const {
      if (!m_font || alpha <= 0.002f)
        return;
      TextStyle cap = words;
      cap.size = (11.5f / 15.0f) * words.size;
      cap.tracking = 0.04f;
      cap.alpha = 0.86f * alpha;

      const bool has_key = !key.empty ();
      const bool wide = key.size () > 1;
      const float key_em = words.size;
      const float key_width = has_key ? (wide ? 3.4f : 1.55f) * key_em : 0.0f;
      const float gap = has_key ? 0.6f * words.size : 0.0f;

      if (has_key) {
        // The keycap sits on the text's optical centre line.
        const float cx = x + 0.5f * key_width;
        const float cy = baseline - 0.5f * m_font->cap_height (words.size);
        const SlugGlyph& outline = wide ? m_shape->wide_key : m_shape->key;
        place_shape (text,
                     outline,
                     cx,
                     cy,
                     key_em,
                     0.0f,
                     0.0f,
                     0.0f,
                     0.0f,
                     0.28f * alpha,
                     6.0f);
        place_shape (
          text, outline, cx, cy, key_em, 0.0f, ink, ink, ink, 0.7f * alpha);
        draw_shaded (text,
                     cx,
                     cy + 0.5f * m_font->cap_height (cap.size),
                     key,
                     cap,
                     TextAlign::Center);
        x += key_width + gap;
      }
      draw_shaded (text, x, baseline, action, words);
    }

    void Hud::draw_gauge (render::TextList& text,
                          const HudState& st,
                          float dt,
                          int width_pts,
                          int height_pts) {
      const bool instrument = !st.on_foot;
      m_gauge_alpha =
        approach (m_gauge_alpha, instrument ? 1.0f : 0.0f, 5.0f, dt);

      // The reserve shows only while it is spent or refilling, and lingers
      // a moment after it is full again.
      const float charge = clamp01 (st.boost_ready01);
      const bool spending = instrument && !st.gliding && charge < 0.995f;
      m_reserve_hold = spending ? 1.2f : std::max (0.0f, m_reserve_hold - dt);
      m_reserve_alpha = approach (
        m_reserve_alpha, m_reserve_hold > 0.0f ? 1.0f : 0.0f, 4.0f, dt);

      const float alpha = ease (m_gauge_alpha);
      if (alpha <= 0.002f || !m_font)
        return;

      const float r = gauge_radius;
      const float cx =
        width_pts - gauge_margin - r - (m_host_offers ? thumb_room : 0.0f);
      const float cy = height_pts - gauge_margin - r;

      // A broad, faint shade under the instrument keeps it legible over
      // snowfields without drawing a panel.
      place_shape (text,
                   m_shape->shade,
                   cx,
                   cy + 2.0f,
                   1.25f * r,
                   0.0f,
                   0.0f,
                   0.0f,
                   0.0f,
                   0.20f * alpha,
                   60.0f);

      place_shape (
        text, m_shape->track, cx, cy, r, 0.0f, ink, ink, ink, 0.30f * alpha);

      const float kmh = std::max (0.0f, st.speed_kmh);
      const float fraction = clamp01 (kmh / gauge_full_scale_kmh);
      const float angle = gauge_start - gauge_sweep * fraction;
      place_shape (text,
                   m_shape->notch,
                   cx,
                   cy,
                   r,
                   angle,
                   0.0f,
                   0.0f,
                   0.0f,
                   0.35f * alpha,
                   4.0f);
      place_shape (
        text, m_shape->notch, cx, cy, r, angle, ink, ink, ink, 0.95f * alpha);

      if (m_reserve_alpha > 0.002f) {
        const float slot = gauge_sweep / reserve_dashes;
        const float filled = charge * reserve_dashes;
        for (int i = 0; i < reserve_dashes; ++i) {
          const float lit = clamp01 (filled - i);
          const float a = (0.07f + 0.36f * lit) * m_reserve_alpha * alpha;
          place_shape (text,
                       m_shape->dash,
                       cx,
                       cy,
                       r,
                       gauge_start - (i + 0.5f) * slot,
                       ink,
                       ink,
                       ink,
                       a);
        }
      }

      TextStyle numeral;
      numeral.size = 25.0f;
      numeral.red = numeral.green = numeral.blue = ink;
      numeral.alpha = 0.94f * alpha;
      numeral.tabular_figures = true;
      const std::string speed = std::to_string ((int)std::lround (kmh));
      draw_shaded (text,
                   cx,
                   cy + 0.5f * m_font->cap_height (numeral.size),
                   speed,
                   numeral,
                   TextAlign::Center);

      TextStyle unit;
      unit.size = 8.5f;
      unit.red = unit.green = unit.blue = ink;
      unit.alpha = 0.55f * alpha;
      unit.tracking = 0.08f;
      draw_shaded (text, cx, cy + 0.80f * r, "km/h", unit, TextAlign::Center);

      if (st.gliding) {
        char climb[24];
        std::snprintf (climb,
                       sizeof climb,
                       "%+.1f m/s",
                       std::fabs (st.vertical_speed_mps) < 0.05f
                         ? 0.0f
                         : st.vertical_speed_mps);
        TextStyle vario = unit;
        vario.size = 10.0f;
        vario.alpha = 0.75f * alpha;
        vario.tracking = 0.02f;
        vario.tabular_figures = true;
        draw_shaded (text, cx, cy - r - 12.0f, climb, vario, TextAlign::Center);
      }

      // A wheelie counts its metres over the gauge as it goes, and the
      // one just ridden lingers there a moment with what it scored.
      const bool wheeling = st.wheelie_m >= 3.0f;
      const float shown = wheeling ? st.wheelie_m : st.last_wheelie_m;
      const float linger =
        wheeling ? 1.0f
                 : clamp01 (1.0f - (st.wheelie_age_s - 1.6f) / 0.6f) *
                     (st.last_wheelie_m >= 3.0f ? 1.0f : 0.0f);
      if (!st.gliding && linger > 0.002f) {
        char line[40];
        if (!wheeling && st.last_wheelie_points > 0)
          std::snprintf (line,
                         sizeof line,
                         "wheelie %.0f m  +%d",
                         shown,
                         st.last_wheelie_points);
        else
          std::snprintf (line, sizeof line, "wheelie %.0f m", shown);
        TextStyle tally = unit;
        tally.size = 10.0f;
        tally.alpha = 0.8f * alpha * ease (linger);
        tally.tracking = 0.02f;
        tally.tabular_figures = true;
        draw_shaded (text, cx, cy - r - 12.0f, line, tally, TextAlign::Center);
      }
    }

    void Hud::draw_basket (render::TextList& text,
                           const HudState& st,
                           float dt,
                           int width_pts,
                           int height_pts) {
      if (!m_font)
        return;
      // The tally takes the gauge's corner while walking with a basket that
      // has something in it.
      const bool showing =
        st.on_foot && (st.basket_total > 0 || st.fish_total > 0);
      m_basket_alpha =
        approach (m_basket_alpha, showing ? 1.0f : 0.0f, 4.0f, dt);
      const float alpha = ease (m_basket_alpha);
      // A pick is celebrated for a moment just above the prompt.
      const float fresh = clamp01 (1.0f - (st.reached_age_s - 1.4f) / 0.8f) *
                          clamp01 (st.reached_age_s / 0.12f);
      if (fresh > 0.002f && st.reached_name) {
        TextStyle cheer;
        cheer.size = st.reached_refused ? 15.0f : 21.0f;
        cheer.red = cheer.green = cheer.blue = ink;
        cheer.alpha = 0.95f * ease (fresh);
        cheer.tracking = 0.02f;
        const std::string name (st.reached_name);
        const std::string line =
          st.reached_refused ? "Not that one -- the " + name + " is poisonous!"
                             : "A " + name + "!";
        draw_shaded (text,
                     0.5f * width_pts,
                     height_pts - 96.0f - 10.0f * (1.0f - ease (fresh)),
                     line,
                     cheer,
                     TextAlign::Center);
      }
      // A word on supper, where a pick is celebrated.
      const float sated = clamp01 (1.0f - (st.meal_age_s - 3.2f) / 1.0f) *
                          clamp01 (st.meal_age_s / 0.25f);
      if (sated > 0.002f && st.meal) {
        TextStyle word;
        word.size = 19.0f;
        word.red = word.green = word.blue = ink;
        word.alpha = 0.95f * ease (sated);
        word.tracking = 0.02f;
        draw_shaded (text,
                     0.5f * width_pts,
                     height_pts - 96.0f - 10.0f * (1.0f - ease (sated)),
                     st.meal,
                     word,
                     TextAlign::Center);
      }
      // A fish landed, or lost, where a pick is celebrated.
      const float caught = clamp01 (1.0f - (st.catch_age_s - 3.0f) / 1.0f) *
                           clamp01 (st.catch_age_s / 0.2f);
      if (caught > 0.002f && st.catch_note) {
        TextStyle word;
        word.size = 21.0f;
        word.red = word.green = word.blue = ink;
        word.alpha = 0.95f * ease (caught);
        word.tracking = 0.02f;
        draw_shaded (text,
                     0.5f * width_pts,
                     height_pts - 96.0f - 10.0f * (1.0f - ease (caught)),
                     st.catch_note,
                     word,
                     TextAlign::Center);
      }
      // News of the other player, small at the top of the frame.
      const float told = clamp01 (1.0f - (st.notice_age_s - 4.0f) / 1.2f) *
                         clamp01 (st.notice_age_s / 0.4f);
      if (told > 0.002f && st.notice) {
        TextStyle news;
        news.size = 15.0f;
        news.red = news.green = news.blue = ink;
        news.alpha = 0.9f * ease (told);
        news.tracking = 0.04f;
        draw_shaded (
          text, 0.5f * width_pts, 54.0f, st.notice, news, TextAlign::Center);
      }
      if (alpha <= 0.002f)
        return;
      const float cx = width_pts - gauge_margin - gauge_radius -
                       (m_host_offers ? thumb_room : 0.0f);
      const float cy = height_pts - gauge_margin - gauge_radius;
      TextStyle numeral;
      numeral.size = 25.0f;
      numeral.red = numeral.green = numeral.blue = ink;
      numeral.alpha = 0.94f * alpha;
      numeral.tabular_figures = true;
      draw_shaded (text,
                   cx,
                   cy + 0.5f * m_font->cap_height (numeral.size),
                   std::to_string (st.basket_total > 0 ? st.basket_total
                                                       : st.fish_total),
                   numeral,
                   TextAlign::Center);
      TextStyle unit;
      unit.size = 8.5f;
      unit.red = unit.green = unit.blue = ink;
      unit.alpha = 0.55f * alpha;
      unit.tracking = 0.08f;
      draw_shaded (text,
                   cx,
                   cy + 0.80f * gauge_radius,
                   st.basket_total > 0 ? "in the basket" : "fish caught",
                   unit,
                   TextAlign::Center);
      if (st.basket_total > 0 && st.fish_total > 0)
        draw_shaded (text,
                     cx,
                     cy + 0.80f * gauge_radius + 11.0f,
                     "and " + std::to_string (st.fish_total) +
                       " fish",
                     unit,
                     TextAlign::Center);
    }

    // Someone lying in the hammock watches the hours go by: the time of
    // day, small, over the prompt.
    void Hud::draw_clock (render::TextList& text,
                          const HudState& st,
                          float dt,
                          int width_pts,
                          int height_pts) {
      m_clock_alpha =
        approach (m_clock_alpha, st.resting ? 1.0f : 0.0f, 1.5f, dt);
      const float alpha = ease (m_clock_alpha);
      if (!m_font || alpha <= 0.002f)
        return;
      const int minutes =
        static_cast<int> (std::floor (st.clock_hours * 60.0f)) % (24 * 60);
      char reading[8];
      std::snprintf (
        reading, sizeof reading, "%02d:%02d", minutes / 60, minutes % 60);
      TextStyle hour;
      hour.size = 17.0f;
      hour.red = hour.green = hour.blue = ink;
      hour.alpha = 0.72f * alpha;
      hour.tracking = 0.06f;
      hour.tabular_figures = true;
      draw_shaded (text,
                   0.5f * width_pts,
                   height_pts - 92.0f,
                   reading,
                   hour,
                   TextAlign::Center);
    }

    void Hud::draw_waymarks (render::TextList& text,
                             const HudState& st,
                             int width_pts,
                             int height_pts) {
      if (!m_font)
        return;
      const bool aside = m_diagnostics || m_host_offers;
      const auto chart = chart_placement (width_pts, height_pts, aside);
      for (int i = 0; i < st.waymark_count; ++i) {
        auto mark = st.waymarks[i];
        if (mark.alpha <= 0.002f)
          continue;
        const int count = std::popcount (mark.vehicles);
        const int rows = std::max (1, (count + 2) / 3);
        const int columns = (count + rows - 1) / rows;
        const float width = count ? 28.0f * columns : 140.0f;
        const float rise = 22.0f * (rows - 1);
        // The compass, its heading and map key own their corner. Slide
        // an edge finder along the edge rather than drawing across them.
        if (!mark.in_view && st.chart.picture && m_chart_alpha > 0.002f) {
          const float across = std::cos (mark.angle);
          const float left = mark.x - (across > 0.35f    ? width + 18.0f
                                       : across < -0.35f ? 10.0f
                                                         : 0.5f * width);
          const float right = mark.x + (across < -0.35f  ? width + 18.0f
                                        : across > 0.35f ? 10.0f
                                                         : 0.5f * width);
          const float top = chart.y - chart.outer - 36.0f;
          const float bottom = chart.y + chart.outer + 12.0f;
          if (left < chart.x + chart.outer + 65.0f &&
              right > chart.x - chart.outer - 8.0f && mark.y + 50.0f > top &&
              mark.y - 50.0f < bottom)
            mark.y = aside ? bottom + 48.0f : top - (count ? 48.0f : 42.0f);
        }
        // Quieter over the thing itself than out at the edge.
        const float alpha = mark.alpha * (mark.vehicles  ? 0.94f
                                          : mark.in_view ? 0.7f
                                                         : 0.82f);
        const float size = mark.in_view ? 5.5f : 7.0f;
        // cy is the bottom icon row. Extra rows grow upward, leaving
        // the distance and pointer together clear of the hiker's head.
        float cx = mark.x, cy = mark.y - 78.0f;
        if (count) {
          const float across = std::cos (mark.angle);
          const float upward = std::sin (mark.angle);
          if (!mark.in_view) {
            if (std::fabs (across) > 0.35f) {
              cx -= std::copysign (0.5f * width + 18.0f, across);
              cy = mark.y + 0.5f * rise;
            } else if (upward > 0.0f) {
              cy = mark.y + 26.0f + rise;
            } else {
              cy = mark.y - 44.0f;
            }
          }
          cx = std::clamp (
            cx,
            12.0f + 0.5f * width,
            std::max (12.0f + 0.5f * width, width_pts - 12.0f - 0.5f * width));
          cy = std::clamp (
            cy, 14.0f + rise, std::max (14.0f + rise, height_pts - 50.0f));
        }
        // Keep the in-view arrow attached to the icon-and-distance
        // group, rather than leaving it stranded down over the target.
        const float arrow_x = count && mark.in_view ? cx : mark.x;
        const float arrow_y = count && mark.in_view ? cy + 41.0f : mark.y;
        place_shape (text,
                     m_shape->arrow,
                     arrow_x,
                     arrow_y,
                     size + 1.5f,
                     mark.angle,
                     0.0f,
                     0.0f,
                     0.0f,
                     0.4f * alpha,
                     4.0f);
        place_shape (text,
                     m_shape->arrow,
                     arrow_x,
                     arrow_y,
                     size,
                     mark.angle,
                     mark.red,
                     mark.green,
                     mark.blue,
                     alpha);

        std::ostringstream reading;
        if (*mark.name)
          reading << mark.name << "  ";
        if (mark.metres >= 1000.0f)
          reading << std::fixed << std::setprecision (1)
                  << mark.metres / 1000.0f << " km";
        else
          reading << static_cast<int> (std::round (mark.metres / 5.0f) * 5.0f)
                  << " m";
        TextStyle label;
        label.size = mark.vehicles ? 12.0f : 11.0f;
        label.red = label.green = label.blue = ink;
        label.alpha = alpha;
        label.tracking = 0.04f;
        label.tabular_figures = true;
        if (count) {
          // At most three per row, balanced and individually centered:
          // four makes 2 + 2, five 3 + 2, and six 3 + 3.
          int icon = 0;
          for (unsigned v = 0; v < m_shape->vehicles.size (); ++v) {
            if (!(mark.vehicles & (1u << v)))
              continue;
            const int row = icon / columns;
            const int in_row = std::min (columns, count - row * columns);
            const float x = cx + (icon % columns - 0.5f * (in_row - 1)) * 28.0f;
            const float y = cy - rise + row * 22.0f;
            const auto& shape = m_shape->vehicles[v];
            // A soft shade keeps the small silhouettes legible over
            // sky, without a dark outline around every wheel and spoke.
            place_shape (text,
                         shape,
                         x,
                         y + 0.5f,
                         0.9f,
                         0.0f,
                         0,
                         0,
                         0,
                         0.5f * alpha,
                         2.0f);
            place_shape (text, shape, x, y, 0.9f, 0.0f, ink, ink, ink, alpha);
            ++icon;
          }
          draw_shaded (
            text, cx, cy + 28.0f, reading.str (), label, TextAlign::Center);
          continue;
        }
        // Behind the arrow: above the thing, or inward from the edge,
        // set to whichever side keeps the words clear of the arrowhead.
        const float across = std::cos (mark.angle);
        const float upward = std::sin (mark.angle);
        float x = mark.x, y = mark.y + 3.5f;
        TextAlign align = TextAlign::Center;
        if (mark.in_view) {
          y -= 15.0f;
        } else if (across > 0.35f) {
          x -= 13.0f;
          align = TextAlign::Right;
        } else if (across < -0.35f) {
          x += 13.0f;
          align = TextAlign::Left;
        } else {
          y += upward > 0.0f ? 18.0f : -18.0f;
        }
        draw_shaded (text, x, y, reading.str (), label, align);
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
                          float dt,
                          int width_pts,
                          int height_pts) {
      const HudState::Chart& chart = st.chart;
      const float width = static_cast<float> (width_pts);
      const float height = static_cast<float> (height_pts);
      const bool wanted = chart.picture && !m_chart_hidden && !st.resting &&
                          chart.period_x_m > 0.0f &&
                          chart.period_z_m > 0.0f &&
                          std::min (width, height) >= 240.0f;
      m_chart_alpha =
        approach (m_chart_alpha, wanted ? 1.0f : 0.0f, 3.0f, dt);
      const float pace = clamp01 ((st.speed_kmh - 15.0f) / 70.0f);
      const float reach =
        st.gliding || st.flying ? chart_reach_air_m
        : st.on_foot
          ? chart_reach_foot_m
          : chart_reach_foot_m +
              (chart_reach_fast_m - chart_reach_foot_m) * ease (pace);
      m_chart_reach_m = m_chart_reach_m > 0.0f
                          ? approach (m_chart_reach_m, reach, 0.8f, dt)
                          : reach;
      const float alpha = ease (m_chart_alpha);
      if (alpha <= 0.002f || !chart.picture || !m_font)
        return;

      // The lower left, balancing the gauge; or the upper right where the
      // diagnostics' trail map, or a touch screen's thumb, has that corner.
      const bool aside = m_diagnostics || m_host_offers;
      const auto placement = chart_placement (width_pts, height_pts, aside);
      const float r = placement.radius, outer = placement.outer;
      const float cx = placement.x, cy = placement.y;
      const float scale = r / m_chart_reach_m; // points a metre

      // The way ahead is up, and the screen's right is the ground's to
      // the right of it.
      float ax = chart.ahead_x, az = chart.ahead_z;
      if (const float n = std::hypot (ax, az); n > 1e-4f) {
        ax /= n;
        az /= n;
      } else {
        ax = 0.0f;
        az = -1.0f;
      }
      const float rx = -az, rz = ax;
      // A stretch of ground from the subject to the screen, in points
      // from the chart's middle (y down).
      const auto on_screen = [&] (float dx, float dz, float& sx, float& sy) {
        sx = (dx * rx + dz * rz) * scale;
        sy = -(dx * ax + dz * az) * scale;
      };
      const auto wrapped = [] (float delta, float period) {
        return delta - period * std::round (delta / period);
      };

      // The picture, as rings of cells cut at the sheet's seams.
      hud_state (dl);
      const float light = 0.62f + 0.38f * clamp01 (chart.daylight);
      dl.set_texture (chart.picture);
      dl.color (light * (0.90f + 0.10f * chart.daylight),
                light * (0.94f + 0.06f * chart.daylight),
                light,
                alpha);
      dl.begin (render::Prim::Triangles);
      constexpr int rings = 6;
      constexpr int sectors = 96;
      const float edge = 1.004f * r;
      const auto corner = [&] (float radius, float angle) {
        const float sx = radius * std::cos (angle);
        const float sy = radius * std::sin (angle);
        const float dx = (rx * sx - ax * sy) / scale;
        const float dz = (rz * sx - az * sy) / scale;
        return ChartCorner { cx + sx,
                             cy + sy,
                             (chart.x_m + dx) / chart.period_x_m,
                             (chart.z_m + dz) / chart.period_z_m };
      };
      for (int ring = 0; ring < rings; ++ring) {
        const float inner = edge * ring / rings;
        const float outer_ring = edge * (ring + 1) / rings;
        for (int sector = 0; sector < sectors; ++sector) {
          const float a0 = 2.0f * PI * sector / sectors;
          const float a1 = 2.0f * PI * (sector + 1) / sectors;
          ChartPolygon cell;
          if (ring == 0) {
            cell.corners[cell.count++] = corner (0.0f, 0.0f);
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
        const float a0 = 2.0f * PI * sector / sectors;
        const float a1 = 2.0f * PI * (sector + 1) / sectors;
        const float inner = 0.70f * r;
        const float c0 = std::cos (a0), s0 = std::sin (a0);
        const float c1 = std::cos (a1), s1 = std::sin (a1);
        const auto at = [&] (float radius, float c, float s, float shade) {
          dl.color (0.06f, 0.05f, 0.04f, shade * alpha);
          dl.vertex (cx + radius * c, cy + radius * s);
        };
        at (inner, c0, s0, 0.0f);
        at (edge, c0, s0, 0.20f);
        at (edge, c1, s1, 0.20f);
        at (inner, c0, s0, 0.0f);
        at (edge, c1, s1, 0.20f);
        at (inner, c1, s1, 0.0f);
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
                   cy + 2.0f,
                   1.08f * outer,
                   0.0f,
                   0.0f,
                   0.0f,
                   0.0f,
                   0.24f * alpha,
                   40.0f);
      place_shape (text,
                   m_shape->bezel,
                   cx,
                   cy,
                   r,
                   0.0f,
                   0.035f,
                   0.040f,
                   0.050f,
                   0.58f * alpha);
      place_shape (
        text, m_shape->rim, cx, cy, r, 0.0f, ink, ink, ink, 0.88f * alpha);

      // A bearing's direction on the screen, as place_shape turns: radians
      // counterclockwise from the right.
      const auto bearing_angle = [&] (float degrees) {
        const float b = degrees * PI / 180.0f;
        float sx, sy;
        on_screen (std::sin (b), -std::cos (b), sx, sy);
        return std::atan2 (-sy, sx);
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
                     bearing_angle (static_cast<float> (degrees)),
                     ink,
                     ink,
                     ink,
                     (major ? 0.85f : 0.45f) * alpha);
      }
      TextStyle quarter;
      quarter.size = std::clamp (0.135f * r, 9.0f, 12.5f);
      quarter.tracking = 0.02f;
      const char* const quarters[] = { "N", "E", "S", "W" };
      for (int q = 0; q < 4; ++q) {
        const float angle = bearing_angle (90.0f * q);
        const float middle = 0.5f * (1.0f + chart_bezel) * r;
        const bool north = q == 0;
        quarter.red = north ? 1.00f : ink;
        quarter.green = north ? 0.42f : ink;
        quarter.blue = north ? 0.33f : ink;
        quarter.alpha = (north ? 1.0f : 0.82f) * alpha;
        m_font->draw (text,
                      cx + middle * std::cos (angle),
                      cy - middle * std::sin (angle) +
                        0.5f * m_font->cap_height (quarter.size),
                      quarters[q],
                      quarter,
                      TextAlign::Center);
      }

      // The heading, read off a fixed mark over the bezel.
      const float heading_degrees =
        std::fmod (std::atan2 (ax, -az) * 180.0f / PI + 360.0f, 360.0f);
      place_shape (text,
                   m_shape->arrow,
                   cx,
                   cy - outer - 3.0f,
                   4.0f,
                   -0.5f * PI,
                   ink,
                   ink,
                   ink,
                   0.92f * alpha);
      char reading[8];
      std::snprintf (reading,
                     sizeof reading,
                     "%03d",
                     static_cast<int> (std::lround (heading_degrees)) % 360);
      TextStyle figure;
      figure.size = 11.5f;
      figure.red = figure.green = figure.blue = ink;
      figure.alpha = 0.9f * alpha;
      figure.tracking = 0.06f;
      figure.tabular_figures = true;
      draw_shaded (
        text, cx, cy - outer - 11.0f, reading, figure, TextAlign::Center);

      // What there is to find on it, pinned to the rim when off it.
      for (int i = 0; i < chart.pin_count; ++i) {
        const HudState::Chart::Pin& pin = chart.pins[i];
        float sx, sy;
        on_screen (wrapped (pin.x_m - chart.x_m, chart.period_x_m),
                   wrapped (pin.z_m - chart.z_m, chart.period_z_m),
                   sx,
                   sy);
        const float far = 0.86f * r;
        const float off = std::hypot (sx, sy);
        const bool beyond = off > far;
        if (beyond) {
          sx *= far / off;
          sy *= far / off;
        }
        using Kind = HudState::Chart::Pin::Kind;
        float size = pin.kind == Kind::Companion   ? 4.4f
                     : pin.kind == Kind::Trailhead ? 4.2f
                                                   : 3.4f;
        float a = alpha;
        if (beyond) {
          size *= 1.3f;
          a *= 0.8f;
        }
        const float x = cx + sx, y = cy + sy;
        const auto& shape = beyond ? m_shape->arrow : m_shape->shade;
        const float angle = beyond ? std::atan2 (-sy, sx) : 0.0f;
        place_shape (text,
                     shape,
                     x,
                     y + 0.6f,
                     size + 2.4f,
                     angle,
                     0.0f,
                     0.0f,
                     0.0f,
                     0.30f * a,
                     3.0f);
        place_shape (
          text, shape, x, y, size + 1.4f, angle, 1.0f, 1.0f, 1.0f, a);
        place_shape (
          text, shape, x, y, size, angle, pin.red, pin.green, pin.blue, a);
        if (pin.kind == Kind::Trailhead && !beyond)
          place_shape (text,
                       m_shape->shade,
                       x,
                       y,
                       0.45f * size,
                       0.0f,
                       1.0f,
                       1.0f,
                       1.0f,
                       a);
      }

      // The subject, facing the way they face.
      float fx, fy;
      on_screen (chart.facing_x, chart.facing_z, fx, fy);
      const float facing = std::hypot (fx, fy) > 1e-6f
                             ? std::atan2 (-fy, fx)
                             : 0.5f * PI;
      place_shape (text,
                   m_shape->arrow,
                   cx,
                   cy + 0.5f,
                   9.5f,
                   facing,
                   0.0f,
                   0.0f,
                   0.0f,
                   0.35f * alpha,
                   4.0f);
      place_shape (text,
                   m_shape->arrow,
                   cx,
                   cy,
                   8.6f,
                   facing,
                   1.0f,
                   1.0f,
                   1.0f,
                   alpha);
      place_shape (text,
                   m_shape->arrow,
                   cx,
                   cy,
                   6.4f,
                   facing,
                   0.80f,
                   0.16f,
                   0.10f,
                   alpha);

      // The key that unfolds the whole map, small against the bezel's
      // lower outer side.
      if (*map_key && !m_host_offers) {
        TextStyle words = prompt_words (0.8f * alpha);
        words.size = 10.5f;
        const float run = keyed_width (map_key, "Map", words);
        const float diagonal = 0.72f * outer;
        const float x =
          aside ? cx - diagonal - 4.0f - run : cx + diagonal + 4.0f;
        draw_keyed (text,
                    x,
                    cy + diagonal + 0.5f * m_font->cap_height (words.size),
                    map_key,
                    "Map",
                    words,
                    0.8f * alpha);
      }
    }

    void Hud::draw_prompts (render::TextList& text,
                            const HudState& st,
                            float dt,
                            int width_pts,
                            int height_pts) {
      std::string key, action;
      if (st.hauling) {
        key = mount_key;
        action = "Let go of " + std::string (st.hauling);
      } else if (st.angle != HudState::Angle::none &&
                 st.angle != HudState::Angle::cast) {
        key = deploy_key;
        switch (st.angle) {
        case HudState::Angle::wait:
          action = *deploy_key ? "Reel in" : "Waiting for a bite";
          break;
        case HudState::Angle::strike:
          action = *deploy_key ? "Strike!" : "A bite!";
          break;
        case HudState::Angle::playing:
          key = "";
          action = "Reeling it in";
          break;
        default:
          break;
        }
      } else if (st.on_foot && st.mushroom_in_reach) {
        const std::string name = capitalized (st.mushroom_in_reach);
        if (!st.mushroom_edible) {
          action = name + " -- poisonous, leave it be";
        } else {
          key = deploy_key;
          action = *deploy_key
                     ? "Pick the " + std::string (st.mushroom_in_reach)
                     : name + " within reach";
        }
      } else if (st.on_foot && st.cook != HudState::Cook::none) {
        key = deploy_key;
        switch (st.cook) {
        case HudState::Cook::fry:
          action = st.cook_fish
                     ? (*deploy_key ? "Fry the fish" : "Fish to fry")
                     : (*deploy_key ? "Fry the mushrooms" : "Mushrooms to fry");
          break;
        case HudState::Cook::frying:
          key = "";
          action = st.cook_fish ? "The fish is frying"
                                : "The mushrooms are frying";
          break;
        case HudState::Cook::eat:
          action = *deploy_key ? "Eat" : "Supper is ready";
          break;
        case HudState::Cook::eat_burnt:
          action = *deploy_key ? "Eat them anyway" : "Supper is burnt";
          break;
        case HudState::Cook::none:
          break;
        }
      } else if (st.on_foot && st.camp != HudState::Camp::none) {
        switch (st.camp) {
        case HudState::Camp::hang:
          key = hammock_key;
          action = *hammock_key ? "Hang the hammock"
                                : "Two good trees for the hammock";
          break;
        case HudState::Camp::lie_down:
          key = deploy_key;
          action = *deploy_key ? "Lie down" : "The hammock";
          break;
        case HudState::Camp::get_up:
          key = deploy_key;
          action = "Get up";
          break;
        case HudState::Camp::light_fire:
          key = fire_key;
          action = "Light a fire";
          break;
        case HudState::Camp::none:
          break;
        }
      } else if (st.angle == HudState::Angle::cast) {
        key = deploy_key;
        action = *deploy_key ? "Cast a line" : "Water to fish";
      } else if (st.can_drop_bike) {
        key = deploy_key;
        action = "Release the Trail 90";
      } else if (st.slung) {
        key = mount_key;
        action = *mount_key ? "Let " + std::string (st.slung) + " go"
                            : capitalized (st.slung) + " is on the line";
      } else if (st.hook) {
        key = mount_key;
        action = *mount_key ? "Hook " + std::string (st.hook)
                            : capitalized (st.hook) + " is under the hook";
      } else if (st.can_deploy_glider) {
        key = deploy_key;
        action = *deploy_key ? "Deploy glider" : "Glider ready";
      } else if (st.haul) {
        key = mount_key;
        // The boat is dragged the other way, back into the water.
        const bool boat = std::string_view (st.haul) == "the boat";
        action = *mount_key ? boat ? "Drag the boat by its handle"
                                   : "Haul " + std::string (st.haul) + " out"
                 : boat     ? "The boat is aground"
                            : capitalized (st.haul) + " is stuck";
      } else if (st.can_ride_along) {
        key = mount_key;
        action = *mount_key ? "Ride along" : "A seat is free";
      } else if (st.can_drive_tractor) {
        key = mount_key;
        action = *mount_key ? "Drive the tractor" : "Tractor within reach";
      } else if (st.can_drive_caddy) {
        key = mount_key;
        action = *mount_key ? "Drive the Caddy" : "Caddy within reach";
      } else if (st.can_take_boat) {
        key = mount_key;
        action = *mount_key ? "Take the boat out" : "Boat within reach";
      } else if (st.can_ride_mountain_bike) {
        key = mount_key;
        action =
          *mount_key ? "Ride the mountainbike" : "Mountainbike within reach";
      } else if (st.can_fly) {
        key = mount_key;
        action = *mount_key ? "Fly the helicopter" : "Helicopter within reach";
      } else if (st.can_drive) {
        key = mount_key;
        action = *mount_key ? "Drive" : "Jeep within reach";
      } else if (st.can_mount) {
        key = mount_key;
        action = *mount_key ? "Ride" : "Bike within reach";
      } else if (st.stuck) {
        action = "Stuck! Keep at it, or step out and haul";
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
        if (m_prompt_alpha < 0.05f || action == m_prompt_action) {
          m_prompt_key = key;
          m_prompt_action = action;
        }
      }
      const bool showing = wanted && action == m_prompt_action;
      m_prompt_alpha = approach (
        m_prompt_alpha, showing ? 1.0f : 0.0f, showing ? 7.0f : 5.0f, dt);
      // A host with a button for it shows a keyed prompt itself.
      if (!m_host_offers || m_prompt_key.empty ())
        draw_prompt (text,
                     m_prompt_key,
                     m_prompt_action,
                     ease (m_prompt_alpha),
                     width_pts,
                     height_pts);
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
                                int width_pts,
                                int height_pts) {
      (void)width_pts;
      (void)height_pts;
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
      style.red = style.green = style.blue = ink;
      style.alpha = 0.85f;
      style.tabular_figures = true;
      const float frame_ms = std::max (st.frame_time_s, 1e-4f) * 1000.0f;
      const float heading =
        std::fmod (st.heading_radians * 180.0f / PI + 360.0f, 360.0f);
      char lines[9][96];
      std::snprintf (
        lines[0], sizeof lines[0], "%.0f fps   %.1f ms", m_fps, frame_ms);
      std::snprintf (lines[1],
                     sizeof lines[1],
                     "%.0f km/h   heading %03.0f°",
                     st.speed_kmh,
                     heading);
      std::snprintf (lines[2],
                     sizeof lines[2],
                     "air %.1f s   whip %.0f°",
                     st.airtime_s,
                     st.spin_degrees);
      std::snprintf (lines[3],
                     sizeof lines[3],
                     "last %s %.1f s   %.0f°   +%d",
                     st.landed_clean ? "clean" : "landing",
                     st.landed_airtime_s,
                     st.landed_spin_degrees,
                     st.landed_points);
      std::snprintf (lines[4],
                     sizeof lines[4],
                     "wheelie %.0f m   last %.0f m +%d   best %.0f m",
                     st.wheelie_m,
                     st.last_wheelie_m,
                     st.last_wheelie_points,
                     st.best_wheelie_m);
      std::snprintf (lines[5], sizeof lines[5], "score %d", st.score);
      std::snprintf (lines[6],
                     sizeof lines[6],
                     "health %.0f%%   lives %d   stars %d",
                     clamp01 (st.health01) * 100.0f,
                     st.lives,
                     st.stars);
      std::snprintf (lines[7],
                     sizeof lines[7],
                     "odometer %.2f km   boost %.0f%%",
                     st.odometer_m / 1000.0f,
                     clamp01 (st.boost_ready01) * 100.0f);
      std::snprintf (
        lines[8], sizeof lines[8], "climb %+.1f m/s", st.vertical_speed_mps);
      float y = 24.0f;
      for (const char* line : lines) {
        draw_shaded (text, 16.0f, y, line, style);
        y += 15.0f;
      }
    }

    void Hud::draw (render::DrawList& dl,
                    render::TextList& text,
                    const HudState& st,
                    int width_pts,
                    int height_pts) {
      const float dt = std::clamp (st.frame_time_s, 0.0f, 0.1f);
      const float instant_fps =
        std::min (240.0f, 1.0f / std::max (st.frame_time_s, 0.001f));
      m_fps = m_fps > 0.0f ? m_fps * 0.9f + instant_fps * 0.1f : instant_fps;
      if (m_hidden)
        return;

      draw_chart (dl, text, st, dt, width_pts, height_pts);
      draw_waymarks (text, st, width_pts, height_pts);
      draw_gauge (text, st, dt, width_pts, height_pts);
      draw_basket (text, st, dt, width_pts, height_pts);
      draw_clock (text, st, dt, width_pts, height_pts);
      draw_prompts (text, st, dt, width_pts, height_pts);
      if (m_diagnostics)
        draw_diagnostics (dl, text, st, width_pts, height_pts);
    }

    void Hud::draw_game_over (render::DrawList& dl,
                              render::TextList& text,
                              int width_pts,
                              int height_pts) {
      const float w = (float)width_pts;
      const float h = (float)height_pts;
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
      title.red = 0.92f;
      title.green = 0.88f;
      title.blue = 0.84f;
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
          text, restart_key, "Ride again", 0.8f, width_pts, height_pts);
    }

    void Hud::draw_quit_question (render::DrawList& dl,
                                  render::TextList& text,
                                  int width_pts,
                                  int height_pts) const {
      const float w = (float)width_pts;
      const float h = (float)height_pts;
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

      TextStyle title = prompt_words (1.0f);
      title.size = 30.0f;
      draw_shaded (text,
                   0.5f * w,
                   0.5f * h - 14.0f,
                   "Quit Moppe?",
                   title,
                   TextAlign::Center);
      // Esc again quits; any other key goes back to the game.
      const TextStyle words = prompt_words (0.9f);
      const float space = 2.0f * words.size;
      const float quit = keyed_width ("Esc", "Quit", words);
      const float stay = keyed_width ("Space", "Keep playing", words);
      const float x = 0.5f * (w - quit - space - stay);
      const float baseline = 0.5f * h + 30.0f;
      draw_keyed (text, x, baseline, "Esc", "Quit", words, 0.9f);
      draw_keyed (
        text, x + quit + space, baseline, "Space", "Keep playing", words, 0.9f);
    }

    void Hud::draw_ride_prompt (render::TextList& text,
                                float alpha,
                                int width_pts,
                                int height_pts) {
      offer (ride_key, "Begin");
      if (*ride_key && !m_host_offers)
        draw_prompt (text, ride_key, "Begin", alpha, width_pts, height_pts);
    }

    void Hud::draw_title_card (render::TextList& text,
                               bool title,
                               const std::string& line,
                               float alpha,
                               float y,
                               int width_pts,
                               int height_pts) const {
      if (!m_font || alpha <= 0.002f)
        return;
      const float height = static_cast<float> (height_pts);
      TextStyle style;
      style.red = style.green = style.blue = ink;
      if (title) {
        // The name alone, large and widely spaced, at the frame's optical
        // centre.
        style.size = std::clamp (0.105f * height, 30.0f, 140.0f);
        style.tracking = 0.14f;
        style.alpha = 0.94f * alpha;
        const float baseline = (y >= 0.0f ? y : 0.47f) * height +
                               0.5f * m_font->x_height (style.size);
        draw_shaded (
          text, 0.5f * width_pts, baseline, line, style, TextAlign::Center);
      } else {
        // A credit, small, in the lower third.
        style.size = std::clamp (0.027f * height, 13.0f, 40.0f);
        style.tracking = 0.09f;
        style.alpha = 0.9f * alpha;
        draw_shaded (text,
                     0.5f * width_pts,
                     (y >= 0.0f ? y : 0.74f) * height,
                     line,
                     style,
                     TextAlign::Center);
      }
    }
  }
}
