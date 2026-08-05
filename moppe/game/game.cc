// The game: the port of the original MoppeGLUT application class onto
// the platform/render abstractions.  World generation runs on a
// background thread behind a loading screen; the frame follows the
// exact pass order of the GL build's render_scene().  The command line
// that configures a launch is resolved before this file is reached; see
// launch_options.hh and main.cc.

#include <moppe/platform/platform.hh>
#include <moppe/profile.hh>
#include <moppe/render/renderer.hh>
#include <moppe/render/text.hh>

#include <moppe/game/blob_shadow.hh>
#include <moppe/game/chase_camera.hh>
#include <moppe/game/cinematic_flight.hh>
#include <moppe/game/dust.hh>
#include <moppe/game/forest.hh>
#include <moppe/game/frame_view.hh>
#include <moppe/game/game_session.hh>
#include <moppe/game/generated_world.hh>
#include <moppe/game/glider_render.hh>
#include <moppe/game/graphics_benchmark.hh>
#include <moppe/game/graphics_settings.hh>
#include <moppe/game/hud.hh>
#include <moppe/game/input_frame_adapter.hh>
#include <moppe/game/landscape_gazetteer.hh>
#include <moppe/game/landscape_summary.hh>
#include <moppe/game/launch_options.hh>
#include <moppe/game/moppe_game.hh>
#include <moppe/game/seed_memory.hh>
#include <moppe/game/simulation_clock.hh>
#include <moppe/game/stars.hh>
#include <moppe/game/surface_presentation.hh>
#include <moppe/game/terrain.hh>
#include <moppe/game/vehicle_render.hh>
#include <moppe/game/walker_render.hh>
#include <moppe/game/water_capture.hh>
#include <moppe/game/water_presentation.hh>
#include <moppe/game/waterfall_surface.hh>
#include <moppe/game/world.hh>
#include <moppe/game/world_loading.hh>
#include <moppe/map/surface.hh>
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
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

namespace moppe {
  namespace game {
    static int cinematic_capture_frame_limit () {
      if (const char* value = ::getenv ("MOPPE_CINEMATIC_CAPTURE_FRAMES"))
        return std::max (1, ::atoi (value));
      return 450;
    }

    static int cinematic_capture_frame_step () {
      if (const char* value = ::getenv ("MOPPE_CINEMATIC_CAPTURE_STEP"))
        return std::max (1, ::atoi (value));
      return 1;
    }

    class MoppeGame : public platform::Game {
    public:
      MoppeGame (const LaunchOptions& options, terrain::WorldRecipe recipe)
          : m_params (bind_world_params (options.world, recipe)),
            m_recipe (std::move (recipe)),
            m_loading (this->recipe (), options.world_cache),
            m_graphics (options.graphics), m_spawn_position (position_value (
                                             this->world ().spawn_position ())),
            m_renderer (0), m_screenshot_path (options.screenshot_path),
            m_water_shot (options.water_shot), m_gazetteer (options.gazetteer),
            m_screenshot_frames (0), m_ready (false),
            m_benchmark (options.benchmark),
            m_benchmark_baseline (options.graphics) {
        if (m_benchmark)
          m_benchmark_replay.emplace (GraphicsBenchmarkReplay::Config {
            m_benchmark->prelude_frames,
            m_benchmark->settle_frames,
            m_benchmark->measured_frames,
          });
      }

      // -- lifecycle ---------------------------------------------------

      void setup (render::Renderer& r, int, int) override {
        MOPPE_PROFILE_ZONE ("MoppeGame::setup");
        m_renderer = &r;
        std::cerr << "moppe: simulation: fixed-step=120 Hz, catch-up-limit="
                  << MAX_SIMULATION_CATCH_UP_STEPS << " steps" << std::endl;

        // Fast, main-thread resource setup; the heavy world build
        // runs behind the loading screen.
        {
          MOPPE_PROFILE_ZONE ("startup.load_hud");
          m_hud.load (r);
        }
        {
          MOPPE_PROFILE_ZONE ("startup.load_loading_font");
          m_loading_font.reset (new render::FontAtlas (
            r, "AvenirNext-Medium", 16, r.scale_factor ()));
          m_loading_title_font.reset (new render::FontAtlas (
            r, "AvenirNext-DemiBold", 30, r.scale_factor ()));
          m_loading_meta_font.reset (
            new render::FontAtlas (r, "Menlo", 11, r.scale_factor ()));
        }
        {
          MOPPE_PROFILE_ZONE ("startup.load_blob_shadow");
          m_blob.load (r);
        }

        {
          MOPPE_PROFILE_ZONE ("startup.dispatch_world_generation");
          m_loading.start (world (), recipe ());
        }
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

      // The recipe and its bound parameters are known before the world is
      // generated; the loading screen runs on them alone.
      const terrain::WorldRecipe& recipe () const noexcept {
        return m_recipe;
      }

      const WorldParams& world () const noexcept {
        return m_params;
      }

      const map::SurfaceGeometry& surface () const noexcept {
        return generated_world ().surface ();
      }

      const map::SurfaceReadings& surface_readings () const noexcept {
        return generated_world ().readings ();
      }

      template <typename Artifact>
      const Artifact& hydrology_artifact () const noexcept {
        return std::get<Artifact> (generated_world ().hydrology ());
      }

      const terrain::FloodField& standing_water () const noexcept {
        return hydrology_artifact<terrain::FloodField> ();
      }

      const terrain::LakeCensus& lake_census () const noexcept {
        return hydrology_artifact<terrain::LakeCensus> ();
      }

      const terrain::DrainageGraph& drainage () const noexcept {
        return hydrology_artifact<terrain::DrainageGraph> ();
      }

      const terrain::RiverNetwork& rivers () const noexcept {
        return hydrology_artifact<terrain::RiverNetwork> ();
      }

      const terrain::TrailNetwork& trail_network () const noexcept {
        return generated_world ().trails ();
      }

      Vec3 trail_cell_position (terrain::CellIndex cell) const {
        if (cell == terrain::no_cell)
          return {};
        const terrain::TerrainDomain& grid = trail_network ().domain;
        const std::size_t width = grid.width ();
        const float x = (cell.value % width) *
                        (grid.spacing_x ()).numerical_value_in (moppe::u::m);
        const float z = (cell.value / width) *
                        (grid.spacing_z ()).numerical_value_in (moppe::u::m);
        return Vec3 (x,
                     terrain::surface_elevation_value (
                       spatial::sample<terrain::surface_elevation> (
                         surface (), moppe::position (Vec3 (x, 0.0f, z)))),
                     z);
      }

      Vec3 trail_alignment_position (
        const terrain::TrailAlignmentPoint& point) const {
        return Vec3 (
          point.x_m,
          terrain::surface_elevation_value (
            spatial::sample<terrain::surface_elevation> (
              surface (), moppe::position (Vec3 (point.x_m, 0.0f, point.z_m)))),
          point.z_m);
      }

      Vec3 trail_direction_from_home () const {
        if (trail_network ().alignment.points.size () < 2)
          return Vec3 (0, 0, 1);
        Vec3 direction =
          trail_alignment_position (trail_network ().alignment.points[1]) -
          trail_alignment_position (trail_network ().alignment.points[0]);
        direction[1] = 0.0f;
        return length2 (direction) > 1e-5f ? normalized (direction)
                                           : Vec3 (0, 0, 1);
      }

      void draw_home_base_marker (render::DrawList& dl) const {
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
                           int height_pts,
                           const Vec3& subject,
                           Vec3 heading) const {
        if (width_pts < 480 || height_pts < 360)
          return;
        const terrain::TerrainDomain& grid = trail_network ().domain;
        const auto& alignment = trail_network ().alignment.points;
        if (alignment.size () < 2)
          return;
        const float period_x =
          grid.width () * (grid.spacing_x ()).numerical_value_in (moppe::u::m);
        const float period_z =
          grid.height () * (grid.spacing_z ()).numerical_value_in (moppe::u::m);
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
          low[0] = std::min (static_cast<float> (low[0]), point[0]);
          low[2] = std::min (static_cast<float> (low[2]), point[2]);
          high[0] = std::max (static_cast<float> (high[0]), point[0]);
          high[2] = std::max (static_cast<float> (high[2]), point[2]);
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
        // Keep the map field opaque. With frame interpolation, a translucent
        // HUD field is first composited over the current rendered scene and
        // then decomposited by MetalFX for the generated midpoint. Fast
        // ground motion makes that reconstruction visibly pulse in flight.
        dl.color (0.01f, 0.025f, 0.035f, 1.0f);
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

        const Vec3 relative_subject (
          wrap_delta (subject[0] - home_x, period_x),
          0,
          wrap_delta (subject[2] - home_z, period_z));
        Vec3 player = map_point (relative_subject);
        player[0] = std::clamp (static_cast<float> (player[0]),
                                map_x + 5.0f,
                                map_x + map_size - 5.0f);
        player[1] = std::clamp (static_cast<float> (player[1]),
                                map_y + 5.0f,
                                map_y + map_size - 5.0f);
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

      void
      activate_completed_world (std::unique_ptr<GeneratedWorld> completed) {
        MOPPE_PROFILE_ZONE ("MoppeGame::activate_completed_world");
        if (!completed)
          throw std::logic_error ("no completed world to activate");

        // Keep the outgoing session and world alive until the new session has
        // bound to the completed world. The session owns every direct
        // terrain/surface borrower, so it must retire before its old world.
        std::unique_ptr<GeneratedWorld> retired_world =
          std::move (m_generated_world);
        std::unique_ptr<GameSession> retired_session = std::move (m_session);
        m_generated_world = std::move (completed);
        m_params = m_generated_world->params ();
        m_recipe = m_generated_world->recipe ();
        m_session = std::make_unique<GameSession> (world (), surface ());
        retired_session.reset ();
        retired_world.reset ();
      }

      void prepare_world_water () {
        MOPPE_PROFILE_ZONE ("startup.prepare_world_water");
        render::Renderer& r = *m_renderer;
        // Horizontal water was prepared with the world on the loading worker.
        // The render handoff builds only the few vertical nickpoint curtains,
        // never a dense mesh along every river reach.
        m_waterfall_surface.rebuild (r, surface (), rivers ());
      }

      void prepare_world_surface () {
        MOPPE_PROFILE_ZONE ("startup.prepare_world_surface");
        session ().bike ().set_water_level (world ().water_level);
        session ().car ().set_water_level (world ().water_level);
        session ().bike ().set_obstacles (&m_obstacles);
        session ().car ().set_obstacles (&m_obstacles);

        if (m_water_shot) {
          m_water_inspection = choose_water_inspection (*m_water_shot,
                                                        surface (),
                                                        standing_water (),
                                                        lake_census (),
                                                        drainage (),
                                                        rivers ());
          if (!m_water_inspection)
            throw std::runtime_error (
              "no " + std::string (water_shot_name (*m_water_shot)) +
              " available for water screenshot");
          std::cerr << "water screenshot: " << water_shot_name (*m_water_shot)
                    << " cell=" << m_water_inspection->cell
                    << " score=" << m_water_inspection->score << '\n';
        }
      }

      void place_stars_and_player () {
        MOPPE_PROFILE_ZONE ("startup.place_stars_and_player");
        session ().stars ().generate (surface (), world (), 80);
        m_home_base_position =
          trail_cell_position (trail_network ().plan.home_base);
        m_spawn_position =
          m_home_base_position - trail_direction_from_home () * 8.0f;
        m_spawn_position[1] =
          terrain::surface_elevation_value (
            spatial::sample<terrain::surface_elevation> (
              surface (),
              moppe::position (
                Vec3 (m_spawn_position[0], 0.0f, m_spawn_position[2])))) +
          1.2f;
        session ().bike ().reset (m_spawn_position);
        session ().bike ().set_heading (trail_direction_from_home ());
      }

      void grow_global_forest () {
        MOPPE_PROFILE_ZONE ("startup.build_global_forest");
        if (m_water_inspection)
          return;
        m_forest.rebuild (*m_renderer, generated_world ().forest ());
        std::cerr << "global forest: " << m_forest.tree_count ()
                  << " canopy representatives, "
                  << m_forest.resident_bytes () / (1024 * 1024)
                  << " MB resident\n";
      }

      void plan_opening_journey () {
        MOPPE_PROFILE_ZONE ("startup.plan_cinematic_flight");
        m_cinematic_plan = plan_cinematic_flight (surface (),
                                                  standing_water (),
                                                  lake_census (),
                                                  drainage (),
                                                  rivers (),
                                                  m_spawn_position,
                                                  &trail_network ());
        if (m_cinematic_plan.empty ())
          return;
        std::cerr << "cinematic flight: " << m_cinematic_plan.waypoints.size ()
                  << " gates through ";
        for (std::size_t i = 0; i < m_cinematic_plan.landmarks.size (); ++i) {
          if (i)
            std::cerr << ", ";
          std::cerr << cinematic_landmark_name (
            m_cinematic_plan.landmarks[i].kind);
        }
        std::cerr << '\n';
      }

      void plan_gazetteer_capture () {
        if (!m_gazetteer)
          return;
        MOPPE_PROFILE_ZONE ("startup.plan_landscape_gazetteer");
        m_gazetteer_plan =
          plan_landscape_gazetteer (surface (),
                                    surface_readings (),
                                    standing_water (),
                                    lake_census (),
                                    drainage (),
                                    rivers (),
                                    trail_network (),
                                    position (m_spawn_position),
                                    sun_direction_for (m_graphics.sun_height));
        if (m_gazetteer_plan.empty ())
          throw std::runtime_error ("landscape gazetteer found no viewpoints");
        std::filesystem::create_directories (m_gazetteer->output_directory);
        const std::filesystem::path manifest =
          std::filesystem::path (m_gazetteer->output_directory) /
          "gazetteer.csv";
        std::ofstream output (manifest);
        if (!output)
          throw std::runtime_error ("cannot write gazetteer manifest: " +
                                    manifest.string ());
        write_landscape_gazetteer_csv (output, m_gazetteer_plan);
        const std::filesystem::path summary_path =
          std::filesystem::path (m_gazetteer->output_directory) /
          "terrain-summary.csv";
        std::ofstream summary_output (summary_path);
        if (!summary_output)
          throw std::runtime_error ("cannot write landscape summary: " +
                                    summary_path.string ());
        const LandscapeSummary summary =
          summarize_landscape (surface (),
                               standing_water (),
                               lake_census (),
                               drainage (),
                               rivers (),
                               generated_world ().recipe ());
        write_landscape_summary_csv (summary_output, summary);
        std::cerr << "landscape gazetteer: " << m_gazetteer_plan.shots.size ()
                  << " frozen viewpoints -> " << manifest << '\n';
      }

      // The finished world arrived from the generation thread.  Everything
      // left runs in one go: build the retained presentations, place the
      // player, upload the terrain, and start playing.  The loading frame
      // announcing this work has already been submitted, so the screen
      // stays honest while it runs.
      void finish_loading (render::Renderer& r,
                           std::unique_ptr<GeneratedWorld> completed) {
        MOPPE_PROFILE_ZONE ("MoppeGame::finish_loading");
        activate_completed_world (std::move (completed));
        prepare_world_water ();
        prepare_world_surface ();
        place_stars_and_player ();
        grow_global_forest ();
        if (m_gazetteer)
          plan_gazetteer_capture ();
        else
          plan_opening_journey ();
        remember_seed (world (),
                       recipe ().generation_profile (),
                       static_cast<int> (recipe ().seed ().value));
        if (::getenv ("MOPPE_REGENERATE_ONCE") &&
            !m_automated_regeneration_done) {
          m_automated_regeneration_done = true;
          regenerate_world ();
          return;
        }
        r.clear_terrain_overlay ();
        upload_world_terrain (r);
        if (m_graphics.terrain_shadows)
          cast_world_shadows (r);
        if (m_gazetteer)
          r.reset_temporal_state ();
        m_ready = true;
        MOPPE_PROFILE_PLOT ("startup.ready", 1);

        const bool automated =
          !m_screenshot_path.empty () || m_benchmark.has_value () ||
          m_water_shot.has_value () || m_gazetteer.has_value () ||
          ::getenv ("MOPPE_DEMO");
        if (!automated && !m_skip_cinematic_requested &&
            !m_cinematic_plan.empty ()) {
          m_cinematic.start (m_cinematic_plan, surface ());
          m_live_input.clear ();
        }
      }

      void upload_world_terrain (render::Renderer& r) {
        MOPPE_PROFILE_ZONE ("startup.upload_world_terrain");
        m_terrain.setup (r, surface (), world (), m_graphics);
        // The typed water and ground presentations can upload only after
        // set_terrain has established the texture dimensions.
        upload_water (r,
                      generated_world ().water_surface (),
                      world ().water_level,
                      world ().map_size);
        upload_surface_readings (
          r, surface (), surface_readings (), !m_water_shot);
      }

      void cast_world_shadows (render::Renderer& r) {
        MOPPE_PROFILE_ZONE ("startup.cast_world_shadows");
        m_terrain.render_shadow (r, sun_direction_for (m_graphics.sun_height));
      }

      void update_world_atmosphere (float total_time) {
        // Weather remains part of the world while actors are paused.
        cloud_cover_t cloudiness =
          (std::sin (total_time * 0.0003f) * 0.4f + 0.5f +
           0.3f * std::pow (std::sin (total_time * 0.0008f), 2.0f) +
           std::sin (total_time * 0.02f) * 0.05f) *
          cloud_cover[one];
        cloudiness = std::clamp (
          cloudiness, 0.0f * cloud_cover[one], 1.0f * cloud_cover[one]);
        logic ().m_cloudiness = cloudiness;

        // Fog stays mostly sky-blue. Directional warmth is added in the
        // shaders only when looking toward the sun.
        const DisplayColor horizon = horizon_color_for (m_graphics.sun_height);
        logic ().m_fog =
          mix_display (horizon, DisplayColor (0.90f, 0.94f, 1.0f), 0.18f);
      }

      // -- simulation --------------------------------------------------

      void tick (float elapsed) override {
        if (!m_ready) {
          m_simulation_clock.reset ();
          return;
        }

        // Offline replay and cinematic capture deliberately bind one logical
        // step to one rendered frame. Ordinary play instead consumes the
        // presentation interval through a fixed 120 Hz clock.
        const bool frame_locked =
          m_benchmark.has_value () ||
          (m_cinematic.active () && ::getenv ("MOPPE_CINEMATIC_CAPTURE_DIR"));
        if (frame_locked) {
          m_simulation_clock.reset ();
          tick_simulation (elapsed);
          return;
        }

        const int steps = m_simulation_clock.consume (elapsed);
        MOPPE_PROFILE_PLOT ("simulation.steps", steps);
        for (int step = 0; step < steps; ++step)
          tick_simulation (static_cast<float> (FIXED_SIMULATION_STEP_SECONDS));

        // The HUD reports rendered-frame cadence, not the internal 120 Hz
        // integration frequency. Gazetteer views intentionally report zero.
        if (m_ready && !m_gazetteer)
          logic ().m_frame_time = elapsed;
      }

      void tick_simulation (float dt) {
        MOPPE_PROFILE_ZONE ("MoppeGame::tick_simulation");
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
            m_benchmark_render_frame.reset ();
            if (m_renderer->benchmark_complete () &&
                !m_benchmark_results_written) {
              m_renderer->write_benchmark_results ();
              m_benchmark_results_written = true;
              platform::request_quit ();
            }
            return;
          }
          prepare_benchmark_epoch ();
          m_benchmark_render_frame = m_benchmark_replay->current_frame ();
          if (!m_benchmark_render_frame)
            throw std::logic_error ("graphics benchmark has no replay frame");
          scripted_input = m_benchmark_render_frame->input;
        }
        if (m_benchmark_render_frame) {
          MOPPE_PROFILE_PLOT ("benchmark.mask", m_benchmark_mask);
          MOPPE_PROFILE_PLOT ("benchmark.partition_mask",
                              m_benchmark_render_frame->partition_mask);
          MOPPE_PROFILE_PLOT ("benchmark.epoch",
                              m_benchmark_render_frame->epoch);
          MOPPE_PROFILE_PLOT ("benchmark.logical_frame",
                              m_benchmark_render_frame->logical_frame);
          MOPPE_PROFILE_PLOT ("benchmark.measured",
                              m_benchmark_render_frame->measured);
        }
        if (!m_ready)
          return;
        logic ().m_frame_time = dt;
        if (logic ().m_game_over)
          return;

        // The gazetteer is an offline frame composer, not a demo playback.
        // Simulation, actors, wind, and weather are frozen; only the camera's
        // terrain-aware sun visibility is derived anew for the current shot.
        if (m_gazetteer) {
          constexpr float documentary_time = 41.0f;
          logic ().m_frame_time = 0.0f;
          logic ().m_total_time = documentary_time;
          update_world_atmosphere (documentary_time);
          const FrameView view = compose_frame_view (frame_view_input (1.0f));
          logic ().m_flare = sun_visibility_target (view, world (), surface ());
          return;
        }

        InputFrame input = m_live_input.take_frame ();
        if (scripted_input)
          input = *scripted_input;

        logic ().m_total_time += dt;
        const float total_time = logic ().m_total_time;
        update_world_atmosphere (total_time);

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
            m_cinematic.tick (dt, surface (), controls);
            if (!m_cinematic.active ())
              leave_cinematic ();
            update_frame_flare ();
            return;
          }
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

        const GameSessionAdvanceResult advance = advance_game_session (
          world (), surface (), m_obstacles, session (), input, seconds (dt));
        if (advance.say_ouchies)
          platform::say ("Ouchies. That hurts.");

        if (m_water_inspection) {
          session ().camera ().place (m_water_inspection->eye,
                                      m_water_inspection->target);
          session ().camera ().limit (surface ());
        }

        if (m_benchmark)
          finish_benchmark_frame (m_benchmark_replay->finish_frame ());
        update_frame_flare ();
      }

      // -- rendering ---------------------------------------------------

      static render::FrameParams frame_params_for (const FrameView& frame) {
        render::FrameParams params;
        params.view = frame.camera.view;
        params.proj = frame.camera.projection;
        params.camera_pos = frame.camera.position;
        params.cam_right = frame.camera.right;
        params.cam_up = frame.camera.up;
        params.cam_forward = frame.camera.frame_forward;
        params.clear_color = frame.lighting.clear_color;
        params.fog_scale = attenuation_value (frame.lighting.fog_scale);
        params.sun_dir = frame.lighting.sun_direction;
        params.sun_diffuse = frame.lighting.sun_diffuse;
        params.sun_specular = frame.lighting.sun_specular;
        params.ambient = frame.lighting.ambient;
        params.exposure_bias = frame.lighting.exposure_bias;
        params.time = frame.lighting.time;
        params.cloud_cover = frame.lighting.cloudiness.numerical_value_in (one);
        params.sun_visibility = frame.lighting.sun_visibility;
        params.upscaling = frame.graphics.upscaling;
        params.scene_scale = frame.graphics.scene_scale;
        params.render_scale_override = frame.graphics.render_scale_override;
        params.scene_megapixel_budget = frame.graphics.scene_megapixel_budget;
        params.bloom = frame.graphics.bloom;
        params.auto_exposure = frame.graphics.auto_exposure;
        params.lens_flare = frame.graphics.lens_flare;
        params.profile = true;
        params.benchmark_mask = frame.benchmark.mask;
        params.benchmark_partition_mask = frame.benchmark.partition_mask;
        params.benchmark_epoch = frame.benchmark.epoch;
        params.benchmark_frame = frame.benchmark.logical_frame;
        params.benchmark_measured = frame.benchmark.measured;
        return params;
      }

      static HudState hud_state_for (const FrameHud& reading) {
        HudState state;
        state.speed_kmh = reading.speed_kmh;
        state.boost_ready01 = reading.boost_ready01;
        state.health01 = reading.health01;
        state.odometer_m = reading.odometer_m;
        state.lives = reading.lives;
        state.stars = reading.stars;
        state.score = reading.score;
        state.airtime_s = reading.airtime_s;
        state.spin_degrees = reading.spin_degrees;
        state.landed_airtime_s = reading.landed_airtime_s;
        state.landed_spin_degrees = reading.landed_spin_degrees;
        state.landed_points = reading.landed_points;
        state.landed_clean = reading.landed_clean;
        state.landed_age_s = reading.landed_age_s;
        state.on_foot = reading.on_foot;
        state.gliding = reading.gliding;
        state.can_deploy_glider = reading.can_deploy_glider;
        state.can_drop_bike = reading.can_drop_bike;
        state.vertical_speed_mps = reading.vertical_speed_mps;
        state.frame_time_s = reading.frame_time_s;
        state.heading_radians = reading.heading_radians;
        return state;
      }

      void draw_world_layers (render::Renderer& r, const FrameView& frame) {
        const FrameVisibility& visibility = frame.visibility;
        const Vec3& camera = frame.camera.position;
        if (m_graphics.terrain_shadows)
          m_terrain.render_local_shadow (r,
                                         position (camera),
                                         frame.camera.frame_forward,
                                         frame.lighting.sun_direction);
        const auto draw_world_sky = [&] {
          render::SkyParams sky;
          sky.time = frame.lighting.time;
          sky.sun_height = frame.lighting.sun_height;
          sky.cloudiness = frame.lighting.cloudiness.numerical_value_in (one);
          sky.sun_dir = frame.lighting.sun_direction;
          sky.fog_color = frame.lighting.fog_color;
          r.draw_sky (sky);
        };

        // At this extreme altitude, drawing the far-plane dome after terrain
        // exposes depth precision at the horizon. Paint it first in the lab;
        // terrain then covers it deterministically. Gameplay retains the
        // cheaper depth-culled order below.
        if (visibility.sky_before_terrain)
          draw_world_sky ();

        // Terrain first, chunk-culled to the haze horizon.
        m_terrain.render (
          r, camera, frame.camera.forward, frame.terrain_distance);

        // Sky AFTER the terrain: depth testing kills the expensive
        // cloud shader wherever terrain covers it.
        if (visibility.sky_after_terrain)
          draw_world_sky ();

        if (visibility.forest)
          m_forest.draw (r);

        // The floor grows itself from the same canopy and moisture fields the
        // trees were planted from, so it arrives already agreeing with them.
        // Gameplay movers part the generated field locally; cinematics keep
        // their authored floor undisturbed even though actors may exist.
        if (visibility.undergrowth) {
          float interaction_radius = 0.0f;
          if (frame.scene == FrameSceneMode::Gameplay) {
            switch (frame.actors.active_mode) {
            case M_BIKE:
              interaction_radius = 1.15f;
              break;
            case M_FOOT:
              interaction_radius = 0.55f;
              break;
            case M_CAR:
              interaction_radius = 1.55f;
              break;
            case M_GLIDER:
              break;
            }
          }
          r.draw_undergrowth (
            { .time = frame.lighting.time,
              .cloud_cover = frame.lighting.cloudiness.numerical_value_in (one),
              .reach = 58.0f,
              .density = 1.0f,
              .interaction_position = frame.hud.subject_position,
              .interaction_radius = interaction_radius });
        }
      }

      void draw_actor_layers (render::Renderer& r, const FrameView& frame) {
        const FrameVisibility& visibility = frame.visibility;
        if (!visibility.actors)
          return;

        // The world draw list, in the GL build's draw order.
        m_world_dl.clear ();
        const FrameActors& actors = frame.actors;

        // Soft blob shadows under the movers.
        draw_home_base_marker (m_world_dl);
        m_blob.draw (m_world_dl, surface (), actors.bike.position, 2.2f);
        if (actors.car)
          m_blob.draw (m_world_dl, surface (), actors.car->position, 2.9f);
        if (actors.walker)
          m_blob.draw (m_world_dl,
                       surface (),
                       actors.walker->position + Vec3 (0, 0.5f, 0),
                       0.8f);
        if (actors.glider)
          m_blob.draw (m_world_dl, surface (), actors.glider->position, 3.4f);

        // In helmet cam you ARE the rider: don't draw yourself.
        const bool helmet = actors.helmet_camera;
        if (!(helmet && actors.active_mode == M_BIKE))
          render_vehicle (
            r, m_world_dl, actors.bike, frame.lighting.time, 0x1000);
        if (actors.car && !(helmet && actors.active_mode == M_CAR))
          render_vehicle (
            r, m_world_dl, *actors.car, frame.lighting.time, 0x2000);
        if (actors.walker && !helmet)
          render_walker (m_world_dl, *actors.walker, frame.lighting.time);
        if (actors.glider && !helmet)
          render_glider (m_world_dl, *actors.glider, frame.lighting.time);

        r.draw_list (m_world_dl, 0x0001);

        // Additive glow after the solid list, so it blends over everything
        // already drawn: exhaust and jump-jet flames, then star halos.
        if (visibility.vehicle_effects &&
            !(helmet && actors.active_mode == M_BIKE))
          render_vehicle_flames (r, actors.bike, frame.lighting.time, 0x1000);
        if (visibility.vehicle_effects && actors.car &&
            !(helmet && actors.active_mode == M_CAR))
          render_vehicle_flames (r, *actors.car, frame.lighting.time, 0x2000);
        if (visibility.star_effects)
          session ().stars ().render (r, frame.environment);
      }

      void draw_water_surfaces (render::Renderer& r, const FrameView& frame) {
        const FrameVisibility& visibility = frame.visibility;
        const Vec3& camera = frame.camera.position;

        // The lab keeps the game's painted water while the map is the game's
        // own; a rebuilt map invalidates the water sheets, so they disappear
        // until the lab's own analysis draws ribbons.
        if (visibility.ocean) {
          render::OceanParams ocean;
          ocean.time = frame.lighting.time;
          ocean.fog_color = frame.lighting.fog_color;
          ocean.fog_scale = attenuation_value (frame.lighting.fog_scale);
          const Vec3& world_extent = extent_value (world ().map_size);
          const Vec3 center (0.5f * world_extent[0], 0, 0.5f * world_extent[2]);
          ocean.world_offset[0] = camera[0] - center[0];
          ocean.world_offset[2] = camera[2] - center[2];
          r.draw_ocean (ocean);
        }

        // Running channels are part of the same clipped water-level field as
        // lakes and the sea. Only vertical nickpoint geometry is separate;
        // ordinary reaches never draw an overlapping ribbon here.
        if (visibility.waterfall_curtains)
          m_waterfall_surface.draw (r, camera);
      }

      void draw_effect_layers (render::Renderer& r, const FrameView& frame) {
        const FrameVisibility& visibility = frame.visibility;

        // Dust last so spray sits atop every water surface.
        if (visibility.dust)
          session ().dust ().render (r);

        // Reconstruction consumes untouched color/depth/motion/reactivity;
        // screen-space grades and feedback operate on its full-size result.
        r.reconstruct_scene ();

        // Screen-space post lighting shares the shaken camera basis,
        // extracted straight from the final view matrix with the frustum
        // half-extents folded into the right/up spans. Occlusion first so
        // the added beams are not darkened; sun shafts march the
        // camera-local shadow map, so the beams carry the same tree and
        // terrain shapes as the ground shadows. Underwater frames get
        // their own grade instead.
        if (!visibility.underwater &&
            (m_graphics.gtao || m_graphics.light_shafts)) {
          const FrameCamera& camera = frame.camera;
          const Mat4& view = camera.view;
          const Vec3 right (view.at (0, 0), view.at (1, 0), view.at (2, 0));
          const Vec3 up (view.at (0, 1), view.at (1, 1), view.at (2, 1));
          const Vec3 forward (
            -view.at (0, 2), -view.at (1, 2), -view.at (2, 2));
          const float half_tangent = tan (camera.field_of_view * 0.5f);
          const Vec3 right_span = right * (half_tangent * camera.aspect);
          const Vec3 up_span = up * half_tangent;
          if (m_graphics.gtao)
            r.apply_gtao ({
              .camera_pos = position (camera.position),
              .forward = forward,
              .right_span = right_span,
              .up_span = up_span,
            });
          if (m_graphics.light_shafts &&
              frame.lighting.sun_direction[1] > 0.02f)
            r.apply_light_shafts ({
              .camera_pos = position (camera.position),
              .forward = forward,
              .right_span = right_span,
              .up_span = up_span,
              .sun_dir = frame.lighting.sun_direction,
              .sun_color = frame.lighting.sun_diffuse,
              .strength = 0.55f * one,
            });
        }
        if (visibility.underwater)
          r.apply_underwater (frame.lighting.time);
        if (visibility.motion_blur)
          r.apply_motion_blur (frame.motion_blur_amount);
      }

      void draw_overlays (render::Renderer& r, const FrameView& frame) {
        const FrameVisibility& visibility = frame.visibility;

        // HUD, kept inside the safe area (notch / home indicator).
        m_hud_dl.clear ();
        const platform::Insets safe_insets = platform::safe_insets ();
        m_hud_dl.translate (safe_insets.left, safe_insets.top, 0);
        const int hud_width =
          r.width_pts () - (int)(safe_insets.left + safe_insets.right);
        const int hud_height =
          r.height_pts () - (int)(safe_insets.top + safe_insets.bottom);
        if (visibility.cinematic_hud) {
          if (m_loading_font && m_loading_font->ok ()) {
            const std::string prompt = "SPACE TO RIDE";
            m_hud_dl.color (
              0.91f, 1.0f, 0.92f, frame.overlay.cinematic_prompt_alpha);
            m_loading_font->draw (m_hud_dl,
                                  hud_width - m_loading_font->measure (prompt) -
                                    28.0f,
                                  hud_height - 42.0f,
                                  prompt);
          }
        } else if (visibility.game_hud) {
          const HudState hud_state = hud_state_for (frame.hud);
          m_hud.draw (m_hud_dl, hud_state, hud_width, hud_height);
          draw_trail_map (m_hud_dl,
                          hud_width,
                          hud_height,
                          frame.hud.subject_position,
                          frame.hud.subject_heading);
        }

        // Even a clean inspection capture needs this empty HUD pass: it is
        // also the final post-chain composite into the drawable.
        r.draw_hud (m_hud_dl);
      }

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

        const float aspect =
          (float)r.width_pts () / std::max (1, r.height_pts ());
        const FrameView frame = compose_frame_view (frame_view_input (aspect));
        const bool cinematic = frame.visibility.cinematic;
        const GazetteerShot* gazetteer_shot = current_gazetteer_shot ();

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
            const int capture_count = cinematic_capture_frame_limit ();
            const bool survey = ::getenv ("MOPPE_CINEMATIC_CAPTURE_PROGRESS");
            const float next_progress =
              (m_cinematic_capture_frame + 0.5f) / capture_count;
            const bool sample_frame =
              survey ? m_cinematic.route_progress () >= next_progress
                     : m_cinematic_capture_render_frame++ %
                           cinematic_capture_frame_step () ==
                         0;
            if (sample_frame && m_cinematic_capture_frame < capture_count) {
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
          r.request_screenshot (m_screenshot_path);
        }
        const bool captured_gazetteer =
          gazetteer_shot && m_gazetteer &&
          m_gazetteer_settle_frame >= m_gazetteer->settle_frames;
        if (captured_gazetteer) {
          const std::filesystem::path path =
            std::filesystem::path (m_gazetteer->output_directory) /
            gazetteer_image_filename (m_gazetteer_shot, gazetteer_shot->name);
          r.request_screenshot (path.string ());
        }
        if (m_snapshot_requested) {
          m_snapshot_requested = false;
          r.request_screenshot (next_snapshot_path ());
        }
        if (!r.begin_frame (frame_params_for (frame)))
          return;

        draw_world_layers (r, frame);
        draw_actor_layers (r, frame);

        draw_water_surfaces (r, frame);
        draw_effect_layers (r, frame);

        draw_overlays (r, frame);

        r.end_frame ();
        if (captured) {
          m_screenshot_path.clear ();
          platform::request_quit ();
        }
        if (captured_cinematic) {
          if (m_cinematic_capture_frame >= cinematic_capture_frame_limit ())
            platform::request_quit ();
        }
        if (gazetteer_shot) {
          if (!captured_gazetteer) {
            ++m_gazetteer_settle_frame;
          } else {
            std::cerr << "gazetteer frame " << m_gazetteer_shot + 1 << '/'
                      << m_gazetteer_plan.shots.size () << ": "
                      << gazetteer_shot->name << '\n';
            ++m_gazetteer_shot;
            m_gazetteer_settle_frame = 0;
            if (m_gazetteer_shot >= m_gazetteer_plan.shots.size ())
              platform::request_quit ();
            else
              r.reset_temporal_state ();
          }
        }
      }

      void render_loading (render::Renderer& r) {
        const float width = static_cast<float> (r.width_pts ());
        const float height = static_cast<float> (r.height_pts ());

        // Take the finished world now, but run the heavy finishing work
        // after this frame is submitted, so the panel first shows what is
        // about to happen.
        std::unique_ptr<GeneratedWorld> completed =
          m_loading.take_completed_world ();
        if (completed)
          m_loading.report ("Finishing the world",
                            "Growing forests and planning the first journey");

        const LoadingStatus loading = m_loading.status ();

        // A fixed camera watching the sky is the whole scene; the panel
        // below carries the actual information.
        const Vec3 eye (0.0f, 34.0f, 0.0f);
        const Vec3 target (0.0f, 27.0f, -100.0f);
        render::FrameParams fp;
        fp.upscaling = m_graphics.upscaling;
        // The loading screen sizes the same render targets the game will use.
        // Leaving the budget off here would build a full-drawable set only to
        // replace it on the first world frame.
        fp.scene_scale = m_graphics.scene_scale;
        fp.render_scale_override = m_graphics.render_scale_override;
        fp.scene_megapixel_budget = m_graphics.scene_megapixel_budget;
        fp.view = Mat4::look_at (eye, target, Vec3 (0, 1, 0));
        fp.proj = Mat4::perspective_reversed (
          64.0f * u::deg, width / std::max (1.0f, height), 0.5f, 9000.0f);
        fp.camera_pos = eye;
        constexpr float loading_sun_height = 0.70f;
        fp.clear_color = horizon_color_for (loading_sun_height);
        fp.sun_dir = normalized (Vec3 (0.82f, 0.58f, 0.0f));
        sun_light_colors_for (
          loading_sun_height, fp.sun_diffuse, fp.sun_specular);
        fp.ambient = DisplayColor (0.58f, 0.55f, 0.48f);
        fp.time = loading.elapsed;
        fp.exposure_bias = 1.0f;
        fp.sun_visibility = 0.32f;
        if (!r.begin_frame (fp)) {
          // The world must not be dropped just because no frame started.
          if (completed)
            finish_loading (r, std::move (completed));
          return;
        }

        render::SkyParams sky;
        sky.time = loading.elapsed;
        sky.sun_height = loading_sun_height;
        sky.cloudiness = 0.14f;
        sky.sun_dir = fp.sun_dir;
        sky.fog_color = fp.clear_color;
        r.draw_sky (sky);

        m_hud_dl.clear ();
        render::DrawState state;
        state.blend = true;
        state.depth_test = false;
        state.depth_write = false;
        state.cull = false;
        m_hud_dl.state (state);
        m_hud_dl.lit (false);
        m_hud_dl.fogged (false);

        if (m_loading_font && m_loading_font->ok () && m_loading_title_font &&
            m_loading_title_font->ok () && m_loading_meta_font &&
            m_loading_meta_font->ok ()) {
          const float panel_x = 24.0f;
          const float panel_width = std::min (660.0f, width - 48.0f);
          const float panel_height = 214.0f;
          const float panel_y = std::max (24.0f, height - panel_height - 24.0f);
          const float text_x = panel_x + 24.0f;
          const float content_width = panel_width - 48.0f;
          const auto fill_rect = [this] (float x, float y, float w, float h) {
            m_hud_dl.begin (render::Prim::Quads);
            m_hud_dl.vertex (x, y);
            m_hud_dl.vertex (x + w, y);
            m_hud_dl.vertex (x + w, y + h);
            m_hud_dl.vertex (x, y + h);
            m_hud_dl.end ();
          };

          m_hud_dl.color (0.025f, 0.055f, 0.045f, 0.78f);
          fill_rect (panel_x, panel_y, panel_width, panel_height);
          m_hud_dl.color (0.69f, 0.89f, 0.70f, 0.78f);
          fill_rect (panel_x, panel_y, 3.0f, panel_height);

          std::ostringstream eyebrow;
          eyebrow << "WORLD GENERATION  /  SEED " << loading.seed;
          m_hud_dl.color (0.72f, 0.86f, 0.74f, 0.88f);
          m_loading_meta_font->draw (
            m_hud_dl, text_x, panel_y + 29.0f, eyebrow.str ());

          m_hud_dl.color (0.95f, 1.0f, 0.94f, 0.98f);
          m_loading_title_font->draw (
            m_hud_dl, text_x, panel_y + 67.0f, loading.title);

          m_hud_dl.color (0.78f, 0.88f, 0.79f, 0.94f);
          m_loading_font->draw (
            m_hud_dl, text_x, panel_y + 94.0f, loading.detail);

          // The rail fills only with a real measurement; stages that cannot
          // measure themselves show their text and nothing else.
          const float rail_y = panel_y + 119.0f;
          m_hud_dl.color (0.28f, 0.38f, 0.31f, 0.82f);
          fill_rect (text_x, rail_y, content_width, 3.0f);
          if (loading.progress >= 0.0f) {
            m_hud_dl.color (0.70f, 0.94f, 0.71f, 0.98f);
            fill_rect (text_x,
                       rail_y,
                       content_width *
                         std::clamp (loading.progress, 0.0f, 1.0f),
                       3.0f);
            std::ostringstream percent;
            percent << static_cast<int> (
                         std::lround (loading.progress * 100.0f))
                    << '%';
            m_hud_dl.color (0.72f, 0.86f, 0.74f, 0.90f);
            m_loading_meta_font->draw (
              m_hud_dl,
              text_x + content_width -
                m_loading_meta_font->measure (percent.str ()),
              panel_y + 143.0f,
              percent.str ());
          }

          const std::size_t history_end =
            loading.events.empty () ? 0 : loading.events.size () - 1;
          const std::size_t history_begin =
            history_end > 2 ? history_end - 2 : 0;
          float line_y = panel_y + 174.0f;
          for (std::size_t i = history_begin; i < history_end; ++i) {
            const LoadingEvent& event = loading.events[i];
            std::ostringstream line;
            line << std::fixed << std::setprecision (1) << event.elapsed
                 << "s  " << event.title;
            m_hud_dl.color (0.64f, 0.75f, 0.65f, 0.76f);
            m_loading_meta_font->draw (m_hud_dl, text_x, line_y, line.str ());
            line_y += 20.0f;
          }
        }

        bool captured = false;
        if (const char* path = ::getenv ("MOPPE_LOADING_SCREENSHOT")) {
          if (m_loading.claim_loading_capture (completed != nullptr)) {
            r.request_screenshot (path);
            captured = true;
          }
        }
        r.draw_hud (m_hud_dl);
        r.end_frame ();
        if (captured)
          platform::request_quit ();

        // The frame announcing the finish is on its way to the display;
        // now do the finishing work.
        if (completed)
          finish_loading (r, std::move (completed));
      }
      void render_game_over (render::Renderer& r) {
        render::FrameParams fp;
        fp.upscaling = m_graphics.upscaling;
        fp.scene_scale = m_graphics.scene_scale;
        fp.render_scale_override = m_graphics.render_scale_override;
        fp.scene_megapixel_budget = m_graphics.scene_megapixel_budget;
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
        if (!m_ready || logic ().m_game_over)
          return;
        m_live_input.controls (state);
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
          if ((k == Key::R || k == Key::Restart) && down)
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

        if (k == Key::Screenshot && down) {
          m_snapshot_requested = true;
          return;
        }

        if (m_cinematic.active ()) {
          if (k == Key::Escape && down)
            platform::request_quit ();
          else
            m_live_input.cinematic_key (k, down);
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
      // The in-game screenshot key drops frames into one per-run timestamped
      // directory, so a walk through the world becomes a reviewable series.
      std::string next_snapshot_path () {
        namespace fs = std::filesystem;
        if (m_snapshot_directory.empty ()) {
          const char* base = ::getenv ("MOPPE_SCREENSHOT_DIR");
          char stamp[32];
          const std::time_t now = std::time (nullptr);
          std::strftime (
            stamp, sizeof stamp, "run-%Y%m%d-%H%M%S", std::localtime (&now));
          fs::path directory = fs::path (base ? base : "screenshots") / stamp;
          std::error_code error;
          fs::create_directories (directory, error);
          if (error) {
            directory =
              fs::temp_directory_path () / "moppe-screenshots" / stamp;
            fs::create_directories (directory, error);
          }
          m_snapshot_directory = directory.string ();
        }
        std::ostringstream path;
        path << m_snapshot_directory << "/shot-" << std::setfill ('0')
             << std::setw (3) << m_snapshot_count++ << ".png";
        std::cerr << "moppe: screenshot " << path.str () << '\n';
        return path.str ();
      }

      const GazetteerShot* current_gazetteer_shot () const noexcept {
        if (!m_gazetteer || m_gazetteer_shot >= m_gazetteer_plan.shots.size ())
          return nullptr;
        return &m_gazetteer_plan.shots[m_gazetteer_shot];
      }

      FrameViewInput frame_view_input (float aspect) const {
        FrameSceneMode scene = FrameSceneMode::Gameplay;
        FrameCameraReading camera;
        const bool cinematic = m_cinematic.active ();

        if (const GazetteerShot* shot = current_gazetteer_shot ()) {
          scene = FrameSceneMode::Gazetteer;
          const Vec3& eye = position_value (shot->eye);
          const Vec3& subject = position_value (shot->subject);
          camera = {
            .position = eye,
            .forward = normalized (subject - eye),
            .view = Mat4::look_at (eye, subject, Vec3 (0, 1, 0)),
            .field_of_view = shot->vertical_field_of_view,
          };
        } else if (cinematic) {
          scene = FrameSceneMode::Cinematic;
          camera = {
            .position = m_cinematic.position (),
            .forward = m_cinematic.forward (),
            .view = m_cinematic.view_matrix (),
            .field_of_view = m_cinematic.field_of_view () * u::deg,
          };
        } else {
          if (m_water_inspection)
            scene = FrameSceneMode::WaterInspection;
          camera = {
            .position = session ().camera ().position (),
            .forward = session ().camera ().forward (),
            .view = session ().camera ().view_matrix (),
            .field_of_view = 70.0f * u::deg,
          };
        }

        FrameBenchmarkTag benchmark { .mask = m_benchmark_mask };
        if (m_benchmark_render_frame) {
          benchmark.partition_mask = m_benchmark_render_frame->partition_mask;
          benchmark.epoch = m_benchmark_render_frame->epoch;
          benchmark.logical_frame = m_benchmark_render_frame->logical_frame;
          benchmark.measured = m_benchmark_render_frame->measured;
        }

        return {
          .world = world (),
          .surface = surface (),
          .session = session (),
          .graphics = m_graphics,
          .selected_camera = camera,
          .scene = scene,
          .aspect = aspect,
          .cinematic_motion_blur =
            cinematic ? m_cinematic.motion_blur () : 0.0f,
          .cinematic_elapsed = cinematic ? m_cinematic.elapsed () : 0.0f,
          .benchmark = benchmark,
        };
      }

      void update_frame_flare () {
        const FrameView frame = compose_frame_view (frame_view_input (1.0f));
        const float target =
          sun_visibility_target (frame, world (), surface ());
        logic ().m_flare += (target - logic ().m_flare) * 0.12f;
      }

      void prepare_benchmark_epoch () {
        if (!m_benchmark_epoch_pending)
          return;
        const std::optional<GraphicsBenchmarkReplay::Frame> frame =
          m_benchmark_replay->current_frame ();
        if (!frame || frame->prelude || !m_benchmark_checkpoint)
          throw std::logic_error ("graphics benchmark lost its checkpoint");

        if (frame->epoch > 0)
          session ().restore (*m_benchmark_checkpoint);
        m_renderer->reset_temporal_state ();
        m_graphics = m_benchmark_baseline;
        m_benchmark_mask =
          apply_graphics_benchmark_mask (m_graphics, frame->partition_mask);
        update_benchmark_title (frame->epoch, frame->partition_mask);
        m_benchmark_epoch_pending = false;
      }

      void finish_benchmark_frame (GraphicsBenchmarkReplay::Boundary boundary) {
        switch (boundary) {
        case GraphicsBenchmarkReplay::Boundary::none:
          return;
        case GraphicsBenchmarkReplay::Boundary::prelude_complete:
          m_benchmark_checkpoint = session ().state ();
          m_benchmark_epoch_pending = true;
          std::cerr << "moppe: graphics benchmark: "
                    << m_benchmark_replay->configuration_count ()
                    << " configurations, " << m_benchmark->settle_frames
                    << " settle + " << m_benchmark->measured_frames
                    << " measured frames each\n";
          return;
        case GraphicsBenchmarkReplay::Boundary::epoch_complete:
          m_benchmark_epoch_pending = true;
          return;
        case GraphicsBenchmarkReplay::Boundary::complete:
          m_benchmark_submitted = true;
          platform::set_window_title (
            "Moppe benchmark - finishing GPU samples");
          return;
        }
      }

      void update_benchmark_title (int epoch, uint32_t partition_mask) const {
        if (!m_benchmark)
          return;
        const int configurations = 1 << graphics_benchmark_dimension_count ();
        std::ostringstream title;
        title << "Moppe benchmark " << (epoch + 1) << '/' << configurations
              << " - ";
        bool any = false;
        for (std::size_t bit = 0; bit < RidingGraphicsPartition::blocks.size ();
             ++bit)
          if (partition_mask & (1u << bit)) {
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

      Vec3 subject_position () const {
        return session ().subject_position ();
      }

      Vec3 subject_heading () const {
        return session ().subject_heading ();
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
        session ().camera ().limit (surface ());
      }

      void regenerate_world () {
        session ().clear_controls ();
        m_live_input.clear ();
        m_ready = false;
        m_skip_cinematic_requested = false;
        m_cinematic.stop ();
        m_cinematic_plan = {};
        m_waterfall_surface.clear ();
        m_water_inspection.reset ();
        const terrain::Seed next_seed = terrain::next_seed (recipe ().seed ());
        terrain::WorldRecipe next_recipe =
          terrain::make_world_recipe (recipe ().extent (),
                                      recipe ().resolution (),
                                      next_seed,
                                      recipe ().water_datum (),
                                      recipe ().generation_profile ());
        logic ().m_mode = M_BIKE;
        logic ().m_car_exists = false;
        logic ().m_game_over = false;
        logic ().m_health = 100.0f;
        m_params = bind_world_params (m_params, next_recipe);
        m_recipe = next_recipe;
        m_loading.start (world (), std::move (next_recipe));
      }

      void revive () {
        logic ().m_lives = 10;
        logic ().m_health = 100.0f;
        logic ().m_shake = 0.0f;
        logic ().m_shake_time = 0.0f;
        logic ().m_jump_airtime = 0.0f;
        logic ().m_jump_spin_radians = 0.0f;
        logic ().m_jump_peak_spin_radians = 0.0f;
        logic ().m_landed_age = 10.0f;
        logic ().m_mode = M_BIKE;
        // Back to the start, but ON the ground rather than 600 m
        // over it.
        const float ground =
          spatial::sample<terrain::surface_elevation> (
            surface (),
            position (Vec3 (m_spawn_position[0], 0, m_spawn_position[2])))
            .quantity_from_zero ()
            .numerical_value_in (u::m);
        session ().bike ().reset (
          Vec3 (m_spawn_position[0], ground + 1.2f, m_spawn_position[2]));
        // Key releases were swallowed during the game-over screen;
        // don't resume with the throttle stuck open.
        m_live_input.clear ();
        session ().clear_controls ();
        logic ().m_game_over = false;
      }

      WorldParams m_params;
      terrain::WorldRecipe m_recipe;
      // Absent until the loading worker finishes the first world. The active
      // owner changes only in activate_completed_world(); all gameplay reads
      // go through the accessors above, so no stale reference aliases survive
      // a handoff.
      std::unique_ptr<GeneratedWorld> m_generated_world;
      // Declared after its world so session-held terrain and surface borrows
      // release first during normal teardown.
      std::unique_ptr<GameSession> m_session;
      WorldLoading m_loading;
      GraphicsSettings m_graphics;
      Vec3 m_spawn_position;
      Vec3 m_home_base_position;
      bool m_skip_cinematic_requested = false;
      CinematicFlightPlan m_cinematic_plan;
      CinematicFlight m_cinematic;
      InputFrameAdapter m_live_input;
      SimulationClock m_simulation_clock;
      WaterfallSurface m_waterfall_surface;
      Terrain m_terrain;
      ForestLandscape m_forest;
      BlobShadow m_blob;
      std::vector<mov::Box> m_obstacles;
      Hud m_hud;
      std::unique_ptr<render::FontAtlas> m_loading_font;
      std::unique_ptr<render::FontAtlas> m_loading_title_font;
      std::unique_ptr<render::FontAtlas> m_loading_meta_font;

      render::Renderer* m_renderer;
      bool m_automated_regeneration_done = false;
      std::string m_screenshot_path;
      bool m_snapshot_requested = false;
      std::string m_snapshot_directory;
      int m_snapshot_count = 0;
      std::optional<WaterShot> m_water_shot;
      std::optional<WaterInspection> m_water_inspection;
      std::optional<GazetteerCaptureConfig> m_gazetteer;
      LandscapeGazetteer m_gazetteer_plan;
      std::size_t m_gazetteer_shot = 0;
      int m_gazetteer_settle_frame = 0;
      int m_screenshot_frames;
      int m_cinematic_capture_frame = 0;
      int m_cinematic_capture_render_frame = 0;
      std::atomic<bool> m_ready;
      std::optional<GraphicsBenchmarkConfig> m_benchmark;
      GraphicsSettings m_benchmark_baseline;
      std::optional<GraphicsBenchmarkReplay> m_benchmark_replay;
      std::optional<GameState> m_benchmark_checkpoint;
      std::optional<GraphicsBenchmarkReplay::Frame> m_benchmark_render_frame;
      uint32_t m_benchmark_mask = 0;
      bool m_benchmark_epoch_pending = false;
      bool m_benchmark_submitted = false;
      bool m_benchmark_results_written = false;

      render::DrawList m_world_dl;
      render::DrawList m_hud_dl;
    };

    std::unique_ptr<platform::Game>
    make_moppe_game (const LaunchOptions& options,
                     terrain::WorldRecipe recipe) {
      return std::make_unique<MoppeGame> (options, std::move (recipe));
    }
  }
}
