#include <moppe/game/fingerposts.hh>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <numeric>
#include <queue>
#include <utility>

namespace moppe::game {
  namespace {
    constexpr float k_infinite = std::numeric_limits<float>::infinity ();
    // How near a way's end must come to another way to join it, and how
    // near cuts must be to be one node.
    constexpr float k_join_m = 8.0f;
    constexpr float k_node_m = 10.0f;
    // How near a place must lie to a way to stand on it.
    constexpr float k_place_m = 25.0f;
    // No post nearer a place than this: the place is its own landmark.
    constexpr float k_clear_of_place_m = 60.0f;
    // Nor nearer another post.
    constexpr float k_clear_of_post_m = 30.0f;

    float post_wrap (float value, float period) {
      return value - period * std::floor (value / period);
    }

    float post_delta (float delta, float period) {
      return delta - period * std::round (delta / period);
    }

    struct PostLine {
      std::vector<float> x, z;
      // Metres along from the first point.
      std::vector<float> along;
    };

    struct PostCut {
      std::size_t road = 0;
      // A fractional point index.
      float at = 0.0f;
      float x = 0.0f, z = 0.0f;
    };

    struct PostEdge {
      std::size_t a = 0, b = 0;
      float metres = 0.0f;
      std::size_t road = 0;
      // Away from each end, a unit vector.
      float a_dx = 1.0f, a_dz = 0.0f;
      float b_dx = 1.0f, b_dz = 0.0f;
    };
  }

  std::vector<Fingerpost> plan_fingerposts (const terrain::RoadNetwork& network,
                                            float period_x,
                                            float period_z) {
    const auto distance = [&] (float ax, float az, float bx, float bz) {
      return std::hypot (post_delta (ax - bx, period_x),
                         post_delta (az - bz, period_z));
    };

    // Every way as a line within one period, read point by point.
    std::vector<PostLine> lines (network.roads.size ());
    for (std::size_t r = 0; r < network.roads.size (); ++r) {
      PostLine& line = lines[r];
      float along = 0.0f;
      for (std::size_t i = 0; i < network.roads[r].points.size (); ++i) {
        const terrain::RoadPoint& p = network.roads[r].points[i];
        if (i > 0) {
          const terrain::RoadPoint& q = network.roads[r].points[i - 1];
          along += std::hypot (p.x_m - q.x_m, p.z_m - q.z_m);
        }
        line.x.push_back (post_wrap (p.x_m, period_x));
        line.z.push_back (post_wrap (p.z_m, period_z));
        line.along.push_back (along);
      }
    }
    const auto point_at =
      [&] (const PostLine& line, float at, float& x, float& z) {
        const std::size_t i =
          std::min (static_cast<std::size_t> (at), line.x.size () - 2);
        const float t = at - static_cast<float> (i);
        x = post_wrap (line.x[i] +
                         t * post_delta (line.x[i + 1] - line.x[i], period_x),
                       period_x);
        z = post_wrap (line.z[i] +
                         t * post_delta (line.z[i + 1] - line.z[i], period_z),
                       period_z);
      };
    const auto metres_at = [&] (const PostLine& line, float at) {
      const std::size_t i =
        std::min (static_cast<std::size_t> (at), line.x.size () - 2);
      const float t = at - static_cast<float> (i);
      return line.along[i] + t * (line.along[i + 1] - line.along[i]);
    };
    // The nearest point of a line to a point: its fractional index and
    // how far off it is.
    const auto nearest = [&] (const PostLine& line, float x, float z) {
      std::pair<float, float> best { 0.0f, k_infinite };
      for (std::size_t i = 0; i + 1 < line.x.size (); ++i) {
        const float ax = post_delta (x - line.x[i], period_x);
        const float az = post_delta (z - line.z[i], period_z);
        const float sx = post_delta (line.x[i + 1] - line.x[i], period_x);
        const float sz = post_delta (line.z[i + 1] - line.z[i], period_z);
        const float length2 = sx * sx + sz * sz;
        const float t =
          length2 > 0.0f
            ? std::clamp ((ax * sx + az * sz) / length2, 0.0f, 1.0f)
            : 0.0f;
        const float off = std::hypot (ax - t * sx, az - t * sz);
        if (off < best.second)
          best = { static_cast<float> (i) + t, off };
      }
      return best;
    };

    // Where the ways are cut: at their ends, where another's end meets
    // them, and where a place stands on them.
    std::vector<PostCut> cuts;
    const auto cut = [&] (std::size_t road, float at) {
      PostCut c { road, at };
      point_at (lines[road], at, c.x, c.z);
      cuts.push_back (c);
      return cuts.size () - 1;
    };
    for (std::size_t r = 0; r < lines.size (); ++r) {
      if (lines[r].x.size () < 2)
        continue;
      cut (r, 0.0f);
      cut (r, static_cast<float> (lines[r].x.size () - 1));
    }
    for (std::size_t r = 0; r < lines.size (); ++r) {
      if (lines[r].x.size () < 2)
        continue;
      for (const bool back : { false, true }) {
        const float ex = back ? lines[r].x.back () : lines[r].x.front ();
        const float ez = back ? lines[r].z.back () : lines[r].z.front ();
        for (std::size_t s = 0; s < lines.size (); ++s) {
          if (s == r || lines[s].x.size () < 2)
            continue;
          const auto [at, off] = nearest (lines[s], ex, ez);
          if (off < k_join_m)
            cut (s, at);
        }
      }
    }
    std::vector<std::size_t> place_cut (network.places.size (),
                                        static_cast<std::size_t> (-1));
    for (std::size_t p = 0; p < network.places.size (); ++p) {
      const float px = post_wrap (network.places[p].x_m, period_x);
      const float pz = post_wrap (network.places[p].z_m, period_z);
      std::size_t best_road = 0;
      std::pair<float, float> best { 0.0f, k_infinite };
      for (std::size_t r = 0; r < lines.size (); ++r) {
        if (lines[r].x.size () < 2)
          continue;
        const auto found = nearest (lines[r], px, pz);
        if (found.second < best.second) {
          best = found;
          best_road = r;
        }
      }
      if (best.second < k_place_m)
        place_cut[p] = cut (best_road, best.first);
    }

    // Cuts close together are one node.
    std::vector<std::size_t> parent (cuts.size ());
    std::iota (parent.begin (), parent.end (), std::size_t { 0 });
    const std::function<std::size_t (std::size_t)> root = [&] (std::size_t i) {
      return parent[i] == i ? i : parent[i] = root (parent[i]);
    };
    for (std::size_t i = 0; i < cuts.size (); ++i)
      for (std::size_t j = i + 1; j < cuts.size (); ++j)
        if (distance (cuts[i].x, cuts[i].z, cuts[j].x, cuts[j].z) < k_node_m)
          parent[root (j)] = root (i);
    std::vector<std::size_t> node_of (cuts.size ());
    std::vector<std::size_t> node_cut;
    {
      std::vector<std::size_t> numbered (cuts.size (),
                                         static_cast<std::size_t> (-1));
      for (std::size_t i = 0; i < cuts.size (); ++i) {
        const std::size_t r = root (i);
        if (numbered[r] == static_cast<std::size_t> (-1)) {
          numbered[r] = node_cut.size ();
          node_cut.push_back (r);
        }
        node_of[i] = numbered[r];
      }
    }
    const std::size_t nodes = node_cut.size ();

    // The stretches of way between consecutive cuts.
    std::vector<PostEdge> edges;
    for (std::size_t r = 0; r < lines.size (); ++r) {
      std::vector<std::pair<float, std::size_t>> along;
      for (std::size_t i = 0; i < cuts.size (); ++i)
        if (cuts[i].road == r)
          along.emplace_back (cuts[i].at, node_of[i]);
      std::ranges::sort (along);
      const PostLine& line = lines[r];
      for (std::size_t k = 0; k + 1 < along.size (); ++k) {
        const auto [from, a] = along[k];
        const auto [to, b] = along[k + 1];
        if (a == b)
          continue;
        PostEdge edge {
          a, b, metres_at (line, to) - metres_at (line, from), r
        };
        // The way's heading some fifteen metres in from each end.
        const auto heading =
          [&] (float start, float sign, float& dx, float& dz) {
            const float s = metres_at (line, start);
            const float reach = std::min (15.0f, 0.5f * edge.metres);
            float at = start;
            while (sign > 0.0f ? at < to : at > from) {
              if (std::fabs (metres_at (line, at) - s) >= reach)
                break;
              at += 0.25f * sign;
            }
            at = std::clamp (at, from, to);
            float sx, sz, ex, ez;
            point_at (line, start, sx, sz);
            point_at (line, at, ex, ez);
            dx = post_delta (ex - sx, period_x);
            dz = post_delta (ez - sz, period_z);
            const float n = std::hypot (dx, dz);
            if (n > 1e-4f) {
              dx /= n;
              dz /= n;
            } else {
              dx = 1.0f;
              dz = 0.0f;
            }
          };
        heading (from, 1.0f, edge.a_dx, edge.a_dz);
        heading (to, -1.0f, edge.b_dx, edge.b_dz);
        edges.push_back (edge);
      }
    }
    std::vector<std::vector<std::size_t>> touching (nodes);
    for (std::size_t e = 0; e < edges.size (); ++e) {
      touching[edges[e].a].push_back (e);
      touching[edges[e].b].push_back (e);
    }

    std::vector<std::size_t> place_node (network.places.size (),
                                         static_cast<std::size_t> (-1));
    for (std::size_t p = 0; p < network.places.size (); ++p)
      if (place_cut[p] != static_cast<std::size_t> (-1))
        place_node[p] = node_of[place_cut[p]];

    std::vector<Fingerpost> posts;
    for (std::size_t n = 0; n < nodes; ++n) {
      const std::vector<std::size_t>& ways = touching[n];
      if (ways.size () < 2)
        continue;
      bool mixed = false;
      for (const std::size_t e : ways)
        mixed = mixed || network.roads[edges[e].road].kind !=
                           network.roads[edges[ways.front ()].road].kind;
      if (ways.size () < 3 && !mixed)
        continue;
      const PostCut& here = cuts[node_cut[n]];
      bool clear = true;
      for (const terrain::RoadPlace& place : network.places)
        clear = clear && distance (here.x,
                                   here.z,
                                   post_wrap (place.x_m, period_x),
                                   post_wrap (place.z_m, period_z)) >=
                           k_clear_of_place_m;
      for (const Fingerpost& post : posts)
        clear =
          clear &&
          distance (here.x, here.z, post.junction_x_m, post.junction_z_m) >=
            k_clear_of_post_m;
      if (!clear)
        continue;

      // The shortest ways out from here, and which arm each leaves by.
      std::vector<float> metres (nodes, k_infinite);
      std::vector<std::size_t> by (nodes, static_cast<std::size_t> (-1));
      using Entry = std::pair<float, std::size_t>;
      std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
      metres[n] = 0.0f;
      open.push ({ 0.0f, n });
      while (!open.empty ()) {
        const auto [reached, node] = open.top ();
        open.pop ();
        if (reached > metres[node])
          continue;
        for (const std::size_t e : touching[node]) {
          const std::size_t next = edges[e].a == node ? edges[e].b : edges[e].a;
          const float further = reached + edges[e].metres;
          if (further < metres[next]) {
            metres[next] = further;
            by[next] = node == n ? e : by[node];
            open.push ({ further, next });
          }
        }
      }

      Fingerpost post;
      post.junction_x_m = here.x;
      post.junction_z_m = here.z;
      for (const std::size_t e : ways) {
        Fingerpost::Arm arm;
        const PostEdge& edge = edges[e];
        arm.direction_x = edge.a == n ? edge.a_dx : edge.b_dx;
        arm.direction_z = edge.a == n ? edge.a_dz : edge.b_dz;
        arm.kind = network.roads[edge.road].kind;
        arm.abandoned = network.roads[edge.road].abandoned_years_ago > 0.0f;
        for (std::size_t p = 0; p < network.places.size (); ++p)
          if (place_node[p] != static_cast<std::size_t> (-1) &&
              by[place_node[p]] == e)
            arm.boards.push_back ({ p, metres[place_node[p]] });
        if (arm.boards.empty ())
          continue;
        std::ranges::sort (arm.boards, {}, &Fingerpost::Board::metres);
        if (arm.boards.size () > 2)
          arm.boards.resize (2);
        post.arms.push_back (std::move (arm));
      }
      if (post.arms.size () < 2)
        continue;

      // The post stands in the widest gap between the ways, off the tread.
      std::vector<float> angles;
      for (const std::size_t e : ways) {
        const PostEdge& edge = edges[e];
        angles.push_back (edge.a == n ? std::atan2 (edge.a_dz, edge.a_dx)
                                      : std::atan2 (edge.b_dz, edge.b_dx));
      }
      std::ranges::sort (angles);
      float widest = -1.0f, middle = 0.0f;
      for (std::size_t i = 0; i < angles.size (); ++i) {
        const float a = angles[i];
        const float b = i + 1 < angles.size ()
                          ? angles[i + 1]
                          : angles.front () + 2.0f * std::numbers::pi_v<float>;
        if (b - a > widest) {
          widest = b - a;
          middle = 0.5f * (a + b);
        }
      }
      // Clear of a cart road's carriageway and shoulder, and of its
      // batter, unless the gap is narrow and the way beside it close.
      const float off = 6.5f / std::max (0.6f, std::sin (0.5f * widest));
      post.x_m = post_wrap (here.x + std::min (off, 11.0f) * std::cos (middle),
                            period_x);
      post.z_m = post_wrap (here.z + std::min (off, 11.0f) * std::sin (middle),
                            period_z);
      posts.push_back (std::move (post));
    }
    return posts;
  }

  namespace {
    // Each board's face in the texture, and on the post, in metres.
    constexpr int k_cell_w = 512, k_cell_h = 80, k_cells_across = 2;
    constexpr float k_board_length = 1.2f;
    constexpr float k_board_height = 0.19f;
    constexpr float k_board_tip = 0.15f;
    constexpr float k_board_thickness = 0.026f;
    constexpr float k_board_step = 0.23f;
    constexpr float k_post_half = 0.06f;
    constexpr float k_post_top = 2.45f;
    constexpr int k_most_boards = 6;

    struct BoardPaint {
      DisplayColor board, letters, edge;
    };

    BoardPaint board_paint (const Fingerpost::Arm& arm) {
      if (arm.abandoned)
        return { { 0.55f, 0.54f, 0.50f },
                 { 0.33f, 0.32f, 0.30f },
                 { 0.42f, 0.40f, 0.37f } };
      if (arm.kind == terrain::Road::Kind::Footpath)
        return { { 0.36f, 0.22f, 0.12f },
                 { 0.95f, 0.90f, 0.78f },
                 { 0.28f, 0.17f, 0.09f } };
      return { { 0.93f, 0.93f, 0.90f },
               { 0.06f, 0.06f, 0.06f },
               { 0.12f, 0.12f, 0.12f } };
    }

    // One board's lettering, rasterized from the face's Slug outlines.
    void letter_board (std::vector<std::uint8_t>& rgba,
                       int atlas_w,
                       int cell,
                       const render::Font& font,
                       const std::string& name,
                       const std::string& distance,
                       const BoardPaint& colours) {
      const int x0 = (cell % k_cells_across) * k_cell_w;
      const int y0 = (cell / k_cells_across) * k_cell_h;
      std::vector<float> ink (static_cast<std::size_t> (k_cell_w) * k_cell_h);
      // The text keeps clear of the tip at either end, since the back of
      // the board reads the other way along it.
      const float pad = 70.0f;
      render::TextStyle style;
      style.size = 50.0f;
      const float room = k_cell_w - 2.0f * pad;
      const float wanted =
        font.measure (name, style) + 24.0f + font.measure (distance, style);
      if (wanted > room)
        style.size *= room / wanted;
      const float baseline =
        0.5f * (k_cell_h + font.cap_height (style.size)) + 0.5f;
      render::TextList text;
      font.draw (text, pad, baseline, name, style);
      font.draw (text,
                 k_cell_w - pad,
                 baseline,
                 distance,
                 style,
                 render::TextAlign::Right);
      for (const render::GlyphQuad& post_quad : text.quads ()) {
        render::SlugGlyph glyph;
        glyph.min_x = post_quad.bounds[0];
        glyph.min_y = post_quad.bounds[1];
        glyph.max_x = post_quad.bounds[2];
        glyph.max_y = post_quad.bounds[3];
        glyph.band_offset = post_quad.glyph[0];
        glyph.horizontal_bands =
          static_cast<std::uint16_t> (post_quad.glyph[1]);
        glyph.vertical_bands = static_cast<std::uint16_t> (post_quad.glyph[2]);
        const float size = post_quad.axis_x[0];
        const float ox = post_quad.origin[0], oy = post_quad.origin[1];
        const int px0 = std::max (
          0, static_cast<int> (std::floor (ox + glyph.min_x * size)) - 1);
        const int px1 = std::min (
          k_cell_w, static_cast<int> (std::ceil (ox + glyph.max_x * size)) + 1);
        const int py0 = std::max (
          0, static_cast<int> (std::floor (oy - glyph.max_y * size)) - 1);
        const int py1 = std::min (
          k_cell_h, static_cast<int> (std::ceil (oy - glyph.min_y * size)) + 1);
        for (int py = py0; py < py1; ++py)
          for (int px = px0; px < px1; ++px) {
            const render::OutlinePoint at {
              (static_cast<float> (px) + 0.5f - ox) / size,
              (oy - static_cast<float> (py) - 0.5f) / size
            };
            float& cover = ink[static_cast<std::size_t> (py) * k_cell_w + px];
            cover =
              std::min (1.0f,
                        cover + render::slug_coverage (
                                  font.data (), glyph, at, { size, size }));
          }
      }
      for (int py = 0; py < k_cell_h; ++py)
        for (int px = 0; px < k_cell_w; ++px) {
          // A painted border a little in from the edge.
          const int edge =
            std::min ({ px, k_cell_w - 1 - px, py, k_cell_h - 1 - py });
          float cover = ink[static_cast<std::size_t> (py) * k_cell_w + px];
          if (edge >= 4 && edge < 7)
            cover = std::max (cover, 0.8f);
          const DisplayColor c {
            colours.board.red +
              (colours.letters.red - colours.board.red) * cover,
            colours.board.green +
              (colours.letters.green - colours.board.green) * cover,
            colours.board.blue +
              (colours.letters.blue - colours.board.blue) * cover
          };
          std::uint8_t* out =
            &rgba[(static_cast<std::size_t> (y0 + py) * atlas_w + x0 + px) * 4];
          out[0] = static_cast<std::uint8_t> (std::lround (c.red * 255.0f));
          out[1] = static_cast<std::uint8_t> (std::lround (c.green * 255.0f));
          out[2] = static_cast<std::uint8_t> (std::lround (c.blue * 255.0f));
          out[3] = 255;
        }
    }

    void post_facet (render::DrawList& dl,
                     const Vec3& normal,
                     const Vec3& a,
                     const Vec3& b,
                     const Vec3& c) {
      dl.normal (normal);
      dl.vertex (a);
      dl.vertex (b);
      dl.vertex (c);
    }

    void post_quad (render::DrawList& dl,
                    const Vec3& a,
                    const Vec3& b,
                    const Vec3& c,
                    const Vec3& d) {
      Vec3 n = cross (b - a, c - a);
      if (length2 (n) < 1e-12f)
        return;
      n = normalized (n);
      post_facet (dl, n, a, b, c);
      post_facet (dl, n, a, c, d);
    }
  }

  FingerpostSigns
  make_fingerposts (render::Renderer& renderer,
                    const render::Font& font,
                    std::span<const Fingerpost> posts,
                    std::span<const std::string> names,
                    const std::function<float (float x, float z)>& ground) {
    FingerpostSigns signs;
    // The boards, one to each place an arm leads to, topmost first.
    struct Placed {
      const Fingerpost* post;
      const Fingerpost::Arm* arm;
      const Fingerpost::Board* board;
      int row;
    };
    std::vector<Placed> placed;
    for (const Fingerpost& post : posts) {
      int row = 0;
      // Every arm's nearest place first, then the second where there is
      // room on the post.
      for (std::size_t rank = 0; rank < 2; ++rank)
        for (const Fingerpost::Arm& arm : post.arms)
          if (rank < arm.boards.size () && row < k_most_boards &&
              arm.boards[rank].place < names.size ())
            placed.push_back ({ &post, &arm, &arm.boards[rank], row++ });
    }
    if (placed.empty ())
      return signs;

    const int rows =
      static_cast<int> ((placed.size () + k_cells_across - 1) / k_cells_across);
    int atlas_h = k_cell_h;
    while (atlas_h < rows * k_cell_h)
      atlas_h *= 2;
    const int atlas_w = k_cell_w * k_cells_across;
    std::vector<std::uint8_t> rgba (
      static_cast<std::size_t> (atlas_w) * atlas_h * 4, 255);
    for (std::size_t i = 0; i < placed.size (); ++i)
      letter_board (rgba,
                    atlas_w,
                    static_cast<int> (i),
                    font,
                    names[placed[i].board->place],
                    fingerpost_distance (placed[i].board->metres),
                    board_paint (*placed[i].arm));
    render::TextureDesc desc;
    desc.width = atlas_w;
    desc.height = atlas_h;
    desc.format = render::TextureFormat::RGBA8;
    desc.filter = render::TextureFilter::Mipmap;
    desc.wrap = render::TextureWrap::Clamp;
    desc.max_anisotropy = 8.0f;
    signs.boards = renderer.create_texture (desc, rgba.data ());

    render::DrawList& dl = signs.mesh;
    dl.state (render::DrawState ());
    dl.lit (true);
    dl.fogged (true);
    const Vec3 up (0, 1, 0);

    // The faces, lettered.
    dl.set_texture (signs.boards.get ());
    dl.color (1.0f, 1.0f, 1.0f, 1.0f);
    dl.begin (render::Prim::Triangles);
    for (std::size_t i = 0; i < placed.size (); ++i) {
      const Placed& p = placed[i];
      const Vec3 foot (
        p.post->x_m, ground (p.post->x_m, p.post->z_m), p.post->z_m);
      const Vec3 along (p.arm->direction_x, 0.0f, p.arm->direction_z);
      // To the left of the way the arm points: the face whose reader
      // sees the tip on the right.
      const Vec3 face (-along[2], 0.0f, along[0]);
      const Vec3 middle =
        foot +
        up * (k_post_top - 0.25f - k_board_step * static_cast<float> (p.row));
      const float u0 = static_cast<float> ((i % k_cells_across) * k_cell_w) /
                       static_cast<float> (atlas_w);
      const float u1 = u0 + static_cast<float> (k_cell_w) / atlas_w;
      const float v0 = static_cast<float> ((i / k_cells_across) * k_cell_h) /
                       static_cast<float> (atlas_h);
      const float v1 = v0 + static_cast<float> (k_cell_h) / atlas_h;
      for (const float side : { 1.0f, -1.0f }) {
        const Vec3 n = face * side;
        const Vec3 base = middle + n * (0.5f * k_board_thickness);
        // A point of the face, a metres out from the post and b up from
        // the board's middle line, with its place in the texture.
        const auto at = [&] (float a, float b) {
          const float t = a / k_board_length;
          const float s = side > 0.0f ? t : 1.0f - t;
          dl.uv (u0 + (u1 - u0) * s,
                 v0 + (v1 - v0) * (0.5f - b / k_board_height));
          return base + along * (k_post_half + a) + up * b;
        };
        const float h = 0.5f * k_board_height;
        const float shoulder = k_board_length - k_board_tip;
        dl.normal (n);
        // Wound to face out on either side.
        const auto triangle =
          [&] (float a0, float b0, float a1, float b1, float a2, float b2) {
            if (side > 0.0f) {
              dl.vertex (at (a0, b0));
              dl.vertex (at (a1, b1));
              dl.vertex (at (a2, b2));
            } else {
              dl.vertex (at (a0, b0));
              dl.vertex (at (a2, b2));
              dl.vertex (at (a1, b1));
            }
          };
        triangle (0, -h, shoulder, -h, shoulder, h);
        triangle (0, -h, shoulder, h, 0, h);
        triangle (shoulder, -h, k_board_length, 0, shoulder, h);
      }
    }
    dl.end ();
    dl.set_texture (nullptr);

    // The boards' edges, painted as their faces.
    dl.begin (render::Prim::Triangles);
    for (const Placed& p : placed) {
      const Vec3 foot (
        p.post->x_m, ground (p.post->x_m, p.post->z_m), p.post->z_m);
      const Vec3 along (p.arm->direction_x, 0.0f, p.arm->direction_z);
      const Vec3 face (-along[2], 0.0f, along[0]);
      const Vec3 middle =
        foot +
        up * (k_post_top - 0.25f - k_board_step * static_cast<float> (p.row));
      const float h = 0.5f * k_board_height;
      const float shoulder = k_board_length - k_board_tip;
      const Vec3 outline[5] = {
        middle + along * k_post_half - up * h,
        middle + along * (k_post_half + shoulder) - up * h,
        middle + along * (k_post_half + k_board_length),
        middle + along * (k_post_half + shoulder) + up * h,
        middle + along * k_post_half + up * h,
      };
      dl.color (board_paint (*p.arm).edge, 1.0f);
      const Vec3 half = face * (0.5f * k_board_thickness);
      for (int k = 0; k < 5; ++k) {
        const Vec3& a = outline[k];
        const Vec3& b = outline[(k + 1) % 5];
        post_quad (dl, a - half, b - half, b + half, a + half);
      }
    }

    // The posts, weathered square timber with a little roof over them.
    dl.color (0.43f, 0.37f, 0.29f, 1.0f);
    for (const Fingerpost& post : posts) {
      const Vec3 foot (post.x_m, ground (post.x_m, post.z_m) - 0.3f, post.z_m);
      // Square to the first arm, as it would have been set.
      const Vec3 e = post.arms.empty () ? Vec3 (1, 0, 0)
                                        : Vec3 (post.arms.front ().direction_x,
                                                0.0f,
                                                post.arms.front ().direction_z);
      const Vec3 f (-e[2], 0.0f, e[0]);
      const float top = k_post_top + 0.3f;
      const Vec3 corner[4] = {
        (e + f) * k_post_half,
        (f - e) * k_post_half,
        (-e - f) * k_post_half,
        (e - f) * k_post_half,
      };
      for (int k = 0; k < 4; ++k) {
        const Vec3& a = corner[k];
        const Vec3& b = corner[(k + 1) % 4];
        post_quad (
          dl, foot + b, foot + a, foot + a + up * top, foot + b + up * top);
      }
      const Vec3 peak = foot + up * (top + 0.07f);
      for (int k = 0; k < 4; ++k) {
        const Vec3 a = foot + up * top + corner[k] * 1.35f;
        const Vec3 b = foot + up * top + corner[(k + 1) % 4] * 1.35f;
        Vec3 n = cross (b - peak, a - peak);
        if (length2 (n) > 1e-12f)
          post_facet (dl, normalized (n), peak, b, a);
      }
    }
    dl.end ();
    return signs;
  }

  std::string fingerpost_distance (float metres) {
    if (metres < 1000.0f)
      return std::to_string (
               static_cast<int> (std::lround (metres / 10.0f) * 10)) +
             " m";
    const long tenths = std::lround (metres / 100.0f);
    return std::to_string (tenths / 10) + "," + std::to_string (tenths % 10) +
           " km";
  }
}
