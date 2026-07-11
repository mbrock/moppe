#include <moppe/game/water_capture.hh>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace moppe::game {
  namespace {
    int minimum_image_delta (int delta, int period) {
      if (delta > period / 2)
        delta -= period;
      else if (delta < -period / 2)
        delta += period;
      return delta;
    }

    Vector3D cell_position (std::uint32_t cell,
                            const map::HeightMap& map,
                            const terrain::FloodField& flood,
                            const terrain::LakeCensus& census,
                            const terrain::DrainageGraph& drainage) {
      const std::size_t width = drainage.width ();
      const int x = static_cast<int> (cell % width);
      const int z = static_cast<int> (cell / width);
      const Vector3D scale = map.scale ();
      const bool water =
        census.body[cell] != terrain::LakeCensus::dry || flood.ocean[cell];
      const float y = water ? flood.water_level.values ()[cell] * scale.y
                            : map.get (x, z) * scale.y;
      return Vector3D (x * scale.x, y, z * scale.z);
    }

    Vector3D cell_direction (std::uint32_t from,
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
        dx = minimum_image_delta (dx, width);
        dz = minimum_image_delta (dz, height);
      }
      Vector3D result (dx * map.scale ().x, 0.0f, dz * map.scale ().z);
      if (result.length2 () < 1e-6f)
        result = Vector3D (0, 0, 1);
      return result.normalized ();
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
          reach.downstream_area_m2 *
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
        consider (
          best, cell, from, drainage.receiver[cell], reach.downstream_area_m2);
      }
      return best;
    }

    Candidate choose_waterfall (const terrain::RiverNetwork& rivers) {
      Candidate best;
      for (const terrain::Waterfall& fall : rivers.waterfalls) {
        const float score =
          fall.drop_m * std::sqrt (std::max (1.0f, fall.contributing_area_m2));
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
        if (!selected || body.area_m2 > selected->area_m2 ||
            (body.area_m2 == selected->area_m2 && body.id < selected->id))
          selected = &body;
      }
      if (!selected)
        for (const terrain::WaterBody& body : census.bodies)
          if (!selected || body.area_m2 > selected->area_m2)
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
      best.score = selected->area_m2;
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

    Vector3D flow =
      best.to != terrain::RiverReach::no_id &&
          best.to != terrain::WaterBody::no_cell && best.to != best.cell
        ? cell_direction (best.cell, best.to, map, drainage)
      : best.from != terrain::RiverReach::no_id && best.from != best.cell
        ? cell_direction (best.from, best.cell, map, drainage)
        : Vector3D (0, 0, 1);
    const Vector3D side (-flow.z, 0, flow.x);
    Vector3D target = cell_position (best.cell, map, flood, census, drainage);
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
    target.y += shot == WaterShot::Lake ? 2.0f : 0.7f;
    Vector3D eye =
      target - flow * back + side * sideways + Vector3D (0, height, 0);
    eye.y = std::max (eye.y, map.interpolated_height (eye.x, eye.z) + 4.0f);
    return WaterInspection { .kind = shot,
                             .cell = best.cell,
                             .score = best.score,
                             .eye = eye,
                             .target = target };
  }
}
