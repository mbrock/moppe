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
    void hud_disc (DrawList& dl, float cx, float cy, float r,
		   float cr, float cg, float cb, float ca)
    {
      dl.color (cr, cg, cb, ca);
      dl.begin (Prim::TriangleFan);
      dl.vertex (cx, cy);
      for (int i = 0; i <= 40; ++i) {
	const float a = 2.0f * 3.14159f * i / 40;
	dl.vertex (cx + r * std::cos (a), cy + r * std::sin (a));
      }
      dl.end ();
    }

    // Annulus segment; dial angle convention: 0 deg = +x, angles
    // increase counterclockwise ON SCREEN (y is down, so -sin)
    void hud_ring (DrawList& dl, float cx, float cy,
		   float r0, float r1, float a0, float a1,
		   float cr, float cg, float cb, float ca)
    {
      dl.color (cr, cg, cb, ca);
      dl.begin (Prim::TriangleStrip);
      const int segs = 48;
      for (int i = 0; i <= segs; ++i) {
	const float a =
	  (a0 + (a1 - a0) * i / segs) * 3.14159f / 180.0f;
	dl.vertex (cx + r1 * std::cos (a), cy - r1 * std::sin (a));
	dl.vertex (cx + r0 * std::cos (a), cy - r0 * std::sin (a));
      }
      dl.end ();
    }

    // A tapered needle with a counterweight tail and a hub
    void hud_needle (DrawList& dl, float cx, float cy, float a_deg,
		     float len, float tail, float w,
		     float cr, float cg, float cb)
    {
      const float a = a_deg * 3.14159f / 180.0f;
      const float dx = std::cos (a), dy = -std::sin (a);
      const float px = -dy, py = dx;

      dl.color (cr, cg, cb, 0.96f);
      dl.begin (Prim::Triangles);
      dl.vertex (cx + px * w, cy + py * w);
      dl.vertex (cx - px * w, cy - py * w);
      dl.vertex (cx + dx * len, cy + dy * len);
      dl.vertex (cx + px * w * 1.5f, cy + py * w * 1.5f);
      dl.vertex (cx - px * w * 1.5f, cy - py * w * 1.5f);
      dl.vertex (cx - dx * tail, cy - dy * tail);
      dl.end ();

      hud_disc (dl, cx, cy, w * 2.6f, 0.16f, 0.17f, 0.19f, 1.0f);
      hud_disc (dl, cx, cy, w * 1.4f, 0.75f, 0.77f, 0.80f, 1.0f);
    }

    // Metallic bezel + dark face + glass highlight
    void hud_dial_face (DrawList& dl, float cx, float cy, float R)
    {
      hud_ring (dl, cx, cy, R * 0.94f, R * 1.08f, 0, 360,
		0.52f, 0.54f, 0.58f, 0.95f);
      hud_ring (dl, cx, cy, R * 1.02f, R * 1.08f, 0, 360,
		0.24f, 0.25f, 0.27f, 0.95f);
      hud_disc (dl, cx, cy, R * 0.96f, 0.06f, 0.07f, 0.09f, 0.93f);
      // glass reflection across the upper left
      hud_ring (dl, cx, cy, R * 0.45f, R * 0.92f, 115, 205,
		1.0f, 1.0f, 1.0f, 0.055f);
    }

    // A little heart: two lobes and a point
    void hud_heart (DrawList& dl, float x, float y, float s,
		    bool full)
    {
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

    void hud_lamp (DrawList& dl, float x, float y, float r, bool on,
		   float cr, float cg, float cb)
    {
      hud_disc (dl, x, y, r + 2.5f, 0.10f, 0.11f, 0.12f, 0.95f);
      if (on) {
	hud_disc (dl, x, y, r + 5.0f, cr, cg, cb, 0.25f); // glow
	hud_disc (dl, x, y, r, cr, cg, cb, 0.98f);
      }
      else
	hud_disc (dl, x, y, r, cr * 0.22f, cg * 0.22f, cb * 0.22f,
		  0.9f);
    }

    // The 2D overlay state: blend on, no depth, no cull (the y-down
    // ortho flips winding), unlit, unfogged.
    void hud_state_on (DrawList& dl)
    {
      render::DrawState s;
      s.blend = true;
      s.depth_test = false;
      s.depth_write = false;
      s.cull = false;
      dl.state (s);
      dl.lit (false);
      dl.fogged (false);
    }

    void hud_state_off (DrawList& dl)
    {
      dl.state (render::DrawState ());
      dl.lit (true);
      dl.fogged (true);
      dl.color (1, 1, 1, 1);
    }
  }

  // ---- the cluster itself -------------------------------------------

  void
  Hud::load (render::Renderer& renderer)
  {
    const float s = renderer.scale_factor ();
    m_helv10.reset
      (new render::FontAtlas (renderer, "Helvetica", 10.0f, s));
    m_helv12.reset
      (new render::FontAtlas (renderer, "Helvetica", 12.0f, s));
    m_times24.reset
      (new render::FontAtlas (renderer, "Times New Roman", 24.0f, s));
    // stand-in for GLUT's 8x13 fixed-width bitmap font
    m_mono.reset
      (new render::FontAtlas (renderer, "Menlo", 13.0f, s));
  }

  void
  Hud::draw (render::DrawList& dl, const HudState& st,
	     int width_pts, int height_pts)
  {
    if (!m_helv10)
      return;  // load() not called

    const float width = (float) width_pts;
    const float height = (float) height_pts;

    const bool riding = !st.on_foot;
    const float kmh = riding ? st.speed_kmh : 0.0f;
    const float frac = std::min (1.0f, kmh / 300.0f);
    const float charge = riding ? st.boost_ready01 : 1.0f;
    const float PI = 3.14159265f;

    // In helmet view the cluster sits centered on a solid backing,
    // like the real instruments on the handlebars; in third person
    // it tucks translucently into the corner
    const bool helmet_hud = st.helmet_view;
    const float X = helmet_hud ? width * 0.5f + 235.0f
			       : width - 14.0f;
    const float Y = height - (helmet_hud ? 4.0f : 10.0f);
    const float panel_alpha = helmet_hud ? 0.93f : 0.80f;

    // one slim horizontal strip along the bottom:
    // fuel dial, boost dial, lamp column, speedometer
    const float cx = X - 95.0f; // speedometer
    const float cy = Y - 78.0f;
    const float R = 66.0f;
    const float bx2 = X - 262.0f; // boost dial
    const float mx = X - 372.0f;  // fuel dial
    const float fy = Y - 72.0f;   // mini dial row
    const float mr = 40.0f;
    const float lampx = X - 180.0f;

    hud_state_on (dl);

    // -- dashboard panel behind everything
    {
      const float x0 = X - 460.0f, y0 = Y - 152.0f;
      const float x1 = X, y1 = Y;
      const float c = 20.0f;
      dl.color (0.10f, 0.11f, 0.13f, panel_alpha);
      dl.begin (Prim::Polygon);
      dl.vertex (x0 + c, y0);
      dl.vertex (x1 - c, y0);
      dl.vertex (x1, y0 + c);
      dl.vertex (x1, y1 - c);
      dl.vertex (x1 - c, y1);
      dl.vertex (x0 + c, y1);
      dl.vertex (x0, y1 - c);
      dl.vertex (x0, y0 + c);
      dl.end ();
      // rim light along the top edge
      dl.color (0.55f, 0.60f, 0.66f, 0.4f);
      dl.line (x0, y0 + c, x0 + c, y0, 2);
      dl.line (x0 + c, y0, x1 - c, y0, 2);
      dl.line (x1 - c, y0, x1, y0 + c, 2);
    }

    // ==================== SPEEDOMETER ====================
    hud_dial_face (dl, cx, cy, R);

    // redline zone 250-300 (speed s maps to 210 - 240*s/300)
    hud_ring (dl, cx, cy, R * 0.86f, R * 0.95f, -30, 10,
	      0.8f, 0.10f, 0.08f, 0.5f);

    // live speed arc, green through amber to red
    if (frac > 0.003f) {
      const int segs = (int) (44 * frac) + 1;
      dl.begin (Prim::TriangleStrip);
      for (int i = 0; i <= segs; ++i) {
	const float f = frac * i / segs;
	const float a = (210.0f - 240.0f * f) * PI / 180.0f;
	dl.color (0.2f + 0.8f * f, 0.95f - 0.8f * f, 0.12f,
		  0.85f);
	dl.vertex (cx + 0.95f * R * std::cos (a),
		   cy - 0.95f * R * std::sin (a));
	dl.vertex (cx + 0.86f * R * std::cos (a),
		   cy - 0.86f * R * std::sin (a));
      }
      dl.end ();
    }

    // graduations: minor every 15, major every 30
    for (int s = 0; s <= 300; s += 15) {
      const bool major = (s % 30 == 0);
      const float a =
	(210.0f - 240.0f * s / 300.0f) * PI / 180.0f;
      if (major)
	dl.color (0.92f, 0.93f, 0.95f, 0.95f);
      else
	dl.color (0.55f, 0.57f, 0.6f, 0.8f);
      const float r0 = major ? 0.72f : 0.77f;
      dl.line (cx + r0 * R * std::cos (a),
	       cy - r0 * R * std::sin (a),
	       cx + 0.84f * R * std::cos (a),
	       cy - 0.84f * R * std::sin (a), 2);
    }

    // the needle
    hud_needle (dl, cx, cy, 210.0f - 240.0f * frac,
		0.80f * R, 0.20f * R, 3.6f,
		0.95f, 0.22f, 0.12f);

    // odometer window
    {
      dl.color (0.02f, 0.02f, 0.03f, 0.95f);
      dl.begin (Prim::Quads);
      dl.vertex (cx - 32, cy + 22);
      dl.vertex (cx + 32, cy + 22);
      dl.vertex (cx + 32, cy + 38);
      dl.vertex (cx - 32, cy + 38);
      dl.end ();
    }

    // ==================== FUEL GAUGE ====================
    hud_dial_face (dl, mx, fy, mr);
    // low-fuel zone (left fifth of the 160..20 sweep)
    hud_ring (dl, mx, fy, mr * 0.62f, mr * 0.88f, 132, 160,
	      0.85f, 0.35f, 0.05f, 0.5f);
    for (int i = 0; i <= 4; ++i) {
      const float a = (160.0f - 35.0f * i) * PI / 180.0f;
      dl.color (0.9f, 0.9f, 0.95f, 0.9f);
      dl.line (mx + 0.66f * mr * std::cos (a),
	       fy - 0.66f * mr * std::sin (a),
	       mx + 0.86f * mr * std::cos (a),
	       fy - 0.86f * mr * std::sin (a), 2);
    }
    hud_needle (dl, mx, fy, 160.0f - 140.0f * (st.fuel / 100.0f),
		0.74f * mr, 0.2f * mr, 2.4f,
		0.92f, 0.92f, 0.95f);

    // ==================== BOOST GAUGE ====================
    hud_dial_face (dl, bx2, fy, mr);
    hud_ring (dl, bx2, fy, mr * 0.62f, mr * 0.88f, 20,
	      20.0f + 140.0f * charge,
	      0.25f, 0.65f, 1.0f, 0.45f);
    for (int i = 0; i <= 4; ++i) {
      const float a = (160.0f - 35.0f * i) * PI / 180.0f;
      dl.color (0.9f, 0.9f, 0.95f, 0.9f);
      dl.line (bx2 + 0.66f * mr * std::cos (a),
	       fy - 0.66f * mr * std::sin (a),
	       bx2 + 0.86f * mr * std::cos (a),
	       fy - 0.86f * mr * std::sin (a), 2);
    }
    hud_needle (dl, bx2, fy, 160.0f - 140.0f * charge,
		0.74f * mr, 0.2f * mr, 2.4f,
		0.35f, 0.75f, 1.0f);

    // ==================== WARNING LAMPS ====================
    const bool blink_slow =
      std::fmod (st.time * 1.6f, 1.0f) < 0.6f;
    const bool blink_fast =
      std::fmod (st.time * 3.5f, 1.0f) < 0.5f;

    // boost ready (green), low fuel (amber), damage (red)
    hud_lamp (dl, lampx, Y - 116.0f, 6.5f,
	      riding && charge >= 1.0f && blink_slow,
	      0.2f, 0.95f, 0.35f);
    hud_lamp (dl, lampx, Y - 78.0f, 6.5f,
	      st.fuel < 20.0f && blink_slow,
	      1.0f, 0.6f, 0.05f);
    hud_lamp (dl, lampx, Y - 40.0f, 6.5f,
	      st.health01 < 0.35f && blink_fast,
	      1.0f, 0.15f, 0.1f);

    // backing plate for the top-left corner readouts
    {
      const float x0 = 10, y0 = 8, x1 = 220, y1 = 114;
      const float c = 12.0f;
      dl.color (0.10f, 0.11f, 0.13f, 0.62f);
      dl.begin (Prim::Polygon);
      dl.vertex (x0 + c, y0);
      dl.vertex (x1 - c, y0);
      dl.vertex (x1, y0 + c);
      dl.vertex (x1, y1 - c);
      dl.vertex (x1 - c, y1);
      dl.vertex (x0 + c, y1);
      dl.vertex (x0, y1 - c);
      dl.vertex (x0, y0 + c);
      dl.end ();
    }

    // health bar under the star counter
    {
      const float hx = 24, hy = 62;
      const float hw = 180, hh = 14;
      const float f = std::max (0.0f, st.health01);

      dl.color (0.05f, 0.08f, 0.12f, 0.6f);
      dl.begin (Prim::Quads);
      dl.vertex (hx, hy);
      dl.vertex (hx + hw, hy);
      dl.vertex (hx + hw, hy + hh);
      dl.vertex (hx, hy + hh);
      dl.end ();

      dl.color (0.9f - 0.7f * f, 0.15f + 0.7f * f, 0.15f,
		0.95f);
      dl.begin (Prim::Quads);
      dl.vertex (hx, hy);
      dl.vertex (hx + hw * f, hy);
      dl.vertex (hx + hw * f, hy + hh);
      dl.vertex (hx, hy + hh);
      dl.end ();
    }

    // golden star icon, top left
    {
      const float sx = 36, sy = 36;
      dl.color (1.0f, 0.85f, 0.15f, 0.95f);
      dl.begin (Prim::TriangleFan);
      dl.vertex (sx, sy);
      for (int i = 0; i <= 10; ++i) {
	const float a = (-90.0f + i * 36.0f) * PI / 180.0f;
	const float r = (i % 2 == 0) ? 17.0f : 7.5f;
	dl.vertex (sx + r * std::cos (a), sy + r * std::sin (a));
      }
      dl.end ();
    }

    // ten lives, worn on the sleeve
    for (int i = 0; i < 10; ++i)
      hud_heart (dl, 26.0f + i * 19.0f, 96.0f, 8.5f, i < st.lives);

    // ---- printed text

    // speed numbers around the dial
    dl.color (0.88f, 0.9f, 0.93f);
    for (int s = 0; s <= 300; s += 60) {
      const float a = (210.0f - 240.0f * s / 300.0f) * PI / 180.0f;
      const std::string label = std::to_string (s);
      m_helv10->draw
	(dl,
	 (float) (int) (cx + 0.58f * R * std::cos (a)
			- 3.0f * label.size ()),
	 (float) (int) (cy - 0.58f * R * std::sin (a) + 3),
	 label);
    }

    dl.color (0.65f, 0.68f, 0.72f);
    m_helv10->draw (dl, (float) (int) (cx - 13),
		    (float) (int) (cy - 18), "km/h");
    m_helv10->draw (dl, (float) (int) (cx - 17),
		    (float) (int) (cy - 31), "MOPPE");

    // odometer digits
    {
      char buf[16];
      snprintf (buf, sizeof buf, "%07.1f", st.odometer_m / 1000.0);
      dl.color (0.85f, 0.9f, 0.85f);
      m_mono->draw (dl, (float) (int) (cx - 28),
		    (float) (int) (cy + 34), buf);
    }

    // gauge labels: E/F on the fuel dial, BOOST below its twin
    dl.color (0.8f, 0.3f, 0.2f);
    m_helv10->draw (dl, (float) (int) (mx - mr * 0.95f),
		    (float) (int) (fy - mr * 0.24f), "E");
    dl.color (0.8f, 0.85f, 0.9f);
    m_helv10->draw (dl, (float) (int) (mx + mr * 0.82f),
		    (float) (int) (fy - mr * 0.24f), "F");
    dl.color (0.65f, 0.68f, 0.72f);
    m_helv10->draw (dl, (float) (int) (mx - 12),
		    (float) (int) (fy + 24), "FUEL");
    m_helv10->draw (dl, (float) (int) (bx2 - 15),
		    (float) (int) (fy + 24), "BOOST");

    // star counter
    dl.color (1.0f, 0.85f, 0.2f);
    m_times24->draw (dl, 60, 44,
		     "x " + std::to_string (st.stars));

    if (st.on_foot) {
      dl.color (0.6f, 0.9f, 1.0f);
      m_helv12->draw (dl, (float) (int) (X - 450),
		      (float) (int) (Y - 138), "ON FOOT");
    }

    hud_state_off (dl);
  }

  // The end, as specified by the design department
  void
  Hud::draw_game_over (render::DrawList& dl,
		       int width_pts, int height_pts)
  {
    if (!m_times24)
      return;  // load() not called

    const int w = width_pts;
    const int h = height_pts;

    hud_state_on (dl);

    // the GL build cleared the frame to black
    dl.color (0, 0, 0, 1);
    dl.begin (Prim::Quads);
    dl.vertex (0, 0);
    dl.vertex ((float) w, 0);
    dl.vertex ((float) w, (float) h);
    dl.vertex (0, (float) h);
    dl.end ();

    dl.color (0.75f, 0.08f, 0.08f);
    m_times24->draw (dl, (float) (w / 2 - 32),
		     (float) (h / 2 - 30), "Sorry.");
    m_times24->draw (dl, (float) (w / 2 - 118),
		     (float) (h / 2 + 10),
		     "You are in great pain.");

    dl.color (0.35f, 0.35f, 0.4f);
    m_helv12->draw (dl, (float) (w / 2 - 66),
		    (float) (h / 2 + 64),
		    "press R to ride again");

    hud_state_off (dl);
  }
}
}
