#include <moppe/game/foliage.hh>

#include <tests/test.hh>

#include <cstdint>

namespace {
  float luminance (const moppe::Vec3& colour) {
    return 0.299f * colour[0] + 0.587f * colour[1] + 0.114f * colour[2];
  }

  float separation (const moppe::Vec3& left, const moppe::Vec3& right) {
    return std::abs (left[0] - right[0]) + std::abs (left[1] - right[1]) +
           std::abs (left[2] - right[2]);
  }
}

// The ramp is the mechanism: a crown lights as a mass because its vertices
// interpolate between an outer skin and an interior, so those two ends have
// to be far enough apart to read as shading rather than as noise.
MOPPE_TEST (a_crown_palette_runs_from_a_lit_skin_to_a_shaded_interior) {
  using namespace moppe::game;
  for (std::uint32_t seed = 0; seed < 64; ++seed)
    for (FoliageKind kind : { FoliageKind::broadleaf, FoliageKind::conifer }) {
      const FoliagePalette palette =
        crown_palette (kind, { .moisture = 0.5f, .cover = 0.5f }, seed);
      MOPPE_CHECK (luminance (palette.sunlit) >
                   1.6f * luminance (palette.shaded));
      MOPPE_CHECK (palette.shaded[1] > 0.0f);
      // Foliage stays green at both ends: the interior is in shade, not on
      // fire and not underwater.
      MOPPE_CHECK (palette.sunlit[1] > palette.sunlit[0]);
      MOPPE_CHECK (palette.sunlit[1] > palette.sunlit[2]);
      MOPPE_CHECK (palette.shaded[1] > palette.shaded[2]);
    }
}

// A hillside of one green is the thing this is meant to prevent, so two
// neighbours must actually differ, and a wet site must read richer than a dry
// one rather than merely differently.
MOPPE_TEST (foliage_colour_varies_by_individual_and_by_ground) {
  using namespace moppe::game;
  const FoliageGround ordinary { .moisture = 0.5f, .cover = 0.5f };
  float widest = 0.0f;
  for (std::uint32_t seed = 0; seed < 200; ++seed)
    widest = std::max (
      widest,
      separation (crown_palette (FoliageKind::broadleaf, ordinary, seed).sunlit,
                  crown_palette (FoliageKind::broadleaf, ordinary, 0).sunlit));
  MOPPE_CHECK (widest > 0.05f);

  const FoliagePalette dry = crown_palette (
    FoliageKind::broadleaf, { .moisture = 0.0f, .cover = 0.2f }, 7);
  const FoliagePalette wet = crown_palette (
    FoliageKind::broadleaf, { .moisture = 1.0f, .cover = 0.9f }, 7);
  MOPPE_CHECK (wet.sunlit[1] > dry.sunlit[1]);

  // Species are told apart at a glance from the air: conifers hold a darker,
  // bluer crown than broadleaves on the same ground.
  const FoliagePalette conifer =
    crown_palette (FoliageKind::conifer, ordinary, 7);
  const FoliagePalette broadleaf =
    crown_palette (FoliageKind::broadleaf, ordinary, 7);
  MOPPE_CHECK (luminance (conifer.sunlit) < luminance (broadleaf.sunlit));
}

// Bark shares the ramp mechanism but not the colour: a stem read as foliage
// is what makes a stand look like green lollipops on sticks.
MOPPE_TEST (bark_is_wood_coloured_at_both_ends_of_its_ramp) {
  using namespace moppe::game;
  for (std::uint32_t seed = 0; seed < 64; ++seed) {
    const FoliagePalette bark = bark_palette (FoliageKind::broadleaf, seed);
    MOPPE_CHECK (bark.sunlit[0] > bark.sunlit[1]);
    MOPPE_CHECK (bark.sunlit[1] > bark.sunlit[2]);
    MOPPE_CHECK (luminance (bark.sunlit) > luminance (bark.shaded));
    // Shaded bark is still lit by the ground and by what gets through the
    // crown; an unlit silhouette is what makes a stand look like fence posts.
    MOPPE_CHECK (luminance (bark.shaded) > 0.25f * luminance (bark.sunlit));
  }
}
