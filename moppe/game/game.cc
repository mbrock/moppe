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
#include <moppe/game/inspector_ui.hh>
#include <moppe/game/launch_options.hh>
#include <moppe/game/moppe_game.hh>
#include <moppe/game/river_surface.hh>
#include <moppe/game/seed_memory.hh>
#include <moppe/game/stars.hh>
#include <moppe/game/surface_presentation.hh>
#include <moppe/game/terrain.hh>
#include <moppe/game/terrain_lab.hh>
#include <moppe/game/tree_stand.hh>
#include <moppe/game/vehicle_render.hh>
#include <moppe/game/walker_render.hh>
#include <moppe/game/water_capture.hh>
#include <moppe/game/water_presentation.hh>
#include <moppe/game/world.hh>
#include <moppe/game/world_loading.hh>
#include <moppe/map/generate.hh>
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
#include <filesystem>
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

    class MoppeGame : public platform::Game {
    public:
      MoppeGame (const LaunchOptions& options, terrain::WorldRecipe recipe)
          : m_generated_world (std::make_unique<GeneratedWorld> (
              options.world, std::move (recipe))),
            m_session (std::make_unique<GameSession> (
              this->world (), this->map (), this->surface ())),
            m_loading (this->recipe ()), m_graphics (options.graphics),
            m_spawn_position (
              position_value (this->world ().spawn_position ())),
            m_renderer (0),
            m_start_in_terrain_lab (options.start_in_terrain_lab),
            m_tree_demo (options.tree_demo), m_tree_count (options.tree_count),
            m_screenshot_path (options.screenshot_path),
            m_water_shot (options.water_shot), m_screenshot_frames (0),
            m_ready (false), m_benchmark (options.benchmark),
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
        const std::size_t width = grid.width;
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
                           int height_pts,
                           const Vec3& subject,
                           Vec3 heading) const {
        if (!trail_network () || width_pts < 480 || height_pts < 360)
          return;
        const terrain::TerrainGrid& grid = trail_network ()->source_grid;
        const auto& alignment = trail_network ()->alignment.points;
        if (alignment.size () < 2)
          return;
        const float period_x = grid.width * grid.spacing_x_m ();
        const float period_z = grid.height * grid.spacing_y_m ();
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

        const Vec3 relative_subject (
          wrap_delta (subject[0] - home_x, period_x),
          0,
          wrap_delta (subject[2] - home_z, period_z));
        Vec3 player = map_point (relative_subject);
        player[0] =
          std::clamp (player[0], map_x + 5.0f, map_x + map_size - 5.0f);
        player[1] =
          std::clamp (player[1], map_y + 5.0f, map_y + map_size - 5.0f);
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

      void prepare_world_water () {
        MOPPE_PROFILE_ZONE ("startup.prepare_world_water");
        render::Renderer& r = *m_renderer;
        // Running rivers are continuous ribbon meshes. The water sheets retain
        // standing bodies and carry each mouth's current into them.
        if (rivers ())
          m_river_surface.rebuild (r, map (), *rivers ());
        m_water_presentation.reset (world ().water_level, world ().map_size);
        if (const auto& water = generated_world ().water_surface ())
          m_water_presentation.refresh (*water, map ().scale ()[1] * u::m);
      }

      void prepare_world_surface () {
        MOPPE_PROFILE_ZONE ("startup.prepare_world_surface");
        m_surface_presentation.refresh (surface ());
        session ().bike ().set_water_level (world ().water_level);
        session ().car ().set_water_level (world ().water_level);
        session ().bike ().set_obstacles (&m_obstacles);
        session ().car ().set_obstacles (&m_obstacles);

        if (m_water_shot) {
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
      }

      void place_stars_and_player () {
        MOPPE_PROFILE_ZONE ("startup.place_stars_and_player");
        session ().stars ().generate (map (), world (), 80);
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

      void grow_global_forest () {
        MOPPE_PROFILE_ZONE ("startup.build_global_forest");
        if (m_tree_demo || m_water_inspection)
          return;
        m_forest.rebuild (
          *m_renderer, surface (), recipe ().seed ().value ^ 0xa34c91e5U);
        std::cerr << "global forest: " << m_forest.tree_count ()
                  << " canopy representatives\n";
      }

      void plant_trailside () {
        MOPPE_PROFILE_ZONE ("startup.plant_trailside");
        render::Renderer& r = *m_renderer;
        if (m_tree_demo) {
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
          return;
        }
        if (m_water_inspection) {
          session ().camera ().place (m_water_inspection->eye,
                                      m_water_inspection->target);
          return;
        }
        constexpr std::size_t forest_size = 32;
        m_tree_stand.rebuild (r,
                              surface (),
                              recipe ().seed ().value ^ 0x4f1bbcdcU,
                              forest_size,
                              m_home_base_position);
        const TreeGrove& forest = m_tree_stand.grove ();
        const auto cohort_count = [&] (TreeCohort cohort) {
          return std::ranges::count_if (
            forest.sites,
            [cohort] (const TreeSite& site) { return site.cohort == cohort; });
        };
        std::cerr << "forest: " << forest.sites.size () << " organisms ("
                  << cohort_count (TreeCohort::canopy) << " canopy, "
                  << cohort_count (TreeCohort::young) << " young, "
                  << cohort_count (TreeCohort::sapling) << " saplings)\n";
      }

      void plan_opening_journey () {
        MOPPE_PROFILE_ZONE ("startup.plan_cinematic_flight");
        if (standing_water () && lake_census () && drainage () && rivers ())
          m_cinematic_plan = plan_cinematic_flight (map (),
                                                    *standing_water (),
                                                    *lake_census (),
                                                    *drainage (),
                                                    *rivers (),
                                                    m_spawn_position,
                                                    trail_network ());
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
        plant_trailside ();
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
        m_ready = true;
        MOPPE_PROFILE_PLOT ("startup.ready", 1);

        const bool automated =
          !m_screenshot_path.empty () || m_benchmark.has_value () ||
          m_water_shot.has_value () || m_tree_demo || ::getenv ("MOPPE_DEMO");
        if (m_start_in_terrain_lab) {
          m_terrain_lab.enter (r,
                               generated_world ().terrain (),
                               m_terrain,
                               world (),
                               m_graphics,
                               recipe (),
                               m_surface_presentation.trails (),
                               m_surface_presentation.home_base (),
                               sun_direction_for (m_graphics.sun_height));
          m_start_in_terrain_lab = false;
        } else if (!automated && !m_skip_cinematic_requested &&
                   !m_cinematic_plan.empty ()) {
          m_cinematic.start (m_cinematic_plan, map ());
          m_live_input.clear ();
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
            update_frame_flare ();
            return;
          }
        }

        // Terrain inspection pauses actors and vehicle physics, but keeps the
        // visual clock, weather, and fog alive so sky and water remain a
        // useful frame of reference around the heightmap.
        if (m_terrain_lab.active ()) {
          m_terrain_lab.tick (dt);
          update_frame_flare ();
          return;
        }

        // The botanical demo is a stationary observatory. Weather and the
        // global animation clock continue above, so the retained trees keep
        // moving in the renderer's wind field while actors remain paused.
        if (m_tree_demo) {
          const TreeGrove& grove = m_tree_stand.grove ();
          session ().camera ().place (grove.camera_eye, grove.camera_target);
          update_frame_flare ();
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

        const GameSessionAdvanceContext advance_context {
          world (),
          map (),
          m_obstacles,
          m_landscape_scale_x,
          m_landscape_scale_y,
        };
        const GameSessionAdvanceResult advance = advance_game_session (
          advance_context, session (), input, seconds (dt));
        if (advance.say_ouchies)
          platform::say ("Ouchies. That hurts.");

        if (m_water_inspection) {
          session ().camera ().place (m_water_inspection->eye,
                                      m_water_inspection->target);
          session ().camera ().limit (map ());
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
        params.sun_visibility = frame.lighting.sun_visibility;
        params.scene_scale = frame.graphics.scene_scale;
        params.render_scale_override = frame.graphics.render_scale_override;
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
        state.landed_airtime_s = reading.landed_airtime_s;
        state.landed_points = reading.landed_points;
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
        const auto draw_world_sky = [&] {
          render::SkyParams sky;
          sky.time = frame.lighting.time;
          sky.sun_height = frame.lighting.sun_height;
          // A world-shaping overview should keep the game world's moving sky,
          // without letting a passing front hide the land being edited.
          sky.cloudiness = visibility.terrain_lab
                             ? std::min (frame.lighting.cloudiness, 0.35f)
                             : frame.lighting.cloudiness;
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
          m_forest.draw (r, camera, frame.camera.forward);

        if (visibility.tree_stand)
          m_tree_stand.draw (r);
      }

      void draw_actor_layers (render::Renderer& r, const FrameView& frame) {
        const FrameVisibility& visibility = frame.visibility;
        if (!visibility.actors)
          return;

        // The world draw list, in the GL build's draw order. Terrain Lab
        // deliberately hides every placed object so generator differences are
        // not confused with stale actor positions.
        m_world_dl.clear ();
        const FrameActors& actors = frame.actors;

        // Soft blob shadows under the movers.
        draw_home_base_marker (m_world_dl);
        m_blob.draw (m_world_dl, map (), actors.bike.position, 2.2f);
        if (actors.car)
          m_blob.draw (m_world_dl, map (), actors.car->position, 2.9f);
        if (actors.walker)
          m_blob.draw (m_world_dl,
                       map (),
                       actors.walker->position + Vec3 (0, 0.5f, 0),
                       0.8f);
        if (actors.glider)
          m_blob.draw (m_world_dl, map (), actors.glider->position, 3.4f);

        // In helmet cam you ARE the rider: don't draw yourself.
        const bool helmet = actors.helmet_camera;
        if (!(helmet && actors.active_mode == M_BIKE))
          render_vehicle (r,
                          m_world_dl,
                          actors.bike,
                          frame.lighting.time,
                          actors.visual_scale);
        if (actors.car && !(helmet && actors.active_mode == M_CAR))
          render_vehicle (r,
                          m_world_dl,
                          *actors.car,
                          frame.lighting.time,
                          actors.visual_scale);
        if (actors.walker && !helmet)
          render_walker (m_world_dl,
                         *actors.walker,
                         frame.lighting.time,
                         actors.visual_scale);
        if (actors.glider && !helmet)
          render_glider (m_world_dl,
                         *actors.glider,
                         frame.lighting.time,
                         actors.visual_scale);

        r.draw_list (m_world_dl);

        // Additive glow after the solid list, so it blends over everything
        // already drawn: exhaust and jump-jet flames, then star halos.
        if (visibility.vehicle_effects &&
            !(helmet && actors.active_mode == M_BIKE))
          render_vehicle_flames (
            r, actors.bike, frame.lighting.time, actors.visual_scale);
        if (visibility.vehicle_effects && actors.car &&
            !(helmet && actors.active_mode == M_CAR))
          render_vehicle_flames (
            r, *actors.car, frame.lighting.time, actors.visual_scale);
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

        // Standing water writes depth first. Rivers then shade only their
        // visible surface, rather than paying for fragments hidden beneath a
        // lake or the sea, and retain a clean current layer through mouths.
        if (visibility.terrain_lab_rivers)
          m_terrain_lab.render_rivers (r, camera);
        else if (visibility.river_ribbons)
          m_river_surface.draw (r, camera);
      }

      void draw_effect_layers (render::Renderer& r, const FrameView& frame) {
        const FrameVisibility& visibility = frame.visibility;

        // Dust last so spray sits atop every water surface.
        if (visibility.dust)
          session ().dust ().render (r);

        // Post effects.
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
        if (visibility.terrain_lab_hud) {
          m_terrain_lab.draw (m_hud_dl, hud_width, hud_height);
        } else if (visibility.cinematic_hud) {
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
        if (visibility.terrain_topology_hint) {
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
          session ().clear_controls ();
          m_live_input.clear ();
          m_terrain_lab.enter (*m_renderer,
                               generated_world ().terrain (),
                               m_terrain,
                               world (),
                               m_graphics,
                               recipe (),
                               m_surface_presentation.trails (),
                               m_surface_presentation.home_base (),
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

      FrameViewInput frame_view_input (float aspect) const {
        FrameSceneMode scene = FrameSceneMode::Gameplay;
        FrameCameraReading camera;
        const bool cinematic = m_cinematic.active ();
        const bool terrain_lab = m_terrain_lab.active ();

        if (cinematic) {
          scene = FrameSceneMode::Cinematic;
          camera = {
            .position = m_cinematic.position (),
            .forward = m_cinematic.forward (),
            .view = m_cinematic.view_matrix (),
            .field_of_view = m_cinematic.field_of_view (),
          };
        } else if (terrain_lab) {
          scene = FrameSceneMode::TerrainLab;
          camera = {
            .position = m_terrain_lab.position (),
            .forward = m_terrain_lab.forward (),
            .view = m_terrain_lab.view_matrix (),
            .field_of_view = 70.0f,
          };
        } else {
          if (m_water_inspection)
            scene = FrameSceneMode::WaterInspection;
          else if (m_tree_demo)
            scene = FrameSceneMode::TreeDemo;
          camera = {
            .position = session ().camera ().position (),
            .forward = session ().camera ().forward (),
            .view = session ().camera ().view_matrix (),
            .field_of_view = 70.0f,
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
          .terrain = map (),
          .session = session (),
          .graphics = m_graphics,
          .selected_camera = camera,
          .scene = scene,
          .terrain_lab_fog = terrain_lab
                               ? m_terrain_lab.scene_fog (world ().fog_scale)
                               : world ().fog_scale,
          .terrain_lab_torus = terrain_lab && m_terrain_lab.torus_view (),
          .terrain_lab_pristine = !terrain_lab || m_terrain_lab.map_pristine (),
          .aspect = aspect,
          .landscape_scale_x = m_landscape_scale_x,
          .landscape_scale_y = m_landscape_scale_y,
          .cinematic_motion_blur =
            cinematic ? m_cinematic.motion_blur () : 0.0f,
          .cinematic_elapsed = cinematic ? m_cinematic.elapsed () : 0.0f,
          .benchmark = benchmark,
        };
      }

      void update_frame_flare () {
        const FrameView frame = compose_frame_view (frame_view_input (1.0f));
        const float target = sun_visibility_target (frame, world (), map ());
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
        session ().camera ().limit (map ());
      }

      void regenerate_world () {
        session ().clear_controls ();
        m_live_input.clear ();
        m_ready = false;
        m_skip_cinematic_requested = false;
        m_cinematic.stop ();
        m_cinematic_plan = {};
        m_river_surface.clear ();
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
        m_loading.start (world (), std::move (next_recipe));
      }

      void revive () {
        logic ().m_lives = 10;
        logic ().m_health = 100.0f;
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
        m_live_input.clear ();
        session ().clear_controls ();
        logic ().m_game_over = false;
      }

      // The active owner changes only in activate_completed_world().  All
      // gameplay reads go through the accessors above, so no stale reference
      // aliases survive a handoff.
      std::unique_ptr<GeneratedWorld> m_generated_world;
      // Declared after its world so session-held terrain and surface borrows
      // release first during normal teardown.
      std::unique_ptr<GameSession> m_session;
      WorldLoading m_loading;
      GraphicsSettings m_graphics;
      Vec3 m_spawn_position;
      Vec3 m_home_base_position;
      SurfacePresentation m_surface_presentation;
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
      std::unique_ptr<render::FontAtlas> m_loading_title_font;
      std::unique_ptr<render::FontAtlas> m_loading_meta_font;

      render::Renderer* m_renderer;
      bool m_start_in_terrain_lab;
      bool m_tree_demo;
      std::size_t m_tree_count;
      bool m_automated_regeneration_done = false;
      std::string m_screenshot_path;
      std::optional<WaterShot> m_water_shot;
      std::optional<WaterInspection> m_water_inspection;
      int m_screenshot_frames;
      int m_cinematic_capture_frame = 0;
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
