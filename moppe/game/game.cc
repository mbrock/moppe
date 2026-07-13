// The game: the port of main.cc's MoppeGLUT application class onto
// the platform/render abstractions.  World generation runs on a
// background thread behind a loading screen; the frame follows the
// exact pass order of the GL build's render_scene().

#include <moppe/platform/platform.hh>
#include <moppe/profile.hh>
#include <moppe/render/renderer.hh>
#include <moppe/render/text.hh>

#include <moppe/game/blob_shadow.hh>
#include <moppe/game/chase_camera.hh>
#include <moppe/game/dust.hh>
#include <moppe/game/game_state.hh>
#include <moppe/game/graphics_benchmark.hh>
#include <moppe/game/graphics_settings.hh>
#include <moppe/game/hud.hh>
#include <moppe/game/inspector_ui.hh>
#include <moppe/game/river_surface.hh>
#include <moppe/game/stars.hh>
#include <moppe/game/terrain.hh>
#include <moppe/game/terrain_lab.hh>
#include <moppe/game/vegetation.hh>
#include <moppe/game/vehicle_render.hh>
#include <moppe/game/walker.hh>
#include <moppe/game/water_capture.hh>
#include <moppe/game/world.hh>
#include <moppe/map/terrain_evaluator.hh>

#include <moppe/map/generate.hh>
#include <moppe/mov/vehicle.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/moisture.hh>
#include <moppe/terrain/watercourse.hh>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

namespace moppe {
  namespace game {
    // THE sun: one fixed direction shared by the light, the shadow
    // map, the sky's sun disc, and the ocean glint.
    static const float SUN_AZIMUTH = 0.8f;
    static Vec3 sun_direction_for (float height) {
      const float el = (height - 0.5f) * 3.14159f;
      return Vec3 (std::cos (el) * std::sin (SUN_AZIMUTH),
                   std::sin (el),
                   std::cos (el) * std::cos (SUN_AZIMUTH));
    }

    struct GraphicsBenchmarkConfig {
      std::string output_path;
      int prelude_frames = 480;
      int settle_frames = 30;
      int measured_frames = 120;
    };

    static float smooth_curve (float edge0, float edge1, float x) {
      float t = (x - edge0) / (edge1 - edge0);
      clamp (t, 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }

    static void fill_rounded_rect (render::DrawList& dl,
                                   float x,
                                   float y,
                                   float width,
                                   float height,
                                   float radius) {
      radius = std::clamp (radius, 0.0f, std::min (width, height) * 0.5f);
      constexpr int corner_steps = 6;
      dl.begin (render::Prim::TriangleFan);
      dl.vertex (x + width * 0.5f, y + height * 0.5f);
      for (int corner = 0; corner < 4; ++corner) {
        const float cx =
          corner == 0 || corner == 3 ? x + radius : x + width - radius;
        const float cy = corner < 2 ? y + radius : y + height - radius;
        const float start = 3.14159265f + corner * 1.57079633f;
        for (int i = 0; i <= corner_steps; ++i) {
          const float angle = start + i * 1.57079633f / corner_steps;
          dl.vertex (cx + std::cos (angle) * radius,
                     cy + std::sin (angle) * radius);
        }
      }
      dl.vertex (x, y + radius);
      dl.end ();
    }

    static void loading_arc (render::DrawList& dl,
                             float cx,
                             float cy,
                             float radius,
                             float thickness,
                             float progress,
                             float alpha) {
      constexpr float pi = 3.14159265f;
      constexpr int segments = 96;
      progress = std::clamp (progress, 0.0f, 1.0f);
      dl.color (0.78f, 0.96f, 0.82f, alpha);
      dl.begin (render::Prim::TriangleStrip);
      for (int i = 0; i <= segments; ++i) {
        const float a = -0.5f * pi + 2.0f * pi * progress * i / segments;
        const float x = std::cos (a);
        const float y = std::sin (a);
        dl.vertex (cx + x * (radius - thickness),
                   cy + y * (radius - thickness));
        dl.vertex (cx + x * radius, cy + y * radius);
      }
      dl.end ();
    }

    static std::string
    terrain_cache_path (const WorldParams& world,
                        terrain::TerrainGenerationProfile profile,
                        int seed) {
      std::ostringstream name;
      name << "terrain-" << platform::executable_build_id () << '-'
           << terrain::profile_id (profile) << '-' << world.resolution << '-'
           << seed << (world.toroidal () ? "-torus.map" : "-bounded.map");
      return platform::cache_path (name.str ());
    }

    static bool
    load_terrain_history (const std::string& path,
                          std::size_t samples,
                          std::vector<std::vector<float>>& history) {
      std::ifstream input (path, std::ios::binary);
      if (!input)
        return false;
      input.seekg (12 + static_cast<std::streamoff> (samples * sizeof (float)));
      char magic[4] {};
      std::uint32_t snapshot_count = 0;
      std::uint64_t sample_count = 0;
      input.read (magic, sizeof (magic));
      input.read (reinterpret_cast<char*> (&snapshot_count),
                  sizeof (snapshot_count));
      input.read (reinterpret_cast<char*> (&sample_count),
                  sizeof (sample_count));
      if (!input || std::memcmp (magic, "HST1", 4) != 0 ||
          sample_count != samples || snapshot_count > 64)
        return false;
      std::vector<std::vector<float>> loaded (snapshot_count,
                                              std::vector<float> (samples));
      for (std::vector<float>& snapshot : loaded)
        input.read (reinterpret_cast<char*> (snapshot.data ()),
                    static_cast<std::streamsize> (samples * sizeof (float)));
      if (!input)
        return false;
      history = std::move (loaded);
      return true;
    }

    static void
    save_terrain_history (const std::string& path,
                          const std::vector<std::vector<float>>& history) {
      if (history.empty ())
        return;
      const std::uint64_t samples = history.front ().size ();
      if (samples == 0 || std::any_of (history.begin (),
                                       history.end (),
                                       [samples] (const auto& h) {
                                         return h.size () != samples;
                                       }))
        return;
      std::ofstream output (path, std::ios::binary | std::ios::app);
      const std::uint32_t snapshot_count =
        static_cast<std::uint32_t> (history.size ());
      output.write ("HST1", 4);
      output.write (reinterpret_cast<const char*> (&snapshot_count),
                    sizeof (snapshot_count));
      output.write (reinterpret_cast<const char*> (&samples), sizeof (samples));
      for (const std::vector<float>& snapshot : history)
        output.write (reinterpret_cast<const char*> (snapshot.data ()),
                      static_cast<std::streamsize> (samples * sizeof (float)));
    }

    static std::string
    last_seed_path (const WorldParams& world,
                    terrain::TerrainGenerationProfile profile) {
      std::ostringstream name;
      name << "last-seed-" << platform::executable_build_id () << '-'
           << terrain::profile_id (profile) << '-' << world.resolution
           << ".txt";
      return platform::cache_path (name.str ());
    }

    static void remember_seed (const WorldParams& world,
                               terrain::TerrainGenerationProfile profile,
                               int seed) {
      std::ofstream output (last_seed_path (world, profile));
      if (output)
        output << seed << '\n';
    }

    static int remembered_seed (const WorldParams& world,
                                terrain::TerrainGenerationProfile profile) {
      std::ifstream input (last_seed_path (world, profile));
      int seed = -1;
      if (input >> seed && seed >= 0)
        return seed;
      return static_cast<int> (::time (0));
    }

    static void prune_obsolete_terrain_caches () {
      const std::string build_id = platform::executable_build_id ();
      std::error_code error;
      const std::filesystem::path root (platform::cache_path (""));
      for (const std::filesystem::directory_entry& entry :
           std::filesystem::directory_iterator (root, error)) {
        if (error || !entry.is_regular_file ())
          continue;
        const std::string name = entry.path ().filename ().string ();
        const bool terrain_file =
          name.starts_with ("terrain-") || name.starts_with ("last-seed-");
        if (terrain_file && name.find (build_id) == std::string::npos)
          std::filesystem::remove (entry.path (), error);
      }
    }

    static float sun_elevation_for (float sun_height) {
      return std::sin ((sun_height - 0.5f) * 3.14159f);
    }

    static float daylight_for (float sun_height) {
      return smooth_curve (-0.08f, 0.18f, sun_elevation_for (sun_height));
    }

    static float golden_light_for (float sun_height) {
      const float elevation = sun_elevation_for (sun_height);
      return daylight_for (sun_height) *
             (1.0f - smooth_curve (0.15f, 0.65f, elevation));
    }

    // Sunlight is warm near the horizon and becomes a soft ivory as
    // it rises.  Intensity follows the real elevation rather than the
    // old cyclic sun-height color branches.
    static void sun_light_colors (float sun_height,
                                  DisplayColor& diffuse,
                                  DisplayColor& specular) {
      const float daylight = daylight_for (sun_height);
      const float golden = golden_light_for (sun_height);
      const DisplayColor warm (1.0f, 0.60f, 0.30f);
      const DisplayColor ivory (1.0f, 0.96f, 0.84f);
      const DisplayColor sun_color = mix_display (ivory, warm, golden);

      diffuse = scale_display (sun_color, 0.10f + 0.98f * daylight);
      specular = scale_display (mix_display (DisplayColor (0.92f, 0.95f, 1.0f),
                                             DisplayColor (1.0f, 0.86f, 0.70f),
                                             golden),
                                0.15f + 0.85f * daylight);
    }

    // Sky horizon color (port of gfx::Sky::get_horizon_color): the
    // CPU twin of the sky shader's horizon math, used to derive the
    // fog color each tick.
    static DisplayColor horizon_color_for (float sun_height) {
      const float daylight = daylight_for (sun_height);
      const float warmth = golden_light_for (sun_height) * 0.16f;
      // Must track sky.metal's day_horizon so distant terrain fades
      // into exactly the color the sky shows at the horizon.
      const DisplayColor day_horizon (0.55f, 0.68f, 0.84f);
      const DisplayColor night_horizon (0.035f, 0.045f, 0.09f);
      const DisplayColor warm_horizon (0.92f, 0.58f, 0.32f);
      return mix_display (mix_display (night_horizon, day_horizon, daylight),
                          warm_horizon,
                          warmth);
    }

    class MoppeGame : public platform::Game, private GameLogicState {
    public:
      MoppeGame (const WorldParams& world,
                 const GraphicsSettings& graphics,
                 bool start_in_terrain_lab,
                 bool terrain_lab_preview,
                 int seed,
                 std::string screenshot_path,
                 std::optional<WaterShot> water_shot,
                 terrain::TerrainGenerationProfile generation_profile,
                 std::optional<GraphicsBenchmarkConfig> benchmark)
          : m_world (world), m_graphics (graphics),
            m_spawn_position (position_value (world.spawn_position ())),
            m_seed (seed), m_generation_profile (generation_profile),
            m_map (world.resolution,
                   world.resolution,
                   extent_value (world.map_size),
                   m_seed,
                   world.topology ()),
            m_loading_map (world.resolution,
                           world.resolution,
                           extent_value (world.map_size),
                           m_seed,
                           world.topology ()),
            m_camera (18 * u::deg, 6.5f * u::m),
            // Dirt-bike figures: 2600 N of launch, 30 kW of engine --
            // hard low-end punch, ~125 km/h against drag (the old
            // 5000 N constant force topped out near 300).
            m_vehicle (world.spawn_position (),
                       45 * u::deg,
                       m_map,
                       2600 * u::N,
                       30 * u::kW,
                       150 * u::kg),
            m_car (world.spawn_position (),
                   45 * u::deg,
                   m_map,
                   14 * u::kN,
                   100 * u::kW,
                   900 * u::kg),
            m_renderer (0), m_start_in_terrain_lab (start_in_terrain_lab),
            m_terrain_lab_preview (terrain_lab_preview),
            m_screenshot_path (std::move (screenshot_path)),
            m_water_shot (water_shot), m_screenshot_frames (0), m_ready (false),
            m_gen_stage (0), m_benchmark (std::move (benchmark)),
            m_benchmark_baseline (graphics) {}

      GameState state () const {
        return { static_cast<const GameLogicState&> (*this),
                 m_vehicle.state (),
                 m_car.state (),
                 m_walker.state (),
                 m_camera.state (),
                 m_stars.state (),
                 m_dust.state () };
      }

      void restore (const GameState& state) {
        static_cast<GameLogicState&> (*this) = state.logic;
        m_vehicle.restore (state.vehicle);
        m_car.restore (state.car);
        m_walker.restore (state.walker);
        m_camera.restore (state.camera);
        m_stars.restore (state.stars);
        m_dust.restore (state.dust);
      }

      // -- lifecycle ---------------------------------------------------

      void setup (render::Renderer& r, int, int) override {
        m_renderer = &r;

        // Fast, main-thread resource setup; the heavy world build
        // runs behind the loading screen.
        m_hud.load (r);
        m_game_ui.load (r);
        m_terrain_lab.load (r);
        m_loading_title_font.reset (new render::FontAtlas (
          r, "AvenirNext-DemiBold", 52, r.scale_factor ()));
        m_loading_font.reset (new render::FontAtlas (
          r, "AvenirNext-Medium", 24, r.scale_factor ()));
        m_blob.load (r);

        platform::async (
          &MoppeGame::generate_thunk, &MoppeGame::finish_thunk, this);
      }

      static void generate_thunk (void* self) {
        ((MoppeGame*)self)->generate_world ();
      }
      static void finish_thunk (void* self) {
        ((MoppeGame*)self)->finish_setup ();
      }

      Vec3 choose_landscape_spawn () {
        // The generated landscape has no authored start.  Sample the
        // finished terrain for a dry, grassy, locally flat patch rather
        // than trusting the old fixed coordinate near the map corner.
        const Vec3& world_extent = extent_value (m_world.map_size);
        const float margin_x = 0.08f * world_extent[0];
        const float margin_z = 0.08f * world_extent[2];
        const float patch = 20.0f; // metres
        const float min_ground = meters_value (m_world.water_level) + 25.0f;
        const float max_ground = 0.32f * world_extent[1];

        std::uniform_real_distribution<float> random_x (
          margin_x, world_extent[0] - margin_x);
        std::uniform_real_distribution<float> random_z (
          margin_z, world_extent[2] - margin_z);

        Vec3 chosen;
        Vec3 fallback;
        int good_count = 0;
        float fallback_score = -1000000.0f;

        const auto standing_depth = [this, &world_extent] (float x, float z) {
          if (!m_standing_water)
            return 0.0f;
          const terrain::TerrainGrid& grid = m_standing_water->source_grid;
          const auto wrap = [] (float value, float period) {
            value = std::fmod (value, period);
            return value < 0.0f ? value + period : value;
          };
          const std::size_t gx =
            static_cast<std::size_t> (wrap (x, world_extent[0]) /
                                      grid.spacing_x_m ()) %
            m_standing_water->width ();
          const std::size_t gz =
            static_cast<std::size_t> (wrap (z, world_extent[2]) /
                                      grid.spacing_y_m ()) %
            m_standing_water->height ();
          return m_standing_water->water_depth.at (gx, gz) * world_extent[1];
        };

        for (int i = 0; i < 6000; ++i) {
          const float x = random_x (m_fx_rng);
          const float z = random_z (m_fx_rng);
          const float h = m_map.interpolated_height (x, z);
          const float hx0 = m_map.interpolated_height (x - patch, z);
          const float hx1 = m_map.interpolated_height (x + patch, z);
          const float hz0 = m_map.interpolated_height (x, z - patch);
          const float hz1 = m_map.interpolated_height (x, z + patch);
          const float low =
            std::min (h, std::min (std::min (hx0, hx1), std::min (hz0, hz1)));
          const float high =
            std::max (h, std::max (std::max (hx0, hx1), std::max (hz0, hz1)));
          const float relief = high - low;
          const float up = m_map.interpolated_normal (x, z)[1];
          const float lake_depth = std::max ({ standing_depth (x, z),
                                               standing_depth (x - patch, z),
                                               standing_depth (x + patch, z),
                                               standing_depth (x, z - patch),
                                               standing_depth (x, z + patch) });

          // Always retain the best fallback.  The large shore penalty makes
          // even an unusual generated map prefer dry ground over a flat seabed.
          const float shore_penalty = std::max (0.0f, min_ground - low) * 2.0f;
          const float alpine_penalty = std::max (0.0f, h - max_ground) * 0.03f;
          const float score = up * 20.0f - relief * 0.2f - shore_penalty -
                              alpine_penalty -
                              (lake_depth > 0.1f ? 10000.0f : 0.0f);
          if (score > fallback_score) {
            fallback_score = score;
            fallback = Vec3 (x, h + 1.2f, z);
          }

          if (lake_depth > 0.1f || low < min_ground || high > max_ground ||
              up < 0.94f || relief > 3.5f)
            continue;

          // Reservoir sampling chooses uniformly among all suitable sites,
          // so different generated worlds do not always start at the first
          // acceptable patch encountered.
          ++good_count;
          std::uniform_int_distribution<int> keep (1, good_count);
          if (keep (m_fx_rng) == 1)
            chosen = Vec3 (x, h + 1.2f, z);
        }

        return good_count > 0 ? chosen : fallback;
      }

      void generate_world () {
        MOPPE_PROFILE_THREAD ("World generation");
        MOPPE_PROFILE_ZONE ("MoppeGame::generate_world");
        // Exceptions must not escape the GCD block (std::terminate).
        // A world that failed to generate is a broken build or broken
        // inputs, not a state to idle in: log and exit with failure.
        try {
          generate_world_inner ();
        } catch (const std::exception& e) {
          std::cerr << "world generation failed: " << e.what () << std::endl;
          std::_Exit (-1);
        }
      }

      void generate_world_inner () {
        MOPPE_PROFILE_ZONE ("MoppeGame::generate_world_inner");
        if (m_terrain_lab_preview) {
          m_gen_stage = 3;
          const terrain::TerrainProgram program =
            terrain::make_geological_program (m_seed);
          map::TerrainEvaluator (m_map).evaluate (program);
        } else {
          // Reuse the automatic build/profile/seed cache when possible.
          // MOPPE_MAPCACHE=<file> remains an explicit experiment override.
          const char* cache_override = ::getenv ("MOPPE_MAPCACHE");
          const std::string automatic_cache =
            terrain_cache_path (m_world, m_generation_profile, m_seed);
          const char* cache =
            cache_override ? cache_override : automatic_cache.c_str ();
          if (cache && m_map.try_load_cache (cache)) {
            const std::size_t count =
              static_cast<std::size_t> (m_map.width ()) * m_map.height ();
            load_terrain_history (cache, count, m_terrain_history);
            const std::lock_guard<std::mutex> lock (m_loading_mutex);
            for (const std::vector<float>& snapshot : m_terrain_history)
              m_loading_snapshots.push_back (
                std::make_shared<const std::vector<float>> (snapshot));
            m_gen_stage = 7;
          } else {
            m_gen_stage = 3;
            const terrain::TerrainProgram program =
              terrain::make_world_program (m_seed, m_generation_profile);
            map::TerrainEvaluator evaluator (m_map);
            m_terrain_history.clear ();
            evaluator.evaluate (
              program,
              [this] (std::size_t, const terrain::TerrainTransform& transform) {
                const std::size_t count =
                  static_cast<std::size_t> (m_map.width ()) * m_map.height ();
                m_terrain_history.emplace_back (m_map.raw_heights (),
                                                m_map.raw_heights () + count);
                {
                  const std::lock_guard<std::mutex> lock (m_loading_mutex);
                  m_loading_snapshots.push_back (
                    std::make_shared<const std::vector<float>> (
                      m_terrain_history.back ()));
                }
                // Publish immutable stage inputs for the loading-screen
                // director rather than racing the GPU against the heightmap as
                // the next transform mutates it on this worker thread.
                if (std::holds_alternative<terrain::HydraulicErosion> (
                      transform))
                  m_gen_stage = 4;
              },
              [this] (std::size_t,
                      const terrain::TerrainTransform& transform,
                      int completed,
                      int total) {
                if (std::holds_alternative<terrain::HydraulicErosion> (
                      transform)) {
                  m_loading_work_done = completed;
                  m_loading_work_total = total;
                }
              },
              [this] (std::size_t completed, std::size_t total) {
                int observed = m_loading_source_done.load ();
                const int value = static_cast<int> (completed);
                while (observed < value &&
                       !m_loading_source_done.compare_exchange_weak (observed,
                                                                     value)) {}
                m_loading_source_total = static_cast<int> (total);
              });
            const std::size_t count =
              static_cast<std::size_t> (m_map.width ()) * m_map.height ();
            m_terrain_history.emplace_back (m_map.raw_heights (),
                                            m_map.raw_heights () + count);
            {
              const std::lock_guard<std::mutex> lock (m_loading_mutex);
              m_loading_snapshots.push_back (
                std::make_shared<const std::vector<float>> (
                  m_terrain_history.back ()));
            }
            if (cache) {
              m_map.save_cache (cache);
              save_terrain_history (cache, m_terrain_history);
            }
          }
        }
        if (m_terrain_history.empty ()) {
          const std::size_t count =
            static_cast<std::size_t> (m_map.width ()) * m_map.height ();
          m_terrain_history.emplace_back (m_map.raw_heights (),
                                          m_map.raw_heights () + count);
        }
        {
          const std::lock_guard<std::mutex> lock (m_loading_mutex);
          if (m_loading_snapshots.empty ())
            for (const std::vector<float>& snapshot : m_terrain_history)
              m_loading_snapshots.push_back (
                std::make_shared<const std::vector<float>> (snapshot));
        }
        m_gen_stage = 5;
        m_map.recompute_normals ();

        // The random world's sea and lakes are one priority-flood surface.
        // Keep this as a reading: terrain and erosion remain authoritative.
        if (!m_terrain_lab_preview) {
          const float sea_level = meters_value (m_world.water_level) /
                                  extent_value (m_world.map_size)[1];
          m_standing_water =
            terrain::analyze_standing_water (m_map.terrain_view (), sea_level);
          m_lake_census = terrain::census_lakes (*m_standing_water);
          {
            // A one-line hydrology reading at load: pond explosions from
            // erosion regressions show up here before any capture does.
            std::size_t wet = 0;
            for (const terrain::WaterBody& body : m_lake_census->bodies)
              wet += terrain::count_value (body.cells);
            std::cerr << "standing water: " << m_lake_census->bodies.size ()
                      << " bodies, " << wet << " wet cells\n";
          }
          m_drainage = terrain::analyze_wet_drainage (
            m_map.terrain_view (), *m_standing_water, *m_lake_census);
          m_water_network = terrain::analyze_water_network (
            *m_standing_water, *m_lake_census, *m_drainage);
          m_rivers = terrain::extract_river_network (
            *m_standing_water,
            *m_lake_census,
            *m_drainage,
            visible_river_minimum_area (m_drainage->source_grid));
          if (m_water_shot) {
            m_water_inspection = choose_water_inspection (*m_water_shot,
                                                          m_map,
                                                          *m_standing_water,
                                                          *m_lake_census,
                                                          *m_drainage,
                                                          *m_rivers);
            if (!m_water_inspection)
              throw std::runtime_error (
                "no " + std::string (water_shot_name (*m_water_shot)) +
                " available for water screenshot");
            std::cerr << "water screenshot: " << water_shot_name (*m_water_shot)
                      << " cell=" << m_water_inspection->cell
                      << " score=" << m_water_inspection->score << '\n';
          }
        }

        if (!m_terrain_lab_preview) {
          m_vegetation.prepare (m_map, m_world);
          if (m_graphics.vegetation)
            m_vegetation.generate (
              m_map, m_world, Vegetation::population_for (m_world));
          m_stars.generate (m_map, m_world, 80);
        }
        m_gen_stage = 6;
      }

      // The recipe behind the current map: entering the lab shows the
      // world's own pipeline instead of rebuilding a bare geological field.
      terrain::TerrainProgram lab_program () const {
        if (m_terrain_lab_preview)
          return terrain::make_geological_program (m_seed);
        return terrain::make_world_program (m_seed, m_generation_profile);
      }

      void finish_setup () {
        render::Renderer& r = *m_renderer;

        m_terrain.setup (r, m_map, m_world, m_graphics);
        // Rivers are painted into the water sheets below; the optional ribbon
        // meshes remain independently selectable for comparison captures.
        if (m_rivers && m_graphics.river_ribbons)
          m_river_surface.rebuild (r,
                                   m_map,
                                   *m_standing_water,
                                   *m_lake_census,
                                   *m_drainage,
                                   *m_rivers);
        if (!m_terrain_lab_preview) {
          if (m_graphics.terrain_shadows)
            m_terrain.render_shadow (
              r, m_map, sun_direction_for (m_graphics.sun_height));
          if (m_graphics.vegetation)
            m_vegetation.load (r);
        }
        render::OceanSetup ocean;
        ocean.level = meters_value (m_world.water_level);
        const Vec3& world_extent = extent_value (m_world.map_size);
        ocean.center = Vec3 (world_extent[0] / 2, 0, world_extent[2] / 2);
        ocean.half_extent = 5500.0f;
        ocean.cells = 300;
        std::vector<float> water_levels;
        std::vector<float> water_flow;
        if (m_standing_water && m_drainage && m_rivers) {
          // The complete waterscape painted onto the terrain lattice:
          // lakes, sea, and rivers in one surface sheet, per-body wave
          // amplitude, and a flow arrow in every wet cell. Rivers render
          // through the same lattice water pass as the lakes; the flow
          // sheet is what carries their motion.
          const terrain::WaterSheets sheets =
            terrain::paint_watercourses (m_map.terrain_view (),
                                         *m_standing_water,
                                         *m_lake_census,
                                         *m_drainage,
                                         *m_rivers);
          water_levels.resize (2 * static_cast<std::size_t> (m_map.width ()) *
                               m_map.height ());
          water_flow.resize (water_levels.size ());
          const std::span<const float> unique = sheets.surface.values ();
          const std::size_t unique_width = m_standing_water->width ();
          const std::size_t unique_height = m_standing_water->height ();
          for (int y = 0; y < m_map.height (); ++y)
            for (int x = 0; x < m_map.width (); ++x) {
              const std::size_t cell =
                (static_cast<std::size_t> (y) % unique_height) * unique_width +
                static_cast<std::size_t> (x) % unique_width;
              const std::size_t out =
                2 * (static_cast<std::size_t> (y) * m_map.width () + x);
              water_levels[out] = unique[cell];
              water_levels[out + 1] = sheets.amplitude[cell];
              water_flow[out] = sheets.flow[2 * cell];
              water_flow[out + 1] = sheets.flow[2 * cell + 1];
            }
        }
        r.set_ocean (ocean, water_levels);
        r.set_water_flow (water_flow);

        if (m_standing_water && m_drainage) {
          // Ground moisture from the hydrology: vegetation reads it for
          // blade height, color, and density.
          const terrain::ScalarRaster moisture = terrain::analyze_moisture (
            *m_standing_water, *m_lake_census, *m_drainage);
          std::vector<float> expanded (
            static_cast<std::size_t> (m_map.width ()) * m_map.height ());
          const std::span<const float> unique = moisture.values ();
          const std::size_t unique_width = m_standing_water->width ();
          const std::size_t unique_height = m_standing_water->height ();
          for (int y = 0; y < m_map.height (); ++y)
            for (int x = 0; x < m_map.width (); ++x)
              expanded[static_cast<std::size_t> (y) * m_map.width () + x] =
                unique[(static_cast<std::size_t> (y) % unique_height) *
                         unique_width +
                       static_cast<std::size_t> (x) % unique_width];
          r.set_terrain_moisture (expanded);
        }

        m_vehicle.set_water_level (m_world.water_level);
        m_car.set_water_level (m_world.water_level);
        m_vehicle.set_obstacles (&m_obstacles);
        m_car.set_obstacles (&m_obstacles);

        if (!m_terrain_lab_preview) {
          m_spawn_position = choose_landscape_spawn ();
          m_vehicle.reset (m_spawn_position);

          std::uniform_real_distribution<float> heading (0.0f, 2.0f * 3.14159f);
          const float a = heading (m_fx_rng);
          m_vehicle.set_heading (Vec3 (std::sin (a), 0, std::cos (a)));
        }
        if (m_water_inspection)
          m_camera.place (m_water_inspection->eye, m_water_inspection->target);

        m_setup_complete = true;
        remember_seed (m_world, m_generation_profile, m_seed);
        if (::getenv ("MOPPE_REGENERATE_ONCE") &&
            !m_automated_regeneration_done) {
          m_automated_regeneration_done = true;
          regenerate_world ();
        }
      }

      // -- simulation --------------------------------------------------

      void tick (float dt) override {
        MOPPE_PROFILE_ZONE ("MoppeGame::tick");
        if (m_benchmark) {
          dt = GRAPHICS_BENCHMARK_DT;
          if (m_benchmark_submitted) {
            m_benchmark_measured = false;
            if (m_renderer->benchmark_complete () &&
                !m_benchmark_results_written) {
              m_renderer->write_benchmark_results ();
              m_benchmark_results_written = true;
              platform::request_quit ();
            }
            return;
          }
          if (m_benchmark_checkpoint) {
            const int epoch_frames =
              m_benchmark->settle_frames + m_benchmark->measured_frames;
            if (m_benchmark_frame == epoch_frames) {
              ++m_benchmark_epoch;
              const int configurations =
                1 << graphics_benchmark_dimension_count ();
              if (m_benchmark_epoch == configurations) {
                m_benchmark_submitted = true;
                m_benchmark_measured = false;
                platform::set_window_title (
                  "Moppe benchmark - finishing GPU samples");
                return;
              }
              restore (*m_benchmark_checkpoint);
              m_renderer->reset_temporal_state ();
              m_graphics = m_benchmark_baseline;
              m_benchmark_partition_mask = gray_code (m_benchmark_epoch);
              m_benchmark_mask = apply_graphics_benchmark_mask (
                m_graphics, m_benchmark_partition_mask);
              m_benchmark_frame = 0;
              update_benchmark_title ();
            }
            controls (benchmark_input (m_benchmark_frame));
            m_benchmark_measured =
              m_benchmark_frame >= m_benchmark->settle_frames;
          } else {
            controls (benchmark_input (m_benchmark_prelude_frame));
            m_benchmark_measured = false;
          }
        }
        if (m_benchmark) {
          MOPPE_PROFILE_PLOT ("benchmark.mask", m_benchmark_mask);
          MOPPE_PROFILE_PLOT ("benchmark.partition_mask",
                              m_benchmark_partition_mask);
          MOPPE_PROFILE_PLOT ("benchmark.epoch", m_benchmark_epoch);
          MOPPE_PROFILE_PLOT ("benchmark.logical_frame", m_benchmark_frame);
          MOPPE_PROFILE_PLOT ("benchmark.measured", m_benchmark_measured);
        }
        m_frame_time = dt;
        if (!m_ready || m_game_over)
          return;

        m_total_time += dt;
        const float total_time = m_total_time;

        // Weather remains part of the world while actors are paused.  This
        // also initializes the shared horizon color when the game starts
        // directly in Terrain Lab.
        float cloudiness =
          std::sin (total_time * 0.0003f) * 0.4f + 0.5f +
          0.3f * std::pow (std::sin (total_time * 0.0008f), 2.0f) +
          std::sin (total_time * 0.02f) * 0.05f;
        clamp (cloudiness, 0.0f, 1.0f);
        m_cloudiness = cloudiness;

        // Fog stays mostly sky-blue.  Directional warmth is added in
        // the shaders only when looking toward the sun.
        const DisplayColor horizon = horizon_color_for (m_graphics.sun_height);
        m_fog = mix_display (horizon, DisplayColor (0.90f, 0.94f, 1.0f), 0.18f);

        // Terrain inspection pauses actors and vehicle physics, but keeps the
        // visual clock, weather, and fog alive so sky and water remain a
        // useful frame of reference around the heightmap.
        if (m_terrain_lab.active ()) {
          m_terrain_lab.tick (dt);
          return;
        }

        // Screenshot autopilot for headless verification: rides in a
        // lazy arc with periodic boost-assisted leaps.
        static const bool demo = ::getenv ("MOPPE_DEMO") != 0;
        if (demo && !m_water_inspection) {
          input_go (1.0f);
          input_turn (0.35f * std::sin (total_time * 0.25f));
          input_boost (std::fmod (total_time, 11.0f) < 1.35f ? 1.0f : 0.0f);
        }

        m_vehicle.update (seconds (dt));
        if (m_car_exists)
          m_car.update (seconds (dt));
        if (m_mode == M_FOOT)
          m_walker.update (seconds (dt), m_map, m_obstacles, m_world);

        const Vec3 vpos = (m_mode == M_FOOT)  ? m_walker.position ()
                          : (m_mode == M_CAR) ? m_car.position ()
                                              : m_vehicle.position ();
        mov::Vehicle& av = active_vehicle ();

        // Parked vehicles' impacts shouldn't linger until remount.
        if (m_mode != M_BIKE) {
          m_vehicle.pop_impact ();
          m_vehicle.pop_fall_drop ();
        }
        if (m_car_exists && m_mode != M_CAR) {
          m_car.pop_impact ();
          m_car.pop_fall_drop ();
        }

        const bool in_water =
          vpos[1] < meters_value (m_world.water_level) + 1.0f;
        const bool driving = (m_mode != M_FOOT);

        // Long jumps become score events after three seconds. Keep the last
        // airborne time locally because Vehicle clears its timer on touchdown.
        if (driving && av.airtime () > 0.0f) {
          m_jump_airtime = av.airtime ();
          m_landed_age += dt;
        } else {
          if (driving && m_jump_airtime >= 3.0f) {
            m_landed_airtime = m_jump_airtime;
            m_landed_points =
              (int)std::round (100.0f * m_jump_airtime * m_jump_airtime);
            m_score += m_landed_points;
            m_landed_age = 0.0f;
          }
          m_jump_airtime = 0.0f;
          m_landed_age += dt;
        }
        const DisplayColor dust_color (0.60f, 0.52f, 0.40f);
        const DisplayColor clod_color (0.42f, 0.34f, 0.24f);
        const DisplayColor spray_color (0.85f, 0.92f, 1.0f);
        const Vec3 fwd = av.orientation ();
        const Vec3 rear_wheel = vpos - fwd * 1.4f + Vec3 (0, -0.7f, 0);

        // Drift kicks up dirt from the rear wheel (or spray).
        if (driving && av.grounded () && av.drift_speed () > 6.0f) {
          int n = std::min (4, (int)(av.drift_speed () * 0.2f));
          m_dust.emit (position (rear_wheel),
                       velocity (av.velocity () * 0.15f),
                       n,
                       in_water ? spray_color : dust_color);
        }

        // Roost: hard throttle sprays an arc of dirt clods backward
        // off the rear knobby, heaviest when the engine is winning
        // against the ground (launches, corner exits).
        if (driving && av.grounded () && !in_water && av.thrust () > 0.6f) {
          const float speed = length (av.velocity ());
          const float slip = scalar_value (av.thrust ()) *
                             (1.0f - std::min (1.0f, speed / 30.0f));
          if (slip > 0.15f) {
            Dust::Style roost;
            roost.size = 0.45f * u::m;
            roost.lifetime = 0.9f * u::s;
            roost.downward_acceleration =
              12.0f * isq::acceleration[u::m / pow<2> (u::s)];
            roost.spread = 0.5f * one;
            m_dust.emit (position (rear_wheel),
                         velocity (fwd * (-6.0f - 14.0f * slip) +
                                   Vec3 (0, 3.5f + 3.0f * slip, 0)),
                         1 + (int)(slip * 3.0f),
                         clod_color,
                         roost);
          }
        }

        // Jet embers: hot additive sparks streaming out of the
        // nozzles while the jets burn, arcing down and dying fast.
        if (driving && av.boost_level () > 0.05f) {
          std::uniform_real_distribution<float> chance (0.0f, 1.0f);
          // Poisson emission: an event rate scaled by the jet level,
          // integrated over the frame into a probability.
          const probability_t spark (34.0f / u::s * av.boost_level () *
                                     (dt * u::s));
          if (chance (m_fx_rng) < scalar_value (spark)) {
            Dust::Style ember;
            ember.size = 0.15f * u::m;
            ember.lifetime = 0.45f * u::s;
            ember.downward_acceleration =
              6.0f * isq::acceleration[u::m / pow<2> (u::s)];
            ember.spread = 0.3f * one;
            ember.additive = true;
            m_dust.emit (position (vpos - fwd * 0.5f + Vec3 (0, -0.5f, 0)),
                         velocity (av.velocity () * 0.5f - fwd * 2.0f +
                                   Vec3 (0, -4.0f, 0)),
                         1,
                         DisplayColor (1.0f, 0.55f, 0.18f),
                         ember);
          }
        }

        // Exhaust smoke: faint gray puffs that rise off the muffler
        // while the throttle is open.
        if (driving && abs (av.thrust ()) > 0.3f && m_mode == M_BIKE) {
          std::uniform_real_distribution<float> chance (0.0f, 1.0f);
          const probability_t puff (14.0f / u::s * (dt * u::s));
          if (chance (m_fx_rng) < scalar_value (puff)) {
            Dust::Style smoke;
            smoke.size = 0.35f * u::m;
            smoke.lifetime = 0.8f * u::s;
            smoke.downward_acceleration =
              -2.5f * isq::acceleration[u::m / pow<2> (u::s)]; // buoyant
            smoke.spread = 0.25f * one;
            m_dust.emit (position (vpos - fwd * 1.2f + Vec3 (0, -0.4f, 0)),
                         velocity (av.velocity () * 0.25f),
                         1,
                         DisplayColor (0.45f, 0.45f, 0.48f),
                         smoke);
          }
        }

        // Wading fast throws up a bow wave.
        if (driving && in_water && length (av.velocity ()) > 15.0f)
          m_dust.emit (position (vpos + Vec3 (0, -0.5f, 0)),
                       velocity (av.velocity () * 0.3f),
                       3,
                       spray_color);

        // Hard landings shake the camera and burst dirt outward:
        // a low pancake of dust plus a ring of ballistic clods.
        const float impact = driving ? av.pop_impact () : 0.0f;
        if (impact > 8.0f) {
          m_shake = std::min (0.28f, 0.010f * impact);
          m_shake_time = 0.0f;
          m_dust.emit (position (vpos + Vec3 (0, -0.7f, 0)),
                       velocity (av.velocity () * 0.2f),
                       12,
                       in_water ? spray_color : dust_color);
          if (!in_water) {
            Dust::Style burst;
            burst.size = 0.5f * u::m;
            burst.lifetime = 1.1f * u::s;
            burst.downward_acceleration =
              10.0f * isq::acceleration[u::m / pow<2> (u::s)];
            burst.spread = 1.4f * one;
            m_dust.emit (position (vpos + Vec3 (0, -0.6f, 0)),
                         velocity (av.velocity () * 0.15f +
                                   Vec3 (0, 2.0f + 0.15f * impact, 0)),
                         (int)std::min (10.0f, impact * 0.5f),
                         clod_color,
                         burst);
          }
        }

        // Crashes hurt; health trickles back slowly.  Falls from
        // above a hundred meters are simply fatal -- house rule.
        if (impact > 9.0f)
          m_health -= (impact - 9.0f) * 4.5f;
        if (driving && av.pop_fall_drop () > 100.0f)
          m_health = 0.0f;
        m_health = std::min (100.0f, m_health + 1.5f * dt);
        if (m_health <= 0.0f) {
          m_dust.emit (position (vpos),
                       velocity (Vec3 (0, 6, 0)),
                       40,
                       DisplayColor (1.0f, 0.5f, 0.1f));
          --m_lives;
          if (m_lives <= 0) {
            m_game_over = true;
          } else {
            // Halfway through the hearts, the game offers its
            // sympathies out loud.
            if (m_lives == 5)
              platform::say ("Ouchies. That hurts.");

            // Respawn where you crashed, upright on the ground.
            // (Deaths used to teleport you 600 m above the map
            // corner -- it read as falling through the cosmos.)
            const float ground = m_map.interpolated_height (vpos[0], vpos[2]);
            av.reset (Vec3 (vpos[0], ground + 1.2f, vpos[2]));
            m_health = 100.0f;
            m_shake = 1.0f;
            m_shake_time = 0.0f;
          }
        }

        // Star pickups sparkle gold and top up fuel and boost reserves.
        {
          const int picked = m_stars.update (vpos, m_total_time, dt);
          if (picked > 0) {
            Dust::Style sparkle;
            sparkle.size = 0.38f * u::m;
            sparkle.lifetime = 0.85f * u::s;
            sparkle.downward_acceleration =
              -1.5f * isq::acceleration[u::m / pow<2> (u::s)];
            sparkle.spread = 1.7f * one;
            sparkle.additive = true;
            m_dust.emit (position (m_stars.last_pos ()),
                         velocity (Vec3 (0, 4, 0)),
                         32,
                         DisplayColor (1.0f, 0.72f, 0.12f),
                         sparkle);
            Dust::Style flash;
            flash.size = 0.9f * u::m;
            flash.lifetime = 0.35f * u::s;
            flash.spread = 0.25f * one;
            flash.additive = true;
            m_dust.emit (position (m_stars.last_pos ()),
                         velocity (Vec3 ()),
                         5,
                         DisplayColor (1.0f, 0.95f, 0.55f),
                         flash);
            m_fuel = std::min (100.0f, m_fuel + 25.0f * picked);
            av.replenish_boost (0.25f * picked);
          }
        }

        // Fuel: the throttle burns it; an empty tank limps along at
        // a third power (never fully stranded).
        if (driving) {
          m_fuel = std::max (
            0.0f, m_fuel - scalar_value (abs (av.thrust ())) * 0.9f * dt);
          m_odometer += length (av.velocity ()) * dt;

          const float want =
            m_go_input * ((m_fuel <= 0.5f && m_go_input > 0) ? 0.3f : 1.0f);
          av.set_thrust (want);
        }

        m_dust.update (seconds (dt));
        m_shake_time += dt;
        m_shake *= decay (7.0f / u::s, dt * u::s);

        if (m_cam_mode == CAM_HELMET) {
          // Ride inside the rider's head; lightly smoothed so
          // terrain bumps don't rattle the eyeballs.
          Vec3 eye, look;
          if (m_mode == M_FOOT) {
            eye =
              m_walker.position () + Vec3 (0, 1.55f / m_landscape_scale_y, 0);
            look = m_walker.heading ();
          } else {
            eye = av.position () + Vec3 (0, 0.95f / m_landscape_scale_y, 0) +
                  av.orientation () * (0.4f / m_landscape_scale_x);
            look = av.orientation ();
          }
          m_fp_eye = m_fp_eye + (eye - m_fp_eye) *
                                  smoothing_alpha (25.0f / u::s, dt * u::s);
          m_camera.place (m_fp_eye, m_fp_eye + look * 10.0f);
        } else {
          m_camera.set_landscape_scale (m_landscape_scale_x,
                                        m_landscape_scale_y);
          const float flip = (m_cam_mode == CAM_FRONT) ? -1.0f : 1.0f;
          if (m_mode == M_FOOT)
            m_camera.update (position (m_walker.position () +
                                       Vec3 (0, 1.0f / m_landscape_scale_y, 0)),
                             m_walker.heading () * flip,
                             velocity (Vec3 ()),
                             seconds (dt));
          else
            m_camera.update (av.physical_position (),
                             av.orientation () * flip,
                             av.physical_velocity (),
                             seconds (dt));
          m_camera.limit (m_map);
        }
        if (m_water_inspection) {
          m_camera.place (m_water_inspection->eye, m_water_inspection->target);
          m_camera.limit (m_map);
        }

        // Speed widens the field of view a touch.
        {
          const float kmh = driving ? length (av.velocity ()) * 3.6f : 0.0f;
          const float k =
            std::min (1.0f, std::max (0.0f, (kmh - 70.0f) / 180.0f));
          m_fov_k += (k - m_fov_k) * smoothing_alpha (5.0f / u::s, dt * u::s);
        }

        if (m_benchmark) {
          if (m_benchmark_checkpoint) {
            ++m_benchmark_frame;
          } else if (++m_benchmark_prelude_frame ==
                     m_benchmark->prelude_frames) {
            m_benchmark_checkpoint = state ();
            m_renderer->reset_temporal_state ();
            m_benchmark_partition_mask = gray_code (0);
            m_graphics = m_benchmark_baseline;
            m_benchmark_mask = apply_graphics_benchmark_mask (
              m_graphics, m_benchmark_partition_mask);
            m_benchmark_frame = 0;
            update_benchmark_title ();
            std::cerr << "moppe: graphics benchmark: "
                      << (1 << graphics_benchmark_dimension_count ())
                      << " configurations, " << m_benchmark->settle_frames
                      << " settle + " << m_benchmark->measured_frames
                      << " measured frames each\n";
          }
        }
      }

      // -- rendering ---------------------------------------------------

      void render (render::Renderer& r) override {
        MOPPE_PROFILE_FRAME ();
        MOPPE_PROFILE_ZONE ("MoppeGame::render");
        if (!m_ready) {
          render_loading (r);
          return;
        }
        if (m_game_over) {
          render_game_over (r);
          return;
        }

        render::FrameParams fp;
        const float aspect =
          (float)r.width_pts () / std::max (1, r.height_pts ());
        const bool terrain_lab = m_terrain_lab.active ();
        const float fov =
          terrain_lab || m_water_inspection ? 70.0f : 100.0f + 9.0f * m_fov_k;
        fp.proj = Mat4::perspective_reversed (
          fov * u::deg,
          aspect,
          std::clamp (0.5f /
                        std::max (m_landscape_scale_x, m_landscape_scale_y),
                      0.02f,
                      0.5f),
          9000.0f);

        // Hard landings produce a brief continuous vibration. Randomizing the
        // rotations each frame looked like violent camera teleportation.
        Mat4 view =
          terrain_lab ? m_terrain_lab.view_matrix () : m_camera.view_matrix ();
        if (!terrain_lab && !m_water_inspection && m_shake > 0.005f) {
          const Vec3 cam = m_camera.position ();
          const float ground = m_map.interpolated_height (cam[0], cam[2]);
          const float clearance = cam[1] - ground;
          const float room =
            std::min (1.0f, std::max (0.0f, (clearance - 2.0f) / 8.0f));

          const float pulse = m_shake * room;
          const float roll =
            pulse * std::sin (2.0f * PI * 15.0f * m_shake_time);
          const float pitch =
            pulse * 0.55f * std::sin (2.0f * PI * 19.0f * m_shake_time + 0.7f);
          view = view * Mat4::rotation (roll * u::deg, Vec3 (0, 0, 1)) *
                 Mat4::rotation (pitch * u::deg, Vec3 (1, 0, 0));
        }
        fp.view = view;

        const Vec3 cam =
          terrain_lab ? m_terrain_lab.position () : m_camera.position ();
        fp.camera_pos = cam;
        fp.cam_right = Vec3 (view.m[0], view.m[4], view.m[8]);
        fp.cam_up = Vec3 (view.m[1], view.m[5], view.m[9]);
        fp.cam_forward = Vec3 (-view.m[2], -view.m[6], -view.m[10]);
        // The lab's plane views render the game's own sky and haze; the
        // torus is an abstract inspection object on a dark backdrop.
        fp.clear_color = terrain_lab && m_terrain_lab.torus_view ()
                           ? DisplayColor (0.012f, 0.016f, 0.022f)
                           : m_fog;
        const attenuation_t scene_fog =
          terrain_lab ? m_terrain_lab.scene_fog (m_world.fog_scale)
                      : m_world.fog_scale;
        fp.fog_scale = attenuation_value (scene_fog);
        fp.sun_dir = sun_direction_for (m_graphics.sun_height);
        sun_light_colors (
          m_graphics.sun_height, fp.sun_diffuse, fp.sun_specular);
        fp.sun_specular = scale_display (fp.sun_specular, 0.5f);
        const float daylight = daylight_for (m_graphics.sun_height);
        // Open terrain receives substantial skylight even when a mountain
        // blocks the sun. Keep the fill cool so cast shadows retain shape and
        // color instead of collapsing into near-black silhouettes.
        fp.ambient = scale_display (DisplayColor (0.39f, 0.43f, 0.49f),
                                    0.35f + 0.65f * daylight);
        if (terrain_lab) {
          // Keep the same time of day while giving the high overview a small
          // legibility lift after automatic exposure.
          fp.ambient = scale_display (fp.ambient, 1.15f);
          fp.exposure_bias = 0.88f;
        }
        fp.time = m_total_time;
        fp.scene_scale = m_graphics.scene_scale;
        fp.render_scale_override = m_graphics.render_scale_override;
        fp.bloom = m_graphics.bloom;
        fp.auto_exposure = m_graphics.auto_exposure;
        fp.lens_flare = m_graphics.lens_flare;
        fp.profile = true;
        fp.benchmark_mask = m_benchmark_mask;
        fp.benchmark_partition_mask = m_benchmark_partition_mask;
        fp.benchmark_epoch = m_benchmark_epoch;
        fp.benchmark_frame = m_benchmark_frame > 0 ? m_benchmark_frame - 1 : 0;
        fp.benchmark_measured = m_benchmark_measured;

        // Lens-flare occlusion: march toward the sun through the
        // heightmap; any ridge above the ray kills the flare.  Cloud
        // cover and submersion dim it; smoothed so it doesn't blink
        // as hills sweep past the sun.
        {
          float vis = 1.0f;
          if (cam[1] < meters_value (m_world.water_level))
            vis = 0.0f;
          else {
            for (int i = 1; i <= 40; ++i) {
              const float t = 90.0f * i;
              const Vec3 p = cam + fp.sun_dir * t;
              if (!m_map.in_bounds (p[0], p[2]))
                break;
              if (m_map.interpolated_height (p[0], p[2]) > p[1]) {
                vis = 0.0f;
                break;
              }
            }
          }
          vis *= 1.0f - 0.65f * m_cloudiness;
          m_flare += (vis - m_flare) * 0.12f;
          fp.sun_visibility = terrain_lab ? 0.0f : m_flare;
        }

        static const int screenshot_delay = [] {
          if (const char* frames = ::getenv ("MOPPE_SCREENSHOT_FRAMES"))
            return std::max (1, ::atoi (frames));
          return 30;
        }();
        const bool captured = !m_screenshot_path.empty () &&
                              ++m_screenshot_frames >= screenshot_delay;
        if (captured) {
          if (m_water_inspection)
            std::cerr << "water screenshot camera: eye=" << m_camera.position ()
                      << " target=" << m_water_inspection->target << '\n';
          r.request_screenshot (m_screenshot_path);
        }
        if (!r.begin_frame (fp))
          return;

        FrameEnv env;
        env.fog_color = m_fog;
        env.fog_scale = scene_fog;
        env.sun_dir = fp.sun_dir;
        env.camera_pos = position (cam);
        env.cam_right = fp.cam_right;
        env.cam_up = fp.cam_up;
        env.cam_forward = fp.cam_forward;
        env.time = seconds (m_total_time);

        const auto draw_world_sky = [&] {
          render::SkyParams sky;
          sky.time = m_total_time;
          sky.sun_height = m_graphics.sun_height;
          // A world-shaping overview should keep the game world's moving sky,
          // without letting a passing front hide the land being edited.
          sky.cloudiness =
            terrain_lab ? std::min (m_cloudiness, 0.35f) : m_cloudiness;
          sky.sun_dir = fp.sun_dir;
          sky.fog_color = m_fog;
          r.draw_sky (sky);
        };

        // At this extreme altitude, drawing the far-plane dome after terrain
        // exposes depth precision at the horizon.  Paint it first in the lab;
        // terrain then covers it deterministically.  Gameplay retains the
        // cheaper depth-culled order below.
        if (terrain_lab && !m_terrain_lab.torus_view ())
          draw_world_sky ();

        // Terrain first, chunk-culled to the haze horizon.
        m_terrain.render (
          r,
          cam,
          terrain_lab ? m_terrain_lab.forward () : m_camera.forward (),
          terrain_lab ? 12000.0f
                      : 3.0f / attenuation_value (m_world.fog_scale));

        // Sky AFTER the terrain: depth testing kills the expensive
        // cloud shader wherever terrain covers it.
        if (!terrain_lab)
          draw_world_sky ();

        if (!terrain_lab && !m_water_inspection) {
          // The world draw list, in the GL build's draw order.  Terrain lab
          // deliberately hides every placed object so generator differences are
          // not confused with stale vegetation or actor positions.
          m_world_dl.clear ();
          if (m_graphics.vegetation || m_graphics.grass)
            m_vegetation.render (r,
                                 env,
                                 m_graphics.vegetation,
                                 m_graphics.grass ? m_graphics.grass_density
                                                  : 0.0f,
                                 landscape_visual_scale ());

          // Soft blob shadows under the movers.
          m_blob.draw (m_world_dl, m_map, m_vehicle.position (), 2.2f);
          if (m_car_exists)
            m_blob.draw (m_world_dl, m_map, m_car.position (), 2.9f);
          if (m_mode == M_FOOT)
            m_blob.draw (m_world_dl,
                         m_map,
                         m_walker.position () + Vec3 (0, 0.5f, 0),
                         0.8f);

          // In helmet cam you ARE the rider: don't draw yourself.
          const bool helmet = (m_cam_mode == CAM_HELMET);
          if (!(helmet && m_mode == M_BIKE))
            render_vehicle (r,
                            m_world_dl,
                            m_vehicle,
                            m_total_time,
                            landscape_visual_scale ());
          if (m_car_exists && !(helmet && m_mode == M_CAR))
            render_vehicle (
              r, m_world_dl, m_car, m_total_time, landscape_visual_scale ());
          if (m_mode == M_FOOT && !helmet)
            m_walker.render (
              m_world_dl, m_total_time, landscape_visual_scale ());

          r.draw_list (m_world_dl);

          // Additive glow after the solid list, so it blends over
          // everything already drawn: exhaust and jump-jet flames, then
          // the star pickups' halos.
          if (m_graphics.vehicle_effects && !(helmet && m_mode == M_BIKE))
            render_vehicle_flames (
              r, m_vehicle, m_total_time, landscape_visual_scale ());
          if (m_graphics.vehicle_effects && m_car_exists &&
              !(helmet && m_mode == M_CAR))
            render_vehicle_flames (
              r, m_car, m_total_time, landscape_visual_scale ());
          if (m_graphics.star_effects)
            m_stars.render (r, env);
        }

        // Translucent water late so the seabed and fish show
        // through; dust last so spray sits atop the surface.
        if (terrain_lab)
          m_terrain_lab.render_rivers (r, cam);
        else
          m_river_surface.draw (r, cam);

        // The lab keeps the game's painted water while the map is the
        // game's own; a rebuilt map invalidates the water sheets, so
        // they disappear until the lab's own analysis draws ribbons.
        const bool draw_ocean =
          m_graphics.ocean && (!terrain_lab || (!m_terrain_lab.torus_view () &&
                                                m_terrain_lab.map_pristine ()));
        if (draw_ocean) {
          render::OceanParams ocean;
          ocean.time = m_total_time;
          ocean.fog_color = m_fog;
          ocean.fog_scale = attenuation_value (scene_fog);
          if (m_world.toroidal ()) {
            const Vec3& world_extent = extent_value (m_world.map_size);
            const Vec3 center (
              0.5f * world_extent[0], 0, 0.5f * world_extent[2]);
            ocean.world_offset[0] = cam[0] - center[0];
            ocean.world_offset[2] = cam[2] - center[2];
          }
          r.draw_ocean (ocean);
        }

        if (terrain_lab)
          m_terrain_lab.render_droplet (r, cam);

        if (!terrain_lab && m_graphics.particles) {
          m_dust.render (r);
        }

        // Post effects.
        if (!terrain_lab && cam[1] < meters_value (m_world.water_level))
          r.apply_underwater (m_total_time);
        if (!terrain_lab) {
          const float kmh = (m_mode == M_FOOT)
                              ? 0.0f
                              : length (active_vehicle ().velocity ()) * 3.6f;
          float k = (kmh - 90.0f) / 160.0f;
          clamp (k, 0.0f, 1.0f);
          if (m_graphics.motion_blur && k > 0.01f)
            r.apply_motion_blur (k);
        }

        // HUD, kept inside the safe area (notch / home indicator).
        m_hud_dl.clear ();
        const platform::Insets si = platform::safe_insets ();
        m_hud_dl.translate (si.left, si.top, 0);
        const int hud_width = r.width_pts () - (int)(si.left + si.right);
        const int hud_height = r.height_pts () - (int)(si.top + si.bottom);
        if (terrain_lab) {
          m_terrain_lab.draw (m_hud_dl, hud_width, hud_height);
        } else if (!m_water_inspection) {
          HudState hs;
          hs.speed_kmh = (m_mode == M_FOOT)
                           ? 0.0f
                           : length (active_vehicle ().velocity ()) * 3.6f;
          hs.fuel = m_fuel;
          hs.boost_ready01 =
            (m_mode == M_FOOT) ? 1.0f : active_vehicle ().boost_charge ();
          hs.health01 = m_health / 100.0f;
          hs.odometer_m = (float)m_odometer;
          hs.lives = m_lives;
          hs.stars = m_stars.collected ();
          hs.score = m_score;
          hs.airtime_s = m_jump_airtime;
          hs.landed_airtime_s = m_landed_airtime;
          hs.landed_points = m_landed_points;
          hs.landed_age_s = m_landed_age;
          hs.on_foot = (m_mode == M_FOOT);
          hs.frame_time_s = m_frame_time;
          const Vec3 heading = m_mode == M_FOOT
                                 ? m_walker.heading ()
                                 : active_vehicle ().orientation ();
          hs.heading_radians = std::atan2 (heading[0], heading[2]);
          m_hud.draw (m_hud_dl, hs, hud_width, hud_height);
          if (m_game_ui_open) {
            m_game_ui_slider_x = std::max (44.0f, hud_width - 364.0f);
            const UiRect panel { m_game_ui_slider_x - 20, 24, 360, 224 };
            const UiRect horizontal { m_game_ui_slider_x, 82, 320, 64 };
            const UiRect vertical { m_game_ui_slider_x, 156, 320, 64 };
            std::ostringstream horizontal_label;
            horizontal_label << "LANDSCAPE WIDTH  " << std::fixed
                             << std::setprecision (2) << m_landscape_scale_x
                             << 'x';
            std::ostringstream vertical_label;
            vertical_label << "LANDSCAPE HEIGHT  " << std::fixed
                           << std::setprecision (2) << m_landscape_scale_y
                           << 'x';
            m_game_ui.begin (m_hud_dl);
            m_game_ui.panel (m_hud_dl,
                             panel.x,
                             panel.y,
                             panel.width,
                             panel.height,
                             "WORLD FEEL");
            m_game_ui.friendly_slider (
              m_hud_dl,
              horizontal,
              horizontal_label.str (),
              "SMALLER",
              "LARGER",
              landscape_scale_normalized (m_landscape_scale_x),
              horizontal.contains (m_pointer_x, m_pointer_y),
              m_game_ui_dragging_axis == 1);
            m_game_ui.friendly_slider (
              m_hud_dl,
              vertical,
              vertical_label.str (),
              "LOWER",
              "TALLER",
              landscape_scale_normalized (m_landscape_scale_y),
              vertical.contains (m_pointer_x, m_pointer_y),
              m_game_ui_dragging_axis == 2);
            m_game_ui.end (m_hud_dl);
          }
        }
        // Even a clean inspection capture needs this empty HUD pass: it is
        // also the final post-chain composite into the drawable.
        r.draw_hud (m_hud_dl);

        r.end_frame ();
        if (captured) {
          m_screenshot_path.clear ();
          platform::request_quit ();
        }
      }

      void render_loading (render::Renderer& r) {
        const float w = (float)r.width_pts ();
        const float h = (float)r.height_pts ();
        const float aspect = w / std::max (1.0f, h);
        const double now = platform::now ();
        if (m_loading_clock_start == 0.0)
          m_loading_clock_start = now;
        // Animation shaders use floats, so give them elapsed loading time.
        // Casting absolute uptime first quantizes motion badly after a Mac has
        // been running for days: many 120 Hz frames receive the same timestamp.
        const float sky_time = static_cast<float> (now - m_loading_clock_start);
        std::vector<std::shared_ptr<const std::vector<float>>>
          loading_snapshots;
        {
          const std::lock_guard<std::mutex> lock (m_loading_mutex);
          loading_snapshots = m_loading_snapshots;
        }

        // The worker may finish several transforms between rendered frames.
        // The director advances only after the current chapter has had time to
        // breathe, and never before the next immutable snapshot exists.
        constexpr float chapter_hold = 3.5f;
        if (!loading_snapshots.empty () && m_loading_terrain_state == 0) {
          const auto& first = *loading_snapshots.front ();
          const float floor = *std::min_element (first.begin (), first.end ());
          std::fill (m_loading_map.raw_heights (),
                     m_loading_map.raw_heights () + first.size (),
                     floor);
          m_terrain.setup (r,
                           m_loading_map,
                           m_world,
                           m_graphics,
                           render::TerrainProjection::Plane,
                           true,
                           true);
          m_loading_terrain_state = 1;
          m_loading_terrain_reveal = sky_time;
          m_loading_snapshot_started = sky_time;
        } else if (!loading_snapshots.empty () &&
                   m_loading_terrain_state == 1) {
          std::copy (loading_snapshots.front ()->begin (),
                     loading_snapshots.front ()->end (),
                     m_loading_map.raw_heights ());
          m_terrain.setup (r,
                           m_loading_map,
                           m_world,
                           m_graphics,
                           render::TerrainProjection::Plane,
                           true,
                           true);
          m_loading_terrain_state = 2;
          m_loading_snapshot_index = 0;
          m_loading_snapshot_started = sky_time;
        } else if (m_loading_terrain_state == 2 &&
                   m_loading_snapshot_index + 1 < loading_snapshots.size ()) {
          if (sky_time - m_loading_snapshot_started >= chapter_hold) {
            ++m_loading_snapshot_index;
            const auto& snapshot = *loading_snapshots[m_loading_snapshot_index];
            std::copy (
              snapshot.begin (), snapshot.end (), m_loading_map.raw_heights ());
            m_terrain.setup (r,
                             m_loading_map,
                             m_world,
                             m_graphics,
                             render::TerrainProjection::Plane,
                             true,
                             true);
            m_loading_snapshot_started = sky_time;
          }
        }

        const bool show_terrain = m_loading_terrain_state > 0;
        const Vec3& world_extent = extent_value (m_world.map_size);
        Vec3 eye (0, 34, 0);
        Vec3 target (0, 27, -100);
        float field_of_view = 64.0f;
        if (show_terrain) {
          struct CameraShot {
            float angle;
            float radius;
            float height;
            float target_x;
            float target_y;
            float target_z;
            float field_of_view;
            float drift;
          };
          // Each transform gets a composition suited to what it changes:
          // geography, lowlands, long valleys, slopes, paths, and channels.
          static constexpr CameraShot shots[] = {
            { 0.20f, 0.70f, 0.68f, 0.00f, 0.11f, 0.00f, 52.0f, 0.002f },
            { 0.72f, 0.56f, 0.44f, -0.08f, 0.08f, 0.08f, 48.0f, 0.002f },
            { 1.14f, 0.48f, 0.34f, 0.10f, 0.06f, -0.08f, 45.0f, 0.002f },
            { 1.70f, 0.58f, 0.47f, 0.04f, 0.13f, 0.06f, 49.0f, 0.002f },
            { 2.22f, 0.40f, 0.29f, -0.12f, 0.08f, 0.10f, 43.0f, 0.002f },
            { 2.73f, 0.46f, 0.38f, 0.09f, 0.05f, 0.11f, 44.0f, 0.002f },
            { 3.18f, 0.41f, 0.30f, 0.08f, 0.07f, -0.12f, 43.0f, 0.002f },
            { 4.12f, 0.78f, 0.64f, 0.00f, 0.08f, 0.00f, 54.0f, 0.002f },
          };
          const std::size_t stage = std::min<std::size_t> (
            m_loading_snapshot_index, std::size (shots) - 1);
          const std::size_t previous = stage > 0 ? stage - 1 : stage;
          const float age = sky_time - m_loading_snapshot_started;
          const float cut = smooth_curve (0.0f, 2.35f, age);
          const auto camera_for = [&] (const CameraShot& shot) {
            const float angle = shot.angle + shot.drift * age;
            const Vec3 focus (world_extent[0] * (0.5f + shot.target_x),
                              world_extent[1] * shot.target_y,
                              world_extent[2] * (0.5f + shot.target_z));
            const Vec3 camera =
              focus + Vec3 (std::sin (angle) * world_extent[0] * shot.radius,
                            world_extent[1] * shot.height,
                            std::cos (angle) * world_extent[2] * shot.radius);
            return std::pair { camera, focus };
          };
          const auto [previous_eye, previous_target] =
            camera_for (shots[previous]);
          const auto [current_eye, current_target] = camera_for (shots[stage]);
          eye = previous_eye + (current_eye - previous_eye) * cut;
          target = previous_target + (current_target - previous_target) * cut;
          field_of_view =
            shots[previous].field_of_view +
            (shots[stage].field_of_view - shots[previous].field_of_view) * cut;
        }
        const Vec3 forward = normalized (target - eye);

        render::FrameParams fp;
        fp.view = Mat4::look_at (eye, target, Vec3 (0, 1, 0));
        fp.proj = Mat4::perspective_reversed (
          field_of_view * u::deg,
          aspect,
          0.5f,
          std::max (9000.0f, world_extent[0] * 2.0f));
        fp.camera_pos = eye;
        // Golden afternoon rather than sunrise: the light comes from the
        // camera's side, leaving the sun itself outside the composition.
        constexpr float loading_sun_height = 0.70f;
        const Vec3 horizon_forward =
          normalized (Vec3 (forward[0], 0.0f, forward[2]));
        const Vec3 horizon_side (-horizon_forward[2], 0.0f, horizon_forward[0]);
        const Vec3 loading_sun_dir =
          normalized (horizon_side * 0.82f + Vec3 (0, 1, 0) * 0.58f);
        fp.clear_color = horizon_color_for (loading_sun_height);
        fp.sun_dir = loading_sun_dir;
        sun_light_colors (loading_sun_height, fp.sun_diffuse, fp.sun_specular);
        fp.ambient = DisplayColor (0.58f, 0.55f, 0.48f);
        fp.fog_scale =
          show_terrain ? attenuation_value (m_world.fog_scale * 0.20f) : 0.0f;
        fp.time = sky_time;
        fp.exposure_bias = 1.0f;
        fp.sun_visibility = 0.32f;
        if (!r.begin_frame (fp))
          return;

        render::SkyParams sky;
        sky.time = sky_time;
        sky.sun_height = loading_sun_height;
        sky.cloudiness = 0.14f;
        sky.sun_dir = fp.sun_dir;
        sky.fog_color = fp.clear_color;
        r.draw_sky (sky);

        if (show_terrain) {
          const float reveal_age = sky_time - m_loading_terrain_reveal;
          // Let the first low plain breathe for a moment before it rises.  The
          // terrain transition itself lasts about a second in the Metal
          // backend.
          if (m_loading_terrain_state == 2 || reveal_age > 0.0f)
            m_terrain.render (r, eye, forward, world_extent[0] * 1.8f);
        }

        m_hud_dl.clear ();
        render::DrawState s;
        s.blend = true;
        s.depth_test = false;
        s.depth_write = false;
        s.cull = false;
        m_hud_dl.state (s);
        m_hud_dl.lit (false);
        m_hud_dl.fogged (false);

        static const char* headlines[] = {
          "A world without time", "Finding sea and summit",
          "Shaping the lowlands", "Ages pass through stone",
          "The slopes relent",    "Rain walks downhill",
          "The earth settles",    "Rivers cut their beds",
        };
        static const char* details[] = {
          "The continuous field becomes land",
          "Height is gathered into a common range",
          "Plains descend and mountains remain",
          "Drainage carries whole valleys through geological time",
          "Loose faces settle below their angle of rest",
          "A hundred thousand small histories seek the sea",
          "Fresh cuts soften and sediment comes to rest",
          "The visible water network receives its final channels",
        };
        const int display_stage =
          std::clamp (static_cast<int> (m_loading_snapshot_index), 0, 7);
        const std::string headline = headlines[display_stage];
        const std::string detail = details[display_stage];

        const int stage = m_gen_stage;
        float target_progress = 0.04f;
        if (stage == 3) {
          const float source =
            (float)m_loading_source_done.load () /
            std::max (1.0f, (float)m_loading_source_total.load ());
          target_progress = 0.04f + 0.16f * source;
        } else if (stage == 4) {
          const float erosion =
            (float)m_loading_work_done.load () /
            std::max (1.0f, (float)m_loading_work_total.load ());
          target_progress = 0.25f + 0.57f * erosion;
        } else if (stage == 5)
          target_progress = 0.88f;
        else if (stage == 6)
          target_progress = 0.97f;
        else if (stage == 7)
          target_progress = 0.92f;
        const float frame_dt =
          m_loading_progress_time > 0.0
            ? std::min (0.1f, (float)(sky_time - m_loading_progress_time))
            : 1.0f / 60.0f;
        m_loading_progress_time = sky_time;
        m_loading_progress_display +=
          (target_progress - m_loading_progress_display) *
          smoothing_alpha (7.0f / u::s, frame_dt * u::s);

        // A quiet cinematic instrument: the full ring is possibility, the
        // luminous arc is the real eased world-generation progress.
        const float center_x = w * 0.5f;
        const float dial_y =
          std::clamp (h * 0.42f, 105.0f, std::max (105.0f, h - 190.0f));
        const float pulse = 0.5f + 0.5f * std::sin (sky_time * 2.2f);
        loading_arc (m_hud_dl, center_x, dial_y, 49.0f, 2.0f, 1.0f, 0.20f);
        loading_arc (m_hud_dl,
                     center_x,
                     dial_y,
                     49.0f,
                     4.5f,
                     m_loading_progress_display,
                     0.92f);
        m_hud_dl.color (0.88f, 1.0f, 0.83f, 0.10f + 0.08f * pulse);
        fill_rounded_rect (
          m_hud_dl, center_x - 12.0f, dial_y - 12.0f, 24.0f, 24.0f, 12.0f);
        m_hud_dl.color (0.94f, 1.0f, 0.89f, 0.92f);
        fill_rounded_rect (
          m_hud_dl, center_x - 4.0f, dial_y - 4.0f, 8.0f, 8.0f, 4.0f);

        const float headline_y = dial_y + 112.0f;
        const float chapter_age = sky_time - m_loading_snapshot_started;
        const float transition = smooth_curve (0.0f, 0.55f, chapter_age);
        const bool final_scene =
          !loading_snapshots.empty () &&
          m_loading_snapshot_index + 1 == loading_snapshots.size ();
        const bool waiting_to_start =
          m_setup_complete && final_scene && chapter_age >= chapter_hold;

        if (m_loading_title_font && m_loading_title_font->ok ()) {
          const float x =
            center_x - m_loading_title_font->measure (headline) * 0.5f;
          const float y = headline_y - 22.0f * (1.0f - transition);
          // A soft shadow is enough separation from the landscape; no card.
          m_hud_dl.color (0.01f, 0.025f, 0.022f, 0.48f * transition);
          m_loading_title_font->draw (m_hud_dl, x + 2.0f, y + 3.0f, headline);
          m_hud_dl.color (0.95f, 1.0f, 0.91f, transition);
          m_loading_title_font->draw (m_hud_dl, x, y, headline);
        }
        if (m_loading_font && m_loading_font->ok ()) {
          const float x = center_x - m_loading_font->measure (detail) * 0.5f;
          m_hud_dl.color (0.01f, 0.025f, 0.022f, 0.42f * transition);
          m_loading_font->draw (m_hud_dl, x + 1.5f, headline_y + 46.0f, detail);
          m_hud_dl.color (0.82f, 0.94f, 0.87f, 0.96f * transition);
          m_loading_font->draw (m_hud_dl, x, headline_y + 44.0f, detail);
          std::ostringstream chapter;
          chapter << "STAGE " << (display_stage + 1) << " OF 8";
          const std::string chapter_text = chapter.str ();
          m_hud_dl.push ();
          m_hud_dl.translate (center_x, headline_y + 86.0f, 0.0f);
          m_hud_dl.scale (0.68f, 0.68f, 1.0f);
          m_hud_dl.color (0.70f, 0.84f, 0.75f, 0.72f * transition);
          m_loading_font->draw (m_hud_dl,
                                -m_loading_font->measure (chapter_text) * 0.5f,
                                0.0f,
                                chapter_text);
          m_hud_dl.pop ();
        }
        if (waiting_to_start && m_loading_font && m_loading_font->ok ()) {
          const std::string prompt = "PRESS SPACE TO ENTER THE WORLD";
          const float prompt_x =
            center_x - m_loading_font->measure (prompt) * 0.5f;
          const float prompt_alpha = 0.72f + 0.20f * std::sin (sky_time * 1.8f);
          m_hud_dl.color (0.01f, 0.025f, 0.022f, 0.52f);
          m_loading_font->draw (m_hud_dl, prompt_x + 1.5f, h - 58.0f, prompt);
          m_hud_dl.color (0.91f, 1.0f, 0.88f, prompt_alpha);
          m_loading_font->draw (m_hud_dl, prompt_x, h - 60.0f, prompt);
        }

        bool captured_loading = false;
        int capture_stage = 1;
        if (const char* value = ::getenv ("MOPPE_LOADING_SCREENSHOT_STAGE"))
          capture_stage = std::clamp (::atoi (value), 1, 8);
        const float capture_age = capture_stage == 8 ? chapter_hold : 0.72f;
        if (!m_loading_capture_done && show_terrain &&
            m_loading_progress_display > 0.40f &&
            static_cast<int> (m_loading_snapshot_index) + 1 >= capture_stage &&
            chapter_age >= capture_age) {
          if (const char* path = ::getenv ("MOPPE_LOADING_SCREENSHOT")) {
            r.request_screenshot (path);
            m_loading_capture_done = true;
            captured_loading = true;
          }
        }
        r.draw_hud (m_hud_dl);
        r.end_frame ();
        const bool automatic_start =
          !m_screenshot_path.empty () || m_benchmark.has_value () ||
          m_water_shot.has_value () || ::getenv ("MOPPE_DEMO");
        if (waiting_to_start &&
            (m_loading_continue_requested || automatic_start)) {
          m_ready = true;
          if (m_start_in_terrain_lab) {
            m_terrain_lab.enter (r,
                                 m_map,
                                 m_terrain,
                                 m_world,
                                 m_graphics,
                                 lab_program (),
                                 m_terrain_history,
                                 sun_direction_for (m_graphics.sun_height));
            m_start_in_terrain_lab = false;
          }
        }
        if (captured_loading)
          platform::request_quit ();
      }

      void render_game_over (render::Renderer& r) {
        render::FrameParams fp;
        fp.clear_color = DisplayColor (0, 0, 0);
        fp.view = Mat4 ();
        fp.proj = Mat4 ();
        if (!r.begin_frame (fp))
          return;

        m_hud_dl.clear ();
        m_hud.draw_game_over (m_hud_dl, r.width_pts (), r.height_pts ());
        r.draw_hud (m_hud_dl);
        r.end_frame ();
      }

      // -- input -------------------------------------------------------

      void controls (const platform::ControlState& state) override {
        if (m_game_over || m_terrain_lab.active ())
          return;
        input_turn (state.steer);
        input_go (state.drive);
        input_boost (state.boost);
      }

      void pointer_move (float x, float y, float dx, float dy) override {
        m_pointer_x = x;
        m_pointer_y = y;
        if (m_game_ui_dragging_axis)
          set_landscape_scale_from_pointer (x, m_game_ui_dragging_axis);
        else if (m_terrain_lab.active ())
          m_terrain_lab.pointer_move (x, y, dx, dy);
      }

      void pointer_button (platform::PointerButton button,
                           bool down,
                           float x,
                           float y) override {
        if (m_game_ui_open && button == platform::PointerButton::Primary) {
          const UiRect horizontal { m_game_ui_slider_x, 82, 320, 64 };
          const UiRect vertical { m_game_ui_slider_x, 156, 320, 64 };
          if (down && horizontal.contains (x, y)) {
            m_game_ui_dragging_axis = 1;
            set_landscape_scale_from_pointer (x, 1);
          } else if (down && vertical.contains (x, y)) {
            m_game_ui_dragging_axis = 2;
            set_landscape_scale_from_pointer (x, 2);
          } else if (!down) {
            m_game_ui_dragging_axis = 0;
          }
          return;
        }
        if (m_terrain_lab.active ())
          m_terrain_lab.pointer_button (button, down, x, y);
      }

      void pointer_scroll (float x, float y, float delta) override {
        if (m_terrain_lab.active ())
          m_terrain_lab.pointer_scroll (x, y, delta);
      }

      void key (platform::Key k, bool down) override {
        using platform::Key;
        const float factor = down ? 1.0f : 0.0f;

        if (!m_ready) {
          if (k == Key::Space && down)
            m_loading_continue_requested = true;
          else if (k == Key::Escape && down)
            platform::request_quit ();
          return;
        }

        // In great pain, only R (ride again) and ESC work.
        if (m_game_over) {
          if (k == Key::R && down)
            revive ();
          else if (k == Key::Escape && down)
            platform::request_quit ();
          return;
        }

        if (m_terrain_lab.active ()) {
          if (k == Key::T && down)
            m_terrain_lab.leave ();
          else
            m_terrain_lab.key (k, down);
          return;
        }

        if (k == Key::T && down && m_ready) {
          input_turn (0);
          input_go (0);
          input_boost (0);
          m_terrain_lab.enter (*m_renderer,
                               m_map,
                               m_terrain,
                               m_world,
                               m_graphics,
                               lab_program (),
                               m_terrain_history,
                               sun_direction_for (m_graphics.sun_height));
          return;
        }

        if (k == Key::M && down && m_ready) {
          m_game_ui_open = !m_game_ui_open;
          m_game_ui_dragging_axis = 0;
          return;
        }

        if (k == Key::N && down && m_ready) {
          regenerate_world ();
          return;
        }

        // The secret dismount combo: 7, then 5, then R.  Arrow keys
        // bypass it (they were "special" codes dispatched before the
        // combo machine in the GLUT build).
        const bool arrow =
          (k == Key::Left || k == Key::Right || k == Key::Up || k == Key::Down);
        if (down && !arrow) {
          static const Key want[3] = { Key::Seven, Key::Five, Key::R };
          if (k == want[m_combo]) {
            if (++m_combo == 3) {
              m_combo = 0;
              toggle_mount ();
            }
          } else
            m_combo = (k == Key::Seven) ? 1 : 0;
        }

        switch (k) {
        case Key::Left:
        case Key::A:
          input_turn (-1 * factor);
          break;
        case Key::Right:
        case Key::D:
          input_turn (1 * factor);
          break;
        case Key::Up:
        case Key::W:
          input_go (1 * factor);
          break;
        case Key::Down:
        case Key::S:
          input_go (-1 * factor);
          break;

        case Key::Tab:
          if (down) {
            m_cam_mode = (CamMode)((m_cam_mode + 1) % 3);
            if (m_cam_mode == CAM_HELMET)
              m_fp_eye = m_camera.position (); // glide in
          }
          break;

        case Key::Space:
          if (m_ready)
            input_boost (factor);
          break;

        case Key::Escape:
          if (down)
            platform::request_quit ();
          break;

        default:
          break;
        }
      }

    private:
      float landscape_scale_normalized (float scale) const {
        return std::log (scale / 0.05f) / std::log (400.0f);
      }

      void set_landscape_scale_from_pointer (float x, int axis) {
        const float normalized =
          std::clamp ((x - m_game_ui_slider_x) / 320.0f, 0.0f, 1.0f);
        const float scale = 0.05f * std::pow (400.0f, normalized);
        if (axis == 1)
          m_landscape_scale_x = scale;
        else
          m_landscape_scale_y = scale;
      }

      Vec3 landscape_visual_scale () const {
        return Vec3 (1.0f / m_landscape_scale_x,
                     1.0f / m_landscape_scale_y,
                     1.0f / m_landscape_scale_x);
      }

      void update_benchmark_title () const {
        if (!m_benchmark)
          return;
        const int configurations = 1 << graphics_benchmark_dimension_count ();
        std::ostringstream title;
        title << "Moppe benchmark " << (m_benchmark_epoch + 1) << '/'
              << configurations << " - ";
        bool any = false;
        for (std::size_t bit = 0; bit < RidingGraphicsPartition::blocks.size ();
             ++bit)
          if (m_benchmark_partition_mask & (1u << bit)) {
            if (any)
              title << " + ";
            title << RidingGraphicsPartition::name (
              RidingGraphicsPartition::blocks[bit]);
            any = true;
          }
        if (!any)
          title << "none";
        platform::set_window_title (title.str ());
      }

      mov::Vehicle& active_vehicle () {
        return m_mode == M_CAR ? m_car : m_vehicle;
      }

      void regenerate_world () {
        input_turn (0.0f);
        input_go (0.0f);
        input_boost (0.0f);
        m_ready = false;
        m_gen_stage = 0;
        m_loading_work_done = 0;
        m_loading_work_total = 1;
        m_loading_source_done = 0;
        m_loading_source_total = 1;
        m_loading_progress_display = 0.0f;
        m_loading_progress_time = 0.0;
        m_loading_clock_start = 0.0;
        m_loading_snapshot_index = 0;
        m_loading_snapshot_started = 0.0f;
        m_loading_terrain_state = 0;
        m_loading_continue_requested = false;
        m_setup_complete = false;
        {
          const std::lock_guard<std::mutex> lock (m_loading_mutex);
          m_loading_snapshots.clear ();
        }
        m_standing_water.reset ();
        m_lake_census.reset ();
        m_drainage.reset ();
        m_water_network.reset ();
        m_rivers.reset ();
        m_river_surface.clear ();
        m_terrain_history.clear ();
        ++m_seed;
        m_mode = M_BIKE;
        m_car_exists = false;
        m_game_over = false;
        m_health = 100.0f;
        m_fuel = 100.0f;
        platform::async (
          &MoppeGame::generate_thunk, &MoppeGame::finish_thunk, this);
      }

      void input_turn (float v) {
        m_turn_input = v;
        if (m_mode == M_FOOT)
          m_walker.set_turn (v);
        else
          active_vehicle ().set_yaw ((90 * v) * u::deg);
      }

      void input_go (float v) {
        m_go_input = v;
        if (m_mode == M_FOOT)
          m_walker.set_walk (v > 0 ? v : v * 0.6f);
        else {
          active_vehicle ().set_thrust (v);
          active_vehicle ().set_boost (m_boost_input, m_go_input);
        }
      }

      void input_boost (float v) {
        const float previous = m_boost_input;
        m_boost_input = std::max (0.0f, std::min (1.0f, v));
        if (m_mode == M_FOOT) {
          if (m_boost_input > 0.1f && previous <= 0.1f)
            m_walker.jump ();
        } else
          active_vehicle ().set_boost (m_boost_input, m_go_input);
      }

      void toggle_mount () {
        if (!m_ready)
          return;

        if (m_mode != M_FOOT) {
          // Step off to the side of whatever we're driving.
          mov::Vehicle& av = active_vehicle ();
          const Vec3 h = av.orientation ();
          const Vec3 side (h[2], 0, -h[0]);
          m_walker.spawn (
            position (av.position () + side * (m_mode == M_CAR ? 2.4f : 1.8f)),
            h);
          av.set_thrust (0);
          av.set_yaw (0 * u::deg);
          av.set_boost (0, 0);
          m_mode = M_FOOT;
          input_turn (m_turn_input);
          input_go (m_go_input);
          return;
        }

        // On foot: bike first, then our parked car, then grand theft.
        if (length2 (m_walker.position () - m_vehicle.position ()) <
            5.0f * 5.0f) {
          m_vehicle.set_thrust (0);
          m_vehicle.set_yaw (0 * u::deg);
          m_mode = M_BIKE;
          input_turn (m_turn_input);
          input_go (m_go_input);
          input_boost (m_boost_input);
          return;
        }

        if (m_car_exists &&
            length2 (m_walker.position () - m_car.position ()) < 6.0f * 6.0f) {
          m_car.set_thrust (0);
          m_car.set_yaw (0 * u::deg);
          m_mode = M_CAR;
          input_turn (m_turn_input);
          input_go (m_go_input);
          input_boost (m_boost_input);
          return;
        }
      }

      void revive () {
        m_lives = 10;
        m_health = 100.0f;
        m_fuel = 100.0f;
        m_shake = 0.0f;
        m_shake_time = 0.0f;
        m_jump_airtime = 0.0f;
        m_landed_age = 10.0f;
        m_mode = M_BIKE;
        // Back to the start, but ON the ground rather than 600 m
        // over it.
        const float ground =
          m_map.interpolated_height (m_spawn_position[0], m_spawn_position[2]);
        m_vehicle.reset (
          Vec3 (m_spawn_position[0], ground + 1.2f, m_spawn_position[2]));
        // Key releases were swallowed during the game-over screen;
        // don't resume with the throttle stuck open.
        m_turn_input = 0;
        m_go_input = 0;
        m_boost_input = 0;
        m_vehicle.set_thrust (0);
        m_vehicle.set_yaw (0 * u::deg);
        m_vehicle.set_boost (0, 0);
        m_game_over = false;
      }

      WorldParams m_world;
      GraphicsSettings m_graphics;
      Vec3 m_spawn_position;
      int m_seed;
      terrain::TerrainGenerationProfile m_generation_profile;
      map::RandomHeightMap m_map;
      map::RandomHeightMap m_loading_map;
      std::vector<std::vector<float>> m_terrain_history;
      std::mutex m_loading_mutex;
      std::vector<std::shared_ptr<const std::vector<float>>>
        m_loading_snapshots;
      std::atomic<int> m_loading_work_done = 0;
      std::atomic<int> m_loading_work_total = 1;
      std::atomic<int> m_loading_source_done = 0;
      std::atomic<int> m_loading_source_total = 1;
      float m_loading_progress_display = 0.0f;
      double m_loading_progress_time = 0.0;
      double m_loading_clock_start = 0.0;
      bool m_loading_capture_done = false;
      int m_loading_terrain_state = 0;
      float m_loading_terrain_reveal = 0.0f;
      std::size_t m_loading_snapshot_index = 0;
      float m_loading_snapshot_started = 0.0f;
      bool m_loading_continue_requested = false;
      std::optional<terrain::FloodField> m_standing_water;
      std::optional<terrain::LakeCensus> m_lake_census;
      std::optional<terrain::DrainageGraph> m_drainage;
      std::optional<terrain::WaterNetwork> m_water_network;
      std::optional<terrain::RiverNetwork> m_rivers;
      RiverSurface m_river_surface;
      Terrain m_terrain;
      TerrainLab m_terrain_lab;
      ChaseCamera m_camera;
      mov::Vehicle m_vehicle;
      mov::Vehicle m_car;
      Vegetation m_vegetation;
      Stars m_stars;
      Dust m_dust;
      BlobShadow m_blob;
      std::vector<mov::Box> m_obstacles;
      Walker m_walker;
      Hud m_hud;
      InspectorUi m_game_ui;
      bool m_game_ui_open = false;
      int m_game_ui_dragging_axis = 0;
      float m_landscape_scale_x = 1.0f;
      float m_landscape_scale_y = 1.0f;
      float m_game_ui_slider_x = 44.0f;
      float m_pointer_x = -1.0f;
      float m_pointer_y = -1.0f;
      std::unique_ptr<render::FontAtlas> m_loading_title_font;
      std::unique_ptr<render::FontAtlas> m_loading_font;

      render::Renderer* m_renderer;
      bool m_start_in_terrain_lab;
      bool m_terrain_lab_preview;
      bool m_automated_regeneration_done = false;
      std::string m_screenshot_path;
      std::optional<WaterShot> m_water_shot;
      std::optional<WaterInspection> m_water_inspection;
      int m_screenshot_frames;
      std::atomic<bool> m_ready;
      std::atomic<bool> m_setup_complete = false;
      std::atomic<int> m_gen_stage;
      std::optional<GraphicsBenchmarkConfig> m_benchmark;
      GraphicsSettings m_benchmark_baseline;
      std::optional<GameState> m_benchmark_checkpoint;
      int m_benchmark_prelude_frame = 0;
      int m_benchmark_epoch = 0;
      int m_benchmark_frame = 0;
      uint32_t m_benchmark_mask = 0;
      uint32_t m_benchmark_partition_mask = 0;
      bool m_benchmark_measured = false;
      bool m_benchmark_submitted = false;
      bool m_benchmark_results_written = false;

      render::DrawList m_world_dl;
      render::DrawList m_hud_dl;
    };
  }
}

int main (int argc, char** argv) {
  using namespace moppe;
  MOPPE_PROFILE_THREAD ("Main");
  MOPPE_PROFILE_ZONE ("main");

  game::WorldParams world;
  game::GraphicsSettings graphics = game::high_graphics_settings ();
  platform::Config config;
  bool start_in_terrain_lab = false;
  bool terrain_lab_preview = false;
  std::string screenshot_path;
  std::optional<game::WaterShot> water_shot;
  int seed = -1;
  terrain::TerrainGenerationProfile generation_profile =
    terrain::TerrainGenerationProfile::Play;
  std::optional<game::GraphicsBenchmarkConfig> graphics_benchmark;
  config.title = "Moppe";

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--fullscreen") {
      config.fullscreen = true;
    } else if (arg == "--windowed") {
      config.fullscreen = false;
    } else if (arg == "--graphics-quality") {
      if (i + 1 >= argc) {
        std::cerr << "--graphics-quality requires low or high\n";
        return -1;
      }
      const std::string quality = argv[++i];
      if (quality == "low")
        graphics = game::low_graphics_settings ();
      else if (quality == "high")
        graphics = game::high_graphics_settings ();
      else {
        std::cerr << "unknown graphics quality: " << quality << '\n';
        return -1;
      }
    } else if (arg == "--graphics-enable" || arg == "--graphics-disable") {
      if (i + 1 >= argc) {
        std::cerr << arg << " requires a comma-separated feature list\n";
        return -1;
      }
      std::string error;
      const bool enabled = arg == "--graphics-enable";
      if (!game::set_graphics_features (graphics, argv[++i], enabled, error)) {
        std::cerr << error << '\n';
        return -1;
      }
    } else if (arg == "--graphics-benchmark") {
      if (i + 1 >= argc) {
        std::cerr << "--graphics-benchmark requires a CSV path\n";
        return -1;
      }
      graphics_benchmark = game::GraphicsBenchmarkConfig {};
      graphics_benchmark->output_path = argv[++i];
      config.fullscreen = false;
    } else if (arg == "--benchmark-frames" || arg == "--benchmark-settle" ||
               arg == "--benchmark-prelude") {
      if (i + 1 >= argc) {
        std::cerr << arg << " requires a positive frame count\n";
        return -1;
      }
      if (!graphics_benchmark)
        graphics_benchmark = game::GraphicsBenchmarkConfig {};
      const int value = std::max (1, std::atoi (argv[++i]));
      if (arg == "--benchmark-frames")
        graphics_benchmark->measured_frames = value;
      else if (arg == "--benchmark-settle")
        graphics_benchmark->settle_frames = value;
      else
        graphics_benchmark->prelude_frames = value;
    } else if (arg == "--fast") {
      generation_profile = terrain::TerrainGenerationProfile::Fast;
    } else if (arg == "--terrain-quality") {
      if (i + 1 >= argc) {
        std::cerr << "--terrain-quality requires fast, play, or research\n";
        return -1;
      }
      const std::string quality = argv[++i];
      if (quality == "fast")
        generation_profile = terrain::TerrainGenerationProfile::Fast;
      else if (quality == "play")
        generation_profile = terrain::TerrainGenerationProfile::Play;
      else if (quality == "research")
        generation_profile = terrain::TerrainGenerationProfile::Research;
      else {
        std::cerr << "unknown terrain quality: " << quality << '\n';
        return -1;
      }
    } else if (arg == "--terrain-lab") {
      start_in_terrain_lab = true;
    } else if (arg == "--terrain-lab-preview") {
      start_in_terrain_lab = true;
      terrain_lab_preview = true;
      config.fullscreen = false;
      world.resolution = 1025;
    } else if (arg == "--terrain-lab-screenshot") {
      if (i + 1 >= argc) {
        std::cerr << "--terrain-lab-screenshot requires a PNG path\n";
        return -1;
      }
      screenshot_path = argv[++i];
      start_in_terrain_lab = true;
      terrain_lab_preview = true;
      config.fullscreen = false;
      world.resolution = 1025;
    } else if (arg == "--screenshot") {
      if (i + 1 >= argc) {
        std::cerr << "--screenshot requires a PNG path\n";
        return -1;
      }
      screenshot_path = argv[++i];
      config.fullscreen = false;
    } else if (arg == "--water-screenshot") {
      if (i + 2 >= argc) {
        std::cerr << "--water-screenshot requires a feature and PNG path\n";
        return -1;
      }
      const std::string feature = argv[++i];
      water_shot = game::parse_water_shot (feature);
      if (!water_shot) {
        std::cerr << "unknown water feature: " << feature
                  << " (use river, confluence, mouth, waterfall, or lake)\n";
        return -1;
      }
      screenshot_path = argv[++i];
      config.fullscreen = false;
    } else if (arg == "--seed") {
      if (i + 1 >= argc) {
        std::cerr << "--seed requires an integer\n";
        return -1;
      }
      seed = std::atoi (argv[++i]);
    }
  }
  if (terrain_lab_preview)
    config.fullscreen = false;
  std::string graphics_error;
  if (!game::apply_graphics_environment (graphics, graphics_error)) {
    std::cerr << graphics_error << '\n';
    return -1;
  }
  game::print_graphics_settings (std::cerr, graphics);
  if (graphics_benchmark) {
    if (graphics_benchmark->output_path.empty ()) {
      std::cerr << "--graphics-benchmark is required with benchmark options\n";
      return -1;
    }
    const int expected = graphics_benchmark->measured_frames *
                         (1 << game::graphics_benchmark_dimension_count ());
    ::setenv (
      "MOPPE_BENCHMARK_OUTPUT", graphics_benchmark->output_path.c_str (), 1);
    const std::string expected_text = std::to_string (expected);
    ::setenv ("MOPPE_BENCHMARK_EXPECTED", expected_text.c_str (), 1);
    std::string feature_names;
    for (const game::GraphicsFeature* feature : game::graphics_features)
      if (feature->hot) {
        if (!feature_names.empty ())
          feature_names += ',';
        feature_names += feature->name;
      }
    ::setenv ("MOPPE_BENCHMARK_FEATURES", feature_names.c_str (), 1);
  }
  if (generation_profile == terrain::TerrainGenerationProfile::Fast &&
      !terrain_lab_preview)
    world.resolution = 1025;
  config.capture_frames = !screenshot_path.empty ();
  if (!screenshot_path.empty () && seed < 0)
    seed = 123;
  game::prune_obsolete_terrain_caches ();
  if (seed < 0)
    seed = game::remembered_seed (world, generation_profile);

  game::MoppeGame game (world,
                        graphics,
                        start_in_terrain_lab,
                        terrain_lab_preview,
                        seed,
                        std::move (screenshot_path),
                        water_shot,
                        generation_profile,
                        graphics_benchmark);

  try {
    return platform::run (game, config);
  } catch (const std::exception& e) {
    std::cerr << "\nError: " << e.what () << "\n";
    return -1;
  }
}
