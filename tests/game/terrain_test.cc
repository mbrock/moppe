#include <moppe/environment.hh>
#include <moppe/game/terrain.hh>

#include <tests/recording_renderer.hh>
#include <tests/test.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>

using namespace moppe;

MOPPE_TEST (terrain_local_shadow_keeps_a_typed_bounded_focus) {
  game::Terrain terrain;
  test::RecordingRenderer renderer;
  const position_t camera = position (Vec3 (100.0f, 20.0f, 200.0f));

  terrain.render_local_shadow (renderer,
                               camera,
                               Vec3 (0.0f, 0.0f, 1.0f),
                               normalized (Vec3 (0.7f, 0.7f, 0.2f)),
                               Vec3 (0.0f, 0.866f, -0.5f),
                               true);

  MOPPE_CHECK (renderer.local_shadow.has_value ());
  const render::LocalShadowParams& shadow = *renderer.local_shadow;
  MOPPE_CHECK (shadow.include_forest);
  const render::ShadowCascade& near =
    shadow.cascades[render::LocalShadowParams::near];
  MOPPE_CHECK_NEAR (near.radius.numerical_value_in (u::m), 160.0f, 1e-6f);
  const Vec3 focus = position_value (near.focus);
  const float texel = 320.0f / 2048.0f;
  MOPPE_CHECK (std::fabs (focus[0] - 100.0f) <= texel);
  MOPPE_CHECK (std::fabs (focus[1] - 20.0f) <= texel);
  MOPPE_CHECK (std::fabs (focus[2] - 248.0f) <= texel);
  // The far cascade reaches well past the near one, mostly ahead.
  const render::ShadowCascade& far =
    shadow.cascades[render::LocalShadowParams::far];
  MOPPE_CHECK (far.radius > 5.0f * near.radius);
  MOPPE_CHECK (position_value (far.focus)[2] > focus[2]);
  for (const render::ShadowCascade& cascade : shadow.cascades)
    for (int element = 0; element < 16; ++element)
      MOPPE_CHECK (std::isfinite (cascade.light_view_proj.element (element)));
}

MOPPE_TEST (the_shadow_lattice_turns_about_the_camera_not_the_origin) {
  // The map's texels are a lattice in the world. When the sun moves the
  // lattice must turn, and about the eye: a point beside the camera keeps
  // very nearly its texel, however far from the world's origin they stand.
  game::Terrain terrain;
  test::RecordingRenderer renderer;
  const position_t camera = position (Vec3 (4200.0f, 70.0f, 3100.0f));
  const Vec3 probe (4203.0f, 71.0f, 3149.0f);
  const Vec3 pole = normalized (Vec3 (0.0f, 0.866f, -0.5f));
  // The equinox sun, square to the pole, a frame apart on a 48-minute day.
  const auto sun_at = [&] (float turn) {
    const Vec3 east (1, 0, 0);
    const Vec3 noon = cross (pole, east);
    return normalized (east * std::cos (turn) + noon * std::sin (turn));
  };
  const auto probe_texel = [&] (float turn, int axis) {
    terrain.render_local_shadow (
      renderer, camera, Vec3 (0, 0, 1), sun_at (turn), pole, true);
    const Mat4& map =
      renderer.local_shadow->cascades[render::LocalShadowParams::near]
        .light_view_proj;
    return 1024.0f * map.transform_point (probe)[axis];
  };
  const float turn = 0.9f, frame = 3.6e-5f;
  for (int axis = 0; axis < 2; ++axis) {
    const float before = probe_texel (turn, axis);
    const float after = probe_texel (turn + frame, axis);
    // Pivoting on the origin slid it most of a texel every frame.
    MOPPE_CHECK_NEAR (after, before, 0.05f);
  }
  // Travel moves the lattice only by whole texels.
  const float before = probe_texel (turn, 0);
  terrain.render_local_shadow (renderer,
                               position (Vec3 (4207.3f, 70.4f, 3111.9f)),
                               Vec3 (0, 0, 1),
                               sun_at (turn),
                               pole,
                               true);
  const float after =
    1024.0f * renderer.local_shadow->cascades[render::LocalShadowParams::near]
                .light_view_proj.transform_point (probe)[0];
  const float moved = after - before;
  MOPPE_CHECK_NEAR (moved, std::round (moved), 0.02f);
}

MOPPE_TEST (terrain_keeps_wet_sheets_in_the_culled_chunk_draws) {
  // Setup reads the authored sward, even with a recording renderer. Keep
  // this independent of CTest's working directory and restore the override.
  struct RestoreAssets {
    std::optional<std::string> previous;
    ~RestoreAssets () {
      set_environment ("MOPPE_ASSETS", previous ? previous->c_str () : nullptr);
    }
  } assets;
  if (const char* previous = environment ("MOPPE_ASSETS"))
    assets.previous = previous;
  set_environment ("MOPPE_ASSETS", MOPPE_TEST_SOURCE_DIR);

  constexpr int cells = render::terrain_chunk_cells;
  map::SurfaceGeometry surface (
    terrain::TerrainDomain (cells, cells, 1.0f * u::m, 1.0f * u::m));
  std::ranges::fill (spatial::get<terrain::surface_elevation> (surface),
                     terrain::surface_elevation_point (2.0f * u::m));
  map::rebuild_geometry (surface);
  terrain::WaterSheets lake (surface.domain ());
  std::ranges::fill (spatial::get<terrain::surface_elevation> (lake),
                     terrain::surface_elevation_point (180.0f * u::m));
  std::ranges::fill (spatial::get<terrain::wave_amplitude> (lake),
                     0.1f * terrain::wave_amplitude[one]);
  terrain::WaterSheets dry = lake;
  spatial::get<terrain::surface_elevation> (dry) =
    spatial::get<terrain::surface_elevation> (surface);

  game::WorldParams world;
  world.resolution = cells;
  world.map_size = spatial_extent_in_metres (Vec3 (cells, 200, cells));
  // Even a datum above the eye must not invent a sheet in the dry controls.
  world.water_level = 140.0f * u::m;
  const game::GraphicsSettings graphics = game::high_graphics_settings ();
  game::Terrain terrain;
  test::RecordingRenderer renderer;
  const auto draw_from = [&] (const Vec3& eye, const Mat4& projection) {
    renderer.terrain_chunks.clear ();
    const Mat4 view = Mat4::look_at (eye, eye + Vec3 (0, 1, 0), Vec3 (0, 0, 1));
    terrain.render (renderer, eye, projection * view, 256.0f);
  };
  const auto has_patch = [&] (int x, int z) {
    return std::ranges::any_of (
      renderer.terrain_chunks, [&] (const render::ChunkDraw& draw) {
        return draw.x0 == x && draw.z0 == z && draw.offset_x == 0.0f &&
               draw.offset_z == 0.0f &&
               draw.cells == render::terrain_patch_cells &&
               draw.lod == render::TerrainLod::Subdivided;
      });
  };

  const Vec3 eye (80, 130, 80);
  const Mat4 projection =
    Mat4::perspective_reversed (60.0f * u::deg, 1.0f, 0.5f, 500.0f);
  terrain.setup (renderer, surface, world, graphics, &lake);
  draw_from (eye, projection);
  // The bed is far below this upward view. Both the enclosing chunk and
  // its near patches must survive so NHAL can draw the water on them.
  MOPPE_CHECK (has_patch (64, 64));

  for (const auto* water :
       std::array<const terrain::WaterSheets*, 2> { &dry, nullptr }) {
    terrain.setup (renderer, surface, world, graphics, water);
    draw_from (eye, projection);
    MOPPE_CHECK (renderer.terrain_chunks.empty ());
  }

  // A narrow view begins 1.4 m beyond the chunk's right edge: beyond
  // jitter and a lake's ripples, but within the sea's horizontal excursion.
  const Vec3 edge_eye (cells + 1.5f, 130, 80);
  const Mat4 edge_view = Mat4::ortho (-0.1f, 0.1f, -0.1f, 0.1f, 0.5f, 500.0f);
  terrain.setup (renderer, surface, world, graphics, &lake);
  draw_from (edge_eye, edge_view);
  MOPPE_CHECK (!has_patch (96, 64));
  std::ranges::fill (spatial::get<terrain::wave_amplitude> (lake),
                     1.0f * terrain::wave_amplitude[one]);
  terrain.setup (renderer, surface, world, graphics, &lake);
  draw_from (edge_eye, edge_view);
  MOPPE_CHECK (has_patch (96, 64));
}
