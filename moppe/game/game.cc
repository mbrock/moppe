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
        // Letters and QWERTY positions both move, so WASD works on any
        // keyboard layout.
        const auto either = [&held] (Key letter, Key position) {
          return std::max (held (letter), held (position));
        };
        view.eye += (forward * (either (Key::W, Key::PhysicalW) -
                                either (Key::S, Key::PhysicalS)) +
                     right * (either (Key::D, Key::PhysicalD) -
                              either (Key::A, Key::PhysicalA)) +
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
                                    sun_direction_for (m_graphics.sun_height));
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

      void plan_opening_journey () {
        MOPPE_PROFILE_ZONE ("startup.plan_cinematic_flight");
        if (m_spectator)
          return;
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
        settle_obstacles ();
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
        m_terrain.render_shadow (
          r, sun_direction_for (m_graphics.sun_height), m_graphics.forest);
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
          (m_cinematic.active () &&
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
          moppe::environment ("MOPPE_RIDE_CAPTURE_DIR");
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
      // "walk", "run", and "jump" hold one gait, and "tour" stands, walks,
      // runs, jumps twice from the run, and comes to rest, turning gently
      // throughout so a following camera sees the figure from changing
      // sides.
      InputFrame scripted_walk (std::string_view script, float dt) {
        const float t = m_walk_script_time;
        m_walk_script_time += dt;
        InputFrame input;
        const auto press = [t, dt] (float at) {
          return t >= at && t < at + 0.1f + dt ? 1.0f : 0.0f;
        };
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
      // ridden bike, to judge the rider and the suspension at work.
      void ride_capture_camera () {
        static const std::string_view view = [] {
          const char* name = moppe::environment ("MOPPE_RIDE_CAMERA");
          return std::string_view (name ? name : "");
        }();
        if (view.empty () || logic ().m_mode != M_BIKE)
          return;
        const auto& bike = session ().bike ();
        Vec3 heading = bike.render_orientation ();
        heading[1] = 0.0f;
        heading = normalized (heading);
        const Vec3 right (heading[2], 0.0f, -heading[0]);
        const Vec3 at = bike.render_position () + Vec3 (0, 0.4f, 0);
        const Vec3 from = view == "front" ? heading * 4.5f + right * 1.2f
                                          : right * 4.5f + heading * 0.6f;
        session ().camera ().place (at + from + Vec3 (0, 0.5f, 0), at);
      }

      void tick_simulation (float dt) {
        MOPPE_PROFILE_ZONE ("MoppeGame::tick_simulation");
        std::optional<InputFrame> scripted_input;
        if (m_cinematic.active () &&
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
        static const bool demo = moppe::environment ("MOPPE_DEMO") != 0;
        m_trunk_field.focus (session ().subject_position ());
        static const bool orbit = moppe::environment ("MOPPE_ORBIT") != 0 ||
                                  moppe::environment ("MOPPE_PAN") != 0;
        if (m_spectator)
          input = {};
        static const char* walk_script = moppe::environment ("MOPPE_WALK");
        if (walk_script && logic ().m_mode == M_FOOT && !orbit)
          input = scripted_walk (walk_script, dt);
        else if (demo && !m_water_inspection && !orbit) {
          input = {
            .turn = 0.35f * std::sin (total_time * 0.25f),
            .drive = 1.0f,
            .boost = std::fmod (total_time, 11.0f) < 1.35f ? 1.0f : 0.0f,
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
        state.can_mount = reading.can_mount;
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
                                         frame.lighting.sun_direction,
                                         m_graphics.forest,
                                         frame.visibility.boulders);
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
        if (actors.walker && !helmet)
          render_walker (m_world_dl, *actors.walker, frame.lighting.time);
        if (actors.glider && !helmet)
          render_glider (m_world_dl, *actors.glider, frame.lighting.time);

        r.draw_list (m_world_dl, 0x0001);

        // Additive glow after the solid list, so it blends over everything
        // already drawn: exhaust and jump-jet flames, then star halos.
        if (visibility.vehicle_effects && !m_spectator &&
            !(helmet && actors.active_mode == M_BIKE))
          render_vehicle_flames (r, actors.bike, frame.lighting.time, 0x1000);
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
          m_hud.draw_ride_prompt (m_hud_text,
                                  frame.overlay.cinematic_prompt_alpha,
                                  hud_width,
                                  hud_height);
        } else if (visibility.game_hud) {
          const HudState hud_state = hud_state_for (frame.hud);
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
          r.request_screenshot (next_snapshot_path ());
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
        m_hud_text.clear ();
        m_hud.draw_game_over (
          m_hud_dl, m_hud_text, r.width_pts (), r.height_pts ());
        r.draw_hud_text (m_hud_text);
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
        // Riding reads letters; physical positions are for free flight.
        if (k == Key::PhysicalW || k == Key::PhysicalA || k == Key::PhysicalS ||
            k == Key::PhysicalD)
          return;

        if (m_cinematic.active ()) {
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
      // The in-game screenshot key drops frames into one per-run timestamped
      // directory, so a walk through the world becomes a reviewable series.
      std::string next_snapshot_path () {
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
        std::ostringstream path;
        path << m_snapshot_directory << "/shot-" << std::setfill ('0')
             << std::setw (3) << m_snapshot_count++ << ".png";
        std::cerr << "moppe: screenshot " << path.str () << '\n';
        return path.str ();
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
        const bool cinematic = m_cinematic.active ();

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
        logic ().m_mode = M_BIKE;
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
      BoulderLandscape m_boulders;
      BlobShadow m_blob;
      mov::TrunkField m_trunk_field;
      float m_walk_script_time = 0.0f;
      Hud m_hud;
      render::TextList m_hud_text;

      render::Renderer* m_renderer;
      bool m_automated_regeneration_done = false;
      std::string m_screenshot_path;
      bool m_snapshot_requested = false;
      std::string m_snapshot_directory;
      int m_snapshot_count = 0;
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
