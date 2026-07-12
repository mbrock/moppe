#include <moppe/game/water_capture.hh>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace moppe::game {
  namespace {
    int capture_minimum_image_delta (int delta, int period) {
      if (delta > period / 2)
        delta -= period;
      else if (delta < -period / 2)
        delta += period;
      return delta;
    }

    Vec3 cell_position (std::uint32_t cell,
                        const map::HeightMap& map,
                        const terrain::FloodField& flood,
                        const terrain::LakeCensus& census,
                        const terrain::DrainageGraph& drainage) {
      const std::size_t width = drainage.width ();
      const int x = static_cast<int> (cell % width);
      const int z = static_cast<int> (cell / width);
      const Vec3 scale = map.scale ();
      const bool water =
        census.body[cell] != terrain::LakeCensus::dry || flood.ocean[cell];
      const float y = water ? flood.water_level.values ()[cell] * scale[1]
                            : map.get (x, z) * scale[1];
      return Vec3 (x * scale[0], y, z * scale[2]);
    }

    Vec3 cell_direction (std::uint32_t from,
                         std::uint32_t to,
                         const map::HeightMap& map,
                         const terrain::DrainageGraph& drainage) {
      const int width = static_cast<int> (drainage.width ());
      const int height = static_cast<int> (drainage.height ());
      const int from_x = static_cast<int> (from % drainage.width ());
      const int from_z = static_cast<int> (from / drainage.width ());
      const int to_x = static_cast<int> (to % drainage.width ());
      const int to_z = static_cast<int> (to / drainage.width ());
      int dx = to_x - from_x;
      int dz = to_z - from_z;
      if (map.periodic ()) {
        dx = capture_minimum_image_delta (dx, width);
        dz = capture_minimum_image_delta (dz, height);
      }
      Vec3 result (dx * map.scale ()[0], 0.0f, dz * map.scale ()[2]);
      if (length2 (result) < 1e-6f)
        result = Vec3 (0, 0, 1);
      return normalized (result);
    }

    struct Candidate {
      std::uint32_t cell = terrain::RiverReach::no_id;
      std::uint32_t from = terrain::RiverReach::no_id;
      std::uint32_t to = terrain::RiverReach::no_id;
      float score = -std::numeric_limits<float>::infinity ();
    };

    void consider (Candidate& best,
                   std::uint32_t cell,
                   std::uint32_t from,
                   std::uint32_t to,
                   float score) {
      if (score > best.score || (score == best.score && cell < best.cell))
        best = { .cell = cell, .from = from, .to = to, .score = score };
    }

    Candidate choose_river (const terrain::DrainageGraph& drainage,
                            const terrain::RiverNetwork& rivers) {
      Candidate best;
      for (const terrain::RiverReach& reach : rivers.reaches) {
        if (reach.cells.empty ())
          continue;
        const std::size_t middle = reach.cells.size () / 2;
        const std::uint32_t cell = reach.cells[middle];
        const std::uint32_t next = drainage.receiver[cell];
        const std::uint32_t from = middle > 0 ? reach.cells[middle - 1] : cell;
        const float score =
          square_meters_value (reach.downstream_area) *
          std::sqrt (static_cast<float> (reach.cells.size ()));
        consider (best, cell, from, next, score);
      }
      return best;
    }

    Candidate choose_confluence (const terrain::DrainageGraph& drainage,
                                 const terrain::RiverNetwork& rivers) {
      std::vector<std::uint32_t> incoming (drainage.receiver.size (), 0);
      std::vector<std::uint32_t> upstream (drainage.receiver.size (),
                                           terrain::RiverReach::no_id);
      for (const terrain::RiverReach& reach : rivers.reaches) {
        if (reach.cells.empty () ||
            reach.downstream_reach == terrain::RiverReach::no_id)
          continue;
        const std::uint32_t from = reach.cells.back ();
        const std::uint32_t cell = drainage.receiver[from];
        ++incoming[cell];
        upstream[cell] = from;
      }
      Candidate best;
      for (std::uint32_t cell = 0; cell < incoming.size (); ++cell) {
        if (incoming[cell] < 2)
          continue;
        consider (best,
                  cell,
                  upstream[cell],
                  drainage.receiver[cell],
                  drainage.contributing_area.values ()[cell]);
      }
      return best;
    }

    Candidate choose_mouth (const terrain::DrainageGraph& drainage,
                            const terrain::RiverNetwork& rivers) {
      Candidate best;
      for (const terrain::RiverReach& reach : rivers.reaches) {
        if (reach.cells.empty () ||
            (reach.downstream_body == terrain::RiverReach::no_id &&
             !reach.downstream_ocean))
          continue;
        const std::uint32_t from = reach.cells.back ();
        const std::uint32_t cell = drainage.receiver[from];
        consider (best,
                  cell,
                  from,
                  drainage.receiver[cell],
                  square_meters_value (reach.downstream_area));
      }
      return best;
    }

    Candidate choose_waterfall (const terrain::RiverNetwork& rivers) {
      Candidate best;
      for (const terrain::Waterfall& fall : rivers.waterfalls) {
        const float score =
          meters_value (fall.drop) *
          std::sqrt (
            std::max (1.0f, square_meters_value (fall.contributing_area)));
        consider (best, fall.lip_cell, fall.lip_cell, fall.foot_cell, score);
      }
      return best;
    }

    Candidate choose_lake (const terrain::FloodField& flood,
                           const terrain::LakeCensus& census) {
      const terrain::WaterBody* selected = nullptr;
      for (const terrain::WaterBody& body : census.bodies) {
        if (body.classification == terrain::WaterBodyClass::Sea)
          continue;
        if (!selected || body.area > selected->area ||
            (body.area == selected->area && body.id < selected->id))
          selected = &body;
      }
      if (!selected)
        for (const terrain::WaterBody& body : census.bodies)
          if (!selected || body.area > selected->area)
            selected = &body;
      Candidate best;
      if (!selected)
        return best;
      for (std::uint32_t cell = 0; cell < census.body.size (); ++cell) {
        if (census.body[cell] != selected->id)
          continue;
        const float depth = flood.water_depth.values ()[cell];
        consider (best, cell, cell, cell, depth);
      }
      best.score = square_meters_value (selected->area);
      best.to = selected->outlet_cell != terrain::WaterBody::no_cell
                  ? selected->outlet_cell
                  : selected->spill_cell;
      return best;
    }
  }

  std::optional<WaterShot> parse_water_shot (std::string_view name) noexcept {
    if (name == "river")
      return WaterShot::River;
    if (name == "confluence")
      return WaterShot::Confluence;
    if (name == "mouth")
      return WaterShot::Mouth;
    if (name == "waterfall" || name == "fall")
      return WaterShot::Waterfall;
    if (name == "lake")
      return WaterShot::Lake;
    return std::nullopt;
  }

  std::string_view water_shot_name (WaterShot shot) noexcept {
    switch (shot) {
    case WaterShot::River:
      return "river";
    case WaterShot::Confluence:
      return "confluence";
    case WaterShot::Mouth:
      return "mouth";
    case WaterShot::Waterfall:
      return "waterfall";
    case WaterShot::Lake:
      return "lake";
    }
    return "water";
  }

  std::optional<WaterInspection>
  choose_water_inspection (WaterShot shot,
                           const map::HeightMap& map,
                           const terrain::FloodField& flood,
                           const terrain::LakeCensus& census,
                           const terrain::DrainageGraph& drainage,
                           const terrain::RiverNetwork& rivers) {
    Candidate best;
    switch (shot) {
    case WaterShot::River:
      best = choose_river (drainage, rivers);
      break;
    case WaterShot::Confluence:
      best = choose_confluence (drainage, rivers);
      break;
    case WaterShot::Mouth:
      best = choose_mouth (drainage, rivers);
      break;
    case WaterShot::Waterfall:
      best = choose_waterfall (rivers);
      break;
    case WaterShot::Lake:
      best = choose_lake (flood, census);
      break;
    }
    if (best.cell == terrain::RiverReach::no_id)
      return std::nullopt;

    Vec3 flow =
      best.to != terrain::RiverReach::no_id &&
          best.to != terrain::WaterBody::no_cell && best.to != best.cell
        ? cell_direction (best.cell, best.to, map, drainage)
      : best.from != terrain::RiverReach::no_id && best.from != best.cell
        ? cell_direction (best.from, best.cell, map, drainage)
        : Vec3 (0, 0, 1);
    const Vec3 side (-flow[2], 0, flow[0]);
    Vec3 target = cell_position (best.cell, map, flood, census, drainage);
    float back = 30.0f;
    float sideways = 22.0f;
    float height = 16.0f;
    if (shot == WaterShot::Waterfall) {
      target =
        (target + cell_position (best.to, map, flood, census, drainage)) * 0.5f;
      back = -70.0f;
      sideways = 45.0f;
      height = 28.0f;
    } else if (shot == WaterShot::Lake) {
      back = 110.0f;
      sideways = 75.0f;
      height = 75.0f;
    } else if (shot == WaterShot::Mouth) {
      back = 38.0f;
      sideways = 26.0f;
      height = 20.0f;
    }
    target[1] += shot == WaterShot::Lake ? 2.0f : 0.7f;
    Vec3 eye = target - flow * back + side * sideways + Vec3 (0, height, 0);
    eye[1] = std::max (eye[1], map.interpolated_height (eye[0], eye[2]) + 4.0f);
    return WaterInspection { .kind = shot,
                             .cell = best.cell,
                             .score = best.score,
                             .eye = eye,
                             .target = target };
  }
}
