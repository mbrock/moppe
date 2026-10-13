#ifndef MOPPE_GAME_RIVER_STONES_HH
#define MOPPE_GAME_RIVER_STONES_HH

#include <moppe/game/boulders.hh>
#include <moppe/terrain/drainage.hh>

#include <cstdint>
#include <vector>

namespace moppe::game {
  // The coarsest grain a river's flood rolls along its bed: Shields'
  // criterion, which sets the bed's shear stress rho g d S against a grain's
  // submerged weight theta_c (rho_s - rho) g D, for rock in water
  // (rho_s / rho - 1 = 1.65). The critical Shields number rises on steep
  // beds, where the grains stand proud of a shallow flow: 0.15 S^0.25
  // (Lamb, Dietrich and Venditti 2008), and no less than 0.03. So the grain
  // a river can move scales as depth times slope: a mountain brook a foot
  // deep falling one in ten moves stones a hand across and leaves the
  // boulders it cannot, while a lowland river two metres deep falling one in
  // a thousand moves nothing coarser than fine gravel.
  [[nodiscard]] meters_t bed_competence (meters_t depth, float slope) noexcept;

  // How fine the bed is that a flood of that competence leaves, from 0 for
  // cobbles and coarser (64 mm) to 1 for sand (2 mm and finer), on the
  // logarithmic scale grain sizes are graded on.
  [[nodiscard]] float bed_fineness (meters_t competence) noexcept;

  // The same reading from the painted water sheet at one lattice cell: its
  // depth over the ground, raised to the spate's that sorted the bed, and
  // the grade its current descends. Zero for dry ground and for still
  // water, which moves nothing.
  [[nodiscard]] meters_t sheet_competence (const map::SurfaceGeometry& surface,
                                           const terrain::WaterSheets& water,
                                           std::size_t cell) noexcept;

  // The ground running water has sorted, at every lattice cell: under the
  // water, and on the strands beside it -- low, level ground within a
  // dozen metres that its spate covers, the bars it lays.
  struct RiverBedReading {
    // How fine it lies (bed_fineness): under the water by the competence
    // there, on a strand by that of the water nearest. Negative where no
    // river laid the ground.
    std::vector<float> fineness;
    // How far up a strand a cell stands, from 0 at the water's edge to 1
    // at the spate's; negative off the strands. The low strand is bare
    // stone and sand, and plants take hold the higher it lies.
    std::vector<float> strand;
  };

  [[nodiscard]] RiverBedReading
  read_river_bed (const map::SurfaceGeometry& surface,
                  const terrain::WaterSheets& water);

  // What the rivers lay: their stones and the rows that cross them.
  struct RiverStones {
    std::vector<BoulderSite> sites;
    std::vector<StoneCrossing> crossings;
  };

  // Loose stone in and along the running water, placed by what each reach's
  // flood can move. Every river is walked down its alignment, reading its
  // section, depth, current, and grade from the painted sheet:
  //
  //   - steep, competent headwaters keep the boulders they cannot shift,
  //     strewn over the bed and jammed in rows across it, the steps of a
  //     step-pool stream, spaced so each step drops about its own height;
  //   - riffles of cobbles stand out of shallow, competent water;
  //   - cobble and gravel bars lie on the inside of bends, where the
  //     current slackens, and below confluences on the side the tributary
  //     brings its coarser load in;
  //   - slow reaches, competent for sand, carry none (the ground there is
  //     sand, see surface_presentation.cc);
  //   - and now and then a great rock that no flood here moved sits in the
  //     channel or on its bank.
  //
  // Where a stream is shallow and narrow enough, a row of stepping stones
  // crosses it, a stride apart with their tops dry, so the walker can
  // cross without wading; they are solid (BoulderSite::solid), and so are
  // the stones that break the water and are broad enough to stand on.
  [[nodiscard]] RiverStones
  plan_river_stones (const map::SurfaceGeometry& surface,
                     const map::SurfaceReadings& readings,
                     const terrain::WaterSheets& water,
                     const terrain::RiverNetwork& rivers,
                     std::uint32_t seed);
}

#endif
