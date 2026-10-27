#include <moppe/game/map_sheet.hh>

#include <moppe/render/renderer.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace moppe::game {
  namespace {
    using render::TextAlign;
    using render::TextStyle;

    constexpr DisplayColor map_paper { 0.955f, 0.940f, 0.895f };
    constexpr DisplayColor map_ink { 0.105f, 0.100f, 0.095f };
    constexpr DisplayColor height_ink { 0.130f, 0.085f, 0.050f };

    float sheet_delta (float delta, float period) {
      if (delta > 0.5f * period)
        delta -= period;
      if (delta < -0.5f * period)
        delta += period;
      return delta;
    }

    void box (render::DrawList& dl, float x0, float y0, float x1, float y1) {
      dl.begin (render::Prim::Quads);
      dl.vertex (x0, y0);
      dl.vertex (x1, y0);
      dl.vertex (x1, y1);
      dl.vertex (x0, y1);
      dl.end ();
    }

    void disc (render::DrawList& dl, float x, float y, float radius) {
      const int sides = 16;
      dl.begin (render::Prim::Triangles);
      for (int side = 0; side < sides; ++side) {
        const float a =
          2.0f * std::numbers::pi_v<float> * static_cast<float> (side) / sides;
        const float b = 2.0f * std::numbers::pi_v<float> *
                        static_cast<float> (side + 1) / sides;
        dl.vertex (x, y);
        dl.vertex (x + radius * std::cos (a), y + radius * std::sin (a));
        dl.vertex (x + radius * std::cos (b), y + radius * std::sin (b));
      }
      dl.end ();
    }

    // An arrowhead at (x, y) pointing along (dx, dy), a unit vector on
    // the screen.
    void arrow (
      render::DrawList& dl, float x, float y, float dx, float dy, float size) {
      const float sx = -dy, sy = dx;
      dl.begin (render::Prim::Triangles);
      dl.vertex (x + dx * size, y + dy * size);
      dl.vertex (x - dx * 0.62f * size + sx * 0.62f * size,
                 y - dy * 0.62f * size + sy * 0.62f * size);
      dl.vertex (x - dx * 0.25f * size, y - dy * 0.25f * size);
      dl.vertex (x + dx * size, y + dy * size);
      dl.vertex (x - dx * 0.25f * size, y - dy * 0.25f * size);
      dl.vertex (x - dx * 0.62f * size - sx * 0.62f * size,
                 y - dy * 0.62f * size - sy * 0.62f * size);
      dl.end ();
    }

    TextStyle style (float size, DisplayColor colour, float tracking = 0.0f) {
      TextStyle s;
      s.size = size;
      s.red = colour.red;
      s.green = colour.green;
      s.blue = colour.blue;
      s.tracking = tracking;
      return s;
    }

    // Words over the map, on a glow of map_paper so the line work under them
    // does not run through the letters.
    void label (render::TextList& text,
                const render::Font& font,
                float x,
                float y,
                const std::string& words,
                const TextStyle& set,
                TextAlign align = TextAlign::Left) {
      TextStyle glow = set;
      glow.red = map_paper.red;
      glow.green = map_paper.green;
      glow.blue = map_paper.blue;
      glow.alpha = 0.85f;
      glow.softness = 5.0f;
      font.draw (text, x, y, words, glow, align);
      font.draw (text, x, y, words, set, align);
    }

    using Box = std::array<float, 4>; // left, top, right, bottom
    using Mark = MapSheet::Mark;

    // Which marks matter most: their symbols are drawn over the others',
    // and their names set first.
    int rank (Mark::Kind kind) {
      switch (kind) {
      case Mark::Kind::Walker:
        return 0;
      case Mark::Kind::Companion:
        return 1;
      case Mark::Kind::Landing:
      case Mark::Kind::Outlet:
      case Mark::Kind::Sanctuary:
        return 2;
      case Mark::Kind::Trailhead:
        return 3;
      case Mark::Kind::Camp:
        return 4;
      default:
        return 5;
      }
    }

    // A mark's symbol at (x, y), `s` times its size on the sheet.
    void symbol (
      render::DrawList& dl, const Mark& mark, float x, float y, float s) {
      Vec3 way (mark.heading[0], 0.0f, mark.heading[2]);
      way = length2 (way) > 1e-6f ? normalized (way) : Vec3 (0, 0, -1);
      const float dx = way[0], dy = way[2];
      switch (mark.kind) {
      case Mark::Kind::Walker:
      case Mark::Kind::Companion: {
        const float size = (mark.kind == Mark::Kind::Walker ? 11.0f : 9.0f) * s;
        dl.color (1.0f, 1.0f, 1.0f, 1.0f);
        arrow (dl,
               x - dx * 0.8f * s,
               y - dy * 0.8f * s,
               dx,
               dy,
               size + 4.0f * s);
        dl.color (mark.colour, 1.0f);
        arrow (dl, x, y, dx, dy, size);
        break;
      }
      case Mark::Kind::Bike:
        dl.color (1.0f, 1.0f, 1.0f, 1.0f);
        disc (dl, x, y, 5.2f * s);
        dl.color (mark.colour, 1.0f);
        disc (dl, x, y, 3.6f * s);
        break;
      case Mark::Kind::Jeep:
        dl.color (1.0f, 1.0f, 1.0f, 1.0f);
        box (dl, x - 5.2f * s, y - 5.2f * s, x + 5.2f * s, y + 5.2f * s);
        dl.color (mark.colour, 1.0f);
        box (dl, x - 3.6f * s, y - 3.6f * s, x + 3.6f * s, y + 3.6f * s);
        break;
      case Mark::Kind::Camp:
        dl.color (1.0f, 1.0f, 1.0f, 1.0f);
        arrow (dl, x, y + 1.0f * s, 0.0f, -1.0f, 9.5f * s);
        dl.color (mark.colour, 1.0f);
        arrow (dl, x, y + 0.4f * s, 0.0f, -1.0f, 6.5f * s);
        break;
      case Mark::Kind::Trailhead:
        dl.color (mark.colour, 1.0f);
        disc (dl, x, y, 5.0f * s);
        dl.color (1.0f, 1.0f, 1.0f, 1.0f);
        disc (dl, x, y, 3.2f * s);
        break;
      case Mark::Kind::Landing:
        // An anchor's ring and stock, as charts mark a landing place.
        dl.color (1.0f, 1.0f, 1.0f, 1.0f);
        disc (dl, x, y, 6.2f * s);
        dl.color (mark.colour, 1.0f);
        disc (dl, x, y, 4.8f * s);
        dl.color (1.0f, 1.0f, 1.0f, 1.0f);
        dl.line (x, y - 3.2f * s, x, y + 3.0f * s, 1.1f * s);
        dl.line (
          x - 2.4f * s, y - 1.6f * s, x + 2.4f * s, y - 1.6f * s, 1.1f * s);
        dl.line (x - 2.6f * s, y + 1.6f * s, x, y + 3.0f * s, 1.0f * s);
        dl.line (x + 2.6f * s, y + 1.6f * s, x, y + 3.0f * s, 1.0f * s);
        break;
      case Mark::Kind::Outlet:
        // A mill wheel: a ring with spokes.
        dl.color (1.0f, 1.0f, 1.0f, 1.0f);
        disc (dl, x, y, 5.6f * s);
        dl.color (mark.colour, 1.0f);
        disc (dl, x, y, 4.3f * s);
        dl.color (1.0f, 1.0f, 1.0f, 1.0f);
        disc (dl, x, y, 1.3f * s);
        dl.line (x - 3.4f * s, y, x + 3.4f * s, y, 0.9f * s);
        dl.line (x, y - 3.4f * s, x, y + 3.4f * s, 0.9f * s);
        break;
      case Mark::Kind::Sanctuary:
        // The sheet already carries its triangle; a dot of fire in it.
        // In a list, where there is no triangle, the triangle too.
        if (s < 1.0f) {
          dl.color (mark.colour, 1.0f);
          arrow (dl, x, y + 2.0f, 0.0f, -1.0f, 8.0f * s);
        }
        dl.color (mark.colour, 1.0f);
        disc (dl, x, y, 2.4f * s);
        break;
      }
    }

    std::string distance_words (float metres) {
      if (metres >= 1000.0f) {
        const float km = metres / 1000.0f;
        return (km == std::floor (km)
                  ? std::to_string (static_cast<int> (km))
                  : std::to_string (static_cast<int> (km)) + "." +
                      std::to_string (
                        static_cast<int> (std::lround (km * 10.0f)) % 10)) +
               " km";
      }
      return std::to_string (static_cast<int> (metres)) + " m";
    }
  }

  void MapSheet::set (render::Renderer& renderer, ReliefMap sheet) {
    render::TextureDesc desc;
    desc.width = sheet.size;
    desc.height = sheet.size;
    desc.format = render::TextureFormat::RGBA8;
    desc.filter = render::TextureFilter::Mipmap;
    desc.wrap = render::TextureWrap::Repeat;
    m_texture = renderer.create_texture (desc, sheet.rgba.data ());
    // The picture lives on the GPU from here on.
    sheet.rgba.clear ();
    sheet.rgba.shrink_to_fit ();
    m_sheet = std::move (sheet);
  }

  void MapSheet::forget () {
    m_texture.reset ();
    m_sheet = {};
  }

  void MapSheet::draw (render::DrawList& dl,
                       render::TextList& text,
                       const render::Font* font,
                       std::span<const Mark> marks,
                       const std::string& title,
                       int width_pts,
                       int height_pts) const {
    if (!open () || !m_texture || m_sheet.period_x_m <= 0.0f)
      return;
    const float width = static_cast<float> (width_pts);
    const float height = static_cast<float> (height_pts);
    const float margin = 12.0f;
    const float pad = 26.0f;
    const float footer = 56.0f;
    const float side = std::floor (std::min (
      width - 2.0f * (margin + pad), height - 2.0f * margin - pad - footer));
    if (side < 120.0f)
      return;
    const float paper_x = std::floor (0.5f * (width - side) - pad);
    const float paper_y = std::floor (0.5f * (height - (side + pad + footer)));
    const float map_x = paper_x + pad;
    const float map_y = paper_y + pad;

    // The stretch of country in view. It is square on the ground, as the
    // world is.
    const float period_x = m_sheet.period_x_m;
    const float period_z = m_sheet.period_z_m;
    const bool near = m_view == View::Near && !marks.empty ();
    const float span = near ? std::min (period_x, 1800.0f) : period_x;
    const float span_z = span * period_z / period_x;
    const float west = near ? marks.front ().position[0] - 0.5f * span : 0.0f;
    const float north =
      near ? marks.front ().position[2] - 0.5f * span_z : 0.0f;
    const float scale = side / span;

    // Where a place in the world falls on the sheet, if it does.
    const auto place = [&] (const Vec3& at, float& x, float& y) {
      float dx, dz;
      if (near) {
        dx = sheet_delta (at[0] - (west + 0.5f * span), period_x) + 0.5f * span;
        dz = sheet_delta (at[2] - (north + 0.5f * span_z), period_z) +
             0.5f * span_z;
      } else {
        dx = at[0] - period_x * std::floor (at[0] / period_x);
        dz = at[2] - period_z * std::floor (at[2] / period_z);
      }
      x = map_x + dx * scale;
      y = map_y + dz * scale;
      return dx >= 0.0f && dx <= span && dz >= 0.0f && dz <= span_z;
    };

    render::DrawState state;
    state.blend = true;
    state.depth_test = false;
    state.depth_write = false;
    state.cull = false;
    dl.state (state);
    dl.lit (false);
    dl.fogged (false);

    // The country dims behind the sheet, which casts a little shadow.
    dl.color (0.0f, 0.0f, 0.0f, 0.42f);
    box (dl, -64.0f, -64.0f, width + 64.0f, height + 64.0f);
    dl.color (0.0f, 0.0f, 0.0f, 0.30f);
    box (dl,
         paper_x + 3.0f,
         paper_y + 4.0f,
         paper_x + side + 2.0f * pad + 3.0f,
         paper_y + side + pad + footer + 4.0f);
    dl.color (map_paper, 1.0f);
    box (dl,
         paper_x,
         paper_y,
         paper_x + side + 2.0f * pad,
         paper_y + side + pad + footer);

    // The painted sheet. It tiles, so the near view may cross its edge.
    dl.color (1.0f, 1.0f, 1.0f, 1.0f);
    dl.set_texture (m_texture.get ());
    const float u0 = west / period_x, u1 = (west + span) / period_x;
    const float v0 = north / period_z, v1 = (north + span_z) / period_z;
    // One quad to each copy of the picture in view, since the sampler
    // does not repeat it.
    dl.begin (render::Prim::Quads);
    for (float tile_v = std::floor (v0); tile_v < v1; tile_v += 1.0f)
      for (float tile_u = std::floor (u0); tile_u < u1; tile_u += 1.0f) {
        const float ua = std::max (u0, tile_u);
        const float ub = std::min (u1, tile_u + 1.0f);
        const float va = std::max (v0, tile_v);
        const float vb = std::min (v1, tile_v + 1.0f);
        if (ub <= ua || vb <= va)
          continue;
        const float xa = map_x + (ua - u0) / (u1 - u0) * side;
        const float xb = map_x + (ub - u0) / (u1 - u0) * side;
        const float ya = map_y + (va - v0) / (v1 - v0) * side;
        const float yb = map_y + (vb - v0) / (v1 - v0) * side;
        dl.uv (ua - tile_u, va - tile_v);
        dl.vertex (xa, ya);
        dl.uv (ub - tile_u, va - tile_v);
        dl.vertex (xb, ya);
        dl.uv (ub - tile_u, vb - tile_v);
        dl.vertex (xb, yb);
        dl.uv (ua - tile_u, vb - tile_v);
        dl.vertex (xa, yb);
      }
    dl.end ();
    dl.set_texture (nullptr);

    // The neatline, and a tick where each kilometre line meets it.
    dl.color (map_ink, 1.0f);
    dl.line (map_x, map_y, map_x + side, map_y, 1.2f);
    dl.line (map_x + side, map_y, map_x + side, map_y + side, 1.2f);
    dl.line (map_x + side, map_y + side, map_x, map_y + side, 1.2f);
    dl.line (map_x, map_y + side, map_x, map_y, 1.2f);
    const int across_km =
      std::max (1, static_cast<int> (std::lround (period_x / 1000.0f)));
    const int down_km =
      std::max (1, static_cast<int> (std::lround (period_z / 1000.0f)));
    const TextStyle figure = style (9.5f, map_ink);
    for (int k = static_cast<int> (std::ceil (west / 1000.0f));
         static_cast<float> (k) * 1000.0f <= west + span;
         ++k) {
      const float x = map_x + (static_cast<float> (k) * 1000.0f - west) * scale;
      if (x < map_x + 8.0f || x > map_x + side - 8.0f)
        continue;
      dl.line (x, map_y - 4.0f, x, map_y, 1.0f);
      dl.line (x, map_y + side, x, map_y + side + 4.0f, 1.0f);
      if (font)
        font->draw (text,
                    x,
                    map_y - 8.0f,
                    std::to_string (((k % across_km) + across_km) % across_km),
                    figure,
                    TextAlign::Center);
    }
    for (int k = static_cast<int> (std::ceil (north / 1000.0f));
         static_cast<float> (k) * 1000.0f <= north + span_z;
         ++k) {
      const float y =
        map_y + (static_cast<float> (k) * 1000.0f - north) * scale;
      if (y < map_y + 8.0f || y > map_y + side - 8.0f)
        continue;
      dl.line (map_x - 4.0f, y, map_x, y, 1.0f);
      dl.line (map_x + side, y, map_x + side + 4.0f, y, 1.0f);
      if (font)
        font->draw (text,
                    map_x - 8.0f,
                    y + 3.4f,
                    std::to_string (((k % down_km) + down_km) % down_km),
                    figure,
                    TextAlign::Right);
    }

    // Tops: a dot now, their heights later, where the names leave room.
    struct Top {
      float x, y;
      std::string words;
    };
    std::vector<Top> tops;
    for (const ReliefMap::Summit& top : m_sheet.summits) {
      float x, y;
      if (!place (Vec3 (top.x_m, 0.0f, top.z_m), x, y) ||
          x > map_x + side - 30.0f || y < map_y + 10.0f ||
          y > map_y + side - 6.0f)
        continue;
      dl.color (height_ink, 1.0f);
      disc (dl, x, y, 1.6f);
      const int metres = static_cast<int> (std::lround (top.height_m));
      tops.push_back ({ x, y, std::to_string (metres) });
    }

    // What is on the sheet, the most important drawn last so that
    // nothing covers it; each symbol is kept as a box in points.
    struct Placed {
      float x, y;
      const Mark* mark;
    };
    std::vector<Placed> placed;
    for (const Mark& mark : marks) {
      float x, y;
      if (place (mark.position, x, y))
        placed.push_back ({ x, y, &mark });
    }
    std::ranges::stable_sort (
      placed, std::greater<> {}, [] (const Placed& p) {
        return rank (p.mark->kind);
      });
    std::vector<Box> symbols;
    for (const Placed& p : placed) {
      symbol (dl, *p.mark, p.x, p.y, 1.0f);
      symbols.push_back ({ p.x - 7.0f, p.y - 7.0f, p.x + 7.0f, p.y + 7.0f });
    }
    if (!font)
      return;

    // Names. Marks whose symbols run together at this scale -- the
    // machines at the trailhead, the boat and the Caddy at the landing --
    // share one label listing them, each line led by its symbol, so the
    // reader can tell which of the huddle is which.
    std::vector<std::vector<std::size_t>> groups;
    {
      std::vector<std::size_t> of (placed.size ());
      for (std::size_t i = 0; i < placed.size (); ++i)
        of[i] = i;
      const auto root = [&] (std::size_t i) {
        while (of[i] != i)
          i = of[i] = of[of[i]];
        return i;
      };
      for (std::size_t i = 0; i < placed.size (); ++i)
        for (std::size_t j = i + 1; j < placed.size (); ++j)
          if (placed[i].mark->name[0] && placed[j].mark->name[0] &&
              std::hypot (placed[i].x - placed[j].x,
                          placed[i].y - placed[j].y) < 10.0f)
            of[root (j)] = root (i);
      std::vector<std::size_t> slot (placed.size (), SIZE_MAX);
      // Most important first: each group, and the groups in turn, from
      // the end of `placed`.
      for (std::size_t i = placed.size (); i-- > 0;) {
        if (!placed[i].mark->name[0])
          continue;
        std::size_t& g = slot[root (i)];
        if (g == SIZE_MAX) {
          g = groups.size ();
          groups.emplace_back ();
        }
        groups[g].push_back (i);
      }
    }

    // Each label goes where it covers least, tried round its marks in
    // the order a cartographer would -- beside them to the right, to the
    // left, at the corners, above, below -- and then further out on a
    // leader line. A machine's name that can only go over another name
    // is left out: its symbol still shows.
    const auto words_of = [&] (const Mark& mark) {
      // Places are named in the water's blue, sanctuaries in their own
      // ink.
      return mark.place ? style (11.0f,
                                 mark.kind == Mark::Kind::Sanctuary
                                   ? mark.colour
                                   : DisplayColor (0.04f, 0.18f, 0.42f),
                                 0.04f)
                        : style (10.5f, map_ink, 0.02f);
    };
    const auto meets = [] (const Box& a, const Box& b) {
      return a[0] < b[2] && b[0] < a[2] && a[1] < b[3] && b[1] < a[3];
    };
    const Box sheet { map_x + 2.0f, map_y + 2.0f, map_x + side - 2.0f,
                      map_y + side - 2.0f };
    std::vector<Box> set;
    for (const auto& group : groups) {
      const bool list = group.size () > 1;
      const float line = 12.5f;
      const float indent = list ? 12.0f : 0.0f;
      float w = 0.0f;
      for (std::size_t i : group)
        w = std::max (w, font->measure (placed[i].mark->name,
                                        words_of (*placed[i].mark)));
      w += indent;
      const float h = line * static_cast<float> (group.size ()) - 1.0f;
      Box around { 1e9f, 1e9f, -1e9f, -1e9f };
      for (std::size_t i : group)
        around = { std::min (around[0], placed[i].x - 5.0f),
                   std::min (around[1], placed[i].y - 5.0f),
                   std::max (around[2], placed[i].x + 5.0f),
                   std::max (around[3], placed[i].y + 5.0f) };
      const float cx = 0.5f * (around[0] + around[2]);
      const float cy = 0.5f * (around[1] + around[3]);

      float cost = 1e9f;
      Box best {};
      int ring_used = 0;
      for (int ring = 0; ring < 3; ++ring) {
        const float d = 3.0f + 16.0f * ring;
        const float c = 0.7f * d + 1.0f;
        const std::array<std::pair<float, float>, 8> corners { {
          { around[2] + d, cy - 0.5f * h },
          { around[0] - d - w, cy - 0.5f * h },
          { around[2] + c, around[1] - c - h },
          { around[2] + c, around[3] + c },
          { around[0] - c - w, around[1] - c - h },
          { around[0] - c - w, around[3] + c },
          { cx - 0.5f * w, around[1] - d - h },
          { cx - 0.5f * w, around[3] + d },
        } };
        for (std::size_t k = 0; k < corners.size (); ++k) {
          const auto [left, top] = corners[k];
          const Box box { left - 1.0f, top - 1.0f, left + w + 1.0f,
                          top + h + 1.0f };
          if (box[0] < sheet[0] || box[2] > sheet[2] || box[1] < sheet[1] ||
              box[3] > sheet[3])
            continue;
          float here = 24.0f * ring + 2.0f * k;
          for (std::size_t s = 0; s < symbols.size (); ++s)
            if (std::ranges::find (group, s) == group.end () &&
                meets (box, symbols[s]))
              here += 400.0f;
          for (const Box& other : set)
            if (meets (box, other))
              here += 1000.0f;
          for (const Top& top : tops)
            if (meets (box, { top.x - 2.0f, top.y - 2.0f, top.x + 24.0f,
                              top.y + 5.0f }))
              here += 30.0f;
          if (here < cost) {
            cost = here;
            best = box;
            ring_used = ring;
          }
        }
      }
      if (cost >= 1e9f ||
          (cost >= 1000.0f && rank (placed[group.front ()].mark->kind) >= 4))
        continue;
      set.push_back (best);

      if (ring_used > 0) {
        // The leader: from the edge of the marks to the nearest point of
        // the label.
        const float lx = std::clamp (cx, best[0], best[2]);
        const float ly = std::clamp (cy, best[1], best[3]);
        const float dx = lx - cx, dy = ly - cy;
        const float n = std::max (1e-3f, std::hypot (dx, dy));
        const float r = 0.5f * std::max (around[2] - around[0],
                                         around[3] - around[1]) +
                        1.5f;
        dl.color (map_paper, 0.8f);
        dl.line (cx + dx / n * r, cy + dy / n * r, lx, ly, 2.4f);
        dl.color (map_ink, 0.75f);
        dl.line (cx + dx / n * r, cy + dy / n * r, lx, ly, 0.8f);
      }
      // Set toward the marks: flush right when the label is to their
      // left.
      const bool right = best[2] <= around[0] + 0.5f;
      float baseline = best[1] + 1.0f + 9.0f;
      for (std::size_t i : group) {
        const Mark& mark = *placed[i].mark;
        const TextStyle words = words_of (mark);
        if (list)
          symbol (dl,
                  mark,
                  right ? best[2] - 5.5f : best[0] + 5.5f,
                  baseline - 3.6f,
                  0.62f);
        if (right)
          label (text,
                 *font,
                 best[2] - 1.0f - indent,
                 baseline,
                 mark.name,
                 words,
                 TextAlign::Right);
        else
          label (text, *font, best[0] + 1.0f + indent, baseline, mark.name,
                 words);
        baseline += line;
      }
    }

    // The heights of tops, where they are clear of the names and marks.
    for (const Top& top : tops) {
      const Box box { top.x + 3.5f, top.y - 6.5f, top.x + 24.0f,
                      top.y + 5.0f };
      if (std::ranges::any_of (set, [&] (const Box& b) {
            return meets (box, b);
          }) ||
          std::ranges::any_of (symbols, [&] (const Box& b) {
            return meets (box, b);
          }))
        continue;
      label (text, *font, top.x + 4.5f, top.y + 3.4f, top.words,
             style (10.5f, height_ink));
    }

    // Under the map: what sheet this is, a scale, and north.
    const float line_one = map_y + side + 24.0f;
    const float line_two = line_one + 17.0f;
    font->draw (text, map_x, line_one, title, style (15.0f, map_ink, 0.14f));
    const std::string size_words =
      distance_words (period_x) == distance_words (period_z)
        ? std::to_string (across_km) + " × " + distance_words (period_z)
        : distance_words (period_x) + " × " + distance_words (period_z);
    font->draw (
      text,
      map_x,
      line_two,
      size_words + "  ·  contours " +
        std::to_string (static_cast<int> (m_sheet.contour_interval_m)) +
        " m  ·  heights in metres above the sea",
      style (10.0f, map_ink));

    // The scale: two lengths of a round number, the first halved.
    const float nice[] = { 50.0f,  100.0f,  200.0f, 250.0f,
                           500.0f, 1000.0f, 2000.0f };
    float unit = nice[0];
    for (const float candidate : nice)
      if (2.0f * candidate <= 0.30f * span)
        unit = candidate;
    const float bar_right = map_x + side - 46.0f;
    const float bar_left = bar_right - 2.0f * unit * scale;
    const float bar_y = line_one - 7.0f;
    dl.color (map_ink, 1.0f);
    box (dl, bar_left - 0.8f, bar_y - 0.8f, bar_right + 0.8f, bar_y + 4.8f);
    dl.color (map_paper, 1.0f);
    box (dl,
         bar_left + 0.5f * unit * scale,
         bar_y,
         bar_left + unit * scale,
         bar_y + 4.0f);
    box (dl, bar_left + 1.5f * unit * scale, bar_y, bar_right, bar_y + 4.0f);
    const TextStyle scale_figure = style (9.5f, map_ink);
    font->draw (text, bar_left, line_two, "0", scale_figure, TextAlign::Center);
    font->draw (text,
                bar_left + unit * scale,
                line_two,
                distance_words (unit),
                scale_figure,
                TextAlign::Center);
    font->draw (text,
                bar_right,
                line_two,
                distance_words (2.0f * unit),
                scale_figure,
                TextAlign::Center);

    const float north_x = map_x + side - 12.0f;
    dl.color (map_ink, 1.0f);
    dl.line (north_x, line_two - 9.0f, north_x, line_one - 10.0f, 1.2f);
    arrow (dl, north_x, line_one - 12.0f, 0.0f, -1.0f, 7.0f);
    font->draw (text,
                north_x,
                line_two + 1.0f,
                "N",
                style (10.0f, map_ink),
                TextAlign::Center);

    dl.state (render::DrawState ());
    dl.lit (true);
    dl.fogged (true);
    dl.color (1, 1, 1, 1);
  }
}
