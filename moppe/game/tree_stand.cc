#include <moppe/game/tree_stand.hh>

#include <atelier/tree.hh>

#include <moppe/profile.hh>
#include <moppe/render/draw.hh>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <random>
#include <ranges>

namespace moppe::game {
  namespace {
    using namespace mp_units;

    float unit_hash (std::uint32_t bits) {
      bits += 0x9e3779b9U;
      bits ^= bits >> 16;
      bits *= 0x7feb352dU;
      bits ^= bits >> 15;
      return static_cast<float> (bits & 0x00ffffffU) /
             static_cast<float> (0x01000000U);
    }

    float elevation_value (const map::Surface& surface, float x, float z) {
      return surface.elevation_at (position (Vec3 (x, 0, z)))
        .quantity_from_zero ()
        .numerical_value_in (u::m);
    }

    Vec3 normal_value (const map::Surface& surface, float x, float z) {
      return normalized (
        surface.normal_at (position (Vec3 (x, 0, z))).numerical_value_in (one));
    }

    float habitat_value (const map::Surface& surface, float x, float z) {
      return surface.tree_habitat_at (position (Vec3 (x, 0, z)))
        .numerical_value_in (one);
    }

    float forest_cover_value (const map::Surface& surface, float x, float z) {
      return surface.forest_cover_at (position (Vec3 (x, 0, z)))
        .numerical_value_in (one);
    }

    TreeSite site_at (const map::Surface& surface,
                      float x,
                      float z,
                      float scale,
                      std::uint32_t seed) {
      const TreeCohort cohort = scale < 0.66f   ? TreeCohort::sapling
                                : scale < 1.08f ? TreeCohort::young
                                                : TreeCohort::canopy;
      return { .position = Vec3 (x, elevation_value (surface, x, z), z),
               .normal = normal_value (surface, x, z),
               .habitat = habitat_value (surface, x, z),
               .scale = scale,
               .seed = seed,
               .cohort = cohort };
    }

    float horizontal_distance_squared (const Vec3& first, const Vec3& second) {
      const float dx = first[0] - second[0];
      const float dz = first[2] - second[2];
      return dx * dx + dz * dz;
    }

    struct TreeFrame {
      Vec3 across;
      Vec3 up;
      Vec3 forward;
    };

    TreeFrame frame_at (const TreeSite& site) {
      const float heading =
        2.0f * std::numbers::pi_v<float> * unit_hash (site.seed ^ 0x51a7U);
      const Vec3 preferred (std::sin (heading), 0.0f, std::cos (heading));
      const Vec3 up = normalized (site.normal);
      Vec3 forward = preferred - up * dot (preferred, up);
      if (length2 (forward) < 1e-5f)
        forward = Vec3 (0, 0, 1);
      forward = normalized (forward);
      return { normalized (cross (up, forward)), up, forward };
    }

    Vec3
    place (const TreeSite& site, const TreeFrame& frame, const Vec3& local) {
      return site.position + (frame.across * local[0] + frame.up * local[1] +
                              frame.forward * local[2]) *
                               site.scale;
    }

    std::vector<Vec3> rest_positions (const atelier::Tree& tree) {
      const atelier::DirectedTreeTopology& topology = tree.topology ();
      const auto& length =
        atelier::get<atelier::branch_rest_length> (tree.edges ());
      std::vector<Vec3> positions (topology.vertex_count (), Vec3 ());
      for (atelier::TreeVertexId vertex = 1; vertex < topology.vertex_count ();
           ++vertex) {
        const atelier::TreeEdgeId edge = topology.parent_edge (vertex);
        const atelier::TreeEdgeForm& form = tree.form (edge);
        const float elevation = atelier::in_radians (form.elevation);
        const float azimuth = atelier::in_radians (form.azimuth);
        const float horizontal = std::cos (elevation);
        const Vec3 direction (horizontal * std::sin (azimuth),
                              std::sin (elevation),
                              horizontal * std::cos (azimuth));
        const float metres = length[edge].numerical_value_in (u::m);
        positions[vertex] =
          positions[topology.edge (edge).parent] + direction * metres;
      }
      return positions;
    }

    void branch_vertex (render::DrawList& draw,
                        const Vec3& position,
                        const Vec3& normal,
                        float wind) {
      draw.normal (normal);
      draw.wind (std::clamp (wind, 0.0f, 1.0f) * proportion[one]);
      draw.vertex (position);
    }

    void append_branch (render::DrawList& draw,
                        const Vec3& start,
                        const Vec3& end,
                        float start_radius,
                        float end_radius,
                        float start_wind,
                        float end_wind,
                        float color_variation) {
      const Vec3 axis = normalized (end - start);
      const Vec3 anchor =
        std::abs (axis[1]) > 0.90f ? Vec3 (1, 0, 0) : Vec3 (0, 1, 0);
      const Vec3 across = normalized (cross (axis, anchor));
      const Vec3 around = normalized (cross (across, axis));
      constexpr int sides = 6;
      draw.color (0.20f + 0.040f * color_variation,
                  0.10f + 0.025f * color_variation,
                  0.040f + 0.015f * color_variation);
      draw.begin (render::Prim::Triangles);
      for (int side = 0; side < sides; ++side) {
        const float a0 = 2.0f * std::numbers::pi_v<float> * side / sides;
        const float a1 = 2.0f * std::numbers::pi_v<float> * (side + 1) / sides;
        const Vec3 n0 = across * std::cos (a0) + around * std::sin (a0);
        const Vec3 n1 = across * std::cos (a1) + around * std::sin (a1);
        const Vec3 s0 = start + n0 * start_radius;
        const Vec3 s1 = start + n1 * start_radius;
        const Vec3 e0 = end + n0 * end_radius;
        const Vec3 e1 = end + n1 * end_radius;
        branch_vertex (draw, s0, n0, start_wind);
        branch_vertex (draw, e0, n0, end_wind);
        branch_vertex (draw, e1, n1, end_wind);
        branch_vertex (draw, s0, n0, start_wind);
        branch_vertex (draw, e1, n1, end_wind);
        branch_vertex (draw, s1, n1, start_wind);
      }
      draw.end ();
    }

    float branch_end_radius (const atelier::Tree& tree,
                             atelier::TreeEdgeId edge) {
      const auto& radius = atelier::get<atelier::branch_radius> (tree.edges ());
      const atelier::TreeVertexId child = tree.topology ().edge (edge).child;
      float continuation = 0.0f;
      for (atelier::TreeEdgeId next : tree.topology ().child_edges (child))
        continuation =
          std::max (continuation, radius[next].numerical_value_in (u::m));
      const float start = radius[edge].numerical_value_in (u::m);
      return continuation > 0.0f ? std::min (0.92f * start, continuation)
                                 : 0.42f * start;
    }

    float vertex_wind (const atelier::Tree& tree,
                       atelier::TreeVertexId vertex,
                       float maximum_generation) {
      if (vertex == 0)
        return 0.0f;
      const atelier::TreeEdgeId edge = tree.topology ().parent_edge (vertex);
      if (tree.topology ().edge (edge).organ == atelier::TreeOrgan::root)
        return 0.0f;
      const auto& flexibility =
        atelier::get<atelier::branch_flexibility> (tree.edges ());
      const float flex = flexibility[edge].numerical_value_in (one);
      const float height =
        static_cast<float> (tree.topology ().vertex (vertex).generation) /
        maximum_generation;
      return std::clamp (
        std::pow (height, 1.35f) * (0.35f + 0.65f * flex), 0.0f, 1.0f);
    }

    void append_leaves (render::DrawList& draw,
                        const atelier::Tree& tree,
                        const TreeSite& site,
                        const TreeFrame& frame,
                        const std::vector<Vec3>& local_positions,
                        const std::vector<Vec3>& world_positions,
                        float maximum_generation) {
      const auto& vigor = atelier::get<atelier::bud_vigor> (tree.vertices ());
      for (atelier::TreeVertexId vertex = 1;
           vertex < tree.topology ().vertex_count ();
           ++vertex) {
        const float strength = vigor[vertex].numerical_value_in (one);
        if (strength <= 0.0f)
          continue;
        const atelier::TreeEdgeId parent =
          tree.topology ().parent_edge (vertex);
        const Vec3 branch =
          normalized (local_positions[vertex] -
                      local_positions[tree.topology ().edge (parent).parent]);
        const float seed = tree.form (parent).material_seed;
        const float wind =
          vertex_wind (tree, vertex, maximum_generation) + 0.12f;
        const int lobe_count = site.cohort == TreeCohort::sapling ? 2 : 3;
        for (int lobe = 0; lobe < lobe_count; ++lobe) {
          const float turn = seed * 5.7f + 2.399963f * lobe;
          const Vec3 offset =
            frame.across * std::sin (turn) + frame.forward * std::cos (turn) +
            frame.up *
              (0.22f * static_cast<float> (lobe - 1) + 0.18f * branch[1]);
          const float spread = site.scale * (0.22f + 0.075f * lobe);
          const Vec3 center = world_positions[vertex] + offset * spread;
          const float size =
            site.scale * (0.25f + 0.085f * strength + 0.020f * lobe);
          const float hue = unit_hash (site.seed + 71 * vertex + 19 * lobe);
          const float youth = site.cohort == TreeCohort::sapling ? 1.0f
                              : site.cohort == TreeCohort::young ? 0.45f
                                                                 : 0.0f;
          draw.color (0.09f + 0.055f * hue + 0.025f * youth,
                      0.21f + 0.09f * site.habitat + 0.045f * hue +
                        0.050f * youth,
                      0.045f + 0.035f * hue + 0.015f * youth);
          draw.wind (std::clamp (wind, 0.0f, 1.0f) * proportion[one]);
          draw.push ();
          draw.translate (center);
          draw.rotate (turn * u::rad, frame.up);
          draw.scale (1.08f * size, 0.90f * size, size);
          draw.sphere (1.0f, 6, 3);
          draw.pop ();
        }
      }
    }

    void append_tree (render::DrawList& draw, const TreeSite& site) {
      const atelier::Tree tree (site.seed);
      const atelier::DirectedTreeTopology& topology = tree.topology ();
      const TreeFrame frame = frame_at (site);
      const std::vector<Vec3> local_positions = rest_positions (tree);
      std::vector<Vec3> world_positions (topology.vertex_count ());
      float maximum_generation = 1.0f;
      for (atelier::TreeVertexId vertex = 0; vertex < topology.vertex_count ();
           ++vertex) {
        world_positions[vertex] = place (site, frame, local_positions[vertex]);
        maximum_generation =
          std::max (maximum_generation,
                    static_cast<float> (topology.vertex (vertex).generation));
      }

      const auto& radius = atelier::get<atelier::branch_radius> (tree.edges ());
      for (atelier::TreeEdgeId id = 0; id < topology.edge_count (); ++id) {
        const atelier::TreeEdge& edge = topology.edge (id);
        const float r0 =
          1.18f * site.scale * radius[id].numerical_value_in (u::m);
        const float r1 = 1.18f * site.scale * branch_end_radius (tree, id);
        append_branch (draw,
                       world_positions[edge.parent],
                       world_positions[edge.child],
                       r0,
                       r1,
                       vertex_wind (tree, edge.parent, maximum_generation),
                       vertex_wind (tree, edge.child, maximum_generation),
                       tree.form (id).material_seed);
      }
      append_leaves (draw,
                     tree,
                     site,
                     frame,
                     local_positions,
                     world_positions,
                     maximum_generation);
    }

    struct PatchCandidate {
      float x;
      float z;
      float score;
    };

    std::vector<PatchCandidate>
    choose_forest_patches (const map::Surface& surface,
                           std::uint32_t seed,
                           std::size_t desired_count,
                           std::optional<Vec3> focus) {
      const map::SurfaceSections& samples = surface.sections ();
      const map::SurfaceDomain& domain = samples.domain ();
      const auto& cover = spatial::get<map::forest_cover> (samples);
      const auto& habitat = spatial::get<map::tree_habitat> (samples);
      const std::size_t margin = std::max<std::size_t> (
        3, std::min (domain.width (), domain.height ()) / 20);
      const std::size_t step = std::max<std::size_t> (
        1, std::min (domain.width (), domain.height ()) / 180);
      std::vector<PatchCandidate> candidates;
      for (std::size_t row = margin; row + margin < domain.height ();
           row += step)
        for (std::size_t column = margin; column + margin < domain.width ();
             column += step) {
          const map::SurfaceIndex index { column, row };
          const std::size_t offset = domain.offset (index);
          const float support = cover[offset].numerical_value_in (one);
          const float habitability = habitat[offset].numerical_value_in (one);
          if (support < 0.18f || habitability < 0.34f)
            continue;
          const float x =
            static_cast<float> (column) * meters_value (domain.spacing_x ());
          const float z =
            static_cast<float> (row) * meters_value (domain.spacing_z ());
          const float variation = unit_hash (
            seed ^ static_cast<std::uint32_t> (offset * 2654435761ULL));
          float score = support + 0.06f * variation;
          if (focus) {
            const float dx = x - (*focus)[0];
            const float dz = z - (*focus)[2];
            const float distance = std::sqrt (dx * dx + dz * dz);
            if (distance < 55.0f)
              continue;
            score -= 0.0012f * distance;
          }
          candidates.push_back ({ x, z, score });
        }
      std::ranges::sort (candidates, std::greater {}, &PatchCandidate::score);

      const std::size_t patch_count =
        std::clamp<std::size_t> ((desired_count + 47) / 48, 1, 2);
      std::vector<PatchCandidate> patches;
      constexpr float patch_separation = 82.0f;
      constexpr float forest_diameter = 260.0f;
      for (const PatchCandidate& candidate : candidates) {
        if (!patches.empty ()) {
          const float dx = candidate.x - patches.front ().x;
          const float dz = candidate.z - patches.front ().z;
          if (dx * dx + dz * dz > forest_diameter * forest_diameter)
            continue;
        }
        if (std::ranges::any_of (patches, [&] (const PatchCandidate& patch) {
              const float dx = candidate.x - patch.x;
              const float dz = candidate.z - patch.z;
              return dx * dx + dz * dz < patch_separation * patch_separation;
            }))
          continue;
        patches.push_back (candidate);
        if (patches.size () == patch_count)
          break;
      }
      return patches;
    }

    struct RecruitedTree {
      TreeSite site;
      float claim;
    };

    float crown_radius (const TreeSite& site) {
      return 1.2f + 2.4f * site.scale;
    }

    bool crown_has_room (const TreeSite& candidate,
                         const std::vector<TreeSite>& forest) {
      return std::ranges::none_of (forest, [&] (const TreeSite& established) {
        const float separation =
          0.72f * (crown_radius (candidate) + crown_radius (established));
        return horizontal_distance_squared (candidate.position,
                                            established.position) <
               separation * separation;
      });
    }
  }

  TreeGrove plan_tree_grove (const map::Surface& surface,
                             std::uint32_t seed,
                             std::size_t desired_count,
                             std::optional<Vec3> focus) {
    TreeGrove grove;
    if (desired_count == 0)
      return grove;

    const std::vector<PatchCandidate> patches =
      choose_forest_patches (surface, seed, desired_count, focus);
    if (patches.empty ())
      return grove;

    std::mt19937 random (seed ^ 0x6a09e667U);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);
    std::vector<RecruitedTree> recruits;
    recruits.reserve (std::max<std::size_t> (512, desired_count * 80));
    for (std::size_t patch = 0; patch < patches.size (); ++patch) {
      TreeSite elder = site_at (surface,
                                patches[patch].x,
                                patches[patch].z,
                                1.30f + 0.18f * unit (random),
                                seed ^ (0xa511e9b3U + 977U * patch));
      elder.cohort = TreeCohort::canopy;
      recruits.push_back ({ elder, 2.0f + elder.habitat });
    }

    const std::size_t recruitment_count =
      std::max<std::size_t> (512, desired_count * 80);
    const map::SurfaceDomain& domain = surface.sections ().domain ();
    const float maximum_x = meters_value (domain.maximum_interpolated_x ());
    const float maximum_z = meters_value (domain.maximum_interpolated_z ());
    for (std::size_t attempt = 0; attempt < recruitment_count; ++attempt) {
      const std::size_t patch = attempt % patches.size ();
      const float angle = 2.0f * std::numbers::pi_v<float> * unit (random);
      const float distance = 6.0f + 44.0f * std::pow (unit (random), 1.65f);
      const float x = patches[patch].x + std::sin (angle) * distance;
      const float z = patches[patch].z + std::cos (angle) * distance;
      if (x < 2.0f || z < 2.0f || x > maximum_x - 2.0f || z > maximum_z - 2.0f)
        continue;
      const float support = forest_cover_value (surface, x, z);
      if (support < 0.08f || habitat_value (surface, x, z) < 0.34f)
        continue;
      const float recruitment = unit (random);
      const float scale = recruitment < 0.34f   ? 0.38f + 0.26f * unit (random)
                          : recruitment < 0.76f ? 0.70f + 0.34f * unit (random)
                                                : 1.08f + 0.30f * unit (random);
      const float patch_falloff = 1.0f - distance / 62.0f;
      if (unit (random) > support * std::clamp (patch_falloff, 0.18f, 1.0f))
        continue;
      TreeSite site = site_at (
        surface,
        x,
        z,
        scale,
        seed ^ (0x9e3779b9U * static_cast<std::uint32_t> (attempt + 1)));
      const float claim =
        scale * (0.72f + 0.28f * support) + 0.08f * unit (random);
      recruits.push_back ({ site, claim });
    }

    std::ranges::sort (recruits, std::greater {}, &RecruitedTree::claim);
    grove.sites.reserve (desired_count);
    const std::size_t canopy_limit =
      desired_count == 1 ? 1
                         : std::max (patches.size (), (desired_count + 3) / 4);
    const std::size_t young_limit =
      desired_count == 1 ? 0 : (desired_count + 1) / 2;
    std::size_t canopy_count = 0;
    std::size_t young_count = 0;
    for (const RecruitedTree& recruit : recruits) {
      if (recruit.site.cohort == TreeCohort::canopy &&
          canopy_count == canopy_limit)
        continue;
      if (recruit.site.cohort == TreeCohort::young &&
          young_count == young_limit)
        continue;
      if (!crown_has_room (recruit.site, grove.sites))
        continue;
      grove.sites.push_back (recruit.site);
      canopy_count += recruit.site.cohort == TreeCohort::canopy ? 1 : 0;
      young_count += recruit.site.cohort == TreeCohort::young ? 1 : 0;
      if (grove.sites.size () == desired_count)
        break;
    }
    if (grove.sites.empty ())
      return grove;

    Vec3 center;
    for (const TreeSite& site : grove.sites)
      center += site.position;
    center /= static_cast<float> (grove.sites.size ());
    const float camera_angle =
      2.0f * std::numbers::pi_v<float> * unit_hash (seed ^ 0xcafef00dU);
    const Vec3 toward_camera (
      std::sin (camera_angle), 0.0f, std::cos (camera_angle));
    const bool portrait = desired_count == 1;
    float radius = 0.0f;
    for (const TreeSite& site : grove.sites)
      radius = std::max (
        radius,
        std::sqrt (horizontal_distance_squared (site.position, center)));
    const float camera_distance =
      portrait ? 24.0f : std::max (72.0f, 1.15f * radius + 48.0f);
    Vec3 eye = center + toward_camera * camera_distance;
    eye[0] = std::clamp (eye[0], 1.0f, maximum_x - 1.0f);
    eye[2] = std::clamp (eye[2], 1.0f, maximum_z - 1.0f);
    eye[1] = std::max (elevation_value (surface, eye[0], eye[2]) +
                         (portrait ? 4.0f : 8.0f),
                       center[1] + (portrait ? 6.0f : 12.0f));
    grove.camera_eye = eye;
    grove.camera_target = center + Vec3 (0, portrait ? 4.2f : 5.5f, 0);
    return grove;
  }

  void TreeStand::rebuild (render::Renderer& renderer,
                           const map::Surface& surface,
                           std::uint32_t seed,
                           std::size_t desired_count,
                           std::optional<Vec3> focus) {
    MOPPE_PROFILE_ZONE ("TreeStand::rebuild");
    m_grove = plan_tree_grove (surface, seed, desired_count, focus);
    m_mesh.reset ();
    if (m_grove.sites.empty ())
      return;

    render::DrawList draw;
    draw.state ().cull = false;
    for (const TreeSite& site : m_grove.sites)
      append_tree (draw, site);
    draw.wind (0.0f * proportion[one]);
    m_mesh = renderer.create_mesh (draw);
  }

  void TreeStand::draw (render::Renderer& renderer) const {
    if (m_mesh)
      renderer.draw_mesh (*m_mesh, Mat4 ());
  }
}
