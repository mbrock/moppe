#include <moppe/game/world_cache.hh>

#include <moppe/spatial/bundle_storage.hh>

#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace moppe::game {
  namespace {
    constexpr std::array<char, 12> CACHE_MAGIC { 'M', 'O', 'P', 'P', 'E', 'W',
                                                 'O', 'R', 'L', 'D', '0', '1' };
    // Version 6 invalidates worlds made by the elevation-only hillslope
    // diffusion pass; hillslope motion now participates in the solid ledger.
    constexpr std::uint32_t CACHE_VERSION = 6;

    std::string recipe_cache_identity (const terrain::WorldRecipe& recipe) {
      const Vec3 extent = extent_value (recipe.extent ());
      const auto bits = [] (float value) {
        return std::bit_cast<std::uint32_t> (value);
      };
      std::ostringstream name;
      name << terrain::profile_id (recipe.generation_profile ()) << '-'
           << recipe.resolution () << '-' << recipe.seed ().value << std::hex
           << "-extent-" << bits (extent[0]) << '-' << bits (extent[1]) << '-'
           << bits (extent[2]) << "-water-"
           << bits ((recipe.water_datum ()).numerical_value_in (u::m));
      name << "-uplift-"
           << bits (recipe.evolution ().uplift_duration.numerical_value_in (
                mp_units::astronomy::Julian_year));
      return name.str ();
    }

    spatial_extent_t forest_period (const terrain::TerrainDomain& domain) {
      return spatial_extent_in_metres (
        Vec3 (domain.period_x ().numerical_value_in (u::m),
              0,
              domain.period_z ().numerical_value_in (u::m)));
    }

    std::filesystem::path file_in (const std::string& directory,
                                   const char* name) {
      return std::filesystem::path (directory) / name;
    }

    template <typename Bundle>
    void save_bundle (const Bundle& bundle, const std::filesystem::path& path) {
      std::ofstream output (path, std::ios::binary);
      if (!output)
        throw std::runtime_error ("can't write world cache: " + path.string ());
      spatial::write_bundle (output, bundle);
      if (!output)
        throw std::runtime_error ("can't write world cache: " + path.string ());
    }

    template <typename Bundle>
    bool load_bundle (Bundle& bundle, const std::filesystem::path& path) {
      std::ifstream input (path, std::ios::binary);
      return input && spatial::load_bundle (input, bundle);
    }

    class BinaryWriter {
    public:
      explicit BinaryWriter (const std::filesystem::path& path)
          : m_output (path, std::ios::binary) {
        if (!m_output)
          throw std::runtime_error ("can't write world cache: " +
                                    path.string ());
      }

      template <typename Value>
        requires std::is_trivially_copyable_v<Value>
      void scalar (Value value) {
        m_output.write (reinterpret_cast<const char*> (&value), sizeof (value));
      }

      void bytes (const void* data, std::size_t size) {
        m_output.write (static_cast<const char*> (data),
                        static_cast<std::streamsize> (size));
      }

      template <typename Value>
        requires std::is_trivially_copyable_v<Value>
      void vector (std::span<const Value> values) {
        scalar (static_cast<std::uint64_t> (values.size ()));
        bytes (values.data (), values.size_bytes ());
      }

      void ids (std::span<const terrain::CellIndex> values) {
        scalar (static_cast<std::uint64_t> (values.size ()));
        for (terrain::CellIndex value : values)
          scalar (value.value);
      }

      void finish () {
        if (!m_output)
          throw std::runtime_error ("failed while writing world cache");
      }

    private:
      std::ofstream m_output;
    };

    class BinaryReader {
    public:
      explicit BinaryReader (const std::filesystem::path& path)
          : m_input (path, std::ios::binary) {}

      explicit operator bool () const {
        return static_cast<bool> (m_input);
      }

      template <typename Value>
        requires std::is_trivially_copyable_v<Value>
      bool scalar (Value& value) {
        m_input.read (reinterpret_cast<char*> (&value), sizeof (value));
        return static_cast<bool> (m_input);
      }

      bool bytes (void* data, std::size_t size) {
        m_input.read (static_cast<char*> (data),
                      static_cast<std::streamsize> (size));
        return static_cast<bool> (m_input);
      }

      template <typename Value>
        requires std::is_trivially_copyable_v<Value>
      bool vector (std::vector<Value>& values, std::size_t maximum) {
        std::uint64_t count = 0;
        if (!scalar (count) || count > maximum)
          return false;
        values.resize (static_cast<std::size_t> (count));
        return bytes (values.data (), values.size () * sizeof (Value));
      }

      bool ids (std::vector<terrain::CellIndex>& values, std::size_t maximum) {
        std::uint64_t count = 0;
        if (!scalar (count) || count > maximum)
          return false;
        values.resize (static_cast<std::size_t> (count));
        for (terrain::CellIndex& value : values)
          if (!scalar (value.value))
            return false;
        return true;
      }

    private:
      std::ifstream m_input;
    };

    float meters (meters_t value) {
      return value.numerical_value_in (u::m);
    }

    float square_meters (square_meters_t value) {
      return value.numerical_value_in (u::m * u::m);
    }

    float cubic_meters (cubic_meters_t value) {
      return value.numerical_value_in (u::m * u::m * u::m);
    }

    float slope_value (terrain::slope_t value) {
      return value.numerical_value_in (mp_units::one);
    }

    void write_recipe (BinaryWriter& output,
                       const terrain::WorldRecipe& recipe) {
      output.bytes (CACHE_MAGIC.data (), CACHE_MAGIC.size ());
      output.scalar (CACHE_VERSION);
      output.scalar (static_cast<std::uint32_t> (recipe.resolution ()));
      output.scalar (recipe.seed ().value);
      output.scalar (static_cast<std::uint32_t> (recipe.generation_profile ()));
      const Vec3 extent = extent_value (recipe.extent ());
      for (std::size_t component = 0; component < 3; ++component)
        output.scalar (extent[component]);
      output.scalar (meters (recipe.water_datum ()));
      output.scalar (recipe.evolution ().uplift_duration.numerical_value_in (
        mp_units::astronomy::Julian_year));
    }

    bool read_recipe (BinaryReader& input, const terrain::WorldRecipe& recipe) {
      std::array<char, CACHE_MAGIC.size ()> magic {};
      std::uint32_t version = 0;
      std::uint32_t resolution = 0;
      std::uint32_t seed = 0;
      std::uint32_t profile = 0;
      Vec3 extent;
      float water = 0.0f;
      float uplift_years = 0.0f;
      if (!input.bytes (magic.data (), magic.size ()) ||
          !input.scalar (version) || !input.scalar (resolution) ||
          !input.scalar (seed) || !input.scalar (profile))
        return false;
      for (std::size_t component = 0; component < 3; ++component)
        if (!input.scalar (extent[component]))
          return false;
      return input.scalar (water) && input.scalar (uplift_years) &&
             magic == CACHE_MAGIC && version == CACHE_VERSION &&
             resolution == static_cast<std::uint32_t> (recipe.resolution ()) &&
             seed == recipe.seed ().value &&
             profile ==
               static_cast<std::uint32_t> (recipe.generation_profile ()) &&
             extent == extent_value (recipe.extent ()) &&
             water == meters (recipe.water_datum ()) &&
             uplift_years ==
               recipe.evolution ().uplift_duration.numerical_value_in (
                 mp_units::astronomy::Julian_year);
    }

    void write_flood (BinaryWriter& output, const terrain::FloodField& flood) {
      output.scalar (flood.sea_level);
      output.scalar (static_cast<std::uint8_t> (flood.has_ocean));
      output.vector<std::uint8_t> (flood.ocean);
      output.ids (flood.spill_receiver);
    }

    bool read_flood (BinaryReader& input,
                     terrain::FloodField& flood,
                     std::size_t cells) {
      std::uint8_t has_ocean = 0;
      if (!input.scalar (flood.sea_level) || !input.scalar (has_ocean) ||
          !input.vector (flood.ocean, cells) ||
          !input.ids (flood.spill_receiver, cells) ||
          flood.ocean.size () != cells || flood.spill_receiver.size () != cells)
        return false;
      flood.has_ocean = has_ocean != 0;
      return true;
    }

    void write_lakes (BinaryWriter& output, const terrain::LakeCensus& lakes) {
      output.scalar (static_cast<std::uint64_t> (lakes.cell_count ()));
      for (terrain::WaterBodyId id : lakes.membership ().values ())
        output.scalar (id.value);
      output.scalar (
        static_cast<std::uint64_t> (lakes.water_bodies ().size ()));
      for (const terrain::WaterBody& body : lakes.water_bodies ()) {
        output.scalar (body.id.value);
        output.scalar (
          static_cast<std::uint64_t> (terrain::count_value (body.cells)));
        output.scalar (square_meters (body.area));
        output.scalar (meters (body.maximum_depth));
        output.scalar (meters (body.mean_depth));
        output.scalar (cubic_meters (body.volume));
        output.scalar (meters (body.surface_level));
        output.scalar (static_cast<std::uint8_t> (body.ocean_connected));
        output.scalar (body.outlet_cell.value);
        output.scalar (body.spill_cell.value);
        output.scalar (static_cast<std::uint32_t> (body.classification));
        output.scalar (meters (body.inradius));
        output.scalar (static_cast<std::uint8_t> (body.channel_like));
      }
    }

    bool read_lakes (BinaryReader& input,
                     terrain::LakeCensus& lakes,
                     std::size_t cells) {
      std::uint64_t membership_count = 0;
      if (!input.scalar (membership_count) || membership_count != cells)
        return false;
      std::vector<terrain::WaterBodyId> membership (cells);
      for (terrain::WaterBodyId& id : membership)
        if (!input.scalar (id.value))
          return false;
      std::uint64_t body_count = 0;
      if (!input.scalar (body_count) || body_count > cells)
        return false;
      std::vector<terrain::WaterBody> bodies;
      bodies.reserve (static_cast<std::size_t> (body_count));
      for (std::uint64_t index = 0; index < body_count; ++index) {
        std::uint32_t id = 0;
        std::uint64_t count = 0;
        float area = 0, maximum_depth = 0, mean_depth = 0, volume = 0;
        float surface_level = 0, inradius = 0;
        std::uint8_t ocean = 0, channel_like = 0;
        std::uint32_t outlet = 0, spill = 0, classification = 0;
        if (!input.scalar (id) || !input.scalar (count) ||
            !input.scalar (area) || !input.scalar (maximum_depth) ||
            !input.scalar (mean_depth) || !input.scalar (volume) ||
            !input.scalar (surface_level) || !input.scalar (ocean) ||
            !input.scalar (outlet) || !input.scalar (spill) ||
            !input.scalar (classification) || !input.scalar (inradius) ||
            !input.scalar (channel_like) || count > cells ||
            classification >
              static_cast<std::uint32_t> (terrain::WaterBodyClass::Sea))
          return false;
        bodies.push_back (
          { .id = terrain::WaterBodyId { id },
            .cells = terrain::cell_count (count),
            .area = area * u::m * u::m,
            .maximum_depth = maximum_depth * u::m,
            .mean_depth = mean_depth * u::m,
            .volume = volume * u::m * u::m * u::m,
            .surface_level = surface_level * u::m,
            .ocean_connected = ocean != 0,
            .outlet_cell = terrain::CellIndex { outlet },
            .spill_cell = terrain::CellIndex { spill },
            .classification =
              static_cast<terrain::WaterBodyClass> (classification),
            .inradius = inradius * u::m,
            .channel_like = channel_like != 0 });
      }
      lakes = terrain::LakeCensus (std::move (membership), std::move (bodies));
      return true;
    }

    void write_rivers (BinaryWriter& output,
                       const terrain::RiverNetwork& rivers) {
      output.scalar (square_meters (rivers.minimum_area));
      output.scalar (meters (rivers.waterfall_parameters.minimum_drop));
      output.scalar (slope_value (rivers.waterfall_parameters.minimum_slope));
      output.scalar (static_cast<std::uint64_t> (
        terrain::count_value (rivers.waterfall_parameters.separation_cells)));
      output.scalar (static_cast<std::uint64_t> (rivers.reaches.size ()));
      for (const terrain::RiverReach& reach : rivers.reaches) {
        output.scalar (reach.id.value);
        output.ids (reach.cells);
        output.scalar (reach.upstream_body.value);
        output.scalar (reach.downstream_body.value);
        output.scalar (static_cast<std::uint8_t> (reach.downstream_ocean));
        output.scalar (reach.downstream_reach.value);
        output.scalar (square_meters (reach.upstream_area));
        output.scalar (square_meters (reach.downstream_area));
        output.scalar (slope_value (reach.maximum_slope));
        output.scalar (reach.alignment.length.numerical_value_in (u::m));
        output.scalar (
          static_cast<std::uint64_t> (reach.alignment.points.size ()));
        for (const terrain::RiverAlignmentPoint& point :
             reach.alignment.points) {
          output.scalar (point.x_m);
          output.scalar (point.z_m);
          output.scalar (point.distance_m);
          output.scalar (point.flow_distance_m);
          output.scalar (point.contributing_area_m2);
          output.scalar (point.slope);
          output.scalar (point.waterfall);
          output.scalar (point.standing_water);
          output.scalar (point.water_level_m);
          output.scalar (point.pooled);
        }
      }
      output.scalar (static_cast<std::uint64_t> (rivers.waterfalls.size ()));
      for (const terrain::Waterfall& waterfall : rivers.waterfalls) {
        output.scalar (waterfall.reach_id.value);
        output.scalar (waterfall.lip_cell.value);
        output.scalar (waterfall.foot_cell.value);
        output.scalar (meters (waterfall.drop));
        output.scalar (meters (waterfall.horizontal_distance));
        output.scalar (slope_value (waterfall.slope));
        output.scalar (square_meters (waterfall.contributing_area));
      }
      output.vector<std::uint8_t> (rivers.body_traversed);
    }

    bool read_rivers (BinaryReader& input,
                      terrain::RiverNetwork& rivers,
                      std::size_t cells,
                      std::size_t bodies) {
      float minimum_area = 0, minimum_drop = 0, minimum_slope = 0;
      std::uint64_t separation = 0, reach_count = 0;
      if (!input.scalar (minimum_area) || !input.scalar (minimum_drop) ||
          !input.scalar (minimum_slope) || !input.scalar (separation) ||
          !input.scalar (reach_count) || reach_count > cells)
        return false;
      rivers.minimum_area = minimum_area * u::m * u::m;
      rivers.waterfall_parameters.minimum_drop = minimum_drop * u::m;
      rivers.waterfall_parameters.minimum_slope =
        minimum_slope * terrain::terrain_slope[mp_units::one];
      rivers.waterfall_parameters.separation_cells =
        terrain::separation_cell_count (separation);
      rivers.reaches.reserve (static_cast<std::size_t> (reach_count));
      for (std::uint64_t index = 0; index < reach_count; ++index) {
        terrain::RiverReach reach;
        std::uint8_t downstream_ocean = 0;
        float upstream_area = 0, downstream_area = 0, maximum_slope = 0;
        double alignment_length = 0;
        std::uint64_t point_count = 0;
        if (!input.scalar (reach.id.value) || !input.ids (reach.cells, cells) ||
            !input.scalar (reach.upstream_body.value) ||
            !input.scalar (reach.downstream_body.value) ||
            !input.scalar (downstream_ocean) ||
            !input.scalar (reach.downstream_reach.value) ||
            !input.scalar (upstream_area) || !input.scalar (downstream_area) ||
            !input.scalar (maximum_slope) || !input.scalar (alignment_length) ||
            !input.scalar (point_count) || point_count > cells * 8)
          return false;
        reach.downstream_ocean = downstream_ocean != 0;
        reach.upstream_area = upstream_area * u::m * u::m;
        reach.downstream_area = downstream_area * u::m * u::m;
        reach.maximum_slope =
          maximum_slope * terrain::terrain_slope[mp_units::one];
        reach.alignment.length = alignment_length * u::m;
        reach.alignment.points.resize (static_cast<std::size_t> (point_count));
        for (terrain::RiverAlignmentPoint& point : reach.alignment.points)
          if (!input.scalar (point.x_m) || !input.scalar (point.z_m) ||
              !input.scalar (point.distance_m) ||
              !input.scalar (point.flow_distance_m) ||
              !input.scalar (point.contributing_area_m2) ||
              !input.scalar (point.slope) || !input.scalar (point.waterfall) ||
              !input.scalar (point.standing_water) ||
              !input.scalar (point.water_level_m) ||
              !input.scalar (point.pooled))
            return false;
        rivers.reaches.push_back (std::move (reach));
      }
      std::uint64_t waterfall_count = 0;
      if (!input.scalar (waterfall_count) || waterfall_count > cells)
        return false;
      rivers.waterfalls.resize (static_cast<std::size_t> (waterfall_count));
      for (terrain::Waterfall& waterfall : rivers.waterfalls) {
        float drop = 0, distance = 0, slope = 0, area = 0;
        if (!input.scalar (waterfall.reach_id.value) ||
            !input.scalar (waterfall.lip_cell.value) ||
            !input.scalar (waterfall.foot_cell.value) || !input.scalar (drop) ||
            !input.scalar (distance) || !input.scalar (slope) ||
            !input.scalar (area))
          return false;
        waterfall.drop = drop * u::m;
        waterfall.horizontal_distance = distance * u::m;
        waterfall.slope = slope * terrain::terrain_slope[mp_units::one];
        waterfall.contributing_area = area * u::m * u::m;
      }
      return input.vector (rivers.body_traversed, bodies) &&
             rivers.body_traversed.size () == bodies;
    }

    void write_trails (BinaryWriter& output,
                       const terrain::TrailNetwork& trails) {
      output.scalar (trails.plan.home_base.value);
      output.scalar (trails.plan.scenic_focus.value);
      output.ids (trails.plan.control_sites);
      output.ids (trails.plan.circuit);
      output.scalar (trails.alignment.length.numerical_value_in (u::m));
      output.scalar (
        static_cast<std::uint64_t> (trails.alignment.points.size ()));
      for (const terrain::TrailAlignmentPoint& point :
           trails.alignment.points) {
        output.scalar (point.x_m);
        output.scalar (point.z_m);
      }
      output.scalar (meters (trails.formed_width));
      output.vector<float> (trails.earthwork_delta_m);
    }

    bool read_trails (BinaryReader& input,
                      terrain::TrailNetwork& trails,
                      std::size_t cells) {
      double alignment_length = 0;
      std::uint64_t point_count = 0;
      float formed_width = 0;
      if (!input.scalar (trails.plan.home_base.value) ||
          !input.scalar (trails.plan.scenic_focus.value) ||
          !input.ids (trails.plan.control_sites, cells) ||
          !input.ids (trails.plan.circuit, cells) ||
          !input.scalar (alignment_length) || !input.scalar (point_count) ||
          point_count > cells * 8)
        return false;
      trails.alignment.length = alignment_length * u::m;
      trails.alignment.points.resize (static_cast<std::size_t> (point_count));
      for (terrain::TrailAlignmentPoint& point : trails.alignment.points)
        if (!input.scalar (point.x_m) || !input.scalar (point.z_m))
          return false;
      if (!input.scalar (formed_width) ||
          !input.vector (trails.earthwork_delta_m, cells) ||
          trails.earthwork_delta_m.size () != cells)
        return false;
      trails.formed_width = formed_width * u::m;
      return true;
    }
  }

  std::string world_cache_name (const terrain::WorldRecipe& recipe,
                                const WorldCacheConfig& config) {
    if (config.mode == WorldCacheMode::Disabled)
      return {};
    const std::string cache_namespace =
      config.key.empty () ? "default" : "key-" + config.key;
    return "world-" + cache_namespace + '-' + recipe_cache_identity (recipe) +
           ".world";
  }

  std::unique_ptr<GeneratedWorld>
  try_load_world_cache (WorldParams params,
                        terrain::WorldRecipe recipe,
                        const std::string& directory) {
    const terrain::TerrainDomain domain (
      recipe.resolution (), recipe.resolution (), recipe.extent ());
    const std::size_t cells = domain.size ();
    BinaryReader topology (file_in (directory, "topology.bin"));
    if (!topology || !read_recipe (topology, recipe))
      return {};

    map::SurfaceGeometry surface (domain);
    terrain::FloodSurface flood_surface (domain);
    terrain::DrainageReadings drainage_readings (domain);
    terrain::WaterSheets water (domain);
    map::SurfaceReadings readings (domain);
    terrain::TrailNetwork trails {
      .domain = domain,
      .use = terrain::TrailUseMap (domain),
    };
    if (!load_bundle (surface, file_in (directory, "surface.arrows")) ||
        !load_bundle (flood_surface, file_in (directory, "flood.arrows")) ||
        !load_bundle (drainage_readings,
                      file_in (directory, "drainage.arrows")) ||
        !load_bundle (water, file_in (directory, "water.arrows")) ||
        !load_bundle (readings, file_in (directory, "readings.arrows")) ||
        !load_bundle (trails.use, file_in (directory, "trail-use.arrows")))
      return {};

    terrain::FloodField flood { .surface = std::move (flood_surface) };
    terrain::LakeCensus lakes;
    terrain::DrainageGraph drainage { .readings =
                                        std::move (drainage_readings) };
    terrain::RiverNetwork rivers;
    if (!read_flood (topology, flood, cells) ||
        !read_lakes (topology, lakes, cells) ||
        !topology.ids (drainage.receiver, cells) ||
        drainage.receiver.size () != cells ||
        !read_rivers (topology, rivers, cells, lakes.domain ().size ()) ||
        !read_trails (topology, trails, cells))
      return {};

    const std::uint32_t forest_seed = recipe.seed ().value ^ 0xa34c91e5U;
    std::optional<ForestPlan> forest =
      try_load_forest_plan (file_in (directory, "forest-plan.bin").string (),
                            forest_seed,
                            forest_period (domain));
    if (!forest)
      return {};

    Hydrology hydrology (std::move (flood),
                         std::move (lakes),
                         std::move (drainage),
                         std::move (rivers));
    return std::make_unique<GeneratedWorld> (params,
                                             std::move (recipe),
                                             std::move (surface),
                                             std::move (hydrology),
                                             std::move (water),
                                             std::move (trails),
                                             std::move (readings),
                                             std::move (*forest));
  }

  void save_world_cache (const GeneratedWorld& world,
                         const std::string& directory) {
    std::filesystem::create_directories (directory);
    save_bundle (world.surface (), file_in (directory, "surface.arrows"));
    const auto& [flood, lakes, drainage, rivers] = world.hydrology ();
    save_bundle (flood.surface, file_in (directory, "flood.arrows"));
    save_bundle (drainage.readings, file_in (directory, "drainage.arrows"));
    save_bundle (world.water_surface (), file_in (directory, "water.arrows"));
    save_bundle (world.readings (), file_in (directory, "readings.arrows"));
    save_bundle (world.trails ().use, file_in (directory, "trail-use.arrows"));
    save_forest_plan (world.forest (),
                      world.recipe ().seed ().value ^ 0xa34c91e5U,
                      file_in (directory, "forest-plan.bin").string ());

    BinaryWriter topology (file_in (directory, "topology.bin"));
    write_recipe (topology, world.recipe ());
    write_flood (topology, flood);
    write_lakes (topology, lakes);
    topology.ids (drainage.receiver);
    write_rivers (topology, rivers);
    write_trails (topology, world.trails ());
    topology.finish ();
  }
}
