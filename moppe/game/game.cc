// The game: the port of the original MoppeGLUT application class onto
// the platform/render abstractions.  World generation runs on a
// background thread behind a loading screen; the frame follows the
// exact pass order of the GL build's render_scene().  The command line
// that configures a launch is resolved before this file is reached; see
// launch_options.hh and main.cc.

#include <moppe/environment.hh>
#include <moppe/platform/platform.hh>
#include <moppe/profile.hh>
#include <moppe/render/renderer.hh>
#include <moppe/render/text.hh>

#include <moppe/game/blob_shadow.hh>
#include <moppe/game/boulders.hh>
#include <moppe/game/cairn.hh>
#include <moppe/game/camp.hh>
#include <moppe/game/chase_camera.hh>
#include <moppe/game/cinematic_flight.hh>
#include <moppe/game/daylight.hh>
#include <moppe/game/dust.hh>
#include <moppe/game/figure.hh>
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
#include <moppe/game/mushrooms.hh>
#include <moppe/game/opening.hh>
#include <moppe/game/seed_memory.hh>
#include <moppe/game/simulation_clock.hh>
#include <moppe/game/stars.hh>
#include <moppe/game/surface_presentation.hh>
#include <moppe/game/terrain.hh>
#include <moppe/game/vehicle_render.hh>
#include <moppe/game/video.hh>
#include <moppe/game/walker_render.hh>
#include <moppe/game/water_capture.hh>
#include <moppe/game/water_presentation.hh>
#include <moppe/game/waterfall_surface.hh>
#include <moppe/game/weather.hh>
#include <moppe/game/world.hh>
#include <moppe/game/world_loading.hh>
#include <moppe/map/surface.hh>
#include <moppe/mov/glider.hh>
#include <moppe/mov/vehicle.hh>
#include <moppe/terrain/flood.hh>
#include <moppe/terrain/fractional_drainage.hh>
#include <moppe/terrain/moisture.hh>
#include <moppe/terrain/readings.hh>
#include <moppe/terrain/river.hh>
#include <moppe/terrain/trail.hh>
#include <moppe/terrain/watercourse.hh>
#include <moppe/terrain/waterline.hh>
#include <moppe/terrain/world_recipe.hh>

#include <algorithm>
#include <atomic>
#include <chrono>
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
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace moppe {
  namespace game {
    static int cinematic_capture_frame_limit () {
      if (const char* value =
            moppe::environment ("MOPPE_CINEMATIC_CAPTURE_FRAMES"))
        return std::max (1, ::atoi (value));
      return 450;
    }

    static int cinematic_capture_frame_step () {
      if (const char* value =
            moppe::environment ("MOPPE_CINEMATIC_CAPTURE_STEP"))
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
            m_benchmark_baseline (options.graphics),
            m_bike_physics (options.bike_physics) {
        m_day = plan_day ();
        m_sky = m_almanac.held (m_graphics.sun_height);
        if (m_benchmark)
          m_benchmark_replay.emplace (GraphicsBenchmarkReplay::Config {
            m_benchmark->prelude_frames,
            m_benchmark->settle_frames,
            m_benchmark->measured_frames,
            m_benchmark->partition,
          });
      }

      // -- lifecycle ---------------------------------------------------

      void setup (render::Renderer& r, int, int) override {
        MOPPE_PROFILE_ZONE ("MoppeGame::setup");
        m_renderer = &r;
        std::cerr << "moppe: simulation: fixed-step=120 Hz, catch-up-limit="
                  << MAX_SIMULATION_CATCH_UP_STEPS << " steps, bike-physics="
                  << (m_bike_physics == mov::BikePhysics::rigid ? "rigid"
                                                                : "classic")
                  << std::endl;

        // Fast, main-thread resource setup; the heavy world build
        // runs behind the loading screen.
        {
          MOPPE_PROFILE_ZONE ("startup.load_hud");
          m_hud.load (r);
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

      // Temporal-stability inspection: MOPPE_ORBIT="radius,height,seconds"
      // circles the camera at a steady pace around the tree nearest the
      // rider, so frame-to-frame discontinuities in the forest stand out
      // against otherwise smooth motion.
      // The spectator starts in the densest conifer stand the forest holds:
      // the plan's sites are counted on a 40-metre lattice and the camera
      // stands at the centre of the fullest cell.
      void place_spectator () {
        if (!m_spectator)
          return;
        const ForestPlan& plan = generated_world ().forest ();
        const Vec3 period = extent_value (plan.period);
        constexpr float cell = 40.0f;
        const int nx = std::max (1, static_cast<int> (period[0] / cell));
        const int nz = std::max (1, static_cast<int> (period[2] / cell));
        std::vector<int> conifers (static_cast<std::size_t> (nx) * nz, 0);
        for (const ForestSite& site : plan.sites) {
          if (site.form != ForestForm::conifer)
            continue;
          const Vec3 at = position_value (site.position);
          const int x = std::clamp (static_cast<int> (at[0] / cell), 0, nx - 1);
          const int z = std::clamp (static_cast<int> (at[2] / cell), 0, nz - 1);
          ++conifers[static_cast<std::size_t> (z) * nx + x];
        }
        const auto densest =
          std::max_element (conifers.begin (), conifers.end ()) -
          conifers.begin ();
        Vec3 eye ((static_cast<float> (densest % nx) + 0.5f) * cell,
                  0.0f,
                  (static_cast<float> (densest / nx) + 0.5f) * cell);
        eye[1] = terrain::surface_elevation_value (
                   spatial::sample<terrain::surface_elevation> (
                     surface (), moppe::position (eye))) +
                 1.7f;
        m_spectator->eye = eye;
        // The pointer is captured: moving the mouse looks around, as in any
        // first-person view, without a button held.
        platform::set_pointer_captured (true);
        std::cerr << "moppe: spectator in the densest conifer stand at " << eye
                  << " (" << conifers[densest] << " spruce in 40 m)\n";
      }

      // WASD walks the eye through the air, Space and Tab rise and sink, and
      // the mouse (captured, no button needed) or the arrow keys turn the
      // head.
      void spectator_camera (float dt) {
        if (!m_spectator)
          return;
        Spectator& view = *m_spectator;
        const auto held = [&view] (platform::Key k) {
          return view.held.count (k) ? 1.0f : 0.0f;
        };
        using platform::Key;
        view.yaw += 1.6f * dt * (held (Key::Right) - held (Key::Left));
        view.pitch += 1.2f * dt * (held (Key::Up) - held (Key::Down));
        view.pitch = std::clamp (view.pitch, -1.45f, 1.45f);
        // W flies where the eye looks, so looking down and pressing W
        // descends.
        const Vec3 forward (std::cos (view.yaw) * std::cos (view.pitch),
                            std::sin (view.pitch),
                            std::sin (view.yaw) * std::cos (view.pitch));
        const Vec3 right (-std::sin (view.yaw), 0.0f, std::cos (view.yaw));
        const float speed = 22.0f;
        view.eye += (forward * (held (Key::W) - held (Key::S)) +
                     right * (held (Key::D) - held (Key::A)) +
                     Vec3 (0, 1, 0) * (held (Key::Space) - held (Key::Tab))) *
                    (speed * dt);
        const float ground = terrain::surface_elevation_value (
                               spatial::sample<terrain::surface_elevation> (
                                 surface (), moppe::position (view.eye))) +
                             0.4f;
        view.eye[1] = std::max (static_cast<float> (view.eye[1]), ground);
        const Vec3 look (std::cos (view.yaw) * std::cos (view.pitch),
                         std::sin (view.pitch),
                         std::sin (view.yaw) * std::cos (view.pitch));
        session ().camera ().place (view.eye, view.eye + look * 10.0f);
      }

      void pointer_move (float, float, float dx, float dy) override {
        if (!m_spectator) {
          if (m_ready && !m_pointer_free)
            m_live_input.look (dx, dy);
          return;
        }
        if (m_spectator->pointer_free)
          return;
        m_spectator->yaw += 0.004f * dx;
        m_spectator->pitch =
          std::clamp (m_spectator->pitch - 0.004f * dy, -1.45f, 1.45f);
      }

      void orbit_camera () {
        // MOPPE_PAN="seconds": stand at the rider and sweep the view left
        // and right through a half turn, the way a rider looks around.
        if (static const char* pan = moppe::environment ("MOPPE_PAN"); pan) {
          const float period = std::max (1.0f, (float)::atof (pan));
          const float yaw =
            1.5707963f * std::sin (6.2831853f * logic ().m_total_time / period);
          const Vec3 eye = session ().subject_position () + Vec3 (0, 1.6f, 0);
          session ().camera ().place (
            eye, eye + Vec3 (std::cos (yaw), 0.08f, std::sin (yaw)) * 10.0f);
          return;
        }
        static const char* orbit = moppe::environment ("MOPPE_ORBIT");
        if (!orbit)
          return;
        float radius = 14.0f, height = 3.0f, period = 24.0f;
        std::sscanf (orbit, "%f,%f,%f", &radius, &height, &period);
        if (!m_orbit_tree) {
          // The tallest-standing loner near the rider: the tree whose nearest
          // neighbour is farthest away, so it fills the frame by itself.
          const Vec3 at = session ().subject_position ();
          std::vector<const mov::Trunk*> near;
          for (const mov::Trunk& trunk : m_forest.trunks ()) {
            const Vec3 d = trunk.root - at;
            if (d[0] * d[0] + d[2] * d[2] < 400.0f * 400.0f)
              near.push_back (&trunk);
          }
          float loneliest = 0.0f;
          for (const mov::Trunk* trunk : near) {
            if (trunk->height < 12.0f)
              continue;
            float nearest = std::numeric_limits<float>::infinity ();
            for (const mov::Trunk* other : near)
              if (other != trunk) {
                const Vec3 d = other->root - trunk->root;
                nearest = std::min (nearest, d[0] * d[0] + d[2] * d[2]);
              }
            if (nearest > loneliest) {
              loneliest = nearest;
              m_orbit_tree = *trunk;
            }
          }
          if (!m_orbit_tree)
            return;
          std::cerr << "moppe: orbiting tree at " << m_orbit_tree->root
                    << " height " << m_orbit_tree->height << '\n';
        }
        const float turn =
          6.2831853f * logic ().m_total_time / std::max (period, 1.0f);
        const Vec3 centre = m_orbit_tree->root;
        Vec3 eye =
          centre +
          Vec3 (radius * std::cos (turn), 0.0f, radius * std::sin (turn));
        eye[1] = spatial::sample<terrain::surface_elevation> (
                   surface (), moppe::position (eye))
                   .quantity_from_zero ()
                   .numerical_value_in (u::m) +
                 height;
        session ().camera ().place (
          eye, centre + Vec3 (0, 0.45f * m_orbit_tree->height, 0));
      }

      // The trodden trail tread is loose dirt; bare eroded faces are soil;
      // turf holds most of its dust and throws up clods of itself, and
      // fallen leaves lie wherever the turned groves have dropped them.
      GroundCover ground_cover (const Vec3& at) const {
        const auto read = [&] (auto quantity) {
          return spatial::sample<quantity> (surface_readings (),
                                            moppe::position (at))
            .numerical_value_in (one);
        };
        const float tread =
          smoothstep (0.80f, 0.88f, read (map::trail_influence));
        const float bare =
          smoothstep (0.50f, 0.70f, read (map::erosion_exposure));
        const float wet =
          smoothstep (0.45f, 0.80f, read (map::surface_moisture));
        GroundCover ground;
        ground.leaves = m_forest.litter_at (at);
        const float loose = std::max (tread, bare);
        ground.dust = (0.2f + 0.8f * loose) * (1.0f - 0.6f * wet) *
                      (1.0f - 0.7f * ground.leaves);
        const auto blend = [] (DisplayColor a, DisplayColor b, float t) {
          return DisplayColor (a.red + (b.red - a.red) * t,
                               a.green + (b.green - a.green) * t,
                               a.blue + (b.blue - a.blue) * t);
        };
        ground.dust_color = blend (DisplayColor (0.56f, 0.53f, 0.43f),
                                   DisplayColor (0.60f, 0.52f, 0.40f),
                                   loose);
        ground.clod_color = blend (DisplayColor (0.27f, 0.32f, 0.16f),
                                   DisplayColor (0.42f, 0.34f, 0.24f),
                                   loose);
        // Under fallen leaves the wheel digs up dark forest soil.
        ground.clod_color = blend (
          ground.clod_color, DisplayColor (0.34f, 0.26f, 0.17f), ground.leaves);
        return ground;
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

      // The trailhead is marked by a cairn at the path's edge, built once
      // per world.
      void build_home_base_marker () {
        m_home_base_marker.clear ();
        build_cairn (m_home_base_marker,
                     m_home_base_position,
                     trail_direction_from_home (),
                     static_cast<std::uint32_t> (recipe ().seed ().value),
                     [this] (float x, float z) {
                       return ground_height (Vec3 (x, 0.0f, z));
                     });
      }

      void draw_home_base_marker (render::DrawList& dl) const {
        dl.append (m_home_base_marker);
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
        m_session =
          std::make_unique<GameSession> (world (), surface (), m_bike_physics);
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
        begin_day ();
        m_home_base_position =
          trail_cell_position (trail_network ().plan.home_base);
        build_home_base_marker ();
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
        const char* demo = moppe::environment ("MOPPE_DEMO");
        if (demo && std::string_view (demo) == "forest")
          move_spawn_to_forest ();
        // It is a walking game first: the rider stands beside the parked
        // bike. The demo autopilot and benchmarks still start riding,
        // unless a scripted walk (MOPPE_WALK) asks for the feet.
        if ((!demo || moppe::environment ("MOPPE_WALK")) && !m_benchmark &&
            !m_gazetteer) {
          session ().start_on_foot ();
          // Scripted walks can watch from behind or in front of the figure.
          if (const char* view = moppe::environment ("MOPPE_WALK_CAMERA")) {
            const std::string_view name (view);
            if (name == "chase" || name == "side")
              logic ().m_cam_mode = CAM_CHASE;
            else if (name == "front")
              logic ().m_cam_mode = CAM_FRONT;
          }
        }
      }

      // MOPPE_DEMO=forest: park the bike at the world's forest-floor site.
      void move_spawn_to_forest () {
        const LandscapeGazetteer views =
          plan_landscape_gazetteer (surface (),
                                    surface_readings (),
                                    standing_water (),
                                    lake_census (),
                                    drainage (),
                                    rivers (),
                                    trail_network (),
                                    position (m_spawn_position),
                                    m_sky.sun);
        const auto forest = std::find_if (
          views.shots.begin (), views.shots.end (), [] (const auto& shot) {
            return shot.name == "forest-floor";
          });
        if (forest == views.shots.end ())
          throw std::runtime_error ("forest demo found no forest-floor site");

        m_spawn_position = position_value (forest->eye);
        m_spawn_position[1] =
          terrain::surface_elevation_value (
            spatial::sample<terrain::surface_elevation> (
              surface (),
              moppe::position (
                Vec3 (m_spawn_position[0], 0.0f, m_spawn_position[2])))) +
          1.2f;
        Vec3 heading = position_value (forest->subject) - m_spawn_position;
        heading[1] = 0.0f;
        if (length2 (heading) < 1e-5f)
          heading = Vec3 (0, 0, 1);
        heading = normalized (heading);
        session ().bike ().reset (m_spawn_position);
        session ().bike ().set_heading (heading);
        std::cerr << "forest demo: site=" << forest->site.value
                  << " forest-cover="
                  << forest->readings.forest_cover.numerical_value_in (one)
                  << '\n';
      }

      // The tree laboratory (MOPPE_TREE_LAB, best with --uplift-years 0 for
      // a rolling plain): the whole forest is replaced by a few specimens
      // standing in the open ahead of the spawn -- a spruce, a birch, and a
      // taller spruce -- so their behaviour can be studied with nothing else
      // in view.
      ForestPlan tree_lab_plan () const {
        ForestPlan plan;
        plan.period = generated_world ().forest ().period;
        const Vec3 ahead = trail_direction_from_home ();
        const Vec3 side (-ahead[2], 0.0f, ahead[0]);
        // Seeds the trunk forest's stable thinning keeps.
        struct Specimen {
          float ahead, side, size;
          ForestForm form;
          std::uint32_t seed;
        };
        const Specimen specimens[] = {
          { 28.0f, 0.0f, 1.0f, ForestForm::conifer, 0x7a3e11u },
          { 26.0f, -18.0f, 1.0f, ForestForm::broadleaf, 0x7a3e12u },
          { 48.0f, 16.0f, 1.3f, ForestForm::conifer, 0x7a3e15u },
        };
        for (const Specimen& specimen : specimens) {
          Vec3 at =
            m_spawn_position + ahead * specimen.ahead + side * specimen.side;
          at[1] = terrain::surface_elevation_value (
            spatial::sample<terrain::surface_elevation> (
              surface (), moppe::position (Vec3 (at[0], 0.0f, at[2]))));
          plan.sites.push_back (
            { .position = moppe::position (at),
              .normal = Vec3 (0, 1, 0) * terrain::terrain_normal[one],
              .cover = 0.5f * map::forest_cover[one],
              .moisture = 0.5f * map::surface_moisture[one],
              .size = specimen.size * tree_size_factor[one],
              .seed = specimen.seed,
              .form = specimen.form,
              .age = ForestAge::mature });
        }
        return plan;
      }

      void grow_global_forest () {
        MOPPE_PROFILE_ZONE ("startup.build_global_forest");
        if (m_water_inspection)
          return;
        static const bool tree_lab = moppe::environment ("MOPPE_TREE_LAB") != 0;
        m_forest.rebuild (*m_renderer,
                          tree_lab ? tree_lab_plan ()
                                   : generated_world ().forest ());
        std::cerr << "global forest: " << m_forest.tree_count ()
                  << " canopy representatives, "
                  << m_forest.resident_bytes () / (1024 * 1024)
                  << " MB resident\n";
      }

      // Loose rock is planned from the finished surface each time a world
      // activates: it is cheap beside the forest and needs no cache.
      void scatter_boulders () {
        MOPPE_PROFILE_ZONE ("startup.scatter_boulders");
        if (m_water_inspection)
          return;
        if (moppe::environment ("MOPPE_TREE_LAB")) {
          m_boulders.rebuild (*m_renderer, BoulderPlan {});
          return;
        }
        const meters_t sea_level = world ().water_level;
        const float sea = sea_level.numerical_value_in (u::m);
        const float highest =
          terrain::measure_height_range (surface ()).maximum;
        const auto start = std::chrono::steady_clock::now ();
        const BoulderPlan plan =
          plan_boulders (surface (),
                         surface_readings (),
                         generated_world ().water_surface (),
                         recipe ().seed ().value ^ 0x6b0d1e55U,
                         sea_level,
                         std::max (highest - sea, 1.0f) * u::m);
        std::cerr << "moppe: boulder plan: "
                  << std::chrono::duration_cast<std::chrono::milliseconds> (
                       std::chrono::steady_clock::now () - start)
                       .count ()
                  << " ms" << std::endl;
        m_boulders.rebuild (*m_renderer, plan);
      }

      // The autumn's mushrooms come up around the trees each time a world
      // activates; like the boulders they are cheap and need no cache.
      void grow_mushrooms () {
        MOPPE_PROFILE_ZONE ("startup.grow_mushrooms");
        if (m_water_inspection || moppe::environment ("MOPPE_TREE_LAB")) {
          m_mushrooms.rebuild ({});
          return;
        }
        m_mushrooms.rebuild (
          plan_mushrooms (generated_world ().forest (),
                          surface (),
                          surface_readings (),
                          generated_world ().water_surface (),
                          recipe ().seed ().value ^ 0x3c5f00d5U,
                          world ().water_level,
                          m_home_base_position));
        if (const auto first = m_mushrooms.nearest (m_spawn_position, 400.0f)) {
          const Vec3 at = m_mushrooms.site (*first).base;
          std::cerr << "moppe: nearest mushrooms ("
                    << mushroom_plural (m_mushrooms.site (*first).kind)
                    << ") "
                    << std::hypot (at[0] - m_spawn_position[0],
                                   at[2] - m_spawn_position[2])
                    << " m from the spawn" << std::endl;
          // The foraging script starts a few steps from them.
          const char* walk = moppe::environment ("MOPPE_WALK");
          if (walk && std::string_view (walk) == "forage" &&
              logic ().m_mode == M_FOOT) {
            Vec3 away = m_spawn_position - at;
            away[1] = 0.0f;
            away = length2 (away) > 1e-4f ? normalized (away) : Vec3 (1, 0, 0);
            Vec3 stand = at + away * 4.0f;
            stand[1] = ground_height (stand) + 0.1f;
            session ().walker ().spawn (moppe::position (stand), away * -1.0f);
            logic ().m_fp_eye = session ().walker ().eye_position ();
          }
        }
      }

      // A walker reaches for the mushroom at hand: an edible one goes in
      // the basket, a poisonous one is left where it grows.
      void reach_for_mushroom (const InputFrame& input) {
        if (logic ().m_mode != M_FOOT)
          return;
        const Vec3 feet = session ().walker ().position ();
        // F still remounts the bike when it is close enough; otherwise it
        // picks too, for the touch screen's single action button.
        const bool bike_near =
          length2 (feet - session ().bike ().position ()) < 5.0f * 5.0f;
        if (!input.deploy_glider && !(input.toggle_mount && !bike_near))
          return;
        const std::optional<std::uint32_t> found =
          m_mushrooms.within_reach (feet, session ().walker ().heading ());
        if (!found)
          return;
        const MushroomSite& site = m_mushrooms.site (*found);
        m_mushrooms.follow (logic ().m_basket);
        Basket& basket = logic ().m_basket;
        basket.last_reach_time = logic ().m_total_time;
        basket.last_kind = site.kind;
        basket.last_refused = !mushroom_edible (site.kind);
        if (basket.last_refused)
          return;
        basket.picked.push_back (*found);
        ++basket.count[static_cast<int> (site.kind)];
        m_mushrooms.follow (basket);
      }

      // Trunks and the larger boulders stop the rider alike, so both feed
      // the one streamed collision field.
      void settle_obstacles () {
        std::vector<mov::Trunk> obstacles = m_forest.trunks ();
        obstacles.insert (obstacles.end (),
                          m_boulders.colliders ().begin (),
                          m_boulders.colliders ().end ());
        const Vec3 period = extent_value (generated_world ().forest ().period);
        m_trunk_field.set_trunks (std::move (obstacles), period[0], period[2]);
        if (m_trunk_field.trunk_count ())
          std::cerr << "moppe: obstacle colliders: "
                    << m_trunk_field.trunk_count ()
                    << " (Box3D, streamed around the rider)" << std::endl;
      }

      // -- camp --------------------------------------------------------

      GroundHeight ground_reader () const {
        return [this] (float x, float z) {
          return ground_height (Vec3 (x, 0.0f, z));
        };
      }

      // The pair of trees the walker stands between, if a hammock would
      // hang there; looked for again only once they have moved.
      void seek_hammock_site () {
        const Vec3 feet = session ().walker ().position ();
        if (m_hammock_sought &&
            length2 (feet - m_hammock_sought_from) < 0.3f * 0.3f)
          return;
        m_hammock_sought = true;
        m_hammock_sought_from = feet;
        const std::vector<mov::Trunk> near = m_trunk_field.gather (feet, 8.0f);
        m_hammock_at_hand = hammock_site_at (near, feet, ground_reader ());
      }

      // Within a step of the hung hammock, beside its length.
      bool beside_hammock () const {
        const Camp& camp = logic ().m_camp;
        return camp.hung &&
               hammock_distance (camp.hammock,
                                 session ().walker ().position ()) < 1.5f;
      }

      // A fire is laid a step ahead of the walker, on dry and fairly
      // level ground.
      std::optional<Vec3> hearth_ahead () const {
        const Walker& walker = session ().walker ();
        Vec3 ahead = walker.heading ();
        ahead[1] = 0.0f;
        if (length2 (ahead) < 1e-6f)
          return std::nullopt;
        ahead = normalized (ahead);
        Vec3 hearth = walker.position () + ahead * 1.3f;
        hearth[1] = ground_height (hearth);
        if (hearth[1] < world ().water_level.numerical_value_in (u::m) + 0.2f)
          return std::nullopt;
        const Vec3 side (ahead[2], 0.0f, -ahead[0]);
        for (const Vec3& off :
             { ahead * 0.5f, ahead * -0.5f, side * 0.5f, side * -0.5f })
          if (std::fabs (ground_height (hearth + off) - hearth[1]) > 0.22f)
            return std::nullopt;
        return hearth;
      }

      // Lying in the hammock the hours run forty times as fast: one goes
      // by every three seconds of an ordinary day.
      static constexpr float RESTING_HASTE = 40.0f;

      // Hanging and taking down the hammock, lighting and dousing the fire,
      // lying down and getting up; and, while lying there, rocking it and
      // looking about, which is all that a sleeper's controls do.
      void tend_camp (InputFrame& input, float dt) {
        Camp& camp = logic ().m_camp;
        const double now = logic ().m_total_time;
        float haste = 1.0f;
        float push = 0.0f;
        if (logic ().m_mode != M_FOOT) {
          if (camp.resting) {
            camp.resting = false;
            camp.rest_time = now;
          }
        } else if (camp.resting) {
          const bool rise = input.deploy_glider || input.toggle_mount ||
                            input_value (input.boost) > 0.5f;
          if (rise && now - camp.rest_time > 0.8) {
            camp.resting = false;
            camp.rest_time = now;
          } else {
            push = input_value (input.turn);
            camp.gaze_yaw =
              std::clamp (camp.gaze_yaw + input.look_yaw, -2.6f, 2.6f);
            camp.gaze_pitch =
              std::clamp (camp.gaze_pitch + input.look_pitch, -0.2f, 1.5f);
            // After a quiet moment the hours begin to run; holding back
            // keeps them at their ordinary pace.
            if (input_value (input.drive) > -0.3f)
              haste =
                1.0f + (RESTING_HASTE - 1.0f) *
                         smoothstep (2.5f,
                                     7.0f,
                                     static_cast<float> (now - camp.rest_time));
          }
          const bool cycle = input.cycle_camera;
          input = {};
          input.cycle_camera = cycle;
        } else {
          seek_hammock_site ();
          const Walker& walker = session ().walker ();
          const Vec3 feet = walker.position ();
          if (input.hang_hammock) {
            if (beside_hammock ()) {
              camp.hung = false;
            } else if (m_hammock_at_hand) {
              camp.hung = true;
              camp.hammock = *m_hammock_at_hand;
              camp.swing = 0.0f;
              camp.swing_rate = 0.5f;
            }
          }
          if (input.light_fire) {
            Vec3 to_fire = feet - camp.hearth;
            to_fire[1] = 0.0f;
            if (camp.fire && length2 (to_fire) < 3.0f * 3.0f) {
              camp.fire = false;
              camp.fire_time = now;
            } else if (const std::optional<Vec3> hearth = hearth_ahead ()) {
              camp.fire = true;
              camp.hearth = *hearth;
              camp.fire_time = now;
              camp.next_spark = camp.next_smoke = now;
            }
          }
          const bool picking =
            m_mushrooms.within_reach (feet, walker.heading ()).has_value ();
          if (input.deploy_glider && !picking && beside_hammock ()) {
            camp.resting = true;
            camp.rest_time = now;
            camp.swing_rate += 0.8f;
            camp.gaze_yaw = 0.0f;
            camp.gaze_pitch = 1.1f;
            input = {};
          }
        }
        // The hammock is a pendulum, damped more when no one is in it.
        camp.swing_rate +=
          (-6.8f * camp.swing -
           (camp.resting ? 0.30f : 0.9f) * camp.swing_rate + 1.5f * push) *
          dt;
        camp.swing =
          std::clamp (camp.swing + camp.swing_rate * dt, -0.6f, 0.6f);
        logic ().m_day_haste +=
          (haste - logic ().m_day_haste) * std::min (1.0f, 1.5f * dt);
        feed_fire ();
      }

      // Sparks fly up from a burning fire and its smoke drifts off; a
      // doused one smoulders a while.
      void feed_fire () {
        Camp& camp = logic ().m_camp;
        const double now = logic ().m_total_time;
        const float burn = fire_burn (camp, now);
        const bool smouldering = !camp.fire && now - camp.fire_time < 8.0;
        if (burn <= 0.02f && !smouldering)
          return;
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);
        std::mt19937& rng = logic ().m_fx_rng;
        const auto scatter = [&] (float reach) {
          return (unit (rng) - 0.5f) * 2.0f * reach;
        };
        if (burn > 0.3f && now >= camp.next_spark) {
          camp.next_spark = now + 0.10 + 0.55 * unit (rng);
          Dust::Style spark;
          spark.size = 0.05f * u::m;
          spark.lifetime = (1.2f + unit (rng)) * u::s;
          spark.downward_acceleration =
            -0.6f * isq::acceleration[u::m / pow<2> (u::s)];
          spark.spread = 0.5f * one;
          spark.additive = true;
          session ().dust ().emit (
            moppe::position (camp.hearth +
                             Vec3 (scatter (0.12f), 0.4f, scatter (0.12f))),
            velocity (
              Vec3 (scatter (0.5f), 1.3f + 1.4f * unit (rng), scatter (0.5f))),
            unit (rng) < 0.3f ? 2 : 1,
            DisplayColor (1.0f, 0.62f, 0.22f),
            spark);
        }
        if (now >= camp.next_smoke) {
          camp.next_smoke = now + (smouldering ? 0.22 : 0.5);
          // Smoke shows by the light there is to see it in.
          const float shade = 0.06f + 0.36f * m_sky.daylight;
          Dust::Style smoke;
          smoke.size = 0.5f * u::m;
          smoke.lifetime = 4.0f * u::s;
          smoke.downward_acceleration =
            -0.35f * isq::acceleration[u::m / pow<2> (u::s)];
          smoke.spread = 0.45f * one;
          session ().dust ().emit (
            moppe::position (camp.hearth + Vec3 (0, 0.9f, 0)),
            velocity (Vec3 (0.35f, 0.7f, 0.12f)),
            1,
            DisplayColor (shade * 1.06f, shade, shade * 0.95f),
            smoke);
        }
      }

      // Someone in the hammock sees through eyes that lie back in it; any
      // other camera stands off and can be walked round with the look.
      // Both ease from and to where the walker stood.
      void rest_camera () {
        const Camp& camp = logic ().m_camp;
        const float load = hammock_load (camp, logic ().m_total_time);
        if (!camp.hung || load <= 0.001f || logic ().m_mode != M_FOOT)
          return;
        ChaseCamera& camera = session ().camera ();
        const Vec3 from = camera.position ();
        const Vec3 looking = camera.forward ();
        Vec3 eye, toward;
        if (logic ().m_cam_mode == CAM_HELMET) {
          eye = resting_eye (camp.hammock, camp.swing);
          toward = resting_gaze (camp.hammock, camp.gaze_yaw, camp.gaze_pitch);
        } else {
          const Vec3 subject =
            hammock_middle (camp.hammock) - Vec3 (0, 0.45f, 0);
          const float around = camp.gaze_yaw + 0.45f;
          eye = subject +
                (hammock_across (camp.hammock) * std::cos (around) +
                 hammock_along (camp.hammock) * std::sin (around)) *
                  3.7f +
                Vec3 (0, 0.75f + 1.2f * (1.1f - camp.gaze_pitch), 0);
          eye[1] =
            std::max (static_cast<float> (eye[1]), ground_height (eye) + 0.4f);
          toward = normalized (subject - eye);
        }
        const Vec3 at = from + (eye - from) * load;
        const Vec3 gaze = normalized (looking + (toward - looking) * load);
        camera.place (at, at + gaze * 10.0f);
      }

      // What there is to do about the camp where the walker stands.
      void add_camp_readings (HudState& state) const {
        const Camp& camp = logic ().m_camp;
        state.resting = camp.resting;
        state.clock_hours = m_sky.clock;
        if (logic ().m_mode != M_FOOT || state.mushroom_in_reach)
          return;
        if (camp.resting)
          state.camp = HudState::Camp::get_up;
        else if (beside_hammock ())
          state.camp = HudState::Camp::lie_down;
        else if (m_hammock_at_hand)
          state.camp = HudState::Camp::hang;
        else if (camp.hung && camp.fire_time < -50.0 &&
                 hammock_distance (camp.hammock,
                                   session ().walker ().position ()) < 12.0f)
          state.camp = HudState::Camp::light_fire;
      }

      void draw_camp (render::DrawList& dl) {
        const Camp& camp = logic ().m_camp;
        const double now = logic ().m_total_time;
        if (camp.hung)
          draw_hammock (dl, camp.hammock, hammock_load (camp, now), camp.swing);
        if (camp.fire_time > -50.0)
          draw_hearth (dl, camp.hearth, fire_burn (camp, now));
      }

      // Whether the figure is drawn lying in the hammock rather than
      // standing beside it.
      bool lying_down () const {
        const Camp& camp = logic ().m_camp;
        return camp.hung && logic ().m_mode == M_FOOT &&
               hammock_load (camp, logic ().m_total_time) > 0.5f;
      }

      void
      draw_sleeper (render::DrawList& dl, bool through_own_eyes, float time) {
        const Camp& camp = logic ().m_camp;
        if (!through_own_eyes)
          figure::draw (
            dl,
            pose_resting (
              camp.hammock,
              camp.swing,
              resting_gaze (camp.hammock, camp.gaze_yaw, camp.gaze_pitch),
              time));
        // The basket waits on the ground under the foot of the hammock.
        Vec3 floor = hammock_middle (camp.hammock) +
                     hammock_across (camp.hammock) * 0.75f -
                     hammock_along (camp.hammock) * 0.5f;
        floor[1] = ground_height (floor);
        render_basket_set_down (
          dl, floor, hammock_along (camp.hammock), m_basket_contents);
      }

      // MOPPE_WALK=camp: the walker starts a few steps from the fit pair of
      // trees nearest the spawn, on whichever side is clearer to walk in
      // from.
      void stage_camp_script () {
        const char* walk = moppe::environment ("MOPPE_WALK");
        if (!walk || std::string_view (walk) != "camp" ||
            logic ().m_mode != M_FOOT)
          return;
        const std::vector<mov::Trunk> near =
          m_trunk_field.gather (m_spawn_position, 700.0f);
        // Under open sky, so there are stars to lie and look at.
        m_camp_script.site = nearest_hammock_site (
          near, m_spawn_position, ground_reader (), 60.0f);
        if (!m_camp_script.site) {
          std::cerr << "moppe: camp script: no two trees near the spawn "
                       "would take a hammock\n";
          return;
        }
        const HammockSite& site = *m_camp_script.site;
        const Vec3 middle = hammock_middle (site);
        const Vec3 across = hammock_across (site);
        // How near the nearest trunk stands to the walk in from a side.
        const auto clearance = [&] (float side) {
          float least = 100.0f;
          for (const mov::Trunk& trunk : near) {
            Vec3 to = trunk.root - middle;
            to[1] = 0.0f;
            const float out = dot (to, across) * side;
            if (out < 1.0f || out > 9.0f)
              continue;
            least = std::min (least, length (to - across * (out * side)));
          }
          return least;
        };
        m_camp_script.side =
          clearance (1.0f) >= clearance (-1.0f) ? 1.0f : -1.0f;
        Vec3 stand = middle + across * (m_camp_script.side * 8.5f) +
                     hammock_along (site) * 1.5f;
        stand[1] = ground_height (stand) + 0.1f;
        Vec3 facing = middle - stand;
        facing[1] = 0.0f;
        session ().walker ().spawn (moppe::position (stand),
                                    normalized (facing));
        logic ().m_fp_eye = session ().walker ().eye_position ();
        std::cerr << "moppe: camp script: two trees "
                  << length (site.strap[1] - site.strap[0]) << " m apart, "
                  << length (middle - m_spawn_position) << " m from the spawn"
                  << std::endl;
      }

      // The script itself: walk in between the trees and hang the hammock,
      // step out and light a fire, come back and lie down, and after a
      // while see the sky through the sleeper's eyes.
      InputFrame camp_script (float dt) {
        InputFrame input;
        CampScript& script = m_camp_script;
        if (!script.site)
          return input;
        const HammockSite& site = *script.site;
        const Walker& walker = session ().walker ();
        const Vec3 feet = walker.position ();
        const float t = m_walk_script_time;
        const float waited = t - script.since;
        const auto next = [&] {
          ++script.step;
          script.since = t;
        };
        // Turns toward `target` and walks until within `near` of it.
        const auto walk_to = [&] (const Vec3& target, float near) {
          Vec3 toward = target - feet;
          toward[1] = 0.0f;
          const float distance = length (toward);
          if (distance <= near)
            return true;
          const Vec3 heading = walker.heading ();
          const Vec3 left (heading[2], 0.0f, -heading[0]);
          const float leftward = dot (toward, left) / distance;
          const float ahead = dot (toward, heading) / distance;
          const float turn = ahead < 0.0f ? (leftward >= 0.0f ? -1.0f : 1.0f)
                                          : -std::asin (leftward);
          input.look_yaw = std::clamp (turn, -2.2f * dt, 2.2f * dt);
          input.drive =
            ahead > 0.8f ? std::min (0.8f, 0.3f + 0.3f * distance) : 0.0f;
          return false;
        };
        Vec3 middle = hammock_middle (site);
        middle[1] = ground_height (middle);
        const Vec3 fireside = middle +
                              hammock_across (site) * (script.side * 3.6f) -
                              hammock_along (site) * 0.6f;
        // The walker's gaze levels out as they go.
        input.look_pitch =
          std::clamp (-0.12f - logic ().m_look_pitch, -1.0f * dt, 1.0f * dt);
        switch (script.step) {
        case 0:
          if (walk_to (middle, 0.45f))
            next ();
          break;
        case 1:
          if (waited > 0.7f) {
            input.hang_hammock = true;
            next ();
          }
          break;
        case 2:
          if (waited > 1.6f)
            next ();
          break;
        case 3:
          // The fire is laid a step ahead of where the walker stops.
          if (walk_to (fireside, 1.35f))
            next ();
          break;
        case 4:
          if (waited > 0.5f) {
            input.light_fire = true;
            next ();
          }
          break;
        case 5:
          if (waited > 4.0f)
            next ();
          break;
        case 6:
          if (walk_to (middle, 1.0f))
            next ();
          break;
        case 7:
          // E picks a mushroom first if one is at hand; press until lying.
          if (logic ().m_camp.resting)
            next ();
          else if (std::fmod (waited, 0.9f) < dt + 1e-4f && waited > 0.5f)
            input.deploy_glider = true;
          break;
        default:
          // Lying there: after a look from outside, through their eyes,
          // gaze wandering a little over the sky.
          if (script.step == 8 && waited > 5.0f &&
              logic ().m_cam_mode == CAM_CHASE) {
            logic ().m_cam_mode = CAM_HELMET;
            m_renderer->reset_temporal_state ();
            next ();
          }
          input.look_pitch = 0.0f;
          input.look_yaw = 0.10f * std::sin (waited * 0.21f) * dt;
          input.turn = 0.25f * std::sin (waited * 0.9f);
          break;
        }
        return input;
      }

      // The opening is the authored shot list (data/opening.txt) when it
      // was composed in this world, and otherwise one generated still of
      // the trailhead; either ends in the player's eyes.  MOPPE_OPENING
      // names another shot list, `generated` for the still, `flight` for
      // the old drone flight through the planned landmarks, which the
      // terrain survey still samples, or `none` to begin in play.
      void plan_opening_journey () {
        MOPPE_PROFILE_ZONE ("startup.plan_opening");
        m_opening_shots.clear ();
        if (m_spectator)
          return;
        const char* choice = moppe::environment ("MOPPE_OPENING");
        if (choice && std::string_view (choice) == "none")
          return;
        const bool flight =
          (choice && std::string_view (choice) == "flight") ||
          moppe::environment ("MOPPE_CINEMATIC_CAPTURE_PROGRESS");
        if (!flight) {
          plan_opening_reel (choice);
          return;
        }
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

      float ground_height (const Vec3& at) const {
        return terrain::surface_elevation_value (
          spatial::sample<terrain::surface_elevation> (
            surface (), moppe::position (Vec3 (at[0], 0.0f, at[2]))));
      }

      // Reads the authored shots, keeping them only if they were composed
      // in this world and every camera stands above its ground.
      std::optional<OpeningReel> authored_opening (const char* choice) const {
        if (choice && std::string_view (choice) == "generated")
          return std::nullopt;
        const std::string path = choice
                                   ? std::string (choice)
                                   : platform::asset_path ("data/opening.txt");
        std::ifstream input (path);
        if (!input) {
          std::cerr << "moppe: opening: no shot list at " << path << '\n';
          return std::nullopt;
        }
        std::string error;
        std::optional<OpeningReel> reel = parse_opening_reel (input, error);
        if (!reel) {
          std::cerr << "moppe: opening: " << path << ": " << error << '\n';
          return std::nullopt;
        }
        if (!reel->matches (
              static_cast<int> (recipe ().seed ().value),
              recipe ().resolution (),
              terrain::profile_id (recipe ().generation_profile ()))) {
          std::cerr << "moppe: opening: " << path
                    << " was composed for another world\n";
          return std::nullopt;
        }
        for (const OpeningShot& shot : reel->shots) {
          if (shot.eye[1] < ground_height (shot.eye) + 0.2f) {
            std::cerr << "moppe: opening: shot " << shot.name
                      << " stands underground in this world\n";
            return std::nullopt;
          }
          if (shot.sun && std::fabs (*shot.sun - m_sky.sun_height) > 0.01f)
            std::cerr << "moppe: opening: shot " << shot.name
                      << " was composed under sun " << *shot.sun
                      << ", the world is lit at " << m_sky.sun_height << '\n';
        }
        return reel;
      }

      // Any world's opening: one long still looking back at the trailhead
      // from up the trail, with the titles over it.
      std::vector<OpeningShot> generated_opening () const {
        const Vec3 along = trail_direction_from_home ();
        Vec3 eye = m_home_base_position + along * 20.0f;
        eye[1] = ground_height (eye) + 3.6f;
        Vec3 look = m_spawn_position - eye;
        look[1] = 0.0f;
        OpeningShot still;
        still.name = "trailhead";
        still.eye = eye;
        still.heading_deg = heading_degrees (look);
        still.pitch_deg = -3.0f;
        still.fov_deg = 46.0f;
        still.hold = 11.0f;
        still.push = 3.0f;
        still.fade = 2.5f;
        still.captions.push_back (
          { OpeningCaption::Style::Title, "moppe", 1.6f, 4.4f, 0.28f });
        still.captions.push_back ({ OpeningCaption::Style::Credit,
                                    "a game by Mikael Brockman",
                                    6.2f,
                                    3.8f });
        return { still };
      }

      void plan_opening_reel (const char* choice) {
        const std::optional<OpeningReel> reel = authored_opening (choice);
        m_opening_shots = reel ? reel->shots : generated_opening ();
        const OpeningArrival arrival =
          reel && reel->arrival ? *reel->arrival : OpeningArrival {};
        Vec3 eye = session ().subject_position () + Vec3 (0, 1.6f, 0);
        if (logic ().m_mode == M_FOOT)
          eye = session ().walker ().eye_position ();
        m_opening_shots.push_back (
          arrival_shot (arrival, eye, subject_heading (), 70.0f));
        std::cerr << "moppe: opening: the player starts at "
                  << format_opening_shot ("start",
                                          eye,
                                          subject_heading (),
                                          70.0f,
                                          logic ().m_total_time,
                                          m_sky.sun_height)
                  << '\n';
        float duration = 0.0f;
        for (const OpeningShot& shot : m_opening_shots)
          duration += shot.hold;
        std::cerr << "moppe: opening: " << m_opening_shots.size ()
                  << (reel ? " authored" : " generated") << " shots, "
                  << duration << " s\n";
      }

      void start_opening () {
        if (!m_cinematic_plan.empty ()) {
          m_cinematic.start (m_cinematic_plan, surface ());
          return;
        }
        if (m_opening_shots.empty ())
          return;
        m_opening.start (m_opening_shots);
        m_opening_shot_seen = SIZE_MAX;
        m_opening_rendered_shot = SIZE_MAX;
        follow_opening_clock ();
      }

      // Each shot may set the world's clock at its cut, choosing its sky.
      void follow_opening_clock () {
        if (!m_opening.active () ||
            m_opening.shot_index () == m_opening_shot_seen)
          return;
        m_opening_shot_seen = m_opening.shot_index ();
        if (const std::optional<float> clock = m_opening.shot ().clock) {
          logic ().m_total_time = *clock + m_opening.shot_time ();
          update_world_atmosphere (logic ().m_total_time);
        }
      }

      bool opening_active () const noexcept {
        return m_cinematic.active () || m_opening.active ();
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
                                    m_sky.sun);
        if (m_gazetteer_plan.empty ())
          throw std::runtime_error ("landscape gazetteer found no viewpoints");
        // The direct observation primitive: put the camera HERE, look
        // THERE, one settled frame. A question about composition deserves
        // a frame composed for that question, never the nearest postcard.
        if (const char* look = moppe::environment ("MOPPE_LOOK")) {
          float ex, ey, ez, sx, sy, sz, fov = 55.0f;
          const int parsed = std::sscanf (
            look, "%f %f %f %f %f %f %f", &ex, &ey, &ez, &sx, &sy, &sz, &fov);
          if (parsed < 6)
            throw std::runtime_error (
              "MOPPE_LOOK wants: eye_x eye_y eye_z subj_x subj_y subj_z "
              "[fov_deg]");
          GazetteerShot shot;
          shot.name = "look";
          shot.eye = position (Vec3 (ex, ey, ez));
          shot.subject = position (Vec3 (sx, sy, sz));
          shot.vertical_field_of_view = fov * u::deg;
          m_gazetteer_plan.shots.assign (1, shot);
        }
        // A glide is the isolated moving-camera instrument: one named
        // shot's camera advancing in a straight line, one capture per
        // rendered frame, no vehicle, no HUD, no particles, frozen wind.
        // Measurement wants exactly one subject in the frame.
        if (const char* glide = moppe::environment ("MOPPE_GLIDE")) {
          auto& shots = m_gazetteer_plan.shots;
          const auto found = std::find_if (
            shots.begin (), shots.end (), [&] (const GazetteerShot& shot) {
              return shot.name == glide;
            });
          if (found == std::end (shots))
            throw std::runtime_error (
              std::string ("MOPPE_GLIDE names no gazetteer shot: ") + glide);
          const GazetteerShot only = *found;
          shots.assign (1, only);
        }
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
        const std::filesystem::path elevation_path =
          std::filesystem::path (m_gazetteer->output_directory) /
          "terrain-elevation.f32";
        std::ofstream elevation_output (elevation_path, std::ios::binary);
        if (!elevation_output)
          throw std::runtime_error ("cannot write landscape elevation: " +
                                    elevation_path.string ());
        write_landscape_elevation_f32 (elevation_output, surface ());
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
        scatter_boulders ();
        grow_mushrooms ();
        settle_obstacles ();
        stage_camp_script ();
        place_spectator ();
        // The mouse looks around during play; M hands it back.
        if (!m_gazetteer && !m_benchmark)
          platform::set_pointer_captured (true);
        if (m_gazetteer)
          plan_gazetteer_capture ();
        else
          plan_opening_journey ();
        remember_seed (world (),
                       recipe ().generation_profile (),
                       static_cast<int> (recipe ().seed ().value));
        if (moppe::environment ("MOPPE_REGENERATE_ONCE") &&
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
        std::cerr << "moppe: world ready " << m_loading.status ().elapsed
                  << " s after loading began" << std::endl;

        const bool automated =
          !m_screenshot_path.empty () || m_benchmark.has_value () ||
          m_water_shot.has_value () || m_gazetteer.has_value () ||
          moppe::environment ("MOPPE_DEMO") ||
          moppe::environment ("MOPPE_WALK");
        if (!automated && !m_skip_cinematic_requested) {
          start_opening ();
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
        m_terrain.render_shadow (r, m_sky.sun, m_graphics.forest);
      }

      void update_world_atmosphere (float total_time) {
        // Weather remains part of the world while actors are paused.
        cloud_cover_t cloudiness =
          (std::sin (total_time * 0.0003f) * 0.4f + 0.5f +
           0.3f * std::pow (std::sin (total_time * 0.0008f), 2.0f) +
           std::sin (total_time * 0.02f) * 0.05f) *
          cloud_cover[one];
        const Weather& weather = current_weather ();
        cloudiness = std::clamp (cloudiness,
                                 weather.cloud_floor * cloud_cover[one],
                                 1.0f * cloud_cover[one]);
        logic ().m_cloudiness = cloudiness;

        // Fog stays mostly sky-blue. Directional warmth is added in the
        // shaders only when looking toward the sun.
        read_sky ();
        // By night the haze keeps the sky's own dark blue, and an overcast
        // greys only as far as there is light to grey it.
        const float lit = 0.12f + 0.88f * m_sky.daylight;
        logic ().m_fog = mix_display (
          mix_display (m_sky.horizon,
                       scale_display (DisplayColor (0.90f, 0.94f, 1.0f), lit),
                       0.18f),
          scale_display (weather.fog_tint, lit),
          weather.fog_tint_amount);
      }

      // The day: MOPPE_DAY is how many minutes of play one takes (48
      // unless told; 0 holds the sun still), and MOPPE_CLOCK the hour it
      // starts at. Composed views -- the gazetteer, benchmarks, water
      // captures, and any launch that names a sun height without asking
      // for a day -- keep the fixed sun they were made under.
      struct Day {
        // Hours of the clock per second of play.
        double pace = 0.0;
        // Whether the sky follows the clock, or holds the authored sun.
        bool turning = false;
        std::optional<float> first_hour;
      };

      Day plan_day () const {
        Day day;
        const char* minutes = moppe::environment ("MOPPE_DAY");
        const char* hour = moppe::environment ("MOPPE_CLOCK");
        if (hour)
          day.first_hour = std::clamp ((float)::atof (hour), 0.0f, 24.0f);
        const bool composed =
          m_gazetteer || m_benchmark || m_water_shot ||
          (moppe::environment ("MOPPE_SUNHEIGHT") && !minutes && !hour);
        const float length = minutes ? (float)::atof (minutes) : 48.0f;
        if (!composed && length > 0.0f)
          day.pace = 24.0 / (60.0 * length);
        day.turning = !composed && (day.pace > 0.0 || hour);
        return day;
      }

      void begin_day () {
        logic ().m_day_hours =
          m_day.first_hour
            ? *m_day.first_hour
            : m_almanac.clock_for_sun_height (m_graphics.sun_height);
        read_sky ();
        if (m_day.turning)
          std::cerr << "moppe: the day begins at " << m_sky.clock
                    << " h and takes "
                    << (m_day.pace > 0.0 ? 24.0 / (60.0 * m_day.pace) : 0.0)
                    << " minutes" << std::endl;
      }

      void read_sky () {
        m_sky = m_day.turning && m_session
                  ? m_almanac.at (logic ().m_day_hours)
                  : m_almanac.held (m_graphics.sun_height);
      }

      // The clock runs with play, and faster for someone lying down to
      // watch it.
      void pass_time (float dt) {
        logic ().m_day_hours += m_day.pace * dt * logic ().m_day_haste;
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
          (opening_active () &&
           moppe::environment ("MOPPE_CINEMATIC_CAPTURE_DIR"));
        if (frame_locked) {
          m_simulation_clock.reset ();
          tick_simulation (elapsed);
          return;
        }
        // Orbit inspections and ride captures advance exactly one 60 Hz frame
        // of world time per rendered frame, so captured motion is even
        // however slowly the frames are written.
        static const bool capture_locked =
          moppe::environment ("MOPPE_ORBIT") ||
          moppe::environment ("MOPPE_PAN") ||
          moppe::environment ("MOPPE_RIDE_CAPTURE_DIR") ||
          moppe::environment ("MOPPE_VIDEO");
        if (capture_locked) {
          m_simulation_clock.reset ();
          tick_simulation (1.0f / 60.0f);
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

      // A deterministic on-foot script for automated captures (MOPPE_WALK):
      // "walk", "run", and "jump" hold one gait, "tour" stands, walks,
      // runs, jumps twice from the run, and comes to rest, turning gently
      // throughout so a following camera sees the figure from changing
      // sides, and "forage" starts beside the mushrooms nearest the spawn
      // and picks its way from one to the next.
      InputFrame scripted_walk (std::string_view script, float dt) {
        const float t = m_walk_script_time;
        m_walk_script_time += dt;
        InputFrame input;
        const auto press = [t, dt] (float at) {
          return t >= at && t < at + 0.1f + dt ? 1.0f : 0.0f;
        };
        if (script == "camp")
          return camp_script (dt);
        if (script == "forage") {
          // Walk to the nearest mushroom, turning toward it, and pick it
          // once within reach; poisonous ones are left after one try.
          const Walker& walker = session ().walker ();
          const Vec3 feet = walker.position ();
          const auto target =
            m_mushrooms.nearest (feet, 60.0f, m_forage_refused);
          if (target) {
            Vec3 toward = m_mushrooms.site (*target).base - feet;
            toward[1] = 0.0f;
            const float distance = length (toward);
            const Vec3 heading = walker.heading ();
            const Vec3 left (heading[2], 0.0f, -heading[0]);
            const float leftward =
              dot (toward, left) / std::max (distance, 1e-3f);
            const float ahead =
              dot (toward, heading) / std::max (distance, 1e-3f);
            // Look yaw turns to the right.
            const float turn = ahead < 0.0f
                                 ? (leftward >= 0.0f ? -1.0f : 1.0f)
                                 : -std::asin (leftward);
            input.look_yaw = std::clamp (turn, -2.5f * dt, 2.5f * dt);
            input.look_pitch = std::clamp (-0.6f - logic ().m_look_pitch,
                                           -1.0f * dt,
                                           1.0f * dt);
            const bool reachable =
              m_mushrooms.within_reach (feet, heading) == target;
            input.drive = !reachable && ahead > 0.7f
                            ? std::min (1.0f, 0.3f + 0.3f * distance)
                            : 0.0f;
            const float beat = std::fmod (t, 0.9f);
            if (reachable && beat < dt + 1e-4f) {
              input.deploy_glider = true;
              if (!mushroom_edible (m_mushrooms.site (*target).kind))
                m_forage_refused.push_back (*target);
            }
          }
          return input;
        }
        if (script == "walk") {
          input.drive = 1.0f;
        } else if (script == "run") {
          input.drive = 1.0f;
          input.run = true;
        } else if (script == "jump") {
          input.drive = 1.0f;
          input.run = true;
          // A jump every 1.6 s, each a fresh press.
          const float beat = std::fmod (t, 1.6f);
          input.boost = beat >= 1.0f && beat < 1.1f ? 1.0f : 0.0f;
        } else {
          input.drive = t > 1.5f && t < 12.5f ? 1.0f : 0.0f;
          input.run = t > 5.0f && t < 11.0f;
          input.boost = std::max (press (8.0f), press (9.6f));
        }
        input.look_yaw = 0.25f * std::sin (t * 0.35f) * dt;
        return input;
      }

      // MOPPE_WALK_CAMERA=side: a camera locked beside the walking figure,
      // so the gait can be judged against the ground passing beneath it.
      void walk_side_camera () {
        static const bool side = [] {
          const char* view = moppe::environment ("MOPPE_WALK_CAMERA");
          return view && std::string_view (view) == "side";
        }();
        if (!side || logic ().m_mode != M_FOOT)
          return;
        const Walker& walker = session ().walker ();
        const Vec3 heading = walker.heading ();
        const Vec3 right (heading[2], 0.0f, -heading[0]);
        const Vec3 at = walker.position () + Vec3 (0, 0.9f, 0);
        session ().camera ().place (at + right * 4.0f + Vec3 (0, 0.25f, 0), at);
      }

      // MOPPE_RIDE_CAMERA=side|front: a camera locked beside or ahead of the
      // ridden bike or the glider, to judge the rider and the machine.
      void ride_capture_camera () {
        static const std::string_view view = [] {
          const char* name = moppe::environment ("MOPPE_RIDE_CAMERA");
          return std::string_view (name ? name : "");
        }();
        if (view.empty () || logic ().m_mode == M_FOOT)
          return;
        const bool gliding = logic ().m_mode == M_GLIDER;
        const auto& bike = session ().bike ();
        Vec3 heading = gliding ? session ().glider ().heading ()
                               : bike.render_orientation ();
        heading[1] = 0.0f;
        heading = normalized (heading);
        const Vec3 right (heading[2], 0.0f, -heading[0]);
        const Vec3 at = gliding
                          ? session ().glider ().position () - Vec3 (0, 0.9f, 0)
                          : bike.render_position () + Vec3 (0, 0.4f, 0);
        const float away = gliding ? 7.0f : 4.5f;
        const Vec3 from = view == "front"
                            ? heading * away + right * (away * 0.27f)
                            : right * away + heading * 0.6f;
        session ().camera ().place (at + from + Vec3 (0, 0.5f, 0), at);
      }

      void tick_simulation (float dt) {
        MOPPE_PROFILE_ZONE ("MoppeGame::tick_simulation");
        std::optional<InputFrame> scripted_input;
        if (opening_active () &&
            moppe::environment ("MOPPE_CINEMATIC_CAPTURE_DIR")) {
          const int fps = [] {
            if (const char* value =
                  moppe::environment ("MOPPE_CINEMATIC_CAPTURE_FPS"))
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

        InputFrame input = m_live_input.take_frame (dt);
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
        if (m_opening.active ()) {
          if (input.leave_cinematic) {
            leave_cinematic ();
            input = {};
          } else {
            m_opening.tick (dt);
            follow_opening_clock ();
            if (!m_opening.active ()) {
              leave_cinematic ();
              // A capture of the opening is done when it is.
              if (moppe::environment ("MOPPE_CINEMATIC_CAPTURE_DIR"))
                platform::request_quit ();
            }
            update_frame_flare ();
            return;
          }
        }

        // Screenshot autopilot for headless verification: rides in a
        // lazy arc with periodic boost-assisted leaps.
        static const bool demo = moppe::environment ("MOPPE_DEMO") != 0;
        static const bool demo_glides = [] {
          const char* name = moppe::environment ("MOPPE_DEMO");
          return name && std::string_view (name) == "glide";
        }();
        m_trunk_field.focus (session ().subject_position ());
        static const bool orbit = moppe::environment ("MOPPE_ORBIT") != 0 ||
                                  moppe::environment ("MOPPE_PAN") != 0;
        if (m_spectator) {
          // The mouse turns the spectator directly; the right stick's look
          // arrives with the frame.
          m_spectator->yaw += input.look_yaw;
          m_spectator->pitch = std::clamp (
            m_spectator->pitch + input.look_pitch, -1.45f, 1.45f);
          input = {};
        }
        static const char* walk_script = moppe::environment ("MOPPE_WALK");
        if (walk_script && logic ().m_mode == M_FOOT && !orbit)
          input = scripted_walk (walk_script, dt);
        else if (demo && !m_water_inspection && !orbit) {
          // The autopilot rides; the player may still look around.
          const float look_yaw = input.look_yaw;
          const float look_pitch = input.look_pitch;
          input = {
            .turn = 0.35f * std::sin (total_time * 0.25f),
            .drive = 1.0f,
            .boost =
              std::fmod (total_time, 11.0f) < (demo_glides ? 3.5f : 1.35f)
                ? 1.0f
                : 0.0f,
            // MOPPE_DEMO=glide opens the wing on the first leap high
            // enough to allow it.
            .deploy_glider_held = demo_glides,
            .look_yaw = look_yaw,
            .look_pitch = look_pitch,
          };
          // Look a few metres ahead; where a trunk stands in the way, steer
          // hard toward the side its contact pushes, so a ride through the
          // woods weaves between trees instead of stopping at the first.
          const Vec3 at = session ().subject_position ();
          const Vec3 heading = session ().subject_heading ();
          const Vec3 right = normalized (cross (heading, Vec3 (0, 1, 0)));
          for (const float ahead : { 4.0f, 9.0f }) {
            const Vec3 probe = at + heading * ahead;
            const mov::TrunkContact contact =
              m_trunk_field.collide (probe, probe + Vec3 (0, 1.5f, 0), 1.8f);
            if (contact.hit) {
              input.turn = dot (contact.normal, right) >= 0.0f ? 1.0f : -1.0f;
              break;
            }
          }
        }

        pass_time (dt);
        tend_camp (input, dt);
        reach_for_mushroom (input);
        const GameSessionAdvanceResult advance =
          advance_game_session (world (),
                                surface (),
                                session (),
                                input,
                                seconds (dt),
                                &m_trunk_field,
                                ground_cover (session ().subject_position ()));
        if (advance.say_ouchies)
          platform::say ("Ouchies. That hurts.");

        if (m_water_inspection) {
          session ().camera ().place (m_water_inspection->eye,
                                      m_water_inspection->target);
          session ().camera ().limit (surface ());
        }
        orbit_camera ();
        spectator_camera (dt);
        rest_camera ();
        walk_side_camera ();
        ride_capture_camera ();

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
        params.mist = frame.lighting.mist;
        params.rain = frame.lighting.rain;
        params.sun_visibility = frame.lighting.sun_visibility;
        params.sky_sun_dir = frame.lighting.sky_sun;
        params.sky_moon_dir = frame.lighting.sky_moon;
        params.sky_pole = frame.lighting.sky_pole;
        params.sky_turn = frame.lighting.sky_turn;
        params.moonlight = frame.lighting.moonlight;
        params.lamp_pos = frame.lighting.lamp_position;
        params.lamp_reach = frame.lighting.lamp_reach;
        params.lamp_color = frame.lighting.lamp_color;
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
        state.can_mount = reading.can_mount;
        state.vertical_speed_mps = reading.vertical_speed_mps;
        state.frame_time_s = reading.frame_time_s;
        state.heading_radians = reading.heading_radians;
        return state;
      }

      // The mushroom at hand and the basket, for the prompts and the tally.
      void add_basket_readings (HudState& state) const {
        const Basket& basket = logic ().m_basket;
        state.basket_total = basket.total ();
        state.reached_name = mushroom_name (basket.last_kind);
        state.reached_age_s =
          static_cast<float> (logic ().m_total_time - basket.last_reach_time);
        state.reached_refused = basket.last_refused;
        if (logic ().m_mode != M_FOOT)
          return;
        if (const std::optional<std::uint32_t> found =
              m_mushrooms.within_reach (session ().walker ().position (),
                                        session ().walker ().heading ())) {
          const MushroomKind kind = m_mushrooms.site (*found).kind;
          state.mushroom_in_reach = mushroom_name (kind);
          state.mushroom_edible = mushroom_edible (kind);
        }
      }

      void draw_world_layers (render::Renderer& r, const FrameView& frame) {
        const FrameVisibility& visibility = frame.visibility;
        const Vec3& camera = frame.camera.position;
        if (m_graphics.terrain_shadows)
          m_terrain.render_local_shadow (r,
                                         position (camera),
                                         frame.camera.frame_forward,
                                         frame.lighting.sun_direction,
                                         m_graphics.forest,
                                         frame.visibility.boulders,
                                         m_graphics.distant_shadows);
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
        m_terrain.render (r,
                          camera,
                          frame.camera.projection * frame.camera.view,
                          frame.terrain_distance);

        // Sky AFTER the terrain: depth testing kills the expensive
        // cloud shader wherever terrain covers it.
        if (visibility.sky_after_terrain)
          draw_world_sky ();

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
            case M_GLIDER:
              break;
            }
          }
          r.draw_undergrowth (
            { .time = frame.lighting.time,
              .cloud_cover = frame.lighting.cloudiness.numerical_value_in (one),
              .reach = 64.0f,
              .density = m_graphics.grass_cover_boost,
              .interaction_position = frame.hud.subject_position,
              .interaction_radius = interaction_radius });
        }

        // Opaque individuals depth-test normally. The distant stand quotient
        // follows the ground medium so its non-depth-writing canopy roof
        // cannot be painted over by the sward's own far density layer.
        if (visibility.boulders)
          m_boulders.draw (r);
        if (visibility.mushrooms) {
          m_mushrooms.follow (logic ().m_basket);
          m_mushrooms.draw (r, camera);
        }
        if (visibility.forest)
          m_forest.draw (r);
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
        if (!m_spectator)
          m_blob.draw (m_world_dl, surface (), actors.bike.position, 2.2f);
        if (actors.walker)
          m_blob.draw (m_world_dl,
                       surface (),
                       actors.walker->position + Vec3 (0, 0.5f, 0),
                       0.8f);
        if (actors.glider)
          m_blob.draw (m_world_dl, surface (), actors.glider->position, 3.4f);

        // In helmet cam you ARE the rider: don't draw yourself.
        const bool helmet = actors.helmet_camera;
        if (!(helmet && actors.active_mode == M_BIKE) && !m_spectator)
          render_vehicle (
            r, m_world_dl, actors.bike, actors.active_mode == M_BIKE, 0x1000);
        const Basket& basket = logic ().m_basket;
        m_basket_contents.clear ();
        for (const std::uint32_t index : basket.picked)
          m_basket_contents.push_back (m_mushrooms.site (index));
        draw_camp (m_world_dl);
        if (actors.walker && lying_down ())
          draw_sleeper (m_world_dl, helmet, frame.lighting.time);
        else if (actors.walker && !helmet)
          render_walker (m_world_dl,
                         *actors.walker,
                         frame.lighting.time,
                         m_basket_contents);
        else if (actors.walker)
          render_basket (m_world_dl,
                         *actors.walker,
                         frame.lighting.time,
                         m_basket_contents);
        // The mushroom just picked rises from the moss into the basket.
        if (actors.walker && !basket.picked.empty () &&
            !basket.last_refused) {
          const float age = static_cast<float> (logic ().m_total_time -
                                                basket.last_reach_time);
          if (age >= 0.0f && age < 0.45f) {
            const float t = age / 0.45f;
            MushroomSite rising = m_mushrooms.site (basket.picked.back ());
            const Vec3 to = actors.walker->position +
                            normalized (actors.walker->heading) * 0.2f +
                            Vec3 (0, 0.55f, 0);
            rising.base = rising.base + (to - rising.base) * t +
                          Vec3 (0, 0.35f * std::sin (PI * t), 0);
            rising.scale *= 1.0f - 0.5f * t;
            rising.yaw += 3.0f * t;
            m_world_dl.state (render::DrawState ());
            m_world_dl.lit (true);
            m_world_dl.fogged (true);
            m_world_dl.begin (render::Prim::Triangles);
            draw_mushroom (m_world_dl, rising);
            m_world_dl.end ();
          }
        }
        if (actors.glider && !helmet)
          render_glider (
            r, m_world_dl, *actors.glider, frame.lighting.time, 0x2000);

        r.draw_list (m_world_dl, 0x0001);

        // Additive glow after the solid list, so it blends over everything
        // already drawn: exhaust and jump-jet flames, then star halos.
        if (visibility.vehicle_effects && !m_spectator &&
            !(helmet && actors.active_mode == M_BIKE))
          render_vehicle_flames (r,
                                 actors.bike,
                                 frame.camera.position,
                                 frame.lighting.time,
                                 0x1000);
        if (logic ().m_camp.fire_time > -50.0)
          draw_fire (r,
                     logic ().m_camp.hearth,
                     frame.camera.position,
                     fire_burn (logic ().m_camp, logic ().m_total_time),
                     frame.lighting.time);
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
        m_hud_text.clear ();
        const platform::Insets safe_insets = platform::safe_insets ();
        m_hud_dl.translate (safe_insets.left, safe_insets.top, 0);
        const int hud_width =
          r.width_pts () - (int)(safe_insets.left + safe_insets.right);
        const int hud_height =
          r.height_pts () - (int)(safe_insets.top + safe_insets.bottom);
        if (visibility.cinematic_hud) {
          draw_opening_titles (hud_width, hud_height);
          m_hud.draw_ride_prompt (m_hud_text,
                                  frame.overlay.cinematic_prompt_alpha,
                                  hud_width,
                                  hud_height);
        } else if (visibility.game_hud) {
          HudState hud_state = hud_state_for (frame.hud);
          add_basket_readings (hud_state);
          add_camp_readings (hud_state);
          m_hud.draw (m_hud_dl, m_hud_text, hud_state, hud_width, hud_height);
          if (m_hud.diagnostics ())
            draw_trail_map (m_hud_dl,
                            hud_width,
                            hud_height,
                            frame.hud.subject_position,
                            frame.hud.subject_heading);
        }
        m_hud_text.translate (safe_insets.left, safe_insets.top);

        // Even a clean inspection capture needs this empty HUD pass: it is
        // also the final post-chain composite into the drawable.
        r.draw_hud_text (m_hud_text);
        r.draw_hud (m_hud_dl);
      }

      // The opening's fades are a black veil over the frame, and its
      // captions are set over that.
      void draw_opening_titles (int width_pts, int height_pts) {
        if (!m_opening.active ())
          return;
        if (const float veil = m_opening.veil (); veil > 0.002f) {
          render::DrawState over;
          over.blend = true;
          over.depth_test = false;
          over.depth_write = false;
          over.cull = false;
          m_hud_dl.state (over);
          m_hud_dl.lit (false);
          m_hud_dl.fogged (false);
          m_hud_dl.color (0.0f, 0.0f, 0.0f, veil);
          m_hud_dl.begin (render::Prim::Quads);
          m_hud_dl.vertex (-64.0f, -64.0f);
          m_hud_dl.vertex (width_pts + 64.0f, -64.0f);
          m_hud_dl.vertex (width_pts + 64.0f, height_pts + 64.0f);
          m_hud_dl.vertex (-64.0f, height_pts + 64.0f);
          m_hud_dl.end ();
          m_hud_dl.state (render::DrawState ());
        }
        for (const OpeningCaptionView& caption : m_opening.captions ())
          m_hud.draw_title_card (m_hud_text,
                                 caption.style == OpeningCaption::Style::Title,
                                 *caption.text,
                                 caption.alpha,
                                 caption.y,
                                 width_pts,
                                 height_pts);
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
        // A cut is a new view: the temporal history of the last one would
        // only smear into it.
        if (m_opening.active () &&
            m_opening.shot_index () != m_opening_rendered_shot) {
          if (m_opening_rendered_shot != SIZE_MAX)
            r.reset_temporal_state ();
          m_opening_rendered_shot = m_opening.shot_index ();
        }

        static const int screenshot_delay = [] {
          if (const char* frames =
                moppe::environment ("MOPPE_SCREENSHOT_FRAMES"))
            return std::max (1, ::atoi (frames));
          return 30;
        }();
        const bool captured = !m_screenshot_path.empty () &&
                              ++m_screenshot_frames >= screenshot_delay;
        bool captured_cinematic = false;
        if (cinematic) {
          if (const char* directory =
                moppe::environment ("MOPPE_CINEMATIC_CAPTURE_DIR")) {
            const int capture_count = cinematic_capture_frame_limit ();
            const bool survey =
              m_cinematic.active () &&
              moppe::environment ("MOPPE_CINEMATIC_CAPTURE_PROGRESS");
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
          if (glide_frame_limit () > 0) {
            std::ostringstream name;
            name << "glide-" << std::setfill ('0') << std::setw (4)
                 << m_glide_frame << ".png";
            const std::filesystem::path path =
              std::filesystem::path (m_gazetteer->output_directory) /
              name.str ();
            r.request_screenshot (path.string ());
          } else {
            const std::filesystem::path path =
              std::filesystem::path (m_gazetteer->output_directory) /
              gazetteer_image_filename (m_gazetteer_shot, gazetteer_shot->name);
            r.request_screenshot (path.string ());
          }
        }
        if (m_snapshot_requested) {
          m_snapshot_requested = false;
          const std::string path = next_snapshot_path ();
          r.request_screenshot (path);
          record_shot_pose (frame, path);
        }
        // A ride capture records CONSECUTIVE gameplay frames: the stimulus a
        // rider actually receives, per-frame LOD transitions included --
        // exactly what settled single captures can never show. Pair with
        // MOPPE_DEMO=1 for a deterministic autopilot ride.
        if (!cinematic && m_ready) {
          static const char* ride_directory =
            moppe::environment ("MOPPE_RIDE_CAPTURE_DIR");
          static const int ride_start = [] {
            const char* start = moppe::environment ("MOPPE_RIDE_CAPTURE_START");
            return start ? std::max (0, ::atoi (start)) : 600;
          }();
          static const int ride_count = [] {
            const char* count =
              moppe::environment ("MOPPE_RIDE_CAPTURE_FRAMES");
            return count ? std::max (1, ::atoi (count)) : 90;
          }();
          if (ride_directory) {
            const int frame_index = m_ride_capture_render_frame++;
            if (frame_index >= ride_start && frame_index % 2 == 0 &&
                m_ride_capture_frame < ride_count) {
              if (m_ride_capture_frame == 0)
                std::filesystem::create_directories (ride_directory);
              std::ostringstream path;
              path << ride_directory << "/ride-" << std::setfill ('0')
                   << std::setw (4) << m_ride_capture_frame++ << ".png";
              r.request_screenshot (path.str ());
            }
            if (m_ride_capture_frame >= ride_count)
              platform::request_quit ();
          }
        }
        record_frame (r);
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
          } else if (glide_frame_limit () > 0) {
            if (++m_glide_frame >= glide_frame_limit ())
              platform::request_quit ();
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
        m_hud_text.clear ();
        render::DrawState state;
        state.blend = true;
        state.depth_test = false;
        state.depth_write = false;
        state.cull = false;
        m_hud_dl.state (state);
        m_hud_dl.lit (false);
        m_hud_dl.fogged (false);

        // No panel: a soft dusk rises from the bottom edge behind a few
        // lines of type, and the sky stays the picture.
        if (m_hud.font ()) {
          const float scrim = std::min (height, 300.0f);
          m_hud_dl.begin (render::Prim::Quads);
          m_hud_dl.color (0.0f, 0.02f, 0.03f, 0.0f);
          m_hud_dl.vertex (0.0f, height - scrim);
          m_hud_dl.vertex (width, height - scrim);
          m_hud_dl.color (0.0f, 0.02f, 0.03f, 0.42f);
          m_hud_dl.vertex (width, height);
          m_hud_dl.vertex (0.0f, height);
          m_hud_dl.end ();

          const float left = 44.0f;
          const float content_width = std::min (520.0f, width - 2.0f * left);
          const float bottom = height - 44.0f;
          const auto style = [] (float size, float alpha, float tracking) {
            render::TextStyle s;
            s.size = size;
            s.red = s.green = s.blue = 0.96f;
            s.alpha = alpha;
            s.tracking = tracking;
            s.tabular_figures = true;
            return s;
          };

          std::ostringstream eyebrow;
          eyebrow << "SEED " << loading.seed;
          m_hud.draw_shaded (m_hud_text,
                             left,
                             bottom - 118.0f,
                             eyebrow.str (),
                             style (9.0f, 0.62f, 0.16f));
          m_hud.draw_shaded (m_hud_text,
                             left,
                             bottom - 86.0f,
                             loading.title,
                             style (27.0f, 0.96f, 0.0f));
          m_hud.draw_shaded (m_hud_text,
                             left,
                             bottom - 62.0f,
                             loading.detail,
                             style (14.0f, 0.74f, 0.01f));

          // The rail fills only with a real measurement; stages that cannot
          // measure themselves show their text and nothing else.
          const auto fill_rect = [this] (float x, float y, float w, float h) {
            m_hud_dl.begin (render::Prim::Quads);
            m_hud_dl.vertex (x, y);
            m_hud_dl.vertex (x + w, y);
            m_hud_dl.vertex (x + w, y + h);
            m_hud_dl.vertex (x, y + h);
            m_hud_dl.end ();
          };
          const float rail_y = bottom - 44.0f;
          m_hud_dl.color (0.96f, 0.96f, 0.96f, 0.16f);
          fill_rect (left, rail_y, content_width, 1.0f);
          if (loading.progress >= 0.0f) {
            m_hud_dl.color (0.96f, 0.96f, 0.96f, 0.78f);
            fill_rect (left,
                       rail_y,
                       content_width *
                         std::clamp (loading.progress, 0.0f, 1.0f),
                       1.0f);
            std::ostringstream percent;
            percent << static_cast<int> (
                         std::lround (loading.progress * 100.0f))
                    << '%';
            m_hud.draw_shaded (m_hud_text,
                               left + content_width,
                               rail_y - 8.0f,
                               percent.str (),
                               style (9.0f, 0.62f, 0.06f),
                               render::TextAlign::Right);
          }

          const std::size_t history_end =
            loading.events.empty () ? 0 : loading.events.size () - 1;
          const std::size_t history_begin =
            history_end > 2 ? history_end - 2 : 0;
          float line_y = bottom - 20.0f;
          for (std::size_t i = history_begin; i < history_end; ++i) {
            const LoadingEvent& event = loading.events[i];
            std::ostringstream line;
            line << std::fixed << std::setprecision (1) << event.elapsed
                 << " s   " << event.title;
            m_hud.draw_shaded (m_hud_text,
                               left,
                               line_y,
                               line.str (),
                               style (10.0f, 0.46f, 0.02f));
            line_y += 16.0f;
          }
        }

        bool captured = false;
        if (const char* path =
              moppe::environment ("MOPPE_LOADING_SCREENSHOT")) {
          if (m_loading.claim_loading_capture (completed != nullptr)) {
            r.request_screenshot (path);
            captured = true;
          }
        }
        r.draw_hud_text (m_hud_text);
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
        fp.scene_scale = m_graphics.scene_scale;
        fp.render_scale_override = m_graphics.render_scale_override;
        fp.scene_megapixel_budget = m_graphics.scene_megapixel_budget;
        fp.clear_color = DisplayColor (0, 0, 0);
        fp.view = Mat4 ();
        fp.proj = Mat4 ();
        if (!r.begin_frame (fp))
          return;

        m_hud_dl.clear ();
        m_hud_text.clear ();
        m_hud.draw_game_over (
          m_hud_dl, m_hud_text, r.width_pts (), r.height_pts ());
        r.draw_hud_text (m_hud_text);
        r.draw_hud (m_hud_dl);
        r.end_frame ();
      }

      // -- input -------------------------------------------------------

      // A launch recording itself runs its remote control by the frames of
      // play it has drawn, starting with the first.
      std::optional<double> script_seconds () const override {
        if (!m_video_plan)
          return std::nullopt;
        return m_ready && !opening_active () ? m_video_render_frame / 60.0
                                             : -1.0;
      }

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

        if (k == Key::H && down) {
          m_hud.set_diagnostics (!m_hud.diagnostics ());
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

        // V starts a video of what is on screen, and V again ends it.
        if (k == Key::Record && down) {
          if (m_video.recording ())
            m_video.finish ();
          else if (!m_video_plan)
            m_video.begin (next_clip_path (), 30);
          return;
        }
        if (k == Key::Escape && down)
          m_video.finish ();

        if (m_spectator) {
          // M frees the mouse -- to start a screen recording, say -- and
          // takes it back.
          if (k == Key::M && down) {
            m_spectator->pointer_free = !m_spectator->pointer_free;
            platform::set_pointer_captured (!m_spectator->pointer_free);
          }
          if (down)
            m_spectator->held.insert (k);
          else
            m_spectator->held.erase (k);
          if (k == Key::Escape && down)
            platform::request_quit ();
          return;
        }
        if (opening_active ()) {
          if (k == Key::Escape && down)
            platform::request_quit ();
          else
            m_live_input.cinematic_key (k, down);
          return;
        }

        if (k == Key::M && down) {
          m_pointer_free = !m_pointer_free;
          platform::set_pointer_captured (!m_pointer_free);
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
      // A launch told to record itself: MOPPE_VIDEO names the file, and
      // beside it MOPPE_VIDEO_SECONDS how long the video runs before the
      // game quits (otherwise until it is quit), MOPPE_VIDEO_START the
      // seconds of play to let pass first, MOPPE_VIDEO_FPS 30 or 60,
      // MOPPE_VIDEO_SPEED a whole number of times faster than life, and
      // MOPPE_VIDEO_HEIGHT a height to scale to. The world then advances a
      // sixtieth of a second per rendered frame, so the video is even
      // however long its frames took (tools/record).
      struct VideoPlan {
        std::string path;
        int fps = 30;
        float seconds = 0.0f;
        float start = 0.0f;
        int speed = 1;
        int height = 0;
      };

      static std::optional<VideoPlan> plan_video () {
        const char* path = moppe::environment ("MOPPE_VIDEO");
        if (!path || !*path)
          return std::nullopt;
        VideoPlan plan;
        plan.path = path;
        const auto number = [] (const char* name, float otherwise) {
          const char* value = moppe::environment (name);
          return value ? (float)::atof (value) : otherwise;
        };
        plan.fps = number ("MOPPE_VIDEO_FPS", 30.0f) > 45.0f ? 60 : 30;
        plan.seconds = std::max (0.0f, number ("MOPPE_VIDEO_SECONDS", 0.0f));
        plan.start = std::max (0.0f, number ("MOPPE_VIDEO_START", 0.0f));
        plan.speed =
          std::max (1, static_cast<int> (number ("MOPPE_VIDEO_SPEED", 1.0f)));
        plan.height =
          std::max (0, static_cast<int> (number ("MOPPE_VIDEO_HEIGHT", 0.0f)));
        return plan;
      }

      // Hands this frame to the video being recorded, if it is one of the
      // frames the video takes.
      void record_frame (render::Renderer& r) {
        const auto take = [&] {
          r.request_frame (
            [this] (const render::FramePixels& frame) { m_video.add (frame); });
        };
        const int index = m_video_render_frame++;
        if (!m_video_plan) {
          // A video begun with V takes every second frame as they come.
          if (m_video.recording () && index % 2 == 0)
            take ();
          return;
        }
        if (m_video_done)
          return;
        const VideoPlan& plan = *m_video_plan;
        const int first = static_cast<int> (std::lround (plan.start * 60.0f));
        if (index < first)
          return;
        if (index == first)
          m_video.begin (plan.path, plan.fps, plan.height);
        if (!m_video.recording () ||
            (plan.seconds > 0.0f &&
             m_video.frames () >= std::lround (plan.seconds * plan.fps))) {
          m_video.finish ();
          m_video_done = true;
          platform::request_quit ();
          return;
        }
        if ((index - first) % (60 / plan.fps * plan.speed) == 0)
          take ();
      }

      std::string next_clip_path () {
        std::ostringstream path;
        path << snapshot_directory () << "/clip-" << std::setfill ('0')
             << std::setw (3) << m_clip_count++ << ".mp4";
        return path.str ();
      }

      // The screenshot key and the video key drop their files into one
      // per-run timestamped directory.
      const std::string& snapshot_directory () {
        namespace fs = std::filesystem;
        if (m_snapshot_directory.empty ()) {
          const char* base = moppe::environment ("MOPPE_SCREENSHOT_DIR");
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
        return m_snapshot_directory;
      }

      // The in-game screenshot key's frames number on through a run, so a
      // walk through the world becomes a reviewable series.
      std::string next_snapshot_path () {
        std::ostringstream path;
        path << snapshot_directory () << "/shot-" << std::setfill ('0')
             << std::setw (3) << m_snapshot_count++ << ".png";
        std::cerr << "moppe: screenshot " << path.str () << '\n';
        return path.str ();
      }

      // Beside each screenshot, the camera that took it as a `shot` line for
      // data/opening.txt: printed to the log (on the Xbox, LocalState's
      // log.txt) and gathered in the run directory's shots.txt.
      void record_shot_pose (const FrameView& frame, const std::string& path) {
        const std::string name = std::filesystem::path (path).stem ().string ();
        const std::string line = format_opening_shot (
          name,
          frame.camera.position,
          frame.camera.forward,
          frame.camera.field_of_view.numerical_value_in (u::deg),
          logic ().m_total_time,
          m_sky.sun_height);
        std::cerr << "moppe: pose: " << line << '\n';
        std::ofstream shots (std::filesystem::path (m_snapshot_directory) /
                               "shots.txt",
                             std::ios::app);
        shots << line << '\n';
      }

      static int glide_frame_limit () {
        static const int frames = [] {
          if (!moppe::environment ("MOPPE_GLIDE"))
            return 0;
          const char* count = moppe::environment ("MOPPE_GLIDE_FRAMES");
          return count ? std::max (2, ::atoi (count)) : 120;
        }();
        return frames;
      }

      static float glide_speed_mps () {
        static const float speed = [] {
          const char* value = moppe::environment ("MOPPE_GLIDE_SPEED");
          return value ? std::max (0.1f, (float)::atof (value)) : 12.0f;
        }();
        return speed;
      }

      static float glide_vertical_speed_mps () {
        static const float speed = [] {
          const char* value = moppe::environment ("MOPPE_GLIDE_VERTICAL_SPEED");
          return value ? (float)::atof (value) : 0.0f;
        }();
        return speed;
      }

      const GazetteerShot* current_gazetteer_shot () const noexcept {
        if (!m_gazetteer || m_gazetteer_shot >= m_gazetteer_plan.shots.size ())
          return nullptr;
        return &m_gazetteer_plan.shots[m_gazetteer_shot];
      }

      FrameViewInput frame_view_input (float aspect) const {
        FrameSceneMode scene = FrameSceneMode::Gameplay;
        FrameCameraReading camera;
        const bool cinematic = opening_active ();

        if (const GazetteerShot* shot = current_gazetteer_shot ()) {
          scene = FrameSceneMode::Gazetteer;
          Vec3 eye = position_value (shot->eye);
          Vec3 subject = position_value (shot->subject);
          // A glide translates the frozen shot's camera along its own
          // horizontal heading, one render frame at a time, so consecutive
          // captures differ by camera motion and nothing else.
          if (glide_frame_limit () > 0) {
            Vec3 heading = subject - eye;
            heading[1] = 0.0f;
            const Vec3 step =
              normalized (heading) * (glide_speed_mps () / 60.0f);
            const Vec3 vertical_step =
              Vec3 (0, glide_vertical_speed_mps () / 60.0f, 0);
            const Vec3 travel =
              (step + vertical_step) * static_cast<float> (m_glide_frame);
            eye += travel;
            subject += travel;
          }
          camera = {
            .position = eye,
            .forward = normalized (subject - eye),
            .view = Mat4::look_at (eye, subject, Vec3 (0, 1, 0)),
            .field_of_view = shot->vertical_field_of_view,
          };
        } else if (m_opening.active ()) {
          scene = FrameSceneMode::Cinematic;
          camera = {
            .position = m_opening.position (),
            .forward = m_opening.forward (),
            .view = m_opening.view_matrix (),
            .field_of_view = m_opening.field_of_view () * u::deg,
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
            m_cinematic.active () ? m_cinematic.motion_blur () : 0.0f,
          .cinematic_elapsed = m_opening.active () ? m_opening.elapsed ()
                               : cinematic         ? m_cinematic.elapsed ()
                                                   : 0.0f,
          .reveal_player = m_opening.active () && !m_opening.shot ().settle,
          .benchmark = benchmark,
          .sky = m_sky,
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

        // The first epoch restores too: a restore rebuilds the rigid bike's
        // physics world without the solver's warm-start history, so only a
        // restored session replays exactly like every other epoch.
        session ().restore (*m_benchmark_checkpoint);
        m_renderer->reset_temporal_state ();
        m_graphics = m_benchmark_baseline;
        m_benchmark_mask = apply_graphics_benchmark_mask (
          m_graphics, frame->partition_mask, m_benchmark->partition);
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
        const int configurations =
          1 << graphics_benchmark_dimension_count (m_benchmark->partition);
        std::ostringstream title;
        title << "Moppe benchmark " << (epoch + 1) << '/' << configurations
              << " - ";
        bool any = false;
        for (int bit = 0;
             bit < graphics_benchmark_dimension_count (m_benchmark->partition);
             ++bit)
          if (partition_mask & (1u << bit)) {
            if (any)
              title << " + ";
            title << graphics_benchmark_block_name (m_benchmark->partition,
                                                    bit);
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
        m_opening.stop ();
        m_live_input.clear ();
        // On foot the opening has already arrived in the player's eyes.
        if (logic ().m_cam_mode == CAM_HELMET) {
          const Vec3 look = session ().subject_heading ();
          session ().camera ().place (logic ().m_fp_eye,
                                      logic ().m_fp_eye + look * 10.0f);
          return;
        }
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
        m_opening.stop ();
        m_opening_shots.clear ();
        m_waterfall_surface.clear ();
        m_water_inspection.reset ();
        const terrain::Seed next_seed = terrain::next_seed (recipe ().seed ());
        terrain::WorldRecipe next_recipe = terrain::make_world_recipe (
          recipe ().extent (),
          recipe ().resolution (),
          next_seed,
          recipe ().water_datum (),
          recipe ().generation_profile (),
          recipe ().evolution ().uplift_duration,
          recipe ().evolution ().channel_initiation_area,
          recipe ().evolution ().fluvial_transport.concentration_at_unit_slope,
          recipe ().evolution ().critical_hillslope_gradient,
          recipe ().evolution ().maximum_hillslope_diffusivity_multiplier);
        session ().set_mode (M_BIKE);
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
        session ().set_mode (M_BIKE);
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
      Almanac m_almanac;
      Day m_day;
      SkyReading m_sky;
      Vec3 m_spawn_position;
      Vec3 m_home_base_position;
      render::DrawList m_home_base_marker;
      bool m_skip_cinematic_requested = false;
      CinematicFlightPlan m_cinematic_plan;
      CinematicFlight m_cinematic;
      std::vector<OpeningShot> m_opening_shots;
      OpeningPlayer m_opening;
      std::size_t m_opening_shot_seen = SIZE_MAX;
      std::size_t m_opening_rendered_shot = SIZE_MAX;
      InputFrameAdapter m_live_input;
      SimulationClock m_simulation_clock;
      WaterfallSurface m_waterfall_surface;
      Terrain m_terrain;
      ForestLandscape m_forest;
      BoulderLandscape m_boulders;
      MushroomPatch m_mushrooms;
      // The poisonous mushrooms the foraging script has left be.
      std::vector<std::uint32_t> m_forage_refused;
      // Scratch for the basket's contents as drawn.
      std::vector<MushroomSite> m_basket_contents;
      BlobShadow m_blob;
      mov::TrunkField m_trunk_field;
      // The trees the walker stands between that would take the hammock,
      // and where they last stood when that was worked out.
      std::optional<HammockSite> m_hammock_at_hand;
      Vec3 m_hammock_sought_from;
      bool m_hammock_sought = false;
      float m_walk_script_time = 0.0f;
      // MOPPE_WALK=camp: the trees it camps between, the side it walks in
      // from, and how far through its steps it is.
      struct CampScript {
        std::optional<HammockSite> site;
        float side = 1.0f;
        int step = 0;
        float since = 0.0f;
      };
      CampScript m_camp_script;
      Hud m_hud;
      render::TextList m_hud_text;

      render::Renderer* m_renderer;
      bool m_automated_regeneration_done = false;
      std::string m_screenshot_path;
      bool m_snapshot_requested = false;
      std::string m_snapshot_directory;
      int m_snapshot_count = 0;
      VideoRecorder m_video;
      std::optional<VideoPlan> m_video_plan = plan_video ();
      int m_video_render_frame = 0;
      int m_clip_count = 0;
      bool m_video_done = false;
      std::optional<WaterShot> m_water_shot;
      std::optional<WaterInspection> m_water_inspection;
      std::optional<mov::Trunk> m_orbit_tree;
      // Whether M has handed the mouse back to the desktop during play.
      bool m_pointer_free = false;
      // A free camera with no rider, for looking at the world as it is.
      struct Spectator {
        Vec3 eye {};
        float yaw = 0.0f;
        float pitch = 0.0f;
        std::set<platform::Key> held;
        bool pointer_free = false;
      };
      std::optional<Spectator> m_spectator =
        moppe::environment ("MOPPE_SPECTATOR")
          ? std::optional<Spectator> (Spectator {})
          : std::nullopt;
      std::optional<GazetteerCaptureConfig> m_gazetteer;
      LandscapeGazetteer m_gazetteer_plan;
      std::size_t m_gazetteer_shot = 0;
      int m_gazetteer_settle_frame = 0;
      int m_glide_frame = 0;
      int m_screenshot_frames;
      int m_cinematic_capture_frame = 0;
      int m_cinematic_capture_render_frame = 0;
      int m_ride_capture_frame = 0;
      int m_ride_capture_render_frame = 0;
      std::atomic<bool> m_ready;
      std::optional<GraphicsBenchmarkConfig> m_benchmark;
      GraphicsSettings m_benchmark_baseline;
      mov::BikePhysics m_bike_physics;
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
