// The website's genesis view (docs/site.md): the land on a small periodic
// lattice as it grows, drawn over NHAL in looks a view weighs against each
// other -- the map sheet, a clay relief model, the lattice as a wire
// drawing, and the land itself under a sky -- seen from a camera swung
// about a point on it. The lattice's tiles surround it, so the land runs
// on to the horizon. Shaders: moppe/site/shaders/genesis.lisp.
#ifndef MOPPE_SITE_GENESIS_HH
#define MOPPE_SITE_GENESIS_HH

#include <moppe/gfx/math.hh>
#include <moppe/nhal/nhal.hh>
#include <moppe/terrain/domain.hh>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace moppe::site {
  // A drawable's width over its height.
  using aspect_ratio_t = quantity<aspect_ratio[one], float>;
  using seconds_f64_t = quantity<si::second, double>;

  struct GenesisShaders {
    nhal::StageCode terrain_vertex, terrain_fragment;
    nhal::StageCode sky_vertex, sky_fragment;
  };

  struct GenesisView {
    // The point the camera swings about, in metres over the lattice
    // (x east, z south, y up), and the swing: heading clockwise from
    // north and pitch below the horizontal.
    position_t target = position (Vec3 (2500.0f, 0.0f, 2500.0f));
    heading_t heading = 0.0f * moppe::heading[si::radian];
    inclination_t pitch = 1.55f * inclination[si::radian];
    meters_t distance = 4000.0f * u::m;
    radians_t fov_y = 0.7f * u::rad;
    // How much of each look: they are added, so they should sum to one;
    // the wire is drawn over them.
    blend_weight_t map = 1.0f * blend_weight[one],
                   relief = 0.0f * blend_weight[one],
                   wire = 0.0f * blend_weight[one],
                   land = 0.0f * blend_weight[one];
    // Heights are drawn this many times as high.
    scale_factor_t exaggeration = 1.0f * scale_factor[one];
    // Drifting clouds and cloud shadows.
    seconds_f64_t seconds = 0.0 * u::s;
  };

  // Where the website's scroll puts the view: PROGRESS from 0, straight
  // down at the map, to 1, low over the land; ASPECT is the drawable's
  // width over its height, so that the map fills a phone's narrow screen.
  GenesisView genesis_choreography (proportion_t progress,
                                    seconds_f64_t seconds,
                                    aspect_ratio_t aspect);

  class Genesis {
  public:
    // A lattice `lattice` samples a side, `period` across, with its sea
    // at `sea_level`.
    Genesis (nhal::Device& device,
             const GenesisShaders& shaders,
             int lattice,
             meters_t period,
             terrain::SurfaceElevation sea_level);
    ~Genesis ();

    // The heights before and after the step shown, `lattice` squared of
    // each, rows from the north, in metres (the raster the GPU reads);
    // `render` mixes between them.
    void set_heights (std::span<const float> from, std::span<const float> to);

    // Begins a frame and records the view, `mix` of the way from the
    // first heights to the second. The host ends the frame (and may
    // capture it first). False when the device had no drawable.
    bool render (const GenesisView& view, blend_weight_t mix);

  private:
    void make_targets ();

    nhal::Device& m_device;
    int m_lattice;
    meters_t m_period;
    terrain::SurfaceElevation m_sea_level;
    nhal::Pipeline m_terrain, m_sky;
    nhal::Buffer m_samples, m_indices;
    std::uint32_t m_index_count = 0;
    // The samples set_heights gave, to be copied in at the next frame.
    std::vector<std::array<float, 4>> m_staged;
    bool m_dirty = false;
    nhal::Texture m_color, m_depth;
    std::uint32_t m_width = 0, m_height = 0;
  };
}

#endif
