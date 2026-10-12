#include <moppe/game/map_sheet.hh>

#include <moppe/render/renderer.hh>

#include <algorithm>
#include <cmath>
#include <numbers>
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
      font.draw (text, x, y, words, set, align);
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

    // Tops and their heights.
    for (const ReliefMap::Summit& top : m_sheet.summits) {
      float x, y;
      if (!place (Vec3 (top.x_m, 0.0f, top.z_m), x, y) ||
          x > map_x + side - 30.0f || y < map_y + 10.0f ||
          y > map_y + side - 6.0f)
        continue;
      dl.color (height_ink, 1.0f);
      disc (dl, x, y, 1.6f);
      if (font)
        label (text,
               *font,
               x + 4.5f,
               y + 3.4f,
               std::to_string (static_cast<int> (std::lround (top.height_m))),
               style (10.5f, height_ink));
    }

    // What moves, the walker last so that nothing covers them.
    std::vector<std::pair<float, float>> named;
    for (std::size_t i = marks.size (); i-- > 0;) {
      const Mark& mark = marks[i];
      float x, y;
      if (!place (mark.position, x, y))
        continue;
      Vec3 way (mark.heading[0], 0.0f, mark.heading[2]);
      way = length2 (way) > 1e-6f ? normalized (way) : Vec3 (0, 0, -1);
      const float dx = way[0], dy = way[2];
      switch (mark.kind) {
      case Mark::Kind::Walker:
      case Mark::Kind::Companion: {
        const float size = mark.kind == Mark::Kind::Walker ? 11.0f : 9.0f;
        dl.color (1.0f, 1.0f, 1.0f, 1.0f);
        arrow (dl, x - dx * 0.8f, y - dy * 0.8f, dx, dy, size + 4.0f);
        dl.color (mark.colour, 1.0f);
        arrow (dl, x, y, dx, dy, size);
        break;
      }
      case Mark::Kind::Bike:
        dl.color (1.0f, 1.0f, 1.0f, 1.0f);
        disc (dl, x, y, 5.2f);
        dl.color (mark.colour, 1.0f);
        disc (dl, x, y, 3.6f);
        break;
      case Mark::Kind::Jeep:
        dl.color (1.0f, 1.0f, 1.0f, 1.0f);
        box (dl, x - 5.2f, y - 5.2f, x + 5.2f, y + 5.2f);
        dl.color (mark.colour, 1.0f);
        box (dl, x - 3.6f, y - 3.6f, x + 3.6f, y + 3.6f);
        break;
      case Mark::Kind::Camp:
        dl.color (1.0f, 1.0f, 1.0f, 1.0f);
        arrow (dl, x, y + 1.0f, 0.0f, -1.0f, 9.5f);
        dl.color (mark.colour, 1.0f);
        arrow (dl, x, y + 0.4f, 0.0f, -1.0f, 6.5f);
        break;
      case Mark::Kind::Trailhead:
        dl.color (mark.colour, 1.0f);
        disc (dl, x, y, 5.0f);
        dl.color (1.0f, 1.0f, 1.0f, 1.0f);
        disc (dl, x, y, 3.2f);
        break;
      }
      if (font && mark.name[0] && x < map_x + side - 50.0f) {
        // Things that stand together have their names one under another.
        float line = y + 3.8f;
        for (bool moved = true; moved;) {
          moved = false;
          for (const auto& [taken_x, taken_y] : named)
            if (std::fabs (taken_x - x) < 60.0f &&
                std::fabs (taken_y - line) < 11.5f) {
              line = taken_y + 12.0f;
              moved = true;
            }
        }
        named.emplace_back (x, line);
        label (text,
               *font,
               x + 10.0f,
               line,
               mark.name,
               style (10.5f, map_ink, 0.02f));
      }
    }

    if (!font)
      return;

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
