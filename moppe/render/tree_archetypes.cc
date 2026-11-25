#include <moppe/render/tree_archetypes.hh>

#include <moppe/correct_math.hh>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace moppe::render {
  namespace {
    inline constexpr radians_t tau = PI2 * u::rad;
    inline constexpr radians_t half_turn = PI * u::rad;
    inline constexpr radians_t golden_angle = 2.39996323f * u::rad;
    inline constexpr proportion_t zero_share = 0.0f * proportion[one];
    inline constexpr proportion_t whole_share = 1.0f * proportion[one];
    inline constexpr scale_factor_t unit_scale = 1.0f * scale_factor[one];

    inline constexpr std::array<ForestSpecies, forest_species_count>
      all_species { ForestSpecies::Broadleaf,
                    ForestSpecies::Conifer,
                    ForestSpecies::Pine,
                    ForestSpecies::FellBirch,
                    ForestSpecies::Snag };

    // Typed geometry helpers over Moppe's Vec3 / position_t / displacement_t /
    // direction_t, keeping all transcendental calls on cr:: for bit-identical
    // archetypes on every platform.
    position_t origin_point () {
      return position (Vec3 (0.0f, 0.0f, 0.0f));
    }

    position_t offset_point (const position_t& from,
                             const displacement_t& delta) {
      return from + quantity_cast<isq::position_vector> (delta);
    }

    displacement_t displacement_between (const position_t& from,
                                         const position_t& to) {
      return quantity_cast<isq::displacement> (to - from);
    }

    displacement_t make_displacement (meters_t x, meters_t y, meters_t z) {
      return displacement (
        Vec3 (meters_value (x), meters_value (y), meters_value (z)));
    }

    position_t make_point (meters_t x, meters_t y, meters_t z) {
      return position (
        Vec3 (meters_value (x), meters_value (y), meters_value (z)));
    }

    meters_t height_of (const position_t& p) {
      return position_value (p)[1] * u::m;
    }

    meters_t length_of (const displacement_t& d) {
      return length (displacement_value (d)) * u::m;
    }

    position_t interpolate_point (const position_t& a,
                                  const position_t& b,
                                  blend_weight_t w) {
      const float alpha = w.numerical_value_in (one);
      return offset_point (a, displacement_between (a, b) * alpha);
    }

    template <Quantity Q>
    Q interpolate (Q a, Q b, blend_weight_t w) {
      return a + (b - a) * w.numerical_value_in (one);
    }

    direction_t horizontal_direction (heading_t azimuth) {
      const radians_t angle = azimuth.numerical_value_in (u::rad) * u::rad;
      return unit_direction (Vec3 (cos (angle), 0.0f, sin (angle)));
    }

    direction_t blend_directions (const direction_t& a,
                                  const direction_t& b,
                                  blend_weight_t w) {
      return unit_direction (normalized (linear_vector_interpolate (
        direction_value (a), direction_value (b), w)));
    }

    direction_t safe_direction (const Vec3& v) {
      const float len = length (v);
      return unit_direction (len > 1e-6f ? v * (1.0f / len)
                                         : Vec3 (0.0f, 1.0f, 0.0f));
    }

    proportion_t
    clamp_share (proportion_t p, proportion_t lo, proportion_t hi) {
      return std::clamp (p, lo, hi);
    }

    proportion_t share_of (meters_t value, meters_t span) {
      const float s = std::max (meters_value (span), 0.01f);
      return clamp_share (
        (meters_value (value) / s) * proportion[one], zero_share, whole_share);
    }

    proportion_t share_power (proportion_t p, float exponent) {
      return cr::pow (std::max (p.numerical_value_in (one), 0.0f), exponent) *
             proportion[one];
    }

    // Deterministic splitmix32 PRNG producing typed quantities directly.
    struct Rng {
      std::uint32_t m_state;

      explicit Rng (std::uint32_t seed) : m_state (seed ? seed : 0x9e3779b9u) {}

      std::uint32_t next_u32 () {
        std::uint32_t z = (m_state += 0x9e3779b9u);
        z = (z ^ (z >> 16)) * 0x85ebca6bu;
        z = (z ^ (z >> 13)) * 0xc2b2ae35u;
        return z ^ (z >> 16);
      }

      proportion_t next_share () {
        return (float (next_u32 () & 0x00ffffffu) / 16777216.0f) *
               proportion[one];
      }

      blend_weight_t next_weight () {
        return next_share ().numerical_value_in (one) * blend_weight[one];
      }

      heading_t next_heading () {
        return radians_t (next_share ().numerical_value_in (one) * tau)
                 .numerical_value_in (u::rad) *
               heading[u::rad];
      }

      template <Quantity Q>
      Q range (Q lo, Q hi) {
        return interpolate (lo, hi, next_weight ());
      }
    };

    // -- Storey 1: Intrinsic Organism Blueprint -----------------------------
    //
    // One blueprint represents a single tree variant in real metres,
    // independent of mesh tessellation or LOD tier. Every LOD tier is emitted
    // from this same blueprint, so silhouettes and attachments agree by
    // construction and volumetric canopy AO is computed from real 3-D
    // neighbours.

    struct StemNode {
      position_t pos;
      meters_t radius;
      proportion_t flutter = zero_share;
      proportion_t flare = zero_share;
      meters_t jag = 0.0f * u::m;
    };

    struct StemSpec {
      TreePart part = TreePart::trunk;
      RadialFrame radial = RadialFrame::trunk_radius;
      std::uint8_t min_tier = 0;
      bool cap_top = false;
      radians_t ring_phase = 0.0f * u::rad;
      scale_factor_t buttress_lobes = 4.0f * scale_factor[one];
      std::uint32_t jag_seed = 0;
      std::vector<StemNode> nodes;
    };

    struct FoliagePuffSpec {
      position_t centre;
      meters_t rx, ry, rz;
      proportion_t droop;
      proportion_t clump_seed;
      proportion_t flutter;
    };

    struct SpruceFrondSpec {
      position_t attach;
      heading_t azimuth;
      meters_t length;
      meters_t width;
      meters_t thickness;
      meters_t droop;
      meters_t upturn;
      proportion_t clump_seed;
      proportion_t flutter;
    };

    struct OccluderSphere {
      position_t centre;
      meters_t radius;
      std::size_t owner_tag;
    };

    struct TreeBlueprint {
      TreeSpeciesProfile profile;
      meters_t trunk_radius;
      position_t crown_core;
      meters_t crown_y0;
      meters_t crown_y1;
      std::vector<StemSpec> stems;
      std::vector<FoliagePuffSpec> puffs;
      std::vector<SpruceFrondSpec> fronds;
      std::vector<OccluderSphere> occluders;
      std::array<std::size_t, 3> puff_counts {};
      std::array<scale_factor_t, 3> puff_boosts { unit_scale,
                                                  unit_scale,
                                                  unit_scale };
      std::array<std::size_t, 3> frond_counts {};
      std::array<scale_factor_t, 3> frond_width_boosts { unit_scale,
                                                         unit_scale,
                                                         unit_scale };
      std::array<scale_factor_t, 3> frond_thick_boosts { unit_scale,
                                                         unit_scale,
                                                         unit_scale };
    };

    void populate_occluders (TreeBlueprint& bp) {
      bp.occluders.clear ();
      bp.occluders.reserve (bp.puffs.size () + bp.fronds.size () * 2);
      for (std::size_t i = 0; i < bp.puffs.size (); ++i) {
        const FoliagePuffSpec& pf = bp.puffs[i];
        const meters_t mean_r = (pf.rx + pf.ry + pf.rz) * (1.0f / 3.0f);
        bp.occluders.push_back ({ pf.centre, mean_r, i });
      }
      const std::size_t frond_tag_base = bp.puffs.size ();
      for (std::size_t i = 0; i < bp.fronds.size (); ++i) {
        const SpruceFrondSpec& fr = bp.fronds[i];
        const direction_t dir = horizontal_direction (fr.azimuth);
        for (const float frac : { 0.38f, 0.72f }) {
          const meters_t dip =
            -fr.droop * sin (cr::pow (frac, 0.82f) * half_turn);
          const meters_t lift = fr.upturn * cr::pow (frac, 2.3f);
          const position_t p = offset_point (
            fr.attach,
            along (dir, fr.length * frac) +
              make_displacement (0.0f * u::m, dip + lift, 0.0f * u::m));
          const meters_t r = (fr.width * 0.65f + fr.thickness * 0.55f);
          bp.occluders.push_back ({ p, r, frond_tag_base + i });
        }
      }
    }

    // Real 3-D volumetric canopy ambient occlusion from all neighbouring
    // foliage volumes in the blueprint: solid-angle hemisphere blockage
    // weighted toward overhead sky, converted via Beer-Lambert attenuation.
    proportion_t volumetric_ao (const position_t& sample,
                                const direction_t& outward,
                                const TreeBlueprint& bp,
                                std::size_t owner_tag,
                                bool is_foliage) {
      const Vec3 n = direction_value (outward);
      magnitude_t optical_depth = 0.0f * one;
      for (const OccluderSphere& occ : bp.occluders) {
        if (occ.owner_tag == owner_tag)
          continue;
        const displacement_t delta = displacement_between (sample, occ.centre);
        const meters_t dist = length_of (delta);
        if (dist < 0.01f * u::m)
          continue;
        const direction_t to_occ = direction_of (delta);
        const proportion_t hemi =
          clamp_share (dot (direction_value (to_occ), n) * proportion[one],
                       zero_share,
                       whole_share);
        const proportion_t sky_bias = clamp_share (
          (0.42f + 0.58f * direction_value (to_occ)[1]) * proportion[one],
          zero_share,
          whole_share);
        const auto r2 = occ.radius * occ.radius;
        const magnitude_t solid =
          std::min (r2 / (dist * dist + 0.35f * r2), 1.4f * one);
        optical_depth += solid *
                         (0.35f + 0.65f * hemi.numerical_value_in (one)) *
                         sky_bias.numerical_value_in (one);
      }

      const proportion_t crown_h =
        share_of (height_of (sample) - bp.crown_y0, bp.crown_y1 - bp.crown_y0);
      const proportion_t sky_exposure =
        interpolate (0.78f * proportion[one],
                     whole_share,
                     crown_h.numerical_value_in (one) * blend_weight[one]);
      const scale_factor_t upward_cue =
        (0.88f + 0.14f * std::max (n[1], -0.6f)) * scale_factor[one];
      const magnitude_t extinction = (is_foliage ? 0.21f : 0.14f) * one;
      const proportion_t transmitted =
        cr::exp (-scalar_value (extinction * optical_depth)) * proportion[one];
      const proportion_t raw_ao = (transmitted.numerical_value_in (one) *
                                   sky_exposure.numerical_value_in (one) *
                                   upward_cue.numerical_value_in (one)) *
                                  proportion[one];
      return clamp_share (raw_ao, 0.36f * proportion[one], whole_share);
    }

    // -- Species Blueprint Planners -----------------------------------------

    TreeBlueprint plan_birch (std::uint32_t variant) {
      Rng rng (0x1001u + variant * 7919u);
      TreeBlueprint bp {};
      bp.profile = tree_species_profile (ForestSpecies::Broadleaf);
      const meters_t H = bp.profile.reference_height;
      const meters_t R = bp.profile.reference_crown_radius;
      const meters_t r0 =
        H * bp.profile.trunk_radius_ratio.numerical_value_in (one);
      bp.trunk_radius = r0;
      bp.crown_y0 =
        H * rng.range (0.38f * proportion[one], 0.46f * proportion[one])
              .numerical_value_in (one);
      bp.crown_y1 = H;
      bp.crown_core = make_point (
        0.0f * u::m,
        interpolate (bp.crown_y0, bp.crown_y1, 0.48f * blend_weight[one]),
        0.0f * u::m);

      const heading_t curve_dir = rng.next_heading ();
      const direction_t curve_vec = horizontal_direction (curve_dir);
      const meters_t curve_amp =
        r0 * rng.range (0.45f * scale_factor[one], 1.15f * scale_factor[one])
               .numerical_value_in (one);
      auto trunk_at = [&] (proportion_t t) -> position_t {
        const float tf = t.numerical_value_in (one);
        const meters_t sway = curve_amp * (sin (tf * 1.15f * half_turn) * tf);
        const meters_t y =
          interpolate (-0.025f * H, 0.84f * H, tf * blend_weight[one]);
        return offset_point (make_point (0.0f * u::m, y, 0.0f * u::m),
                             along (curve_vec, sway));
      };

      StemSpec trunk {};
      trunk.part = TreePart::trunk;
      trunk.radial = RadialFrame::trunk_radius;
      trunk.min_tier = 0;
      trunk.cap_top = true;
      trunk.ring_phase = rng.range (0.0f * u::rad, tau);
      trunk.buttress_lobes =
        rng.range (3.0f * scale_factor[one], 5.5f * scale_factor[one]);
      trunk.jag_seed = 0x101u + variant;
      for (int i = 0; i < 6; ++i) {
        const proportion_t t = (float (i) / 5.0f) * proportion[one];
        const scale_factor_t taper =
          i == 0
            ? 1.85f * scale_factor[one]
            : interpolate (1.06f * scale_factor[one],
                           0.16f * scale_factor[one],
                           share_power (t, 0.85f).numerical_value_in (one) *
                             blend_weight[one]);
        trunk.nodes.push_back (
          { trunk_at (t),
            r0 * taper.numerical_value_in (one),
            (t.numerical_value_in (one) * t.numerical_value_in (one) * 0.25f) *
              proportion[one],
            i == 0 ? 0.35f * proportion[one] : zero_share,
            0.0f * u::m });
      }
      bp.stems.push_back (std::move (trunk));

      struct SubCrown {
        position_t tip;
        meters_t radius;
        heading_t azimuth;
        proportion_t seed;
      };
      constexpr int limb_count = 6;
      std::vector<SubCrown> limbs;
      const heading_t base_az = rng.next_heading ();

      // Apex tuft anchors the top of the crown in all LODs.
      bp.puffs.push_back ({ make_point (0.0f * u::m, H * 0.90f, 0.0f * u::m),
                            R * 0.36f,
                            H * 0.11f,
                            R * 0.34f,
                            0.32f * proportion[one],
                            rng.next_share (),
                            0.92f * proportion[one] });

      for (int i = 0; i < limb_count; ++i) {
        const proportion_t frac =
          (float (i) / float (limb_count - 1)) * proportion[one];
        const blend_weight_t w_frac =
          frac.numerical_value_in (one) * blend_weight[one];
        const heading_t az =
          base_az +
          quantity_cast<heading> (float (i) * golden_angle +
                                  rng.range (-0.28f * u::rad, 0.28f * u::rad));
        const direction_t dir = horizontal_direction (az);
        const proportion_t attach_t = interpolate (
          0.48f * proportion[one], 0.84f * proportion[one], w_frac);
        const position_t attach = trunk_at (attach_t);
        const float env = cr::pow (
          sin ((0.14f + 0.78f * frac.numerical_value_in (one)) * half_turn),
          0.7f);
        const meters_t reach =
          R * (env *
               rng.range (0.42f * scale_factor[one], 0.82f * scale_factor[one])
                 .numerical_value_in (one));
        const meters_t rise_m =
          H * interpolate (0.12f * proportion[one],
                           0.24f * proportion[one],
                           (1.0f - 0.4f * frac.numerical_value_in (one)) *
                             blend_weight[one])
                .numerical_value_in (one);
        const position_t tip = make_point (
          position_value (attach)[0] * u::m + direction_value (dir)[0] * reach,
          std::min (height_of (attach) + rise_m, H * 0.94f),
          position_value (attach)[2] * u::m + direction_value (dir)[2] * reach);
        const meters_t sub_r =
          R *
          interpolate (0.36f * proportion[one], 0.26f * proportion[one], w_frac)
            .numerical_value_in (one) *
          rng.range (0.88f * scale_factor[one], 1.16f * scale_factor[one])
            .numerical_value_in (one);
        const proportion_t seed = rng.next_share ();
        limbs.push_back ({ tip, sub_r, az, seed });

        StemSpec branch {};
        branch.part = TreePart::branch;
        branch.radial = RadialFrame::crown_radius;
        branch.min_tier = 1;
        branch.ring_phase = rng.range (0.0f * u::rad, tau);
        const position_t root_anchor =
          make_point (position_value (attach)[0] * 0.25f * u::m,
                      height_of (attach),
                      position_value (attach)[2] * 0.25f * u::m);
        const position_t mid = offset_point (
          interpolate_point (attach, tip, 0.52f * blend_weight[one]),
          along (dir, reach * 0.08f) +
            make_displacement (0.0f * u::m, rise_m * 0.12f, 0.0f * u::m));
        const position_t end =
          interpolate_point (attach, tip, 0.88f * blend_weight[one]);
        branch.nodes = {
          { root_anchor,
            r0 * interpolate (
                   0.42f * scale_factor[one], 0.26f * scale_factor[one], w_frac)
                   .numerical_value_in (one),
            0.12f * proportion[one] },
          { mid,
            r0 * interpolate (
                   0.24f * scale_factor[one], 0.14f * scale_factor[one], w_frac)
                   .numerical_value_in (one),
            0.24f * proportion[one] },
          { end, r0 * 0.08f, 0.36f * proportion[one] },
        };
        bp.stems.push_back (std::move (branch));

        bp.puffs.push_back (
          { tip,
            sub_r,
            sub_r *
              rng.range (0.95f * scale_factor[one], 1.25f * scale_factor[one])
                .numerical_value_in (one),
            sub_r *
              rng.range (0.85f * scale_factor[one], 1.12f * scale_factor[one])
                .numerical_value_in (one),
            0.32f * proportion[one],
            seed,
            interpolate (
              0.45f * proportion[one], 0.95f * proportion[one], w_frac) });
      }

      // Five layered weeping satellite rounds enrich mid and hero tiers.
      for (int round = 0; round < 5; ++round) {
        for (const SubCrown& sc : limbs) {
          const heading_t sub_az =
            sc.azimuth + quantity_cast<heading> (
                           float (round) * 1.32f * u::rad +
                           rng.range (-0.28f * u::rad, 0.28f * u::rad));
          const direction_t dir = horizontal_direction (sub_az);
          const meters_t out =
            sc.radius *
            rng.range (0.42f * scale_factor[one], 1.02f * scale_factor[one])
              .numerical_value_in (one);
          const meters_t dy =
            sc.radius * (round >= 3 ? rng.range (-0.78f * scale_factor[one],
                                                 -0.10f * scale_factor[one])
                                    : rng.range (-0.24f * scale_factor[one],
                                                 0.54f * scale_factor[one]))
                          .numerical_value_in (one);
          const position_t p =
            offset_point (make_point (position_value (sc.tip)[0] * u::m,
                                      std::clamp (height_of (sc.tip) + dy,
                                                  bp.crown_y0 * 0.92f,
                                                  H * 0.98f),
                                      position_value (sc.tip)[2] * u::m),
                          along (dir, out));
          const meters_t rad =
            sc.radius * (round >= 3 ? rng.range (0.38f * scale_factor[one],
                                                 0.58f * scale_factor[one])
                                    : rng.range (0.46f * scale_factor[one],
                                                 0.70f * scale_factor[one]))
                          .numerical_value_in (one);
          const proportion_t dist_share = share_of (
            length_of (displacement_between (bp.crown_core, p)), R * 1.15f);
          const proportion_t seed = clamp_share (
            (sc.seed.numerical_value_in (one) +
             rng.range (-0.18f * scale_factor[one], 0.18f * scale_factor[one])
               .numerical_value_in (one)) *
              proportion[one],
            zero_share,
            whole_share);
          bp.puffs.push_back (
            { p,
              rad,
              rad *
                rng.range (0.88f * scale_factor[one], 1.24f * scale_factor[one])
                  .numerical_value_in (one),
              rad *
                rng.range (0.82f * scale_factor[one], 1.14f * scale_factor[one])
                  .numerical_value_in (one),
              0.32f * proportion[one],
              seed,
              interpolate (0.55f * proportion[one],
                           whole_share,
                           dist_share.numerical_value_in (one) *
                             blend_weight[one]) });
        }
      }

      bp.puff_counts = { 5, 12, bp.puffs.size () };
      bp.puff_boosts = { 1.54f * scale_factor[one],
                         1.22f * scale_factor[one],
                         0.92f * scale_factor[one] };
      populate_occluders (bp);
      return bp;
    }

    TreeBlueprint plan_spruce (std::uint32_t variant) {
      Rng rng (0x2002u + variant * 6133u);
      TreeBlueprint bp {};
      bp.profile = tree_species_profile (ForestSpecies::Conifer);
      const meters_t H = bp.profile.reference_height;
      const meters_t R = bp.profile.reference_crown_radius;
      const meters_t r0 =
        H * bp.profile.trunk_radius_ratio.numerical_value_in (one);
      bp.trunk_radius = r0;
      bp.crown_y0 =
        H * rng.range (0.09f * proportion[one], 0.14f * proportion[one])
              .numerical_value_in (one);
      bp.crown_y1 = H;
      bp.crown_core = make_point (0.0f * u::m, H * 0.45f, 0.0f * u::m);

      StemSpec trunk {};
      trunk.part = TreePart::trunk;
      trunk.radial = RadialFrame::trunk_radius;
      trunk.min_tier = 0;
      trunk.cap_top = true;
      trunk.ring_phase = rng.range (0.0f * u::rad, tau);
      trunk.buttress_lobes =
        rng.range (3.5f * scale_factor[one], 5.5f * scale_factor[one]);
      trunk.jag_seed = 0x202u + variant;
      for (int i = 0; i < 5; ++i) {
        const proportion_t t = (float (i) / 4.0f) * proportion[one];
        const float tf = t.numerical_value_in (one);
        const scale_factor_t taper =
          i == 0
            ? 1.95f * scale_factor[one]
            : interpolate (1.05f * scale_factor[one],
                           0.09f * scale_factor[one],
                           share_power (t, 0.90f).numerical_value_in (one) *
                             blend_weight[one]);
        trunk.nodes.push_back (
          { make_point (
              0.0f * u::m,
              interpolate (-0.025f * H, 0.94f * H, tf * blend_weight[one]),
              0.0f * u::m),
            r0 * taper.numerical_value_in (one),
            (tf * tf * 0.18f) * proportion[one],
            i == 0 ? 0.45f * proportion[one] : zero_share,
            0.0f * u::m });
      }
      bp.stems.push_back (std::move (trunk));

      // Eight spiral whorls ordered by spoke round so coarsest LODs sketch
      // the full spruce spire symmetrically before filling interior spokes.
      constexpr int whorls = 8;
      const heading_t base_az = rng.next_heading ();
      for (int round = 0; round < 5; ++round) {
        for (int wh = 0; wh < whorls; ++wh) {
          const int max_spokes = wh >= 6 ? 3 : (wh >= 4 ? 4 : 5);
          if (round >= max_spokes)
            continue;
          const proportion_t wf =
            (float (wh) / float (whorls - 1)) * proportion[one];
          const blend_weight_t w_wf =
            wf.numerical_value_in (one) * blend_weight[one];
          const meters_t y =
            interpolate (bp.crown_y0,
                         H * 0.89f,
                         share_power (wf, 0.94f).numerical_value_in (one) *
                           blend_weight[one]) +
            rng.range (-0.018f * H, 0.018f * H);
          const heading_t az =
            base_az + quantity_cast<heading> (
                        float (wh) * 1.18f * u::rad +
                        tau * (float (round) / float (max_spokes)) +
                        rng.range (-0.22f * u::rad, 0.22f * u::rad));
          const float spire =
            cr::pow (1.0f - 0.82f * wf.numerical_value_in (one), 1.15f) *
            (wh == 0 ? 0.86f : (wh == 1 ? 1.04f : 1.0f));
          const meters_t len =
            R * spire *
            rng.range (0.86f * scale_factor[one], 1.14f * scale_factor[one])
              .numerical_value_in (one);
          const meters_t width =
            len *
            interpolate (
              0.34f * scale_factor[one], 0.44f * scale_factor[one], w_wf)
              .numerical_value_in (one) *
            rng.range (0.88f * scale_factor[one], 1.15f * scale_factor[one])
              .numerical_value_in (one);
          const meters_t thickness =
            interpolate (0.58f * u::m, 0.28f * u::m, w_wf) *
            (meters_value (H) / 24.0f) *
            rng.range (0.85f * scale_factor[one], 1.15f * scale_factor[one])
              .numerical_value_in (one);
          const meters_t droop = len * interpolate (0.36f * scale_factor[one],
                                                    0.08f * scale_factor[one],
                                                    w_wf)
                                         .numerical_value_in (one);
          const meters_t upturn = len * interpolate (0.20f * scale_factor[one],
                                                     0.14f * scale_factor[one],
                                                     w_wf)
                                          .numerical_value_in (one);
          bp.fronds.push_back ({ make_point (0.0f * u::m, y, 0.0f * u::m),
                                 az,
                                 len,
                                 width,
                                 thickness,
                                 droop,
                                 upturn,
                                 rng.next_share (),
                                 interpolate (0.30f * proportion[one],
                                              0.85f * proportion[one],
                                              w_wf) });
        }
      }

      // Narrow feathery leader spire capping the top of the spruce.
      bp.puffs.push_back ({ make_point (0.0f * u::m, H * 0.93f, 0.0f * u::m),
                            R * 0.22f,
                            H * 0.09f,
                            R * 0.22f,
                            0.12f * proportion[one],
                            rng.next_share (),
                            0.88f * proportion[one] });

      bp.puff_counts = { 1, 1, 1 };
      bp.puff_boosts = { 1.25f * scale_factor[one], unit_scale, unit_scale };
      bp.frond_counts = { 9, 20, bp.fronds.size () };
      bp.frond_width_boosts = { 1.72f * scale_factor[one],
                                1.26f * scale_factor[one],
                                unit_scale };
      bp.frond_thick_boosts = { 1.48f * scale_factor[one],
                                1.20f * scale_factor[one],
                                unit_scale };
      populate_occluders (bp);
      return bp;
    }

    TreeBlueprint plan_pine (std::uint32_t variant) {
      Rng rng (0x3003u + variant * 8291u);
      TreeBlueprint bp {};
      bp.profile = tree_species_profile (ForestSpecies::Pine);
      const meters_t H = bp.profile.reference_height;
      const meters_t R = bp.profile.reference_crown_radius;
      const meters_t r0 =
        H * bp.profile.trunk_radius_ratio.numerical_value_in (one);
      bp.trunk_radius = r0;
      bp.crown_y0 =
        H * rng.range (0.54f * proportion[one], 0.62f * proportion[one])
              .numerical_value_in (one);
      bp.crown_y1 = H;
      bp.crown_core = make_point (
        0.0f * u::m,
        interpolate (bp.crown_y0, bp.crown_y1, 0.42f * blend_weight[one]),
        0.0f * u::m);

      const heading_t curve_dir = rng.next_heading ();
      const direction_t curve_vec = horizontal_direction (curve_dir);
      const meters_t curve_amp =
        r0 * rng.range (0.65f * scale_factor[one], 1.50f * scale_factor[one])
               .numerical_value_in (one);
      auto trunk_at = [&] (proportion_t t) -> position_t {
        const float tf = t.numerical_value_in (one);
        const meters_t sway = curve_amp * (sin (tf * 0.95f * half_turn) * tf);
        const meters_t y =
          interpolate (-0.025f * H, 0.82f * H, tf * blend_weight[one]);
        return offset_point (make_point (0.0f * u::m, y, 0.0f * u::m),
                             along (curve_vec, sway));
      };

      StemSpec trunk {};
      trunk.part = TreePart::trunk;
      trunk.radial = RadialFrame::trunk_radius;
      trunk.min_tier = 0;
      trunk.cap_top = true;
      trunk.ring_phase = rng.range (0.0f * u::rad, tau);
      trunk.buttress_lobes =
        rng.range (3.0f * scale_factor[one], 5.0f * scale_factor[one]);
      trunk.jag_seed = 0x303u + variant;
      for (int i = 0; i < 6; ++i) {
        const proportion_t t = (float (i) / 5.0f) * proportion[one];
        const float tf = t.numerical_value_in (one);
        const scale_factor_t taper =
          i == 0
            ? 1.80f * scale_factor[one]
            : interpolate (1.06f * scale_factor[one],
                           0.30f * scale_factor[one],
                           share_power (t, 0.82f).numerical_value_in (one) *
                             blend_weight[one]);
        trunk.nodes.push_back ({ trunk_at (t),
                                 r0 * taper.numerical_value_in (one),
                                 (tf * tf * 0.24f) * proportion[one],
                                 i == 0 ? 0.32f * proportion[one] : zero_share,
                                 0.0f * u::m });
      }
      bp.stems.push_back (std::move (trunk));

      // Broad billowing crown-top cloud cushion.
      bp.puffs.push_back ({ make_point (rng.range (-0.12f * R, 0.12f * R),
                                        H * 0.91f,
                                        rng.range (-0.12f * R, 0.12f * R)),
                            R * 0.46f,
                            R * 0.28f,
                            R * 0.42f,
                            0.08f * proportion[one],
                            rng.next_share (),
                            0.82f * proportion[one] });

      struct LimbInfo {
        position_t tip;
        heading_t azimuth;
        meters_t pad_radius;
        proportion_t seed;
      };
      constexpr int limb_count = 5;
      std::vector<LimbInfo> limbs;
      const heading_t base_az = rng.next_heading ();
      for (int i = 0; i < limb_count; ++i) {
        const proportion_t frac =
          (float (i) / float (limb_count - 1)) * proportion[one];
        const blend_weight_t w_frac =
          frac.numerical_value_in (one) * blend_weight[one];
        const heading_t az =
          base_az +
          quantity_cast<heading> (float (i) * golden_angle +
                                  rng.range (-0.30f * u::rad, 0.30f * u::rad));
        const direction_t dir = horizontal_direction (az);
        const proportion_t attach_t = interpolate (
          0.68f * proportion[one], 0.96f * proportion[one], w_frac);
        const position_t attach = trunk_at (attach_t);
        const meters_t reach =
          R * rng.range (0.58f * scale_factor[one], 0.92f * scale_factor[one])
                .numerical_value_in (one);
        const meters_t lift =
          H * rng.range (0.06f * proportion[one], 0.16f * proportion[one])
                .numerical_value_in (one);
        const position_t elbow =
          offset_point (attach,
                        along (dir, reach * 0.52f) +
                          make_displacement (
                            0.0f * u::m,
                            lift * 0.35f - rng.range (0.0f * u::m, 0.025f * H),
                            0.0f * u::m));
        const position_t tip = offset_point (
          make_point (position_value (attach)[0] * u::m,
                      std::min (height_of (attach) + lift, H * 0.93f),
                      position_value (attach)[2] * u::m),
          along (dir, reach));
        const meters_t pad_r =
          R * rng.range (0.34f * scale_factor[one], 0.46f * scale_factor[one])
                .numerical_value_in (one);
        const proportion_t seed = rng.next_share ();
        limbs.push_back ({ tip, az, pad_r, seed });

        StemSpec branch {};
        branch.part = TreePart::branch;
        branch.radial = RadialFrame::crown_radius;
        branch.min_tier = 1;
        branch.ring_phase = rng.range (0.0f * u::rad, tau);
        const position_t root_anchor =
          make_point (position_value (attach)[0] * 0.20f * u::m,
                      height_of (attach),
                      position_value (attach)[2] * 0.20f * u::m);
        branch.nodes = {
          { root_anchor,
            r0 * interpolate (
                   0.52f * scale_factor[one], 0.34f * scale_factor[one], w_frac)
                   .numerical_value_in (one),
            0.16f * proportion[one] },
          { elbow,
            r0 * interpolate (
                   0.34f * scale_factor[one], 0.20f * scale_factor[one], w_frac)
                   .numerical_value_in (one),
            0.26f * proportion[one] },
          { interpolate_point (elbow, tip, 0.85f * blend_weight[one]),
            r0 * 0.12f,
            0.36f * proportion[one] },
        };
        bp.stems.push_back (std::move (branch));

        bp.puffs.push_back (
          { offset_point (
              tip, make_displacement (0.0f * u::m, pad_r * 0.22f, 0.0f * u::m)),
            pad_r,
            pad_r *
              rng.range (0.56f * scale_factor[one], 0.70f * scale_factor[one])
                .numerical_value_in (one),
            pad_r *
              rng.range (0.86f * scale_factor[one], 1.12f * scale_factor[one])
                .numerical_value_in (one),
            0.08f * proportion[one],
            seed,
            interpolate (
              0.55f * proportion[one], 0.90f * proportion[one], w_frac) });
      }

      // Five billowing cloud rounds perched around and atop each bough.
      for (int round = 0; round < 5; ++round) {
        for (const LimbInfo& li : limbs) {
          const heading_t sub_az =
            li.azimuth + quantity_cast<heading> (
                           float (round) * 1.28f * u::rad +
                           rng.range (-0.28f * u::rad, 0.28f * u::rad));
          const direction_t dir = horizontal_direction (sub_az);
          const meters_t out =
            li.pad_radius * (round >= 3 ? rng.range (0.15f * scale_factor[one],
                                                     0.55f * scale_factor[one])
                                        : rng.range (0.36f * scale_factor[one],
                                                     0.84f * scale_factor[one]))
                              .numerical_value_in (one);
          const meters_t dy =
            li.pad_radius * (round >= 3 ? rng.range (0.18f * scale_factor[one],
                                                     0.48f * scale_factor[one])
                                        : rng.range (-0.12f * scale_factor[one],
                                                     0.36f * scale_factor[one]))
                              .numerical_value_in (one);
          const position_t p = offset_point (
            make_point (
              position_value (li.tip)[0] * u::m,
              std::clamp (height_of (li.tip) + dy, bp.crown_y0, H * 0.99f),
              position_value (li.tip)[2] * u::m),
            along (dir, out));
          const meters_t rad =
            li.pad_radius * (round >= 3 ? rng.range (0.40f * scale_factor[one],
                                                     0.62f * scale_factor[one])
                                        : rng.range (0.48f * scale_factor[one],
                                                     0.74f * scale_factor[one]))
                              .numerical_value_in (one);
          const proportion_t h_share =
            share_of (height_of (p) - bp.crown_y0, bp.crown_y1 - bp.crown_y0);
          const proportion_t seed = clamp_share (
            (li.seed.numerical_value_in (one) +
             rng.range (-0.15f * scale_factor[one], 0.15f * scale_factor[one])
               .numerical_value_in (one)) *
              proportion[one],
            zero_share,
            whole_share);
          bp.puffs.push_back (
            { p,
              rad,
              rad *
                rng.range (0.54f * scale_factor[one], 0.70f * scale_factor[one])
                  .numerical_value_in (one),
              rad *
                rng.range (0.84f * scale_factor[one], 1.12f * scale_factor[one])
                  .numerical_value_in (one),
              0.08f * proportion[one],
              seed,
              interpolate (0.50f * proportion[one],
                           0.92f * proportion[one],
                           h_share.numerical_value_in (one) *
                             blend_weight[one]) });
        }
      }

      bp.puff_counts = { 5, 11, bp.puffs.size () };
      bp.puff_boosts = { 1.54f * scale_factor[one],
                         1.22f * scale_factor[one],
                         0.92f * scale_factor[one] };
      populate_occluders (bp);
      return bp;
    }

    TreeBlueprint plan_fell_birch (std::uint32_t variant) {
      Rng rng (0x4004u + variant * 5381u);
      TreeBlueprint bp {};
      bp.profile = tree_species_profile (ForestSpecies::FellBirch);
      const meters_t H = bp.profile.reference_height;
      const meters_t R = bp.profile.reference_crown_radius;
      const meters_t r0 =
        H * bp.profile.trunk_radius_ratio.numerical_value_in (one);
      bp.trunk_radius = r0;
      bp.crown_y0 = H * 0.24f;
      bp.crown_y1 = H;
      bp.crown_core = make_point (0.0f * u::m, H * 0.52f, 0.0f * u::m);

      constexpr int stem_count = 5;
      const heading_t base_az = rng.next_heading ();
      struct StemInfo {
        position_t top, twig_tip;
        heading_t kink_az;
        proportion_t seed;
      };
      std::vector<StemInfo> stems;

      for (int i = 0; i < stem_count; ++i) {
        const heading_t az =
          base_az +
          quantity_cast<heading> (float (i) * golden_angle +
                                  rng.range (-0.30f * u::rad, 0.30f * u::rad));
        const direction_t dir = horizontal_direction (az);
        const meters_t reach =
          R * rng.range (0.56f * scale_factor[one], 0.96f * scale_factor[one])
                .numerical_value_in (one);
        const meters_t top_y =
          H * rng.range (0.64f * proportion[one], 0.96f * proportion[one])
                .numerical_value_in (one);
        const position_t p0 =
          offset_point (make_point (0.0f * u::m, -0.03f * H, 0.0f * u::m),
                        along (dir, r0 * 0.8f));
        const position_t p1 = offset_point (
          make_point (
            0.0f * u::m,
            H * rng.range (0.08f * proportion[one], 0.16f * proportion[one])
                  .numerical_value_in (one),
            0.0f * u::m),
          along (dir, reach * 0.34f));
        const heading_t kink_az =
          az +
          quantity_cast<heading> (rng.range (-0.42f * u::rad, 0.42f * u::rad));
        const direction_t kink_dir = horizontal_direction (kink_az);
        const position_t knee = offset_point (
          make_point (
            0.0f * u::m,
            H * rng.range (0.20f * proportion[one], 0.34f * proportion[one])
                  .numerical_value_in (one),
            0.0f * u::m),
          along (dir, reach * 0.62f));
        const position_t p3 = offset_point (
          make_point (
            position_value (knee)[0] * u::m,
            interpolate (height_of (knee), top_y, 0.56f * blend_weight[one]),
            position_value (knee)[2] * u::m),
          along (kink_dir, reach * 0.26f));
        const position_t top =
          offset_point (make_point (position_value (knee)[0] * u::m,
                                    top_y,
                                    position_value (knee)[2] * u::m),
                        along (kink_dir, reach * 0.42f));

        const float side_sign =
          rng.next_share () > 0.5f * proportion[one] ? 1.0f : -1.0f;
        const heading_t tw_az =
          kink_az + quantity_cast<heading> (
                      side_sign * rng.range (0.50f * u::rad, 0.90f * u::rad));
        const direction_t tw_dir = horizontal_direction (tw_az);
        const position_t tw_start =
          interpolate_point (knee, p3, 0.55f * blend_weight[one]);
        const position_t tw_tip = offset_point (
          make_point (
            position_value (tw_start)[0] * u::m,
            std::clamp (
              height_of (tw_start) +
                H * rng.range (0.18f * proportion[one], 0.34f * proportion[one])
                      .numerical_value_in (one),
              H * 0.38f,
              H * 0.92f),
            position_value (tw_start)[2] * u::m),
          along (
            tw_dir,
            R * rng.range (0.28f * scale_factor[one], 0.46f * scale_factor[one])
                  .numerical_value_in (one)));

        const proportion_t seed = rng.next_share ();
        stems.push_back ({ top, tw_tip, kink_az, seed });

        const meters_t girth =
          r0 * rng.range (0.85f * scale_factor[one], 1.18f * scale_factor[one])
                 .numerical_value_in (one);
        StemSpec main_stem {};
        main_stem.part = TreePart::trunk;
        main_stem.radial = RadialFrame::crown_radius;
        main_stem.min_tier = i < 3 ? 0 : (i < 4 ? 1 : 2);
        main_stem.ring_phase = rng.range (0.0f * u::rad, tau);
        main_stem.buttress_lobes = 3.5f * scale_factor[one];
        main_stem.nodes = {
          { p0, girth * 1.45f, zero_share, 0.25f * proportion[one] },
          { p1, girth * 0.96f, 0.05f * proportion[one] },
          { knee, girth * 0.78f, 0.14f * proportion[one] },
          { p3, girth * 0.46f, 0.28f * proportion[one] },
          { top, girth * 0.20f, 0.42f * proportion[one] },
        };
        bp.stems.push_back (std::move (main_stem));

        StemSpec twig {};
        twig.part = TreePart::branch;
        twig.radial = RadialFrame::crown_radius;
        twig.min_tier = 2;
        twig.ring_phase = rng.range (0.0f * u::rad, tau);
        twig.nodes = {
          { tw_start, girth * 0.38f, 0.20f * proportion[one] },
          { offset_point (
              interpolate_point (tw_start, tw_tip, 0.55f * blend_weight[one]),
              make_displacement (0.0f * u::m, H * 0.04f, 0.0f * u::m)),
            girth * 0.22f,
            0.34f * proportion[one] },
          { tw_tip, girth * 0.10f, 0.46f * proportion[one] },
        };
        bp.stems.push_back (std::move (twig));

        const meters_t tr =
          R * rng.range (0.26f * scale_factor[one], 0.35f * scale_factor[one])
                .numerical_value_in (one);
        bp.puffs.push_back (
          { top,
            tr,
            tr *
              rng.range (0.72f * scale_factor[one], 0.95f * scale_factor[one])
                .numerical_value_in (one),
            tr *
              rng.range (0.85f * scale_factor[one], 1.12f * scale_factor[one])
                .numerical_value_in (one),
            0.22f * proportion[one],
            seed,
            0.80f * proportion[one] });
      }

      for (const StemInfo& st : stems) {
        const meters_t tr =
          R * rng.range (0.22f * scale_factor[one], 0.30f * scale_factor[one])
                .numerical_value_in (one);
        const proportion_t seed = clamp_share (
          (st.seed.numerical_value_in (one) +
           rng.range (-0.15f * scale_factor[one], 0.15f * scale_factor[one])
             .numerical_value_in (one)) *
            proportion[one],
          zero_share,
          whole_share);
        bp.puffs.push_back (
          { st.twig_tip,
            tr,
            tr *
              rng.range (0.72f * scale_factor[one], 0.95f * scale_factor[one])
                .numerical_value_in (one),
            tr *
              rng.range (0.85f * scale_factor[one], 1.12f * scale_factor[one])
                .numerical_value_in (one),
            0.22f * proportion[one],
            seed,
            0.85f * proportion[one] });
      }

      for (int round = 0; round < 2; ++round) {
        for (const StemInfo& st : stems) {
          const heading_t sub_az =
            st.kink_az + quantity_cast<heading> (
                           (round == 0 ? 0.70f : -0.70f) * u::rad +
                           rng.range (-0.30f * u::rad, 0.30f * u::rad));
          const direction_t dir = horizontal_direction (sub_az);
          const position_t base_p = round == 0 ? st.top : st.twig_tip;
          const position_t p = offset_point (
            make_point (position_value (base_p)[0] * u::m,
                        std::clamp (height_of (base_p) +
                                      rng.range (-0.10f * H, 0.12f * H),
                                    bp.crown_y0,
                                    H * 0.98f),
                        position_value (base_p)[2] * u::m),
            along (
              dir,
              R *
                rng.range (0.14f * scale_factor[one], 0.28f * scale_factor[one])
                  .numerical_value_in (one)));
          const meters_t tr =
            R * rng.range (0.18f * scale_factor[one], 0.25f * scale_factor[one])
                  .numerical_value_in (one);
          const proportion_t seed = clamp_share (
            (st.seed.numerical_value_in (one) +
             rng.range (-0.20f * scale_factor[one], 0.20f * scale_factor[one])
               .numerical_value_in (one)) *
              proportion[one],
            zero_share,
            whole_share);
          bp.puffs.push_back (
            { p,
              tr,
              tr *
                rng.range (0.70f * scale_factor[one], 0.92f * scale_factor[one])
                  .numerical_value_in (one),
              tr *
                rng.range (0.84f * scale_factor[one], 1.12f * scale_factor[one])
                  .numerical_value_in (one),
              0.22f * proportion[one],
              seed,
              0.92f * proportion[one] });
        }
      }

      bp.puff_counts = { 5, 11, bp.puffs.size () };
      bp.puff_boosts = { 1.50f * scale_factor[one],
                         1.24f * scale_factor[one],
                         unit_scale };
      populate_occluders (bp);
      return bp;
    }

    TreeBlueprint plan_snag (std::uint32_t variant) {
      Rng rng (0x5005u + variant * 4421u);
      TreeBlueprint bp {};
      bp.profile = tree_species_profile (ForestSpecies::Snag);
      const meters_t H = bp.profile.reference_height;
      const meters_t R = bp.profile.reference_crown_radius;
      const meters_t r0 =
        H * bp.profile.trunk_radius_ratio.numerical_value_in (one);
      bp.trunk_radius = r0;
      bp.crown_y0 = H * 0.30f;
      bp.crown_y1 = H;
      bp.crown_core = make_point (0.0f * u::m, H * 0.50f, 0.0f * u::m);

      const heading_t curve_dir = rng.next_heading ();
      const direction_t curve_vec = horizontal_direction (curve_dir);
      const meters_t curve_amp =
        r0 * rng.range (0.55f * scale_factor[one], 1.35f * scale_factor[one])
               .numerical_value_in (one);
      auto trunk_at = [&] (proportion_t t) -> position_t {
        const float tf = t.numerical_value_in (one);
        const meters_t sway = curve_amp * (sin (tf * 0.85f * half_turn) * tf);
        const meters_t y =
          interpolate (-0.025f * H, 0.96f * H, tf * blend_weight[one]);
        return offset_point (make_point (0.0f * u::m, y, 0.0f * u::m),
                             along (curve_vec, sway));
      };

      StemSpec trunk {};
      trunk.part = TreePart::trunk;
      trunk.radial = RadialFrame::trunk_radius;
      trunk.min_tier = 0;
      trunk.cap_top = true;
      trunk.ring_phase = rng.range (0.0f * u::rad, tau);
      trunk.buttress_lobes =
        rng.range (3.5f * scale_factor[one], 5.5f * scale_factor[one]);
      trunk.jag_seed = 0x505u + variant;
      for (int i = 0; i < 6; ++i) {
        const proportion_t t = (float (i) / 5.0f) * proportion[one];
        const float tf = t.numerical_value_in (one);
        const scale_factor_t taper =
          i == 0
            ? 1.85f * scale_factor[one]
            : interpolate (1.04f * scale_factor[one],
                           0.36f * scale_factor[one],
                           share_power (t, 0.80f).numerical_value_in (one) *
                             blend_weight[one]);
        trunk.nodes.push_back ({ trunk_at (t),
                                 r0 * taper.numerical_value_in (one),
                                 (tf * tf * 0.14f) * proportion[one],
                                 i == 0 ? 0.40f * proportion[one] : zero_share,
                                 i == 5 ? H * 0.045f : 0.0f * u::m });
      }
      bp.stems.push_back (std::move (trunk));

      constexpr int stub_count = 7;
      const heading_t base_az = rng.next_heading ();
      for (int i = 0; i < stub_count; ++i) {
        const proportion_t frac =
          (float (i) / float (stub_count - 1)) * proportion[one];
        const heading_t az =
          base_az +
          quantity_cast<heading> (float (i) * golden_angle +
                                  rng.range (-0.35f * u::rad, 0.35f * u::rad));
        const direction_t dir = horizontal_direction (az);
        const position_t attach = trunk_at (
          interpolate (0.36f * proportion[one],
                       0.88f * proportion[one],
                       frac.numerical_value_in (one) * blend_weight[one]));
        const meters_t reach =
          R * rng.range (0.36f * scale_factor[one], 0.78f * scale_factor[one])
                .numerical_value_in (one);
        const meters_t lift = rng.range (-0.03f * H, 0.09f * H);
        const position_t mid = offset_point (
          attach,
          along (dir, reach * 0.55f) +
            make_displacement (0.0f * u::m, lift * 0.45f, 0.0f * u::m));
        const position_t tip =
          offset_point (attach,
                        along (dir, reach) +
                          make_displacement (0.0f * u::m, lift, 0.0f * u::m));
        StemSpec stub {};
        stub.part = TreePart::branch;
        stub.radial = RadialFrame::crown_radius;
        stub.min_tier = i < 3 ? 0 : (i < 5 ? 1 : 2);
        stub.cap_top = true;
        stub.ring_phase = rng.range (0.0f * u::rad, tau);
        stub.jag_seed = 0x600u + variant * 17u + std::uint32_t (i);
        stub.nodes = {
          { make_point (position_value (attach)[0] * 0.25f * u::m,
                        height_of (attach),
                        position_value (attach)[2] * 0.25f * u::m),
            r0 *
              rng.range (0.30f * scale_factor[one], 0.44f * scale_factor[one])
                .numerical_value_in (one),
            0.08f * proportion[one] },
          { mid,
            r0 *
              rng.range (0.18f * scale_factor[one], 0.26f * scale_factor[one])
                .numerical_value_in (one),
            0.14f * proportion[one] },
          { tip, r0 * 0.09f, 0.20f * proportion[one], zero_share, r0 * 0.5f },
        };
        bp.stems.push_back (std::move (stub));
      }
      return bp;
    }

    TreeBlueprint plan_species (ForestSpecies species, std::uint32_t variant) {
      switch (species) {
      case ForestSpecies::Broadleaf:
        return plan_birch (variant);
      case ForestSpecies::Conifer:
        return plan_spruce (variant);
      case ForestSpecies::Pine:
        return plan_pine (variant);
      case ForestSpecies::FellBirch:
        return plan_fell_birch (variant);
      case ForestSpecies::Snag:
        return plan_snag (variant);
      }
      throw std::invalid_argument ("render: unknown ForestSpecies");
    }

    // -- Storey 2: Multi-LOD Mesh Emitter -----------------------------------
    //
    // Converts one TreeBlueprint into a single LOD tier with verified outward
    // CCW winding, and packs typed values into TreeArchetypeVertex once at
    // the storage boundary.

    struct MeshEmitter {
      TreeArchetypeLibrary& lib;
      const TreeBlueprint& bp;
      std::vector<position_t> positions;

      std::uint16_t emit (const position_t& p,
                          const direction_t& normal,
                          proportion_t ao,
                          float u,
                          proportion_t v,
                          proportion_t flutter,
                          TreePart part,
                          RadialFrame radial) {
        const std::size_t idx = lib.vertices.size ();
        if (idx >= 65535u)
          throw std::runtime_error ("render: tree archetype vertices > uint16");
        const meters_t r_div = radial == RadialFrame::trunk_radius
                                 ? bp.trunk_radius
                                 : bp.profile.reference_crown_radius;
        const Vec3 pm = position_value (p);
        const Vec3 nv = direction_value (normal);
        const float r_m = meters_value (r_div);
        const float h_m = meters_value (bp.profile.reference_height);
        TreeArchetypeVertex vert {};
        vert.pos_scale = { pm[0] / r_m,
                           pm[1] / h_m,
                           pm[2] / r_m,
                           radial == RadialFrame::trunk_radius ? 0.0f : 1.0f };
        vert.norm_ao = { nv[0], nv[1], nv[2], ao.numerical_value_in (one) };
        vert.uv_meta = { u,
                         v.numerical_value_in (one),
                         flutter.numerical_value_in (one),
                         float (part) };
        lib.vertices.push_back (vert);
        positions.push_back (p);
        return std::uint16_t (idx);
      }

      void tri (std::uint16_t a,
                std::uint16_t b,
                std::uint16_t c,
                const displacement_t& outward) {
        const Vec3 pa = position_value (positions[a]);
        const Vec3 pb = position_value (positions[b]);
        const Vec3 pc = position_value (positions[c]);
        const Vec3 g = cross (pb - pa, pc - pa);
        if (dot (g, g) < 1e-12f)
          return;
        if (dot (g, displacement_value (outward)) < 0.0f)
          std::swap (b, c);
        lib.indices.push_back (a);
        lib.indices.push_back (b);
        lib.indices.push_back (c);
      }

      displacement_t centroid_offset (std::uint16_t a,
                                      std::uint16_t b,
                                      std::uint16_t c,
                                      const position_t& ref) const {
        const Vec3 avg =
          (position_value (positions[a]) + position_value (positions[b]) +
           position_value (positions[c])) *
          (1.0f / 3.0f);
        return displacement (avg - position_value (ref));
      }

      void emit_stem (const StemSpec& stem, int detail) {
        if (stem.nodes.size () < 2)
          return;
        // Decimate rings cleanly for coarser LODs while keeping root & tip.
        std::vector<StemNode> nodes;
        if (detail == 2 || stem.nodes.size () <= 3) {
          nodes = stem.nodes;
        } else if (detail == 1) {
          nodes.push_back (stem.nodes.front ());
          for (std::size_t i = 1; i + 1 < stem.nodes.size (); i += 2)
            nodes.push_back (stem.nodes[i]);
          nodes.push_back (stem.nodes.back ());
        } else {
          nodes.push_back (stem.nodes.front ());
          nodes.push_back (stem.nodes[stem.nodes.size () / 2]);
          nodes.push_back (stem.nodes.back ());
        }

        const int sides = stem.part == TreePart::trunk
                            ? (detail == 2 ? 7 : (detail == 1 ? 5 : 3))
                            : (detail == 2 ? 4 : 3);
        const int n = int (nodes.size ());
        std::vector<std::uint16_t> rings;
        rings.reserve (std::size_t (n) * sides);

        direction_t tangent =
          direction_of (displacement_between (nodes[0].pos, nodes[1].pos));
        const Vec3 ref_axis = std::abs (direction_value (tangent)[1]) < 0.92f
                                ? Vec3 (0.0f, 1.0f, 0.0f)
                                : Vec3 (1.0f, 0.0f, 0.0f);
        direction_t u_axis =
          safe_direction (cross (direction_value (tangent), ref_axis));
        direction_t v_axis = safe_direction (
          cross (direction_value (tangent), direction_value (u_axis)));

        Rng jag_rng (stem.jag_seed);
        std::vector<scale_factor_t> jag_jitter (sides, unit_scale);
        for (int s = 0; s < sides; ++s)
          jag_jitter[s] =
            jag_rng.range (-1.0f * scale_factor[one], 1.0f * scale_factor[one]);

        const meters_t r_norm = stem.radial == RadialFrame::trunk_radius
                                  ? bp.trunk_radius
                                  : bp.profile.reference_crown_radius;
        for (int i = 0; i < n; ++i) {
          if (i > 0) {
            const displacement_t seg =
              i + 1 < n
                ? displacement_between (nodes[i - 1].pos, nodes[i + 1].pos)
                : displacement_between (nodes[i - 1].pos, nodes[i].pos);
            tangent = direction_of (seg);
            const Vec3 t_vec = direction_value (tangent);
            u_axis =
              safe_direction (direction_value (u_axis) -
                              t_vec * dot (direction_value (u_axis), t_vec));
            v_axis = safe_direction (cross (t_vec, direction_value (u_axis)));
          }
          const StemNode& nd = nodes[i];
          for (int s = 0; s < sides; ++s) {
            const radians_t ang =
              stem.ring_phase + tau * (float (s) / float (sides));
            const direction_t radial =
              safe_direction (direction_value (u_axis) * cos (ang) +
                              direction_value (v_axis) * sin (ang));
            meters_t r = nd.radius;
            if (nd.flare > zero_share) {
              const radians_t lobe_ang =
                ang * stem.buttress_lobes.numerical_value_in (one) +
                0.7f * u::rad;
              const float flute = 0.5f + 0.5f * cos (lobe_ang);
              r = r * (1.0f + nd.flare.numerical_value_in (one) * flute);
            }
            position_t p = offset_point (nd.pos, along (radial, r));
            if (nd.jag > 0.0f * u::m)
              p = offset_point (
                p,
                along (tangent,
                       nd.jag * jag_jitter[s].numerical_value_in (one)));
            const float bark_u =
              radians_value (ang) * (meters_value (nd.radius) /
                                     std::max (meters_value (r_norm), 1e-4f));
            const proportion_t bark_v =
              share_of (height_of (p), bp.profile.reference_height);
            const proportion_t ao =
              volumetric_ao (p, radial, bp, std::size_t (-1), false);
            rings.push_back (emit (p,
                                   radial,
                                   ao,
                                   bark_u,
                                   bark_v,
                                   nd.flutter,
                                   stem.part,
                                   stem.radial));
          }
        }

        for (int i = 0; i + 1 < n; ++i) {
          const position_t mid = interpolate_point (
            nodes[i].pos, nodes[i + 1].pos, 0.5f * blend_weight[one]);
          for (int s = 0; s < sides; ++s) {
            const int sn = (s + 1) % sides;
            const std::uint16_t a = rings[i * sides + s];
            const std::uint16_t b = rings[i * sides + sn];
            const std::uint16_t c = rings[(i + 1) * sides + s];
            const std::uint16_t d = rings[(i + 1) * sides + sn];
            tri (a, c, b, centroid_offset (a, c, b, mid));
            tri (b, c, d, centroid_offset (b, c, d, mid));
          }
        }

        if (stem.cap_top) {
          const StemNode& last = nodes.back ();
          const position_t tip_pos =
            offset_point (last.pos, along (tangent, last.radius * 0.8f));
          const proportion_t tip_v =
            share_of (height_of (tip_pos), bp.profile.reference_height);
          const proportion_t tip_ao =
            volumetric_ao (tip_pos, tangent, bp, std::size_t (-1), false);
          const std::uint16_t tip = emit (tip_pos,
                                          tangent,
                                          tip_ao,
                                          0.0f,
                                          tip_v,
                                          last.flutter,
                                          stem.part,
                                          stem.radial);
          const int base = (n - 1) * sides;
          const displacement_t cap_out = along (tangent, 1.0f * u::m);
          for (int s = 0; s < sides; ++s) {
            const int sn = (s + 1) % sides;
            tri (rings[base + s], tip, rings[base + sn], cap_out);
          }
        }
      }

      void emit_puff (const FoliagePuffSpec& pf,
                      std::size_t puff_index,
                      scale_factor_t area_boost,
                      int detail) {
        const float boost = area_boost.numerical_value_in (one);
        const meters_t rx = pf.rx * boost;
        const meters_t ry = pf.ry * (1.0f + (boost - 1.0f) * 0.82f);
        const meters_t rz = pf.rz * boost;

        auto frac = [] (float x) { return x - std::floor (x); };
        const float cs = pf.clump_seed.numerical_value_in (one);
        const radians_t p1 = frac (cs * 3.17f + 0.13f) * tau;
        const radians_t p2 = frac (cs * 7.31f + 0.41f) * tau;
        const radians_t yaw = frac (cs * 13.73f + 0.29f) * tau;
        const radians_t pitch = ((frac (cs * 5.91f) - 0.5f) * 0.45f) * u::rad;
        const float cy = cos (yaw), sy = sin (yaw);
        const float cp = cos (pitch), sp = sin (pitch);

        auto emit_unit = [&] (const Vec3& u) {
          const Vec3 r1 (u[0], u[1] * cp - u[2] * sp, u[1] * sp + u[2] * cp);
          const Vec3 d (
            r1[0] * cy - r1[2] * sy, r1[1], r1[0] * sy + r1[2] * cy);
          const radians_t ang = cr::atan2 (d[2], d[0]) * u::rad;
          const float bump =
            1.0f + 0.17f * sin (3.0f * ang + p1) +
            0.12f * cos (2.0f * d[1] * half_turn + p2) +
            0.08f * sin (5.0f * ang + (d[1] * 3.0f) * u::rad + p1);
          const meters_t y_scale =
            d[1] < 0.0f
              ? ry * (1.0f + pf.droop.numerical_value_in (one) * 0.35f)
              : ry;
          const displacement_t off = make_displacement (
            rx * (d[0] * bump), y_scale * (d[1] * bump), rz * (d[2] * bump));
          const position_t p = offset_point (pf.centre, off);
          const Vec3 ov = displacement_value (off);
          const direction_t local_n =
            safe_direction (Vec3 (ov[0] / std::max (meters_value (rx), 0.05f),
                                  ov[1] / std::max (meters_value (ry), 0.05f),
                                  ov[2] / std::max (meters_value (rz), 0.05f)));
          const direction_t env_n =
            direction_of (displacement_between (bp.crown_core, p));
          const direction_t n =
            blend_directions (local_n, env_n, 0.56f * blend_weight[one]);
          const proportion_t crown_h =
            share_of (height_of (p) - bp.crown_y0, bp.crown_y1 - bp.crown_y0);
          const proportion_t ao = volumetric_ao (p, n, bp, puff_index, true);
          return emit (p,
                       n,
                       ao,
                       cs,
                       crown_h,
                       pf.flutter,
                       TreePart::foliage,
                       RadialFrame::crown_radius);
        };

        if (detail == 0) {
          static const std::array<Vec3, 6> oct_v {
            Vec3 (0, 1, 0), Vec3 (0, -1, 0), Vec3 (1, 0, 0),
            Vec3 (0, 0, 1), Vec3 (-1, 0, 0), Vec3 (0, 0, -1),
          };
          static constexpr int oct_f[8][3] {
            { 0, 3, 2 }, { 0, 4, 3 }, { 0, 5, 4 }, { 0, 2, 5 },
            { 1, 2, 3 }, { 1, 3, 4 }, { 1, 4, 5 }, { 1, 5, 2 },
          };
          std::array<std::uint16_t, 6> v {};
          for (std::size_t i = 0; i < 6; ++i)
            v[i] = emit_unit (oct_v[i]);
          for (const auto& f : oct_f)
            tri (v[f[0]],
                 v[f[1]],
                 v[f[2]],
                 centroid_offset (v[f[0]], v[f[1]], v[f[2]], pf.centre));
          return;
        }

        // Regular icosahedron (12 vertices, 20 balanced triangles).
        constexpr float phi = 1.61803398875f;
        const float a = 1.0f / std::sqrt (1.0f + phi * phi);
        const float b = phi * a;
        const std::array<Vec3, 12> ico_v {
          Vec3 (-a, b, 0), Vec3 (a, b, 0), Vec3 (-a, -b, 0), Vec3 (a, -b, 0),
          Vec3 (0, -a, b), Vec3 (0, a, b), Vec3 (0, -a, -b), Vec3 (0, a, -b),
          Vec3 (b, 0, -a), Vec3 (b, 0, a), Vec3 (-b, 0, -a), Vec3 (-b, 0, a),
        };
        static constexpr int ico_f[20][3] {
          { 0, 11, 5 },  { 0, 5, 1 },  { 0, 1, 7 },  { 0, 7, 10 },
          { 0, 10, 11 }, { 1, 5, 9 },  { 5, 11, 4 }, { 11, 10, 2 },
          { 10, 7, 6 },  { 7, 1, 8 },  { 3, 9, 4 },  { 3, 4, 2 },
          { 3, 2, 6 },   { 3, 6, 8 },  { 3, 8, 9 },  { 4, 9, 5 },
          { 2, 4, 11 },  { 6, 2, 10 }, { 8, 6, 7 },  { 9, 8, 1 },
        };
        std::array<std::uint16_t, 12> v {};
        for (std::size_t i = 0; i < 12; ++i)
          v[i] = emit_unit (ico_v[i]);
        for (const auto& f : ico_f)
          tri (v[f[0]],
               v[f[1]],
               v[f[2]],
               centroid_offset (v[f[0]], v[f[1]], v[f[2]], pf.centre));
      }

      void emit_spruce_frond (const SpruceFrondSpec& fr,
                              std::size_t owner_tag,
                              scale_factor_t width_boost,
                              scale_factor_t thick_boost,
                              int stations) {
        const direction_t dir = horizontal_direction (fr.azimuth);
        const Vec3 dv = direction_value (dir);
        const direction_t side = unit_direction (Vec3 (-dv[2], 0.0f, dv[0]));
        const direction_t up = unit_direction (Vec3 (0.0f, 1.0f, 0.0f));
        const meters_t width = fr.width * width_boost.numerical_value_in (one);
        const meters_t thickness =
          fr.thickness * thick_boost.numerical_value_in (one);

        auto spine = [&] (proportion_t t) -> position_t {
          const meters_t dip =
            -fr.droop *
            sin (share_power (t, 0.82f).numerical_value_in (one) * half_turn);
          const meters_t lift =
            fr.upturn * share_power (t, 2.3f).numerical_value_in (one);
          return offset_point (
            fr.attach,
            along (dir, fr.length * t.numerical_value_in (one)) +
              along (up, dip + lift));
        };

        auto emit_v =
          [&] (const position_t& p, const direction_t& n, proportion_t t) {
            const proportion_t crown_h =
              share_of (height_of (p) - bp.crown_y0, bp.crown_y1 - bp.crown_y0);
            const proportion_t ao = volumetric_ao (p, n, bp, owner_tag, true);
            const proportion_t flut =
              (fr.flutter.numerical_value_in (one) *
               (0.25f + 0.75f * t.numerical_value_in (one))) *
              proportion[one];
            return emit (p,
                         n,
                         ao,
                         fr.clump_seed.numerical_value_in (one),
                         crown_h,
                         flut,
                         TreePart::foliage,
                         RadialFrame::crown_radius);
          };

        const std::uint16_t root_v =
          emit_v (spine (zero_share), unit_direction (dv * -1.0f), zero_share);

        std::vector<std::array<std::uint16_t, 4>> rings;
        rings.reserve (stations);
        for (int s = 1; s <= stations; ++s) {
          const proportion_t t =
            (float (s) / float (stations + 1)) * proportion[one];
          const position_t c = spine (t);
          const proportion_t env =
            sin (share_power (t, 0.68f).numerical_value_in (one) * half_turn) *
            proportion[one];
          const scale_factor_t scallop =
            (1.0f +
             0.18f * sin (float (s) * 2.4f * u::rad +
                          fr.clump_seed.numerical_value_in (one) * tau)) *
            scale_factor[one];
          const meters_t w =
            width * ((0.35f + 0.78f * env.numerical_value_in (one)) *
                     scallop.numerical_value_in (one));
          const meters_t th =
            thickness * (0.45f + 0.65f * env.numerical_value_in (one));
          const Vec3 sv = direction_value (side);
          const Vec3 uv = direction_value (up);

          rings.push_back (
            { emit_v (offset_point (c, along (up, th * 0.85f)),
                      safe_direction (uv * 0.85f + dv * 0.45f),
                      t),
              emit_v (
                offset_point (c, along (side, w) + along (up, -th * 0.18f)),
                safe_direction (sv * 0.72f + uv * 0.48f + dv * 0.35f),
                t),
              emit_v (offset_point (c, along (up, -th * 0.65f)),
                      safe_direction (uv * -0.82f + dv * 0.35f),
                      t),
              emit_v (
                offset_point (c, along (side, -w) + along (up, -th * 0.18f)),
                safe_direction (sv * -0.72f + uv * 0.48f + dv * 0.35f),
                t) });
        }

        const std::uint16_t tip_v =
          emit_v (spine (whole_share),
                  safe_direction (dv * 0.75f + Vec3 (0.0f, 0.66f, 0.0f)),
                  whole_share);

        const position_t c0 =
          spine ((0.5f / float (stations + 1)) * proportion[one]);
        for (int k = 0; k < 4; ++k) {
          const int kn = (k + 1) % 4;
          tri (root_v,
               rings[0][kn],
               rings[0][k],
               centroid_offset (root_v, rings[0][kn], rings[0][k], c0));
        }

        for (int s = 0; s + 1 < stations; ++s) {
          const position_t cm = spine (
            ((float (s) + 1.5f) / float (stations + 1)) * proportion[one]);
          for (int k = 0; k < 4; ++k) {
            const int kn = (k + 1) % 4;
            const std::uint16_t a = rings[s][k], b = rings[s][kn];
            const std::uint16_t c = rings[s + 1][k], d = rings[s + 1][kn];
            tri (a, b, c, centroid_offset (a, b, c, cm));
            tri (b, d, c, centroid_offset (b, d, c, cm));
          }
        }

        const auto& last = rings.back ();
        const position_t ce = spine (
          ((float (stations) + 0.5f) / float (stations + 1)) * proportion[one]);
        for (int k = 0; k < 4; ++k) {
          const int kn = (k + 1) % 4;
          tri (last[k],
               last[kn],
               tip_v,
               centroid_offset (last[k], last[kn], tip_v, ce));
        }
      }

      void emit_tier (int detail) {
        for (const StemSpec& stem : bp.stems)
          if (detail >= int (stem.min_tier))
            emit_stem (stem, detail);

        const std::size_t keep_puffs =
          std::min (bp.puff_counts[detail], bp.puffs.size ());
        for (std::size_t i = 0; i < keep_puffs; ++i)
          emit_puff (bp.puffs[i], i, bp.puff_boosts[detail], detail);

        const std::size_t keep_fronds =
          std::min (bp.frond_counts[detail], bp.fronds.size ());
        const int stations = detail == 2 ? 3 : (detail == 1 ? 2 : 1);
        for (std::size_t i = 0; i < keep_fronds; ++i)
          emit_spruce_frond (bp.fronds[i],
                             bp.puffs.size () + i,
                             bp.frond_width_boosts[detail],
                             bp.frond_thick_boosts[detail],
                             stations);
      }
    };
  }

  TreeSpeciesProfile tree_species_profile (ForestSpecies species) {
    switch (species) {
    case ForestSpecies::Broadleaf:
      return { 18.0f * u::m,
               3.6f * u::m,
               0.0085f * proportion[one],
               0.44f * proportion[one] };
    case ForestSpecies::Conifer:
      return { 24.0f * u::m,
               3.1f * u::m,
               0.0078f * proportion[one],
               0.11f * proportion[one] };
    case ForestSpecies::Pine:
      return { 20.0f * u::m,
               4.2f * u::m,
               0.0085f * proportion[one],
               0.56f * proportion[one] };
    case ForestSpecies::FellBirch:
      return { 4.8f * u::m,
               2.5f * u::m,
               0.014f * proportion[one],
               0.16f * proportion[one] };
    case ForestSpecies::Snag:
      return { 16.0f * u::m,
               2.2f * u::m,
               0.0078f * proportion[one],
               0.30f * proportion[one] };
    }
    throw std::invalid_argument ("render: unknown ForestSpecies");
  }

  TreeArchetypeLibrary build_tree_archetypes (std::uint32_t variants,
                                              std::uint32_t tiers) {
    TreeArchetypeLibrary lib;
    lib.variants = variants;
    lib.tiers = tiers;
    lib.vertices.reserve (12000);
    lib.indices.reserve (64000);
    lib.slices.resize (std::size_t (forest_species_count) * variants * tiers);

    for (const ForestSpecies species : all_species) {
      for (std::uint32_t v = 0; v < variants; ++v) {
        const TreeBlueprint blueprint = plan_species (species, v);
        for (std::uint32_t t = 0; t < tiers; ++t) {
          const std::size_t cls =
            (std::size_t (species) * variants + v) * tiers + t;
          const std::uint32_t first = std::uint32_t (lib.indices.size ());
          MeshEmitter emitter { lib, blueprint, {} };
          emitter.positions.resize (lib.vertices.size (), origin_point ());
          const int detail = tiers <= 1 ? 2 : int (t * 2u / (tiers - 1u));
          emitter.emit_tier (detail);
          lib.slices[cls] = { first,
                              std::uint32_t (lib.indices.size ()) - first };
        }
      }
    }
    return lib;
  }
}
