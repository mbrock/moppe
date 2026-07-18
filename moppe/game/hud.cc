#include <moppe/game/hud.hh>
#include <moppe/render/renderer.hh>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace moppe {
  namespace game {
    using render::DrawList;
    using render::Prim;

    // ---- instrument cluster drawing helpers --------------------------

    namespace {
      const float PI = 3.14159265f;

      struct Point {
        float x;
        float y;

        Point () : x (0), y (0) {}
        Point (float px, float py) : x (px), y (py) {}
      };

      struct Rect {
        float x;
        float y;
        float width;
        float height;

        Rect (float px, float py, float w, float h)
            : x (px), y (py), width (w), height (h) {}
      };

      struct Dial {
        Point center;
        float radius;

        Dial () : radius (0) {}
        Dial (const Point& c, float r) : center (c), radius (r) {}

        Point at (float radius_scale, float angle_deg) const {
          const float a = angle_deg * PI / 180.0f;
          return Point (center.x + radius_scale * radius * std::cos (a),
                        center.y - radius_scale * radius * std::sin (a));
        }
      };

      // Positions are local to two anchors: the status panel's top-left
      // and the instrument cluster's bottom-right.  Mini dials are derived
      // from the speed dial, keeping their column aligned by construction.
      struct HudLayout {
        Rect status;
        Point cluster_anchor;
        Dial speed;
        Dial boost;

        HudLayout (float width, float height)
            : status (10, 8, 171, 77), cluster_anchor (width - 10, height - 10),
              speed (Point (-58, -61), 54) {
          const float mini_x = speed.center.x - speed.radius - 33.0f;
          boost = Dial (Point (mini_x, speed.center.y), 28);
        }
      };

      float clamp01 (float value) {
        return std::max (0.0f, std::min (1.0f, value));
      }

      float wrap_degrees (float value) {
        return value - 360.0f * std::floor ((value + 180.0f) / 360.0f);
      }

      // Segment count for a screen-space arc: one segment per ~4px of
      // arc length reads as a smooth curve at HUD scale.
      int arc_segs (float radius_px, float sweep_deg) {
        const float arc = std::fabs (sweep_deg) * (PI / 180.0f) * radius_px;
        return std::max (8, std::min (48, (int)(arc * 0.25f)));
      }

      void hud_disc (DrawList& dl,
                     const Point& center,
                     float r,
                     float cr,
                     float cg,
                     float cb,
                     float ca) {
        const int segs = arc_segs (r, 360);
        dl.color (cr, cg, cb, ca);
        dl.begin (Prim::TriangleFan);
        dl.vertex (center.x, center.y);
        for (int i = 0; i <= segs; ++i) {
          const float a = 2.0f * PI * i / segs;
          dl.vertex (center.x + r * std::cos (a), center.y + r * std::sin (a));
        }
        dl.end ();
      }

      // Annulus segment; dial angle convention: 0 deg = +x, angles
      // increase counterclockwise ON SCREEN (y is down, so -sin)
      void hud_ring (DrawList& dl,
                     const Dial& dial,
                     float r0,
                     float r1,
                     float a0,
                     float a1,
                     float cr,
                     float cg,
                     float cb,
                     float ca) {
        dl.color (cr, cg, cb, ca);
        dl.begin (Prim::TriangleStrip);
        const int segs = arc_segs (dial.radius * r1, a1 - a0);
        const float ri = dial.radius * r0, ro = dial.radius * r1;
        for (int i = 0; i <= segs; ++i) {
          const float a = (a0 + (a1 - a0) * i / segs) * (PI / 180.0f);
          const float ca_ = std::cos (a), sa = std::sin (a);
          dl.vertex (dial.center.x + ro * ca_, dial.center.y - ro * sa);
          dl.vertex (dial.center.x + ri * ca_, dial.center.y - ri * sa);
        }
        dl.end ();
      }

      void hud_ticks (DrawList& dl,
                      const Dial& dial,
                      int count,
                      float a0,
                      float a1,
                      float inner,
                      float outer,
                      float width,
                      float cr,
                      float cg,
                      float cb,
                      float ca) {
        dl.color (cr, cg, cb, ca);
        for (int i = 0; i < count; ++i) {
          const float f = count > 1 ? (float)i / (count - 1) : 0.0f;
          const float angle = a0 + (a1 - a0) * f;
          const Point p0 = dial.at (inner, angle);
          const Point p1 = dial.at (outer, angle);
          dl.line (p0.x, p0.y, p1.x, p1.y, width);
        }
      }

      // Metallic bezel + dark face + glass highlight
      void hud_dial_face (DrawList& dl, const Dial& dial) {
        hud_disc (
          dl, dial.center, dial.radius * 1.12f, 0.0f, 0.0f, 0.0f, 0.25f);
        hud_ring (dl, dial, 0.94f, 1.08f, 0, 360, 0.52f, 0.54f, 0.58f, 0.95f);
        hud_ring (dl, dial, 1.02f, 1.08f, 0, 360, 0.24f, 0.25f, 0.27f, 0.95f);
        hud_disc (
          dl, dial.center, dial.radius * 0.96f, 0.035f, 0.045f, 0.06f, 0.94f);
        // glass reflection across the upper left
        hud_ring (dl, dial, 0.45f, 0.92f, 115, 205, 1.0f, 1.0f, 1.0f, 0.055f);
      }

      void hud_panel (DrawList& dl,
                      float x0,
                      float y0,
                      float x1,
                      float y1,
                      float corner,
                      float alpha) {
        dl.color (0.045f, 0.055f, 0.075f, alpha);
        dl.begin (Prim::Polygon);
        dl.vertex (x0 + corner, y0);
        dl.vertex (x1 - corner, y0);
        dl.vertex (x1, y0 + corner);
        dl.vertex (x1, y1 - corner);
        dl.vertex (x1 - corner, y1);
        dl.vertex (x0 + corner, y1);
        dl.vertex (x0, y1 - corner);
        dl.vertex (x0, y0 + corner);
        dl.end ();

        // Complete outline; all eight segments share the panel transform.
        dl.color (0.46f, 0.54f, 0.67f, 0.48f);
        dl.line (x0 + corner, y0, x1 - corner, y0, 1.5f);
        dl.line (x1 - corner, y0, x1, y0 + corner, 1.5f);
        dl.line (x1, y0 + corner, x1, y1 - corner, 1.5f);
        dl.line (x1, y1 - corner, x1 - corner, y1, 1.5f);
        dl.line (x1 - corner, y1, x0 + corner, y1, 1.5f);
        dl.line (x0 + corner, y1, x0, y1 - corner, 1.5f);
        dl.line (x0, y1 - corner, x0, y0 + corner, 1.5f);
        dl.line (x0, y0 + corner, x0 + corner, y0, 1.5f);

        dl.color (0.72f, 0.8f, 0.92f, 0.32f);
        dl.line (x0 + corner, y0, x1 - corner, y0, 1.0f);
      }

      // A little heart: two lobes and a point
      void hud_heart (DrawList& dl, float x, float y, float s, bool full) {
        if (full)
          dl.color (0.95f, 0.2f, 0.25f, 0.95f);
        else
          dl.color (0.28f, 0.24f, 0.26f, 0.9f);

        dl.begin (Prim::Triangles);
        dl.vertex (x - 0.72f * s, y - 0.02f * s);
        dl.vertex (x + 0.72f * s, y - 0.02f * s);
        dl.vertex (x, y + 0.9f * s);
        dl.end ();

        // the two lobes (color is already set)
        dl.begin (Prim::TriangleFan);
        dl.vertex (x - 0.36f * s, y - 0.25f * s);
        for (int i = 0; i <= 12; ++i) {
          const float a = 2.0f * 3.14159f * i / 12;
          dl.vertex (x - 0.36f * s + 0.42f * s * std::cos (a),
                     y - 0.25f * s + 0.42f * s * std::sin (a));
        }
        dl.end ();
        dl.begin (Prim::TriangleFan);
        dl.vertex (x + 0.36f * s, y - 0.25f * s);
        for (int i = 0; i <= 12; ++i) {
          const float a = 2.0f * 3.14159f * i / 12;
          dl.vertex (x + 0.36f * s + 0.42f * s * std::cos (a),
                     y - 0.25f * s + 0.42f * s * std::sin (a));
        }
        dl.end ();
      }

      // The 2D overlay state: blend on, no depth, no cull (the y-down
      // ortho flips winding), unlit, unfogged.
      void hud_state_on (DrawList& dl) {
        render::DrawState s;
        s.blend = true;
        s.depth_test = false;
        s.depth_write = false;
        s.cull = false;
        dl.state (s);
        dl.lit (false);
        dl.fogged (false);
      }

      void hud_state_off (DrawList& dl) {
        dl.state (render::DrawState ());
        dl.lit (true);
        dl.fogged (true);
        dl.color (1, 1, 1, 1);
      }

      // Panel rectangles used by both the static bake and the live
      // overlays drawn on top of them.
      Rect compass_rect (float width_pts) {
        const float w = std::min (230.0f, width_pts * 0.38f);
        return Rect ((width_pts - w) * 0.5f, 8.0f, w, 43.0f);
      }

      Rect ecu_rect (float width_pts) {
        return Rect (width_pts - 126.0f, 8.0f, 116.0f, 43.0f);
      }
    }

    // ---- the cluster itself -------------------------------------------

    Hud::Hud ()
        : m_fps (0), m_fps_cursor (0), m_fps_count (0), m_static_width (-1),
          m_static_height (-1) {
      std::fill (m_fps_history, m_fps_history + 48, 0.0f);
    }

    void Hud::load (render::Renderer& renderer) {
      const float s = renderer.scale_factor ();
      m_helv10.reset (new render::FontAtlas (renderer, "Helvetica", 10.0f, s));
      m_helv12.reset (new render::FontAtlas (renderer, "Helvetica", 12.0f, s));
      m_display24.reset (new render::FontAtlas (renderer, "Menlo", 24.0f, s));
      m_times24.reset (
        new render::FontAtlas (renderer, "Times New Roman", 24.0f, s));
    }

    // Tessellate the layout-static parts of the overlay once.  Every
    // live element drawn per frame sits strictly on top of these (or
    // doesn't overlap them), so splicing the bake first preserves the
    // original paint order.
    void Hud::rebuild_static (int width_pts, int height_pts) {
      DrawList& dl = m_static;
      dl.clear ();

      const HudLayout layout ((float)width_pts, (float)height_pts);
      hud_state_on (dl);

      // ---- instrument cluster ----
      dl.push ();
      dl.translate (layout.cluster_anchor.x, layout.cluster_anchor.y, 0);

      // Boost: face and the lightning-bolt mark inside the charge arc.
      hud_dial_face (dl, layout.boost);
      const Point boost = layout.boost.center;
      dl.color (0.9f, 0.96f, 1.0f, 0.95f);
      dl.begin (Prim::Triangles);
      dl.vertex (boost.x + 2, boost.y - 14);
      dl.vertex (boost.x - 9, boost.y + 2);
      dl.vertex (boost.x, boost.y + 1);
      dl.vertex (boost.x, boost.y - 1);
      dl.vertex (boost.x + 9, boost.y - 2);
      dl.vertex (boost.x - 3, boost.y + 14);
      dl.end ();

      // Speedometer: face, quiet track with redline, ticks, unit label.
      hud_dial_face (dl, layout.speed);
      hud_ring (
        dl, layout.speed, 0.78f, 0.88f, -30, 210, 0.32f, 0.36f, 0.40f, 0.55f);
      hud_ring (
        dl, layout.speed, 0.78f, 0.88f, -30, 10, 0.92f, 0.12f, 0.08f, 0.8f);
      hud_ticks (dl,
                 layout.speed,
                 11,
                 210,
                 -30,
                 0.66f,
                 0.75f,
                 1.4f,
                 0.85f,
                 0.88f,
                 0.92f,
                 0.85f);
      dl.color (0.72f, 0.75f, 0.8f);
      const std::string unit = "KM/H";
      m_helv10->draw (dl,
                      layout.speed.center.x - m_helv10->measure (unit) * 0.5f,
                      layout.speed.center.y + 18,
                      unit);

      dl.pop ();

      // ---- status panel: backdrop, empty health bar, star badge ----
      dl.push ();
      dl.translate (layout.status.x, layout.status.y, 0);
      hud_panel (
        dl, 0, 0, layout.status.width, layout.status.height, 11.0f, 0.78f);

      const float bar_x = 10, bar_y = 38;
      const float bar_width = 142, bar_height = 8;
      dl.color (0.015f, 0.025f, 0.04f, 0.88f);
      dl.begin (Prim::Quads);
      dl.vertex (bar_x, bar_y);
      dl.vertex (bar_x + bar_width, bar_y);
      dl.vertex (bar_x + bar_width, bar_y + bar_height);
      dl.vertex (bar_x, bar_y + bar_height);
      dl.end ();

      const Point star (21, 20);
      dl.color (1.0f, 0.85f, 0.15f, 0.98f);
      dl.begin (Prim::TriangleFan);
      dl.vertex (star.x, star.y);
      for (int i = 0; i <= 10; ++i) {
        const float a = (-90.0f + i * 36.0f) * PI / 180.0f;
        const float r = (i % 2 == 0) ? 12.0f : 5.2f;
        dl.vertex (star.x + r * std::cos (a), star.y + r * std::sin (a));
      }
      dl.end ();
      dl.pop ();

      // ---- heading ribbon and ECU panels ----
      {
        const Rect c = compass_rect ((float)width_pts);
        hud_panel (dl, c.x, c.y, c.x + c.width, c.y + c.height, 7.0f, 0.62f);

        const Rect e = ecu_rect ((float)width_pts);
        hud_panel (dl, e.x, e.y, e.x + e.width, e.y + e.height, 7.0f, 0.62f);
        dl.color (0.48f, 0.58f, 0.68f, 0.9f);
        m_helv10->draw (dl, e.x + 7, e.y + 13, "ECU");
        dl.color (0.22f, 0.3f, 0.35f, 0.65f);
        dl.line (e.x + 7,
                 e.y + e.height - 6,
                 e.x + e.width - 7,
                 e.y + e.height - 6,
                 1.0f);
      }

      m_static_width = width_pts;
      m_static_height = height_pts;
    }

    void Hud::draw (render::DrawList& dl,
                    const HudState& st,
                    int width_pts,
                    int height_pts) {
      if (!m_helv10 || !m_display24)
        return; // load() not called

      const bool riding = !st.on_foot;
      const float kmh = riding ? st.speed_kmh : 0.0f;
      const float speed_fraction = clamp01 (kmh / 300.0f);
      const float charge = clamp01 (riding ? st.boost_ready01 : 1.0f);
      const HudLayout layout ((float)width_pts, (float)height_pts);
      const float frame_time = std::max (st.frame_time_s, 0.001f);
      const float instant_fps = std::min (240.0f, 1.0f / frame_time);
      m_fps = m_fps > 0.0f ? m_fps * 0.9f + instant_fps * 0.1f : instant_fps;
      m_fps_history[m_fps_cursor] = m_fps;
      m_fps_cursor = (m_fps_cursor + 1) % 48;
      m_fps_count = std::min (m_fps_count + 1, 48);

      hud_state_on (dl);

      // Splice in the baked static geometry (dial faces, ticks, panels,
      // fixed labels), re-tessellating only when its key changes.
      if (m_static_width != width_pts || m_static_height != height_pts)
        rebuild_static (width_pts, height_pts);
      dl.append (m_static);

      // All cluster geometry is local to one bottom-right anchor.
      dl.push ();
      dl.translate (layout.cluster_anchor.x, layout.cluster_anchor.y, 0);

      // ==================== BOOST GAUGE ====================
      hud_ring (dl,
                layout.boost,
                0.67f,
                0.84f,
                210,
                210.0f + 240.0f * charge,
                0.12f,
                0.62f,
                1.0f,
                0.9f);

      // ==================== SPEEDOMETER ====================
      // Live speed arc, green through amber to red.
      if (speed_fraction > 0.003f) {
        const int segs = (int)(44 * speed_fraction) + 1;
        dl.begin (Prim::TriangleStrip);
        for (int i = 0; i <= segs; ++i) {
          const float f = speed_fraction * i / segs;
          const float angle = 210.0f - 240.0f * f;
          const Point outer = layout.speed.at (0.88f, angle);
          const Point inner = layout.speed.at (0.78f, angle);
          dl.color (0.2f + 0.8f * f, 0.95f - 0.8f * f, 0.12f, 0.85f);
          dl.vertex (outer.x, outer.y);
          dl.vertex (inner.x, inner.y);
        }
        dl.end ();
      }

      // Large digital speed and a small odometer readout.
      {
        const std::string speed = std::to_string ((int)(kmh + 0.5f));
        dl.color (0.95f, 0.96f, 0.98f);
        m_display24->draw (dl,
                           layout.speed.center.x -
                             m_display24->measure (speed) * 0.5f,
                           layout.speed.center.y + 4,
                           speed);

        char buf[16];
        snprintf (buf, sizeof buf, "%07.1f", st.odometer_m / 1000.0);
        dl.color (0.76f, 0.8f, 0.76f);
        m_helv10->draw (dl,
                        layout.speed.center.x - m_helv10->measure (buf) * 0.5f,
                        layout.speed.center.y + 35,
                        buf);
      }

      if (st.on_foot || st.gliding) {
        char text[16];
        const std::string label =
          st.gliding
            ? (snprintf (text, sizeof text, "%+.1f", st.vertical_speed_mps),
               text)
            : "ON FOOT";
        if (st.gliding && st.vertical_speed_mps >= 0)
          dl.color (0.25f, 1.0f, 0.55f);
        else if (st.gliding)
          dl.color (1.0f, 0.55f, 0.2f);
        else
          dl.color (0.6f, 0.9f, 1.0f);
        m_helv10->draw (dl,
                        layout.boost.center.x -
                          m_helv10->measure (label) * 0.5f,
                        layout.boost.center.y + 4,
                        label);
      }

      dl.pop ();

      if (st.can_deploy_glider || st.can_drop_bike) {
        const std::string prompt =
          st.can_drop_bike ? "E  DROP MOTOCROSS" : "E  DEPLOY GLIDER";
        dl.color (0.65f, 0.9f, 1.0f, 0.94f);
        m_helv12->draw (dl,
                        width_pts * 0.5f - m_helv12->measure (prompt) * 0.5f,
                        height_pts - 42.0f,
                        prompt);
      }

      // The status contents use panel-local coordinates, so moving or
      // resizing the panel cannot separate its icon, bar, and hearts.
      // The panel, empty bar, and star badge come from the bake.
      dl.push ();
      dl.translate (layout.status.x, layout.status.y, 0);

      const float health = clamp01 (st.health01);
      const float bar_x = 10, bar_y = 38;
      const float bar_width = 142, bar_height = 8;
      dl.color (0.9f - 0.72f * health, 0.18f + 0.7f * health, 0.12f, 0.98f);
      dl.begin (Prim::Quads);
      dl.vertex (bar_x, bar_y);
      dl.vertex (bar_x + bar_width * health, bar_y);
      dl.vertex (bar_x + bar_width * health, bar_y + bar_height);
      dl.vertex (bar_x, bar_y + bar_height);
      dl.end ();

      // A one-point highlight keeps the narrow bar crisp on Retina.
      dl.color (0.9f, 1.0f, 0.9f, 0.28f);
      dl.line (bar_x, bar_y, bar_x + bar_width * health, bar_y, 1.0f);

      for (int i = 0; i < 10; ++i)
        hud_heart (dl, 11.0f + i * 15.6f, 62.0f, 6.2f, i < st.lives);

      dl.color (0.95f, 0.96f, 0.98f);
      m_helv12->draw (dl, 41, 25, "x " + std::to_string (st.stars));
      const std::string score = std::to_string (st.score) + " PTS";
      dl.color (0.95f, 0.78f, 0.24f, 0.95f);
      m_helv10->draw (
        dl, layout.status.width - 9 - m_helv10->measure (score), 24, score);
      dl.pop ();

      // Long-jump callout. Each new tenth briefly punches the live counter
      // larger; a landing banks the score, bounces once, then swishes away.
      if (st.airtime_s >= 3.0f) {
        const float tenth = st.airtime_s * 10.0f;
        const float tick = 1.0f - (tenth - std::floor (tenth));
        const float build = clamp01 ((st.airtime_s - 3.0f) / 5.0f);
        const float scale = 1.0f + 0.18f * build + 0.10f * tick * tick;
        char text[32];
        snprintf (text, sizeof text, "AIR  %.1f s", st.airtime_s);
        dl.push ();
        dl.translate (width_pts * 0.5f, height_pts * 0.28f, 0);
        dl.scale (scale, scale, 1);
        dl.color (1.0f, 0.82f + 0.12f * tick, 0.2f, 0.96f);
        m_display24->draw (dl, -m_display24->measure (text) * 0.5f, 0, text);
        dl.pop ();
      } else if (st.landed_age_s < 1.8f && st.landed_points > 0) {
        const float age = st.landed_age_s;
        const float fade = clamp01 ((1.8f - age) / 0.65f);
        const float bounce =
          1.0f + 0.22f * std::exp (-4.0f * age) * std::sin (18.0f * age);
        const float swish = age * age * 75.0f;
        char text[48];
        snprintf (text,
                  sizeof text,
                  "LANDED %.1f s  +%d",
                  st.landed_airtime_s,
                  st.landed_points);
        dl.push ();
        dl.translate (
          width_pts * 0.5f + swish, height_pts * 0.28f - age * 18.0f, 0);
        dl.scale (bounce, bounce, 1);
        dl.color (1.0f, 0.78f, 0.12f, fade);
        m_display24->draw (dl, -m_display24->measure (text) * 0.5f, 0, text);
        dl.pop ();
      }

      // Heading ribbon.  World +Z is north and +X is east, matching
      // the coordinate convention used by vehicle and walker headings.
      // The panel itself comes from the bake.
      {
        const Rect rect = compass_rect ((float)width_pts);
        const float width = rect.width;
        const float x = rect.x;
        const float y = rect.y;
        const float center = x + width * 0.5f;
        const float heading = st.heading_radians * 180.0f / PI;
        const float pixels_per_degree = (width - 20.0f) / 180.0f;

        static const char* labels[8] = { "N", "", "E", "", "S", "", "W", "" };
        for (int i = 0; i < 8; ++i) {
          const float bearing = i * 45.0f;
          const float delta = wrap_degrees (bearing - heading);
          if (std::fabs (delta) > 94.0f)
            continue;
          const float tx = center + delta * pixels_per_degree;
          const bool cardinal = labels[i][0] != '\0';
          dl.color (cardinal ? 0.86f : 0.48f,
                    cardinal ? 0.91f : 0.56f,
                    cardinal ? 0.96f : 0.63f,
                    cardinal ? 0.95f : 0.72f);
          dl.line (
            tx, y + 8, tx, y + (cardinal ? 18 : 14), cardinal ? 1.5f : 1.0f);
          if (cardinal)
            m_helv12->draw (
              dl, tx - m_helv12->measure (labels[i]) * 0.5f, y + 31, labels[i]);
        }

        dl.color (1.0f, 0.72f, 0.16f, 0.98f);
        dl.line (center, y + 5, center, y + 22, 2.0f);
        dl.begin (Prim::Triangles);
        dl.vertex (center, y + 25);
        dl.vertex (center - 4, y + 19);
        dl.vertex (center + 4, y + 19);
        dl.end ();

        int degrees = static_cast<int> (std::round (heading));
        degrees = (degrees % 360 + 360) % 360;
        char bearing[8];
        snprintf (bearing, sizeof bearing, "%03d", degrees);
        dl.color (0.92f, 0.78f, 0.38f, 0.92f);
        m_helv10->draw (
          dl, x + width - 7 - m_helv10->measure (bearing), y + 14, bearing);
      }

      // A compact ECU module: useful frame telemetry disguised as one
      // more instrument on the bike, with a short rolling pace trace.
      // The panel, label, and graph baseline come from the bake.
      {
        const Rect rect = ecu_rect ((float)width_pts);
        const float x = rect.x;
        const float y = rect.y;
        const float w = rect.width;
        const float h = rect.height;

        char fps[16];
        snprintf (fps, sizeof fps, "%3.0f FPS", m_fps);
        dl.color (0.72f, 0.9f, 0.92f, 0.94f);
        m_helv10->draw (dl, x + w - 7 - m_helv10->measure (fps), y + 13, fps);

        const float gx0 = x + 7, gx1 = x + w - 7;
        const float gy0 = y + 20, gy1 = y + h - 6;

        dl.color (0.2f, 0.78f, 0.76f, 0.84f);
        for (int i = 1; i < m_fps_count; ++i) {
          const int i0 = (m_fps_cursor - m_fps_count + i - 1 + 48) % 48;
          const int i1 = (m_fps_cursor - m_fps_count + i + 48) % 48;
          const float x0 = gx0 + (gx1 - gx0) * (i - 1) / 47.0f;
          const float x1 = gx0 + (gx1 - gx0) * i / 47.0f;
          const float f0 = clamp01 (m_fps_history[i0] / 144.0f);
          const float f1 = clamp01 (m_fps_history[i1] / 144.0f);
          dl.line (
            x0, gy1 - (gy1 - gy0) * f0, x1, gy1 - (gy1 - gy0) * f1, 1.25f);
        }
      }

      hud_state_off (dl);
    }

    // The end, as specified by the design department
    void
    Hud::draw_game_over (render::DrawList& dl, int width_pts, int height_pts) {
      if (!m_times24)
        return; // load() not called

      const int w = width_pts;
      const int h = height_pts;

      hud_state_on (dl);

      // the GL build cleared the frame to black
      dl.color (0, 0, 0, 1);
      dl.begin (Prim::Quads);
      dl.vertex (0, 0);
      dl.vertex ((float)w, 0);
      dl.vertex ((float)w, (float)h);
      dl.vertex (0, (float)h);
      dl.end ();

      dl.color (0.75f, 0.08f, 0.08f);
      m_times24->draw (dl, (float)(w / 2 - 32), (float)(h / 2 - 30), "Sorry.");
      m_times24->draw (dl,
                       (float)(w / 2 - 118),
                       (float)(h / 2 + 10),
                       "You are in great pain.");

      dl.color (0.35f, 0.35f, 0.4f);
      m_helv12->draw (
        dl, (float)(w / 2 - 66), (float)(h / 2 + 64), "press R to ride again");

      hud_state_off (dl);
    }
  }
}
