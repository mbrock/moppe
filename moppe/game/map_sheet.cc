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

    // How many points of the sheet a metre of the country takes.
    using points_per_metre_t = quantity<display_point / si::metre, float>;

    constexpr DisplayColor map_paper { 0.955f, 0.940f, 0.895f };
    constexpr DisplayColor map_ink { 0.105f, 0.100f, 0.095f };
    constexpr DisplayColor height_ink { 0.130f, 0.085f, 0.050f };
    constexpr DisplayColor sheet_white { 1.0f, 1.0f, 1.0f };

    const opacity_t solid = 1.0f * opacity[one];

    // North on the sheet, which is up.
    const plan_direction_t north_way = plan_unit_direction (Vec2 (0.0f, -1.0f));

    float ratio (auto value) {
      return value.numerical_value_in (one);
    }

    points_t floor_points (points_t value) {
      return std::floor (points_value (value)) * u::pt;
    }

    void set_ink (render::DrawList& dl, DisplayColor colour, opacity_t alpha) {
      dl.color (colour, opacity_value (alpha));
    }

    void vertex (render::DrawList& dl, points_t x, points_t y) {
      dl.vertex (points_value (x), points_value (y));
    }

    void line (render::DrawList& dl,
               points_t x0,
               points_t y0,
               points_t x1,
               points_t y1,
               points_t width) {
      dl.line (points_value (x0),
               points_value (y0),
               points_value (x1),
               points_value (y1),
               points_value (width));
    }

    meters_t sheet_delta (meters_t delta, meters_t period) {
      if (delta > 0.5f * period)
        delta -= period;
      if (delta < -0.5f * period)
        delta += period;
      return delta;
    }

    void box (render::DrawList& dl,
              points_t x0,
              points_t y0,
              points_t x1,
              points_t y1) {
      dl.begin (render::Prim::Quads);
      vertex (dl, x0, y0);
      vertex (dl, x1, y0);
      vertex (dl, x1, y1);
      vertex (dl, x0, y1);
      dl.end ();
    }

    void disc (render::DrawList& dl, points_t x, points_t y, points_t radius) {
      const int sides = 16;
      dl.begin (render::Prim::Triangles);
      for (int side = 0; side < sides; ++side) {
        const radians_t a = 2.0f * std::numbers::pi_v<float> *
                            static_cast<float> (side) / sides * u::rad;
        const radians_t b = 2.0f * std::numbers::pi_v<float> *
                            static_cast<float> (side + 1) / sides * u::rad;
        vertex (dl, x, y);
        vertex (dl,
                x + radius * std::cos (radians_value (a)),
                y + radius * std::sin (radians_value (a)));
        vertex (dl,
                x + radius * std::cos (radians_value (b)),
                y + radius * std::sin (radians_value (b)));
      }
      dl.end ();
    }

    // An arrowhead at (x, y) pointing `way`, the sheet's plan seen from
    // above: x to the right and z down the screen.
    void arrow (render::DrawList& dl,
                points_t x,
                points_t y,
                const plan_direction_t& way,
                points_t size) {
      const float dx = plan_value (way).x (), dy = plan_value (way).z ();
      const float sx = -dy, sy = dx;
      dl.begin (render::Prim::Triangles);
      vertex (dl, x + dx * size, y + dy * size);
      vertex (dl,
              x - dx * 0.62f * size + sx * 0.62f * size,
              y - dy * 0.62f * size + sy * 0.62f * size);
      vertex (dl, x - dx * 0.25f * size, y - dy * 0.25f * size);
      vertex (dl, x + dx * size, y + dy * size);
      vertex (dl, x - dx * 0.25f * size, y - dy * 0.25f * size);
      vertex (dl,
              x - dx * 0.62f * size - sx * 0.62f * size,
              y - dy * 0.62f * size - sy * 0.62f * size);
      dl.end ();
    }

    // Lettering `size` points to the em, with `tracking` of an em after
    // every glyph.
    TextStyle style (points_t size,
                     DisplayColor colour,
                     proportion_t tracking = 0.0f * proportion[one]) {
      TextStyle s;
      s.size = points_value (size);
      s.red = colour.red;
      s.green = colour.green;
      s.blue = colour.blue;
      s.tracking = tracking.numerical_value_in (one);
      return s;
    }

    // Words over the map, on a glow of map_paper so the line work under them
    // does not run through the letters.
    void label (render::TextList& text,
                const render::Font& font,
                points_t x,
                points_t y,
                const std::string& words,
                const TextStyle& set,
                TextAlign align = TextAlign::Left) {
      const opacity_t glow_alpha = 0.85f * opacity[one];
      TextStyle glow = set;
      glow.red = map_paper.red;
      glow.green = map_paper.green;
      glow.blue = map_paper.blue;
      glow.alpha = opacity_value (glow_alpha);
      glow.softness = 5.0f;
      font.draw (text, points_value (x), points_value (y), words, glow, align);
      font.draw (text, points_value (x), points_value (y), words, set, align);
    }

    using Box = std::array<points_t, 4>; // left, top, right, bottom
    using Mark = MapSheet::Mark;

    // Which marks matter most: their symbols are drawn over the others',
    // and their names set first.
    int rank (const Mark& mark) {
      switch (mark.kind) {
      case Mark::Kind::Walker:
        return 0;
      case Mark::Kind::Companion:
        return 1;
      case Mark::Kind::Place:
        return mark.named () ? 2 : 3;
      case Mark::Kind::Camp:
        return 4;
      case Mark::Kind::BusStop:
        return 6;
      default:
        return 5;
      }
    }

    // A mark's symbol at (x, y), `s` times its size on the sheet.
    void symbol (render::DrawList& dl,
                 const Mark& mark,
                 points_t x,
                 points_t y,
                 scale_factor_t s) {
      // A point of the symbol at its size on the sheet.
      const points_t unit = s.numerical_value_in (one) * u::pt;
      const Vec3& heading = direction_value (mark.heading);
      Vec3 way (heading[0], 0.0f, heading[2]);
      way = length2 (way) > 1e-6f ? normalized (way) : Vec3 (0, 0, -1);
      const plan_direction_t facing = plan_unit_direction (plan (way));
      const float dx = way[0], dy = way[2];
      switch (mark.kind) {
      case Mark::Kind::Walker:
      case Mark::Kind::Companion: {
        const points_t size =
          (mark.kind == Mark::Kind::Walker ? 11.0f : 9.0f) * unit;
        set_ink (dl, sheet_white, solid);
        arrow (dl,
               x - dx * 0.8f * unit,
               y - dy * 0.8f * unit,
               facing,
               size + 4.0f * unit);
        set_ink (dl, mark.colour, solid);
        arrow (dl, x, y, facing, size);
        break;
      }
      case Mark::Kind::Bike:
        set_ink (dl, sheet_white, solid);
        disc (dl, x, y, 5.2f * unit);
        set_ink (dl, mark.colour, solid);
        disc (dl, x, y, 3.6f * unit);
        break;
      case Mark::Kind::Jeep:
        set_ink (dl, sheet_white, solid);
        box (dl,
             x - 5.2f * unit,
             y - 5.2f * unit,
             x + 5.2f * unit,
             y + 5.2f * unit);
        set_ink (dl, mark.colour, solid);
        box (dl,
             x - 3.6f * unit,
             y - 3.6f * unit,
             x + 3.6f * unit,
             y + 3.6f * unit);
        break;
      case Mark::Kind::Bus: {
        // A bus seen from above, along the way it goes.
        const float sx = -dy, sy = dx;
        const auto oblong = [&] (points_t half_long, points_t half_wide) {
          dl.begin (render::Prim::Quads);
          vertex (dl,
                  x + dx * half_long + sx * half_wide,
                  y + dy * half_long + sy * half_wide);
          vertex (dl,
                  x + dx * half_long - sx * half_wide,
                  y + dy * half_long - sy * half_wide);
          vertex (dl,
                  x - dx * half_long - sx * half_wide,
                  y - dy * half_long - sy * half_wide);
          vertex (dl,
                  x - dx * half_long + sx * half_wide,
                  y - dy * half_long + sy * half_wide);
          dl.end ();
        };
        set_ink (dl, sheet_white, solid);
        oblong (7.4f * unit, 4.2f * unit);
        set_ink (dl, mark.colour, solid);
        oblong (6.0f * unit, 2.8f * unit);
        set_ink (dl, DisplayColor (0.92f, 0.87f, 0.72f), solid);
        oblong (6.0f * unit, 0.8f * unit);
        break;
      }
      case Mark::Kind::BusStop:
        // A hållplats's yellow plate with its green edge.
        set_ink (dl, DisplayColor (0.06f, 0.42f, 0.22f), solid);
        box (dl,
             x - 3.0f * unit,
             y - 3.0f * unit,
             x + 3.0f * unit,
             y + 3.0f * unit);
        set_ink (dl, DisplayColor (0.97f, 0.80f, 0.12f), solid);
        box (dl,
             x - 1.9f * unit,
             y - 1.9f * unit,
             x + 1.9f * unit,
             y + 1.9f * unit);
        break;
      case Mark::Kind::Camp:
        set_ink (dl, sheet_white, solid);
        arrow (dl, x, y + 1.0f * unit, north_way, 9.5f * unit);
        set_ink (dl, mark.colour, solid);
        arrow (dl, x, y + 0.4f * unit, north_way, 6.5f * unit);
        break;
      case Mark::Kind::Place:
        switch (mark.place) {
        case terrain::RoadPlace::Kind::Trailhead:
          set_ink (dl, mark.colour, solid);
          disc (dl, x, y, 5.0f * unit);
          set_ink (dl, sheet_white, solid);
          disc (dl, x, y, 3.2f * unit);
          break;
        case terrain::RoadPlace::Kind::Landing:
          // An anchor's ring and stock, as charts mark a landing place.
          set_ink (dl, sheet_white, solid);
          disc (dl, x, y, 6.2f * unit);
          set_ink (dl, mark.colour, solid);
          disc (dl, x, y, 4.8f * unit);
          set_ink (dl, sheet_white, solid);
          line (dl, x, y - 3.2f * unit, x, y + 3.0f * unit, 1.1f * unit);
          line (dl,
                x - 2.4f * unit,
                y - 1.6f * unit,
                x + 2.4f * unit,
                y - 1.6f * unit,
                1.1f * unit);
          line (dl,
                x - 2.6f * unit,
                y + 1.6f * unit,
                x,
                y + 3.0f * unit,
                1.0f * unit);
          line (dl,
                x + 2.6f * unit,
                y + 1.6f * unit,
                x,
                y + 3.0f * unit,
                1.0f * unit);
          break;
        case terrain::RoadPlace::Kind::LakeOutlet:
          // A mill wheel: a ring with spokes.
          set_ink (dl, sheet_white, solid);
          disc (dl, x, y, 5.6f * unit);
          set_ink (dl, mark.colour, solid);
          disc (dl, x, y, 4.3f * unit);
          set_ink (dl, sheet_white, solid);
          disc (dl, x, y, 1.3f * unit);
          line (dl, x - 3.4f * unit, y, x + 3.4f * unit, y, 0.9f * unit);
          line (dl, x, y - 3.4f * unit, x, y + 3.4f * unit, 0.9f * unit);
          break;
        case terrain::RoadPlace::Kind::Sanctuary:
          // The sheet already carries its triangle; a dot of fire in it.
          // In a list, where there is no triangle, the triangle too.
          if (s < 1.0f * scale_factor[one]) {
            set_ink (dl, mark.colour, solid);
            arrow (dl, x, y + 2.0f * u::pt, north_way, 8.0f * unit);
          }
          set_ink (dl, mark.colour, solid);
          disc (dl, x, y, 2.4f * unit);
          break;
        }
        break;
      }
    }

    std::string distance_words (meters_t metres) {
      if (metres >= 1000.0f * u::m) {
        const float km = ratio (metres / (1000.0f * u::m));
        return (km == std::floor (km)
                  ? std::to_string (static_cast<int> (km))
                  : std::to_string (static_cast<int> (km)) + "." +
                      std::to_string (
                        static_cast<int> (std::lround (km * 10.0f)) % 10)) +
               " km";
      }
      return std::to_string (static_cast<int> (meters_value (metres))) + " m";
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
                       points_t width,
                       points_t height) const {
    const meters_t period_x = plan_value (m_sheet.period).x () * u::m;
    const meters_t period_z = plan_value (m_sheet.period).z () * u::m;
    if (!open () || !m_texture || period_x <= 0.0f * u::m)
      return;
    const points_t margin = 12.0f * u::pt;
    const points_t pad = 26.0f * u::pt;
    const points_t footer = 56.0f * u::pt;
    const points_t side = floor_points (std::min (
      width - 2.0f * (margin + pad), height - 2.0f * margin - pad - footer));
    if (side < 120.0f * u::pt)
      return;
    const points_t paper_x = floor_points (0.5f * (width - side) - pad);
    const points_t paper_y =
      floor_points (0.5f * (height - (side + pad + footer)));
    const points_t map_x = paper_x + pad;
    const points_t map_y = paper_y + pad;

    // The stretch of country in view. It is square on the ground, as the
    // world is.
    const bool near = m_view == View::Near && !marks.empty ();
    const meters_t span = near ? std::min (period_x, 1800.0f * u::m) : period_x;
    const meters_t span_z = span * period_z / period_x;
    const terrain::torus_plan_position_t middle =
      marks.empty () ? terrain::torus_plan_position (Vec2 (0.0f, 0.0f))
                     : terrain::plan (marks.front ().position);
    const meters_t west =
      near ? terrain::x_of (middle) - 0.5f * span : 0.0f * u::m;
    const meters_t north =
      near ? terrain::z_of (middle) - 0.5f * span_z : 0.0f * u::m;
    const points_per_metre_t scale = side / span;
    const meters_t kilometre = 1000.0f * u::m;

    // Where a place on the torus falls on the sheet, if it does.
    const auto place = [&] (const terrain::torus_plan_position_t& at,
                            points_t& x,
                            points_t& y) {
      meters_t dx, dz;
      if (near) {
        dx = sheet_delta (terrain::x_of (at) - (west + 0.5f * span), period_x) +
             0.5f * span;
        dz =
          sheet_delta (terrain::z_of (at) - (north + 0.5f * span_z), period_z) +
          0.5f * span_z;
      } else {
        dx = terrain::x_of (at) -
             period_x * std::floor (ratio (terrain::x_of (at) / period_x));
        dz = terrain::z_of (at) -
             period_z * std::floor (ratio (terrain::z_of (at) / period_z));
      }
      x = map_x + dx * scale;
      y = map_y + dz * scale;
      return dx >= 0.0f * u::m && dx <= span && dz >= 0.0f * u::m &&
             dz <= span_z;
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
    const DisplayColor black { 0.0f, 0.0f, 0.0f };
    set_ink (dl, black, 0.42f * opacity[one]);
    box (dl,
         -64.0f * u::pt,
         -64.0f * u::pt,
         width + 64.0f * u::pt,
         height + 64.0f * u::pt);
    set_ink (dl, black, 0.30f * opacity[one]);
    box (dl,
         paper_x + 3.0f * u::pt,
         paper_y + 4.0f * u::pt,
         paper_x + side + 2.0f * pad + 3.0f * u::pt,
         paper_y + side + pad + footer + 4.0f * u::pt);
    set_ink (dl, map_paper, solid);
    box (dl,
         paper_x,
         paper_y,
         paper_x + side + 2.0f * pad,
         paper_y + side + pad + footer);

    // The painted sheet. It tiles, so the near view may cross its edge.
    // Its texture coordinates count laps of the world.
    set_ink (dl, sheet_white, solid);
    dl.set_texture (m_texture.get ());
    const float u0 = ratio (west / period_x),
                u1 = ratio ((west + span) / period_x);
    const float v0 = ratio (north / period_z),
                v1 = ratio ((north + span_z) / period_z);
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
        const points_t xa = map_x + (ua - u0) / (u1 - u0) * side;
        const points_t xb = map_x + (ub - u0) / (u1 - u0) * side;
        const points_t ya = map_y + (va - v0) / (v1 - v0) * side;
        const points_t yb = map_y + (vb - v0) / (v1 - v0) * side;
        dl.uv (ua - tile_u, va - tile_v);
        vertex (dl, xa, ya);
        dl.uv (ub - tile_u, va - tile_v);
        vertex (dl, xb, ya);
        dl.uv (ub - tile_u, vb - tile_v);
        vertex (dl, xb, yb);
        dl.uv (ua - tile_u, vb - tile_v);
        vertex (dl, xa, yb);
      }
    dl.end ();
    dl.set_texture (nullptr);

    // The neatline, and a tick where each kilometre line meets it.
    const points_t neat = 1.2f * u::pt;
    const points_t hair = 1.0f * u::pt;
    const points_t tick = 4.0f * u::pt;
    set_ink (dl, map_ink, solid);
    line (dl, map_x, map_y, map_x + side, map_y, neat);
    line (dl, map_x + side, map_y, map_x + side, map_y + side, neat);
    line (dl, map_x + side, map_y + side, map_x, map_y + side, neat);
    line (dl, map_x, map_y + side, map_x, map_y, neat);
    const int across_km = std::max (
      1, static_cast<int> (std::lround (ratio (period_x / kilometre))));
    const int down_km = std::max (
      1, static_cast<int> (std::lround (ratio (period_z / kilometre))));
    const TextStyle figure = style (9.5f * u::pt, map_ink);
    for (int k = static_cast<int> (std::ceil (ratio (west / kilometre)));
         static_cast<float> (k) * 1000.0f * u::m <= west + span;
         ++k) {
      const points_t x =
        map_x + (static_cast<float> (k) * 1000.0f * u::m - west) * scale;
      if (x < map_x + 8.0f * u::pt || x > map_x + side - 8.0f * u::pt)
        continue;
      line (dl, x, map_y - tick, x, map_y, hair);
      line (dl, x, map_y + side, x, map_y + side + tick, hair);
      if (font)
        font->draw (text,
                    points_value (x),
                    points_value (map_y - 8.0f * u::pt),
                    std::to_string (((k % across_km) + across_km) % across_km),
                    figure,
                    TextAlign::Center);
    }
    for (int k = static_cast<int> (std::ceil (ratio (north / kilometre)));
         static_cast<float> (k) * 1000.0f * u::m <= north + span_z;
         ++k) {
      const points_t y =
        map_y + (static_cast<float> (k) * 1000.0f * u::m - north) * scale;
      if (y < map_y + 8.0f * u::pt || y > map_y + side - 8.0f * u::pt)
        continue;
      line (dl, map_x - tick, y, map_x, y, hair);
      line (dl, map_x + side, y, map_x + side + tick, y, hair);
      if (font)
        font->draw (text,
                    points_value (map_x - 8.0f * u::pt),
                    points_value (y + 3.4f * u::pt),
                    std::to_string (((k % down_km) + down_km) % down_km),
                    figure,
                    TextAlign::Right);
    }

    // Tops: a dot now, their heights later, where the names leave room.
    struct Top {
      points_t x, y;
      std::string words;
    };
    std::vector<Top> tops;
    for (const ReliefMap::Summit& top : m_sheet.summits) {
      points_t x, y;
      if (!place (
            top.position.point_for (terrain::torus_plan_origin, m_sheet.period),
            x,
            y) ||
          x > map_x + side - 30.0f * u::pt || y < map_y + 10.0f * u::pt ||
          y > map_y + side - 6.0f * u::pt)
        continue;
      set_ink (dl, height_ink, solid);
      disc (dl, x, y, 1.6f * u::pt);
      const int metres = static_cast<int> (
        std::lround (top.above_sea.numerical_value_in (u::m)));
      tops.push_back ({ x, y, std::to_string (metres) });
    }

    // What is on the sheet, the most important drawn last so that
    // nothing covers it; each symbol is kept as a box in points.
    struct Placed {
      points_t x, y;
      const Mark* mark;
    };
    std::vector<Placed> placed;
    for (const Mark& mark : marks) {
      points_t x, y;
      if (place (terrain::plan (mark.position), x, y))
        placed.push_back ({ x, y, &mark });
    }
    std::ranges::stable_sort (placed, std::greater<> {}, [] (const Placed& p) {
      return rank (*p.mark);
    });
    const points_t symbol_half = 7.0f * u::pt;
    std::vector<Box> symbols;
    for (const Placed& p : placed) {
      symbol (dl, *p.mark, p.x, p.y, 1.0f * scale_factor[one]);
      symbols.push_back ({ p.x - symbol_half,
                           p.y - symbol_half,
                           p.x + symbol_half,
                           p.y + symbol_half });
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
      // A stop is named only near to; the whole sheet has too many.
      const auto named = [near] (const Mark& mark) {
        return mark.name[0] && (near || mark.kind != Mark::Kind::BusStop);
      };
      for (std::size_t i = 0; i < placed.size (); ++i)
        for (std::size_t j = i + 1; j < placed.size (); ++j)
          if (named (*placed[i].mark) && named (*placed[j].mark) &&
              hypot (placed[i].x - placed[j].x, placed[i].y - placed[j].y) <
                10.0f * u::pt)
            of[root (j)] = root (i);
      std::vector<std::size_t> slot (placed.size (), SIZE_MAX);
      // Most important first: each group, and the groups in turn, from
      // the end of `placed`.
      for (std::size_t i = placed.size (); i-- > 0;) {
        if (!named (*placed[i].mark))
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
      return mark.named ()
               ? style (11.0f * u::pt,
                        mark.place == terrain::RoadPlace::Kind::Sanctuary
                          ? mark.colour
                          : DisplayColor (0.04f, 0.18f, 0.42f),
                        0.04f * proportion[one])
               : style (10.5f * u::pt, map_ink, 0.02f * proportion[one]);
    };
    const auto meets = [] (const Box& a, const Box& b) {
      return a[0] < b[2] && b[0] < a[2] && a[1] < b[3] && b[1] < a[3];
    };
    const points_t inset = 2.0f * u::pt;
    const Box sheet {
      map_x + inset, map_y + inset, map_x + side - inset, map_y + side - inset
    };
    const points_t edge = 1.0f * u::pt;
    // Placing a label is scored by what it covers; the cheapest wins.
    const float nowhere = 1e9f;
    std::vector<Box> set;
    for (const auto& group : groups) {
      const bool list = group.size () > 1;
      const points_t line_height = 12.5f * u::pt;
      const points_t indent = list ? 12.0f * u::pt : 0.0f * u::pt;
      points_t w = 0.0f * u::pt;
      for (std::size_t i : group)
        w = std::max (
          w,
          font->measure (placed[i].mark->name, words_of (*placed[i].mark)) *
            u::pt);
      w += indent;
      const points_t h =
        line_height * static_cast<float> (group.size ()) - 1.0f * u::pt;
      const points_t far = 1e9f * u::pt;
      const points_t reach = 5.0f * u::pt;
      Box around { far, far, -far, -far };
      for (std::size_t i : group)
        around = { std::min (around[0], placed[i].x - reach),
                   std::min (around[1], placed[i].y - reach),
                   std::max (around[2], placed[i].x + reach),
                   std::max (around[3], placed[i].y + reach) };
      const points_t cx = 0.5f * (around[0] + around[2]);
      const points_t cy = 0.5f * (around[1] + around[3]);

      float cost = nowhere;
      Box best {};
      int ring_used = 0;
      for (int ring = 0; ring < 3; ++ring) {
        const points_t d =
          3.0f * u::pt + 16.0f * static_cast<float> (ring) * u::pt;
        const points_t c = 0.7f * d + 1.0f * u::pt;
        const std::array<std::pair<points_t, points_t>, 8> corners { {
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
          const Box box {
            left - edge, top - edge, left + w + edge, top + h + edge
          };
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
            if (meets (box,
                       { top.x - 2.0f * u::pt,
                         top.y - 2.0f * u::pt,
                         top.x + 24.0f * u::pt,
                         top.y + 5.0f * u::pt }))
              here += 30.0f;
          if (here < cost) {
            cost = here;
            best = box;
            ring_used = ring;
          }
        }
      }
      if (cost >= nowhere ||
          (cost >= 1000.0f && rank (*placed[group.front ()].mark) >= 4))
        continue;
      set.push_back (best);

      if (ring_used > 0) {
        // The leader: from the edge of the marks to the nearest point of
        // the label.
        const points_t lx = std::clamp (cx, best[0], best[2]);
        const points_t ly = std::clamp (cy, best[1], best[3]);
        const points_t dx = lx - cx, dy = ly - cy;
        const points_t n = std::max (1e-3f * u::pt, hypot (dx, dy));
        const points_t r =
          0.5f * std::max (around[2] - around[0], around[3] - around[1]) +
          1.5f * u::pt;
        set_ink (dl, map_paper, 0.8f * opacity[one]);
        line (dl, cx + dx / n * r, cy + dy / n * r, lx, ly, 2.4f * u::pt);
        set_ink (dl, map_ink, 0.75f * opacity[one]);
        line (dl, cx + dx / n * r, cy + dy / n * r, lx, ly, 0.8f * u::pt);
      }
      // Set toward the marks: flush right when the label is to their
      // left.
      const bool right = best[2] <= around[0] + 0.5f * u::pt;
      points_t baseline = best[1] + 1.0f * u::pt + 9.0f * u::pt;
      for (std::size_t i : group) {
        const Mark& mark = *placed[i].mark;
        const TextStyle words = words_of (mark);
        if (list)
          symbol (dl,
                  mark,
                  right ? best[2] - 5.5f * u::pt : best[0] + 5.5f * u::pt,
                  baseline - 3.6f * u::pt,
                  0.62f * scale_factor[one]);
        if (right)
          label (text,
                 *font,
                 best[2] - 1.0f * u::pt - indent,
                 baseline,
                 mark.name,
                 words,
                 TextAlign::Right);
        else
          label (text,
                 *font,
                 best[0] + 1.0f * u::pt + indent,
                 baseline,
                 mark.name,
                 words);
        baseline += line_height;
      }
    }

    // The heights of tops, where they are clear of the names and marks.
    for (const Top& top : tops) {
      const Box box { top.x + 3.5f * u::pt,
                      top.y - 6.5f * u::pt,
                      top.x + 24.0f * u::pt,
                      top.y + 5.0f * u::pt };
      if (std::ranges::any_of (set,
                               [&] (const Box& b) { return meets (box, b); }) ||
          std::ranges::any_of (symbols,
                               [&] (const Box& b) { return meets (box, b); }))
        continue;
      label (text,
             *font,
             top.x + 4.5f * u::pt,
             top.y + 3.4f * u::pt,
             top.words,
             style (10.5f * u::pt, height_ink));
    }

    // Under the map: what sheet this is, a scale, and north.
    const points_t line_one = map_y + side + 24.0f * u::pt;
    const points_t line_two = line_one + 17.0f * u::pt;
    font->draw (text,
                points_value (map_x),
                points_value (line_one),
                title,
                style (15.0f * u::pt, map_ink, 0.14f * proportion[one]));
    const std::string size_words =
      distance_words (period_x) == distance_words (period_z)
        ? std::to_string (across_km) + " × " + distance_words (period_z)
        : distance_words (period_x) + " × " + distance_words (period_z);
    font->draw (text,
                points_value (map_x),
                points_value (line_two),
                size_words + "  ·  contours " +
                  std::to_string (static_cast<int> (
                    m_sheet.contour_interval.numerical_value_in (u::m))) +
                  " m  ·  heights in metres above the sea",
                style (10.0f * u::pt, map_ink));

    // The scale: two lengths of a round number, the first halved.
    const meters_t nice[] = { 50.0f * u::m,  100.0f * u::m, 200.0f * u::m,
                              250.0f * u::m, 500.0f * u::m, 1000.0f * u::m,
                              2000.0f * u::m };
    meters_t unit = nice[0];
    for (const meters_t candidate : nice)
      if (2.0f * candidate <= 0.30f * span)
        unit = candidate;
    const points_t bar_right = map_x + side - 46.0f * u::pt;
    const points_t bar_left = bar_right - 2.0f * unit * scale;
    const points_t bar_y = line_one - 7.0f * u::pt;
    const points_t bar_edge = 0.8f * u::pt;
    set_ink (dl, map_ink, solid);
    box (dl,
         bar_left - bar_edge,
         bar_y - bar_edge,
         bar_right + bar_edge,
         bar_y + 4.8f * u::pt);
    set_ink (dl, map_paper, solid);
    box (dl,
         bar_left + 0.5f * unit * scale,
         bar_y,
         bar_left + unit * scale,
         bar_y + 4.0f * u::pt);
    box (dl,
         bar_left + 1.5f * unit * scale,
         bar_y,
         bar_right,
         bar_y + 4.0f * u::pt);
    const TextStyle scale_figure = style (9.5f * u::pt, map_ink);
    font->draw (text,
                points_value (bar_left),
                points_value (line_two),
                "0",
                scale_figure,
                TextAlign::Center);
    font->draw (text,
                points_value (bar_left + unit * scale),
                points_value (line_two),
                distance_words (unit),
                scale_figure,
                TextAlign::Center);
    font->draw (text,
                points_value (bar_right),
                points_value (line_two),
                distance_words (2.0f * unit),
                scale_figure,
                TextAlign::Center);

    const points_t north_x = map_x + side - 12.0f * u::pt;
    set_ink (dl, map_ink, solid);
    line (dl,
          north_x,
          line_two - 9.0f * u::pt,
          north_x,
          line_one - 10.0f * u::pt,
          neat);
    arrow (dl, north_x, line_one - 12.0f * u::pt, north_way, 7.0f * u::pt);
    font->draw (text,
                points_value (north_x),
                points_value (line_two + 1.0f * u::pt),
                "N",
                style (10.0f * u::pt, map_ink),
                TextAlign::Center);

    dl.state (render::DrawState ());
    dl.lit (true);
    dl.fogged (true);
    dl.color (1, 1, 1, 1);
  }
}
