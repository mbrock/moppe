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
#include <moppe/game/fishing.hh>
#include <moppe/game/chase_camera.hh>
#include <moppe/game/cinematic_flight.hh>
#include <moppe/game/companion_link.hh>
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
#include <moppe/game/helicopter_render.hh>
#include <moppe/game/hud.hh>
#include <moppe/game/input_frame_adapter.hh>
#include <moppe/game/jeep_render.hh>
#include <moppe/game/landscape_gazetteer.hh>
#include <moppe/game/landscape_summary.hh>
#include <moppe/game/launch_options.hh>
#include <moppe/game/map_sheet.hh>
#include <moppe/game/moppe_game.hh>
#include <moppe/game/mushrooms.hh>
#include <moppe/game/opening.hh>
#include <moppe/game/seed_memory.hh>
#include <moppe/game/simulation_clock.hh>
#include <moppe/game/stars.hh>
#include <moppe/game/surface_presentation.hh>
#include <moppe/game/terrain.hh>
#include <moppe/game/boat_render.hh>
#include <moppe/game/caddy_render.hh>
#include <moppe/game/long_john_render.hh>
#include <moppe/game/mountain_bike_render.hh>
#include <moppe/game/tractor_render.hh>
#include <moppe/game/places.hh>
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
#include <moppe/mov/box3d_vec.hh>
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
        if (options.multiplayer && !m_benchmark && !m_gazetteer &&
            !m_water_shot)
          seek_company (*options.multiplayer);
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
        // Before the session places anything: its bodies stand on the
        // roads.
        mov::attach_roads (surface (), &m_generated_world->road_surface ());
        m_session =
          std::make_unique<GameSession> (world (), surface (), m_bike_physics);
        retired_session.reset ();
        retired_world.reset ();
        m_map.forget ();
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
        forget_companion ();
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
            else if (name == "eyes")
              logic ().m_cam_mode = CAM_HELMET;
          }
        }
        // MOPPE_DEMO=jeep, tractor, caddy, mountain_bike, or long_john: the
        // autopilot drives that instead.
        if (demo && !m_benchmark && !m_gazetteer && logic ().m_mode == M_BIKE) {
          if (std::string_view (demo) == "jeep")
            session ().set_mode (M_JEEP);
          else if (std::string_view (demo) == "tractor")
            session ().set_mode (M_TRACTOR);
          else if (std::string_view (demo) == "caddy")
            session ().set_mode (M_CADDY);
          else if (std::string_view (demo) == "mountain_bike")
            session ().set_mode (M_MOUNTAIN_BIKE);
          else if (std::string_view (demo) == "long_john")
            session ().set_mode (M_LONG_JOHN);
        }
      }

      // -- places ----------------------------------------------------

      PlaceGround place_ground () const {
        const terrain::WaterSheets& sheets =
          generated_world ().water_surface ();
        const float sea = world ().water_level.numerical_value_in (u::m);
        const auto level = [this, &sheets] (float x, float z) {
          return still_water_at (world (), surface (), &sheets,
                                 Vec3 (x, 0.0f, z))
            .numerical_value_in (u::m);
        };
        return {
          .ground = [this] (float x, float z) {
            return ground_height (Vec3 (x, 0.0f, z));
          },
          .water = level,
          .sea = [level, sea] (float x, float z) {
            return level (x, z) <= sea + 0.05f;
          },
          .current = [&sheets] (float x, float z) {
            return spatial::sample<terrain::water_velocity> (
                     sheets, moppe::position (Vec3 (x, 0.0f, z)))
              .numerical_value_in (u::m / u::s);
          },
          .roads = &generated_world ().road_surface (),
          .sea_level = sea,
          .period_x =
            surface ().domain ().period_x ().numerical_value_in (u::m),
          .period_z =
            surface ().domain ().period_z ().numerical_value_in (u::m),
        };
      }

      // Where the places' things stand is planned before the trees and
      // rocks, which keep out of their clearings.
      void plan_world_places () {
        MOPPE_PROFILE_ZONE ("startup.plan_places");
        m_places = {};
        if (m_water_inspection || moppe::environment ("MOPPE_TREE_LAB"))
          return;
        m_places =
          plan_places (generated_world ().roads (), place_ground (),
                       static_cast<std::uint32_t> (recipe ().seed ().value) ^
                         0x91ace5u);
        logic ().m_beacons.assign (m_places.sanctuaries.size (), Beacon {});
        // MOPPE_BEACONS=lit: every beacon already burning, for captures.
        if (const char* lit = moppe::environment ("MOPPE_BEACONS");
            lit && std::string_view (lit) == "lit")
          for (Beacon& beacon : logic ().m_beacons)
            beacon.lit = logic ().m_total_time - 30.0;
        tend_beacons ();
      }

      // Once the trees stand: the camps among them, and the built things
      // with their colliders.
      void build_world_places () {
        MOPPE_PROFILE_ZONE ("startup.build_places");
        const PlaceGround ground = place_ground ();
        place_camps (
          m_places, generated_world ().roads (), ground,
          [this] (const Vec3& near) -> std::optional<Vec3> {
            const std::vector<mov::Trunk> trunks =
              m_trunk_field.gather (near, 90.0f);
            const std::optional<HammockSite> site = nearest_hammock_site (
              trunks, near, ground_reader (), 0.6f);
            if (!site)
              return std::nullopt;
            const Vec3 middle = hammock_middle (*site);
            if (std::hypot (middle[0] - near[0], middle[2] - near[2]) > 80.0f)
              return std::nullopt;
            return middle;
          });
        if (!m_renderer)
          return;
        m_places_landscape.rebuild (*m_renderer, m_places, ground);
        if (!m_places_landscape.colliders ().empty ()) {
          std::vector<mov::Trunk> obstacles = m_forest.trunks ();
          obstacles.insert (obstacles.end (),
                            m_boulders.colliders ().begin (),
                            m_boulders.colliders ().end ());
          obstacles.insert (obstacles.end (),
                            m_places_landscape.colliders ().begin (),
                            m_places_landscape.colliders ().end ());
          const Vec3 period =
            extent_value (generated_world ().forest ().period);
          m_trunk_field.set_trunks (std::move (obstacles), period[0],
                                    period[2]);
        }
      }

      // The beacon a walker stands at, if any.
      std::optional<std::size_t> beacon_at_hand () const {
        if (logic ().m_mode != M_FOOT || logic ().m_camp.resting)
          return std::nullopt;
        return sanctuary_at_hand (m_places, session ().walker ().position ());
      }

      void tend_beacons () {
        const double now = logic ().m_total_time;
        std::vector<Beacon>& beacons = logic ().m_beacons;
        m_beacon_burn.resize (beacons.size ());
        for (std::size_t i = 0; i < beacons.size (); ++i)
          m_beacon_burn[i] = beacon_burn (beacons[i].lit, beacons[i].out, now);
      }

      // The boat lies at the water's edge of the sea, the nearest open
      // shore of it to the spawn (or, with no sea in reach or with
      // `MOPPE_BOAT=lake`, of a lake big enough to take it out on): afloat
      // a few metres out with its bow drawn up toward the land, as left by
      // someone gone ashore. `outward` turns it to face the open water.
      void park_boat (bool outward = false) {
        static const bool lakes_first = [] {
          const char* water = moppe::environment ("MOPPE_BOAT");
          return water && std::string_view (water) == "lake";
        }();
        static const bool river = [] {
          const char* water = moppe::environment ("MOPPE_BOAT");
          return water && std::string_view (water) == "river";
        }();
        m_mooring.reset ();
        m_boat_route.clear ();
        m_boat_route_at = 0;
        if (river && moor_on_river ())
          return;
        if (!lakes_first && m_places.landing) {
          // At the end of the road, alongside the landing's jetty.
          const Landing& landing = *m_places.landing;
          mov::Boat& boat = session ().boat ();
          boat.set_trunks (&m_trunk_field);
          boat.place (landing.berth,
                      outward ? -landing.berth_forward : landing.berth_forward,
                      landing.berth[1]);
          m_mooring = landing.jetty.root;
          std::cerr << "moppe: boat moored at the landing's jetty, "
                    << std::hypot (landing.berth[0] - m_spawn_position[0],
                                   landing.berth[2] - m_spawn_position[2])
                    << " m from the spawn, at " << landing.berth[0] << ' '
                    << landing.berth[2] << std::endl;
          return;
        }
        if (!lakes_first && moor_boat (true, outward))
          return;
        if (!moor_boat (false, outward))
          std::cerr << "moppe: boat: no open water near the spawn\n";
      }

      // How deep the still water stands over the ground at `at`.
      float boat_depth_at (const Vec3& at) const {
        return still_water_at (world (), surface (),
                               &generated_world ().water_surface (), at)
                 .numerical_value_in (u::m) -
               ground_height (at);
      }

      // MOPPE_BOAT=river: the boat lies on the greatest river, as far up
      // from the sea as the water stays deep enough to drive it the whole
      // way down, facing downstream, and the autopilot follows the
      // river's course out to sea.
      bool moor_on_river () {
        // The course from its head down to the mouth, unwrapped about the
        // periodic world.
        const float period[2] = {
          surface ().domain ().period_x ().numerical_value_in (u::m),
          surface ().domain ().period_z ().numerical_value_in (u::m),
        };
        const auto course = greatest_river_course (lake_census (), rivers ());
        std::vector<Vec3> line;
        for (auto reach = course.rbegin (); reach != course.rend (); ++reach)
          for (const terrain::RiverAlignmentPoint& point :
               (*reach)->alignment.points) {
            Vec3 at (point.x_m, 0.0f, point.z_m);
            if (!line.empty ())
              for (int axis : { 0, 2 }) {
                const float p = period[axis / 2];
                at[axis] -= p * std::round ((at[axis] - line.back ()[axis]) / p);
              }
            if (line.empty () || length (at - line.back ()) > 4.0f)
              line.push_back (at);
          }
        if (line.size () < 3)
          return false;
        // So far up from the mouth, and on to where it runs deep.
        static const float most = [] {
          const char* reach = moppe::environment ("MOPPE_BOAT_RIVER_M");
          return reach ? std::strtof (reach, nullptr) : 1200.0f;
        }();
        std::size_t start = line.size () - 1;
        float up = 0.0f;
        while (start > 0 && (up < most || boat_depth_at (line[start]) < 0.6f)) {
          up += length (line[start] - line[start - 1]);
          --start;
        }
        if (start + 2 >= line.size ())
          return false;
        const Vec3 at = line[start];
        const Vec3 ahead = normalized (line[start + 1] - at);
        mov::Boat& boat = session ().boat ();
        boat.set_trunks (&m_trunk_field);
        boat.place (at, ahead, boat_depth_at (at) + ground_height (at));
        m_mooring = at;
        m_boat_route.assign (line.begin () + start + 1, line.end ());
        std::cerr << "moppe: boat moored on the river " << up
                  << " m above its mouth, at " << at[0] << ' ' << at[2]
                  << std::endl;
        return true;
      }

      // The boat autopilot (MOPPE_DEMO=boat): down the river's course
      // where it has one, and otherwise a lazy weave over open water;
      // either way it steers clear of shallows it sees ahead.
      InputFrame boat_autopilot (float t) {
        const mov::Boat& boat = session ().boat ();
        const Vec3 at = boat.position ();
        Vec3 heading = boat.forward ();
        heading[1] = 0.0f;
        heading = normalized (heading);
        const auto turned = [] (const Vec3& v, float a) {
          return Vec3 (v[0] * std::cos (a) - v[2] * std::sin (a), 0.0f,
                       v[0] * std::sin (a) + v[2] * std::cos (a));
        };
        // How far open water runs along `way`, up to 90 m.
        const bool river = m_boat_route_at < m_boat_route.size ();
        const float shallow = river ? 0.12f : 0.8f;
        const auto open_reach = [&] (const Vec3& way) {
          for (float reach = 6.0f; reach <= 90.0f; reach += 6.0f)
            if (boat_depth_at (at + way * reach) < shallow)
              return reach;
          return 100.0f;
        };
        Vec3 want;
        float drive = 1.0f;
        while (m_boat_route_at < m_boat_route.size ()) {
          const Vec3 next = m_boat_route[m_boat_route_at];
          const Vec3 to (next[0] - at[0], 0.0f, next[2] - at[2]);
          if (length (to) > 22.0f && dot (to, heading) > -5.0f)
            break;
          ++m_boat_route_at;
        }
        if (m_boat_route_at < m_boat_route.size ()) {
          const Vec3 next = m_boat_route[m_boat_route_at];
          want = normalized (Vec3 (next[0] - at[0], 0.0f, next[2] - at[2]));
          drive = 0.85f;
        } else {
          // Over open water: the way that runs open furthest near a
          // slowly wandering course.
          const float wander = 0.5f * std::sin (t * 0.21f);
          float best = -1e9f;
          for (int k = -8; k <= 8; ++k) {
            const float a = k * (PI / 16.0f);
            const float score =
              std::min (open_reach (turned (heading, a)), 60.0f) -
              40.0f * std::abs (a - wander * 0.6f);
            if (score > best) {
              best = score;
              want = turned (heading, a);
            }
          }
        }
        // Shallows close ahead win over the course.
        if (open_reach (heading) < (river ? 13.0f : 20.0f)) {
          const float left = open_reach (turned (heading, -0.6f));
          const float right = open_reach (turned (heading, 0.6f));
          want = turned (heading, right >= left ? 0.9f : -0.9f);
        }
        const Vec3 right = normalized (cross (heading, Vec3 (0, 1, 0)));
        const float bearing =
          std::atan2 (dot (want, right), dot (want, heading));
        return {
          .turn = std::clamp (2.2f * bearing, -1.0f, 1.0f),
          .drive = std::abs (bearing) > 1.2f ? 0.5f : drive,
        };
      }

      bool moor_boat (bool sea_only, bool outward) {
        const terrain::WaterSheets& sheets =
          generated_world ().water_surface ();
        const float sea = world ().water_level.numerical_value_in (u::m);
        const auto level_at = [&] (const Vec3& at) {
          return still_water_at (world (), surface (), &sheets, at)
            .numerical_value_in (u::m);
        };
        const auto depth_at = [&] (const Vec3& at) {
          return level_at (at) - ground_height (at);
        };
        // Still water: the sea, or (unless `sea_only`) a lake's sheet
        // that does not run.
        const auto still = [&] (const Vec3& at) {
          const position_t where = moppe::position (Vec3 (at[0], 0.0f, at[2]));
          if (level_at (at) <= sea + 0.05f)
            return true;
          if (sea_only)
            return false;
          Vec3 current = spatial::sample<terrain::water_velocity> (sheets,
                                                                   where)
                           .numerical_value_in (u::m / u::s);
          current[1] = 0.0f;
          return length (current) < 0.1f;
        };
        // Open water ahead: deep enough to drive in over a good stretch.
        const auto open = [&] (const Vec3& from, const Vec3& out) {
          int wet = 0;
          for (int k = 0; k < 9; ++k) {
            const float b = 0.6f * (k - 4) / 4.0f;
            const Vec3 dir (out[0] * std::cos (b) - out[2] * std::sin (b),
                            0.0f,
                            out[0] * std::sin (b) + out[2] * std::cos (b));
            bool through = true;
            for (const float reach : { 15.0f, 35.0f, 60.0f })
              through = through && depth_at (from + dir * reach) > 0.8f;
            wet += through;
          }
          return wet >= 7;
        };
        constexpr float step = 5.0f;
        for (int ring = 0; ring < 800; ++ring) {
          const float radius = step * ring;
          const int count =
            std::max (1, static_cast<int> (2.0f * PI * radius / step));
          for (int k = 0; k < count; ++k) {
            const float a = 2.0f * PI * k / count;
            const Vec3 shore = m_spawn_position +
                               Vec3 (std::cos (a), 0.0f, std::sin (a)) * radius;
            // Dry ground just above the water, not a cliff.
            const float d = depth_at (shore);
            if (d > -0.05f || d < -0.6f)
              continue;
            for (int e = 0; e < 16; ++e) {
              const float b = 2.0f * PI * e / 16;
              const Vec3 out (std::cos (b), 0.0f, std::sin (b));
              const Vec3 at = shore + out * 5.0f;
              if (depth_at (at) < 0.45f || depth_at (shore + out * 2.0f) > 0.4f ||
                  !still (at) || !open (at, out))
                continue;
              // Facing out, it lies further off, so that its long tail
              // reaches water deep enough to drive it from the start.
              const Vec3 moor =
                outward && depth_at (shore + out * 9.0f) > 0.7f
                  ? shore + out * 9.0f
                  : at;
              mov::Boat& boat = session ().boat ();
              boat.set_trunks (&m_trunk_field);
              boat.place (moor, outward ? out : -out, level_at (moor));
              m_mooring = shore;
              std::cerr << "moppe: boat moored "
                        << length (Vec3 (at[0] - m_spawn_position[0],
                                         0.0f,
                                         at[2] - m_spawn_position[2]))
                        << " m from the spawn, on "
                        << (level_at (at) <= sea + 0.05f ? "the sea" : "a lake")
                        << std::endl;
              return true;
            }
          }
        }
        return false;
      }

      // MOPPE_WALK=haul: the tractor is stuck where it stands and the
      // walker stands before it, facing away, to haul it out on a rope.
      // MOPPE_WALK=bump: the jeep stands back from the tractor's flank,
      // aimed at it, with the walker at its door.
      void stage_rescue () {
        const char* walk = moppe::environment ("MOPPE_WALK");
        if (!walk || logic ().m_mode != M_FOOT)
          return;
        const std::string_view script (walk);
        const mov::Tractor& tractor = session ().tractor ();
        if (script == "haul") {
          logic ().m_trouble[static_cast<std::size_t> (Machine::tractor)] = {
            .anchor = tractor.position (), .strain = 4.0f, .stuck = true
          };
          Vec3 ahead = tractor.forward ();
          ahead[1] = 0.0f;
          ahead = length2 (ahead) > 1e-6f ? normalized (ahead) : Vec3 (0, 0, 1);
          Vec3 stand = tractor.position () + ahead * 3.0f;
          stand[1] = ground_height (stand) + 0.1f;
          session ().walker ().spawn (moppe::position (stand), ahead);
        } else if (script == "bump") {
          Vec3 across = tractor.left ();
          across[1] = 0.0f;
          across =
            length2 (across) > 1e-6f ? normalized (across) : Vec3 (1, 0, 0);
          session ().jeep ().place (tractor.position () + across * 18.0f,
                                    -across);
          Vec3 stand = session ().jeep ().point (Vec3 (1.75f, 0.0f, -0.1f));
          stand[1] = ground_height (stand) + 0.1f;
          session ().walker ().spawn (moppe::position (stand), -across);
        } else {
          return;
        }
        logic ().m_fp_eye = session ().walker ().eye_position ();
        session ().settle_camera (surface ());
        std::cerr << "moppe: walk: " << script << " beside the tractor at "
                  << tractor.position ()[0] << ", " << tractor.position ()[2]
                  << std::endl;
      }

      // Whether a machine `room` metres round could stand at `at`: on dry
      // ground well above any water, no steeper across that than `relief`
      // metres, and with no trunk or boulder within reach.
      bool open_ground (const Vec3& at, float room, float relief) const {
        const terrain::WaterSheets& sheets =
          generated_world ().water_surface ();
        const float h = ground_height (at);
        float low = h, high = h;
        for (int i = 0; i < 12; ++i) {
          const float b = 6.2831853f * i / 12.0f;
          const Vec3 p = at + Vec3 (std::cos (b), 0.0f, std::sin (b)) * room;
          const float g = ground_height (p);
          if (still_water_at (world (), surface (), &sheets, p)
                .numerical_value_in (u::m) > g - 0.6f)
            return false;
          low = std::min (low, g);
          high = std::max (high, g);
        }
        if (high - low > relief)
          return false;
        for (const mov::Trunk& trunk : m_trunk_field.gather (at, room + 2.0f)) {
          const float dx = trunk.root[0] - at[0];
          const float dz = trunk.root[2] - at[2];
          const float clear = room + trunk.radius;
          if (dx * dx + dz * dz < clear * clear)
            return false;
        }
        return true;
      }

      // The open ground nearest `want`, looked for out to `search` metres
      // round it; `want` itself when there is none.
      Vec3 open_ground_near (const Vec3& want,
                             float room,
                             float relief,
                             float search = 30.0f) const {
        for (float r = 0.0f; r <= search; r += 1.5f) {
          const int around =
            std::max (1, static_cast<int> (6.2831853f * r / 1.5f));
          for (int k = 0; k < around; ++k) {
            const float a = 6.2831853f * k / around;
            const Vec3 at = want + Vec3 (std::cos (a), 0.0f, std::sin (a)) * r;
            if (open_ground (at, room, relief))
              return at;
          }
        }
        return want;
      }

      // The trailhead's frame: the bike's heading on the level, and its
      // left.
      std::pair<Vec3, Vec3> bike_frame () const {
        Vec3 heading = session ().bike ().orientation ();
        heading[1] = 0.0f;
        heading =
          length2 (heading) > 1e-6f ? normalized (heading) : Vec3 (0, 0, 1);
        return { heading, Vec3 (heading[2], 0, -heading[0]) };
      }

      // Where a point unwrapped across the world's seam stands in the lap
      // nearest the spawn.
      Vec3 near_spawn (Vec3 at) const {
        const float px =
          surface ().domain ().period_x ().numerical_value_in (u::m);
        const float pz =
          surface ().domain ().period_z ().numerical_value_in (u::m);
        at[0] -= px * std::round ((at[0] - m_spawn_position[0]) / px);
        at[2] -= pz * std::round ((at[2] - m_spawn_position[2]) / pz);
        at[1] = ground_height (at);
        return at;
      }

      // Whether a machine `room` metres round at `at` keeps clear of the
      // machines already parked there.
      bool clear_of_machines (const Vec3& at, float room) const {
        for (const Vec3& other : { session ().jeep ().position (),
                                   session ().long_john ().position (),
                                   session ().mountain_bike ().position () })
          if (length2 (Vec3 (other[0] - at[0], 0.0f, other[2] - at[2])) <
              (room + 3.0f) * (room + 3.0f))
            return false;
        return true;
      }

      float from_spawn (const Vec3& at) const {
        return length (Vec3 (
          at[0] - m_spawn_position[0], 0.0f, at[2] - m_spawn_position[2]));
      }

      void report_parked (const char* name, const Vec3& at, const char* where) {
        std::cerr << "moppe: " << name << " parked "
                  << std::lround (from_spawn (at)) << " m from the spawn, "
                  << where << ", at " << std::lround (at[0]) << ", "
                  << std::lround (ground_height (at)) << ", "
                  << std::lround (at[2]) << std::endl;
      }

      // The jeep is pulled off onto the verge of the nearest cart road, a
      // few hundred metres out from the trailhead, nose along the road as
      // if left there for the walk in; beside the bike when no road comes
      // near enough.
      void park_jeep () {
        const auto [heading, side] = bike_frame ();
        std::optional<std::pair<Vec3, Vec3>> best;
        float best_score = 1e30f;
        for (const terrain::Road& road : generated_world ().roads ().roads) {
          if (road.kind != terrain::Road::Kind::Road)
            continue;
          for (std::size_t p = 1; p + 1 < road.points.size (); p += 2) {
            const Vec3 a = near_spawn (
              Vec3 (road.points[p - 1].x_m, 0.0f, road.points[p - 1].z_m));
            const Vec3 b = near_spawn (
              Vec3 (road.points[p + 1].x_m, 0.0f, road.points[p + 1].z_m));
            Vec3 along (b[0] - a[0], 0.0f, b[2] - a[2]);
            if (length2 (along) < 1e-4f)
              continue;
            along = normalized (along);
            const Vec3 mid =
              near_spawn (Vec3 (road.points[p].x_m, 0.0f, road.points[p].z_m));
            const float r = from_spawn (mid);
            if (r < 150.0f || r > 1200.0f)
              continue;
            // Nearest the trailhead wins, a road still in use before one
            // gone over.
            const float score =
              std::abs (r - 300.0f) +
              (road.abandoned_years_ago > 0.0f ? 600.0f : 0.0f);
            if (score >= best_score)
              continue;
            const Vec3 across (along[2], 0.0f, -along[0]);
            for (const float offset : { 9.0f, -9.0f, 12.0f, -12.0f }) {
              const Vec3 at = mid + across * offset;
              if (!open_ground (at, 2.6f, 0.9f))
                continue;
              best_score = score;
              best = {
                at, normalized (along + across * (offset > 0 ? 0.25f : -0.25f))
              };
              break;
            }
          }
        }
        if (best) {
          const Vec3 at =
            open_ground_near (best->first + m_park_aside, 2.6f, 0.9f, 12.0f);
          session ().jeep ().place (at, best->second);
          report_parked ("jeep", at, "beside a road");
          return;
        }
        const Vec3 at = open_ground_near (
          session ().bike ().position () + side * 11.0f - heading * 4.0f,
          2.6f, 0.7f);
        session ().jeep ().place (at, normalized (heading + side * 0.35f));
        report_parked ("jeep", at, "beside the bike (no road near)");
      }

      // The tractor stands out on the open ground ahead on the bike's
      // other side, turned to show its flank, as if left there at the end
      // of a day's work.
      void park_tractor () {
        const auto [heading, side] = bike_frame ();
        session ().tractor ().set_trunks (&m_trunk_field);
        const Vec3 at = open_ground_near (
          session ().bike ().position () - side * 17.0f + heading * 15.0f,
          2.4f, 0.8f, 40.0f);
        session ().tractor ().place (at, normalized (-side - heading * 0.6f));
      }

      // The Caddy is parked by the sea, on the nearest level, open ground
      // above the shore the boat was left at, nose to the water as if its
      // driver had stood looking out; with no boat, at the trailhead.
      void park_caddy () {
        // Off the helicopter's line.
        logic ().m_sling = {};
        session ().caddy ().set_trunks (&m_trunk_field);
        const float water = world ().water_level.numerical_value_in (u::m);
        if (m_mooring) {
          // Level ground near the shore first; failing that, out to a few
          // hundred metres along the coast, on ground a little less level.
          const Vec3 shore = *m_mooring + m_park_aside;
          for (const auto [reach, relief] :
               { std::pair (90.0f, 0.45f), std::pair (320.0f, 0.8f) })
            for (float r = 12.0f; r <= reach; r += 2.0f) {
              const int around = static_cast<int> (6.2831853f * r / 2.0f);
              for (int k = 0; k < around; ++k) {
                const float a = 6.2831853f * k / around;
                const Vec3 at =
                  shore + Vec3 (std::cos (a), 0.0f, std::sin (a)) * r;
                if (ground_height (at) < water + 1.0f ||
                    !open_ground (at, 3.2f, relief) ||
                    !clear_of_machines (at, 3.2f))
                  continue;
                const Vec3 to_sea =
                  normalized (Vec3 (shore[0] - at[0], 0.0f, shore[2] - at[2]));
                session ().caddy ().place (at, to_sea);
                report_parked ("Caddy", at, "up from the boat's shore");
                return;
              }
            }
        }
        const auto [heading, side] = bike_frame ();
        const Vec3 at = open_ground_near (session ().bike ().position () +
                                            side * 11.0f - heading * 11.0f,
                                          3.2f,
                                          0.6f);
        session ().caddy ().place (at, normalized (heading - side * 0.12f));
        report_parked ("Caddy", at, "at the trailhead (no shore found)");
      }

      // The mountainbike is left at the top of the trail's nearest climb,
      // the highest point of it within a kilometre or so, off the tread and
      // facing back down the way it came up, ready for the descent; a few
      // steps off the motocross when the trail does not climb.
      void park_mountain_bike () {
        const auto [heading, side] = bike_frame ();
        const auto& points = trail_network ().alignment.points;
        const float start = ground_height (m_spawn_position);
        std::optional<std::pair<Vec3, Vec3>> top;
        float best = start + 15.0f;
        for (std::size_t p = 0; p + 1 < points.size (); ++p) {
          const Vec3 at =
            near_spawn (Vec3 (points[p].x_m, 0.0f, points[p].z_m));
          const float r = from_spawn (at);
          if (r < 120.0f || r > 1000.0f)
            continue;
          // Higher is better, but not at the price of a long walk.
          const float score = at[1] - 0.03f * r;
          if (score <= best)
            continue;
          const Vec3 next =
            near_spawn (Vec3 (points[p + 1].x_m, 0.0f, points[p + 1].z_m));
          Vec3 along (next[0] - at[0], 0.0f, next[2] - at[2]);
          if (length2 (along) < 1e-4f)
            continue;
          along = normalized (along);
          const Vec3 across (along[2], 0.0f, -along[0]);
          // Down the way the trail falls from here.
          const Vec3 down = next[1] < at[1] ? along : -along;
          for (const float offset : { 3.0f, -3.0f, 4.5f, -4.5f }) {
            const Vec3 stand = at + across * offset;
            if (!open_ground (stand, 1.0f, 0.5f))
              continue;
            best = score;
            top = { stand, normalized (down + across * 0.3f) };
            break;
          }
        }
        // Without a climb on the trail, a lookout: the highest open ground
        // within a walk, facing back down toward the trailhead.
        const char* where = "at the top of the trail's climb";
        if (!top) {
          float highest = start + 8.0f;
          for (float r = 150.0f; r <= 700.0f; r += 25.0f)
            for (int k = 0; k < 32; ++k) {
              const float a = 6.2831853f * k / 32.0f;
              Vec3 at =
                m_spawn_position + Vec3 (std::cos (a), 0.0f, std::sin (a)) * r;
              at[1] = ground_height (at);
              if (at[1] - 0.03f * r <= highest || !open_ground (at, 1.0f, 0.5f))
                continue;
              highest = at[1] - 0.03f * r;
              const Vec3 home = normalized (Vec3 (m_spawn_position[0] - at[0],
                                                  0.0f,
                                                  m_spawn_position[2] - at[2]));
              top = { at, home };
              where = "at a lookout above the trailhead";
            }
        }
        mov::Vehicle& mtb = session ().mountain_bike ();
        mtb.set_trunks (&m_trunk_field);
        if (top) {
          Vec3 at =
            open_ground_near (top->first + m_park_aside, 1.0f, 0.5f, 8.0f);
          report_parked ("mountainbike", at, where);
          at[1] =
            ground_height (at) + mov::mountain_bike_rig.ride_height () + 0.02f;
          mtb.set_heading (top->second);
          mtb.reset (at);
          return;
        }
        Vec3 at = open_ground_near (
          session ().bike ().position () - side * 5.5f - heading * 3.5f, 1.0f,
          0.4f, 12.0f);
        at[1] =
          ground_height (at) + mov::mountain_bike_rig.ride_height () + 0.02f;
        mtb.set_heading (normalized (heading + side * 0.2f));
        report_parked (
          "mountainbike", at, "beside the bike (the trail does not climb)");
        mtb.reset (at);
      }

      // The Long John, a cargo bike for errands, stands outside the nearest
      // farmhouse still lived in, or else at the nearest hamlet the roads
      // join -- a lake's outlet or the landing -- pointed home
      // toward the trailhead; back down the trail behind the motocross when
      // none is near.
      void park_long_john () {
        const auto [heading, side] = bike_frame ();
        mov::Vehicle& lj = session ().long_john ();
        lj.set_trunks (&m_trunk_field);
        std::optional<Vec3> hamlet;
        const char* where = "in the nearest hamlet";
        // Best of all outside the door of a farmhouse still lived in.
        for (const Farm& farm : m_places.farms) {
          if (!farm.kept || farm.ruined)
            continue;
          for (const Building& building : farm.buildings) {
            if (building.kind != Building::Kind::house)
              continue;
            const Vec3 door =
              near_spawn (building.at + building.forward * 6.0f);
            const float r = from_spawn (door);
            if (r > 3000.0f || (hamlet && r >= from_spawn (*hamlet)))
              continue;
            hamlet = door;
            where = "outside a farmhouse";
          }
        }
        for (const terrain::RoadPlace& place :
             generated_world ().roads ().places) {
          if (hamlet && std::string_view (where) == "outside a farmhouse")
            break;
          using Kind = terrain::RoadPlace::Kind;
          if (place.kind != Kind::LakeOutlet && place.kind != Kind::Landing)
            continue;
          const Vec3 at = near_spawn (Vec3 (place.x_m, 0.0f, place.z_m));
          const float r = from_spawn (at);
          if (r > 3000.0f || (hamlet && r >= from_spawn (*hamlet)))
            continue;
          hamlet = at;
        }
        if (hamlet) {
          Vec3 at =
            open_ground_near (*hamlet + m_park_aside, 1.8f, 0.4f, 60.0f);
          if (open_ground (at, 1.8f, 0.4f)) {
            report_parked ("Long John", at, where);
            const Vec3 home = Vec3 (
              m_spawn_position[0] - at[0], 0.0f, m_spawn_position[2] - at[2]);
            at[1] =
              ground_height (at) + mov::long_john_rig.ride_height () + 0.02f;
            lj.set_heading (length2 (home) > 1e-4f ? normalized (home)
                                                   : heading);
            lj.reset (at);
            return;
          }
        }
        Vec3 at = open_ground_near (session ().bike ().position () -
                                      heading * 24.0f + side * 4.0f,
                                    1.8f, 0.3f, 20.0f);
        at[1] = ground_height (at) + mov::long_john_rig.ride_height () + 0.02f;
        lj.set_heading (normalized (heading - side * 0.15f));
        report_parked ("Long John", at, "down the trail (no hamlet near)");
        lj.reset (at);
      }

      // The helicopter stands on a pad of its own a few hundred metres
      // from the trailhead: the levellest open ground round there, raised
      // above its surroundings where it can be, as a knoll or a shoulder
      // makes a landing site, with no tree within reach of its rotor;
      // clear of `avoid` (the other player's) when given one.
      void park_helicopter (std::optional<Vec3> avoid = std::nullopt) {
        const Vec3 centre = m_spawn_position;
        const float water = world ().water_level.numerical_value_in (u::m);
        const float reach = mov::Helicopter::rotor_radius + 2.5f;
        std::optional<Vec3> best;
        float best_score = 1e30f;
        for (float r = 220.0f; r <= 600.0f; r += 6.0f) {
          const int around = static_cast<int> (6.2831853f * r / 6.0f);
          for (int k = 0; k < around; ++k) {
            const float a = 6.2831853f * (k + 0.5f * (int (r) % 2)) / around;
            Vec3 at = centre + Vec3 (std::cos (a), 0.0f, std::sin (a)) * r;
            at[1] = ground_height (at);
            if (at[1] < water + 1.0f)
              continue;
            if (avoid && length2 (Vec3 (at[0] - (*avoid)[0],
                                        0.0f,
                                        at[2] - (*avoid)[2])) < 15.0f * 15.0f)
              continue;
            // Level under the skids and the tail, and not much less so
            // under the rotor's reach.
            float low = at[1], high = at[1], wide = 0.0f;
            for (int i = 0; i < 12; ++i) {
              const float b = 6.2831853f * i / 12.0f;
              const Vec3 out (std::cos (b), 0.0f, std::sin (b));
              const float inner = ground_height (at + out * 3.0f);
              const float outer = ground_height (at + out * 6.5f);
              low = std::min (low, inner);
              high = std::max (high, inner);
              wide = std::max (wide, std::abs (outer - at[1]));
            }
            if (high - low > 0.6f || wide > 1.6f)
              continue;
            // How far it stands over the ground some way round.
            float around_it = 0.0f;
            for (int i = 0; i < 8; ++i) {
              const float b = 6.2831853f * (i + 0.5f) / 8.0f;
              around_it += ground_height (
                at + Vec3 (std::cos (b), 0.0f, std::sin (b)) * 30.0f);
            }
            const float rise = at[1] - around_it / 8.0f;
            const float score = (r - 220.0f) * 0.004f +
                                (high - low) * 4.0f + wide -
                                std::clamp (rise, -2.0f, 6.0f) * 0.25f;
            if (score >= best_score)
              continue;
            bool clear = true;
            for (const mov::Trunk& trunk : m_trunk_field.gather (at, reach + 1.0f)) {
              const float dx = trunk.root[0] - at[0];
              const float dz = trunk.root[2] - at[2];
              const float room = reach + trunk.radius;
              if (dx * dx + dz * dz < room * room) {
                clear = false;
                break;
              }
            }
            if (!clear)
              continue;
            best_score = score;
            best = at;
          }
          // Somewhere in the nearer half of the band wins over a far
          // better one further off.
          if (best && r > 380.0f)
            break;
        }
        Vec3 heading = trail_direction_from_home ();
        Vec3 at;
        if (best) {
          at = *best;
          // Side on to the spawn, so it shows its whole length from there.
          const Vec3 to =
            normalized (Vec3 (at[0] - centre[0], 0.0f, at[2] - centre[2]));
          heading = Vec3 (to[2], 0.0f, -to[0]);
        } else {
          heading[1] = 0.0f;
          heading =
            length2 (heading) > 1e-6f ? normalized (heading) : Vec3 (0, 0, 1);
          at = centre - Vec3 (heading[2], 0.0f, -heading[0]) * 14.0f;
        }
        session ().helicopter ().set_trunks (&m_trunk_field);
        session ().helicopter ().place (at, heading);
        std::cerr << "moppe: helicopter parked "
                  << length (Vec3 (at[0] - centre[0], 0.0f, at[2] - centre[2]))
                  << " m from the spawn"
                  << (best ? "" : " (no clear site found)") << std::endl;
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
        if (tree_lab || m_places.clearings.empty ()) {
          m_forest.rebuild (*m_renderer,
                            tree_lab ? tree_lab_plan ()
                                     : generated_world ().forest ());
        } else {
          // Nothing grows where the places stand.
          ForestPlan plan = generated_world ().forest ();
          std::erase_if (plan.sites, [this] (const ForestSite& site) {
            return cleared (m_places, position_value (site.position));
          });
          m_forest.rebuild (*m_renderer, plan);
        }
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
        BoulderPlan plan =
          plan_boulders (surface (),
                         surface_readings (),
                         generated_world ().water_surface (),
                         rivers (),
                         recipe ().seed ().value ^ 0x6b0d1e55U,
                         sea_level,
                         std::max (highest - sea, 1.0f) * u::m);
        std::cerr << "moppe: boulder plan: "
                  << std::chrono::duration_cast<std::chrono::milliseconds> (
                       std::chrono::steady_clock::now () - start)
                       .count ()
                  << " ms" << std::endl;
        // Loose rock is cleared from where the places stand; the stones
        // laid across running water stay.
        std::erase_if (plan.sites, [this] (const BoulderSite& site) {
          return !site.solid &&
                 cleared (m_places, position_value (site.centre));
        });
        m_boulders.rebuild (*m_renderer, plan);
        // Where to cross a river dry-shod, for whoever wants to find one.
        const auto nearest = std::ranges::min_element (
          plan.crossings, {}, [this] (const StoneCrossing& crossing) {
            const Vec3 at = position_value (crossing.from);
            return std::hypot (at[0] - m_spawn_position[0],
                               at[2] - m_spawn_position[2]);
          });
        if (nearest != plan.crossings.end ()) {
          const Vec3 at = position_value (nearest->from);
          std::cerr << "moppe: stepping stones: " << plan.crossings.size ()
                    << " crossings, the nearest "
                    << std::hypot (at[0] - m_spawn_position[0],
                                   at[2] - m_spawn_position[2])
                    << " m from the spawn, at " << at[0] << ' ' << at[2]
                    << std::endl;
        }
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
                    << mushroom_plural (m_mushrooms.site (*first).kind) << ") "
                    << std::hypot (at[0] - m_spawn_position[0],
                                   at[2] - m_spawn_position[2])
                    << " m from the spawn" << std::endl;
          // The foraging script starts a few steps from them.
          const char* walk = moppe::environment ("MOPPE_WALK");
          if (walk &&
              (std::string_view (walk) == "forage" ||
               std::string_view (walk) == "supper") &&
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
          length2 (feet - session ().bike ().position ()) < 5.0f * 5.0f ||
          session ().mount_within_reach () != GameSession::Mount::none ||
          session ().hauling ();
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

      // The fire a walker could cook on: the nearest one burning well
      // within a couple of steps, their own or the other player's.
      std::optional<Vec3> fire_at_hand () const {
        if (logic ().m_mode != M_FOOT || logic ().m_camp.resting)
          return std::nullopt;
        const Vec3 feet = session ().walker ().position ();
        const Camp& camp = logic ().m_camp;
        std::optional<Vec3> found;
        float nearest = 2.4f * 2.4f;
        const auto consider = [&] (const Vec3& hearth, float burn) {
          Vec3 to = feet - hearth;
          to[1] = 0.0f;
          if (burn > 0.5f && length2 (to) < nearest) {
            nearest = length2 (to);
            found = hearth;
          }
        };
        consider (camp.hearth, fire_burn (camp, logic ().m_total_time));
        if (m_companion && m_companion->hearth_laid)
          consider (m_companion->hearth, m_companion->fire_burn);
        for (std::size_t i = 0;
             i < m_places.sanctuaries.size () && i < m_beacon_burn.size (); ++i)
          consider (m_places.sanctuaries[i].hearth, m_beacon_burn[i]);
        return found;
      }

      bool beside_pan () const {
        const Pan& pan = logic ().m_camp.pan;
        if (!pan.on || logic ().m_mode != M_FOOT || logic ().m_camp.resting)
          return false;
        Vec3 to = session ().walker ().position () - pan.place;
        to[1] = 0.0f;
        return length2 (to) < pan_reach * pan_reach;
      }

      // What there is to do about supper where the walker stands.
      HudState::Cook supper_at_hand () const {
        const Camp& camp = logic ().m_camp;
        const Basket& basket = logic ().m_basket;
        if (beside_pan ()) {
          const float cooked = pan_cooked (camp.pan, logic ().m_total_time);
          return cooked < 1.0f   ? HudState::Cook::frying
                 : cooked > 1.4f ? HudState::Cook::eat_burnt
                                 : HudState::Cook::eat;
        }
        if (!camp.pan.on &&
            (basket.used < basket.picked.size () ||
             logic ().m_fishing.creel.kept () > 0) &&
            fire_at_hand ())
          return HudState::Cook::fry;
        return HudState::Cook::none;
      }

      // By a fire, the pick key sets a pan of mushrooms from the basket
      // on its stones; once they are fried it eats them, which mends the
      // walker -- less so when they were left to burn.
      void cook_supper (InputFrame& input) {
        Camp& camp = logic ().m_camp;
        Basket& basket = logic ().m_basket;
        const double now = logic ().m_total_time;
        if (logic ().m_mode == M_FOOT && input.deploy_glider &&
            !m_mushrooms
               .within_reach (session ().walker ().position (),
                              session ().walker ().heading ())
               .has_value ()) {
          switch (supper_at_hand ()) {
          case HudState::Cook::fry: {
            Pan pan =
              pan_set_on (*fire_at_hand (), session ().walker ().position ());
            pan.since = now;
            pan.next_steam = now + 1.0;
            // A fish first, if there is one, and mushrooms beside it.
            Creel& creel = logic ().m_fishing.creel;
            if (creel.kept () > 0) {
              pan.has_fish = true;
              pan.fish = creel.fish[creel.used++];
            }
            while (pan.count < Pan::room - (pan.has_fish ? 2 : 0) &&
                   basket.used < basket.picked.size ()) {
              const std::uint32_t index = basket.picked[basket.used++];
              pan.mushrooms[pan.count++] = index;
              if (index < m_mushrooms.mushroom_count ())
                --basket
                    .count[static_cast<int> (m_mushrooms.site (index).kind)];
            }
            camp.pan = pan;
            input.deploy_glider = false;
            break;
          }
          case HudState::Cook::eat:
          case HudState::Cook::eat_burnt:
            camp.ate_cooked = pan_cooked (camp.pan, now);
            camp.ate_count = camp.pan.count;
            camp.ate_fish = camp.pan.has_fish;
            camp.ate = camp.pan.fish;
            camp.ate_time = now;
            camp.pan.on = false;
            logic ().m_health = std::min (
              100.0f,
              logic ().m_health +
                (camp.ate_cooked > 1.4f ? 5.0f : 20.0f) *
                  (camp.ate_count + (camp.ate_fish ? 2 : 0)));
            input.deploy_glider = false;
            break;
          case HudState::Cook::frying:
            input.deploy_glider = false;
            break;
          case HudState::Cook::none:
            break;
          }
        }

        // What fries gives off a little steam, and then smoke.
        Pan& pan = camp.pan;
        if (!pan.on || now < pan.next_steam)
          return;
        const float cooked = pan_cooked (pan, now);
        pan.next_steam = now + (cooked > 1.2f ? 0.25 : 0.45);
        const float shade =
          cooked > 1.2f ? 0.18f : 0.55f + 0.35f * m_sky.daylight;
        Dust::Style steam;
        steam.size = 0.16f * u::m;
        steam.lifetime = 1.6f * u::s;
        steam.downward_acceleration =
          -0.5f * isq::acceleration[u::m / pow<2> (u::s)];
        steam.spread = 0.25f * one;
        session ().dust ().emit (
          moppe::position (pan.place + Vec3 (0, 0.08f, 0)),
          velocity (Vec3 (0, 0.5f, 0)),
          1,
          DisplayColor (shade, shade, shade),
          steam);
      }

      // -- fishing -------------------------------------------------------

      struct FishingWater {
        Vec3 aim;
        Waters waters = Waters::still;
        bool upland = false;
      };

      // Where the angler fishes from and the way they would cast: on foot,
      // the walker's feet and heading; at the boat's helm, the seat and
      // the way the view looks.
      struct Angler {
        Vec3 at;
        Vec3 ahead;
      };
      std::optional<Angler> angler () const {
        if (logic ().m_mode == M_FOOT && !logic ().m_camp.resting) {
          const Walker& walker = session ().walker ();
          return Angler { walker.position (), walker.heading () };
        }
        if (logic ().m_mode == M_BOAT) {
          const mov::Boat& boat = session ().boat ();
          return Angler { boat.helm_seat (),
                          Quaternion::rotate (boat.forward (),
                                              Vec3 (0, 1, 0),
                                              -logic ().m_look_yaw * u::rad) };
        }
        return std::nullopt;
      }

      // The river's current at `at`, level.
      Vec3 current_at (const Vec3& at) const {
        Vec3 current =
          spatial::sample<terrain::water_velocity> (
            generated_world ().water_surface (),
            moppe::position (Vec3 (at[0], 0.0f, at[2])))
            .numerical_value_in (u::m / u::s);
        current[1] = 0.0f;
        return current;
      }

      // The boat only floats, its engine at rest and hardly moving
      // through the water (a river may carry it): a line can go out.
      bool boat_drifting () const {
        const mov::Boat& boat = session ().boat ();
        Vec3 way = boat.velocity () - current_at (boat.position ());
        way[1] = 0.0f;
        return boat.afloat () && std::abs (boat.state ().drive) < 0.1f &&
               length2 (way) < 0.8f * 0.8f;
      }

      // The open water the angler could cast into from where they are: as
      // far out ahead as a cast reaches where it is deep enough to float a
      // bait. From the helm, only over the bow or the sides, not back over
      // the long-tail.
      std::optional<FishingWater> water_ahead () const {
        const std::optional<Angler> from = angler ();
        if (!from)
          return std::nullopt;
        Vec3 ahead = from->ahead;
        ahead[1] = 0.0f;
        if (length2 (ahead) < 1e-6f)
          return std::nullopt;
        ahead = normalized (ahead);
        if (logic ().m_mode == M_BOAT) {
          Vec3 bow = session ().boat ().forward ();
          bow[1] = 0.0f;
          if (length2 (bow) < 1e-6f || dot (ahead, normalized (bow)) < -0.17f)
            return std::nullopt;
        }
        const terrain::WaterSheets& sheets =
          generated_world ().water_surface ();
        const float sea = world ().water_level.numerical_value_in (u::m);
        for (float reach = longest_cast; reach >= shortest_cast;
             reach -= 0.5f) {
          Vec3 at = from->at + ahead * reach;
          const float ground = ground_height (at);
          const float level =
            still_water_at (world (), surface (), &sheets, at)
              .numerical_value_in (u::m);
          if (level - ground < fishing_depth)
            continue;
          at[1] = level;
          FishingWater found { .aim = at, .upland = level > sea + 150.0f };
          const position_t where = moppe::position (Vec3 (at[0], 0.0f, at[2]));
          const float sheet = terrain::surface_elevation_value (
            spatial::sample<terrain::surface_elevation> (sheets, where));
          if (sheet > ground && sheet > sea + 0.05f)
            found.waters = length (current_at (at)) > 0.15f ? Waters::running
                                                             : Waters::still;
          else
            found.waters = Waters::sea;
          return found;
        }
        return std::nullopt;
      }

      // Whether the pick key has nothing nearer to do than cast.
      bool nothing_else_at_hand () const {
        const Walker& walker = session ().walker ();
        return !m_mushrooms.within_reach (walker.position (), walker.heading ())
                  .has_value () &&
               supper_at_hand () == HudState::Cook::none &&
               !beside_hammock ();
      }

      // A splash where the line meets the water.
      void splash (const Vec3& at, int drops, float vigour) {
        const float shade = 0.25f + 0.65f * m_sky.daylight;
        Dust::Style spray;
        spray.size = 0.07f * u::m;
        spray.lifetime = 0.7f * u::s;
        spray.downward_acceleration =
          6.0f * isq::acceleration[u::m / pow<2> (u::s)];
        spray.spread = (0.5f + 0.5f * vigour) * one;
        spray.flake = true;
        session ().dust ().emit (moppe::position (at + Vec3 (0, 0.03f, 0)),
                                 velocity (Vec3 (0, 1.2f + 1.4f * vigour, 0)),
                                 drops,
                                 DisplayColor (shade, shade * 1.02f, shade),
                                 spray);
      }

      // On foot by open water, or at the helm of a boat that only floats,
      // the pick key casts a line, and while it is out the key is the
      // line's: it strikes when something takes the float under, and
      // otherwise winds the line in. Walking off, or getting the boat
      // under way, winds it in too; while a fish is fought in and held up,
      // the walker stands and the boat's engine rests.
      void go_fishing (InputFrame& input, float dt) {
        Fishing& fishing = logic ().m_fishing;
        Line& line = fishing.line;
        const double now = logic ().m_total_time;
        const std::optional<Angler> from = angler ();
        const bool afloat = logic ().m_mode == M_BOAT;
        if (!from || (line_out (line) && line.from_boat != afloat)) {
          line.phase = Line::Phase::idle;
          return;
        }
        if (!line_out (line)) {
          if (!input.deploy_glider ||
              !(afloat ? boat_drifting () : nothing_else_at_hand ()))
            return;
          if (const std::optional<FishingWater> water = water_ahead ()) {
            cast_line (
              line, from->at, water->aim, water->waters, water->upland, now);
            line.from_boat = afloat;
            input.deploy_glider = false;
          }
          return;
        }

        const bool fast = line.phase == Line::Phase::casting ||
                          line.phase == Line::Phase::waiting ||
                          line.phase == Line::Phase::biting ||
                          line.phase == Line::Phase::playing;
        bool gone;
        if (afloat) {
          // The line goes where the boat drifts; opening the throttle
          // while the float rides, or making way through the water
          // however it comes about, takes it in.
          line.stand = from->at;
          const mov::Boat& boat = session ().boat ();
          Vec3 way = boat.velocity () - current_at (boat.position ());
          way[1] = 0.0f;
          Vec3 out = line.bobber - from->at;
          out[1] = 0.0f;
          gone = length2 (way) > 1.5f * 1.5f ||
                 length2 (out) > (longest_cast + 4.0f) *
                                   (longest_cast + 4.0f) ||
                 (std::abs (input_value (input.drive)) > 0.1f &&
                  (line.phase == Line::Phase::waiting ||
                   line.phase == Line::Phase::biting));
        } else {
          Vec3 moved = from->at - line.stand;
          moved[1] = 0.0f;
          gone = length2 (moved) > fishing_leash * fishing_leash;
        }
        if (fast && gone) {
          if (line.phase == Line::Phase::playing) {
            fishing.creel.last = line.fish;
            fishing.creel.last_lost = true;
            fishing.creel.last_time = now;
            line.bobber = fish_in_water (line, now);
          }
          line.phase = Line::Phase::empty;
          line.since = now;
        }

        const Line::Phase before = line.phase;
        if (tend_line (fishing, now, input.deploy_glider))
          input.deploy_glider = false;
        if (line.phase == Line::Phase::casting ||
            line.phase == Line::Phase::playing ||
            line.phase == Line::Phase::landed) {
          input.drive = 0.0f * one;
          input.turn = 0.0f * one;
          input.boost = 0.0f * one;
          input.run = false;
        }

        const terrain::WaterSheets& sheets =
          generated_world ().water_surface ();
        const auto level_at = [&] (const Vec3& at) {
          return water_surface_at (world (), surface (), &sheets, at,
                                  logic ().m_total_time)
            .numerical_value_in (u::m);
        };
        // A river carries the float downstream while there is water under
        // it and line to give: held back by a line to the bank, but along
        // with the boat it is cast from, which the river carries too.
        if (line.phase == Line::Phase::waiting) {
          const float carried = line.from_boat ? 0.9f : 0.45f;
          Vec3 next = line.bobber + current_at (line.bobber) * (carried * dt);
          Vec3 out = next - line.stand;
          out[1] = 0.0f;
          const float level = level_at (next);
          if (level - ground_height (next) > 0.2f &&
              length (out) < longest_cast + 2.5f) {
            next[1] = level;
            line.bobber = next;
          }
        }

        // The water answers: where the float lands, where it is taken
        // under, about a fish fought in, and where it leaves the water.
        if (before == Line::Phase::casting &&
            line.phase == Line::Phase::waiting)
          splash (line.bobber, 4, 0.0f);
        if (before == Line::Phase::waiting &&
            line.phase == Line::Phase::biting)
          splash (line.bobber, 3, 0.2f);
        if (line.phase == Line::Phase::playing && now >= line.next_splash) {
          line.next_splash = now + 0.18;
          splash (fish_in_water (line, now), 2, 0.5f);
        }
        if (before == Line::Phase::playing &&
            line.phase == Line::Phase::landed)
          splash (line.bobber, 8, 1.0f);
      }

      // What there is to do about the line here, and the last fish.
      void add_fishing_readings (HudState& state) const {
        const Fishing& fishing = logic ().m_fishing;
        const Creel& creel = fishing.creel;
        state.fish_total = creel.kept ();
        const float since =
          static_cast<float> (logic ().m_total_time - creel.last_time);
        if (since < 5.0f) {
          const std::string name = fish_name (creel.last.kind);
          if (creel.last_lost) {
            m_catch_note = "It got away.";
          } else {
            const bool vowel =
              std::string_view ("aeiou").find (name[0]) != std::string::npos;
            m_catch_note = (vowel ? "An " : "A ") + name + ", " +
                           std::to_string (static_cast<int> (
                             std::lround (100.0f * creel.last.length))) +
                           " cm!";
          }
          state.catch_note = m_catch_note.c_str ();
          state.catch_age_s = since;
        }
        if (!angler ())
          return;
        const bool afloat = logic ().m_mode == M_BOAT;
        switch (fishing.line.phase) {
        case Line::Phase::idle:
          if (afloat ? boat_drifting () && water_ahead ()
                     : !state.mushroom_in_reach &&
                         state.cook == HudState::Cook::none &&
                         state.camp != HudState::Camp::lie_down &&
                         water_ahead ())
            state.angle = HudState::Angle::cast;
          break;
        case Line::Phase::waiting:
          state.angle = HudState::Angle::wait;
          break;
        case Line::Phase::biting:
          state.angle = HudState::Angle::strike;
          break;
        case Line::Phase::playing:
          state.angle = HudState::Angle::playing;
          break;
        default:
          break;
        }
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
          if (const std::optional<std::size_t> beacon = beacon_at_hand ();
              input.light_fire && beacon) {
            // A sanctuary's fire is lit on its platform, and shows from
            // the summits that see it.
            Beacon& b = logic ().m_beacons[*beacon];
            (b.burning () ? b.out : b.lit) = now;
            std::cerr << "moppe: places: the beacon on sanctuary " << *beacon
                      << (b.burning () ? " lit, seen from " : " put out")
                      << (b.burning ()
                            ? std::to_string (
                                m_places.sanctuaries[*beacon].seen_from.size ())
                            : std::string ())
                      << (b.burning () ? " others" : "") << std::endl;
          } else if (input.light_fire) {
            Vec3 to_fire = feet - camp.hearth;
            to_fire[1] = 0.0f;
            if (camp.fire && length2 (to_fire) < 3.0f * 3.0f) {
              camp.fire = false;
              camp.fire_time = now;
            } else if (const std::optional<Vec3> ring =
                         hearth_at_hand (m_places, feet)) {
              // An old fire ring takes the fire.
              camp.fire = true;
              camp.hearth = *ring;
              camp.fire_time = now;
              camp.next_spark = camp.next_smoke = now;
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
        tend_beacons ();
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
        if (static_cast<float> (logic ().m_total_time - camp.ate_time) < 6.0f) {
          const std::string with =
            camp.ate_count > 1   ? " and mushrooms"
            : camp.ate_count > 0 ? " and a mushroom"
                                 : "";
          m_meal_note =
            camp.ate_cooked > 1.4f ? "Burnt to a cinder. Still, it is supper."
            : camp.ate_fish
              ? "Fried " + std::string (fish_name (camp.ate.kind)) + with +
                  " by the fire. Lovely."
            : camp.ate_count > 1 ? "Fried mushrooms by the fire. Lovely."
                                 : "A fried mushroom by the fire. Lovely.";
          state.meal = m_meal_note.c_str ();
          state.meal_age_s =
            static_cast<float> (logic ().m_total_time - camp.ate_time);
        }
        // What Space and F do here, and what can be done about the camp
        // without a prompt for it (HudState::thumb).
        if (logic ().m_mode != m_thumb_mode) {
          m_thumb_mode = logic ().m_mode;
          m_thumb_from = logic ().m_total_time;
        }
        // The bike's jets and the boat's tied-back governor are prompted
        // for a while after getting on, until they have been tried once.
        const bool afloat = logic ().m_mode == M_BOAT;
        const bool boosts = logic ().m_mode == M_BIKE || afloat;
        if (boosts && logic ().m_boost_input > 0.1f)
          m_boost_tried[afloat] = true;
        state.thumb_hint = boosts && !m_boost_tried[afloat] &&
                           logic ().m_total_time - m_thumb_from < 15.0;
        // The wheelie is prompted once the jets' prompt is done with, for
        // half a minute after getting on, until one has been ridden.
        if (logic ().m_mode == M_BIKE && session ().bike ().wheeling ())
          m_wheelie_tried = true;
        state.wheelie_hint = logic ().m_mode == M_BIKE && !state.thumb_hint &&
                             !m_wheelie_tried &&
                             logic ().m_total_time - m_thumb_from < 45.0;
        switch (logic ().m_mode) {
        case M_FOOT:
          if (!camp.resting) {
            state.thumb = "Jump";
            state.can_take_down = beside_hammock ();
            Vec3 to_fire = session ().walker ().position () - camp.hearth;
            to_fire[1] = 0.0f;
            state.can_douse_fire = camp.fire && length2 (to_fire) < 3.0f * 3.0f;
            state.can_light_fire =
              !camp.fire &&
              (hearth_ahead ().has_value () ||
               hearth_at_hand (m_places, session ().walker ().position ()));
            if (const std::optional<std::size_t> beacon = beacon_at_hand ()) {
              const bool lit = logic ().m_beacons[*beacon].burning ();
              state.can_douse_fire = lit;
              state.can_light_fire = !lit;
            }
          }
          break;
        case M_BIKE:
          state.thumb = "Boost";
          state.dismount = "Get off";
          state.skids = true;
          state.wheelies = true;
          break;
        case M_MOUNTAIN_BIKE:
          state.thumb = "Hop";
          state.dismount = "Get off";
          state.skids = true;
          break;
        case M_LONG_JOHN:
          state.dismount = "Get off";
          state.skids = true;
          break;
        case M_GLIDER:
          state.thumb = "Flare";
          break;
        case M_JEEP:
          state.thumb = "Brake";
          state.dismount = "Step out";
          break;
        case M_PASSENGER:
          state.dismount = "Step out";
          break;
        case M_TRACTOR:
          state.thumb = "Brake";
          state.dismount = "Climb down";
          break;
        case M_CADDY:
          state.thumb = "Brake";
          state.dismount = "Step out";
          break;
        case M_BOAT:
          state.thumb = "Flat out";
          state.dismount = "Step ashore";
          break;
        case M_HELICOPTER:
          state.thumb = "Climb";
          state.flying = true;
          state.can_step_out = session ().can_step_out_of_helicopter ();
          // Letting go is prompted only once the load hangs near the
          // ground, though the key lets it fall from any height.
          if (logic ().m_sling.hooked &&
              session ().helicopter ().altitude () < sling_hang + 4.0f)
            state.slung = machine_name (logic ().m_sling.load);
          if (const std::optional<Machine> under =
                session ().hook_within_reach ())
            state.hook = machine_name (*under);
          if (logic ().m_sling.hooked)
            state.dismount = "Let go";
          else if (state.hook)
            state.dismount = "Hook on";
          else if (state.can_step_out)
            state.dismount = "Step out";
          break;
        }
        if (logic ().m_mode != M_FOOT || state.mushroom_in_reach)
          return;
        state.cook = supper_at_hand ();
        state.cook_fish =
          camp.pan.on ? camp.pan.has_fish
                      : logic ().m_fishing.creel.kept () > 0;
        if (const std::optional<std::size_t> beacon = beacon_at_hand ();
            beacon && !logic ().m_beacons[*beacon].burning () &&
            !camp.resting)
          state.camp = HudState::Camp::light_beacon;
        else if (camp.resting)
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

      // The places' stones, timber, and buildings; each beacon's laid
      // wood; and the old fire rings, cold until a camp's fire is lit in
      // one.
      void draw_places (render::Renderer& r, const FrameView& frame) {
        const Vec3& camera = frame.camera.position;
        m_places_landscape.draw (r, camera, frame.lighting.time, 0x10000);
        m_places_landscape.draw_beacons (m_world_dl, camera, m_beacon_burn);
        const Camp& camp = logic ().m_camp;
        for (const Vec3& ring : m_places.hearths) {
          if (length2 (ring - camera) > 200.0f * 200.0f)
            continue;
          if (camp.fire_time > -50.0 && length2 (camp.hearth - ring) < 0.04f)
            continue;
          draw_hearth (m_world_dl, ring, 0.0f);
        }
      }

      void draw_camp (render::DrawList& dl) {
        const Camp& camp = logic ().m_camp;
        const double now = logic ().m_total_time;
        if (camp.hung)
          draw_hammock (dl, camp.hammock, hammock_load (camp, now), camp.swing);
        if (camp.fire_time > -50.0)
          draw_hearth (dl, camp.hearth, fire_burn (camp, now));
        if (camp.pan.on) {
          m_pan_contents.clear ();
          for (int i = 0; i < camp.pan.count; ++i)
            if (camp.pan.mushrooms[i] < m_mushrooms.mushroom_count ())
              m_pan_contents.push_back (
                m_mushrooms.site (camp.pan.mushrooms[i]));
          draw_pan (dl,
                    camp.pan.place,
                    camp.pan.yaw,
                    m_pan_contents,
                    pan_cooked (camp.pan, now),
                    camp.pan.has_fish ? &camp.pan.fish : nullptr);
        }
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
              time),
            m_outfit);
        // The basket waits on the ground under the foot of the hammock.
        Vec3 floor = hammock_middle (camp.hammock) +
                     hammock_across (camp.hammock) * 0.75f -
                     hammock_along (camp.hammock) * 0.5f;
        floor[1] = ground_height (floor);
        render_basket_set_down (
          dl, floor, hammock_along (camp.hammock), m_basket_contents);
      }

      // MOPPE_WALK=fish: the walker starts on the bank of the open water
      // nearest the spawn, facing out over it.
      void stage_fish_script () {
        const char* walk = moppe::environment ("MOPPE_WALK");
        if (!walk || std::string_view (walk) != "fish" ||
            logic ().m_mode != M_FOOT)
          return;
        const terrain::WaterSheets& sheets =
          generated_world ().water_surface ();
        const auto depth_at = [&] (const Vec3& at) {
          return still_water_at (world (), surface (), &sheets, at)
                   .numerical_value_in (u::m) -
                 ground_height (at);
        };
        constexpr float step = 4.0f;
        for (int ring = 0; ring < 160; ++ring) {
          const float radius = step * ring;
          const int count =
            std::max (1, static_cast<int> (2.0f * PI * radius / step));
          for (int k = 0; k < count; ++k) {
            const float a = 2.0f * PI * k / count;
            Vec3 stand =
              m_spawn_position + Vec3 (std::cos (a), 0.0f, std::sin (a)) *
                                   radius;
            if (depth_at (stand) > -0.3f)
              continue;
            for (int d = 0; d < 16; ++d) {
              const float b = 2.0f * PI * d / 16;
              const Vec3 out (std::cos (b), 0.0f, std::sin (b));
              if (depth_at (stand + out * 4.5f) < 0.5f ||
                  depth_at (stand + out * 8.0f) < 0.8f)
                continue;
              stand[1] = ground_height (stand) + 0.1f;
              session ().walker ().spawn (moppe::position (stand), out);
              logic ().m_fp_eye = session ().walker ().eye_position ();
              std::cerr << "moppe: fish script: a bank "
                        << length (stand - m_spawn_position)
                        << " m from the spawn" << std::endl;
              return;
            }
          }
        }
        std::cerr << "moppe: fish script: no open water near the spawn\n";
      }

      // MOPPE_WALK=camp: the walker starts a few steps from the fit pair of
      // trees nearest the spawn, on whichever side is clearer to walk in
      // from.
      // MOPPE_WALK=landing|sanctuary|farm: on foot at the landing's
      // jetty, a sanctuary's gate, or a farm, facing it.
      void stage_place_script () {
        const char* walk = moppe::environment ("MOPPE_WALK");
        if (!walk || logic ().m_mode != M_FOOT)
          return;
        const std::string_view script (walk);
        std::optional<std::pair<Vec3, Vec3>> stand;
        if (script == "landing" && m_places.landing) {
          // On the strand, looking past the jetty to the skiff beside it.
          const Landing& landing = *m_places.landing;
          const Vec3 back = landing.jetty.root - landing.berth;
          const Vec3 aside = landing.berth - (landing.jetty.root +
                                              landing.jetty.out *
                                                landing.jetty.length);
          stand = { landing.berth + back * 0.55f + aside * 6.0f,
                    landing.berth };
        } else if (script == "sanctuary" && !m_places.sanctuaries.empty ()) {
          const Sanctuary& s = m_places.sanctuaries.front ();
          stand = { s.centre + s.gate * (s.wall_radius + 4.0f), s.centre };
        } else if (script == "farm" && !m_places.farms.empty () &&
                   !m_places.farms.front ().buildings.empty ()) {
          const Building& b = m_places.farms.front ().buildings.front ();
          stand = { b.at + b.forward * 16.0f +
                      Vec3 (b.forward[2], 0.0f, -b.forward[0]) * 9.0f,
                    b.at };
        }
        if (!stand)
          return;
        Vec3 at = stand->first;
        at[1] = ground_height (at) + 0.1f;
        Vec3 facing = stand->second - at;
        facing[1] = 0.0f;
        session ().walker ().spawn (moppe::position (at), normalized (facing));
        logic ().m_fp_eye = session ().walker ().eye_position ();
        session ().settle_camera (surface ());
        std::cerr << "moppe: walk: " << script << " at " << at[0] << ' '
                  << at[2] << std::endl;
      }

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
      // terrain survey still samples, `river` for a flight down the
      // greatest watercourse from its divide to the sea (at a watching
      // pace, or in MOPPE_FLYOVER_SECONDS), or `none` to begin in play.
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
        if (choice && std::string_view (choice) == "river") {
          const char* seconds = moppe::environment ("MOPPE_FLYOVER_SECONDS");
          std::vector<Vec3> trees;
          trees.reserve (generated_world ().forest ().sites.size ());
          for (const ForestSite& site : generated_world ().forest ().sites) {
            Vec3 tree = position_value (site.position);
            tree[1] = tree_height (site).numerical_value_in (u::m);
            trees.push_back (tree);
          }
          m_cinematic_plan =
            plan_river_flight (surface (),
                               trees,
                               generated_world ().water_surface (),
                               lake_census (),
                               drainage (),
                               rivers (),
                               seconds ? std::strtof (seconds, nullptr) : 0.0f);
          return;
        }
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
              terrain::profile_id (recipe ().generation_profile ())) ||
            recipe ().extent () != WorldParams ().map_size) {
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
        m_opening_arrival =
          reel && reel->arrival ? *reel->arrival : OpeningArrival {};
        const OpeningShot last = arrival ();
        m_opening_shots.push_back (last);
        const Vec3 start = session ().camera ().position ();
        std::cerr << "moppe: opening: the player starts at "
                  << format_opening_shot ("start",
                                          start,
                                          session ().camera ().forward (),
                                          last.fov_deg,
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

      // Whether frames are drawn to keep up with the display. A capture, a
      // recording, or a benchmark draws each frame it is given however
      // long that takes, and its picture must not follow the machine's
      // speed: those hold their resolution.
      bool keeping_pace () const {
        static const bool frame_by_frame =
          moppe::environment ("MOPPE_ORBIT") ||
          moppe::environment ("MOPPE_PAN") ||
          moppe::environment ("MOPPE_RIDE_CAPTURE_DIR") ||
          moppe::environment ("MOPPE_CINEMATIC_CAPTURE_DIR") ||
          moppe::environment ("MOPPE_VIDEO");
        return !frame_by_frame && m_screenshot_path.empty () && !m_benchmark &&
               !m_gazetteer && !m_water_shot;
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
        plan_world_places ();
        grow_global_forest ();
        scatter_boulders ();
        grow_mushrooms ();
        settle_obstacles ();
        build_world_places ();
        park_jeep ();
        park_tractor ();
        park_mountain_bike ();
        park_long_john ();
        park_helicopter ();
        // MOPPE_DEMO=boat: the autopilot takes the boat out instead, and
        // MOPPE_WALK=boat-fish takes it out to fish.
        const char* demo_name = moppe::environment ("MOPPE_DEMO");
        const char* walk_name = moppe::environment ("MOPPE_WALK");
        const bool boat_demo =
          ((demo_name && std::string_view (demo_name) == "boat") ||
           (walk_name && std::string_view (walk_name) == "boat-fish")) &&
          !m_benchmark && !m_gazetteer;
        park_boat (boat_demo);
        park_caddy ();
        // A demo already at the wheel of a machine just parked follows it.
        if (logic ().m_mode != M_FOOT && logic ().m_mode != M_BIKE)
          session ().settle_camera (surface ());
        if (boat_demo) {
          session ().set_mode (M_BOAT);
          logic ().m_cam_mode = CAM_CHASE;
          session ().settle_camera (surface ());
        }
        stage_rescue ();
        // MOPPE_DEMO=helicopter: the autopilot takes it up instead, hooks
        // the jeep, parked beside it, on its line, and flies it off.
        if (const char* demo = moppe::environment ("MOPPE_DEMO");
            demo && std::string_view (demo) == "helicopter" && !m_benchmark &&
            !m_gazetteer) {
          const mov::Helicopter& heli = session ().helicopter ();
          session ().jeep ().place (heli.point (Vec3 (6.0f, 0.0f, 0.0f)),
                                    heli.forward ());
          session ().set_mode (M_HELICOPTER);
          logic ().m_cam_mode = CAM_CHASE;
        }
        stage_camp_script ();
        stage_fish_script ();
        stage_place_script ();
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
        // The chart in the HUD's corner wants the map from the first
        // frame of play.
        const auto said = [] (const char* name, std::string_view value) {
          const char* set = moppe::environment (name);
          return set && std::string_view (set) == value;
        };
        if (!m_gazetteer && !m_benchmark && !m_water_shot &&
            !said ("MOPPE_CHART", "none") && !said ("MOPPE_HUD", "none"))
          paint_map (r);
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
        const terrain::RoadSurface& roads = generated_world ().road_surface ();
        r.set_roads ({ .segment_rows = roads.segment_rows (),
                       .index = roads.index (),
                       .bucket_m = terrain::RoadSurface::bucket_m,
                       .buckets_x = roads.buckets_x (),
                       .buckets_z = roads.buckets_z (),
                       .half_width = roads.formation ().half_width,
                       .shoulder = roads.formation ().shoulder,
                       .crown = roads.formation ().crown,
                       .blend = terrain::RoadSurface::blend_m });
        // The typed water and ground presentations can upload only after
        // set_terrain has established the texture dimensions.
        upload_water (r,
                      generated_world ().water_surface (),
                      spatial::get<terrain::surface_elevation> (surface ()),
                      world ().water_level,
                      world ().map_size);
        upload_surface_readings (r,
                                 surface (),
                                 surface_readings (),
                                 generated_world ().water_surface (),
                                 !m_water_shot);
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
        double& hours = logic ().m_day_hours;
        hours += m_day.pace * dt * logic ().m_day_haste;
        // Two players share one sky: whoever's clock is ahead leads, as
        // when one of them lies down to let the hours run. A little ahead
        // is caught up smoothly, a long way at once.
        const double behind = m_companion_hours - hours;
        if (m_day.turning && behind > 0.0)
          hours +=
            behind > 0.25 ? behind : std::min (behind, behind * 6.0 * dt);
      }

      // -- company -----------------------------------------------------

      // Opens the link to another player (game/companion_link.hh). A
      // launch that cannot have one plays on alone.
      void seek_company (const std::string& address) {
        CompanionLink::Config config { .address = address };
        if (const char* port = moppe::environment ("MOPPE_MULTIPLAYER_PORT"))
          config.port = static_cast<std::uint16_t> (::atoi (port));
        std::string error;
        m_link = CompanionLink::open (config, error);
        if (!m_link) {
          std::cerr << "moppe: multiplayer: " << error << std::endl;
          return;
        }
        std::cerr << "moppe: multiplayer: listening on port " << m_link->port ()
                  << (address.empty () ? ", calling on the local network"
                                       : ", calling " + address)
                  << std::endl;
      }

      std::uint32_t this_world () const {
        return world_tag (static_cast<std::uint32_t> (recipe ().seed ().value),
                          recipe ().resolution (),
                          static_cast<int> (recipe ().generation_profile ()));
      }

      void forget_companion () {
        m_track.clear ();
        m_companion.reset ();
        m_companion_picks.clear ();
        m_companion_hours = -1.0;
        m_companion_met = false;
        m_companion_elsewhere = false;
      }

      void notice (const char* words) {
        m_notice = words;
        m_notice_at = m_link ? m_link->now () : 0.0;
        std::cerr << "moppe: multiplayer: " << words << std::endl;
      }

      // How this player looks now, for the other to draw.
      CompanionSnapshot my_snapshot () const {
        const GameLogicState& state = logic ();
        CompanionSnapshot mine;
        mine.world = this_world ();
        mine.day_hours = m_day.turning ? state.m_day_hours : -1.0;
        mine.mode = state.m_mode;
        mine.bike = vehicle_pose (session ().bike ());
        mine.jeep = jeep_pose (session ().jeep ());
        mine.jeep_velocity = session ().jeep ().velocity ();
        mine.helicopter = helicopter_pose (session ().helicopter ());
        mine.tractor = tractor_pose (session ().tractor ());
        mine.caddy = caddy_pose (session ().caddy ());
        mine.mountain_bike = vehicle_pose (session ().mountain_bike ());
        mine.long_john = vehicle_pose (session ().long_john ());
        mine.boat = boat_pose (session ().boat ());
        mine.boat_velocity = session ().boat ().velocity ();
        mine.in_boat = state.m_mode == M_PASSENGER && state.m_seat_boat;
        if (state.m_mode == M_GLIDER)
          mine.glider = glider_pose (session ().glider ());
        if (state.m_mode == M_FOOT)
          mine.walker = walker_pose (session ().walker (), state.m_look_pitch);
        const Camp& camp = state.m_camp;
        mine.hung = camp.hung;
        mine.hammock = camp.hammock;
        mine.hammock_load = hammock_load (camp, state.m_total_time);
        mine.swing = camp.swing;
        mine.gaze_yaw = camp.gaze_yaw;
        mine.gaze_pitch = camp.gaze_pitch;
        mine.hearth_laid = camp.fire_time > -50.0;
        mine.hearth = camp.hearth;
        mine.fire_burn = fire_burn (camp, state.m_total_time);
        mine.pan_on = camp.pan.on;
        mine.pan_place = camp.pan.place;
        mine.pan_yaw = camp.pan.yaw;
        mine.pan_cooked = pan_cooked (camp.pan, state.m_total_time);
        mine.pan_count = static_cast<std::uint8_t> (camp.pan.count);
        std::copy (std::begin (camp.pan.mushrooms),
                   std::end (camp.pan.mushrooms),
                   std::begin (mine.pan_mushrooms));
        mine.pick_used = static_cast<std::uint32_t> (state.m_basket.used);
        return mine;
      }

      // The companion's picks leave the ground here too, and fill the
      // basket they are seen carrying.
      void hear_of_picks (const CompanionSnapshot& theirs) {
        if (theirs.pick_count < m_companion_picks.size ())
          m_companion_picks.clear ();
        constexpr std::uint32_t unknown = UINT32_MAX;
        // No one has picked more mushrooms than grow.
        if (theirs.pick_count > m_mushrooms.mushroom_count ())
          return;
        m_companion_picks.resize (theirs.pick_count, unknown);
        for (std::size_t i = 0; i < theirs.picks.size (); ++i) {
          const std::size_t ordinal = theirs.picks_from + i;
          const std::uint32_t index = theirs.picks[i];
          if (ordinal >= m_companion_picks.size () ||
              index >= m_mushrooms.mushroom_count ())
            continue;
          m_companion_picks[ordinal] = index;
          m_mushrooms.take (index);
        }
      }

      // Both players begin beside a bike and a jeep parked on the same
      // spots. Whoever came later moves theirs a few steps aside, and
      // themselves with them if they have not yet walked off.
      void make_room (const CompanionSnapshot& theirs) {
        if (m_spectator || logic ().m_mode != M_FOOT)
          return;
        const double here = m_link->now ();
        if (here > theirs.sent ||
            (here == theirs.sent && m_link->player () < theirs.player))
          return;
        mov::Vehicle& bike = session ().bike ();
        Vec3 apart = bike.position () - theirs.bike.position;
        apart[1] = 0.0f;
        if (length2 (apart) > 2.5f * 2.5f)
          return;
        Vec3 heading = bike.orientation ();
        heading[1] = 0.0f;
        heading =
          length2 (heading) > 1e-6f ? normalized (heading) : Vec3 (0, 0, 1);
        const Vec3 side (heading[2], 0, -heading[0]);
        const bool beside = length2 (session ().walker ().position () -
                                     bike.position ()) < 6.0f * 6.0f;
        // Far enough that this jeep, parked beyond the bike, clears
        // theirs.
        Vec3 place = bike.position () - side * 7.0f;
        place[1] = ground_height (place) + 1.2f;
        bike.reset (place);
        bike.set_heading (heading);
        m_park_aside = place - m_spawn_position;
        m_park_aside[1] = 0.0f;
        m_spawn_position = place;
        park_jeep ();
        park_tractor ();
        park_mountain_bike ();
        park_long_john ();
        park_helicopter (theirs.helicopter.position);
        if (logic ().m_mode != M_BOAT)
          park_boat ();
        park_caddy ();
        if (!beside)
          return;
        session ().walker ().spawn (moppe::position (place + side * 1.8f),
                                    heading);
        logic ().m_fp_eye = session ().walker ().eye_position ();
        if (m_opening.active ())
          m_opening.replace_arrival (arrival ());
      }

      Outfit their_outfit () const {
        return m_outfit == Outfit::blue_jacket ? Outfit::green_jacket
                                               : Outfit::blue_jacket;
      }

      // The passenger's seat of the other player's jeep, in the world.
      static Vec3 companion_seat (const CompanionSnapshot& theirs) {
        const JeepPose& jeep = theirs.jeep;
        const Vec3 left = normalized (cross (jeep.up, jeep.forward));
        return jeep.position - left * mov::Jeep::seat[0] +
               jeep.up * mov::Jeep::seat[1] + jeep.forward * mov::Jeep::seat[2];
      }

      // The fore seat of the other player's boat, in the world.
      static Vec3 companion_boat_seat (const CompanionSnapshot& theirs) {
        const BoatPose& boat = theirs.boat;
        const Vec3 left = normalized (cross (boat.up, boat.forward));
        return boat.position + left * mov::Boat::fore[0] +
               boat.up * mov::Boat::fore[1] +
               boat.forward * mov::Boat::fore[2];
      }

      // On foot beside the other player's jeep, or their boat while they
      // are at its helm, and nearer it than anything of one's own to
      // ride: which of them, if either.
      enum class Seat { none, jeep, boat };
      Seat seat_within_reach () const {
        if (!m_companion || m_spectator || logic ().m_mode != M_FOOT)
          return Seat::none;
        const Vec3 feet = session ().walker ().position ();
        const float mine =
          std::min ({ length2 (feet - session ().jeep ().position ()),
                      length2 (feet - session ().bike ().position ()),
                      length2 (feet - session ().boat ().position ()) });
        const float jeep = length2 (feet - m_companion->jeep.position);
        float boat = 1e30f;
        if (m_companion->mode == M_BOAT)
          boat = length2 (feet - companion_boat_seat (*m_companion));
        if (boat < 4.0f * 4.0f && boat < mine && boat < jeep)
          return Seat::boat;
        if (jeep < 4.5f * 4.5f && jeep < mine)
          return Seat::jeep;
        return Seat::none;
      }
      bool can_ride_along () const {
        return seat_within_reach () != Seat::none;
      }

      // Taking the seat beside the other player, or ahead of them in
      // their boat, keeping it as it moves, and leaving it: by the mount
      // key, or when they have gone. Their weight sits in our boat while
      // they ride in it.
      void ride_along (InputFrame& input) {
        GameLogicState& state = logic ();
        session ().boat ().set_passenger (m_companion &&
                                          m_companion->mode == M_PASSENGER &&
                                          m_companion->in_boat);
        if (state.m_mode == M_PASSENGER) {
          if (!m_companion || input.toggle_mount) {
            // Out on the passenger's side, the right; over the boat's
            // gunwale, a step from it.
            const Vec3 left =
              normalized (cross (state.m_seat_up, state.m_seat_heading));
            session ().walker ().spawn (
              moppe::position (state.m_seat_position -
                               left * (state.m_seat_boat ? 1.1f : 1.4f)),
              state.m_seat_heading);
            session ().set_mode (M_FOOT);
            state.m_seat_boat = false;
            input.toggle_mount = false;
            return;
          }
        } else if (input.toggle_mount && can_ride_along ()) {
          state.m_seat_boat = seat_within_reach () == Seat::boat;
          session ().clear_controls ();
          session ().set_mode (M_PASSENGER);
          input.toggle_mount = false;
        } else if (m_companion && !m_rode_along &&
                   moppe::environment ("MOPPE_RIDE_ALONG")) {
          // For captures: take the seat as soon as there is one, from
          // wherever this player stands.
          m_rode_along = true;
          state.m_seat_boat = m_companion->mode == M_BOAT;
          session ().clear_controls ();
          session ().set_mode (M_PASSENGER);
        }
        if (state.m_mode != M_PASSENGER)
          return;
        if (state.m_seat_boat) {
          state.m_seat_position = companion_boat_seat (*m_companion);
          state.m_seat_heading = m_companion->boat.forward;
          state.m_seat_up = m_companion->boat.up;
          state.m_seat_velocity = m_companion->boat_velocity;
          return;
        }
        state.m_seat_position = companion_seat (*m_companion);
        state.m_seat_heading = m_companion->jeep.forward;
        state.m_seat_up = m_companion->jeep.up;
        state.m_seat_velocity = m_companion->jeep_velocity;
      }

      // The other player's machines, for ours to bump into: where they
      // stand, and how they move from frame to frame (the jeep's motion
      // is told).
      void meet_strangers (float elapsed) {
        m_strangers.clear ();
        if (!m_companion) {
          m_strangers_were.reset ();
          return;
        }
        const CompanionSnapshot& theirs = *m_companion;
        const auto stranger = [&] (Machine machine,
                                   const Vec3& position,
                                   const Vec3& forward,
                                   const Vec3& up) {
          MachineBody body;
          body.machine = machine;
          body.position = position;
          body.forward = forward;
          body.up = up;
          body.mass = session ().machine_body (machine).mass;
          body.ours = false;
          const std::size_t i = static_cast<std::size_t> (machine);
          if (m_strangers_were && elapsed > 0.0f) {
            const Vec3 moved = position - (*m_strangers_were)[i];
            if (length2 (moved) < 25.0f)
              body.velocity = moved * (1.0f / elapsed);
          }
          m_strangers.push_back (body);
        };
        stranger (Machine::bike,
                  theirs.bike.position,
                  theirs.bike.render_orientation,
                  theirs.bike.render_normal);
        stranger (Machine::jeep,
                  theirs.jeep.position,
                  theirs.jeep.forward,
                  theirs.jeep.up);
        stranger (Machine::tractor,
                  theirs.tractor.position,
                  theirs.tractor.forward,
                  theirs.tractor.up);
        stranger (Machine::caddy,
                  theirs.caddy.position,
                  theirs.caddy.forward,
                  theirs.caddy.up);
        stranger (Machine::helicopter,
                  theirs.helicopter.position,
                  theirs.helicopter.forward,
                  theirs.helicopter.up);
        stranger (Machine::mountain_bike,
                  theirs.mountain_bike.position,
                  theirs.mountain_bike.render_orientation,
                  theirs.mountain_bike.render_normal);
        stranger (Machine::long_john,
                  theirs.long_john.position,
                  theirs.long_john.render_orientation,
                  theirs.long_john.render_normal);
        stranger (Machine::boat,
                  theirs.boat.position,
                  theirs.boat.forward,
                  theirs.boat.up);
        std::array<Vec3, machine_count> were;
        for (const MachineBody& body : m_strangers)
          were[static_cast<std::size_t> (body.machine)] = body.position;
        m_strangers_were = were;
        m_strangers[static_cast<std::size_t> (Machine::jeep)].velocity =
          theirs.jeep_velocity;
        m_strangers[static_cast<std::size_t> (Machine::boat)].velocity =
          theirs.boat_velocity;
      }

      // Once a frame: hears what the other player has sent, works out how
      // they look just now, and tells them how this one does.
      void keep_company (float elapsed) {
        if (!m_link)
          return;
        m_arrived.clear ();
        m_link->receive (m_arrived);
        const double now = m_link->now ();
        // The poses told are the simulation's, which moves on in whole
        // steps, now two to a frame and now three. They are stamped by a
        // clock that moves with it, so that the other machine sees even
        // motion however unevenly the frames fall.
        m_pose_clock =
          m_pose_heard < 0.0
            ? now
            : m_pose_clock + (m_stepped > 0.0 ? m_stepped : now - m_pose_heard);
        m_pose_heard = now;
        m_stepped = 0.0;
        const bool was_present = m_track.present (now);
        for (CompanionSnapshot& theirs : m_arrived) {
          if (theirs.world != this_world ()) {
            if (!m_companion_elsewhere) {
              forget_companion ();
              m_companion_elsewhere = true;
              notice ("Someone is playing, but in another world");
              std::cerr << "moppe: multiplayer: launch both games with the "
                           "same --seed and terrain options"
                        << std::endl;
            }
            continue;
          }
          m_companion_elsewhere = false;
          if (!m_track.present (now))
            m_companion_picks.clear ();
          hear_of_picks (theirs);
          m_companion_hours = theirs.day_hours;
          if (!m_companion_met) {
            m_companion_met = true;
            // So that the two can be told apart, and each machine agrees
            // on who is who, the player with the greater number changes
            // into the green jacket.
            m_outfit = m_link->player () > theirs.player ? Outfit::green_jacket
                                                         : Outfit::blue_jacket;
            make_room (theirs);
          }
          m_track.push (std::move (theirs), now);
        }
        const bool present = m_track.present (now);
        if (present && !was_present) {
          notice ("Your companion has arrived");
          std::cerr << "moppe: multiplayer: companion at "
                    << m_link->companion_address () << std::endl;
        } else if (!present && was_present) {
          notice ("Your companion has gone");
          m_companion_hours = -1.0;
          m_companion_met = false;
        }
        m_companion = m_track.advance (now, elapsed);
        meet_strangers (elapsed);

        // A spectator has no one to show.
        if (m_spectator || !m_link->due ())
          return;
        CompanionSnapshot mine = my_snapshot ();
        const std::vector<std::uint32_t>& picked = logic ().m_basket.picked;
        mine.pick_count = static_cast<std::uint32_t> (picked.size ());
        mine.picks_from =
          companion_pick_window_start (picked.size (), m_snapshots_sent++);
        for (std::size_t i = mine.picks_from;
             i < picked.size () && mine.picks.size () < companion_pick_window;
             ++i)
          mine.picks.push_back (picked[i]);
        m_link->send (std::move (mine), m_pose_clock);
      }

      // The other player as the world's second figure: their bike where
      // they left or ride it, themselves on foot, in the hammock, or
      // under the wing, and their camp.
      void
      draw_companion (render::Renderer& r, render::DrawList& dl, float time) {
        if (!m_companion)
          return;
        const CompanionSnapshot& theirs = *m_companion;
        m_blob.draw (dl, surface (), theirs.bike.position, 2.2f);
        render_vehicle (
          r, dl, theirs.bike, theirs.mode == M_BIKE, 0x3000, their_outfit ());
        m_blob.draw (dl, surface (), theirs.jeep.position, 3.4f);
        // This player beside them, unless seeing through their own eyes.
        render_jeep (r,
                     dl,
                     theirs.jeep,
                     { .driver = theirs.mode == M_JEEP,
                       .passenger = logic ().m_mode == M_PASSENGER &&
                                    !logic ().m_seat_boat &&
                                    logic ().m_cam_mode != CAM_HELMET,
                       .driver_outfit = their_outfit (),
                       .passenger_outfit = m_outfit },
                     0x6000);
        m_blob.draw (dl, surface (), theirs.helicopter.position, 4.5f);
        render_helicopter (r, theirs.helicopter, 0x7000);
        m_blob.draw (dl, surface (), theirs.tractor.position, 2.8f);
        render_tractor (r,
                        dl,
                        theirs.tractor,
                        theirs.mode == M_TRACTOR
                          ? std::optional<Outfit> (their_outfit ())
                          : std::nullopt,
                        0x9000);
        m_blob.draw (dl, surface (), theirs.caddy.position, 3.6f);
        render_caddy (r,
                      dl,
                      theirs.caddy,
                      { .driver = theirs.mode == M_CADDY,
                        .driver_outfit = their_outfit () },
                      0xB000);
        render_boat (r,
                     dl,
                     theirs.boat,
                     theirs.mode == M_BOAT
                       ? std::optional<Outfit> (their_outfit ())
                       : std::nullopt,
                     0xF800,
                     std::nullopt,
                     time,
                     logic ().m_mode == M_PASSENGER &&
                         logic ().m_seat_boat &&
                         logic ().m_cam_mode != CAM_HELMET
                       ? std::optional<Outfit> (m_outfit)
                       : std::nullopt);
        m_blob.draw (dl, surface (), theirs.mountain_bike.position, 1.3f);
        render_mountain_bike (r,
                              dl,
                              theirs.mountain_bike,
                              theirs.mode == M_MOUNTAIN_BIKE
                                ? std::optional<Outfit> (their_outfit ())
                                : std::nullopt,
                              0xE000);
        m_blob.draw (dl, surface (), theirs.long_john.position, 2.0f);
        render_long_john (r,
                          dl,
                          theirs.long_john,
                          theirs.mode == M_LONG_JOHN
                            ? std::optional<Outfit> (their_outfit ())
                            : std::nullopt,
                          0xE100);
        if (theirs.hung)
          draw_hammock (dl, theirs.hammock, theirs.hammock_load, theirs.swing);
        if (theirs.hearth_laid)
          draw_hearth (dl, theirs.hearth, theirs.fire_burn);

        m_companion_basket.clear ();
        for (std::size_t i = theirs.pick_used; i < m_companion_picks.size ();
             ++i)
          if (m_companion_picks[i] < m_mushrooms.mushroom_count ())
            m_companion_basket.push_back (
              m_mushrooms.site (m_companion_picks[i]));
        if (theirs.pan_on) {
          m_pan_contents.clear ();
          for (int i = 0; i < theirs.pan_count; ++i)
            if (theirs.pan_mushrooms[i] < m_mushrooms.mushroom_count ())
              m_pan_contents.push_back (
                m_mushrooms.site (theirs.pan_mushrooms[i]));
          draw_pan (dl,
                    theirs.pan_place,
                    theirs.pan_yaw,
                    m_pan_contents,
                    theirs.pan_cooked);
        }
        if (theirs.walker && theirs.lying_down ()) {
          figure::draw (
            dl,
            pose_resting (
              theirs.hammock,
              theirs.swing,
              resting_gaze (theirs.hammock, theirs.gaze_yaw, theirs.gaze_pitch),
              time),
            their_outfit ());
          Vec3 floor = hammock_middle (theirs.hammock) +
                       hammock_across (theirs.hammock) * 0.75f -
                       hammock_along (theirs.hammock) * 0.5f;
          floor[1] = ground_height (floor);
          render_basket_set_down (
            dl, floor, hammock_along (theirs.hammock), m_companion_basket);
        } else if (theirs.walker) {
          m_blob.draw (
            dl, surface (), theirs.walker->position + Vec3 (0, 0.5f, 0), 0.8f);
          render_walker (
            dl, *theirs.walker, time, m_companion_basket, their_outfit ());
        }
        if (theirs.glider) {
          m_blob.draw (dl, surface (), theirs.glider->position, 3.4f);
          render_glider (r, dl, *theirs.glider, time, 0x4000, their_outfit ());
        }
      }

      // A burning beacon near the eye lights what stands round it.
      void light_beacons (FrameView& frame) const {
        for (std::size_t i = 0;
             i < m_places.sanctuaries.size () && i < m_beacon_burn.size ();
             ++i) {
          if (m_beacon_burn[i] <= 0.01f)
            continue;
          const Vec3 lamp =
            m_places.sanctuaries[i].hearth + Vec3 (0.0f, 0.7f, 0.0f);
          FrameLighting& lighting = frame.lighting;
          if (lighting.lamp_reach > 0.0f &&
              length2 (lighting.lamp_position - frame.camera.position) <=
                length2 (lamp - frame.camera.position))
            continue;
          lighting.lamp_position = lamp;
          lighting.lamp_reach = 1.8f * fire_light_reach;
          lighting.lamp_color = scale_display (
            fire_light_color (m_beacon_burn[i], lighting.time),
            1.4f * (1.0f - 0.25f * lighting.daylight));
        }
      }

      // The renderer has one lamp. With two fires burning it is the one
      // nearer the eye.
      void light_companion_fire (FrameView& frame) const {
        if (!m_companion || !m_companion->hearth_laid ||
            m_companion->fire_burn <= 0.01f)
          return;
        Camp theirs;
        theirs.hearth = m_companion->hearth;
        const Vec3 lamp = fire_light_position (theirs);
        FrameLighting& lighting = frame.lighting;
        if (lighting.lamp_reach > 0.0f &&
            length2 (lighting.lamp_position - frame.camera.position) <=
              length2 (lamp - frame.camera.position))
          return;
        lighting.lamp_position = lamp;
        lighting.lamp_reach = fire_light_reach;
        lighting.lamp_color = scale_display (
          fire_light_color (m_companion->fire_burn, lighting.time),
          1.0f - 0.25f * lighting.daylight);
      }

      // One waymark: where `at` falls in the frame, or which way it lies.
      // Nothing is marked nearer than `near`, and it fades in to `far`.
      static void add_waymark (HudState& state,
                               const FrameView& frame,
                               int width,
                               int height,
                               const char* name,
                               unsigned vehicles,
                               const Vec3& at,
                               float near,
                               float far,
                               const DisplayColor& tint) {
        if (state.waymark_count >= static_cast<int> (state.waymarks.size ()))
          return;
        const FrameCamera& camera = frame.camera;
        const Vec3 to = at - camera.position;
        const float metres = length (to);
        const float shown =
          std::clamp ((metres - near) / (far - near), 0.0f, 1.0f);
        if (shown <= 0.0f)
          return;
        const float across = dot (to, camera.right);
        const float above = dot (to, camera.up);
        const float ahead = dot (to, camera.frame_forward);
        const float half =
          std::tan (0.5f * camera.field_of_view.numerical_value_in (u::rad));
        HudState::Waymark mark;
        mark.name = name;
        mark.vehicles = vehicles;
        mark.position = at;
        mark.metres = metres;
        mark.alpha = shown;
        mark.red = tint.red;
        mark.green = tint.green;
        mark.blue = tint.blue;
        const float x =
          ahead > 0.1f ? across / (ahead * half * camera.aspect) : 0.0f;
        const float y = ahead > 0.1f ? above / (ahead * half) : 0.0f;
        if (ahead > 0.1f && std::fabs (x) < 0.86f && std::fabs (y) < 0.80f) {
          // In view: a small arrow over it, pointing down at it.
          mark.in_view = true;
          mark.x = (0.5f + 0.5f * x) * width;
          mark.y = (0.5f - 0.5f * y) * height - 12.0f;
          mark.angle = -1.5707963f;
        } else {
          // Out of view: at the edge, toward where turning would find it.
          float angle = std::atan2 (above, across);
          if (std::fabs (across) + std::fabs (above) < 1e-3f)
            angle = -1.5707963f;
          mark.angle = angle;
          mark.x = 0.5f * width + 0.43f * width * std::cos (angle);
          mark.y = 0.5f * height - 0.40f * height * std::sin (angle);
        }
        state.waymarks[state.waymark_count++] = mark;
      }

      // The way to the other player and to whatever of one's own is
      // parked out of reach.
      void add_waymarks (HudState& state,
                         const FrameView& frame,
                         int width,
                         int height) const {
        if (m_spectator)
          return;
        const Mode mode = logic ().m_mode;
        const DisplayColor pale (0.96f, 0.96f, 0.96f);
        if (m_companion && mode != M_PASSENGER &&
            m_companion->mode != M_PASSENGER) {
          const CompanionSnapshot& theirs = *m_companion;
          Vec3 at = theirs.bike.position;
          if (theirs.walker)
            at = theirs.walker->position;
          else if (theirs.glider)
            at = theirs.glider->position;
          else if (theirs.mode == M_JEEP)
            at = theirs.jeep.position;
          else if (theirs.mode == M_HELICOPTER)
            at = theirs.helicopter.position + Vec3 (0, 1.0f, 0);
          else if (theirs.mode == M_TRACTOR)
            at = theirs.tractor.position + Vec3 (0, 0.8f, 0);
          else if (theirs.mode == M_CADDY)
            at = theirs.caddy.position + Vec3 (0, 1.0f, 0);
          else if (theirs.mode == M_MOUNTAIN_BIKE)
            at = theirs.mountain_bike.position + Vec3 (0, 1.4f, 0);
          else if (theirs.mode == M_LONG_JOHN)
            at = theirs.long_john.position + Vec3 (0, 1.4f, 0);
          else if (theirs.mode == M_BOAT)
            at = theirs.boat.position + Vec3 (0, 1.2f, 0);
          // In the colour of the jacket they wear.
          DisplayColor jacket = pale;
          for (std::size_t c = 0; c < figure::colours.size (); ++c)
            if (std::string_view (figure::colour_names[c]) == "jacket")
              jacket = mix_display (
                figure::outfit_colour (c, their_outfit ()), pale, 0.35f);
          add_waymark (state,
                       frame,
                       width,
                       height,
                       "companion",
                       0,
                       at + Vec3 (0, 2.0f, 0),
                       12.0f,
                       24.0f,
                       jacket);
        }
        using Mark = HudState::Waymark;
        if (mode != M_BIKE && !session ().can_drop_bike ())
          add_waymark (state,
                       frame,
                       width,
                       height,
                       "",
                       Mark::Bike,
                       session ().bike ().position () + Vec3 (0, 1.6f, 0),
                       30.0f,
                       50.0f,
                       pale);
        if (mode != M_JEEP)
          add_waymark (state,
                       frame,
                       width,
                       height,
                       "",
                       Mark::Jeep,
                       session ().jeep ().position () + Vec3 (0, 1.9f, 0),
                       30.0f,
                       50.0f,
                       pale);
        if (mode != M_TRACTOR)
          add_waymark (state,
                       frame,
                       width,
                       height,
                       "",
                       Mark::Tractor,
                       session ().tractor ().position () + Vec3 (0, 2.4f, 0),
                       30.0f,
                       50.0f,
                       pale);
        if (mode != M_CADDY)
          add_waymark (state,
                       frame,
                       width,
                       height,
                       "",
                       Mark::Caddy,
                       session ().caddy ().position () + Vec3 (0, 2.0f, 0),
                       30.0f,
                       50.0f,
                       pale);
        if (mode != M_MOUNTAIN_BIKE)
          add_waymark (state,
                       frame,
                       width,
                       height,
                       "",
                       Mark::MountainBike,
                       session ().mountain_bike ().position () +
                         Vec3 (0, 1.5f, 0),
                       30.0f,
                       50.0f,
                       pale);
        if (mode != M_LONG_JOHN)
          add_waymark (state,
                       frame,
                       width,
                       height,
                       "",
                       Mark::LongJohn,
                       session ().long_john ().position () +
                         Vec3 (0, 1.5f, 0),
                       30.0f,
                       50.0f,
                       pale);
        if (mode != M_BOAT)
          add_waymark (state,
                       frame,
                       width,
                       height,
                       "",
                       Mark::Boat,
                       session ().boat ().position () + Vec3 (0, 1.4f, 0),
                       30.0f,
                       50.0f,
                       pale);
        if (mode != M_HELICOPTER)
          add_waymark (state,
                       frame,
                       width,
                       height,
                       "",
                       Mark::Helicopter,
                       session ().helicopter ().position () + Vec3 (0, 3.4f, 0),
                       30.0f,
                       50.0f,
                       pale);
        bundle_waymarks (state);
      }

      void add_company_readings (HudState& state) const {
        state.can_ride_along = can_ride_along ();
        if (!m_link || !m_notice)
          return;
        state.notice = m_notice;
        state.notice_age_s = static_cast<float> (m_link->now () - m_notice_at);
      }

      // -- simulation --------------------------------------------------

      void tick (float elapsed) override {
        if (!m_ready) {
          m_simulation_clock.reset ();
          return;
        }
        keep_company (elapsed);

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
          // The time a video lets pass before it begins is not looked at,
          // so it goes by many steps to a drawn frame, until two seconds
          // before the first frame taken, which gives the picture's
          // history time to settle.
          // A remote control's taps are timed by drawn frames, so a
          // driven video keeps to one step each.
          int steps = 1;
          if (m_video_plan && !m_video_done &&
              !moppe::environment ("MOPPE_CONTROL_FILE")) {
            const int first =
              static_cast<int> (std::lround (m_video_plan->start * 60.0f));
            steps = std::clamp (first - 120 - m_video_render_frame, 1, 20);
          }
          for (int step = 0; step < steps; ++step)
            tick_simulation (1.0f / 60.0f);
          m_video_render_frame += steps - 1;
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
        if (script == "fish") {
          // Cast, strike at the take, and cast again; with two fish in
          // the creel, turn from the water, light a fire, fry one, and eat.
          const Line& line = logic ().m_fishing.line;
          if (m_supper_since < 0.0f && logic ().m_fishing.creel.kept () >= 2 &&
              !line_out (line))
            m_supper_since = t;
          if (m_supper_since >= 0.0f) {
            const float since = t - m_supper_since;
            input.look_yaw = since < 1.2f ? PI / 1.2f * dt : 0.0f;
            input.drive = since > 1.2f && since < 3.4f ? 1.0f : 0.0f;
            input.look_pitch = std::clamp (
              -0.55f - logic ().m_look_pitch, -1.0f * dt, 1.0f * dt);
            input.light_fire =
              press (m_supper_since + 4.2f) > 0.0f && !logic ().m_camp.fire;
            input.deploy_glider = press (7.5f + m_supper_since) > 0.0f ||
                                  press (19.0f + m_supper_since) > 0.0f;
            return input;
          }
          input.look_pitch = std::clamp (
            -0.2f - logic ().m_look_pitch, -1.0f * dt, 1.0f * dt);
          if (line_out (line))
            m_fish_script_rest = t;
          const float age =
            static_cast<float> (logic ().m_total_time - line.since);
          input.deploy_glider =
            (!line_out (line) && t - m_fish_script_rest > 1.5f) ||
            (line.phase == Line::Phase::biting && age > 0.4f);
          return input;
        }
        if (script == "boat-fish") {
          // Out from the shore, the engine stopped to drift, a look out
          // over the side, and a cast; strike at the take and land two.
          // Then cast again and open the throttle, which winds it in.
          const Line& line = logic ().m_fishing.line;
          const float age =
            static_cast<float> (logic ().m_total_time - line.since);
          if (t < 5.0f || m_boat_fish_off >= 0.0f) {
            input.drive = 1.0f;
            return input;
          }
          input.look_yaw =
            std::clamp (1.25f - logic ().m_look_yaw, -1.0f * dt, 1.0f * dt);
          input.look_pitch =
            std::clamp (0.3f - logic ().m_look_pitch, -1.0f * dt, 1.0f * dt);
          if (line_out (line))
            m_fish_script_rest = t;
          if (logic ().m_fishing.creel.kept () >= 2 &&
              line.phase == Line::Phase::waiting && age > 3.0f)
            m_boat_fish_off = t;
          input.deploy_glider =
            (!line_out (line) && t - m_fish_script_rest > 1.5f &&
             boat_drifting ()) ||
            (line.phase == Line::Phase::biting && age > 0.4f);
          return input;
        }
        // The mount key is a toggle, taken once a press.
        const auto tap = [t, dt] (float at) { return t >= at && t < at + dt; };
        if (script == "haul") {
          // Take up the rope, walk off with the tractor in tow, turning a
          // little, let go, and climb up.
          input.toggle_mount = tap (1.0f) || tap (12.5f) || tap (14.0f);
          input.drive = t > 1.6f && t < 11.5f ? 1.0f : 0.0f;
          input.look_yaw = t > 4.0f && t < 8.0f ? 0.12f * dt : 0.0f;
          return input;
        }
        if (script == "bump") {
          // Into the jeep, into the tractor's side, back off, and again.
          input.toggle_mount = tap (0.5f);
          input.drive = t > 1.0f && t < 4.2f    ? 1.0f
                        : t > 5.5f && t < 7.5f  ? -1.0f
                        : t > 8.0f && t < 11.0f ? 1.0f
                                                : 0.0f;
          input.boost = (t > 4.2f && t < 5.5f) || t > 11.0f ? 1.0f : 0.0f;
          return input;
        }
        if (script == "forage" || script == "supper") {
          // "supper" forages three mushrooms, walks a few steps on,
          // lights a fire, fries them, and eats.
          if (script == "supper" && m_supper_since < 0.0f &&
              logic ().m_basket.total () >= 3)
            m_supper_since = t;
          if (m_supper_since >= 0.0f) {
            const float since = t - m_supper_since;
            input.drive = since < 2.5f ? 1.0f : 0.0f;
            input.look_pitch = std::clamp (
              -0.55f - logic ().m_look_pitch, -1.0f * dt, 1.0f * dt);
            input.light_fire =
              press (m_supper_since + 3.2f) > 0.0f && !logic ().m_camp.fire;
            input.deploy_glider = press (6.5f + m_supper_since) > 0.0f ||
                                  press (18.0f + m_supper_since) > 0.0f;
            return input;
          }
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
            const float turn = ahead < 0.0f ? (leftward >= 0.0f ? -1.0f : 1.0f)
                                            : -std::asin (leftward);
            input.look_yaw = std::clamp (turn, -2.5f * dt, 2.5f * dt);
            input.look_pitch =
              std::clamp (-0.6f - logic ().m_look_pitch, -1.0f * dt, 1.0f * dt);
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
        } else if (script == "turn") {
          // Standing, a slow look aside that the head and chest follow,
          // a look round further that steps the feet round, and a quick
          // spin twice round that they step after; then a walk with a
          // sharp look aside, and a run swung round in an arc.
          const auto during = [t] (float from, float to) {
            return t >= from && t < to;
          };
          input.drive = during (12.0f, 20.0f) ? 1.0f : 0.0f;
          input.run = during (16.0f, 20.0f);
          input.look_yaw = (during (1.0f, 3.0f)    ? 0.5f
                            : during (4.5f, 5.5f)  ? 1.6f
                            : during (7.5f, 8.5f)  ? -4.0f * PI
                            : during (13.5f, 13.8f) ? -5.2f
                            : during (17.0f, 19.5f) ? 1.3f
                                                    : 0.0f) *
                           dt;
          return input;
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
      // ridden bike or mountainbike, the glider, the driven jeep or boat,
      // or whatever hangs under the helicopter, to judge the rider and the
      // machine.
      void ride_capture_camera (float dt) {
        static const std::string_view view = [] {
          const char* name = moppe::environment ("MOPPE_RIDE_CAMERA");
          return std::string_view (name ? name : "");
        }();
        m_film_fov.reset ();
        if (logic ().m_mode == M_BOAT && boat_film_camera (view, dt))
          return;
        // Flying, it is the machine on the line it stands beside.
        const bool slung =
          logic ().m_mode == M_HELICOPTER && logic ().m_sling.hooked;
        if (view.empty () || logic ().m_mode == M_FOOT ||
            (logic ().m_mode == M_HELICOPTER && !slung))
          return;
        if (slung) {
          const MachineBody load =
            session ().machine_body (logic ().m_sling.load);
          Vec3 heading = load.forward;
          heading[1] = 0.0f;
          heading =
            length2 (heading) > 1e-6f ? normalized (heading) : Vec3 (0, 0, 1);
          const Vec3 right (heading[2], 0.0f, -heading[0]);
          const Vec3 at = load.position + Vec3 (0, 0.5f, 0);
          const Vec3 from = view == "front"
                              ? heading * 7.5f + right * 2.0f
                              : right * 7.5f + heading * 0.6f;
          session ().camera ().place (at + from + Vec3 (0, 0.5f, 0), at);
          return;
        }
        const bool gliding = logic ().m_mode == M_GLIDER;
        const bool tractor = logic ().m_mode == M_TRACTOR;
        const bool caddy = logic ().m_mode == M_CADDY;
        const bool boat = logic ().m_mode == M_BOAT;
        const bool driving =
          logic ().m_mode == M_JEEP || tractor || caddy || boat;
        const bool pedalled = game::pedalled (logic ().m_mode);
        const auto& bike = session ().ridden_bike ();
        const Vec3 car = tractor ? session ().tractor ().position ()
                         : caddy ? session ().caddy ().position ()
                         : boat  ? session ().boat ().position ()
                                 : session ().jeep ().position ();
        Vec3 heading = gliding   ? session ().glider ().heading ()
                       : tractor ? session ().tractor ().forward ()
                       : caddy   ? session ().caddy ().forward ()
                       : boat    ? session ().boat ().forward ()
                       : driving ? session ().jeep ().forward ()
                                 : bike.render_orientation ();
        heading[1] = 0.0f;
        heading = normalized (heading);
        const Vec3 right (heading[2], 0.0f, -heading[0]);
        const Vec3 at = gliding
                          ? session ().glider ().position () - Vec3 (0, 0.9f, 0)
                        : driving ? car + Vec3 (0, tractor ? 0.9f : 0.5f, 0)
                        : pedalled
                          ? bike.render_position () + Vec3 (0, 0.55f, 0)
                          : bike.render_position () + Vec3 (0, 0.4f, 0);
        const float away = gliding    ? 7.0f
                           : caddy    ? 7.5f
                           : boat     ? 7.0f
                           : driving  ? 6.0f
                           : pedalled ? 3.4f
                                      : 4.5f;
        const Vec3 from = view == "front"
                            ? heading * away + right * (away * 0.27f)
                            : right * away + heading * 0.6f;
        session ().camera ().place (at + from + Vec3 (0, 0.5f, 0), at);
      }

      // Cameras for filming the boat (MOPPE_RIDE_CAMERA): `drone` follows
      // high behind, `aerial` far off with a long lens, `orbit` circles it,
      // `top` looks straight down on it, `fpv` races close behind and low,
      // `hull` is mounted low on its side looking back at the helmsman,
      // `stern` over the long-tail looking ahead, `helm` is the
      // helmsman's eyes, and `flyby` stands on the water ahead of it and
      // watches it pass on a long lens. MOPPE_RIDE_CAMERA_ANGLE
      // swings the following ones round, degrees to the right of behind.
      bool boat_film_camera (std::string_view view, float dt) {
        static const float angle = [] {
          const char* value = moppe::environment ("MOPPE_RIDE_CAMERA_ANGLE");
          return value ? std::strtof (value, nullptr) * PI / 180.0f : 0.0f;
        }();
        const mov::Boat& boat = session ().boat ();
        const Vec3 at = boat.position () + Vec3 (0, 0.6f, 0);
        Vec3 heading = boat.forward ();
        heading[1] = 0.0f;
        heading = normalized (heading);
        // The subject eased, so the waves' heave and the hull's yawing
        // do not shake a camera that is meant to be flown.
        const bool fresh = m_film_time <= 0.0f;
        const auto ease = [dt] (float rate) {
          return 1.0f - std::exp (-rate * dt);
        };
        if (fresh) {
          m_film_at = at;
          m_film_heading = heading;
        }
        m_film_at += (at - m_film_at) * ease (5.0f);
        m_film_heading = normalized (
          m_film_heading + (heading - m_film_heading) * ease (0.9f));
        m_film_time += dt;
        const Vec3 h = m_film_heading;
        const Vec3 r (h[2], 0.0f, -h[0]);
        const auto around = [&] (float a) {
          return -h * std::cos (a) - r * std::sin (a);
        };
        const Vec3 up (0, 1, 0);
        Vec3 eye, look;
        float fov = 55.0f;
        bool clear = true;
        if (view == "drone") {
          eye = m_film_at + around (angle) * 26.0f + up * 13.0f;
          look = m_film_at + h * 8.0f;
          fov = 48.0f;
        } else if (view == "aerial") {
          eye = m_film_at + around (angle) * 75.0f + up * 48.0f;
          look = m_film_at + h * 6.0f;
          fov = 30.0f;
        } else if (view == "orbit") {
          eye = m_film_at + around (angle + 0.25f * m_film_time) * 15.0f +
                up * 3.5f;
          look = m_film_at;
          fov = 50.0f;
        } else if (view == "top") {
          eye = m_film_at + up * 30.0f - h * 3.0f;
          look = m_film_at + h * 2.0f;
          fov = 40.0f;
        } else if (view == "fpv") {
          const float sway = 0.35f * std::sin (0.45f * m_film_time);
          eye = m_film_at + around (angle + sway) * 5.0f + up * 1.0f;
          look = m_film_at + h * 14.0f + up * 0.3f;
          fov = 84.0f;
        } else if (view == "hull") {
          eye = boat.point (Vec3 (-1.35f, 0.45f, 1.3f));
          look = boat.point (Vec3 (0.0f, 0.85f, -1.0f));
          fov = 70.0f;
          clear = false;
        } else if (view == "stern") {
          eye = boat.point (Vec3 (0.4f, 1.45f, -2.9f));
          look = boat.point (Vec3 (0.0f, 0.5f, 9.0f));
          fov = 72.0f;
          clear = false;
        } else if (view == "flyby") {
          // A new stand whenever it has gone by and well off.
          const Vec3 off = m_film_stand - at;
          if (fresh || (length (off) > 65.0f && dot (off, h) < 0.0f)) {
            const float side = std::sin (angle) >= 0.0f ? 1.0f : -1.0f;
            m_film_stand = at + heading * 45.0f + r * (side * 8.0f);
            m_film_stand[1] =
              std::max (boat_depth_at (m_film_stand) +
                          ground_height (m_film_stand),
                        ground_height (m_film_stand)) +
              1.1f;
          }
          // A long lens that keeps the boat the same size as it nears.
          eye = m_film_stand;
          look = at;
          fov = std::clamp (2.0f * std::atan (5.0f / length (at - eye)) *
                              180.0f / PI,
                            8.0f, 60.0f);
        } else if (view == "helm") {
          // The helmsman's eyes, through play's own camera.
          logic ().m_cam_mode = CAM_HELMET;
          m_film_fov = 75.0f;
          return true;
        } else {
          return false;
        }
        session ().camera ().place (eye, look);
        if (clear)
          session ().camera ().keep_above (surface ());
        m_film_fov = fov;
        return true;
      }

      void tick_simulation (float dt) {
        MOPPE_PROFILE_ZONE ("MoppeGame::tick_simulation");
        m_stepped += dt;
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
        static const bool demo_wheelies = [] {
          const char* name = moppe::environment ("MOPPE_DEMO");
          return name && std::string_view (name) == "wheelie";
        }();
        m_trunk_field.focus (session ().subject_position ());
        static const bool orbit = moppe::environment ("MOPPE_ORBIT") != 0 ||
                                  moppe::environment ("MOPPE_PAN") != 0;
        if (m_spectator) {
          // The mouse turns the spectator directly; the right stick's look
          // arrives with the frame.
          m_spectator->yaw += input.look_yaw;
          m_spectator->pitch =
            std::clamp (m_spectator->pitch + input.look_pitch, -1.45f, 1.45f);
          input = {};
        }
        static const char* walk_script = moppe::environment ("MOPPE_WALK");
        // The bump script drives the jeep too, and boat-fish the boat.
        if (walk_script && !orbit &&
            (logic ().m_mode == M_FOOT ||
             std::string_view (walk_script) == "bump" ||
             std::string_view (walk_script) == "boat-fish"))
          input = scripted_walk (walk_script, dt);
        else if (demo && !m_water_inspection && !orbit) {
          // The autopilot rides; the player may still look around.
          const float look_yaw = input.look_yaw;
          const float look_pitch = input.look_pitch;
          if (logic ().m_mode == M_HELICOPTER) {
            // Spool up, climb out, hook what is under it, and cruise in
            // long easy curves.
            const float t = total_time;
            input = {
              .turn = t > 9.0f ? 0.45f * std::sin ((t - 9.0f) * 0.22f) : 0.0f,
              .drive = t > 8.0f ? 1.0f : 0.0f,
              .boost = t > 4.0f && t < 9.0f ? 1.0f : 0.0f,
              .toggle_mount = !logic ().m_sling.hooked &&
                              session ().hook_within_reach ().has_value (),
              .look_yaw = look_yaw,
              .look_pitch = look_pitch,
            };
          } else if (demo_wheelies && logic ().m_mode == M_BIKE) {
            // MOPPE_DEMO=wheelie: rolling for a few seconds of every
            // twelve, then up on the back wheel and held there as a rider
            // at the keyboard holds it, the throttle all or nothing --
            // open while the nose is low or falling, shut as it rises, and
            // a dab of the rear brake if it gets too high -- judged by
            // where the pitch is heading a moment on.
            const mov::Vehicle& bike = session ().bike ();
            const float pitch = bike.pitch ();
            const float rate =
              dt > 0.0f ? (pitch - m_demo_pitch) / dt : 0.0f;
            m_demo_pitch = pitch;
            const float ahead = pitch + 0.4f * rate;
            const bool up = std::fmod (total_time, 12.0f) > 3.0f;
            input = {
              .turn = 0.2f * std::sin (total_time * 0.25f),
              .drive = !up ? 0.6f : ahead < 0.7f ? 1.0f : 0.0f,
              .lean_back = up,
              .look_yaw = look_yaw,
              .look_pitch = look_pitch,
              .run = up && ahead > 1.1f,
            };
          } else if (logic ().m_mode == M_BOAT) {
            input = boat_autopilot (total_time);
            input.look_yaw = look_yaw;
            input.look_pitch = look_pitch;
          } else {
            input = {
              .turn = 0.35f * std::sin (total_time * 0.25f),
              .drive = 1.0f,
              .boost =
                logic ().m_mode != M_TRACTOR && logic ().m_mode != M_CADDY &&
                    logic ().m_mode != M_BOAT &&
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
        }

        pass_time (dt);
        ride_along (input);
        go_fishing (input, dt);
        cook_supper (input);
        tend_camp (input, dt);
        reach_for_mushroom (input);
        const GameSessionAdvanceResult advance =
          advance_game_session (world (),
                                surface (),
                                session (),
                                input,
                                seconds (dt),
                                &m_trunk_field,
                                ground_cover (session ().subject_position ()),
                                &generated_world ().water_surface (),
                                m_strangers);
        if (advance.say_ouchies)
          platform::say ("Ouchies. That hurts.");

        if (m_water_inspection) {
          session ().camera ().place (m_water_inspection->eye,
                                      m_water_inspection->target);
          session ().camera ().keep_above (surface ());
        }
        orbit_camera ();
        spectator_camera (dt);
        rest_camera ();
        walk_side_camera ();
        ride_capture_camera (dt);

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
        params.frame_rate_target = frame.graphics.target_frame_rate;
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
        state.wheelie_m = reading.wheelie_m;
        state.last_wheelie_m = reading.last_wheelie_m;
        state.last_wheelie_points = reading.last_wheelie_points;
        state.best_wheelie_m = reading.best_wheelie_m;
        state.wheelie_age_s = reading.wheelie_age_s;
        state.on_foot = reading.on_foot;
        state.gliding = reading.gliding;
        state.can_deploy_glider = reading.can_deploy_glider;
        state.can_drop_bike = reading.can_drop_bike;
        state.can_mount = reading.can_mount;
        state.can_drive = reading.can_drive;
        state.can_fly = reading.can_fly;
        state.can_drive_tractor = reading.can_drive_tractor;
        state.can_drive_caddy = reading.can_drive_caddy;
        state.can_take_boat = reading.can_take_boat;
        state.haul = reading.haul;
        state.hauling = reading.hauling;
        state.stuck = reading.stuck;
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

      // The vehicles lend their meshes to the sun's shadow before it is
      // drawn, so that they shade the ground under them and their own
      // insides; riders, skinned into a draw list, cast none.
      void cast_actor_shadows (render::Renderer& r, const FrameView& frame) {
        if (m_spectator || frame.scene != FrameSceneMode::Gameplay)
          return;
        const FrameActors& actors = frame.actors;
        m_caster_dl.clear ();
        r.begin_shadow_casters ();
        render_vehicle (r, m_caster_dl, actors.bike, false, 0);
        render_mountain_bike (
          r, m_caster_dl, actors.mountain_bike, std::nullopt, 0);
        render_long_john (r, m_caster_dl, actors.long_john, std::nullopt, 0);
        render_jeep (r, m_caster_dl, actors.jeep, {}, 0);
        render_caddy (r, m_caster_dl, actors.caddy, {}, 0);
        render_tractor (r, m_caster_dl, actors.tractor, std::nullopt, 0);
        render_helicopter (r, actors.helicopter, 0);
        render_boat (r, m_caster_dl, actors.boat, std::nullopt, 0);
        m_places_landscape.cast_shadows (r, frame.camera.position);
        if (m_companion) {
          const CompanionSnapshot& theirs = *m_companion;
          render_vehicle (r, m_caster_dl, theirs.bike, false, 0);
          render_mountain_bike (
            r, m_caster_dl, theirs.mountain_bike, std::nullopt, 0);
          render_long_john (
            r, m_caster_dl, theirs.long_john, std::nullopt, 0);
          render_jeep (r, m_caster_dl, theirs.jeep, {}, 0);
          render_caddy (r, m_caster_dl, theirs.caddy, {}, 0);
          render_tractor (r, m_caster_dl, theirs.tractor, std::nullopt, 0);
          render_helicopter (r, theirs.helicopter, 0);
          render_boat (r, m_caster_dl, theirs.boat, std::nullopt, 0);
        }
        r.end_shadow_casters ();
      }

      void draw_world_layers (render::Renderer& r, const FrameView& frame) {
        const FrameVisibility& visibility = frame.visibility;
        const Vec3& camera = frame.camera.position;
        if (m_graphics.terrain_shadows)
          cast_actor_shadows (r, frame);
        if (m_graphics.terrain_shadows)
          m_terrain.render_local_shadow (r,
                                         position (camera),
                                         frame.camera.frame_forward,
                                         frame.lighting.sun_direction,
                                         frame.lighting.sky_pole,
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
            case M_JEEP:
              interaction_radius = 1.7f;
              break;
            case M_TRACTOR:
              interaction_radius = 1.8f;
              break;
            case M_CADDY:
              interaction_radius = 1.6f;
              break;
            case M_MOUNTAIN_BIKE:
              interaction_radius = 0.6f;
              break;
            case M_LONG_JOHN:
              interaction_radius = 0.8f;
              break;
            case M_HELICOPTER:
              // The downwash flattens the grass under it.
              interaction_radius = 4.0f;
              break;
            case M_GLIDER:
            case M_PASSENGER:
            case M_BOAT:
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
          render_vehicle (r,
                          m_world_dl,
                          actors.bike,
                          actors.active_mode == M_BIKE,
                          0x1000,
                          m_outfit);
        if (!m_spectator) {
          m_blob.draw (m_world_dl, surface (), actors.jeep.position, 3.4f);
          render_jeep (
            r,
            m_world_dl,
            actors.jeep,
            { .driver = actors.active_mode == M_JEEP && !helmet,
              .passenger = m_companion &&
                           m_companion->mode == M_PASSENGER &&
                           !m_companion->in_boat,
              .driver_outfit = m_outfit,
              .passenger_outfit = their_outfit () },
            0x5000);
          m_blob.draw (
            m_world_dl, surface (), actors.helicopter.position, 4.5f);
          render_helicopter (r, actors.helicopter, 0x8000);
          m_blob.draw (m_world_dl, surface (), actors.tractor.position, 2.8f);
          render_tractor (r,
                          m_world_dl,
                          actors.tractor,
                          actors.active_mode == M_TRACTOR && !helmet
                            ? std::optional<Outfit> (m_outfit)
                            : std::nullopt,
                          0xA000);
          m_blob.draw (m_world_dl, surface (), actors.caddy.position, 3.6f);
          render_caddy (r,
                        m_world_dl,
                        actors.caddy,
                        { .driver = actors.active_mode == M_CADDY && !helmet,
                          .driver_outfit = m_outfit },
                        0xC000);
          if (actors.sling)
            render_sling (m_world_dl, *actors.sling);
          render_boat (r,
                       m_world_dl,
                       actors.boat,
                       actors.active_mode == M_BOAT && !helmet
                         ? std::optional<Outfit> (m_outfit)
                         : std::nullopt,
                       0xF000,
                       actors.helm_rod,
                       frame.lighting.time,
                       m_companion && m_companion->mode == M_PASSENGER &&
                           m_companion->in_boat
                         ? std::optional<Outfit> (their_outfit ())
                         : std::nullopt);
          m_blob.draw (
            m_world_dl, surface (), actors.mountain_bike.position, 1.3f);
          // Through the rider's eyes, the bars and the fork below them.
          render_mountain_bike (r,
                                m_world_dl,
                                actors.mountain_bike,
                                actors.active_mode == M_MOUNTAIN_BIKE && !helmet
                                  ? std::optional<Outfit> (m_outfit)
                                  : std::nullopt,
                                0xD000);
          m_blob.draw (m_world_dl, surface (), actors.long_john.position, 2.0f);
          render_long_john (r,
                            m_world_dl,
                            actors.long_john,
                            actors.active_mode == M_LONG_JOHN && !helmet
                              ? std::optional<Outfit> (m_outfit)
                              : std::nullopt,
                            0xD100);
        }
        const Basket& basket = logic ().m_basket;
        m_basket_contents.clear ();
        for (std::size_t i = basket.used; i < basket.picked.size (); ++i)
          m_basket_contents.push_back (m_mushrooms.site (basket.picked[i]));
        draw_camp (m_world_dl);
        draw_places (r, frame);
        if (actors.walker && lying_down ())
          draw_sleeper (m_world_dl, helmet, frame.lighting.time);
        else if (actors.walker && !helmet)
          render_walker (m_world_dl,
                         *actors.walker,
                         frame.lighting.time,
                         m_basket_contents,
                         m_outfit);
        else if (actors.walker)
          render_basket (
            m_world_dl, *actors.walker, frame.lighting.time, m_basket_contents);
        // The mushroom just picked rises from the moss into the basket.
        if (actors.walker && !basket.picked.empty () && !basket.last_refused) {
          const float age =
            static_cast<float> (logic ().m_total_time - basket.last_reach_time);
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
          render_glider (r,
                         m_world_dl,
                         *actors.glider,
                         frame.lighting.time,
                         0x2000,
                         m_outfit);
        draw_companion (r, m_world_dl, frame.lighting.time);

        r.draw_list (m_world_dl, 0x0001);

        // Additive glow after the solid list, so it blends over everything
        // already drawn: exhaust and jump-jet flames, then star halos.
        if (visibility.vehicle_effects && !m_spectator &&
            !(helmet && actors.active_mode == M_BIKE))
          render_vehicle_flames (
            r, actors.bike, frame.camera.position, frame.lighting.time, 0x1000);
        m_places_landscape.draw_beacon_fires (r,
                                              frame.camera.position,
                                              m_beacon_burn,
                                              frame.lighting.daylight,
                                              frame.lighting.time);
        if (logic ().m_camp.fire_time > -50.0)
          draw_fire (r,
                     logic ().m_camp.hearth,
                     frame.camera.position,
                     fire_burn (logic ().m_camp, logic ().m_total_time),
                     frame.lighting.time);
        if (m_companion) {
          if (visibility.vehicle_effects)
            render_vehicle_flames (r,
                                   m_companion->bike,
                                   frame.camera.position,
                                   frame.lighting.time,
                                   0x3000);
          if (m_companion->hearth_laid)
            draw_fire (r,
                       m_companion->hearth,
                       frame.camera.position,
                       m_companion->fire_burn,
                       frame.lighting.time);
        }
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

      // The boats' wakes lie on the water, so they are drawn over it.
      void draw_boat_wakes (render::Renderer& r, const FrameView& frame) {
        if (!frame.visibility.actors || !frame.visibility.ocean ||
            m_spectator)
          return;
        const terrain::WaterSheets& sheets =
          generated_world ().water_surface ();
        const float time = frame.lighting.time;
        const auto level = [&] (const Vec3& at) {
          return water_surface_at (world (), surface (), &sheets, at, time)
            .numerical_value_in (u::m);
        };
        const auto depth = [&] (const Vec3& at) {
          return level (at) - ground_height (at);
        };
        const Vec3& ours = frame.actors.boat.position;
        m_boat_wake.follow (frame.actors.boat, level (ours), depth (ours), time);
        m_wake_dl.clear ();
        m_boat_wake.draw (m_wake_dl, time, m_sky.daylight, level);
        if (m_companion) {
          const Vec3& theirs = m_companion->boat.position;
          m_their_wake.follow (
            m_companion->boat, level (theirs), depth (theirs), time);
          m_their_wake.draw (m_wake_dl, time, m_sky.daylight, level);
        } else {
          m_their_wake.clear ();
        }
        r.draw_list (m_wake_dl, 0x0002);
      }

      void draw_effect_layers (render::Renderer& r, const FrameView& frame) {
        const FrameVisibility& visibility = frame.visibility;

        // Dust last so spray sits atop every water surface.
        if (visibility.dust)
          session ().dust ().render (r);

        // Screen-space post lighting shares the shaken camera basis,
        // extracted straight from the final view matrix with the frustum
        // half-extents folded into the right/up spans. Occlusion first so
        // the added beams are not darkened; sun shafts march the
        // camera-local shadow map, so the beams carry the same tree and
        // terrain shapes as the ground shadows. Underwater frames get
        // their own grade instead. Both shade the jittered scene, so they
        // come before its reconstruction.
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

        // Reconstruction consumes the scene's colour, depth, and motion;
        // screen-space grades and feedback operate on its full-size result.
        r.reconstruct_scene ();
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
          add_fishing_readings (hud_state);
          add_company_readings (hud_state);
          add_waymarks (hud_state, frame, hud_width, hud_height);
          add_chart (hud_state, frame);
          m_hud.draw (m_hud_dl, m_hud_text, hud_state, hud_width, hud_height);
          if (m_hud.diagnostics ())
            draw_trail_map (m_hud_dl,
                            hud_width,
                            hud_height,
                            frame.hud.subject_position,
                            frame.hud.subject_heading);
          // MOPPE_MAP=whole|near starts with the map unfolded, for
          // captures.
          if (!m_map_asked) {
            m_map_asked = true;
            if (const char* map = moppe::environment ("MOPPE_MAP"))
              m_map.show (std::string_view (map) == "near"
                            ? MapSheet::View::Near
                            : MapSheet::View::Whole);
          }
          if (m_map.open ())
            draw_map (r, hud_width, hud_height);
          if (m_quit_asked)
            m_hud.draw_quit_question (
              m_hud_dl, m_hud_text, hud_width, hud_height);
        }
        m_hud.publish_offers ();
        m_hud_text.translate (safe_insets.left, safe_insets.top);

        // Even a clean inspection capture needs this empty HUD pass: it is
        // also the final post-chain composite into the drawable.
        r.draw_hud_text (m_hud_text);
        r.draw_hud (m_hud_dl);
      }

      // Paints the map sheet of this world and uploads it: for the map
      // over the view (M) and the chart in the HUD's corner.
      void paint_map (render::Renderer& r) {
        MOPPE_PROFILE_ZONE ("startup.paint_map");
#ifdef __EMSCRIPTEN__
        // A browser's page has less to spare for one picture.
        const int limit = 2048;
#else
        const int limit = 4096;
#endif
        const int size = std::min (limit, 2 * recipe ().resolution ());
        m_map.set (r,
                   paint_relief_map (surface (),
                                     surface_readings (),
                                     generated_world ().water_surface (),
                                     rivers (),
                                     trail_network (),
                                     generated_world ().roads (),
                                     world ().water_level,
                                     size));
      }

      // What there is to find on the map, the walker first.
      std::vector<MapSheet::Mark> map_marks () const {
        using Mark = MapSheet::Mark;
        const DisplayColor dark (0.09f, 0.09f, 0.10f);
        std::vector<Mark> marks;
        const Mode mode = logic ().m_mode;
        marks.push_back ({ .kind = Mark::Kind::Walker,
                           .position = session ().subject_position (),
                           .heading = subject_heading (),
                           .colour = dark,
                           .name = "" });
        if (m_companion && mode != M_PASSENGER &&
            m_companion->mode != M_PASSENGER) {
          const CompanionSnapshot& theirs = *m_companion;
          Vec3 at = theirs.bike.position;
          if (theirs.walker)
            at = theirs.walker->position;
          else if (theirs.glider)
            at = theirs.glider->position;
          else if (theirs.mode == M_JEEP)
            at = theirs.jeep.position;
          else if (theirs.mode == M_HELICOPTER)
            at = theirs.helicopter.position;
          else if (theirs.mode == M_TRACTOR)
            at = theirs.tractor.position;
          else if (theirs.mode == M_CADDY)
            at = theirs.caddy.position;
          else if (theirs.mode == M_MOUNTAIN_BIKE)
            at = theirs.mountain_bike.position;
          else if (theirs.mode == M_LONG_JOHN)
            at = theirs.long_john.position;
          else if (theirs.mode == M_BOAT)
            at = theirs.boat.position;
          DisplayColor jacket = dark;
          for (std::size_t c = 0; c < figure::colours.size (); ++c)
            if (std::string_view (figure::colour_names[c]) == "jacket")
              jacket = figure::outfit_colour (c, their_outfit ());
          marks.push_back ({ .kind = Mark::Kind::Companion,
                             .position = at,
                             .heading = Vec3 (0, 0, -1),
                             .colour = jacket,
                             .name = "companion" });
        }
        if (mode != M_BIKE && !session ().can_drop_bike ())
          marks.push_back ({ .kind = Mark::Kind::Bike,
                             .position = session ().bike ().position (),
                             .colour = dark,
                             .name = "bike" });
        if (mode != M_JEEP)
          marks.push_back ({ .kind = Mark::Kind::Jeep,
                             .position = session ().jeep ().position (),
                             .colour = dark,
                             .name = "jeep" });
        if (mode != M_TRACTOR)
          marks.push_back ({ .kind = Mark::Kind::Jeep,
                             .position = session ().tractor ().position (),
                             .colour = DisplayColor (0.70f, 0.12f, 0.08f),
                             .name = "tractor" });
        if (mode != M_CADDY)
          marks.push_back ({ .kind = Mark::Kind::Jeep,
                             .position = session ().caddy ().position (),
                             .colour = DisplayColor (0.22f, 0.50f, 0.45f),
                             .name = "Caddy" });
        if (mode != M_MOUNTAIN_BIKE)
          marks.push_back ({ .kind = Mark::Kind::Bike,
                             .position = session ().mountain_bike ().position (),
                             .colour = DisplayColor (0.06f, 0.30f, 0.22f),
                             .name = "mountainbike" });
        if (mode != M_LONG_JOHN)
          marks.push_back ({ .kind = Mark::Kind::Bike,
                             .position = session ().long_john ().position (),
                             .colour = DisplayColor (0.58f, 0.575f, 0.57f),
                             .name = "Long John" });
        if (mode != M_HELICOPTER)
          marks.push_back ({ .kind = Mark::Kind::Jeep,
                             .position = session ().helicopter ().position (),
                             .colour = DisplayColor (0.20f, 0.30f, 0.20f),
                             .name = "helicopter" });
        if (mode != M_BOAT)
          marks.push_back ({ .kind = Mark::Kind::Jeep,
                             .position = session ().boat ().position (),
                             .colour = DisplayColor (0.62f, 0.48f, 0.30f),
                             .name = "boat" });
        const Camp& camp = logic ().m_camp;
        if (camp.hung || camp.fire)
          marks.push_back (
            { .kind = Mark::Kind::Camp,
              .position = camp.hung ? hammock_middle (camp.hammock)
                                    : camp.hearth,
              .colour = DisplayColor (0.80f, 0.38f, 0.08f),
              .name = "camp" });
        marks.push_back ({ .kind = Mark::Kind::Trailhead,
                           .position = m_home_base_position,
                           .colour = DisplayColor (0.80f, 0.13f, 0.11f),
                           .name = "trailhead" });
        return marks;
      }

      // The chart in the HUD's corner: the map sheet round the subject,
      // turned to the way the camera looks, with the marks on it.
      void add_chart (HudState& state, const FrameView& frame) const {
        if (m_spectator || !m_map.picture ())
          return;
        HudState::Chart& chart = state.chart;
        chart.picture = m_map.picture ();
        chart.period_x_m = m_map.period_x_m ();
        chart.period_z_m = m_map.period_z_m ();
        const Vec3 at = frame.hud.subject_position;
        chart.x_m = at[0];
        chart.z_m = at[2];
        // Ahead is where the camera looks; looking down or up, its top
        // (or its foot) leans the way it faces.
        const Vec3 forward = frame.camera.forward;
        const Vec3 up = frame.camera.up;
        const float lean = forward[1] < 0.0f ? 1.0f : -1.0f;
        chart.ahead_x = forward[0] + lean * up[0];
        chart.ahead_z = forward[2] + lean * up[2];
        const Vec3 facing = frame.hud.subject_heading;
        chart.facing_x = facing[0];
        chart.facing_z = facing[2];
        chart.daylight = frame.lighting.daylight;
        using Kind = MapSheet::Mark::Kind;
        using Pin = HudState::Chart::Pin;
        for (const MapSheet::Mark& mark : map_marks ()) {
          if (mark.kind == Kind::Walker ||
              chart.pin_count >= static_cast<int> (chart.pins.size ()))
            continue;
          Pin& pin = chart.pins[chart.pin_count++];
          pin.kind = mark.kind == Kind::Companion   ? Pin::Kind::Companion
                     : mark.kind == Kind::Bike      ? Pin::Kind::Bike
                     : mark.kind == Kind::Camp      ? Pin::Kind::Camp
                     : mark.kind == Kind::Trailhead ? Pin::Kind::Trailhead
                                                    : Pin::Kind::Machine;
          pin.x_m = mark.position[0];
          pin.z_m = mark.position[2];
          pin.red = mark.colour.red;
          pin.green = mark.colour.green;
          pin.blue = mark.colour.blue;
        }
      }

      // The map over the view (M): the sheet is painted with the world
      // (or the first time it is unfolded), and what moves is marked on
      // it each frame. It takes the place of the rest of the HUD while it
      // is open.
      void draw_map (render::Renderer& r, int width_pts, int height_pts) {
        if (!m_map.painted ())
          paint_map (r);
        m_hud_dl.clear ();
        m_hud_text.clear ();
        const platform::Insets safe_insets = platform::safe_insets ();
        m_hud_dl.translate (safe_insets.left, safe_insets.top, 0);
        m_map.draw (m_hud_dl,
                    m_hud_text,
                    m_hud.font (),
                    map_marks (),
                    "MOPPE  \u00b7  SEED " +
                      std::to_string (recipe ().seed ().value),
                    width_pts,
                    height_pts);
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
        FrameView frame = compose_frame_view (frame_view_input (aspect));
        light_companion_fire (frame);
        light_beacons (frame);
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
        render::FrameParams params = frame_params_for (frame);
        if (!keeping_pace ())
          params.frame_rate_target = 0.0f;
        if (!r.begin_frame (params))
          return;

        draw_world_layers (r, frame);
        draw_actor_layers (r, frame);

        draw_water_surfaces (r, frame);
        draw_boat_wakes (r, frame);
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
        m_hud.publish_offers ();
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
        // Esc in play asks whether to quit: Esc again quits, and any
        // other key goes back to the game (its releases still count, so
        // nothing stays held).
        if (m_quit_asked && down) {
          m_quit_asked = false;
          if (k == Key::Escape) {
            m_video.finish ();
            platform::request_quit ();
          }
          return;
        }

        if (m_spectator) {
          // L lets the mouse loose -- to start a screen recording, say --
          // and takes it back.
          if (k == Key::L && down) {
            m_spectator->pointer_free = !m_spectator->pointer_free;
            platform::set_pointer_captured (!m_spectator->pointer_free);
          }
          if (down)
            m_spectator->held.insert (k);
          else
            m_spectator->held.erase (k);
          if (k == Key::Escape && down) {
            m_video.finish ();
            platform::request_quit ();
          }
          return;
        }
        if (opening_active ()) {
          if (k == Key::Escape && down) {
            m_video.finish ();
            platform::request_quit ();
          } else
            m_live_input.cinematic_key (k, down);
          return;
        }

        if (k == Key::L && down) {
          m_pointer_free = !m_pointer_free;
          platform::set_pointer_captured (!m_pointer_free);
          return;
        }

        // M unfolds the map, looks closer, and folds it away; Esc folds
        // it away too, and with it folded asks whether to quit.
        if (k == Key::M && down && m_ready) {
          m_map.turn ();
          return;
        }
        if (k == Key::Escape && down) {
          if (m_map.open ())
            m_map.show (MapSheet::View::Closed);
          else
            m_quit_asked = true;
          return;
        }

        if (k == Key::N && down && m_ready) {
          regenerate_world ();
          return;
        }

        m_live_input.key (k, down);
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
        // A video of the river flight is over when the flight is: the
        // drone comes down a little sooner than its planned seconds, and
        // what follows is only the walker standing at the trailhead.
        static const bool flight_only = [] {
          const char* opening = moppe::environment ("MOPPE_OPENING");
          return opening && std::string_view (opening) == "river";
        }();
        if (flight_only && opening_active ())
          m_video_saw_flight = true;
        const bool landed =
          flight_only && m_video_saw_flight && !opening_active ();
        if (!m_video.recording () || landed ||
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
          .capture_field_of_view =
            m_film_fov && !cinematic && !m_opening.active ()
              ? std::optional<degrees_t> (*m_film_fov * u::deg)
              : std::nullopt,
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
        // The opening has already arrived where play takes up the view.
        session ().settle_camera (surface ());
      }

      // The opening's last shot lands where play takes up the view: at
      // the following camera's place behind the player, or in their eyes.
      OpeningShot arrival () {
        session ().settle_camera (surface ());
        const ChaseCamera& camera = session ().camera ();
        const float fov = logic ().m_cam_mode == CAM_HELMET
                            ? 70.0f
                            : CHASE_FIELD_OF_VIEW.numerical_value_in (u::deg);
        return arrival_shot (
          m_opening_arrival, camera.position (), camera.forward (), fov);
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
        logic ().m_wheelie_m = 0.0f;
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
      // The shore the boat was left at, when it was.
      std::optional<Vec3> m_mooring;
      // How far aside the later of two players parks the machines that
      // stand at places in the world, so that theirs and ours do not
      // stand in one another.
      Vec3 m_park_aside {};
      // MOPPE_BOAT=river: the river's course down to the sea, which the
      // boat autopilot follows, and the next point of it ahead.
      std::vector<Vec3> m_boat_route;
      std::size_t m_boat_route_at = 0;
      // The boat's film camera: how long it has run, the subject and its
      // heading as eased, the fly-by's stand, and its lens (degrees).
      float m_film_time = 0.0f;
      Vec3 m_film_at;
      Vec3 m_film_heading;
      Vec3 m_film_stand;
      std::optional<float> m_film_fov;
      Vec3 m_home_base_position;
      render::DrawList m_home_base_marker;
      bool m_skip_cinematic_requested = false;
      // Whose Space was last prompted (HudState::thumb_hint), since when,
      // and whether the bike's and the boat's boosts have been tried.
      mutable int m_thumb_mode = -1;
      mutable double m_thumb_from = 0.0;
      mutable bool m_boost_tried[2] = { false, false };
      mutable bool m_wheelie_tried = false;
      // MOPPE_DEMO=wheelie's last reading of the bike's pitch.
      float m_demo_pitch = 0.0f;
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
      // What stands at the ends of the roads (game/places.hh), and how
      // each sanctuary's beacon burns this frame.
      PlacesPlan m_places;
      PlacesLandscape m_places_landscape;
      std::vector<float> m_beacon_burn;
      MushroomPatch m_mushrooms;
      // The poisonous mushrooms the foraging script has left be.
      std::vector<std::uint32_t> m_forage_refused;
      // Scratch for the basket's contents as drawn.
      std::vector<MushroomSite> m_basket_contents;
      // The other player, when there is one (game/companion.hh): the wire
      // to them, what they have lately sent, and how they look this frame.
      std::unique_ptr<CompanionLink> m_link;
      CompanionTrack m_track;
      std::vector<CompanionSnapshot> m_arrived;
      std::optional<CompanionSnapshot> m_companion;
      // The other player's machines as ours bump into them, and where
      // each stood a frame ago, to tell how they move.
      std::vector<MachineBody> m_strangers;
      std::optional<std::array<Vec3, machine_count>> m_strangers_were;
      // Their picks in the order they made them, as far as heard, and
      // the same as mushrooms for the basket they carry.
      std::vector<std::uint32_t> m_companion_picks;
      std::vector<MushroomSite> m_companion_basket;
      // Scratch for a pan's contents as drawn, and the last meal's note.
      std::vector<MushroomSite> m_pan_contents;
      mutable std::string m_meal_note;
      mutable std::string m_catch_note;
      // The boats' wakes, ours and the other player's, and their list.
      BoatWake m_boat_wake;
      BoatWake m_their_wake;
      render::DrawList m_wake_dl;
      // MOPPE_WALK=fish: when the line last came in.
      float m_fish_script_rest = 1.0f;
      // MOPPE_WALK=boat-fish: when it got under way again.
      float m_boat_fish_off = -1.0f;
      // Their hour of the day; negative with no one to keep time with.
      double m_companion_hours = -1.0;
      bool m_companion_met = false;
      bool m_rode_along = false;
      // What this player wears; the other wears the other.
      Outfit m_outfit = Outfit::blue_jacket;
      bool m_companion_elsewhere = false;
      std::uint32_t m_snapshots_sent = 0;
      // The clock this player's snapshots are stamped by, the link's
      // clock when it was last read, and how far the simulation has
      // stepped since.
      double m_pose_clock = 0.0;
      double m_pose_heard = -1.0;
      double m_stepped = 0.0;
      const char* m_notice = nullptr;
      double m_notice_at = 0.0;
      OpeningArrival m_opening_arrival;
      BlobShadow m_blob;
      mov::TrunkField m_trunk_field;
      // The trees the walker stands between that would take the hammock,
      // and where they last stood when that was worked out.
      std::optional<HammockSite> m_hammock_at_hand;
      Vec3 m_hammock_sought_from;
      bool m_hammock_sought = false;
      float m_walk_script_time = 0.0f;
      // MOPPE_WALK=supper: when the foraging was done.
      float m_supper_since = -1.0f;
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
      bool m_video_saw_flight = false;
      int m_clip_count = 0;
      bool m_video_done = false;
      std::optional<WaterShot> m_water_shot;
      std::optional<WaterInspection> m_water_inspection;
      std::optional<mov::Trunk> m_orbit_tree;
      // Whether L has handed the mouse back to the desktop during play.
      bool m_pointer_free = false;
      MapSheet m_map;
      // Esc in play has asked whether to quit (Hud::draw_quit_question).
      bool m_quit_asked = false;
      bool m_map_asked = false;
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
      // Where the shadow casters' riders go, to be thrown away.
      render::DrawList m_caster_dl;
      render::DrawList m_hud_dl;
    };

    std::unique_ptr<platform::Game>
    make_moppe_game (const LaunchOptions& options,
                     terrain::WorldRecipe recipe) {
      return std::make_unique<MoppeGame> (options, std::move (recipe));
    }
  }
}
