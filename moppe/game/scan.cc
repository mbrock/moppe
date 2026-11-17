#include <moppe/game/scan.hh>

#include <moppe/render/renderer.hh>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <numbers>

namespace moppe::game {
  namespace {
    // A find's mark stands this far over it, metres.
    constexpr meters_t k_mark_rise = 1.6f * u::m;
    // The ring's width on the ground, and its crisp front's.
    constexpr meters_t k_ring_width = 2.5f * u::m;
    constexpr meters_t k_ring_edge = 0.18f * u::m;
    // The labels' cyan, and the poisonous one's red.
    constexpr DisplayColor k_scan_ink { 0.62f, 0.95f, 1.0f };
    constexpr DisplayColor k_warn_ink { 1.0f, 0.42f, 0.32f };
    constexpr DisplayColor k_camp_ink { 1.0f, 0.86f, 0.52f };
    const direction_t k_up = unit_direction (Vec3 (0, 1, 0));

    // What moves a place by an offset (docs/typed-quantities.md).
    auto scan_shift (const displacement_t& d) {
      return quantity_cast<isq::position_vector> (d);
    }

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

    position_t scan_mark_point (const ScanFind& find) {
      const meters_t rise =
        find.kind == ScanFind::Kind::hammock ? 0.4f * u::m : k_mark_rise;
      return find.at + scan_shift (along (k_up, rise));
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
                                        const position_t& from) {
    std::vector<std::uint32_t> near = patch.within (from, scan_reach);
    std::ranges::sort (near, [&] (std::uint32_t a, std::uint32_t b) {
      const displacement_t da = patch.base_near (a, from) - from;
      const displacement_t db = patch.base_near (b, from) - from;
      return dot (da, da) < dot (db, db);
    });
    std::vector<ScanFind> finds;
    std::vector<Vec3> sums;
    for (const std::uint32_t index : near) {
      const MushroomSite& site = patch.site (index);
      const position_t base = patch.base_near (index, from);
      const ScanFind::Kind kind = mushroom_edible (site.kind)
                                    ? ScanFind::Kind::mushrooms
                                    : ScanFind::Kind::poisonous;
      bool joined = false;
      for (std::size_t i = 0; i < finds.size (); ++i) {
        ScanFind& f = finds[i];
        const plan_displacement_t apart =
          plan_position (plan (sums[i] / static_cast<float> (f.count))) -
          plan (base);
        if (f.kind == kind && f.mushroom == site.kind &&
            libm::hypot (x_of (apart), z_of (apart)) < 4.0f * u::m) {
          sums[i] = sums[i] + position_value (base);
          ++f.count;
          f.at = position (sums[i] / static_cast<float> (f.count));
          joined = true;
          break;
        }
      }
      if (joined)
        continue;
      ScanFind find;
      find.kind = kind;
      find.at = base;
      find.mushroom = site.kind;
      finds.push_back (find);
      sums.push_back (position_value (base));
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

  void Scan::start (const position_t& origin,
                    play_time_t now,
                    std::vector<ScanFind> finds) {
    m_origin = origin;
    m_start = now;
    m_finds = std::move (finds);
  }

  meters_t Scan::radius (play_time_t now) const {
    return std::min (scan_reach,
                     meters_t (value_cast<float> (now - m_start) * scan_speed));
  }

  bool Scan::showing (play_time_t now) const {
    return now - m_start < scan_reach / scan_speed + scan_lit + scan_fade;
  }

  seconds_t Scan::since_found (const ScanFind& find, play_time_t now) const {
    const plan_displacement_t out = plan (find.at) - plan (m_origin);
    const meters_t away = libm::hypot (x_of (out), z_of (out));
    return value_cast<float> (now - m_start) - away / scan_speed;
  }

  proportion_t Scan::shown (const ScanFind& find, play_time_t now) const {
    const seconds_t t = since_found (find, now);
    if (t < 0.0f * u::s)
      return 0.0f * proportion[one];
    return scan_ease ((t / (0.25f * u::s)).numerical_value_in (one)) *
           (1.0f -
            scan_ease (((t - scan_lit) / scan_fade).numerical_value_in (one))) *
           proportion[one];
  }

  void draw_scan_glow (render::DrawList& dl,
                       const Scan& scan,
                       play_time_t now,
                       const position_t& eye,
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
    const meters_t r = scan.radius (now);
    const proportion_t travel = r / scan_reach * proportion[one];
    const proportion_t strength =
      (1.0f - scan_ease ((travel.numerical_value_in (one) - 0.75f) / 0.25f)) *
      proportion[one];
    if (r < scan_reach && strength > 0.0f * proportion[one]) {
      const plan_position_t o = plan (scan.origin ());
      constexpr int segments = 240;
      const meters_t inner = std::max (0.0f * u::m, r - k_ring_width);
      const auto on_ground = [&] (meters_t radius, radians_t a) {
        const plan_position_t at =
          o +
          plan_displacement (radius * libm::cos (a), radius * libm::sin (a));
        const Vec2& xz = plan_value (at);
        return at_height (at, (ground (xz.x (), xz.z ()) + 0.08f) * u::m);
      };
      constexpr radians_t turn = 2.0f * std::numbers::pi_v<float> * u::rad;
      for (int i = 0; i < segments; ++i) {
        const radians_t a0 = turn * i / segments;
        const radians_t a1 = turn * (i + 1) / segments;
        const position_t p0 = on_ground (inner, a0), p1 = on_ground (inner, a1);
        const position_t q0 = on_ground (r, a0), q1 = on_ground (r, a1);
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
        const position_t e0 = on_ground (r - k_ring_edge, a0);
        const position_t e1 = on_ground (r - k_ring_edge, a1);
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
      const proportion_t shown = scan.shown (find, now);
      if (shown <= 0.0f * proportion[one])
        continue;
      const DisplayColor ink = scan_ink_of (find);
      const position_t& foot = find.at;
      const position_t top =
        scan_mark_point (find) - scan_shift (along (k_up, 0.12f * u::m));
      const auto square = cross (top - foot, eye - foot);
      if (length2 (square.numerical_value_in (u::m2)) < 1e-8f)
        continue;
      const auto side = scan_shift (
        along (unit_direction (normalized (square.numerical_value_in (u::m2))),
               0.012f * u::m));
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
        const position_t& a = find.hammock.strap[0];
        const position_t& b = find.hammock.strap[1];
        const auto span = cross (b - a, eye - a);
        if (length2 (span.numerical_value_in (u::m2)) > 1e-8f) {
          const auto aside = scan_shift (along (
            unit_direction (normalized (span.numerical_value_in (u::m2))),
            0.015f * u::m));
          dl.color (ink, 0.7f * shown);
          dl.vertex (a - aside);
          dl.vertex (b - aside);
          dl.vertex (b + aside);
          dl.vertex (a - aside);
          dl.vertex (b + aside);
          dl.vertex (a + aside);
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
                         play_time_t now,
                         const position_t& walker_at,
                         const ScanView& view) {
    if (!marks.set || !scan.showing (now))
      return;
    // The finds in sight, nearest the walker first, with where each
    // falls on screen in points.
    struct Seen {
      const ScanFind* find;
      meters_t away;
      // Where it falls on screen, in points.
      float x, y;
    };
    std::vector<Seen> seen;
    const float aspect = view.width_pts / std::max (view.height_pts, 1.0f);
    for (const ScanFind& find : scan.finds ()) {
      if (scan.shown (find, now) <= 0.0f * proportion[one])
        continue;
      const displacement_t rel = scan_mark_point (find) - view.eye;
      const meters_t depth =
        quantity_cast<isq::length> (dot (rel, view.forward));
      if (depth < 0.3f * u::m)
        continue;
      const float nx =
        (dot (rel, view.right) / (depth * view.tan_half_height * aspect))
          .numerical_value_in (one);
      const float ny = (dot (rel, view.up) / (depth * view.tan_half_height))
                         .numerical_value_in (one);
      if (std::fabs (nx) > 1.2f || std::fabs (ny) > 1.2f)
        continue;
      const plan_displacement_t away = plan (find.at) - plan (walker_at);
      seen.push_back ({ &find,
                        libm::hypot (x_of (away), z_of (away)),
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
      const proportion_t shown = scan.shown (find, now);
      const DisplayColor ink = scan_ink_of (find);
      const seconds_t since = scan.since_found (find, now);

      std::string name;
      std::string detail;
      char buffer[64];
      std::snprintf (buffer, sizeof buffer, "%.0f m", meters_value (s.away));
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
        const plan_displacement_t apart =
          plan (find.hammock.strap[1]) - plan (find.hammock.strap[0]);
        const meters_t span = libm::hypot (x_of (apart), z_of (apart));
        std::snprintf (buffer, sizeof buffer, "%.1f", meters_value (span));
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
      const scale_factor_t pop =
        (1.0f +
         0.6f * (1.0f - scan_ease (
                          (since / (0.35f * u::s)).numerical_value_in (one)))) *
        scale_factor[one];
      const std::size_t first = text.quads ().size ();
      const render::SlugGlyph& shape =
        find.kind == ScanFind::Kind::hammock ? marks.ring : marks.diamond;
      const float size = (clear ? 11.0f : 7.0f) * pop.numerical_value_in (one);
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
        quad.color[3] = (shade ? 0.4f : 1.0f) * (clear ? 1.0f : 0.7f) *
                        shown.numerical_value_in (one);
        quad.glyph[0] = shape.band_offset;
        quad.glyph[1] = shape.horizontal_bands;
        quad.glyph[2] = shape.vertical_bands;
        text.add (marks.set, quad);
      }

      if (clear) {
        // Its name over its distance, to the right, the lettering typing
        // itself in over a moment.
        const std::size_t typed = static_cast<std::size_t> (
          seconds_value (std::max (0.0f * u::s, since - 0.1f * u::s)) * 40.0f);
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
          t.alpha = (shade ? 0.5f : 1.0f) * shown.numerical_value_in (one);
          d.alpha = (shade ? 0.5f : 0.85f) * shown.numerical_value_in (one);
          if (shade)
            t.softness = d.softness = 5.0f;
          font.draw (text, 20.0f, 3.0f, typing (name, 0), t);
          font.draw (text, 20.0f, 21.0f, typing (detail, name.size ()), d);
        }
      }
      text.stand (
        position_value (scan_mark_point (find)), view.height_pts, first);
    }
  }

  void Scanner::start (const position_t& at,
                       play_time_t now,
                       const MushroomPatch& mushrooms,
                       const mov::TrunkField& trunks,
                       const GroundHeight& ground) {
    std::vector<ScanFind> finds = scan_mushrooms (mushrooms, at);
    const std::vector<mov::Trunk> near =
      trunks.gather (at, scan_reach + 6.0f * u::m);
    const std::vector<ScanFind> trees = scan_hammocks (hammock_sites_near (
      near, at, scan_reach, [&ground] (const plan_position_t& spot) {
        return terrain::surface_elevation_point (
          ground (plan_value (spot).x (), plan_value (spot).z ()) * u::m);
      }));
    finds.insert (finds.end (), trees.begin (), trees.end ());
    const Vec3& feet = position_value (at);
    std::cerr << "moppe: scan from " << feet[0] << " " << feet[1] << " "
              << feet[2] << " found " << finds.size () << "\n";
    m_scan.start (at, now, std::move (finds));
  }

  void Scanner::draw (render::Renderer& r,
                      const render::Font* font,
                      play_time_t now,
                      const ScanView& view,
                      const position_t& walker,
                      const GroundHeight& ground) {
    if (!m_scan.showing (now) || !font)
      return;
    if (!m_marks.set)
      m_marks = make_scan_marks (r);
    m_dl.clear ();
    draw_scan_glow (m_dl, m_scan, now, view.eye, ground);
    r.draw_list (m_dl, 0);
    m_text.clear ();
    draw_scan_labels (m_text, *font, m_marks, m_scan, now, walker, view);
    r.draw_world_labels (m_text);
  }
}
