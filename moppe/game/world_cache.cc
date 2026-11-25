#include <moppe/game/world_cache.hh>

#include <moppe/game/land.hh>

#include <moppe/spatial/bundle_storage.hh>
#include <moppe/terrain/domain_storage.hh>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// The cache is portable data: every value is written as a fixed-width
// little-endian scalar (the Arrow bundles carry their own schema), so a world
// baked on an arm64 Mac loads unchanged on an x86-64 Xbox.
static_assert (std::endian::native == std::endian::little,
               "world caches are stored little-endian");

namespace moppe::game {
  namespace {
    constexpr std::array<char, 12> CACHE_MAGIC { 'M', 'O', 'P', 'P', 'E', 'W',
                                                 'O', 'R', 'L', 'D', '0', '1' };
    // Version 19 runs rivers at four tenths of their spate, inside the bars
    // their beds were shaped by.
    // Version 18 carries running water across the corner where a stream
    // steps diagonally from cell to cell.
    // Version 17 takes each reach's nearest section for a cell's level and
    // keeps running levels off standing water.
    // Version 16 stands running water at the stage its discharge needs
    // (terrain/watercourse.hh); earlier water was a film on the bed.
    // Version 15 is the first world generated the same on every platform
    // (docs/determinism.md); earlier caches hold their platform's own.
    // Version 13 reconstructs the full hillslope gradient before applying the
    // nonlinear transport law. Version 12 used one cardinal component.
    // Version 30 takes smaller lakes and millponds for outlets and favours
    // sanctuaries in sight of one another.
    // Version 29 joins each branch to the road it leaves before laying it
    // along the contours.
    // Version 28 bridges the rivers the roads cross (RoadBridge).
    // Version 27 lays each cart road again at full resolution along the
    // contours, winding round spurs and into gullies.
    // Version 26 counts every journey along a way, not only those crossing
    // its middle step.
    // Version 25 plans roads round the earthwork between the planning
    // lattice's nodes and holds their grade through deep cuts and fills.
    // Version 24 reads the ground's disturbance (game/disturbance.hh).
    // Version 23 builds the roads into the ground and keeps their profiles.
    // Version 22 keeps the planned roads (terrain/roads.hh).
    // Version 32 bridges a river over a wide floodplain on its own span,
    // reached by embankments, rather than leaving the road in it.
    // Version 33 crosses ponds and small streams on embankments over
    // culverts (RoadCulvert).
    // Version 34 ends a bridge where the bank already stands at its deck.
    // Version 35 cuts the ground under a span clear of the deck.
    // Version 36 ends each cart road on dry ground in a level turning
    // place, keeps the treads clear of standing water beside them, and
    // leaves the ground under a bridge's deck bare.
    // Version 38 lays a loop round the world through the landing for the
    // bus (Road::loop).
    // Version 39 widens the roads, the bus's lane among them.
    // Version 40 cuts roadside ditches along road cuts.
    // Version 41 permits inland bus routes and bakes river beds before trails.
    // Version 42 penalises lengthwise road routing in rivers and shallow water.
    constexpr std::uint32_t CACHE_VERSION = 43;
    // A finished world is derived from its land, so a change to what land a
    // recipe makes (LAND_VERSION) retires the finished worlds made from the
    // old land as surely as a change to their own layout does.
    constexpr std::uint32_t STORED_VERSION =
      CACHE_VERSION * 1000 + static_cast<std::uint32_t> (LAND_VERSION);
  }

  std::string recipe_cache_identity (const terrain::WorldRecipe& recipe) {
    const auto bits = [] (float value) {
      return std::bit_cast<std::uint32_t> (value);
    };
    std::ostringstream name;
    name << terrain::profile_id (recipe.generation_profile ()) << '-'
         << recipe.resolution () << '-' << recipe.seed ().value << std::hex
         << "-extent-";
    for (std::size_t component = 0; component < 3; ++component)
      name << (component ? "-" : "")
           << bits (extent_value (recipe.extent ())[component]);
    name << "-water-"
         << bits ((recipe.water_datum ()).numerical_value_in (u::m));
    name << "-uplift-"
         << bits (recipe.evolution ().uplift_duration.numerical_value_in (
              moppe::astronomy::Julian_year));
    name << "-channel-"
         << bits (
              recipe.evolution ().channel_initiation_area.numerical_value_in (
                u::m * u::m));
    name << "-runoff-"
         << bits (recipe.evolution ()
                    .fluvial_transport.runoff_rate.numerical_value_in (
                      u::m / moppe::astronomy::Julian_year));
    name << "-concentration-"
         << bits (recipe.evolution ()
                    .fluvial_transport.concentration_at_unit_slope
                    .numerical_value_in (mp_units::one));
    name << "-slope-"
         << bits (recipe.evolution ()
                    .critical_hillslope_gradient.numerical_value_in (
                      mp_units::one));
    name << "-waste-"
         << bits (
              recipe.evolution ()
                .maximum_hillslope_diffusivity_multiplier.numerical_value_in (
                  mp_units::one));
    return name.str ();
  }

  namespace {

    spatial_extent_t forest_period (const terrain::TerrainDomain& domain) {
      return spatial_extent_in_metres (
        Vec3 (domain.period_x ().numerical_value_in (u::m),
              0,
              domain.period_z ().numerical_value_in (u::m)));
    }

    // Creates the directory and whichever of its parents are missing,
    // without asking for the ones that exist: a UWP app container refuses
    // even to "create" an existing ancestor it may not write, which
    // std::filesystem::create_directories does.
    void create_missing_directories (const std::filesystem::path& directory) {
      std::error_code error;
      if (std::filesystem::create_directory (directory, error) || !error)
        return;
      const std::filesystem::path parent = directory.parent_path ();
      if (!parent.empty () && parent != directory)
        create_missing_directories (parent);
      std::filesystem::create_directory (directory);
    }

    std::filesystem::path file_in (const std::string& directory,
                                   const char* name) {
      return std::filesystem::path (directory) / name;
    }

    class TemporaryCache {
    public:
      explicit TemporaryCache (const std::filesystem::path& destination) {
        const auto parent = destination.parent_path ();
        if (!parent.empty ())
          create_missing_directories (parent);
        std::random_device random;
        for (;;) {
          m_path = destination.string () + ".partial-" +
                   std::to_string (random ()) + '-' +
                   std::to_string (random ());
          std::error_code error;
          if (std::filesystem::create_directory (m_path, error))
            break;
          if (error)
            throw std::filesystem::filesystem_error (
              "create temporary world cache", m_path, error);
        }
      }

      ~TemporaryCache () {
        std::error_code ignored;
        if (!m_keep)
          std::filesystem::remove_all (m_path, ignored);
      }

      const std::filesystem::path& path () const {
        return m_path;
      }

      void keep () {
        m_keep = true;
      }

      void discard () {
        m_keep = false;
      }

    private:
      std::filesystem::path m_path;
      bool m_keep = false;
    };

    template <typename Bundle>
    void save_bundle (const Bundle& bundle, const std::filesystem::path& path) {
      std::ofstream output (path, std::ios::binary);
      if (!output)
        throw std::runtime_error ("can't write world cache: " + path.string ());
      spatial::write_bundle (output, bundle);
      output.close ();
      if (!output)
        throw std::runtime_error ("can't write world cache: " + path.string ());
    }

    template <typename Bundle>
    std::optional<Bundle> read_field (const std::filesystem::path& path,
                                      const terrain::TerrainDomain& expected) {
      std::ifstream input (path, std::ios::binary);
      if (input) {
        auto field = spatial::read_bundle<Bundle> (input);
        if (field && field->domain () == expected)
          return field;
      }
      return std::nullopt;
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

      // A quantity as the number it is in `reference`'s unit, the file's.
      template <mp_units::Quantity Q, mp_units::Reference R>
      void quantity (Q value, R reference) {
        scalar (value.numerical_value_in (get_unit (reference)));
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
        m_output.close ();
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

      // What the writer's quantity wrote: a number in `reference`'s unit,
      // of `value`'s representation.
      template <mp_units::Quantity Q, mp_units::Reference R>
      bool quantity (Q& value, R reference) {
        typename Q::rep number {};
        if (!scalar (number))
          return false;
        value = number * reference;
        return true;
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

    // The units the file keeps each kind of quantity in.
    constexpr auto square_metre = u::m * u::m;
    constexpr auto cubic_metre = u::m * u::m * u::m;
    constexpr auto julian_year = moppe::astronomy::Julian_year;
    constexpr auto slope_ratio = terrain::terrain_slope[mp_units::one];
    constexpr auto proportion_unit = proportion[mp_units::one];

    void write_recipe (BinaryWriter& output,
                       const terrain::WorldRecipe& recipe) {
      output.bytes (CACHE_MAGIC.data (), CACHE_MAGIC.size ());
      output.scalar (STORED_VERSION);
      output.scalar (static_cast<std::uint32_t> (recipe.resolution ()));
      output.scalar (recipe.seed ().value);
      output.scalar (static_cast<std::uint32_t> (recipe.generation_profile ()));
      const auto& evolution = recipe.evolution ();
      for (std::size_t component = 0; component < 3; ++component)
        output.scalar (extent_value (recipe.extent ())[component]);
      output.quantity (recipe.water_datum (), u::m);
      output.quantity (evolution.uplift_duration, julian_year);
      output.quantity (evolution.channel_initiation_area, square_metre);
      output.quantity (evolution.fluvial_transport.runoff_rate,
                       u::m / julian_year);
      output.quantity (evolution.fluvial_transport.concentration_at_unit_slope,
                       mp_units::one);
      output.quantity (evolution.critical_hillslope_gradient, mp_units::one);
      output.quantity (evolution.maximum_hillslope_diffusivity_multiplier,
                       mp_units::one);
    }

    std::optional<spatial_extent_t> read_extent (BinaryReader& input) {
      std::array<meters_t, 3> lanes {};
      for (auto& lane : lanes)
        if (!input.quantity (lane, u::m) || !isfinite (lane) ||
            lane <= 0 * u::m)
          return std::nullopt;
      return spatial_extent_in_metres (Vec3 (meters_value (lanes[0]),
                                             meters_value (lanes[1]),
                                             meters_value (lanes[2])));
    }

    bool read_recipe (BinaryReader& input, const terrain::WorldRecipe& recipe) {
      std::array<char, CACHE_MAGIC.size ()> magic {};
      std::uint32_t version = 0;
      std::uint32_t resolution = 0;
      std::uint32_t seed = 0;
      std::uint32_t profile = 0;
      meters_t water {};
      julian_years_t uplift {};
      square_meters_t channel_initiation_area {};
      meters_per_julian_year_t runoff {};
      terrain::SedimentConcentration concentration_at_unit_slope {};
      proportion_t critical_hillslope_gradient {};
      proportion_t maximum_hillslope_multiplier {};
      if (!input.bytes (magic.data (), magic.size ()) ||
          !input.scalar (version) || !input.scalar (resolution) ||
          !input.scalar (seed) || !input.scalar (profile))
        return false;
      const auto extent = read_extent (input);
      if (!extent)
        return false;
      const auto& evolution = recipe.evolution ();
      return input.quantity (water, u::m) &&
             input.quantity (uplift, julian_year) &&
             input.quantity (channel_initiation_area, square_metre) &&
             input.quantity (runoff, u::m / julian_year) &&
             input.quantity (concentration_at_unit_slope,
                             terrain::sediment_concentration[mp_units::one]) &&
             input.quantity (critical_hillslope_gradient,
                             proportion[mp_units::one]) &&
             input.quantity (maximum_hillslope_multiplier,
                             proportion[mp_units::one]) &&
             magic == CACHE_MAGIC && version == STORED_VERSION &&
             resolution == static_cast<std::uint32_t> (recipe.resolution ()) &&
             seed == recipe.seed ().value &&
             profile ==
               static_cast<std::uint32_t> (recipe.generation_profile ()) &&
             *extent == recipe.extent () && water == recipe.water_datum () &&
             uplift == evolution.uplift_duration &&
             channel_initiation_area == evolution.channel_initiation_area &&
             runoff == evolution.fluvial_transport.runoff_rate &&
             concentration_at_unit_slope ==
               evolution.fluvial_transport.concentration_at_unit_slope &&
             critical_hillslope_gradient ==
               evolution.critical_hillslope_gradient &&
             maximum_hillslope_multiplier ==
               evolution.maximum_hillslope_diffusivity_multiplier;
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
        output.quantity (body.area, square_metre);
        output.quantity (body.maximum_depth, u::m);
        output.quantity (body.mean_depth, u::m);
        output.quantity (body.volume, cubic_metre);
        output.quantity (body.surface_level, u::m);
        output.scalar (static_cast<std::uint8_t> (body.ocean_connected));
        output.scalar (body.outlet_cell.value);
        output.scalar (body.spill_cell.value);
        output.scalar (static_cast<std::uint32_t> (body.classification));
        output.quantity (body.inradius, u::m);
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
        square_meters_t area {};
        meters_t maximum_depth {}, mean_depth {}, surface_level {}, inradius {};
        cubic_meters_t volume {};
        std::uint8_t ocean = 0, channel_like = 0;
        std::uint32_t outlet = 0, spill = 0, classification = 0;
        if (!input.scalar (id) || !input.scalar (count) ||
            !input.quantity (area, square_metre) ||
            !input.quantity (maximum_depth, u::m) ||
            !input.quantity (mean_depth, u::m) ||
            !input.quantity (volume, cubic_metre) ||
            !input.quantity (surface_level, u::m) || !input.scalar (ocean) ||
            !input.scalar (outlet) || !input.scalar (spill) ||
            !input.scalar (classification) ||
            !input.quantity (inradius, u::m) || !input.scalar (channel_like) ||
            count > cells ||
            classification >
              static_cast<std::uint32_t> (terrain::WaterBodyClass::Sea))
          return false;
        bodies.push_back (
          { .id = terrain::WaterBodyId { id },
            .cells = terrain::cell_count (count),
            .area = area,
            .maximum_depth = maximum_depth,
            .mean_depth = mean_depth,
            .volume = volume,
            .surface_level = surface_level,
            .ocean_connected = ocean != 0,
            .outlet_cell = terrain::CellIndex { outlet },
            .spill_cell = terrain::CellIndex { spill },
            .classification =
              static_cast<terrain::WaterBodyClass> (classification),
            .inradius = inradius,
            .channel_like = channel_like != 0 });
      }
      lakes = terrain::LakeCensus (std::move (membership), std::move (bodies));
      return true;
    }

    void write_rivers (BinaryWriter& output,
                       const terrain::RiverNetwork& rivers) {
      output.quantity (rivers.minimum_area, square_metre);
      output.quantity (rivers.waterfall_parameters.minimum_drop, u::m);
      output.quantity (rivers.waterfall_parameters.minimum_slope, slope_ratio);
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
        output.quantity (reach.upstream_area, square_metre);
        output.quantity (reach.downstream_area, square_metre);
        output.quantity (reach.maximum_slope, slope_ratio);
        output.quantity (reach.alignment.length, u::m);
        output.scalar (
          static_cast<std::uint64_t> (reach.alignment.points.size ()));
        for (const terrain::RiverAlignmentPoint& point :
             reach.alignment.points) {
          output.quantity (x_of (point.place), u::m);
          output.quantity (z_of (point.place), u::m);
          output.quantity (point.distance, u::m);
          output.quantity (point.flow_distance, u::m);
          output.quantity (point.contributing_area, square_metre);
          output.quantity (point.slope, slope_ratio);
          output.quantity (point.waterfall, proportion_unit);
          output.quantity (point.standing_water, proportion_unit);
          output.quantity (point.water_level.delta_from_zero (), u::m);
          output.quantity (point.pooled, proportion_unit);
        }
      }
      output.scalar (static_cast<std::uint64_t> (rivers.waterfalls.size ()));
      for (const terrain::Waterfall& waterfall : rivers.waterfalls) {
        output.scalar (waterfall.reach_id.value);
        output.scalar (waterfall.lip_cell.value);
        output.scalar (waterfall.foot_cell.value);
        output.quantity (waterfall.drop, u::m);
        output.quantity (waterfall.horizontal_distance, u::m);
        output.quantity (waterfall.slope, slope_ratio);
        output.quantity (waterfall.contributing_area, square_metre);
      }
      output.vector<std::uint8_t> (rivers.body_traversed);
    }

    bool read_rivers (BinaryReader& input,
                      terrain::RiverNetwork& rivers,
                      std::size_t cells,
                      std::size_t bodies) {
      std::uint64_t separation = 0, reach_count = 0;
      if (!input.quantity (rivers.minimum_area, square_metre) ||
          !input.quantity (rivers.waterfall_parameters.minimum_drop, u::m) ||
          !input.quantity (rivers.waterfall_parameters.minimum_slope,
                           slope_ratio) ||
          !input.scalar (separation) || !input.scalar (reach_count) ||
          reach_count > cells)
        return false;
      rivers.waterfall_parameters.separation_cells =
        terrain::separation_cell_count (separation);
      rivers.reaches.reserve (static_cast<std::size_t> (reach_count));
      for (std::uint64_t index = 0; index < reach_count; ++index) {
        terrain::RiverReach reach;
        std::uint8_t downstream_ocean = 0;
        std::uint64_t point_count = 0;
        if (!input.scalar (reach.id.value) || !input.ids (reach.cells, cells) ||
            !input.scalar (reach.upstream_body.value) ||
            !input.scalar (reach.downstream_body.value) ||
            !input.scalar (downstream_ocean) ||
            !input.scalar (reach.downstream_reach.value) ||
            !input.quantity (reach.upstream_area, square_metre) ||
            !input.quantity (reach.downstream_area, square_metre) ||
            !input.quantity (reach.maximum_slope, slope_ratio) ||
            !input.quantity (reach.alignment.length, u::m) ||
            !input.scalar (point_count) || point_count > cells * 8)
          return false;
        reach.downstream_ocean = downstream_ocean != 0;
        reach.alignment.points.resize (static_cast<std::size_t> (point_count));
        for (terrain::RiverAlignmentPoint& point : reach.alignment.points) {
          meters_t x, z, water_level;
          if (!input.quantity (x, u::m) || !input.quantity (z, u::m) ||
              !input.quantity (point.distance, u::m) ||
              !input.quantity (point.flow_distance, u::m) ||
              !input.quantity (point.contributing_area, square_metre) ||
              !input.quantity (point.slope, slope_ratio) ||
              !input.quantity (point.waterfall, proportion_unit) ||
              !input.quantity (point.standing_water, proportion_unit) ||
              !input.quantity (water_level, u::m) ||
              !input.quantity (point.pooled, proportion_unit))
            return false;
          point.place = plan_position (x, z);
          point.water_level = terrain::surface_elevation_point (water_level);
        }
        rivers.reaches.push_back (std::move (reach));
      }
      std::uint64_t waterfall_count = 0;
      if (!input.scalar (waterfall_count) || waterfall_count > cells)
        return false;
      rivers.waterfalls.resize (static_cast<std::size_t> (waterfall_count));
      for (terrain::Waterfall& waterfall : rivers.waterfalls) {
        if (!input.scalar (waterfall.reach_id.value) ||
            !input.scalar (waterfall.lip_cell.value) ||
            !input.scalar (waterfall.foot_cell.value) ||
            !input.quantity (waterfall.drop, u::m) ||
            !input.quantity (waterfall.horizontal_distance, u::m) ||
            !input.quantity (waterfall.slope, slope_ratio) ||
            !input.quantity (waterfall.contributing_area, square_metre))
          return false;
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
      output.quantity (trails.alignment.length, u::m);
      output.scalar (
        static_cast<std::uint64_t> (trails.alignment.points.size ()));
      for (const plan_position_t& point : trails.alignment.points)
        output.scalar (point);
      output.quantity (trails.formed_width, u::m);
      // Each change is stored as its float metres.
      static_assert (sizeof (terrain::ElevationChange) == sizeof (float));
      output.vector<terrain::ElevationChange> (trails.earthwork_delta);
    }

    // Roads are stored as they were before their types: a point two
    // floats (x, z), a height one float, a culvert (point, invert,
    // diameter) twelve bytes, and ages the years as floats.
    static_assert (sizeof (plan_position_t) == 2 * sizeof (float) &&
                   std::is_trivially_copyable_v<plan_position_t>);
    static_assert (sizeof (age_t) == sizeof (float) &&
                   std::is_trivially_copyable_v<age_t>);
    static_assert (sizeof (terrain::SurfaceElevation) == sizeof (float) &&
                   std::is_trivially_copyable_v<terrain::SurfaceElevation>);
    static_assert (sizeof (terrain::RoadCulvert) == 12 &&
                   offsetof (terrain::RoadCulvert, invert) == 4 &&
                   offsetof (terrain::RoadCulvert, diameter) == 8 &&
                   std::is_trivially_copyable_v<terrain::RoadCulvert>);

    void write_roads (BinaryWriter& output, const terrain::RoadNetwork& roads) {
      output.scalar (static_cast<std::uint64_t> (roads.places.size ()));
      for (const terrain::RoadPlace& place : roads.places) {
        output.scalar (static_cast<std::uint32_t> (place.kind));
        output.scalar (place.at);
        output.scalar (place.height);
        output.scalar (place.founded);
        output.scalar (place.abandoned);
      }
      output.scalar (static_cast<std::uint64_t> (roads.roads.size ()));
      for (const terrain::Road& road : roads.roads) {
        output.scalar (static_cast<std::uint32_t> (road.kind));
        output.scalar (road.traffic);
        output.scalar (road.age);
        output.scalar (road.abandoned);
        output.scalar (static_cast<std::uint32_t> (road.loop));
        output.vector<plan_position_t> (road.points);
        output.vector<terrain::SurfaceElevation> (road.heights);
        output.vector<terrain::RoadBridge> (road.bridges);
        output.vector<terrain::RoadCulvert> (road.culverts);
      }
      output.scalar (static_cast<std::uint64_t> (roads.sightlines.size ()));
      for (const auto& [a, b] : roads.sightlines) {
        output.scalar (static_cast<std::uint64_t> (a));
        output.scalar (static_cast<std::uint64_t> (b));
      }
    }

    bool read_roads (BinaryReader& input,
                     terrain::RoadNetwork& roads,
                     std::size_t cells) {
      std::uint64_t count = 0;
      if (!input.scalar (count) || count > cells)
        return false;
      roads.places.resize (static_cast<std::size_t> (count));
      for (terrain::RoadPlace& place : roads.places) {
        std::uint32_t kind = 0;
        if (!input.scalar (kind) ||
            kind > static_cast<std::uint32_t> (
                     terrain::RoadPlace::Kind::Sanctuary) ||
            !input.scalar (place.at) || !input.scalar (place.height) ||
            !input.scalar (place.founded) || !input.scalar (place.abandoned))
          return false;
        place.kind = static_cast<terrain::RoadPlace::Kind> (kind);
      }
      if (!input.scalar (count) || count > cells)
        return false;
      roads.roads.resize (static_cast<std::size_t> (count));
      for (terrain::Road& road : roads.roads) {
        std::uint32_t kind = 0, loop = 0;
        if (!input.scalar (kind) ||
            kind > static_cast<std::uint32_t> (terrain::Road::Kind::Footpath) ||
            !input.scalar (road.traffic) || !input.scalar (road.age) ||
            !input.scalar (road.abandoned) || !input.scalar (loop) ||
            loop > 1 || !input.vector (road.points, cells) ||
            !input.vector (road.heights, cells) ||
            !input.vector (road.bridges, cells) ||
            !input.vector (road.culverts, cells))
          return false;
        for (const terrain::RoadCulvert& culvert : road.culverts)
          if (culvert.at >= road.points.size ())
            return false;
        for (const terrain::RoadBridge& bridge : road.bridges)
          if (bridge.first > bridge.last || bridge.last >= road.points.size ())
            return false;
        road.kind = static_cast<terrain::Road::Kind> (kind);
        road.loop = loop != 0;
      }
      if (!input.scalar (count) || count > cells)
        return false;
      roads.sightlines.resize (static_cast<std::size_t> (count));
      for (auto& [a, b] : roads.sightlines) {
        std::uint64_t first = 0, second = 0;
        if (!input.scalar (first) || !input.scalar (second) ||
            first >= roads.places.size () || second >= roads.places.size ())
          return false;
        a = static_cast<std::size_t> (first);
        b = static_cast<std::size_t> (second);
      }
      return true;
    }

    bool read_trails (BinaryReader& input,
                      terrain::TrailNetwork& trails,
                      std::size_t cells) {
      std::uint64_t point_count = 0;
      if (!input.scalar (trails.plan.home_base.value) ||
          !input.scalar (trails.plan.scenic_focus.value) ||
          !input.ids (trails.plan.control_sites, cells) ||
          !input.ids (trails.plan.circuit, cells) ||
          !input.quantity (trails.alignment.length, u::m) ||
          !input.scalar (point_count) || point_count > cells * 8)
        return false;
      trails.alignment.points.resize (static_cast<std::size_t> (point_count));
      for (plan_position_t& point : trails.alignment.points)
        if (!input.scalar (point))
          return false;
      return input.quantity (trails.formed_width, u::m) &&
             input.vector (trails.earthwork_delta, cells) &&
             trails.earthwork_delta.size () == cells;
    }
  }

  std::string world_cache_name (const terrain::WorldRecipe& recipe,
                                const WorldCacheConfig& config) {
    if (config.mode == WorldCacheMode::Disabled)
      return {};
    const std::string cache_namespace =
      config.key.empty () ? "default" : "key-" + config.key;
    return "world-" + cache_namespace + "-v" + std::to_string (STORED_VERSION) +
           '-' + recipe_cache_identity (recipe) + ".world";
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

    // Past a matching header, a rejection means a damaged or incompatible
    // cache rather than another world, so it says which part failed.
    const auto rejected = [&directory] (const char* part) {
      std::cerr << "moppe: world cache " << directory << ": " << part
                << " did not validate" << std::endl;
      return std::unique_ptr<GeneratedWorld> {};
    };
    auto surface = read_field<map::SurfaceGeometry> (
      file_in (directory, "surface.arrows"), domain);
    if (!surface)
      return rejected ("surface.arrows");
    auto flood_surface = read_field<terrain::FloodSurface> (
      file_in (directory, "flood.arrows"), domain);
    if (!flood_surface)
      return rejected ("flood.arrows");
    auto drainage_readings = read_field<terrain::DrainageReadings> (
      file_in (directory, "drainage.arrows"), domain);
    if (!drainage_readings)
      return rejected ("drainage.arrows");
    auto water = read_field<terrain::WaterSheets> (
      file_in (directory, "water.arrows"), domain);
    if (!water)
      return rejected ("water.arrows");
    auto readings = read_field<map::SurfaceReadings> (
      file_in (directory, "readings.arrows"), domain);
    if (!readings)
      return rejected ("readings.arrows");
    auto trail_use = read_field<terrain::TrailUseMap> (
      file_in (directory, "trail-use.arrows"), domain);
    if (!trail_use)
      return rejected ("trail-use.arrows");
    terrain::TrailNetwork trails {
      .domain = domain,
      .use = std::move (*trail_use),
    };

    terrain::FloodField flood { .surface = std::move (*flood_surface) };
    terrain::LakeCensus lakes;
    terrain::DrainageGraph drainage { .readings =
                                        std::move (*drainage_readings) };
    terrain::RiverNetwork rivers;
    if (!read_flood (topology, flood, cells) ||
        !read_lakes (topology, lakes, cells) ||
        !topology.ids (drainage.receiver, cells) ||
        drainage.receiver.size () != cells ||
        !read_rivers (topology, rivers, cells, lakes.domain ().size ()) ||
        !read_trails (topology, trails, cells))
      return rejected ("topology.bin");
    terrain::RoadNetwork roads;
    if (!read_roads (topology, roads, cells))
      return rejected ("topology.bin roads");

    const std::uint32_t forest_seed = recipe.seed ().value ^ 0xa34c91e5U;
    const std::string forest_path =
      file_in (directory, "forest-plan.bin").string ();
    std::optional<ForestPlan> forest =
      try_load_forest_plan (forest_path, forest_seed, forest_period (domain));
    if (!forest) {
      // The plan is cheap beside the evolved surface it reads: an outdated
      // plan is re-planted from the cached fields instead of discarding the
      // whole world.
      forest = plan_global_forest (*surface, *readings, *water, forest_seed);
      try {
        save_forest_plan (*forest, forest_seed, forest_path);
      } catch (const std::exception& error) {
        std::cerr << "moppe: could not refresh the cached forest plan: "
                  << error.what () << std::endl;
      }
    }

    Hydrology hydrology (std::move (flood),
                         std::move (lakes),
                         std::move (drainage),
                         std::move (rivers));
    return std::make_unique<GeneratedWorld> (params,
                                             std::move (recipe),
                                             std::move (*surface),
                                             std::move (hydrology),
                                             std::move (*water),
                                             std::move (trails),
                                             std::move (*readings),
                                             std::move (roads),
                                             std::move (*forest));
  }

  void save_world_cache (const GeneratedWorld& world,
                         const std::string& directory) {
    TemporaryCache temporary (directory);
    const auto partial = [&temporary] (const char* name) {
      return temporary.path () / name;
    };
    save_bundle (world.surface (), partial ("surface.arrows"));
    const auto& [flood, lakes, drainage, rivers] = world.hydrology ();
    save_bundle (flood.surface, partial ("flood.arrows"));
    save_bundle (drainage.readings, partial ("drainage.arrows"));
    save_bundle (world.water_surface (), partial ("water.arrows"));
    save_bundle (world.readings (), partial ("readings.arrows"));
    save_bundle (world.trails ().use, partial ("trail-use.arrows"));
    save_forest_plan (world.forest (),
                      world.recipe ().seed ().value ^ 0xa34c91e5U,
                      partial ("forest-plan.bin").string ());

    {
      BinaryWriter topology (partial ("topology.bin"));
      write_recipe (topology, world.recipe ());
      write_flood (topology, flood);
      write_lakes (topology, lakes);
      topology.ids (drainage.receiver);
      write_rivers (topology, rivers);
      write_trails (topology, world.trails ());
      write_roads (topology, world.roads ());
      topology.finish ();
    }
    // First try to publish without disturbing another writer's cache.
    std::error_code error;
    std::filesystem::rename (temporary.path (), directory, error);
    if (!error)
      return;

    // Keep the previous directory intact until the replacement is ready.
    // A reader in the interval between the renames sees a cache miss.
    TemporaryCache previous (directory);
    const auto old = previous.path () / "previous";
    error.clear ();
    std::filesystem::rename (directory, old, error);
    const bool moved_previous = !error;
    if (error && error != std::errc::no_such_file_or_directory)
      throw std::filesystem::filesystem_error (
        "move old world cache", directory, old, error);
    if (moved_previous)
      previous.keep ();

    error.clear ();
    std::filesystem::rename (temporary.path (), directory, error);
    if (!error ||
        try_load_world_cache (world.params (), world.recipe (), directory)) {
      previous.discard ();
      return;
    }

    if (moved_previous) {
      std::error_code restore_error;
      std::filesystem::rename (old, directory, restore_error);
      if (restore_error) {
        throw std::filesystem::filesystem_error (
          "publish world cache; previous cache kept at " + old.string (),
          temporary.path (),
          directory,
          error);
      }
      previous.discard ();
    }
    throw std::filesystem::filesystem_error (
      "publish world cache", temporary.path (), directory, error);
  }

  std::optional<WorldCacheIdentity>
  read_world_cache_identity (const std::string& directory) {
    if (directory.empty ())
      return std::nullopt;
    BinaryReader input (file_in (directory, "topology.bin"));
    std::array<char, CACHE_MAGIC.size ()> magic {};
    std::uint32_t version = 0;
    std::uint32_t resolution = 0;
    std::uint32_t seed = 0;
    std::uint32_t profile = 0;
    if (!input || !input.bytes (magic.data (), magic.size ()) ||
        !input.scalar (version) || !input.scalar (resolution) ||
        !input.scalar (seed) || !input.scalar (profile) ||
        magic != CACHE_MAGIC || version != STORED_VERSION || resolution == 0 ||
        resolution > 1u << 16 ||
        profile > static_cast<std::uint32_t> (
                    terrain::TerrainGenerationProfile::Research))
      return std::nullopt;
    const auto extent = read_extent (input);
    if (!extent)
      return std::nullopt;
    return WorldCacheIdentity {
      .extent = *extent,
      .resolution = static_cast<int> (resolution),
      .seed = seed,
      .profile = static_cast<terrain::TerrainGenerationProfile> (profile),
    };
  }
}
