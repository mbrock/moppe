#include <moppe/game/scan.hh>

#include <moppe/render/renderer.hh>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace moppe::game {
  namespace {
    // A find's mark stands this far over it, metres.
    constexpr float k_mark_rise = 1.6f;
    // The ring's width on the ground, metres.
    constexpr float k_ring_width = 2.5f;
    constexpr float k_ring_edge = 0.18f;
    // The labels' cyan, and the poisonous one's red.
    constexpr DisplayColor k_scan_ink { 0.62f, 0.95f, 1.0f };
    constexpr DisplayColor k_warn_ink { 1.0f, 0.42f, 0.32f };
    constexpr DisplayColor k_camp_ink { 1.0f, 0.86f, 0.52f };

    float scan_ease (float t) {
      t = std::clamp (t, 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }

    DisplayColor scan_ink_of (const ScanFind& find) {
      switch (find.kind) {
      case ScanFind::Kind::poisonous:
        return k_warn_ink;
      case ScanFind::Kind::hammock:
        return k_camp_ink;
      default:
        return k_scan_ink;
      }
    }

    Vec3 scan_mark_point (const ScanFind& find) {
      return find.at +
             Vec3 (
               0, find.kind == ScanFind::Kind::hammock ? 0.4f : k_mark_rise, 0);
    }

    // A closed polygon as Slug lines.
    render::OutlineContour
    scan_polygon (std::span<const render::OutlinePoint> at) {
      render::OutlineContour contour;
      for (std::size_t i = 0; i < at.size (); ++i)
        contour.push_back (
          render::outline_line (at[i], at[(i + 1) % at.size ()]));
      return contour;
    }

    render::OutlineContour scan_round (float radius, bool clockwise) {
      constexpr int sides = 40;
      std::vector<render::OutlinePoint> at;
      for (int i = 0; i < sides; ++i) {
        const float a = (clockwise ? -1.0f : 1.0f) * 2.0f *
                        std::numbers::pi_v<float> * i / sides;
        at.push_back ({ radius * std::cos (a), radius * std::sin (a) });
      }
      return scan_polygon (at);
    }
  }

  std::vector<ScanFind> scan_mushrooms (const MushroomPatch& patch,
                                        const Vec3& origin) {
    std::vector<std::uint32_t> near = patch.within (origin, scan_reach);
    std::ranges::sort (near, [&] (std::uint32_t a, std::uint32_t b) {
      const Vec3 da = patch.site (a).base - origin;
      const Vec3 db = patch.site (b).base - origin;
      return length2 (da) < length2 (db);
    });
    std::vector<ScanFind> finds;
    std::vector<Vec3> sums;
    for (const std::uint32_t index : near) {
      const MushroomSite& site = patch.site (index);
      const ScanFind::Kind kind = mushroom_edible (site.kind)
                                    ? ScanFind::Kind::mushrooms
                                    : ScanFind::Kind::poisonous;
      bool joined = false;
      for (std::size_t i = 0; i < finds.size (); ++i) {
        ScanFind& f = finds[i];
        const Vec3 first = sums[i] / static_cast<float> (f.count);
        if (f.kind == kind && f.mushroom == site.kind &&
            std::hypot (first[0] - site.base[0], first[2] - site.base[2]) <
              4.0f) {
          sums[i] = sums[i] + site.base;
          ++f.count;
          f.at = sums[i] / static_cast<float> (f.count);
          joined = true;
          break;
        }
      }
      if (joined)
        continue;
      ScanFind find;
      find.kind = kind;
      find.at = site.base;
      find.mushroom = site.kind;
      finds.push_back (find);
      sums.push_back (site.base);
    }
    return finds;
  }

  std::vector<ScanFind> scan_hammocks (std::span<const HammockSite> sites) {
    std::vector<ScanFind> finds;
    for (const HammockSite& site : sites) {
      ScanFind find;
      find.kind = ScanFind::Kind::hammock;
      find.at = hammock_middle (site);
      find.hammock = site;
      finds.push_back (find);
    }
    return finds;
  }

  void
  Scan::start (const Vec3& origin, double now, std::vector<ScanFind> finds) {
    m_origin = origin;
    m_start = now;
    m_finds = std::move (finds);
  }

  float Scan::radius (double now) const {
    return std::min (scan_reach,
                     static_cast<float> (now - m_start) * scan_speed);
  }

  bool Scan::showing (double now) const {
    return now - m_start < scan_reach / scan_speed + scan_lit + scan_fade;
  }

  float Scan::since_found (const ScanFind& find, double now) const {
    const float away =
      std::hypot (find.at[0] - m_origin[0], find.at[2] - m_origin[2]);
    return static_cast<float> (now - m_start) - away / scan_speed;
  }

  float Scan::shown (const ScanFind& find, double now) const {
    const float t = since_found (find, now);
    if (t < 0.0f)
      return 0.0f;
    return scan_ease (t / 0.25f) *
           (1.0f - scan_ease ((t - scan_lit) / scan_fade));
  }

  void draw_scan_glow (render::DrawList& dl,
                       const Scan& scan,
                       double now,
                       const Vec3& camera,
                       const GroundHeight& ground) {
    if (!scan.showing (now))
      return;
    render::DrawState glow;
    glow.blend = true;
    glow.additive = true;
    glow.depth_write = false;
    glow.cull = false;
    dl.state (glow);
    dl.lit (false);
    dl.fogged (false);
    dl.begin (render::Prim::Triangles);

    // The ring: a band lying on the ground, brightest at its front edge
    // and fading as it nears its reach.
    const float r = scan.radius (now);
    const float travel = r / scan_reach;
    const float strength = 1.0f - scan_ease ((travel - 0.75f) / 0.25f);
    if (r < scan_reach && strength > 0.0f) {
      const Vec3& o = scan.origin ();
      constexpr int segments = 240;
      const float inner = std::max (0.0f, r - k_ring_width);
      const auto on_ground = [&] (float radius, float a) {
        const float x = o[0] + radius * std::cos (a);
        const float z = o[2] + radius * std::sin (a);
        return Vec3 (x, ground (x, z) + 0.08f, z);
      };
      for (int i = 0; i < segments; ++i) {
        const float a0 = 2.0f * std::numbers::pi_v<float> * i / segments;
        const float a1 = 2.0f * std::numbers::pi_v<float> * (i + 1) / segments;
        const Vec3 p0 = on_ground (inner, a0), p1 = on_ground (inner, a1);
        const Vec3 q0 = on_ground (r, a0), q1 = on_ground (r, a1);
        // The wake, fading back toward the walker...
        dl.color (k_scan_ink, 0.0f);
        dl.vertex (p0);
        dl.color (k_scan_ink, 0.45f * strength);
        dl.vertex (q0);
        dl.vertex (q1);
        dl.color (k_scan_ink, 0.0f);
        dl.vertex (p0);
        dl.color (k_scan_ink, 0.45f * strength);
        dl.vertex (q1);
        dl.color (k_scan_ink, 0.0f);
        dl.vertex (p1);
        // ...and the crisp front.
        const Vec3 e0 = on_ground (r - k_ring_edge, a0);
        const Vec3 e1 = on_ground (r - k_ring_edge, a1);
        dl.color (k_scan_ink, strength);
        dl.vertex (e0);
        dl.vertex (q0);
        dl.vertex (q1);
        dl.vertex (e0);
        dl.vertex (q1);
        dl.vertex (e1);
      }
    }

    // A thin line from each find up to its mark, facing the camera, and
    // a pool of light on the ground under it.
    for (const ScanFind& find : scan.finds ()) {
      const float shown = scan.shown (find, now);
      if (shown <= 0.0f)
        continue;
      const DisplayColor ink = scan_ink_of (find);
      const Vec3 foot = find.at;
      const Vec3 top = scan_mark_point (find) - Vec3 (0, 0.12f, 0);
      Vec3 side = cross (top - foot, camera - foot);
      if (length2 (side) < 1e-8f)
        continue;
      side = normalized (side) * 0.012f;
      dl.color (ink, 0.0f);
      dl.vertex (foot - side);
      dl.vertex (foot + side);
      dl.color (ink, 0.8f * shown);
      dl.vertex (top + side);
      dl.color (ink, 0.0f);
      dl.vertex (foot - side);
      dl.color (ink, 0.8f * shown);
      dl.vertex (top + side);
      dl.vertex (top - side);
      if (find.kind == ScanFind::Kind::hammock) {
        // The span itself, strap to strap.
        const Vec3 a = find.hammock.strap[0], b = find.hammock.strap[1];
        Vec3 across = cross (b - a, camera - a);
        if (length2 (across) > 1e-8f) {
          across = normalized (across) * 0.015f;
          dl.color (ink, 0.7f * shown);
          dl.vertex (a - across);
          dl.vertex (b - across);
          dl.vertex (b + across);
          dl.vertex (a - across);
          dl.vertex (b + across);
          dl.vertex (a + across);
        }
      }
    }
    dl.end ();
  }

  ScanMarks make_scan_marks (render::Renderer& renderer) {
    render::SlugAtlasBuilder builder;
    ScanMarks marks;
    {
      const render::OutlinePoint outer[] = {
        { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 }
      };
      const render::OutlinePoint inner[] = {
        { 0, 0.62f }, { -0.62f, 0 }, { 0, -0.62f }, { 0.62f, 0 }
      };
      const render::OutlinePoint core[] = {
        { 0, 0.32f }, { 0.32f, 0 }, { 0, -0.32f }, { -0.32f, 0 }
      };
      const render::OutlineContour contours[] = { scan_polygon (outer),
                                                  scan_polygon (inner),
                                                  scan_polygon (core) };
      marks.diamond = builder.add (contours);
    }
    {
      const render::OutlineContour contours[] = { scan_round (1.0f, true),
                                                  scan_round (0.74f, false) };
      marks.ring = builder.add (contours);
    }
    marks.set = renderer.create_glyph_set (builder.data ());
    return marks;
  }

  void draw_scan_labels (render::TextList& text,
                         const render::Font& font,
                         const ScanMarks& marks,
                         const Scan& scan,
                         double now,
                         const Vec3& walker,
                         const ScanView& view) {
    if (!marks.set || !scan.showing (now))
      return;
    // The finds in sight, nearest the walker first, with where each
    // falls on screen in points.
    struct Seen {
      const ScanFind* find;
      float away, x, y;
    };
    std::vector<Seen> seen;
    const float aspect = view.width_pts / std::max (view.height_pts, 1.0f);
    for (const ScanFind& find : scan.finds ()) {
      if (scan.shown (find, now) <= 0.0f)
        continue;
      const Vec3 rel = scan_mark_point (find) - view.eye;
      const float depth = dot (rel, view.forward);
      if (depth < 0.3f)
        continue;
      const float nx =
        dot (rel, view.right) / (depth * view.tan_half_height * aspect);
      const float ny = dot (rel, view.up) / (depth * view.tan_half_height);
      if (std::fabs (nx) > 1.2f || std::fabs (ny) > 1.2f)
        continue;
      seen.push_back (
        { &find,
          std::hypot (find.at[0] - walker[0], find.at[2] - walker[2]),
          (0.5f + 0.5f * nx) * view.width_pts,
          (0.5f - 0.5f * ny) * view.height_pts });
    }
    std::ranges::sort (seen, {}, &Seen::away);

    render::TextStyle title;
    title.size = 21.0f;
    render::TextStyle small;
    small.size = 14.0f;
    small.tracking = 0.04f;
    struct Box {
      float x0, y0, x1, y1;
    };
    std::vector<Box> named;
    constexpr int most_named = 7;

    for (const Seen& s : seen) {
      const ScanFind& find = *s.find;
      const float shown = scan.shown (find, now);
      const DisplayColor ink = scan_ink_of (find);
      const float since = scan.since_found (find, now);

      std::string name;
      std::string detail;
      char buffer[64];
      std::snprintf (buffer, sizeof buffer, "%.0f m", s.away);
      const std::string distance = buffer;
      switch (find.kind) {
      case ScanFind::Kind::mushrooms:
      case ScanFind::Kind::poisonous:
        if (find.count > 1) {
          name =
            std::to_string (find.count) + " " + mushroom_plural (find.mushroom);
        } else {
          name = mushroom_name (find.mushroom);
          name[0] = static_cast<char> (std::toupper (name[0]));
        }
        detail = find.kind == ScanFind::Kind::poisonous
                   ? "POISONOUS  \xC2\xB7  " + distance
                   : distance;
        break;
      case ScanFind::Kind::hammock: {
        const float span =
          std::hypot (find.hammock.strap[1][0] - find.hammock.strap[0][0],
                      find.hammock.strap[1][2] - find.hammock.strap[0][2]);
        std::snprintf (buffer, sizeof buffer, "%.1f", span);
        std::string metres = buffer;
        std::replace (metres.begin (), metres.end (), '.', ',');
        name = "Hammock";
        detail = metres + " m apart  \xC2\xB7  " + distance;
        break;
      }
      }

      // Named if it is among the nearest and its label covers none
      // already named.
      const float width = 24.0f + std::max (font.measure (name, title),
                                            font.measure (detail, small));
      const Box box { s.x - 14.0f, s.y - 22.0f, s.x + width, s.y + 26.0f };
      bool clear = static_cast<int> (named.size ()) < most_named;
      for (const Box& b : named)
        if (clear && box.x0 < b.x1 && b.x0 < box.x1 && box.y0 < b.y1 &&
            b.y0 < box.y1)
          clear = false;
      if (clear)
        named.push_back (box);

      // The mark lands from a little larger, as if it had just been
      // struck; a mark alone is smaller and dimmer.
      const float pop = 1.0f + 0.6f * (1.0f - scan_ease (since / 0.35f));
      const std::size_t first = text.quads ().size ();
      const render::SlugGlyph& shape =
        find.kind == ScanFind::Kind::hammock ? marks.ring : marks.diamond;
      const float size = (clear ? 11.0f : 7.0f) * pop;
      for (const bool shade : { true, false }) {
        render::GlyphQuad quad {};
        quad.origin[3] = shade ? 5.0f : 1.0f;
        quad.axis_x[0] = size;
        quad.axis_y[1] = -size;
        quad.bounds[0] = shape.min_x;
        quad.bounds[1] = shape.min_y;
        quad.bounds[2] = shape.max_x;
        quad.bounds[3] = shape.max_y;
        quad.color[0] = shade ? 0.0f : ink.red;
        quad.color[1] = shade ? 0.0f : ink.green;
        quad.color[2] = shade ? 0.0f : ink.blue;
        quad.color[3] = (shade ? 0.4f : 1.0f) * (clear ? 1.0f : 0.7f) * shown;
        quad.glyph[0] = shape.band_offset;
        quad.glyph[1] = shape.horizontal_bands;
        quad.glyph[2] = shape.vertical_bands;
        text.add (marks.set, quad);
      }

      if (clear) {
        // Its name over its distance, to the right, the lettering typing
        // itself in over a moment.
        const std::size_t typed =
          static_cast<std::size_t> (std::max (0.0f, since - 0.1f) * 40.0f);
        const auto typing = [&] (const std::string& line, std::size_t start) {
          if (typed <= start)
            return std::string ();
          // Whole UTF-8 characters only.
          std::size_t n = std::min (line.size (), typed - start);
          while (n < line.size () && (line[n] & 0xC0) == 0x80)
            ++n;
          return line.substr (0, n);
        };
        // A soft dark shadow under each line, so it reads over the sky.
        for (const bool shade : { true, false }) {
          render::TextStyle t = title, d = small;
          t.red = d.red = shade ? 0.0f : ink.red;
          t.green = d.green = shade ? 0.0f : ink.green;
          t.blue = d.blue = shade ? 0.0f : ink.blue;
          t.alpha = (shade ? 0.5f : 1.0f) * shown;
          d.alpha = (shade ? 0.5f : 0.85f) * shown;
          if (shade)
            t.softness = d.softness = 5.0f;
          font.draw (text, 20.0f, 3.0f, typing (name, 0), t);
          font.draw (text, 20.0f, 21.0f, typing (detail, name.size ()), d);
        }
      }
      text.stand (scan_mark_point (find), view.height_pts, first);
    }
  }
}
