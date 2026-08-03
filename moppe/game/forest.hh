#ifndef MOPPE_GAME_FOREST_HH
#define MOPPE_GAME_FOREST_HH

#include <moppe/game/foliage.hh>
#include <moppe/map/surface.hh>
#include <moppe/render/renderer.hh>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace moppe::game {
  using ForestForm = FoliageKind;

  using TreeSizeFactor =
    mp_units::quantity<tree_size_factor[mp_units::one], float>;

  struct ForestSite {
    position_t position {};
    terrain::TerrainNormal normal {};
    map::ForestCover cover {};
    map::SurfaceMoisture moisture {};
    TreeSizeFactor size = 1.0f * tree_size_factor[mp_units::one];
    std::uint32_t seed = 0;
    ForestForm form = ForestForm::broadleaf;
  };

  struct ForestPlan {
    std::vector<ForestSite> sites;
    spatial_extent_t period {};
  };

  struct ForestView {
    position_t position {};
    Vec3 forward { 0, 0, 1 };
    Vec3 right { 1, 0, 0 };
    Vec3 up { 0, 1, 0 };
    degrees_t vertical_field_of_view = 70.0f * u::deg;
    magnitude_t aspect_ratio = 1.0f * mp_units::one;
  };

  // Convert the continuous canopy field into stable individuals on a
  // jittered grid. Positions and identities depend only on world seed and
  // lattice cell, so revisiting an area never produces a different forest.
  [[nodiscard]] ForestPlan
  plan_global_forest (const map::SurfaceGeometry& surface,
                      const map::SurfaceReadings& readings,
                      std::uint32_t seed,
                      meters_t spacing = 12.0f * u::m);

  // Terrain-scale presentation of the population.
  //
  // Three things happen to a tree as it recedes. Near enough to ride past it
  // is an organism: a stem with a flare and a lean, limbs, a crown with
  // separate masses in it. Further off only its volume and its colour still
  // carry, and it becomes a handful of triangles. Further off again it is
  // smaller than the pixel it lands in, and the terrain's own filtered
  // canopy is the more honest representation of a forest than a scatter of
  // specks over it would be, so no tree is drawn at all.
  //
  // Only the first of those is expensive, and at any moment only a dozen
  // chunks out of the world's whole lattice are close enough to want it. So
  // the cheap mesh is built once for everything and kept, and the near mesh
  // is built when a chunk comes within reach and released again when it
  // leaves. That is what pays for the near tree being an organism.
  class ForestLandscape {
  public:
    void rebuild (render::Renderer& renderer,
                  const map::SurfaceGeometry& surface,
                  const map::SurfaceReadings& readings,
                  std::uint32_t seed);

    // Brings the near meshes for the chunks about to be drawn into
    // residence, a bounded number per call so a fast approach costs a few
    // frames of coarser trees rather than one long stall. Baking a mesh
    // needs no encoder, but it must not happen inside a frame, so this is
    // called before the frame opens.
    void prepare (render::Renderer& renderer, const ForestView& view);
    void draw (render::Renderer& renderer, const ForestView& view) const;

    std::size_t tree_count () const noexcept {
      return m_tree_count;
    }

    // Development accounting: vertex bytes held right now, and how many
    // chunks are carrying a near mesh.
    std::size_t resident_bytes () const noexcept;
    std::size_t resident_chunk_count () const noexcept;

  private:
    struct Chunk {
      position_t center {};
      meters_t radius {};
      std::vector<ForestSite> sites;
      render::MeshPtr near_mesh;
      render::MeshPtr far_mesh;
      std::unique_ptr<render::DrawList> near_build;
      std::size_t near_build_site = 0;
      std::size_t near_bytes = 0;
      std::size_t far_bytes = 0;
      std::uint64_t wanted_epoch = 0;
    };

    std::vector<Chunk> m_chunks;
    render::TexturePtr m_leaf_texture;
    render::TexturePtr m_conifer_texture;
    render::TexturePtr m_distant_conifer_texture;
    spatial_extent_t m_period {};
    std::size_t m_tree_count = 0;
    std::uint64_t m_epoch = 0;
    int m_chunks_x = 0;
    int m_chunks_z = 0;
  };
}

#endif
