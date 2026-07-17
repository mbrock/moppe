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
#include <moppe/game/cinematic_flight.hh>
#include <moppe/game/dust.hh>
#include <moppe/game/forest.hh>
#include <moppe/game/game_session.hh>
#include <moppe/game/generated_world.hh>
#include <moppe/game/glider_render.hh>
#include <moppe/game/graphics_benchmark.hh>
#include <moppe/game/graphics_settings.hh>
#include <moppe/game/hud.hh>
#include <moppe/game/input_frame_adapter.hh>
#include <moppe/game/inspector_ui.hh>
#include <moppe/game/river_surface.hh>
#include <moppe/game/stars.hh>
#include <moppe/game/surface_presentation.hh>
#include <moppe/game/terrain.hh>
#include <moppe/game/terrain_lab.hh>
#include <moppe/game/tree_stand.hh>
#include <moppe/game/vehicle_render.hh>
#include <moppe/game/walker.hh>
#include <moppe/game/water_capture.hh>
#include <moppe/game/water_presentation.hh>
#include <moppe/game/world.hh>
#include <moppe/map/surface.hh>
#include <moppe/map/terrain_evaluator.hh>

#include <moppe/map/generate.hh>
#include <moppe/mov/glider.hh>
#include <moppe/mov/vehicle.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/fractional_drainage.hh>
#include <moppe/terrain/moisture.hh>
#include <moppe/terrain/river.hh>
#include <moppe/terrain/trail.hh>
#include <moppe/terrain/watercourse.hh>
#include <moppe/terrain/waterline.hh>
#include <moppe/terrain/world_recipe.hh>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
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

    static int cinematic_capture_frame_limit () {
      if (const char* value = ::getenv ("MOPPE_CINEMATIC_CAPTURE_FRAMES"))
        return std::max (1, ::atoi (value));
      return 450;
    }

    static float smooth_curve (float edge0, float edge1, float x) {
      float t = (x - edge0) / (edge1 - edge0);
      clamp (t, 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    }

    enum class LoadingStage {
      Starting,
      LookingForCache,
      ReadingCache,
      BuildingContinents,
      EvolvingTerrain,
      SavingTerrain,
      RebuildingSurface,
      FindingStandingWater,
      CataloguingLakes,
      TracingDrainage,
      ConnectingWaterways,
      ExtractingRivers,
      PlacingStars,
      AssemblingWorld,
      UploadingTerrain,
      CastingShadows,
      Ready,
    };

    struct LoadingStageText {
      const char* title;
      const char* detail;
      float progress;
    };

    struct LoadingEvent {
      LoadingStage stage;
      double elapsed;
    };

    static LoadingStageText loading_stage_text (LoadingStage stage) {
      switch (stage) {
      case LoadingStage::Starting:
        return { "WAKING THE WORLD BUILDER",
                 "Preparing terrain storage and compute",
                 0.02f };
      case LoadingStage::LookingForCache:
        return { "LOOKING FOR SAVED TERRAIN",
                 "Checking this build, profile, and seed",
                 0.04f };
      case LoadingStage::ReadingCache:
        return { "READING SAVED TERRAIN",
                 "Reusing the finished heightfield",
                 0.10f };
      case LoadingStage::BuildingContinents:
        return { "DRAWING THE CONTINENTS",
                 "Materializing the geological field",
                 0.06f };
      case LoadingStage::EvolvingTerrain:
        return { "RUNNING GEOLOGICAL TIME",
                 "Uplift and erosion are reshaping the land",
                 0.20f };
      case LoadingStage::SavingTerrain:
        return { "SAVING THE TERRAIN",
                 "Keeping this expensive result for the next launch",
                 0.69f };
      case LoadingStage::RebuildingSurface:
        return { "CALCULATING SLOPES",
                 "Rebuilding normals and the sampled surface",
                 0.72f };
      case LoadingStage::FindingStandingWater:
        return { "FILLING SEAS AND LAKES",
                 "Finding the connected water surface",
                 0.75f };
      case LoadingStage::CataloguingLakes:
        return { "CATALOGUING LAKES",
                 "Measuring every separate body of water",
                 0.78f };
      case LoadingStage::TracingDrainage:
        return { "TRACING THE DRAINAGE",
                 "Following every wet cell downhill",
                 0.81f };
      case LoadingStage::ConnectingWaterways:
        return { "CONNECTING THE WATERWAYS",
                 "Joining lakes, outlets, and the sea",
                 0.84f };
      case LoadingStage::ExtractingRivers:
        return { "EXTRACTING THE RIVERS",
                 "Selecting the channels visible in the world",
                 0.87f };
      case LoadingStage::PlacingStars:
        return { "PLACING THE STARS",
                 "Finding bright landmarks across the terrain",
                 0.94f };
      case LoadingStage::AssemblingWorld:
        return { "ASSEMBLING THE WORLD",
                 "Painting water, moisture, materials, and the opening route",
                 0.95f };
      case LoadingStage::UploadingTerrain:
        return { "UPLOADING THE LANDSCAPE",
                 "Moving the finished world onto the GPU",
                 0.97f };
      case LoadingStage::CastingShadows:
        return { "CASTING THE FIRST SHADOWS",
                 "Precomputing sunlight across the terrain",
                 0.99f };
      case LoadingStage::Ready:
        return { "THE WORLD IS READY", "Setting out", 1.0f };
      }
      return { "BUILDING THE WORLD", "Working", 0.0f };
    }

    // A cache must distinguish the exact physical recipe, not a rounded
    // presentation of it.
    static void append_cache_float (std::ostream& stream, float value) {
      stream << std::hex << std::bit_cast<std::uint32_t> (value) << std::dec;
    }

    static std::string terrain_cache_path (const terrain::WorldRecipe& recipe) {
      const Vec3 extent = extent_value (recipe.extent ());
      std::ostringstream name;
      name << "terrain-" << platform::executable_build_id () << '-'
           << terrain::profile_id (recipe.generation_profile ()) << '-'
           << recipe.resolution () << '-' << recipe.seed ().value << "-extent-";
      append_cache_float (name, extent[0]);
      name << '-';
      append_cache_float (name, extent[1]);
      name << '-';
      append_cache_float (name, extent[2]);
      name << "-water-";
      append_cache_float (name, meters_value (recipe.water_datum ()));
      name << (recipe.topology () == terrain::Topology::Torus ? "-torus.map"
                                                              : "-bounded.map");
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
      // Skip the sediment-ledger section the map cache stores between
      // the heights and this history section.
      char section[4] {};
      input.read (section, sizeof (section));
      if (input && std::memcmp (section, "LGR1", 4) == 0)
        input.seekg (static_cast<std::streamoff> (2 * samples * sizeof (float)),
                     std::ios::cur);
      else
        input.seekg (-static_cast<std::streamoff> (sizeof (section)),
                     std::ios::cur);
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

    class MoppeGame : public platform::Game {
      struct GenerationJob {
        GenerationJob (MoppeGame& owner,
                       WorldParams requested_params,
                       terrain::WorldRecipe requested_recipe,
                       bool requested_terrain_lab_preview)
            : game (&owner), params (std::move (requested_params)),
              recipe (std::move (requested_recipe)),
              terrain_lab_preview (requested_terrain_lab_preview) {}

        // The worker and main-thread callback each hold this while touching
        // game.  Destruction takes it before revoking the raw game pointer.
        std::mutex lifetime_mutex;
        MoppeGame* game;
        WorldParams params;
        terrain::WorldRecipe recipe;
        bool terrain_lab_preview;
      };

    public:
      MoppeGame (const WorldParams& world,
                 terrain::WorldRecipe recipe,
                 const GraphicsSettings& graphics,
                 bool start_in_terrain_lab,
                 bool terrain_lab_preview,
                 bool tree_demo,
                 std::size_t tree_count,
                 std::string screenshot_path,
                 std::optional<WaterShot> water_shot,
                 std::optional<GraphicsBenchmarkConfig> benchmark)
          : m_generated_world (
              std::make_unique<GeneratedWorld> (world, std::move (recipe))),
            m_session (std::make_unique<GameSession> (
              this->world (), this->map (), this->surface ())),
            m_graphics (graphics), m_spawn_position (position_value (
                                     this->world ().spawn_position ())),
            m_loading_world (this->world ()),
            m_loading_map (this->recipe ().resolution (),
                           this->recipe ().resolution (),
                           extent_value (this->recipe ().extent ()),
                           this->recipe ().seed ().value,
                           this->recipe ().topology ()),
            m_renderer (0), m_start_in_terrain_lab (start_in_terrain_lab),
            m_terrain_lab_preview (terrain_lab_preview),
            m_tree_demo (tree_demo), m_tree_count (tree_count),
            m_screenshot_path (std::move (screenshot_path)),
            m_water_shot (water_shot), m_screenshot_frames (0), m_ready (false),
            m_loading_stage (LoadingStage::Starting),
            m_benchmark (std::move (benchmark)),
            m_benchmark_baseline (graphics) {}

      ~MoppeGame () override {
        if (!m_generation_job)
          return;

        // Do not let the stack-owned game disappear while its worker still
        // publishes loading state.  platform::async keeps the job itself
        // alive until its queued completion callback has returned.
        const std::lock_guard<std::mutex> lock (
          m_generation_job->lifetime_mutex);
        m_generation_job->game = nullptr;
      }

      GameState state () const {
        return session ().state ();
      }

      void restore (const GameState& state) {
        session ().restore (state);
      }

      // -- lifecycle ---------------------------------------------------

      void setup (render::Renderer& r, int, int) override {
        MOPPE_PROFILE_ZONE ("MoppeGame::setup");
        m_renderer = &r;
        set_loading_stage (LoadingStage::Starting);

        // Fast, main-thread resource setup; the heavy world build
        // runs behind the loading screen.
        {
          MOPPE_PROFILE_ZONE ("startup.load_hud");
          m_hud.load (r);
        }
        {
          MOPPE_PROFILE_ZONE ("startup.load_game_ui");
          m_game_ui.load (r);
        }
        {
          MOPPE_PROFILE_ZONE ("startup.load_terrain_lab");
          m_terrain_lab.load (r);
        }
        {
          MOPPE_PROFILE_ZONE ("startup.load_loading_font");
          m_loading_font.reset (new render::FontAtlas (
            r, "AvenirNext-Medium", 24, r.scale_factor ()));
        }
        {
          MOPPE_PROFILE_ZONE ("startup.load_blob_shadow");
          m_blob.load (r);
        }

        {
          MOPPE_PROFILE_ZONE ("startup.dispatch_world_generation");
          start_world_generation (recipe ());
        }
      }

      static void generate_thunk (void* self) {
        GenerationJob& job = *static_cast<GenerationJob*> (self);
        const std::lock_guard<std::mutex> lock (job.lifetime_mutex);
        if (job.game)
          job.game->generate_world (job);
      }
      static void finish_thunk (void* self) {
        GenerationJob& job = *static_cast<GenerationJob*> (self);
        MoppeGame* game = nullptr;
        {
          const std::lock_guard<std::mutex> lock (job.lifetime_mutex);
          game = job.game;
          if (!game)
            return;

          // platform::async retains job through this callback, even though
          // activation releases MoppeGame's shared job owner below.
          try {
            // platform::async invokes this only after the worker returned, on
            // the main/render thread. This is the sole ownership commit point.
            game->activate_completed_world ();
          } catch (const std::exception& e) {
            std::cerr << "world activation failed: " << e.what () << std::endl;
            std::_Exit (-1);
          }
          game->m_generation_complete = true;
        }

        // A job must not destroy its mutex while this callback has it locked.
        game->m_generation_job.reset ();
      }

      void start_world_generation (terrain::WorldRecipe recipe) {
        // Generation is deliberately single-flight.  There is no cancellation
        // path: the current playable world remains active until this one
        // complete candidate crosses the main-thread activation boundary.
        if (m_generation_job)
          throw std::logic_error ("world generation is already in flight");
        {
          const std::lock_guard<std::mutex> lock (m_completed_world_mutex);
          if (m_completed_world)
            throw std::logic_error ("a completed world has not activated");
        }
        m_generation_job = std::make_shared<GenerationJob> (
          *this, world (), std::move (recipe), m_terrain_lab_preview);
        m_loading_world = m_generation_job->params;
        platform::async (&MoppeGame::generate_thunk,
                         &MoppeGame::finish_thunk,
                         m_generation_job);
      }

      GameSession& session () noexcept {
        return *m_session;
      }

      const GameSession& session () const noexcept {
        return *m_session;
      }

      GameLogicState& logic () noexcept {
        return session ().logic ();
      }

      const GameLogicState& logic () const noexcept {
        return session ().logic ();
      }

      const GeneratedWorld& generated_world () const noexcept {
        return *m_generated_world;
      }

      GeneratedWorld& generated_world () noexcept {
        return *m_generated_world;
      }

      const terrain::WorldRecipe& recipe () const noexcept {
        return generated_world ().recipe ();
      }

      const WorldParams& world () const noexcept {
        return generated_world ().params ();
      }

      const map::RandomHeightMap& map () const noexcept {
        return generated_world ().terrain ();
      }

      const map::Surface& surface () const noexcept {
        return generated_world ().surface ();
      }

      const std::vector<std::vector<float>>& terrain_history () const noexcept {
        return generated_world ().terrain_history ();
      }

      void set_loading_stage (LoadingStage stage) {
        m_loading_stage = stage;
        const double elapsed = platform::now () - m_loading_clock_start;
        const std::lock_guard<std::mutex> lock (m_loading_mutex);
        if (m_loading_events.empty () ||
            m_loading_events.back ().stage != stage)
          m_loading_events.push_back ({ stage, elapsed });
      }

      void publish_loading_terrain (const map::RandomHeightMap& terrain) {
        const std::size_t count =
          static_cast<std::size_t> (terrain.width ()) * terrain.height ();
        const std::lock_guard<std::mutex> lock (m_loading_mutex);
        m_loading_heights = std::make_shared<const std::vector<float>> (
          terrain.raw_heights (), terrain.raw_heights () + count);
      }

      const GeneratedWorld::Hydrology* hydrology () const noexcept {
        const auto& value = generated_world ().hydrology ();
        return value ? &*value : nullptr;
      }

      const terrain::FloodField* standing_water () const noexcept {
        const auto* value = hydrology ();
        return value ? &value->standing_water () : nullptr;
      }

      const terrain::LakeCensus* lake_census () const noexcept {
        const auto* value = hydrology ();
        return value ? &value->lakes () : nullptr;
      }

      const terrain::DrainageGraph* drainage () const noexcept {
        const auto* value = hydrology ();
        return value ? &value->drainage () : nullptr;
      }

      const terrain::RiverNetwork* rivers () const noexcept {
        const auto* value = hydrology ();
        return value ? &value->rivers () : nullptr;
      }

      const terrain::TrailNetwork* trail_network () const noexcept {
        const auto& value = generated_world ().trails ();
        return value ? &*value : nullptr;
      }

      Vec3 choose_landscape_spawn () {
        MOPPE_PROFILE_ZONE ("startup.choose_landscape_spawn");
        // The generated landscape has no authored start.  Sample the
        // finished terrain for a dry, grassy, locally flat patch rather
        // than trusting the old fixed coordinate near the map corner.
        const Vec3& world_extent = extent_value (world ().map_size);
        const float margin_x = 0.08f * world_extent[0];
        const float margin_z = 0.08f * world_extent[2];
        const float patch = 20.0f; // metres
        const float min_ground = meters_value (world ().water_level) + 25.0f;
        const float max_ground = 0.32f * world_extent[1];
        const auto elevation_at = [this] (float x, float z) {
          return surface ()
            .elevation_at (position (Vec3 (x, 0, z)))
            .quantity_from_zero ()
            .numerical_value_in (u::m);
        };
        const auto normal_at = [this] (float x, float z) {
          return surface ()
            .normal_at (position (Vec3 (x, 0, z)))
            .numerical_value_in (one);
        };

        std::uniform_real_distribution<float> random_x (
          margin_x, world_extent[0] - margin_x);
        std::uniform_real_distribution<float> random_z (
          margin_z, world_extent[2] - margin_z);

        Vec3 chosen;
        Vec3 fallback;
        int good_count = 0;
        float fallback_score = -1000000.0f;

        const auto standing_depth = [this, &world_extent] (float x, float z) {
          if (!standing_water ())
            return 0.0f;
          const terrain::TerrainGrid& grid = standing_water ()->source_grid;
          const auto wrap = [] (float value, float period) {
            value = std::fmod (value, period);
            return value < 0.0f ? value + period : value;
          };
          const std::size_t gx =
            static_cast<std::size_t> (wrap (x, world_extent[0]) /
                                      grid.spacing_x_m ()) %
            standing_water ()->width ();
          const std::size_t gz =
            static_cast<std::size_t> (wrap (z, world_extent[2]) /
                                      grid.spacing_y_m ()) %
            standing_water ()->height ();
          return standing_water ()->water_depth.at (gx, gz) * world_extent[1];
        };

        for (int i = 0; i < 6000; ++i) {
          const float x = random_x (logic ().m_fx_rng);
          const float z = random_z (logic ().m_fx_rng);
          const float h = elevation_at (x, z);
          const float hx0 = elevation_at (x - patch, z);
          const float hx1 = elevation_at (x + patch, z);
          const float hz0 = elevation_at (x, z - patch);
          const float hz1 = elevation_at (x, z + patch);
          const float low =
            std::min (h, std::min (std::min (hx0, hx1), std::min (hz0, hz1)));
          const float high =
            std::max (h, std::max (std::max (hx0, hx1), std::max (hz0, hz1)));
          const float relief = high - low;
          const float up = normal_at (x, z)[1];
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
          if (keep (logic ().m_fx_rng) == 1)
            chosen = Vec3 (x, h + 1.2f, z);
        }

        return good_count > 0 ? chosen : fallback;
      }

      Vec3 trail_cell_position (terrain::CellIndex cell) const {
        if (!trail_network () || cell == terrain::no_cell)
          return {};
        const terrain::TerrainGrid& grid = trail_network ()->source_grid;
        const std::size_t width = grid.unique_width ();
        const float x = (cell.value % width) * grid.spacing_x_m ();
        const float z = (cell.value / width) * grid.spacing_y_m ();
        return Vec3 (x, map ().interpolated_height (x, z), z);
      }

      Vec3 trail_alignment_position (
        const terrain::TrailAlignmentPoint& point) const {
        return Vec3 (point.x_m,
                     map ().interpolated_height (point.x_m, point.z_m),
                     point.z_m);
      }

      Vec3 trail_direction_from_home () const {
        if (!trail_network () || trail_network ()->alignment.points.size () < 2)
          return Vec3 (0, 0, 1);
        Vec3 direction =
          trail_alignment_position (trail_network ()->alignment.points[1]) -
          trail_alignment_position (trail_network ()->alignment.points[0]);
        direction[1] = 0.0f;
        return length2 (direction) > 1e-5f ? normalized (direction)
                                           : Vec3 (0, 0, 1);
      }

      void draw_home_base_marker (render::DrawList& dl) const {
        if (!trail_network ())
          return;
        const Vec3 base = m_home_base_position;
        render::DrawState marker_state;
        marker_state.cull = false;
        dl.state (marker_state);
        dl.lit (true);
        dl.fogged (true);
        dl.push ();
        dl.translate (base + Vec3 (0, 2.8f, 0));
        dl.color (0.18f, 0.14f, 0.08f);
        dl.scale (0.22f, 5.6f, 0.22f);
        dl.cube (1.0f);
        dl.pop ();

        const Vec3 along = trail_direction_from_home ();
        Vec3 side = cross (Vec3 (0, 1, 0), along);
        if (length2 (side) < 1e-5f)
          side = Vec3 (1, 0, 0);
        side = normalized (side);
        const Vec3 flag_top = base + Vec3 (0, 5.5f, 0);
        dl.lit (false);
        dl.color (1.0f, 0.55f, 0.08f);
        dl.begin (render::Prim::Triangles);
        dl.vertex (flag_top);
        dl.vertex (flag_top + Vec3 (0, -2.0f, 0));
        dl.vertex (flag_top + side * 2.8f + Vec3 (0, -0.8f, 0));
        dl.end ();
        dl.lit (true);
        dl.state (render::DrawState ());
      }

      void draw_trail_map (render::DrawList& dl,
                           int width_pts,
                           int height_pts) const {
        if (!trail_network () || width_pts < 480 || height_pts < 360)
          return;
        const terrain::TerrainGrid& grid = trail_network ()->source_grid;
        const auto& alignment = trail_network ()->alignment.points;
        if (alignment.size () < 2)
          return;
        const float period_x = grid.unique_width () * grid.spacing_x_m ();
        const float period_z = grid.unique_height () * grid.spacing_y_m ();
        const float home_x = alignment.front ().x_m;
        const float home_z = alignment.front ().z_m;
        const auto wrap_delta = [] (float delta, float period) {
          if (delta > period * 0.5f)
            delta -= period;
          if (delta < -period * 0.5f)
            delta += period;
          return delta;
        };
        const auto relative_alignment =
          [&] (terrain::TrailAlignmentPoint point) {
            return Vec3 (point.x_m - home_x, 0, point.z_m - home_z);
          };

        Vec3 low (0, 0, 0);
        Vec3 high (0, 0, 0);
        for (const terrain::TrailAlignmentPoint alignment_point : alignment) {
          const Vec3 point = relative_alignment (alignment_point);
          low[0] = std::min (low[0], point[0]);
          low[2] = std::min (low[2], point[2]);
          high[0] = std::max (high[0], point[0]);
          high[2] = std::max (high[2], point[2]);
        }
        const float world_span =
          std::max ({ high[0] - low[0], high[2] - low[2], 100.0f }) * 1.16f;
        const float center_x = 0.5f * (low[0] + high[0]);
        const float center_z = 0.5f * (low[2] + high[2]);
        const float map_size = std::min (154.0f, height_pts * 0.24f);
        const float map_x = 12.0f;
        const float map_y = height_pts - map_size - 12.0f;
        const float inset = 9.0f;
        const float scale = (map_size - 2.0f * inset) / world_span;
        const auto map_point = [&] (const Vec3& point) {
          return Vec3 (map_x + map_size * 0.5f + (point[0] - center_x) * scale,
                       map_y + map_size * 0.5f - (point[2] - center_z) * scale,
                       0);
        };

        render::DrawState state;
        state.blend = true;
        state.depth_test = false;
        state.depth_write = false;
        state.cull = false;
        dl.state (state);
        dl.lit (false);
        dl.fogged (false);
        dl.color (0.01f, 0.025f, 0.035f, 0.78f);
        dl.begin (render::Prim::Quads);
        dl.vertex (map_x, map_y);
        dl.vertex (map_x + map_size, map_y);
        dl.vertex (map_x + map_size, map_y + map_size);
        dl.vertex (map_x, map_y + map_size);
        dl.end ();
        dl.color (0.22f, 0.42f, 0.46f, 0.9f);
        dl.line (map_x, map_y, map_x + map_size, map_y, 1.0f);
        dl.line (
          map_x + map_size, map_y, map_x + map_size, map_y + map_size, 1.0f);
        dl.line (
          map_x + map_size, map_y + map_size, map_x, map_y + map_size, 1.0f);
        dl.line (map_x, map_y + map_size, map_x, map_y, 1.0f);

        dl.color (1.0f, 0.58f, 0.12f, 0.96f);
        for (std::size_t point = 0; point < alignment.size (); ++point) {
          const Vec3 a = relative_alignment (alignment[point]);
          Vec3 b =
            relative_alignment (alignment[(point + 1) % alignment.size ()]);
          b[0] = a[0] + wrap_delta (b[0] - a[0], period_x);
          b[2] = a[2] + wrap_delta (b[2] - a[2], period_z);
          const Vec3 ma = map_point (a);
          const Vec3 mb = map_point (b);
          dl.line (ma[0], ma[1], mb[0], mb[1], 2.4f);
        }

        const Vec3 home_map = map_point (Vec3 (0, 0, 0));
        dl.color (1.0f, 0.9f, 0.45f, 1.0f);
        dl.begin (render::Prim::Quads);
        dl.vertex (home_map[0] - 3.0f, home_map[1] - 3.0f);
        dl.vertex (home_map[0] + 3.0f, home_map[1] - 3.0f);
        dl.vertex (home_map[0] + 3.0f, home_map[1] + 3.0f);
        dl.vertex (home_map[0] - 3.0f, home_map[1] + 3.0f);
        dl.end ();

        const Vec3 subject = subject_position ();
        const Vec3 relative_subject (
          wrap_delta (subject[0] - home_x, period_x),
          0,
          wrap_delta (subject[2] - home_z, period_z));
        Vec3 player = map_point (relative_subject);
        player[0] =
          std::clamp (player[0], map_x + 5.0f, map_x + map_size - 5.0f);
        player[1] =
          std::clamp (player[1], map_y + 5.0f, map_y + map_size - 5.0f);
        Vec3 heading = subject_heading ();
        heading[1] = 0.0f;
        if (length2 (heading) < 1e-5f)
          heading = Vec3 (0, 0, 1);
        heading = normalized (heading);
        const Vec3 side (-heading[2], 0, heading[0]);
        dl.color (0.35f, 0.95f, 1.0f, 1.0f);
        dl.begin (render::Prim::Triangles);
        dl.vertex (player[0] + heading[0] * 7.0f,
                   player[1] - heading[2] * 7.0f);
        dl.vertex (player[0] - heading[0] * 4.0f + side[0] * 4.0f,
                   player[1] + heading[2] * 4.0f - side[2] * 4.0f);
        dl.vertex (player[0] - heading[0] * 4.0f - side[0] * 4.0f,
                   player[1] + heading[2] * 4.0f + side[2] * 4.0f);
        dl.end ();
        dl.state (render::DrawState ());
        dl.lit (true);
        dl.fogged (true);
        dl.color (1, 1, 1, 1);
      }

      void generate_world (GenerationJob& job) {
        MOPPE_PROFILE_THREAD ("World generation");
        MOPPE_PROFILE_ZONE ("MoppeGame::generate_world");
        // Exceptions must not escape the GCD block (std::terminate).
        // A world that failed to generate is a broken build or broken
        // inputs, not a state to idle in: log and exit with failure.
        try {
          generate_world_inner (job);
        } catch (const std::exception& e) {
          std::cerr << "world generation failed: " << e.what () << std::endl;
          std::_Exit (-1);
        }
      }

      void publish_completed_world (std::unique_ptr<GeneratedWorld> world) {
        const std::lock_guard<std::mutex> lock (m_completed_world_mutex);
        if (m_completed_world)
          throw std::logic_error (
            "a completed world is already awaiting activation");
        m_completed_world = std::move (world);
      }

      void generate_world_inner (const GenerationJob& job) {
        MOPPE_PROFILE_ZONE ("MoppeGame::generate_world_inner");
        auto completed =
          std::make_unique<GeneratedWorld> (job.params, job.recipe);
        GeneratedWorld::Builder build = completed->build ();
        map::RandomHeightMap& terrain = build.terrain ();
        std::vector<std::vector<float>>& history = build.terrain_history ();
        std::optional<terrain::TrailNetwork> generated_trails;
        const terrain::WorldRecipe& recipe = completed->recipe ();
        std::unique_ptr<terrain::FieldEvaluator> field_evaluator;
        {
          MOPPE_PROFILE_ZONE ("startup.create_field_evaluator");
          field_evaluator = platform::create_field_evaluator ();
        }
        if (job.terrain_lab_preview) {
          set_loading_stage (LoadingStage::BuildingContinents);
          {
            MOPPE_PROFILE_ZONE ("startup.make_preview_program");
            map::TerrainEvaluator (terrain, field_evaluator.get ())
              .evaluate (recipe.terrain_program ());
          }
        } else {
          // Reuse the automatic build/profile/seed cache when possible.
          // MOPPE_MAPCACHE=<file> remains an explicit experiment override.
          const char* cache_override = ::getenv ("MOPPE_MAPCACHE");
          const std::string automatic_cache = terrain_cache_path (recipe);
          const char* cache =
            cache_override ? cache_override : automatic_cache.c_str ();
          set_loading_stage (LoadingStage::LookingForCache);
          bool cache_loaded = false;
          {
            MOPPE_PROFILE_ZONE ("startup.try_load_terrain_cache");
            cache_loaded = cache && terrain.try_load_cache (cache);
          }
          if (cache_loaded) {
            set_loading_stage (LoadingStage::ReadingCache);
            const std::size_t count =
              static_cast<std::size_t> (terrain.width ()) * terrain.height ();
            {
              MOPPE_PROFILE_ZONE ("startup.load_terrain_history");
              load_terrain_history (cache, count, history);
            }
            publish_loading_terrain (terrain);
          } else {
            set_loading_stage (LoadingStage::BuildingContinents);
            map::TerrainEvaluator evaluator (terrain, field_evaluator.get ());
            history.clear ();
            {
              MOPPE_PROFILE_ZONE ("startup.evaluate_terrain_program");
              evaluator.evaluate (
                recipe.terrain_program (),
                [this, &history, &terrain] (
                  std::size_t, const terrain::TerrainTransform& transform) {
                  const std::size_t count =
                    static_cast<std::size_t> (terrain.width ()) *
                    terrain.height ();
                  history.emplace_back (terrain.raw_heights (),
                                        terrain.raw_heights () + count);
                  publish_loading_terrain (terrain);
                  (void)transform;
                },
                [this] (std::size_t,
                        const terrain::TerrainTransform& transform,
                        int completed,
                        int total) {
                  if (std::holds_alternative<terrain::OrogenyEvolution> (
                        transform)) {
                    set_loading_stage (LoadingStage::EvolvingTerrain);
                    m_loading_work_done = completed;
                    m_loading_work_total = total;
                  }
                },
                [this] (std::size_t completed, std::size_t total) {
                  int observed = m_loading_source_done.load ();
                  const int value = static_cast<int> (completed);
                  while (observed < value &&
                         !m_loading_source_done.compare_exchange_weak (observed,
                                                                       value)) {
                  }
                  m_loading_source_total = static_cast<int> (total);
                });
            }
            generated_trails = evaluator.release_trail_network ();
            const std::size_t count =
              static_cast<std::size_t> (terrain.width ()) * terrain.height ();
            {
              MOPPE_PROFILE_ZONE ("startup.snapshot_finished_terrain");
              history.emplace_back (terrain.raw_heights (),
                                    terrain.raw_heights () + count);
            }
            if (cache) {
              set_loading_stage (LoadingStage::SavingTerrain);
              {
                MOPPE_PROFILE_ZONE ("startup.save_terrain_cache");
                terrain.save_cache (cache);
              }
              {
                MOPPE_PROFILE_ZONE ("startup.save_terrain_history");
                save_terrain_history (cache, history);
              }
            }
          }
        }
        if (history.empty ()) {
          const std::size_t count =
            static_cast<std::size_t> (terrain.width ()) * terrain.height ();
          history.emplace_back (terrain.raw_heights (),
                                terrain.raw_heights () + count);
        }
        publish_loading_terrain (terrain);
        set_loading_stage (LoadingStage::RebuildingSurface);
        {
          MOPPE_PROFILE_ZONE ("startup.recompute_terrain_normals");
          build.rebuild_surface ();
        }

        // The random world's sea and lakes are one priority-flood surface.
        // Keep this as a reading: terrain and erosion remain authoritative.
        if (!job.terrain_lab_preview) {
          build.analyze_hydrology (
            [this] (GeneratedWorld::HydrologyStage stage) {
              switch (stage) {
              case GeneratedWorld::HydrologyStage::StandingWater:
                set_loading_stage (LoadingStage::FindingStandingWater);
                break;
              case GeneratedWorld::HydrologyStage::Lakes:
                set_loading_stage (LoadingStage::CataloguingLakes);
                break;
              case GeneratedWorld::HydrologyStage::Drainage:
                set_loading_stage (LoadingStage::TracingDrainage);
                break;
              case GeneratedWorld::HydrologyStage::Waterways:
                set_loading_stage (LoadingStage::ConnectingWaterways);
                break;
              case GeneratedWorld::HydrologyStage::Channels:
              case GeneratedWorld::HydrologyStage::Rivers:
                set_loading_stage (LoadingStage::ExtractingRivers);
                break;
              }
            });
          {
            // A one-line hydrology reading at load: pond explosions from
            // erosion regressions show up here before any capture does.
            const auto& hydrology = completed->hydrology ();
            if (!hydrology)
              throw std::logic_error ("completed world has no hydrology");
            std::size_t wet = 0;
            for (const terrain::WaterBody& body : hydrology->lakes ().bodies)
              wet += terrain::count_value (body.cells);
            std::cerr << "standing water: "
                      << hydrology->lakes ().bodies.size () << " bodies, "
                      << wet << " wet cells\n";
          }
        }

        set_loading_stage (LoadingStage::AssemblingWorld);
        build.materialize_analyses (std::move (generated_trails));
        publish_completed_world (std::move (completed));
      }

      void activate_completed_world () {
        MOPPE_PROFILE_ZONE ("MoppeGame::activate_completed_world");
        std::unique_ptr<GeneratedWorld> completed;
        {
          const std::lock_guard<std::mutex> lock (m_completed_world_mutex);
          completed = std::move (m_completed_world);
        }
        if (!completed)
          throw std::logic_error ("no completed world to activate");

        // The Lab stores raw borrows into the live world.  Let it restore and
        // release them before the outgoing owner is allowed to retire.
        if (m_terrain_lab.active ())
          m_terrain_lab.leave ();

        // Keep the outgoing session and world alive until the new session has
        // bound to the completed world. The session owns every direct
        // terrain/surface borrower, so it must retire before its old world.
        std::unique_ptr<GeneratedWorld> retired_world =
          std::move (m_generated_world);
        std::unique_ptr<GameSession> retired_session = std::move (m_session);
        m_generated_world = std::move (completed);
        m_session =
          std::make_unique<GameSession> (world (), map (), surface ());
        retired_session.reset ();
        retired_world.reset ();
      }

      void finish_setup () {
        MOPPE_PROFILE_ZONE ("MoppeGame::finish_setup");
        render::Renderer& r = *m_renderer;

        // Running rivers are continuous ribbon meshes. The water sheets below
        // retain standing bodies and carry each mouth's current into them.
        if (rivers ()) {
          MOPPE_PROFILE_ZONE ("startup.rebuild_river_ribbons");
          m_river_surface.rebuild (r, map (), *rivers ());
        }
        m_water_presentation.reset (world ().water_level, world ().map_size);
        if (const auto& water = generated_world ().water_surface ())
          m_water_presentation.refresh (*water, map ().scale ()[1] * u::m);
        m_surface_presentation.refresh (surface ());

        session ().bike ().set_water_level (world ().water_level);
        session ().car ().set_water_level (world ().water_level);
        session ().bike ().set_obstacles (&m_obstacles);
        session ().car ().set_obstacles (&m_obstacles);

        if (!m_terrain_lab_preview && m_water_shot) {
          m_water_inspection = choose_water_inspection (*m_water_shot,
                                                        map (),
                                                        *standing_water (),
                                                        *lake_census (),
                                                        *drainage (),
                                                        *rivers ());
          if (!m_water_inspection)
            throw std::runtime_error (
              "no " + std::string (water_shot_name (*m_water_shot)) +
              " available for water screenshot");
          std::cerr << "water screenshot: " << water_shot_name (*m_water_shot)
                    << " cell=" << m_water_inspection->cell
                    << " score=" << m_water_inspection->score << '\n';
        }

        if (!m_terrain_lab_preview) {
          set_loading_stage (LoadingStage::PlacingStars);
          MOPPE_PROFILE_ZONE ("startup.generate_stars");
          session ().stars ().generate (map (), world (), 80);
        }

        if (!m_terrain_lab_preview) {
          if (trail_network ()) {
            m_home_base_position =
              trail_cell_position (trail_network ()->plan.home_base);
            m_spawn_position =
              m_home_base_position - trail_direction_from_home () * 8.0f;
            m_spawn_position[1] = map ().interpolated_height (
                                    m_spawn_position[0], m_spawn_position[2]) +
                                  1.2f;
          } else {
            m_spawn_position = choose_landscape_spawn ();
            m_home_base_position = m_spawn_position - Vec3 (0, 1.2f, 0);
          }
          session ().bike ().reset (m_spawn_position);
          session ().bike ().set_heading (
            trail_network () ? trail_direction_from_home () : Vec3 (0, 0, 1));
        }
        if (m_tree_demo) {
          MOPPE_PROFILE_ZONE ("startup.build_tree_grove");
          m_tree_stand.rebuild (
            r, surface (), recipe ().seed ().value ^ 0x4f1bbcdcU, m_tree_count);
          if (m_tree_stand.empty ())
            throw std::runtime_error (
              "the generated surface has no viable tree habitat");
          const TreeGrove& grove = m_tree_stand.grove ();
          session ().camera ().place (grove.camera_eye, grove.camera_target);
          std::cerr << "tree grove: " << grove.sites.size ()
                    << " organisms, eye=" << grove.camera_eye
                    << " target=" << grove.camera_target << '\n';
        } else if (m_water_inspection)
          session ().camera ().place (m_water_inspection->eye,
                                      m_water_inspection->target);
        else if (!m_terrain_lab_preview) {
          {
            MOPPE_PROFILE_ZONE ("startup.build_global_forest");
            m_forest.rebuild (
              r, surface (), recipe ().seed ().value ^ 0xa34c91e5U);
            std::cerr << "global forest: " << m_forest.tree_count ()
                      << " canopy representatives\n";
          }
          MOPPE_PROFILE_ZONE ("startup.build_forest");
          constexpr std::size_t forest_size = 32;
          m_tree_stand.rebuild (r,
                                surface (),
                                recipe ().seed ().value ^ 0x4f1bbcdcU,
                                forest_size,
                                m_home_base_position);
          const TreeGrove& forest = m_tree_stand.grove ();
          const auto cohort_count = [&] (TreeCohort cohort) {
            return std::ranges::count_if (forest.sites,
                                          [cohort] (const TreeSite& site) {
                                            return site.cohort == cohort;
                                          });
          };
          std::cerr << "forest: " << forest.sites.size () << " organisms ("
                    << cohort_count (TreeCohort::canopy) << " canopy, "
                    << cohort_count (TreeCohort::young) << " young, "
                    << cohort_count (TreeCohort::sapling) << " saplings)\n";
        }

        if (standing_water () && lake_census () && drainage () && rivers ()) {
          MOPPE_PROFILE_ZONE ("startup.plan_cinematic_flight");
          m_cinematic_plan = plan_cinematic_flight (map (),
                                                    *standing_water (),
                                                    *lake_census (),
                                                    *drainage (),
                                                    *rivers (),
                                                    m_spawn_position,
                                                    trail_network ());
        }
        if (!m_cinematic_plan.empty ()) {
          std::cerr << "cinematic flight: "
                    << m_cinematic_plan.waypoints.size () << " gates through ";
          for (std::size_t i = 0; i < m_cinematic_plan.landmarks.size (); ++i) {
            if (i)
              std::cerr << ", ";
            std::cerr << cinematic_landmark_name (
              m_cinematic_plan.landmarks[i].kind);
          }
          std::cerr << '\n';
        }
        m_setup_complete = true;
        remember_seed (world (),
                       recipe ().generation_profile (),
                       static_cast<int> (recipe ().seed ().value));
        if (::getenv ("MOPPE_REGENERATE_ONCE") &&
            !m_automated_regeneration_done) {
          m_automated_regeneration_done = true;
          regenerate_world ();
        }
      }

      void upload_world_terrain (render::Renderer& r) {
        MOPPE_PROFILE_ZONE ("startup.upload_world_terrain");
        m_terrain.setup (r, map (), world (), m_graphics);
        // The typed water and ground presentations can upload only after
        // set_terrain has established the texture dimensions.
        m_water_presentation.upload (r);
        m_surface_presentation.upload (r, !m_water_shot);
      }

      void cast_world_shadows (render::Renderer& r) {
        MOPPE_PROFILE_ZONE ("startup.cast_world_shadows");
        m_terrain.render_shadow (
          r, map (), sun_direction_for (m_graphics.sun_height));
      }

      // -- simulation --------------------------------------------------

      void tick (float dt) override {
        MOPPE_PROFILE_ZONE ("MoppeGame::tick");
        std::optional<InputFrame> scripted_input;
        if (m_cinematic.active () && ::getenv ("MOPPE_CINEMATIC_CAPTURE_DIR")) {
          const int fps = [] {
            if (const char* value = ::getenv ("MOPPE_CINEMATIC_CAPTURE_FPS"))
              return std::clamp (::atoi (value), 1, 120);
            return 30;
          }();
          dt = 1.0f / fps;
        }
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
            scripted_input = benchmark_input (m_benchmark_frame);
            m_benchmark_measured =
              m_benchmark_frame >= m_benchmark->settle_frames;
          } else {
            scripted_input = benchmark_input (m_benchmark_prelude_frame);
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
        logic ().m_frame_time = dt;
        if (!m_ready || logic ().m_game_over)
          return;

        InputFrame input = m_live_input.take_frame ();
        if (scripted_input)
          input = *scripted_input;

        logic ().m_total_time += dt;
        const float total_time = logic ().m_total_time;

        // Weather remains part of the world while actors are paused.  This
        // also initializes the shared horizon color when the game starts
        // directly in Terrain Lab.
        float cloudiness =
          std::sin (total_time * 0.0003f) * 0.4f + 0.5f +
          0.3f * std::pow (std::sin (total_time * 0.0008f), 2.0f) +
          std::sin (total_time * 0.02f) * 0.05f;
        clamp (cloudiness, 0.0f, 1.0f);
        logic ().m_cloudiness = cloudiness;

        // Fog stays mostly sky-blue.  Directional warmth is added in
        // the shaders only when looking toward the sun.
        const DisplayColor horizon = horizon_color_for (m_graphics.sun_height);
        logic ().m_fog =
          mix_display (horizon, DisplayColor (0.90f, 0.94f, 1.0f), 0.18f);

        if (m_cinematic.active ()) {
          if (input.leave_cinematic) {
            leave_cinematic ();
            input = {};
          } else {
            const CinematicFlightControls controls {
              .lateral = input_value (input.turn),
              .lift = input_value (input.boost),
              .pace = input_value (input.drive),
            };
            m_cinematic.tick (dt, map (), controls);
            if (!m_cinematic.active ())
              leave_cinematic ();
            return;
          }
        }

        // Terrain inspection pauses actors and vehicle physics, but keeps the
        // visual clock, weather, and fog alive so sky and water remain a
        // useful frame of reference around the heightmap.
        if (m_terrain_lab.active ()) {
          m_terrain_lab.tick (dt);
          return;
        }

        // The botanical demo is a stationary observatory. Weather and the
        // global animation clock continue above, so the retained trees keep
        // moving in the renderer's wind field while actors remain paused.
        if (m_tree_demo) {
          const TreeGrove& grove = m_tree_stand.grove ();
          session ().camera ().place (grove.camera_eye, grove.camera_target);
          return;
        }

        // Screenshot autopilot for headless verification: rides in a
        // lazy arc with periodic boost-assisted leaps.
        static const bool demo = ::getenv ("MOPPE_DEMO") != 0;
        if (demo && !m_water_inspection) {
          input = {
            .turn = 0.35f * std::sin (total_time * 0.25f),
            .drive = 1.0f,
            .boost = std::fmod (total_time, 11.0f) < 1.35f ? 1.0f : 0.0f,
          };
        }

        apply_input_frame (input);

        session ().bike ().update (seconds (dt));
        if (logic ().m_car_exists)
          session ().car ().update (seconds (dt));
        if (logic ().m_mode == M_GLIDER &&
            session ().glider ().update (seconds (dt)))
          finish_glide ();
        if (logic ().m_mode == M_FOOT)
          session ().walker ().update (
            seconds (dt), map (), m_obstacles, world ());

        const Vec3 vpos = subject_position ();
        mov::Vehicle& av = active_vehicle ();

        // Parked vehicles' impacts shouldn't linger until remount.
        if (logic ().m_mode != M_BIKE) {
          session ().bike ().pop_impact ();
          session ().bike ().pop_fall_drop ();
        }
        if (logic ().m_car_exists && logic ().m_mode != M_CAR) {
          session ().car ().pop_impact ();
          session ().car ().pop_fall_drop ();
        }

        const bool in_water =
          vpos[1] < meters_value (world ().water_level) + 1.0f;
        const bool driving =
          logic ().m_mode == M_BIKE || logic ().m_mode == M_CAR;

        // Long jumps become score events after three seconds. Keep the last
        // airborne time locally because Vehicle clears its timer on touchdown.
        if (driving && av.airtime () > 0.0f) {
          logic ().m_jump_airtime = av.airtime ();
          logic ().m_landed_age += dt;
        } else {
          if (driving && logic ().m_jump_airtime >= 3.0f) {
            logic ().m_landed_airtime = logic ().m_jump_airtime;
            logic ().m_landed_points = (int)std::round (
              100.0f * logic ().m_jump_airtime * logic ().m_jump_airtime);
            logic ().m_score += logic ().m_landed_points;
            logic ().m_landed_age = 0.0f;
          }
          logic ().m_jump_airtime = 0.0f;
          logic ().m_landed_age += dt;
        }
        const DisplayColor dust_color (0.60f, 0.52f, 0.40f);
        const DisplayColor clod_color (0.42f, 0.34f, 0.24f);
        const DisplayColor spray_color (0.85f, 0.92f, 1.0f);
        const Vec3 fwd = subject_heading ();
        const Vec3 rear_wheel = vpos - fwd * 1.4f + Vec3 (0, -0.7f, 0);

        // Drift kicks up dirt from the rear wheel (or spray).
        if (driving && av.grounded () && av.drift_speed () > 6.0f) {
          int n = std::min (4, (int)(av.drift_speed () * 0.2f));
          session ().dust ().emit (position (rear_wheel),
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
            session ().dust ().emit (position (rear_wheel),
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
          if (chance (logic ().m_fx_rng) < scalar_value (spark)) {
            Dust::Style ember;
            ember.size = 0.15f * u::m;
            ember.lifetime = 0.45f * u::s;
            ember.downward_acceleration =
              6.0f * isq::acceleration[u::m / pow<2> (u::s)];
            ember.spread = 0.3f * one;
            ember.additive = true;
            session ().dust ().emit (
              position (vpos - fwd * 0.5f + Vec3 (0, -0.5f, 0)),
              velocity (av.velocity () * 0.5f - fwd * 2.0f +
                        Vec3 (0, -4.0f, 0)),
              1,
              DisplayColor (1.0f, 0.55f, 0.18f),
              ember);
          }
        }

        // Exhaust smoke: faint gray puffs that rise off the muffler
        // while the throttle is open.
        if (driving && abs (av.thrust ()) > 0.3f && logic ().m_mode == M_BIKE) {
          std::uniform_real_distribution<float> chance (0.0f, 1.0f);
          const probability_t puff (14.0f / u::s * (dt * u::s));
          if (chance (logic ().m_fx_rng) < scalar_value (puff)) {
            Dust::Style smoke;
            smoke.size = 0.35f * u::m;
            smoke.lifetime = 0.8f * u::s;
            smoke.downward_acceleration =
              -2.5f * isq::acceleration[u::m / pow<2> (u::s)]; // buoyant
            smoke.spread = 0.25f * one;
            session ().dust ().emit (
              position (vpos - fwd * 1.2f + Vec3 (0, -0.4f, 0)),
              velocity (av.velocity () * 0.25f),
              1,
              DisplayColor (0.45f, 0.45f, 0.48f),
              smoke);
          }
        }

        // Wading fast throws up a bow wave.
        if (driving && in_water && length (av.velocity ()) > 15.0f)
          session ().dust ().emit (position (vpos + Vec3 (0, -0.5f, 0)),
                                   velocity (av.velocity () * 0.3f),
                                   3,
                                   spray_color);

        // Hard landings shake the camera and burst dirt outward:
        // a low pancake of dust plus a ring of ballistic clods.
        const float impact = driving ? av.pop_impact () : 0.0f;
        if (impact > 8.0f) {
          logic ().m_shake = std::min (0.28f, 0.010f * impact);
          logic ().m_shake_time = 0.0f;
          session ().dust ().emit (position (vpos + Vec3 (0, -0.7f, 0)),
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
            session ().dust ().emit (
              position (vpos + Vec3 (0, -0.6f, 0)),
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
          logic ().m_health -= (impact - 9.0f) * 4.5f;
        if (driving && av.pop_fall_drop () > 100.0f)
          logic ().m_health = 0.0f;
        logic ().m_health = std::min (100.0f, logic ().m_health + 1.5f * dt);
        if (logic ().m_health <= 0.0f) {
          session ().dust ().emit (position (vpos),
                                   velocity (Vec3 (0, 6, 0)),
                                   40,
                                   DisplayColor (1.0f, 0.5f, 0.1f));
          --logic ().m_lives;
          if (logic ().m_lives <= 0) {
            logic ().m_game_over = true;
          } else {
            // Halfway through the hearts, the game offers its
            // sympathies out loud.
            if (logic ().m_lives == 5)
              platform::say ("Ouchies. That hurts.");

            // Respawn where you crashed, upright on the ground.
            // (Deaths used to teleport you 600 m above the map
            // corner -- it read as falling through the cosmos.)
            const float ground = map ().interpolated_height (vpos[0], vpos[2]);
            av.reset (Vec3 (vpos[0], ground + 1.2f, vpos[2]));
            logic ().m_health = 100.0f;
            logic ().m_shake = 1.0f;
            logic ().m_shake_time = 0.0f;
          }
        }

        // Star pickups sparkle gold and top up fuel and boost reserves.
        {
          const int picked =
            session ().stars ().update (vpos, logic ().m_total_time, dt);
          if (picked > 0) {
            Dust::Style sparkle;
            sparkle.size = 0.38f * u::m;
            sparkle.lifetime = 0.85f * u::s;
            sparkle.downward_acceleration =
              -1.5f * isq::acceleration[u::m / pow<2> (u::s)];
            sparkle.spread = 1.7f * one;
            sparkle.additive = true;
            session ().dust ().emit (position (session ().stars ().last_pos ()),
                                     velocity (Vec3 (0, 4, 0)),
                                     32,
                                     DisplayColor (1.0f, 0.72f, 0.12f),
                                     sparkle);
            Dust::Style flash;
            flash.size = 0.9f * u::m;
            flash.lifetime = 0.35f * u::s;
            flash.spread = 0.25f * one;
            flash.additive = true;
            session ().dust ().emit (position (session ().stars ().last_pos ()),
                                     velocity (Vec3 ()),
                                     5,
                                     DisplayColor (1.0f, 0.95f, 0.55f),
                                     flash);
            logic ().m_fuel =
              std::min (100.0f, logic ().m_fuel + 25.0f * picked);
            if (logic ().m_mode != M_GLIDER)
              av.replenish_boost (0.25f * picked);
          }
        }

        // Fuel: the throttle burns it; an empty tank limps along at
        // a third power (never fully stranded).
        if (driving) {
          logic ().m_fuel = std::max (
            0.0f,
            logic ().m_fuel - scalar_value (abs (av.thrust ())) * 0.9f * dt);
          logic ().m_odometer += length (av.velocity ()) * dt;

          const float want =
            logic ().m_go_input *
            ((logic ().m_fuel <= 0.5f && logic ().m_go_input > 0) ? 0.3f
                                                                  : 1.0f);
          av.set_thrust (want);
        }

        session ().dust ().update (seconds (dt));
        logic ().m_shake_time += dt;
        logic ().m_shake *= decay (7.0f / u::s, dt * u::s);

        if (logic ().m_cam_mode == CAM_HELMET) {
          // Ride inside the rider's head; lightly smoothed so
          // terrain bumps don't rattle the eyeballs.
          Vec3 eye, look;
          if (logic ().m_mode == M_FOOT) {
            eye = session ().walker ().position () +
                  Vec3 (0, 1.55f / m_landscape_scale_y, 0);
            look = session ().walker ().heading ();
          } else if (logic ().m_mode == M_GLIDER) {
            eye = session ().glider ().position () -
                  Vec3 (0, 0.75f / m_landscape_scale_y, 0);
            look = session ().glider ().heading ();
          } else {
            eye = av.position () + Vec3 (0, 0.95f / m_landscape_scale_y, 0) +
                  av.orientation () * (0.4f / m_landscape_scale_x);
            look = av.orientation ();
          }
          logic ().m_fp_eye =
            logic ().m_fp_eye + (eye - logic ().m_fp_eye) *
                                  smoothing_alpha (25.0f / u::s, dt * u::s);
          session ().camera ().place (logic ().m_fp_eye,
                                      logic ().m_fp_eye + look * 10.0f);
        } else {
          session ().camera ().set_landscape_scale (m_landscape_scale_x,
                                                    m_landscape_scale_y);
          const float flip = (logic ().m_cam_mode == CAM_FRONT) ? -1.0f : 1.0f;
          if (logic ().m_mode == M_FOOT)
            session ().camera ().update (
              position (session ().walker ().position () +
                        Vec3 (0, 1.0f / m_landscape_scale_y, 0)),
              session ().walker ().heading () * flip,
              velocity (Vec3 ()),
              seconds (dt));
          else if (logic ().m_mode == M_GLIDER)
            session ().camera ().update (
              session ().glider ().physical_position (),
              session ().glider ().heading () * flip,
              session ().glider ().physical_velocity (),
              seconds (dt));
          else
            session ().camera ().update (av.physical_position (),
                                         av.orientation () * flip,
                                         av.physical_velocity (),
                                         seconds (dt));
          session ().camera ().limit (map ());
        }
        if (m_water_inspection) {
          session ().camera ().place (m_water_inspection->eye,
                                      m_water_inspection->target);
          session ().camera ().limit (map ());
        }

        // Speed widens the field of view a touch.
        {
          const float kmh = (driving || logic ().m_mode == M_GLIDER)
                              ? subject_speed_kmh ()
                              : 0.0f;
          const float k =
            std::min (1.0f, std::max (0.0f, (kmh - 70.0f) / 180.0f));
          logic ().m_fov_k +=
            (k - logic ().m_fov_k) * smoothing_alpha (5.0f / u::s, dt * u::s);
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
        if (logic ().m_game_over) {
          render_game_over (r);
          return;
        }

        render::FrameParams fp;
        const float aspect =
          (float)r.width_pts () / std::max (1, r.height_pts ());
        const bool terrain_lab = m_terrain_lab.active ();
        const bool cinematic = m_cinematic.active ();
        const float fov = cinematic ? m_cinematic.field_of_view ()
                          : terrain_lab || m_water_inspection || m_tree_demo
                            ? 70.0f
                            : 100.0f + 9.0f * logic ().m_fov_k;
        fp.proj = Mat4::perspective_reversed (
          fov * u::deg,
          aspect,
          std::clamp (0.5f /
                        std::max (m_landscape_scale_x, m_landscape_scale_y),
                      0.02f,
                      0.5f),
          terrain_lab ? 30000.0f : 9000.0f);

        // Hard landings produce a brief continuous vibration. Randomizing the
        // rotations each frame looked like violent camera teleportation.
        Mat4 view = cinematic     ? m_cinematic.view_matrix ()
                    : terrain_lab ? m_terrain_lab.view_matrix ()
                                  : session ().camera ().view_matrix ();
        if (!cinematic && !terrain_lab && !m_water_inspection && !m_tree_demo &&
            logic ().m_shake > 0.005f) {
          const Vec3 cam = session ().camera ().position ();
          const float ground = map ().interpolated_height (cam[0], cam[2]);
          const float clearance = cam[1] - ground;
          const float room =
            std::min (1.0f, std::max (0.0f, (clearance - 2.0f) / 8.0f));

          const float pulse = logic ().m_shake * room;
          const float roll =
            pulse * std::sin (2.0f * PI * 15.0f * logic ().m_shake_time);
          const float pitch =
            pulse * 0.55f *
            std::sin (2.0f * PI * 19.0f * logic ().m_shake_time + 0.7f);
          view = view * Mat4::rotation (roll * u::deg, Vec3 (0, 0, 1)) *
                 Mat4::rotation (pitch * u::deg, Vec3 (1, 0, 0));
        }
        fp.view = view;

        const Vec3 cam = cinematic     ? m_cinematic.position ()
                         : terrain_lab ? m_terrain_lab.position ()
                                       : session ().camera ().position ();
        fp.camera_pos = cam;
        fp.cam_right = Vec3 (view.m[0], view.m[4], view.m[8]);
        fp.cam_up = Vec3 (view.m[1], view.m[5], view.m[9]);
        fp.cam_forward = Vec3 (-view.m[2], -view.m[6], -view.m[10]);
        // The lab's plane views render the game's own sky and haze; the
        // torus is an abstract inspection object on a dark backdrop.
        fp.clear_color = terrain_lab && m_terrain_lab.torus_view ()
                           ? DisplayColor (0.012f, 0.016f, 0.022f)
                           : logic ().m_fog;
        const attenuation_t scene_fog =
          terrain_lab ? m_terrain_lab.scene_fog (world ().fog_scale)
                      : world ().fog_scale;
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
        fp.time = logic ().m_total_time;
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
          if (cam[1] < meters_value (world ().water_level))
            vis = 0.0f;
          else {
            for (int i = 1; i <= 40; ++i) {
              const float t = 90.0f * i;
              const Vec3 p = cam + fp.sun_dir * t;
              if (!map ().in_bounds (p[0], p[2]))
                break;
              if (map ().interpolated_height (p[0], p[2]) > p[1]) {
                vis = 0.0f;
                break;
              }
            }
          }
          vis *= 1.0f - 0.65f * logic ().m_cloudiness;
          logic ().m_flare += (vis - logic ().m_flare) * 0.12f;
          fp.sun_visibility = terrain_lab ? 0.0f : logic ().m_flare;
        }

        static const int screenshot_delay = [] {
          if (const char* frames = ::getenv ("MOPPE_SCREENSHOT_FRAMES"))
            return std::max (1, ::atoi (frames));
          return 30;
        }();
        const bool captured = !m_screenshot_path.empty () &&
                              ++m_screenshot_frames >= screenshot_delay;
        bool captured_cinematic = false;
        if (cinematic) {
          if (const char* directory =
                ::getenv ("MOPPE_CINEMATIC_CAPTURE_DIR")) {
            if (m_cinematic_capture_frame < cinematic_capture_frame_limit ()) {
              if (m_cinematic_capture_frame == 0)
                std::filesystem::create_directories (directory);
              std::ostringstream path;
              path << directory << "/frame-" << std::setfill ('0')
                   << std::setw (5) << m_cinematic_capture_frame++ << ".png";
              r.request_screenshot (path.str ());
              captured_cinematic = true;
            }
          }
        }
        if (captured) {
          if (m_water_inspection)
            std::cerr << "water screenshot camera: eye="
                      << session ().camera ().position ()
                      << " target=" << m_water_inspection->target << '\n';
          if (m_tree_demo)
            std::cerr << "tree screenshot camera: eye="
                      << session ().camera ().position ()
                      << " target=" << m_tree_stand.grove ().camera_target
                      << '\n';
          r.request_screenshot (m_screenshot_path);
        }
        if (!r.begin_frame (fp))
          return;

        FrameEnv env;
        env.fog_color = logic ().m_fog;
        env.fog_scale = scene_fog;
        env.sun_dir = fp.sun_dir;
        env.camera_pos = position (cam);
        env.cam_right = fp.cam_right;
        env.cam_up = fp.cam_up;
        env.cam_forward = fp.cam_forward;
        env.time = seconds (logic ().m_total_time);

        const auto draw_world_sky = [&] {
          render::SkyParams sky;
          sky.time = logic ().m_total_time;
          sky.sun_height = m_graphics.sun_height;
          // A world-shaping overview should keep the game world's moving sky,
          // without letting a passing front hide the land being edited.
          sky.cloudiness = terrain_lab ? std::min (logic ().m_cloudiness, 0.35f)
                                       : logic ().m_cloudiness;
          sky.sun_dir = fp.sun_dir;
          sky.fog_color = logic ().m_fog;
          r.draw_sky (sky);
        };

        // At this extreme altitude, drawing the far-plane dome after terrain
        // exposes depth precision at the horizon.  Paint it first in the lab;
        // terrain then covers it deterministically.  Gameplay retains the
        // cheaper depth-culled order below.
        if (terrain_lab && !m_terrain_lab.torus_view ())
          draw_world_sky ();

        // Terrain first, chunk-culled to the haze horizon.
        m_terrain.render (r,
                          cam,
                          cinematic     ? m_cinematic.forward ()
                          : terrain_lab ? m_terrain_lab.forward ()
                                        : session ().camera ().forward (),
                          terrain_lab
                            ? 30000.0f
                            : 3.0f / attenuation_value (world ().fog_scale));

        // Sky AFTER the terrain: depth testing kills the expensive
        // cloud shader wherever terrain covers it.
        if (!terrain_lab)
          draw_world_sky ();

        if (!terrain_lab && !m_water_inspection && !m_tree_demo)
          m_forest.draw (r,
                         cam,
                         cinematic ? m_cinematic.forward ()
                                   : session ().camera ().forward ());

        if (!terrain_lab && !m_water_inspection)
          m_tree_stand.draw (r);

        if (!terrain_lab && !m_water_inspection && !m_tree_demo) {
          // The world draw list, in the GL build's draw order.  Terrain lab
          // deliberately hides every placed object so generator differences are
          // not confused with stale actor positions.
          m_world_dl.clear ();

          // Soft blob shadows under the movers.
          draw_home_base_marker (m_world_dl);
          m_blob.draw (
            m_world_dl, map (), session ().bike ().position (), 2.2f);
          if (logic ().m_car_exists)
            m_blob.draw (
              m_world_dl, map (), session ().car ().position (), 2.9f);
          if (logic ().m_mode == M_FOOT)
            m_blob.draw (m_world_dl,
                         map (),
                         session ().walker ().position () + Vec3 (0, 0.5f, 0),
                         0.8f);
          if (logic ().m_mode == M_GLIDER)
            m_blob.draw (
              m_world_dl, map (), session ().glider ().position (), 3.4f);

          // In helmet cam you ARE the rider: don't draw yourself.
          const bool helmet = (logic ().m_cam_mode == CAM_HELMET);
          if (!(helmet && logic ().m_mode == M_BIKE))
            render_vehicle (r,
                            m_world_dl,
                            session ().bike (),
                            logic ().m_total_time,
                            landscape_visual_scale ());
          if (logic ().m_car_exists && !(helmet && logic ().m_mode == M_CAR))
            render_vehicle (r,
                            m_world_dl,
                            session ().car (),
                            logic ().m_total_time,
                            landscape_visual_scale ());
          if (logic ().m_mode == M_FOOT && !helmet)
            session ().walker ().render (
              m_world_dl, logic ().m_total_time, landscape_visual_scale ());
          if (logic ().m_mode == M_GLIDER && !helmet)
            render_glider (m_world_dl,
                           session ().glider (),
                           logic ().m_total_time,
                           landscape_visual_scale ());

          r.draw_list (m_world_dl);

          // Additive glow after the solid list, so it blends over
          // everything already drawn: exhaust and jump-jet flames, then
          // the star pickups' halos.
          if (m_graphics.vehicle_effects &&
              !(helmet && logic ().m_mode == M_BIKE))
            render_vehicle_flames (r,
                                   session ().bike (),
                                   logic ().m_total_time,
                                   landscape_visual_scale ());
          if (m_graphics.vehicle_effects && logic ().m_car_exists &&
              !(helmet && logic ().m_mode == M_CAR))
            render_vehicle_flames (r,
                                   session ().car (),
                                   logic ().m_total_time,
                                   landscape_visual_scale ());
          if (m_graphics.star_effects)
            session ().stars ().render (r, env);
        }

        // The lab keeps the game's painted water while the map is the
        // game's own; a rebuilt map invalidates the water sheets, so
        // they disappear until the lab's own analysis draws ribbons.
        const bool draw_ocean =
          m_graphics.ocean && (!terrain_lab || (!m_terrain_lab.torus_view () &&
                                                m_terrain_lab.map_pristine ()));
        if (draw_ocean) {
          render::OceanParams ocean;
          ocean.time = logic ().m_total_time;
          ocean.fog_color = logic ().m_fog;
          ocean.fog_scale = attenuation_value (scene_fog);
          if (world ().toroidal ()) {
            const Vec3& world_extent = extent_value (world ().map_size);
            const Vec3 center (
              0.5f * world_extent[0], 0, 0.5f * world_extent[2]);
            ocean.world_offset[0] = cam[0] - center[0];
            ocean.world_offset[2] = cam[2] - center[2];
          }
          r.draw_ocean (ocean);
        }

        // Standing water writes depth first. Rivers then shade only their
        // visible surface, rather than paying for fragments hidden beneath a
        // lake or the sea, and retain a clean current layer through mouths.
        if (terrain_lab)
          m_terrain_lab.render_rivers (r, cam);
        else if (m_graphics.river_ribbons)
          m_river_surface.draw (r, cam);

        // Dust last so spray sits atop every water surface.
        if (!terrain_lab && m_graphics.particles) {
          session ().dust ().render (r);
        }

        // Post effects.
        if (!terrain_lab && cam[1] < meters_value (world ().water_level))
          r.apply_underwater (logic ().m_total_time);
        if (!terrain_lab) {
          float blur = 0.0f;
          if (cinematic)
            blur = m_cinematic.motion_blur ();
          else {
            const float kmh = subject_speed_kmh ();
            blur = std::clamp ((kmh - 90.0f) / 160.0f, 0.0f, 1.0f);
          }
          if (m_graphics.motion_blur && blur > 0.01f)
            r.apply_motion_blur (blur);
        }

        // HUD, kept inside the safe area (notch / home indicator).
        m_hud_dl.clear ();
        const platform::Insets si = platform::safe_insets ();
        m_hud_dl.translate (si.left, si.top, 0);
        const int hud_width = r.width_pts () - (int)(si.left + si.right);
        const int hud_height = r.height_pts () - (int)(si.top + si.bottom);
        if (terrain_lab) {
          m_terrain_lab.draw (m_hud_dl, hud_width, hud_height);
        } else if (cinematic) {
          if (m_loading_font && m_loading_font->ok ()) {
            const std::string prompt = "SPACE TO RIDE";
            const float alpha = std::clamp (
              1.0f - std::max (0.0f, m_cinematic.elapsed () - 4.0f) / 2.0f,
              0.0f,
              0.72f);
            m_hud_dl.color (0.91f, 1.0f, 0.92f, alpha);
            m_loading_font->draw (m_hud_dl,
                                  hud_width - m_loading_font->measure (prompt) -
                                    28.0f,
                                  hud_height - 42.0f,
                                  prompt);
          }
        } else if (!m_water_inspection && !m_tree_demo) {
          HudState hs;
          hs.speed_kmh = subject_speed_kmh ();
          hs.fuel = logic ().m_fuel;
          if (logic ().m_mode == M_GLIDER) {
            const float lift =
              session ().glider ().air_mass_lift ().numerical_value_in (u::m /
                                                                        u::s);
            hs.boost_ready01 = std::clamp (lift / 4.0f, 0.0f, 1.0f);
          } else {
            hs.boost_ready01 = (logic ().m_mode == M_FOOT)
                                 ? 1.0f
                                 : active_vehicle ().boost_charge ();
          }
          hs.health01 = logic ().m_health / 100.0f;
          hs.odometer_m = (float)logic ().m_odometer;
          hs.lives = logic ().m_lives;
          hs.stars = session ().stars ().collected ();
          hs.score = logic ().m_score;
          hs.airtime_s = logic ().m_jump_airtime;
          hs.landed_airtime_s = logic ().m_landed_airtime;
          hs.landed_points = logic ().m_landed_points;
          hs.landed_age_s = logic ().m_landed_age;
          hs.on_foot = (logic ().m_mode == M_FOOT);
          hs.gliding = (logic ().m_mode == M_GLIDER);
          hs.can_deploy_glider = can_deploy_glider ();
          hs.vertical_speed_mps =
            logic ().m_mode == M_GLIDER
              ? session ().glider ().vertical_speed ().numerical_value_in (
                  u::m / u::s)
              : 0.0f;
          hs.frame_time_s = logic ().m_frame_time;
          const Vec3 heading = subject_heading ();
          hs.heading_radians = std::atan2 (heading[0], heading[2]);
          m_hud.draw (m_hud_dl, hs, hud_width, hud_height);
          draw_trail_map (m_hud_dl, hud_width, hud_height);
          if (m_game_ui_open) {
            if (!m_game_ui_window_positioned) {
              m_game_ui_window.set_position (
                std::max (24.0f, hud_width - 384.0f), 24.0f);
              m_game_ui_window_positioned = true;
            }
            m_game_ui_window.set_size (360.0f, 224.0f);
            m_game_ui_window.constrain (hud_width, hud_height);
            const UiRect horizontal { 20, 58, 320, 64 };
            const UiRect vertical { 20, 132, 320, 64 };
            const float local_pointer_x =
              m_game_ui_window.local_x (m_pointer_x);
            const float local_pointer_y =
              m_game_ui_window.local_y (m_pointer_y);
            std::ostringstream horizontal_label;
            horizontal_label << "LANDSCAPE WIDTH  " << std::fixed
                             << std::setprecision (2) << m_landscape_scale_x
                             << 'x';
            std::ostringstream vertical_label;
            vertical_label << "LANDSCAPE HEIGHT  " << std::fixed
                           << std::setprecision (2) << m_landscape_scale_y
                           << 'x';
            m_game_ui.begin (m_hud_dl);
            m_game_ui.begin_window (m_hud_dl, m_game_ui_window, "WORLD FEEL");
            m_game_ui.friendly_slider (
              m_hud_dl,
              horizontal,
              horizontal_label.str (),
              "SMALLER",
              "LARGER",
              landscape_scale_normalized (m_landscape_scale_x),
              horizontal.contains (local_pointer_x, local_pointer_y),
              m_game_ui_dragging_axis == 1);
            m_game_ui.friendly_slider (
              m_hud_dl,
              vertical,
              vertical_label.str (),
              "LOWER",
              "TALLER",
              landscape_scale_normalized (m_landscape_scale_y),
              vertical.contains (local_pointer_x, local_pointer_y),
              m_game_ui_dragging_axis == 2);
            m_game_ui.end_window (m_hud_dl);
            m_game_ui.end (m_hud_dl);
          }
        }
        if (m_graphics.terrain_topology) {
          m_game_ui.begin (m_hud_dl);
          m_game_ui.key_hint (m_hud_dl,
                              24.0f,
                              hud_height - 28.0f,
                              "G",
                              "VERTEX GRID  CYAN MESH  AMBER SURFACE SAMPLES");
          m_game_ui.end (m_hud_dl);
        }
        // Even a clean inspection capture needs this empty HUD pass: it is
        // also the final post-chain composite into the drawable.
        r.draw_hud (m_hud_dl);

        r.end_frame ();
        if (captured) {
          m_screenshot_path.clear ();
          platform::request_quit ();
        }
        if (captured_cinematic) {
          if (m_cinematic_capture_frame >= cinematic_capture_frame_limit ())
            platform::request_quit ();
        }
      }

      void render_loading (render::Renderer& r) {
        if (m_generation_complete && !m_setup_complete) {
          if (!m_loading_finalize_announced) {
            set_loading_stage (LoadingStage::AssemblingWorld);
            m_loading_finalize_announced = true;
          } else {
            finish_setup ();
          }
        }

        if (m_setup_complete) {
          if (m_loading_activation_stage == 0) {
            set_loading_stage (LoadingStage::UploadingTerrain);
            m_loading_activation_stage = 1;
          } else if (m_loading_activation_stage == 1) {
            r.clear_terrain_overlay ();
            upload_world_terrain (r);
            if (!m_terrain_lab_preview && m_graphics.terrain_shadows)
              set_loading_stage (LoadingStage::CastingShadows);
            else
              set_loading_stage (LoadingStage::Ready);
            m_loading_activation_stage = 2;
          } else if (m_loading_activation_stage == 2) {
            if (!m_terrain_lab_preview && m_graphics.terrain_shadows)
              cast_world_shadows (r);
            set_loading_stage (LoadingStage::Ready);
            m_loading_activation_stage = 3;
          } else {
            m_ready = true;
            MOPPE_PROFILE_PLOT ("startup.ready", 1);

            const bool automated = !m_screenshot_path.empty () ||
                                   m_benchmark.has_value () ||
                                   m_water_shot.has_value () || m_tree_demo ||
                                   ::getenv ("MOPPE_DEMO");
            if (m_start_in_terrain_lab) {
              m_terrain_lab.enter (
                r,
                generated_world ().terrain_for_terrain_lab (),
                m_terrain,
                world (),
                m_graphics,
                recipe (),
                m_surface_presentation.trails (),
                m_surface_presentation.home_base (),
                terrain_history (),
                sun_direction_for (m_graphics.sun_height));
              m_start_in_terrain_lab = false;
            } else if (!automated && !m_skip_cinematic_requested &&
                       !m_cinematic_plan.empty ()) {
              m_cinematic.start (m_cinematic_plan, map ());
              m_live_input.clear ();
            }
            return;
          }
        }

        const float width = static_cast<float> (r.width_pts ());
        const float height = static_cast<float> (r.height_pts ());
        const double now = platform::now ();
        const float sky_time = static_cast<float> (now - m_loading_clock_start);

        const LoadingStage stage = m_loading_stage.load ();
        const LoadingStageText copy = loading_stage_text (stage);
        float progress = copy.progress;
        float local_progress = -1.0f;
        if (stage == LoadingStage::BuildingContinents) {
          const float source =
            static_cast<float> (m_loading_source_done.load ()) /
            std::max (1.0f,
                      static_cast<float> (m_loading_source_total.load ()));
          local_progress = source;
          progress = 0.06f + 0.13f * source;
        } else if (stage == LoadingStage::EvolvingTerrain) {
          const float erosion =
            static_cast<float> (m_loading_work_done.load ()) /
            std::max (1.0f, static_cast<float> (m_loading_work_total.load ()));
          local_progress = erosion;
          progress = 0.20f + 0.48f * erosion;
        }
        m_loading_progress_display =
          std::max (m_loading_progress_display, progress);

        std::shared_ptr<const std::vector<float>> loading_heights;
        std::vector<LoadingEvent> loading_events;
        {
          const std::lock_guard<std::mutex> lock (m_loading_mutex);
          loading_heights = m_loading_heights;
          loading_events = m_loading_events;
        }
        if (loading_heights && loading_heights != m_displayed_loading_heights) {
          std::copy (loading_heights->begin (),
                     loading_heights->end (),
                     m_loading_map.raw_heights ());
          r.clear_terrain_overlay ();
          m_terrain.setup (r,
                           m_loading_map,
                           m_loading_world,
                           m_graphics,
                           render::TerrainProjection::Plane,
                           true,
                           true,
                           true);
          m_loading_terrain_visible = true;
          m_displayed_loading_heights = std::move (loading_heights);
        }

        const bool show_terrain = m_loading_terrain_visible;
        const Vec3& world_extent = extent_value (m_loading_world.map_size);
        Vec3 eye (0.0f, 34.0f, 0.0f);
        Vec3 target (0.0f, 27.0f, -100.0f);
        if (show_terrain) {
          const float orbit = -0.65f + sky_time * 0.025f;
          const float center_x = world_extent[0] * 0.5f;
          const float center_z = world_extent[2] * 0.5f;
          target =
            Vec3 (center_x,
                  m_loading_map.interpolated_height (center_x, center_z) +
                    world_extent[1] * 0.07f,
                  center_z);
          eye = target + Vec3 (std::sin (orbit) * world_extent[0] * 0.48f,
                               world_extent[1] * 0.34f,
                               std::cos (orbit) * world_extent[2] * 0.48f);
        }
        const Vec3 forward = normalized (target - eye);

        render::FrameParams fp;
        fp.view = Mat4::look_at (eye, target, Vec3 (0, 1, 0));
        fp.proj = Mat4::perspective_reversed (
          (show_terrain ? 50.0f : 64.0f) * u::deg,
          width / std::max (1.0f, height),
          0.5f,
          std::max (9000.0f, world_extent[0] * 2.0f));
        fp.camera_pos = eye;
        constexpr float loading_sun_height = 0.70f;
        const Vec3 horizon_forward =
          normalized (Vec3 (forward[0], 0.0f, forward[2]));
        const Vec3 horizon_side (-horizon_forward[2], 0.0f, horizon_forward[0]);
        fp.clear_color = horizon_color_for (loading_sun_height);
        fp.sun_dir = normalized (horizon_side * 0.82f + Vec3 (0, 1, 0) * 0.58f);
        sun_light_colors (loading_sun_height, fp.sun_diffuse, fp.sun_specular);
        fp.ambient = DisplayColor (0.58f, 0.55f, 0.48f);
        fp.fog_scale =
          show_terrain ? attenuation_value (m_loading_world.fog_scale * 0.035f)
                       : 0.0f;
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
        if (show_terrain)
          m_terrain.render (r, eye, forward, world_extent[0] * 1.8f);

        m_hud_dl.clear ();
        render::DrawState state;
        state.blend = true;
        state.depth_test = false;
        state.depth_write = false;
        state.cull = false;
        m_hud_dl.state (state);
        m_hud_dl.lit (false);
        m_hud_dl.fogged (false);

        if (m_loading_font && m_loading_font->ok ()) {
          const float text_x = 42.0f;
          m_hud_dl.push ();
          m_hud_dl.translate (text_x, 38.0f, 0.0f);
          m_hud_dl.scale (0.55f, 0.55f, 1.0f);
          m_hud_dl.color (0.80f, 0.90f, 0.82f, 0.78f);
          m_loading_font->draw (m_hud_dl, 0.0f, 0.0f, "BUILDING THE WORLD");
          m_hud_dl.pop ();

          m_hud_dl.push ();
          m_hud_dl.translate (text_x, 71.0f, 0.0f);
          m_hud_dl.scale (1.18f, 1.18f, 1.0f);
          m_hud_dl.color (0.93f, 1.0f, 0.91f, 0.96f);
          m_loading_font->draw (m_hud_dl, 0.0f, 0.0f, copy.title);
          m_hud_dl.pop ();

          std::string detail = copy.detail;
          if (local_progress >= 0.0f) {
            const int done = stage == LoadingStage::BuildingContinents
                               ? m_loading_source_done.load ()
                               : m_loading_work_done.load ();
            const int total = stage == LoadingStage::BuildingContinents
                                ? m_loading_source_total.load ()
                                : m_loading_work_total.load ();
            std::ostringstream status;
            status << detail << "  " << done << " / " << total;
            detail = status.str ();
          }
          m_hud_dl.push ();
          m_hud_dl.translate (text_x, 106.0f, 0.0f);
          m_hud_dl.scale (0.72f, 0.72f, 1.0f);
          m_hud_dl.color (0.80f, 0.90f, 0.82f, 0.90f);
          m_loading_font->draw (m_hud_dl, 0.0f, 0.0f, detail);
          m_hud_dl.pop ();

          const int available_lines =
            std::max (1, static_cast<int> ((height - 160.0f) / 22.0f));
          const std::size_t first =
            loading_events.size () > static_cast<std::size_t> (available_lines)
              ? loading_events.size () - available_lines
              : 0;
          float line_y = 157.0f;
          for (std::size_t i = first; i < loading_events.size (); ++i) {
            const LoadingEvent& event = loading_events[i];
            const LoadingStageText event_text =
              loading_stage_text (event.stage);
            std::ostringstream line;
            line << std::fixed << std::setprecision (1) << event.elapsed
                 << "s  " << event_text.title << "  -  " << event_text.detail;
            m_hud_dl.push ();
            m_hud_dl.translate (text_x, line_y, 0.0f);
            m_hud_dl.scale (0.53f, 0.53f, 1.0f);
            const bool current = i + 1 == loading_events.size ();
            m_hud_dl.color (0.83f, 0.93f, 0.84f, current ? 0.96f : 0.68f);
            m_loading_font->draw (m_hud_dl, 0.0f, 0.0f, line.str ());
            m_hud_dl.pop ();
            line_y += 22.0f;
          }
        }

        bool captured = false;
        if (!m_loading_capture_done && m_loading_progress_display >= 0.20f) {
          if (const char* path = ::getenv ("MOPPE_LOADING_SCREENSHOT")) {
            r.request_screenshot (path);
            m_loading_capture_done = true;
            captured = true;
          }
        }
        r.draw_hud (m_hud_dl);
        r.end_frame ();
        if (captured)
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
        if (logic ().m_game_over || m_terrain_lab.active ())
          return;
        m_live_input.controls (state);
      }

      void pointer_move (float x, float y, float dx, float dy) override {
        m_pointer_x = x;
        m_pointer_y = y;
        if (m_game_ui_window.dragging ()) {
          m_game_ui_window.drag_to (
            x, y, m_renderer->width_pts (), m_renderer->height_pts ());
        } else if (m_game_ui_dragging_axis) {
          set_landscape_scale_from_pointer (x, m_game_ui_dragging_axis);
        } else if (m_terrain_lab.active ()) {
          m_terrain_lab.pointer_move (x, y, dx, dy);
        }
      }

      void pointer_button (platform::PointerButton button,
                           bool down,
                           float x,
                           float y) override {
        if (m_game_ui_open && button == platform::PointerButton::Primary) {
          const UiRect horizontal { 20, 58, 320, 64 };
          const UiRect vertical { 20, 132, 320, 64 };
          const float local_x = m_game_ui_window.local_x (x);
          const float local_y = m_game_ui_window.local_y (y);
          if (down && m_game_ui_window.begin_drag (x, y)) {
            return;
          } else if (down && horizontal.contains (local_x, local_y)) {
            m_game_ui_dragging_axis = 1;
            set_landscape_scale_from_pointer (x, 1);
          } else if (down && vertical.contains (local_x, local_y)) {
            m_game_ui_dragging_axis = 2;
            set_landscape_scale_from_pointer (x, 2);
          } else if (!down) {
            m_game_ui_window.end_drag ();
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

        if (!m_ready) {
          if (k == Key::Space && down)
            m_skip_cinematic_requested = true;
          else if (k == Key::Escape && down)
            platform::request_quit ();
          return;
        }

        // In great pain, only R (ride again) and ESC work.
        if (logic ().m_game_over) {
          if (k == Key::R && down)
            revive ();
          else if (k == Key::Escape && down)
            platform::request_quit ();
          return;
        }

        if (k == Key::G && down) {
          m_graphics.terrain_topology = !m_graphics.terrain_topology;
          m_renderer->set_terrain_topology_overlay (
            m_graphics.terrain_topology);
          std::cerr << "moppe: terrain vertex grid "
                    << (m_graphics.terrain_topology ? "on" : "off") << '\n';
          return;
        }

        if (m_cinematic.active ()) {
          if (k == Key::Escape && down)
            platform::request_quit ();
          else
            m_live_input.cinematic_key (k, down);
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
          m_live_input.clear ();
          m_terrain_lab.enter (*m_renderer,
                               generated_world ().terrain_for_terrain_lab (),
                               m_terrain,
                               world (),
                               m_graphics,
                               recipe (),
                               m_surface_presentation.trails (),
                               m_surface_presentation.home_base (),
                               terrain_history (),
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

        m_live_input.key (k, down);
        if (k == Key::Escape && down)
          platform::request_quit ();
      }

    private:
      float landscape_scale_normalized (float scale) const {
        return std::log (scale / 0.05f) / std::log (400.0f);
      }

      void set_landscape_scale_from_pointer (float x, int axis) {
        const float normalized = std::clamp (
          (m_game_ui_window.local_x (x) - 20.0f) / 320.0f, 0.0f, 1.0f);
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
        return logic ().m_mode == M_CAR ? session ().car ()
                                        : session ().bike ();
      }

      Vec3 subject_position () const {
        if (logic ().m_mode == M_FOOT)
          return session ().walker ().position ();
        if (logic ().m_mode == M_GLIDER)
          return session ().glider ().position ();
        return logic ().m_mode == M_CAR ? session ().car ().position ()
                                        : session ().bike ().position ();
      }

      Vec3 subject_heading () const {
        if (logic ().m_mode == M_FOOT)
          return session ().walker ().heading ();
        if (logic ().m_mode == M_GLIDER)
          return session ().glider ().heading ();
        return logic ().m_mode == M_CAR ? session ().car ().orientation ()
                                        : session ().bike ().orientation ();
      }

      float subject_speed_kmh () const {
        if (logic ().m_mode == M_FOOT)
          return 0.0f;
        if (logic ().m_mode == M_GLIDER)
          return session ().glider ().airspeed ().numerical_value_in (u::m /
                                                                      u::s) *
                 3.6f;
        const Vec3 v = logic ().m_mode == M_CAR
                         ? session ().car ().velocity ()
                         : session ().bike ().velocity ();
        return length (v) * 3.6f;
      }

      bool can_deploy_glider () const {
        if (logic ().m_mode != M_BIKE || !session ().bike ().airborne ())
          return false;
        const Vec3 p = session ().bike ().position ();
        const float ground = map ().interpolated_height (p[0], p[2]);
        return p[1] - ground > 3.0f;
      }

      void deploy_glider () {
        if (!can_deploy_glider ())
          return;
        const Vec3 p = session ().bike ().position ();
        const Vec3 heading = session ().bike ().orientation ();
        const velocity_t inherited = session ().bike ().physical_velocity ();
        session ().bike ().set_thrust (0);
        session ().bike ().set_yaw (0 * u::deg);
        session ().bike ().set_boost (0, 0);
        session ().glider ().launch (
          position (p + Vec3 (0, 1.0f, 0)), inherited, heading);
        logic ().m_mode = M_GLIDER;
        session ().glider ().set_turn (logic ().m_turn_input);
        session ().glider ().set_speed_control (logic ().m_go_input);
        session ().glider ().set_flare (logic ().m_boost_input > 0.1f);
      }

      void finish_glide () {
        const Vec3 p = session ().glider ().position ();
        session ().walker ().spawn (position (p + Vec3 (0, 0.15f, 0)),
                                    session ().glider ().heading ());
        logic ().m_mode = M_FOOT;
        input_turn (logic ().m_turn_input);
        input_go (logic ().m_go_input);
        input_boost (0);
      }

      void leave_cinematic () {
        m_cinematic.stop ();
        m_live_input.clear ();
        const Vec3 subject =
          subject_position () +
          (logic ().m_mode == M_FOOT ? Vec3 (0, 1.0f, 0) : Vec3 ());
        Vec3 heading = subject_heading ();
        heading[1] = 0.0f;
        if (length2 (heading) < 1e-5f)
          heading = Vec3 (0, 0, 1);
        else
          normalize (heading);
        const Vec3 eye = subject - heading * 6.2f + Vec3 (0, 2.5f, 0);
        session ().camera ().place (eye, subject + heading * 2.0f);
        session ().camera ().limit (map ());
      }

      void regenerate_world () {
        input_turn (0.0f);
        input_go (0.0f);
        input_boost (0.0f);
        m_live_input.clear ();
        m_ready = false;
        m_loading_work_done = 0;
        m_loading_work_total = 1;
        m_loading_source_done = 0;
        m_loading_source_total = 1;
        m_loading_progress_display = 0.0f;
        m_loading_capture_done = false;
        m_loading_clock_start = platform::now ();
        m_loading_terrain_visible = false;
        m_displayed_loading_heights.reset ();
        {
          const std::lock_guard<std::mutex> lock (m_loading_mutex);
          m_loading_events.clear ();
          m_loading_heights.reset ();
        }
        set_loading_stage (LoadingStage::Starting);
        m_generation_complete = false;
        m_loading_finalize_announced = false;
        m_loading_activation_stage = 0;
        m_skip_cinematic_requested = false;
        m_cinematic.stop ();
        m_cinematic_plan = {};
        m_setup_complete = false;
        m_river_surface.clear ();
        m_water_inspection.reset ();
        const terrain::Seed next_seed = terrain::next_seed (recipe ().seed ());
        terrain::WorldRecipe next_recipe =
          m_terrain_lab_preview
            ? terrain::make_geological_world_recipe (
                recipe ().extent (),
                recipe ().resolution (),
                recipe ().topology (),
                next_seed,
                recipe ().water_datum (),
                recipe ().generation_profile ())
            : terrain::make_world_recipe (recipe ().extent (),
                                          recipe ().resolution (),
                                          recipe ().topology (),
                                          next_seed,
                                          recipe ().water_datum (),
                                          recipe ().generation_profile ());
        logic ().m_mode = M_BIKE;
        logic ().m_car_exists = false;
        logic ().m_game_over = false;
        logic ().m_health = 100.0f;
        logic ().m_fuel = 100.0f;
        start_world_generation (std::move (next_recipe));
      }

      void input_turn (float v) {
        logic ().m_turn_input = v;
        if (logic ().m_mode == M_FOOT)
          session ().walker ().set_turn (v);
        else if (logic ().m_mode == M_GLIDER)
          session ().glider ().set_turn (v);
        else
          active_vehicle ().set_yaw ((90 * v) * u::deg);
      }

      void input_go (float v) {
        logic ().m_go_input = v;
        if (logic ().m_mode == M_FOOT)
          session ().walker ().set_walk (v > 0 ? v : v * 0.6f);
        else if (logic ().m_mode == M_GLIDER)
          session ().glider ().set_speed_control (v);
        else {
          active_vehicle ().set_thrust (v);
          active_vehicle ().set_boost (logic ().m_boost_input,
                                       logic ().m_go_input);
        }
      }

      void input_boost (float v) {
        const float previous = logic ().m_boost_input;
        logic ().m_boost_input = std::max (0.0f, std::min (1.0f, v));
        if (logic ().m_mode == M_FOOT) {
          if (logic ().m_boost_input > 0.1f && previous <= 0.1f)
            session ().walker ().jump ();
        } else if (logic ().m_mode == M_GLIDER)
          session ().glider ().set_flare (logic ().m_boost_input > 0.1f);
        else
          active_vehicle ().set_boost (logic ().m_boost_input,
                                       logic ().m_go_input);
      }

      // This is the single ordinary-gameplay read of live or recorded input.
      // Loading, Terrain Lab, and application commands stay above this seam.
      void apply_input_frame (const InputFrame& input) {
        input_turn (input_value (input.turn));
        input_go (input_value (input.drive));
        input_boost (input_value (input.boost));

        if (input.deploy_glider)
          deploy_glider ();
        if (input.toggle_mount) {
          if (can_deploy_glider ())
            deploy_glider ();
          else
            toggle_mount ();
        }
        if (input.cycle_camera) {
          logic ().m_cam_mode = (CamMode)((logic ().m_cam_mode + 1) % 3);
          if (logic ().m_cam_mode == CAM_HELMET)
            logic ().m_fp_eye = session ().camera ().position ();
        }
      }

      void toggle_mount () {
        if (!m_ready)
          return;

        if (logic ().m_mode == M_GLIDER)
          return;

        if (logic ().m_mode != M_FOOT) {
          // Step off to the side of whatever we're driving.
          mov::Vehicle& av = active_vehicle ();
          const Vec3 h = av.orientation ();
          const Vec3 side (h[2], 0, -h[0]);
          session ().walker ().spawn (
            position (av.position () +
                      side * (logic ().m_mode == M_CAR ? 2.4f : 1.8f)),
            h);
          av.set_thrust (0);
          av.set_yaw (0 * u::deg);
          av.set_boost (0, 0);
          logic ().m_mode = M_FOOT;
          input_turn (logic ().m_turn_input);
          input_go (logic ().m_go_input);
          return;
        }

        // On foot: bike first, then our parked car, then grand theft.
        if (length2 (session ().walker ().position () -
                     session ().bike ().position ()) < 5.0f * 5.0f) {
          session ().bike ().set_thrust (0);
          session ().bike ().set_yaw (0 * u::deg);
          logic ().m_mode = M_BIKE;
          input_turn (logic ().m_turn_input);
          input_go (logic ().m_go_input);
          input_boost (logic ().m_boost_input);
          return;
        }

        if (logic ().m_car_exists &&
            length2 (session ().walker ().position () -
                     session ().car ().position ()) < 6.0f * 6.0f) {
          session ().car ().set_thrust (0);
          session ().car ().set_yaw (0 * u::deg);
          logic ().m_mode = M_CAR;
          input_turn (logic ().m_turn_input);
          input_go (logic ().m_go_input);
          input_boost (logic ().m_boost_input);
          return;
        }
      }

      void revive () {
        logic ().m_lives = 10;
        logic ().m_health = 100.0f;
        logic ().m_fuel = 100.0f;
        logic ().m_shake = 0.0f;
        logic ().m_shake_time = 0.0f;
        logic ().m_jump_airtime = 0.0f;
        logic ().m_landed_age = 10.0f;
        logic ().m_mode = M_BIKE;
        // Back to the start, but ON the ground rather than 600 m
        // over it.
        const float ground =
          surface ()
            .elevation_at (
              position (Vec3 (m_spawn_position[0], 0, m_spawn_position[2])))
            .quantity_from_zero ()
            .numerical_value_in (u::m);
        session ().bike ().reset (
          Vec3 (m_spawn_position[0], ground + 1.2f, m_spawn_position[2]));
        // Key releases were swallowed during the game-over screen;
        // don't resume with the throttle stuck open.
        logic ().m_turn_input = 0;
        logic ().m_go_input = 0;
        logic ().m_boost_input = 0;
        m_live_input.clear ();
        session ().bike ().set_thrust (0);
        session ().bike ().set_yaw (0 * u::deg);
        session ().bike ().set_boost (0, 0);
        logic ().m_game_over = false;
      }

      // The active owner changes only in activate_completed_world().  All
      // gameplay reads go through the accessors above, so no stale reference
      // aliases survive a handoff.
      std::unique_ptr<GeneratedWorld> m_generated_world;
      // Declared after its world so session-held terrain and surface borrows
      // release first during normal teardown.
      std::unique_ptr<GameSession> m_session;
      std::shared_ptr<GenerationJob> m_generation_job;
      std::mutex m_completed_world_mutex;
      std::unique_ptr<GeneratedWorld> m_completed_world;
      GraphicsSettings m_graphics;
      Vec3 m_spawn_position;
      Vec3 m_home_base_position;
      WorldParams m_loading_world;
      map::RandomHeightMap m_loading_map;
      SurfacePresentation m_surface_presentation;
      std::mutex m_loading_mutex;
      std::vector<LoadingEvent> m_loading_events;
      std::shared_ptr<const std::vector<float>> m_loading_heights;
      std::shared_ptr<const std::vector<float>> m_displayed_loading_heights;
      std::atomic<int> m_loading_work_done = 0;
      std::atomic<int> m_loading_work_total = 1;
      std::atomic<int> m_loading_source_done = 0;
      std::atomic<int> m_loading_source_total = 1;
      float m_loading_progress_display = 0.0f;
      double m_loading_clock_start = platform::now ();
      bool m_loading_capture_done = false;
      bool m_loading_terrain_visible = false;
      bool m_loading_finalize_announced = false;
      int m_loading_activation_stage = 0;
      bool m_skip_cinematic_requested = false;
      CinematicFlightPlan m_cinematic_plan;
      CinematicFlight m_cinematic;
      InputFrameAdapter m_live_input;
      RiverSurface m_river_surface;
      Terrain m_terrain;
      WaterPresentation m_water_presentation;
      TerrainLab m_terrain_lab;
      ForestLandscape m_forest;
      TreeStand m_tree_stand;
      BlobShadow m_blob;
      std::vector<mov::Box> m_obstacles;
      Hud m_hud;
      InspectorUi m_game_ui;
      UiWindow m_game_ui_window { { 24, 24, 360, 224 } };
      bool m_game_ui_window_positioned = false;
      bool m_game_ui_open = false;
      int m_game_ui_dragging_axis = 0;
      float m_landscape_scale_x = 1.0f;
      float m_landscape_scale_y = 1.0f;
      float m_pointer_x = -1.0f;
      float m_pointer_y = -1.0f;
      std::unique_ptr<render::FontAtlas> m_loading_font;

      render::Renderer* m_renderer;
      bool m_start_in_terrain_lab;
      bool m_terrain_lab_preview;
      bool m_tree_demo;
      std::size_t m_tree_count;
      bool m_automated_regeneration_done = false;
      std::string m_screenshot_path;
      std::optional<WaterShot> m_water_shot;
      std::optional<WaterInspection> m_water_inspection;
      int m_screenshot_frames;
      int m_cinematic_capture_frame = 0;
      std::atomic<bool> m_ready;
      std::atomic<bool> m_setup_complete = false;
      std::atomic<bool> m_generation_complete = false;
      std::atomic<LoadingStage> m_loading_stage;
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
  if (::getenv ("MOPPE_TRACY_WAIT"))
    MOPPE_PROFILE_WAIT ();
  MOPPE_PROFILE_THREAD ("Main");
  MOPPE_PROFILE_ZONE ("main");

  game::WorldParams world;
  game::GraphicsSettings graphics = game::high_graphics_settings ();
  platform::Config config;
  bool start_in_terrain_lab = false;
  bool terrain_lab_preview = false;
  bool tree_demo = false;
  std::size_t tree_count = 9;
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
    } else if (arg == "--tree-demo") {
      tree_demo = true;
    } else if (arg == "--tree-count") {
      if (i + 1 >= argc) {
        std::cerr << "--tree-count requires an integer from 1 to 64\n";
        return -1;
      }
      const int count = std::atoi (argv[++i]);
      if (count < 1 || count > 64) {
        std::cerr << "--tree-count must be between 1 and 64\n";
        return -1;
      }
      tree_count = static_cast<std::size_t> (count);
    } else if (arg == "--tree-screenshot") {
      if (i + 1 >= argc) {
        std::cerr << "--tree-screenshot requires a PNG path\n";
        return -1;
      }
      tree_demo = true;
      screenshot_path = argv[++i];
      config.fullscreen = false;
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
                  << " (use stream, river, confluence, mouth, waterfall, "
                     "or lake)\n";
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
  config.capture_frames =
    !screenshot_path.empty () || ::getenv ("MOPPE_CINEMATIC_CAPTURE_DIR");
  config.activate = !config.capture_frames && !graphics_benchmark;
  if (!screenshot_path.empty () && seed < 0)
    seed = 123;
  game::prune_obsolete_terrain_caches ();
  if (seed < 0)
    seed = game::remembered_seed (world, generation_profile);

  const terrain::Seed world_seed { static_cast<std::uint32_t> (seed) };
  terrain::WorldRecipe recipe =
    terrain_lab_preview
      ? terrain::make_geological_world_recipe (world.map_size,
                                               world.resolution,
                                               world.topology (),
                                               world_seed,
                                               world.water_level,
                                               generation_profile)
      : terrain::make_world_recipe (world.map_size,
                                    world.resolution,
                                    world.topology (),
                                    world_seed,
                                    world.water_level,
                                    generation_profile);

  game::MoppeGame game (world,
                        std::move (recipe),
                        graphics,
                        start_in_terrain_lab,
                        terrain_lab_preview,
                        tree_demo,
                        tree_count,
                        std::move (screenshot_path),
                        water_shot,
                        graphics_benchmark);

  try {
    return platform::run (game, config);
  } catch (const std::exception& e) {
    std::cerr << "\nError: " << e.what () << "\n";
    return -1;
  }
}
