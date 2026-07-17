#include <moppe/map/generate.hh>
#include <moppe/map/terrain_evaluator.hh>
#include <moppe/terrain/image.hh>
#include <moppe/terrain/program.hh>
#include <moppe/terrain/world_recipe.hh>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
  float parse_float (std::string_view text) {
    std::size_t consumed = 0;
    const float value = std::stof (std::string (text), &consumed);
    if (consumed != text.size ())
      throw std::invalid_argument ("invalid number: " + std::string (text));
    return value;
  }

  int parse_int (std::string_view text) {
    std::size_t consumed = 0;
    const int value = std::stoi (std::string (text), &consumed);
    if (consumed != text.size ())
      throw std::invalid_argument ("invalid integer: " + std::string (text));
    return value;
  }

  void apply_option (moppe::terrain::TerrainProgram& program,
                     std::optional<moppe::terrain::WorldRecipe>& world_recipe,
                     int resolution,
                     std::string_view option) {
    using namespace moppe::terrain;
    if (option == "world") {
      const GeologicalLayer layer = program.source.layer;
      world_recipe.emplace (make_world_recipe (
        moppe::spatial_extent_in_metres (moppe::Vec3 (11000, 650, 11000)),
        resolution,
        Topology::Torus,
        program.seed,
        50.0f * mp_units::si::metre,
        TerrainGenerationProfile::Research));
      program = world_recipe->terrain_program ();
      program.source.layer = layer;
      return;
    }
    if (option == "raw") {
      program.transforms.clear ();
      return;
    }
    if (option == "normalize") {
      program.transforms.emplace_back (NormalizeHeights {});
      return;
    }
    if (option == "trails") {
      program.transforms.emplace_back (TrailFormation {});
      return;
    }

    const std::size_t equals = option.find ('=');
    if (equals == std::string_view::npos)
      throw std::invalid_argument ("unknown pipeline option: " +
                                   std::string (option));
    const std::string_view name = option.substr (0, equals);
    const std::string_view value = option.substr (equals + 1);

    if (name == "power")
      program.transforms.emplace_back (PowerHeights { parse_float (value) });
    else if (name == "analytical") {
      std::vector<std::string_view> parts;
      std::size_t start = 0;
      while (start <= value.size ()) {
        const std::size_t comma = value.find (',', start);
        parts.push_back (value.substr (start,
                                       comma == std::string_view::npos
                                         ? value.size () - start
                                         : comma - start));
        if (comma == std::string_view::npos)
          break;
        start = comma + 1;
      }
      if (parts.empty () || parts.size () > 7)
        throw std::invalid_argument (
          "analytical expects age[,uplift,k,m,sea,iterations,relaxation]");
      AnalyticalErosion erosion;
      erosion.duration =
        parse_float (parts[0]) * mp_units::astronomy::Julian_year;
      if (parts.size () > 1)
        erosion.uplift_rate = parse_float (parts[1]) * mp_units::si::metre /
                              mp_units::astronomy::Julian_year;
      if (parts.size () > 2)
        erosion.erodibility = parse_float (parts[2]);
      if (parts.size () > 3)
        erosion.area_exponent = parse_float (parts[3]);
      if (parts.size () > 4)
        erosion.sea_level = parse_float (parts[4]);
      if (parts.size () > 5)
        erosion.fixed_point_iterations = iteration_count (parse_int (parts[5]));
      if (parts.size () > 6)
        erosion.relaxation = parse_float (parts[6]);
      program.transforms.emplace_back (erosion);
    } else if (name == "orogeny") {
      std::vector<std::string_view> parts;
      std::size_t start = 0;
      while (start <= value.size ()) {
        const std::size_t comma = value.find (',', start);
        parts.push_back (value.substr (start,
                                       comma == std::string_view::npos
                                         ? value.size () - start
                                         : comma - start));
        if (comma == std::string_view::npos)
          break;
        start = comma + 1;
      }
      if (parts.size () < 6 || parts.size () > 12)
        throw std::invalid_argument (
          "orogeny expects duration,dt,uplift,reference_rate,m,D"
          "[,sea,land_relief,coast,bathymetry,routing,channel_memory]");
      if (program.transforms.size () == 1 &&
          std::holds_alternative<NormalizeHeights> (
            program.transforms.front ()))
        program.transforms.clear ();
      program.source.mode = GeologicalSource::Mode::Orogeny;
      OrogenyEvolution orogeny;
      orogeny.evolution.duration =
        parse_float (parts[0]) * mp_units::astronomy::Julian_year;
      orogeny.evolution.time_step =
        parse_float (parts[1]) * mp_units::astronomy::Julian_year;
      orogeny.maximum_uplift_rate = parse_float (parts[2]) *
                                    mp_units::si::metre /
                                    mp_units::astronomy::Julian_year;
      orogeny.evolution.reference_incision_rate =
        parse_float (parts[3]) * mp_units::si::metre /
        mp_units::astronomy::Julian_year;
      orogeny.evolution.area_exponent = parse_float (parts[4]);
      orogeny.evolution.diffusivity =
        parse_float (parts[5]) * mp_units::si::metre * mp_units::si::metre /
        mp_units::astronomy::Julian_year;
      if (parts.size () > 6) {
        program.source.sea_level = parse_float (parts[6]);
        orogeny.evolution.sea_level = program.source.sea_level;
      }
      if (parts.size () > 7)
        program.source.initial_land_relief =
          parse_float (parts[7]) * mp_units::si::metre;
      if (parts.size () > 8)
        program.source.coastline = parse_float (parts[8]);
      if (parts.size () > 9)
        program.source.initial_bathymetric_relief =
          parse_float (parts[9]) * mp_units::si::metre;
      if (parts.size () > 10) {
        if (parts[10] == "d8")
          orogeny.evolution.routing = StreamPowerRouting::D8;
        else if (parts[10] == "d-infinity" || parts[10] == "dinf")
          orogeny.evolution.routing = StreamPowerRouting::DInfinity;
        else
          throw std::invalid_argument (
            "orogeny routing must be d8 or d-infinity");
      }
      if (parts.size () > 11)
        orogeny.evolution.channel_persistence =
          parse_float (parts[11]) * channel_persistence[mp_units::one];
      program.transforms.emplace_back (orogeny);
    } else if (name == "trails") {
      std::vector<std::string_view> parts;
      std::size_t start = 0;
      while (start <= value.size ()) {
        const std::size_t comma = value.find (',', start);
        parts.push_back (value.substr (start,
                                       comma == std::string_view::npos
                                         ? value.size () - start
                                         : comma - start));
        if (comma == std::string_view::npos)
          break;
        start = comma + 1;
      }
      if (parts.empty () || parts.size () > 12)
        throw std::invalid_argument (
          "trails expects min_m2[,max_m2,width,shoulder,cut,fill,"
          "max_grade,sea,designed_grade,highland_preference,"
          "alpine_avoidance,crossfall]");
      TrailFormation trails;
      trails.minimum_catchment_area =
        parse_float (parts[0]) * mp_units::si::metre * mp_units::si::metre;
      if (parts.size () > 1)
        trails.maximum_catchment_area =
          parse_float (parts[1]) * mp_units::si::metre * mp_units::si::metre;
      if (parts.size () > 2)
        trails.width = parse_float (parts[2]) * mp_units::si::metre;
      if (parts.size () > 3)
        trails.shoulder_blend = parse_float (parts[3]) * mp_units::si::metre;
      if (parts.size () > 4)
        trails.maximum_cut = parse_float (parts[4]) * mp_units::si::metre;
      if (parts.size () > 5)
        trails.maximum_fill = parse_float (parts[5]) * mp_units::si::metre;
      if (parts.size () > 6)
        trails.maximum_grade =
          parse_float (parts[6]) * terrain_slope[mp_units::one];
      if (parts.size () > 7)
        trails.sea_level = parse_float (parts[7]);
      if (parts.size () > 8)
        trails.designed_grade =
          parse_float (parts[8]) * terrain_slope[mp_units::one];
      if (parts.size () > 9)
        trails.highland_preference_height_above_sea =
          parse_float (parts[9]) * mp_units::si::metre;
      if (parts.size () > 10)
        trails.alpine_avoidance_height_above_sea =
          parse_float (parts[10]) * mp_units::si::metre;
      if (parts.size () > 11)
        trails.crossfall =
          parse_float (parts[11]) * terrain_slope[mp_units::one];
      program.transforms.emplace_back (trails);
    } else if (name == "diffuse") {
      const std::size_t comma = value.find (',');
      if (comma == std::string_view::npos)
        throw std::invalid_argument ("diffuse expects duration,diffusivity");
      program.transforms.emplace_back (HillslopeDiffusion {
        .duration = parse_float (value.substr (0, comma)) *
                    mp_units::astronomy::Julian_year,
        .diffusivity = parse_float (value.substr (comma + 1)) *
                       mp_units::si::metre * mp_units::si::metre /
                       mp_units::astronomy::Julian_year });
    } else if (name == "thermal") {
      const std::size_t comma = value.find (',');
      if (comma == std::string_view::npos)
        throw std::invalid_argument ("thermal expects iterations,talus");
      program.transforms.emplace_back (
        ThermalErosion { iteration_count (parse_int (value.substr (0, comma))),
                         parse_float (value.substr (comma + 1)) });
    } else if (name == "warp-amplitude")
      program.source.recipe.warp.amplitude = parse_float (value);
    else if (name == "continent-frequency")
      program.source.recipe.continent.noise.cycles = parse_int (value);
    else if (name == "plains-frequency")
      program.source.recipe.plains.noise.cycles = parse_int (value);
    else if (name == "mountain-frequency")
      program.source.recipe.mountains.cycles = parse_int (value);
    else if (name == "mask-low")
      program.source.recipe.blend.mask_low = parse_float (value);
    else if (name == "mask-high")
      program.source.recipe.blend.mask_high = parse_float (value);
    else if (name == "continent-weight")
      program.source.recipe.blend.continent_weight = parse_float (value);
    else if (name == "plains-weight")
      program.source.recipe.blend.plains_weight = parse_float (value);
    else if (name == "mountain-weight")
      program.source.recipe.blend.mountain_weight = parse_float (value);
    else
      throw std::invalid_argument ("unknown pipeline option: " +
                                   std::string (name));
  }
}

int main (int argc, char** argv) {
  using namespace moppe;
  using namespace moppe::terrain;

  try {
    const std::string path = argc > 1 ? argv[1] : "pipeline-demo.png";
    const int resolution = argc > 2 ? parse_int (argv[2]) : 257;
    const auto seed =
      static_cast<std::uint32_t> (argc > 3 ? std::stoul (argv[3]) : 0);
    const std::string_view layer_id = argc > 4 ? argv[4] : "combined";
    const auto layer = geological_layer_from_id (layer_id);
    if (resolution < 2)
      throw std::invalid_argument ("resolution must be at least two");
    if (!layer)
      throw std::invalid_argument ("unknown layer: " + std::string (layer_id));

    TerrainProgram program = make_geological_program (seed, *layer);
    std::optional<WorldRecipe> world_recipe;
    for (int i = 5; i < argc; ++i)
      apply_option (program, world_recipe, resolution, argv[i]);

    if (world_recipe)
      world_recipe = world_recipe->with_terrain_program (std::move (program));
    const TerrainProgram& world_program =
      world_recipe ? world_recipe->terrain_program () : program;

    const Vec3 physical_size =
      world_recipe ? extent_value (world_recipe->extent ())
      : world_program.source.mode == GeologicalSource::Mode::Orogeny
        ? Vec3 (11000, 650, 11000)
        : Vec3 (1, 1, 1);
    map::RandomHeightMap map (
      world_recipe ? world_recipe->resolution () : resolution,
      world_recipe ? world_recipe->resolution () : resolution,
      physical_size,
      world_recipe ? world_recipe->seed ().value : seed,
      world_recipe ? world_recipe->topology () : Topology::Torus);
    map::TerrainEvaluator evaluator (map);
    evaluator.begin (world_program);
    std::vector<TerrainTransformReport> reports;
    for (const TerrainTransform& transform : world_program.transforms)
      reports.push_back (evaluator.apply (transform));
    const std::size_t count = static_cast<std::size_t> (resolution) *
                              static_cast<std::size_t> (resolution);
    std::vector<float> values (map.raw_heights (), map.raw_heights () + count);
    const ScalarRaster raster (
      { .width = static_cast<std::size_t> (resolution),
        .height = static_cast<std::size_t> (resolution) },
      std::move (values));

    std::ofstream output (path, std::ios::binary);
    if (!output)
      throw std::runtime_error ("cannot open output: " + path);
    write_grayscale_png (output, raster);

    std::cout << "wrote " << path << " (" << layer_id << ", seed " << seed
              << ", " << resolution << "x" << resolution << ", stages";
    for (const TerrainTransform& transform : world_program.transforms)
      std::cout << " " << terrain_transform_id (transform);
    std::cout << ")\n";
    for (const TerrainTransformReport& result : reports)
      if (const auto* report =
            std::get_if<StreamPowerEvolutionReport> (&result))
        std::cout << "orogeny: steps=" << report->steps
                  << " diffusion_sweeps=" << report->diffusion_sweeps
                  << " fixed=" << report->fixed_boundaries << " uplift_m3="
                  << cubic_meters_value (report->tectonic_uplift_volume)
                  << " incised_m3="
                  << cubic_meters_value (report->incised_volume)
                  << " lowered_m3="
                  << cubic_meters_value (report->lowered_volume)
                  << " raised_m3=" << cubic_meters_value (report->raised_volume)
                  << " final_step_mean_m="
                  << meters_value (report->final_step_mean_change)
                  << " final_step_max_m="
                  << meters_value (report->final_step_maximum_change) << "\n";
      else if (const auto* report =
                 std::get_if<AnalyticalErosionReport> (&result))
        std::cout << "analytical: fixed=" << report->fixed_boundaries
                  << " lowered_m3="
                  << cubic_meters_value (report->lowered_volume)
                  << " raised_m3=" << cubic_meters_value (report->raised_volume)
                  << " mean_change_m="
                  << meters_value (report->mean_absolute_change)
                  << " max_change_m="
                  << meters_value (report->maximum_absolute_change) << "\n";
      else if (const auto* report =
                 std::get_if<HillslopeDiffusionReport> (&result))
        std::cout << "diffuse: sweeps=" << report->sweeps << " lowered_m3="
                  << cubic_meters_value (report->lowered_volume)
                  << " raised_m3=" << cubic_meters_value (report->raised_volume)
                  << " mean_change_m="
                  << meters_value (report->mean_absolute_change)
                  << " max_change_m="
                  << meters_value (report->maximum_absolute_change) << "\n";
      else if (const auto* report = std::get_if<TrailFormationReport> (&result))
        std::cout << "trails: centerline=" << report->centerline_cells
                  << " components=" << report->connected_components
                  << " circuit_m=" << meters_value (report->circuit_length)
                  << " shaped=" << report->shaped_cells
                  << " cut_m3=" << cubic_meters_value (report->cut_volume)
                  << " fill_m3=" << cubic_meters_value (report->fill_volume)
                  << " mean_grade=" << report->mean_centerline_grade
                  << " max_grade=" << report->maximum_centerline_grade
                  << " grade_exceptions=" << report->grade_exceptions
                  << " max_high_m="
                  << meters_value (report->maximum_centerline_height_above_sea)
                  << " max_step_m="
                  << meters_value (report->maximum_centerline_step)
                  << " mean_m=" << meters_value (report->mean_absolute_change)
                  << " max_m=" << meters_value (report->maximum_absolute_change)
                  << "\n";
  } catch (const std::exception& error) {
    std::cerr << "terrain pipeline demo: " << error.what () << "\n";
    std::cerr << "usage: terrain-pipeline-demo OUTPUT SIZE SEED LAYER "
              << "[world|raw|normalize|NAME=VALUE ...]\n";
    return -1;
  }

  return 0;
}
