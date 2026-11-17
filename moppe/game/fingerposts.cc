#include <moppe/game/fingerposts.hh>
#include <moppe/terrain/torus.hh>

#include <moppe/render/renderer.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <queue>
#include <ranges>
#include <utility>

namespace moppe::game {
  namespace {
    constexpr meters_t k_infinite =
      std::numeric_limits<float>::infinity () * u::m;
    // How near a way's end must come to another way to join it (the
    // smoothed lines stray some way from the lattice node the planner
    // joined them at, past the 30 m roads.cc snaps them back by), and how
    // near other cuts must be to be one node.
    constexpr meters_t k_join = 40.0f * u::m;
    constexpr meters_t k_node = 10.0f * u::m;
    // How near a place must lie to a way to stand on it.
    constexpr meters_t k_place = 25.0f * u::m;
    // No post nearer a place than this: the place is its own landmark.
    constexpr meters_t k_clear_of_place = 60.0f * u::m;
    // Nor nearer another post.
    constexpr meters_t k_clear_of_post = 30.0f * u::m;

    // Into the world's one copy, lane by lane (not torus.hh's wrap, which
    // also sends a point that rounds up to the period back to zero).
    plan_position_t post_wrap (const plan_position_t& at,
                               const plan_extent_t& period) {
      const Vec2& p = plan_value (at);
      const Vec2& n = plan_value (period);
      return plan_position (p.x () - n.x () * std::floor (p.x () / n.x ()),
                            p.z () - n.z () * std::floor (p.z () / n.z ()));
    }

    // How far a plan step reaches, as std::hypot measures it (which
    // rounds otherwise than cr::hypot).
    meters_t reach_of (const plan_displacement_t& step) {
      const Vec2& d = plan_value (step);
      return std::hypot (d.x (), d.z ()) * u::m;
    }

    struct PostLine {
      std::vector<plan_position_t> points;
      // How far along from the first point.
      std::vector<meters_t> along;
    };

    struct PostCut {
      std::size_t road = 0;
      // A fractional point index.
      float at = 0.0f;
      plan_position_t point {};
    };

    struct PostEdge {
      std::size_t a = 0, b = 0;
      meters_t length {};
      std::size_t road = 0;
      // Away from each end.
      plan_direction_t a_way = plan_unit_direction (Vec2 (1.0f, 0.0f));
      plan_direction_t b_way = plan_unit_direction (Vec2 (1.0f, 0.0f));
    };
  }

  std::vector<Fingerpost> plan_fingerposts (const terrain::RoadNetwork& network,
                                            plan_extent_t period) {
    const auto distance = [&] (const plan_position_t& a,
                               const plan_position_t& b) {
      return reach_of (terrain::torus_offset (b, a, period));
    };

    // Every way as a line within one period, read point by point.
    std::vector<PostLine> lines (network.roads.size ());
    for (std::size_t r = 0; r < network.roads.size (); ++r) {
      PostLine& line = lines[r];
      meters_t along = meters_t::zero ();
      for (std::size_t i = 0; i < network.roads[r].points.size (); ++i) {
        const plan_position_t& p = network.roads[r].points[i];
        if (i > 0)
          along += reach_of (p - network.roads[r].points[i - 1]);
        line.points.push_back (post_wrap (p, period));
        line.along.push_back (along);
      }
    }
    const auto point_at = [&] (const PostLine& line, float at) {
      const std::size_t i =
        std::min (static_cast<std::size_t> (at), line.points.size () - 2);
      const float t = at - static_cast<float> (i);
      const Vec2& p = plan_value (line.points[i]);
      const Vec2 d = plan_value (
        terrain::torus_offset (line.points[i], line.points[i + 1], period));
      return post_wrap (
        plan_position (p.x () + t * d.x (), p.z () + t * d.z ()), period);
    };
    const auto metres_at = [&] (const PostLine& line, float at) {
      const std::size_t i =
        std::min (static_cast<std::size_t> (at), line.points.size () - 2);
      const float t = at - static_cast<float> (i);
      return line.along[i] + t * (line.along[i + 1] - line.along[i]);
    };
    // The nearest point of a line to a point: its fractional index and
    // how far off it is.
    const auto nearest = [&] (const PostLine& line, const plan_position_t& at) {
      std::pair<float, meters_t> best { 0.0f, k_infinite };
      for (std::size_t i = 0; i + 1 < line.points.size (); ++i) {
        const Vec2 a =
          plan_value (terrain::torus_offset (line.points[i], at, period));
        const Vec2 s = plan_value (
          terrain::torus_offset (line.points[i], line.points[i + 1], period));
        const float length2 = s.x () * s.x () + s.z () * s.z ();
        const float t =
          length2 > 0.0f
            ? std::clamp (
                (a.x () * s.x () + a.z () * s.z ()) / length2, 0.0f, 1.0f)
            : 0.0f;
        const meters_t off =
          std::hypot (a.x () - t * s.x (), a.z () - t * s.z ()) * u::m;
        if (off < best.second)
          best = { static_cast<float> (i) + t, off };
      }
      return best;
    };

    // Where the ways are cut: at their ends, where another's end meets
    // them, and where a place stands on them.
    std::vector<PostCut> cuts;
    const auto cut = [&] (std::size_t road, float at) {
      cuts.push_back ({ road, at, point_at (lines[road], at) });
      return cuts.size () - 1;
    };
    std::vector<std::array<std::size_t, 2>> ends (lines.size ());
    for (std::size_t r = 0; r < lines.size (); ++r) {
      if (lines[r].points.size () < 2)
        continue;
      ends[r][0] = cut (r, 0.0f);
      ends[r][1] = cut (r, static_cast<float> (lines[r].points.size () - 1));
    }
    // Each join with the end that made it, which are one node however
    // far apart.
    std::vector<std::pair<std::size_t, std::size_t>> joins;
    for (std::size_t r = 0; r < lines.size (); ++r) {
      if (lines[r].points.size () < 2)
        continue;
      for (const bool back : { false, true }) {
        const plan_position_t end =
          back ? lines[r].points.back () : lines[r].points.front ();
        for (std::size_t s = 0; s < lines.size (); ++s) {
          if (s == r || lines[s].points.size () < 2)
            continue;
          const auto [at, off] = nearest (lines[s], end);
          if (off < k_join)
            joins.emplace_back (cut (s, at), ends[r][back ? 1 : 0]);
        }
      }
    }
    std::vector<std::size_t> place_cut (network.places.size (),
                                        static_cast<std::size_t> (-1));
    for (std::size_t p = 0; p < network.places.size (); ++p) {
      const plan_position_t place = post_wrap (network.places[p].at, period);
      std::size_t best_road = 0;
      std::pair<float, meters_t> best { 0.0f, k_infinite };
      for (std::size_t r = 0; r < lines.size (); ++r) {
        if (lines[r].points.size () < 2)
          continue;
        const auto found = nearest (lines[r], place);
        if (found.second < best.second) {
          best = found;
          best_road = r;
        }
      }
      if (best.second < k_place)
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
        if (distance (cuts[i].point, cuts[j].point) < k_node)
          parent[root (j)] = root (i);
    for (const auto& [join, end] : joins)
      parent[root (end)] = root (join);
    std::vector<std::size_t> node_of (cuts.size ());
    std::vector<std::size_t> node_cut;
    {
      std::vector<std::size_t> numbered (cuts.size (),
                                         static_cast<std::size_t> (-1));
      for (std::size_t i = 0; i < cuts.size (); ++i) {
        const std::size_t r = root (i);
        if (numbered[r] == static_cast<std::size_t> (-1)) {
          numbered[r] = node_cut.size ();
          node_cut.push_back (i);
        }
        node_of[i] = numbered[r];
        // The node stands on the way that runs through it, if one does,
        // rather than at the end of one that meets it.
        const PostCut& c = cuts[i];
        if (c.at > 0.0f &&
            c.at < static_cast<float> (lines[c.road].points.size () - 1))
          node_cut[numbered[r]] = i;
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
        const auto heading = [&] (float start, float sign) {
          const meters_t s = metres_at (line, start);
          const meters_t reach = std::min (15.0f * u::m, 0.5f * edge.length);
          float at = start;
          while (sign > 0.0f ? at < to : at > from) {
            if (abs (metres_at (line, at) - s) >= reach)
              break;
            at += 0.25f * sign;
          }
          at = std::clamp (at, from, to);
          const Vec2 d = plan_value (terrain::torus_offset (
            point_at (line, start), point_at (line, at), period));
          const float n = std::hypot (d.x (), d.z ());
          return plan_unit_direction (n > 1e-4f ? Vec2 (d.x () / n, d.z () / n)
                                                : Vec2 (1.0f, 0.0f));
        };
        edge.a_way = heading (from, 1.0f);
        edge.b_way = heading (to, -1.0f);
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
        clear = clear && distance (here.point, post_wrap (place.at, period)) >=
                           k_clear_of_place;
      for (const Fingerpost& post : posts)
        clear =
          clear && distance (here.point, post.junction) >= k_clear_of_post;
      if (!clear)
        continue;

      // The shortest ways out from here, and which arm each leaves by.
      std::vector<meters_t> metres (nodes, k_infinite);
      std::vector<std::size_t> by (nodes, static_cast<std::size_t> (-1));
      using Entry = std::pair<meters_t, std::size_t>;
      std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
      metres[n] = meters_t::zero ();
      open.push ({ meters_t::zero (), n });
      while (!open.empty ()) {
        const auto [reached, node] = open.top ();
        open.pop ();
        if (reached > metres[node])
          continue;
        for (const std::size_t e : touching[node]) {
          const std::size_t next = edges[e].a == node ? edges[e].b : edges[e].a;
          const meters_t further = reached + edges[e].length;
          if (further < metres[next]) {
            metres[next] = further;
            by[next] = node == n ? e : by[node];
            open.push ({ further, next });
          }
        }
      }

      Fingerpost post;
      post.junction = here.point;
      for (const std::size_t e : ways) {
        Fingerpost::Arm arm;
        const PostEdge& edge = edges[e];
        arm.direction = edge.a == n ? edge.a_way : edge.b_way;
        arm.kind = network.roads[edge.road].kind;
        arm.abandoned = network.roads[edge.road].abandoned > age_t::zero ();
        for (std::size_t p = 0; p < network.places.size (); ++p)
          if (place_node[p] != static_cast<std::size_t> (-1) &&
              by[place_node[p]] == e)
            arm.boards.push_back ({ p, metres[place_node[p]] });
        if (arm.boards.empty ())
          continue;
        std::ranges::sort (arm.boards, {}, &Fingerpost::Board::distance);
        if (arm.boards.size () > 2)
          arm.boards.resize (2);
        post.arms.push_back (std::move (arm));
      }
      if (post.arms.size () < 2)
        continue;

      // The post stands in the widest gap between the ways, off the tread.
      std::vector<radians_t> angles;
      for (const std::size_t e : ways) {
        const PostEdge& edge = edges[e];
        const Vec2& way = plan_value (edge.a == n ? edge.a_way : edge.b_way);
        angles.push_back (std::atan2 (way.z (), way.x ()) * u::rad);
      }
      std::ranges::sort (angles);
      radians_t widest = -1.0f * u::rad, middle = radians_t::zero ();
      for (std::size_t i = 0; i < angles.size (); ++i) {
        const radians_t a = angles[i];
        const radians_t b =
          i + 1 < angles.size ()
            ? angles[i + 1]
            : angles.front () + 2.0f * std::numbers::pi_v<float> * u::rad;
        if (b - a > widest) {
          widest = b - a;
          middle = 0.5f * (a + b);
        }
      }
      // Clear of a cart road's carriageway and shoulder, and of its
      // batter, unless the gap is narrow and the way beside it close.
      const meters_t off =
        6.5f * u::m / std::max (0.6f, std::sin (0.5f * radians_value (widest)));
      const plan_direction_t out = plan_unit_direction (Vec2 (
        std::cos (radians_value (middle)), std::sin (radians_value (middle))));
      post.at = post_wrap (
        here.point + along (out, std::min (off, 11.0f * u::m)), period);
      posts.push_back (std::move (post));
    }
    return posts;
  }

  namespace {
    // The boards, in metres, after Sweden's vägvisare (Transportstyrelsen's
    // F5): an arrow, its tip a right-angled chevron, with a border inset
    // along its edge and letters whose capitals stand some 45% of its
    // height. Each is as long as its lettering wants.
    constexpr float k_board_height = 0.25f;
    constexpr float k_board_tip = 0.125f;
    constexpr float k_board_thickness = 0.026f;
    constexpr float k_board_step = 0.29f;
    constexpr float k_post_half = 0.06f;
    constexpr float k_post_top = 2.5f;
    constexpr int k_most_boards = 6;
    // The lettering's em, the room it keeps from either end (the back of
    // the board reads the other way along it, so both ends are a tip's
    // and a margin's), and between the name and the distance.
    constexpr float k_letter_em = 0.155f;
    constexpr float k_margin = 0.055f;
    constexpr float k_gap = 0.09f;
    constexpr float k_shortest_board = 0.8f;
    constexpr float k_longest_board = 2.0f;
    // The paint stands this far proud of the face, so that it is never
    // behind it.
    constexpr float k_paint_m = 0.002f;
    // The painted border's band, in from the board's edge.
    constexpr float k_border_outer_m = 0.011f;
    constexpr float k_border_inner_m = 0.024f;

    struct BoardPaint {
      DisplayColor board, letters, edge;
    };

    // Rural Sweden's colours: an enskild väg's signs are yellow with black
    // letters, a hiking trail's brown with white, and a board along a way
    // no longer kept has weathered to the grey of old wood.
    BoardPaint board_paint (const Fingerpost::Arm& arm) {
      if (arm.abandoned)
        return { { 0.55f, 0.54f, 0.50f },
                 { 0.30f, 0.29f, 0.27f },
                 { 0.42f, 0.40f, 0.37f } };
      if (arm.kind == terrain::Road::Kind::Footpath)
        return { { 0.36f, 0.20f, 0.10f },
                 { 0.97f, 0.95f, 0.90f },
                 { 0.26f, 0.15f, 0.08f } };
      return { { 0.98f, 0.78f, 0.06f },
               { 0.04f, 0.04f, 0.04f },
               { 0.12f, 0.12f, 0.12f } };
    }

    render::TextStyle letter_style (const DisplayColor& ink) {
      render::TextStyle style;
      style.size = k_letter_em;
      style.red = ink.red;
      style.green = ink.green;
      style.blue = ink.blue;
      return style;
    }

    // How long a board must be to carry its name and distance, to a
    // twentieth of a metre, so that boards share their borders' shapes.
    float board_length (const render::Font& font,
                        const std::string& name,
                        const std::string& distance) {
      const render::TextStyle style = letter_style ({});
      const float wanted = 2.0f * (k_board_tip + k_margin) +
                           font.measure (name, style) + k_gap +
                           font.measure (distance, style);
      return std::clamp (
        std::ceil (wanted * 20.0f) / 20.0f, k_shortest_board, k_longest_board);
    }

    // One face's lettering in its own plane, in millimetres along the
    // board from the end its reader starts at and down from its top (in
    // millimetres because Font::draw sets a line's start on a whole
    // pixel, here a fraction of one): the name, and the distance flush
    // right, shrunk only should a board at its longest still be short of
    // room.
    void letter_board (render::TextList& text,
                       const render::Font& font,
                       const std::string& name,
                       const std::string& distance,
                       const DisplayColor& ink,
                       float length) {
      render::TextStyle style = letter_style (ink);
      style.size *= 1000.0f;
      const float pad = 1000.0f * (k_board_tip + k_margin);
      const float room = 1000.0f * length - 2.0f * pad;
      const float wanted = font.measure (name, style) + 1000.0f * k_gap +
                           font.measure (distance, style);
      if (wanted > room)
        style.size *= room / wanted;
      length *= 1000.0f;
      const float baseline =
        0.5f * (1000.0f * k_board_height + font.cap_height (style.size));
      font.draw (text, pad, baseline, name, style);
      font.draw (text,
                 length - pad,
                 baseline,
                 distance,
                 style,
                 render::TextAlign::Right);
    }

    // A board's edge, counterclockwise from the bottom corner at the
    // post, in metres out along the arm and up from its middle line.
    std::array<render::OutlinePoint, 5> board_edge (float length) {
      const float h = 0.5f * k_board_height;
      const float shoulder = length - k_board_tip;
      return { {
        { 0.0f, -h },
        { shoulder, -h },
        { length, 0.0f },
        { shoulder, h },
        { 0.0f, h },
      } };
    }

    // The board's outline moved in by `inset`.
    render::OutlineContour board_outline (float length, float inset) {
      const std::array<render::OutlinePoint, 5> edge = board_edge (length);
      // Each side moved inward, to its left, and its ends where it meets
      // its neighbours.
      const auto side =
        [&] (int k, float& px, float& py, float& dx, float& dy) {
          const render::OutlinePoint a = edge[k], b = edge[(k + 1) % 5];
          const float span = std::hypot (b.x - a.x, b.y - a.y);
          dx = (b.x - a.x) / span;
          dy = (b.y - a.y) / span;
          px = a.x - dy * inset;
          py = a.y + dx * inset;
        };
      std::array<render::OutlinePoint, 5> corner;
      for (int k = 0; k < 5; ++k) {
        float ax, ay, adx, ady, bx, by, bdx, bdy;
        side ((k + 4) % 5, ax, ay, adx, ady);
        side (k, bx, by, bdx, bdy);
        const float t =
          ((bx - ax) * bdy - (by - ay) * bdx) / (adx * bdy - ady * bdx);
        corner[k] = { ax + adx * t, ay + ady * t };
      }
      render::OutlineContour contour;
      for (int k = 0; k < 5; ++k)
        contour.push_back (
          render::outline_line (corner[k], corner[(k + 1) % 5]));
      return contour;
    }

    // The painted border, a band round the board's edge, as one Slug
    // shape: the outline in, with a hole a little further in.
    render::SlugGlyph board_border (render::SlugAtlasBuilder& builder,
                                    float length) {
      render::OutlineContour hole;
      for (const render::OutlineCurve& c :
           board_outline (length, k_border_inner_m) | std::views::reverse)
        hole.push_back (render::outline_line (c.p3, c.p1));
      const render::OutlineContour contours[] = {
        board_outline (length, k_border_outer_m), hole
      };
      return builder.add (contours);
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

  FingerpostSigns make_fingerposts (render::Renderer& renderer,
                                    const render::Font& font,
                                    std::span<const Fingerpost> posts,
                                    std::span<const std::string> names,
                                    const terrain::ElevationField& ground) {
    FingerpostSigns signs;
    // The boards, one to each place an arm leads to, topmost first.
    struct Placed {
      const Fingerpost* post;
      const Fingerpost::Arm* arm;
      const Fingerpost::Board* board;
      int row;
      std::string name, distance;
      float length = 0.0f;
      Vec3 middle, along, face;
    };
    std::vector<Placed> placed;
    const Vec3 up (0, 1, 0);
    for (const Fingerpost& post : posts) {
      int row = 0;
      const position_t foot = terrain::at_elevation (post.at, ground (post.at));
      // Every arm's nearest place first, then the second where there is
      // room on the post.
      for (std::size_t rank = 0; rank < 2; ++rank)
        for (const Fingerpost::Arm& arm : post.arms) {
          if (rank >= arm.boards.size () || row >= k_most_boards ||
              arm.boards[rank].place >= names.size ())
            continue;
          Placed p { &post, &arm, &arm.boards[rank], row++ };
          p.name = names[p.board->place];
          p.distance = fingerpost_distance (p.board->distance);
          p.length = board_length (font, p.name, p.distance);
          p.along = lift (plan_value (arm.direction));
          // To the left of the way the arm points: the face whose reader
          // sees the tip on the right.
          p.face = Vec3 (-p.along[2], 0.0f, p.along[0]);
          p.middle = position_value (foot) +
                     up * (k_post_top - 0.2f -
                           k_board_step * static_cast<float> (p.row));
          placed.push_back (std::move (p));
        }
    }
    if (placed.empty ())
      return signs;

    render::DrawList& dl = signs.mesh;
    dl.state (render::DrawState ());
    dl.lit (true);
    dl.fogged (true);

    // The faces, in their boards' paint, and the edges round them.
    dl.begin (render::Prim::Triangles);
    for (const Placed& p : placed) {
      const BoardPaint paint = board_paint (*p.arm);
      const std::array<render::OutlinePoint, 5> edge = board_edge (p.length);
      // A point of the board, a metres out from the post and b up from
      // its middle line, on the side `side` of its middle plane.
      const auto at = [&] (const render::OutlinePoint& e, float side) {
        return p.middle + p.along * (k_post_half + e.x) + up * e.y +
               p.face * (side * 0.5f * k_board_thickness);
      };
      dl.color (paint.board, 1.0f);
      for (const float side : { 1.0f, -1.0f }) {
        dl.normal (p.face * side);
        // A fan from the corner at the post, wound to face out.
        for (int k = 1; k + 1 < 5; ++k) {
          dl.vertex (at (edge[0], side));
          dl.vertex (at (edge[side > 0.0f ? k : k + 1], side));
          dl.vertex (at (edge[side > 0.0f ? k + 1 : k], side));
        }
      }
      dl.color (paint.edge, 1.0f);
      for (int k = 0; k < 5; ++k) {
        const render::OutlinePoint& a = edge[k];
        const render::OutlinePoint& b = edge[(k + 1) % 5];
        post_quad (
          dl, at (a, -1.0f), at (b, -1.0f), at (b, 1.0f), at (a, 1.0f));
      }
    }
    dl.end ();

    // The lettering on each face, laid out in metres from the top corner
    // its reader starts at, and then the borders, so that each glyph set
    // is one run.
    for (const Placed& p : placed) {
      const DisplayColor ink = board_paint (*p.arm).letters;
      for (const float side : { 1.0f, -1.0f }) {
        const Vec3 right = p.along * side;
        const Vec3 surface =
          p.middle + p.face * (side * (0.5f * k_board_thickness + k_paint_m));
        const Vec3 corner =
          surface +
          p.along * (side > 0.0f ? k_post_half : k_post_half + p.length) +
          up * (0.5f * k_board_height);
        const std::size_t first = signs.lettering.quads ().size ();
        letter_board (signs.lettering, font, p.name, p.distance, ink, p.length);
        signs.lettering.place (corner, right * 0.001f, up * 0.001f, first);
      }
    }
    render::SlugAtlasBuilder shapes;
    std::map<int, render::SlugGlyph> borders;
    for (const Placed& p : placed) {
      const int key = static_cast<int> (std::lround (p.length * 20.0f));
      if (!borders.contains (key))
        borders[key] = board_border (shapes, p.length);
    }
    const render::GlyphSetPtr border_set =
      renderer.create_glyph_set (shapes.data ());
    for (const Placed& p : placed) {
      const DisplayColor ink = board_paint (*p.arm).letters;
      const render::SlugGlyph& border =
        borders.at (static_cast<int> (std::lround (p.length * 20.0f)));
      for (const float side : { 1.0f, -1.0f }) {
        // The outline's own frame on either face: out along the arm and
        // up, from the post.
        const Vec3 origin =
          p.middle + p.along * k_post_half +
          p.face * (side * (0.5f * k_board_thickness + k_paint_m));
        render::GlyphQuad quad {};
        for (int i = 0; i < 3; ++i) {
          quad.origin[i] = origin[i];
          quad.axis_x[i] = p.along[i];
          quad.axis_y[i] = up[i];
        }
        quad.origin[3] = 1.0f;
        quad.bounds[0] = border.min_x;
        quad.bounds[1] = border.min_y;
        quad.bounds[2] = border.max_x;
        quad.bounds[3] = border.max_y;
        quad.color[0] = ink.red;
        quad.color[1] = ink.green;
        quad.color[2] = ink.blue;
        quad.color[3] = 1.0f;
        quad.glyph[0] = border.band_offset;
        quad.glyph[1] = border.horizontal_bands;
        quad.glyph[2] = border.vertical_bands;
        signs.lettering.add (border_set, quad);
      }
    }

    // The posts, weathered square timber with a little roof over them.
    dl.begin (render::Prim::Triangles);
    dl.color (0.43f, 0.37f, 0.29f, 1.0f);
    for (const Fingerpost& post : posts) {
      // Sunk a little into the ground.
      const Vec3 foot = position_value (
        terrain::at_elevation (post.at, ground (post.at) - 0.3f * u::m));
      // Square to the first arm, as it would have been set.
      const Vec3 e = post.arms.empty ()
                       ? Vec3 (1, 0, 0)
                       : lift (plan_value (post.arms.front ().direction));
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

  std::string fingerpost_distance (meters_t distance) {
    const float metres = meters_value (distance);
    if (metres < 1000.0f)
      return std::to_string (
               static_cast<int> (std::lround (metres / 10.0f) * 10)) +
             " m";
    const long tenths = std::lround (metres / 100.0f);
    return std::to_string (tenths / 10) + "," + std::to_string (tenths % 10);
  }

  void RoadSigns::plan (const terrain::RoadNetwork& network,
                        plan_extent_t period) {
    m_posts = plan_fingerposts (network, period);
    std::cerr << "moppe: fingerposts: " << m_posts.size () << '\n';
    m_made = false;
    m_mesh.reset ();
    m_lettering.reset ();
  }

  void RoadSigns::draw (render::Renderer& r,
                        const render::Font* hud_font,
                        std::span<const std::string> names,
                        const terrain::ElevationField& ground) {
    if (!m_made && hud_font) {
      m_made = true;
      if (!m_font)
        m_font = render::Font::load (r, "fonts/Geomini-SemiBold.ttf");
      FingerpostSigns signs = make_fingerposts (
        r, m_font->ok () ? *m_font : *hud_font, m_posts, names, ground);
      m_mesh = signs.mesh.empty () ? nullptr : r.create_mesh (signs.mesh);
      m_lettering = signs.lettering.empty ()
                      ? nullptr
                      : r.create_world_text (signs.lettering);
    }
    if (m_mesh)
      r.draw_mesh (*m_mesh, Mat4 {}, 0);
    if (m_lettering)
      r.draw_world_text (*m_lettering);
  }

  void RoadSigns::cast_shadow (render::Renderer& r) const {
    if (m_mesh)
      r.draw_mesh (*m_mesh, Mat4 {}, 0);
  }
}
