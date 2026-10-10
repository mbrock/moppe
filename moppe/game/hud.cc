#include <moppe/game/hud.hh>
#include <moppe/render/renderer.hh>

#include <moppe/environment.hh>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
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
#elif defined(__APPLE__) && TARGET_OS_IPHONE
      constexpr const char* deploy_key = "";
      constexpr const char* mount_key = "";
      constexpr const char* restart_key = "";
      constexpr const char* ride_key = "";
      constexpr const char* hammock_key = "";
      constexpr const char* fire_key = "";
#else
      constexpr const char* deploy_key = "E";
      constexpr const char* mount_key = "F";
      constexpr const char* restart_key = "R";
      constexpr const char* ride_key = "Space";
      constexpr const char* hammock_key = "R";
      constexpr const char* fire_key = "T";
#endif

      // The gauge: a hairline arc of radius gauge_radius points, open at the
      // bottom, with a notch riding it from zero at lower left to full
      // scale at lower right.
      constexpr float gauge_radius = 30.0f;
      constexpr float gauge_margin = 30.0f;
      constexpr float gauge_start = 225.0f * PI / 180.0f;
      constexpr float gauge_sweep = 270.0f * PI / 180.0f;
      constexpr float gauge_full_scale_kmh = 160.0f;
      constexpr int reserve_dashes = 12;

      constexpr float ink = 0.96f; // the off-white every element is set in

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

    // The gauge and prompt outlines, in units of the gauge radius (gauge
    // shapes) or of the prompt's em (keycaps).
    struct Hud::Shape {
      SlugGlyph track;
      SlugGlyph notch;
      SlugGlyph dash;
      SlugGlyph shade;
      SlugGlyph key;
      SlugGlyph wide_key;
    };

    Hud::Hud ()
        : m_diagnostics (false), m_fps (0), m_gauge_alpha (0),
          m_reserve_alpha (0), m_reserve_hold (0), m_prompt_alpha (0),
          m_basket_alpha (0) {
      if (const char* mode = moppe::environment ("MOPPE_HUD"))
        m_diagnostics = std::string (mode) == "debug";
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
      m_shape->shade = add ({ disc (1.0f) });
      m_shape->key = add ({ rounded_rect (1.55f, 1.55f, 0.32f, false),
                            rounded_rect (1.43f, 1.43f, 0.26f, true) });
      m_shape->wide_key = add ({ rounded_rect (3.4f, 1.55f, 0.32f, false),
                                 rounded_rect (3.28f, 1.43f, 0.26f, true) });
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
      TextStyle words;
      words.size = 15.0f;
      words.red = words.green = words.blue = ink;
      words.alpha = 0.92f * alpha;
      words.tracking = 0.02f;

      TextStyle cap = words;
      cap.size = 11.5f;
      cap.tracking = 0.04f;
      cap.alpha = 0.86f * alpha;

      const float baseline = height_pts - 54.0f;
      const bool has_key = !key.empty ();
      const bool wide = key.size () > 1;
      const float key_em = words.size;
      const float key_width = has_key ? (wide ? 3.4f : 1.55f) * key_em : 0.0f;
      const float gap = has_key ? 0.6f * words.size : 0.0f;
      const float width = key_width + gap + m_font->measure (action, words);
      float x = 0.5f * (width_pts - width);

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
      const float cx = width_pts - gauge_margin - r;
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
      const bool showing = st.on_foot && st.basket_total > 0;
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
          st.reached_refused
            ? "Not that one -- the " + name + " is poisonous!"
            : "A " + name + "!";
        draw_shaded (text,
                     0.5f * width_pts,
                     height_pts - 96.0f - 10.0f * (1.0f - ease (fresh)),
                     line,
                     cheer,
                     TextAlign::Center);
      }
      if (alpha <= 0.002f)
        return;
      const float cx = width_pts - gauge_margin - gauge_radius;
      const float cy = height_pts - gauge_margin - gauge_radius;
      TextStyle numeral;
      numeral.size = 25.0f;
      numeral.red = numeral.green = numeral.blue = ink;
      numeral.alpha = 0.94f * alpha;
      numeral.tabular_figures = true;
      draw_shaded (text,
                   cx,
                   cy + 0.5f * m_font->cap_height (numeral.size),
                   std::to_string (st.basket_total),
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
                   "in the basket",
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

    void Hud::draw_prompts (render::TextList& text,
                            const HudState& st,
                            float dt,
                            int width_pts,
                            int height_pts) {
      std::string key, action;
      if (st.on_foot && st.mushroom_in_reach) {
        const std::string name = capitalized (st.mushroom_in_reach);
        if (!st.mushroom_edible) {
          action = name + " -- poisonous, leave it be";
        } else {
          key = deploy_key;
          action = *deploy_key ? "Pick the " + std::string (st.mushroom_in_reach)
                               : name + " within reach";
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
      } else if (st.can_drop_bike) {
        key = deploy_key;
        action = "Release the motocross";
      } else if (st.can_deploy_glider) {
        key = deploy_key;
        action = *deploy_key ? "Deploy glider" : "Glider ready";
      } else if (st.can_mount) {
        key = mount_key;
        action = *mount_key ? "Ride" : "Bike within reach";
      }
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
      draw_prompt (text,
                   m_prompt_key,
                   m_prompt_action,
                   ease (m_prompt_alpha),
                   width_pts,
                   height_pts);
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
      dl.vertex (268.0f, 138.0f);
      dl.vertex (8.0f, 138.0f);
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
      char lines[8][96];
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
      std::snprintf (lines[4], sizeof lines[4], "score %d", st.score);
      std::snprintf (lines[5],
                     sizeof lines[5],
                     "health %.0f%%   lives %d   stars %d",
                     clamp01 (st.health01) * 100.0f,
                     st.lives,
                     st.stars);
      std::snprintf (lines[6],
                     sizeof lines[6],
                     "odometer %.2f km   boost %.0f%%",
                     st.odometer_m / 1000.0f,
                     clamp01 (st.boost_ready01) * 100.0f);
      std::snprintf (
        lines[7], sizeof lines[7], "climb %+.1f m/s", st.vertical_speed_mps);
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
      draw_prompt (
        text, restart_key, "Ride again", 0.8f, width_pts, height_pts);
    }

    void Hud::draw_ride_prompt (render::TextList& text,
                                float alpha,
                                int width_pts,
                                int height_pts) const {
      if (*ride_key)
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
