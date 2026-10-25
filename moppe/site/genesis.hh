// The website's genesis view (docs/site.md): the land on a small periodic
// lattice as it grows, drawn over NHAL in looks a view weighs against each
// other -- the map sheet, a clay relief model, the lattice as a wire
// drawing, and the land itself under a sky -- seen from a camera swung
// about a point on it. The lattice's tiles surround it, so the land runs
// on to the horizon. Shaders: moppe/site/shaders/genesis.lisp.
#ifndef MOPPE_SITE_GENESIS_HH
#define MOPPE_SITE_GENESIS_HH

#include <moppe/nhal/nhal.hh>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace moppe::site {
  struct GenesisShaders {
    nhal::StageCode terrain_vertex, terrain_fragment;
    nhal::StageCode sky_vertex, sky_fragment;
  };

  struct GenesisView {
    // The point the camera swings about, in metres over the lattice
    // (x east, z south, y up), and the swing: heading clockwise from
    // north and pitch below the horizontal, in radians.
    std::array<float, 3> target { 2500.0f, 0.0f, 2500.0f };
    float heading = 0.0f;
    float pitch = 1.55f;
    float distance = 4000.0f;
    float fov_y = 0.7f;
    // How much of each look: they are added, so they should sum to one;
    // the wire is drawn over them.
    float map = 1.0f, relief = 0.0f, wire = 0.0f, land = 0.0f;
    // Heights are drawn this many times as high.
    float exaggeration = 1.0f;
    // Seconds of drifting clouds and cloud shadows.
    double seconds = 0.0;
  };

  // Where the website's scroll puts the view: PROGRESS from 0, straight
  // down at the map, to 1, low over the land; ASPECT is the drawable's
  // width over its height, so that the map fills a phone's narrow screen.
  GenesisView
  genesis_choreography (float progress, double seconds, float aspect);

  class Genesis {
  public:
    // A lattice `lattice` samples a side, `period_m` metres across, with
    // its sea at `sea_level_m`.
    Genesis (nhal::Device& device,
             const GenesisShaders& shaders,
             int lattice,
             float period_m,
             float sea_level_m);
    ~Genesis ();

    // The heights before and after the step shown, `lattice` squared of
    // each, rows from the north; `render` mixes between them.
    void set_heights (std::span<const float> from, std::span<const float> to);

    // Begins a frame and records the view, `mix` of the way from the
    // first heights to the second. The host ends the frame (and may
    // capture it first). False when the device had no drawable.
    bool render (const GenesisView& view, float mix);

  private:
    void make_targets ();

    nhal::Device& m_device;
    int m_lattice;
    float m_period;
    float m_sea_level;
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
