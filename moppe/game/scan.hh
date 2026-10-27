#ifndef MOPPE_GAME_SCAN_HH
#define MOPPE_GAME_SCAN_HH

// The scan, after Death Stranding's: on foot, `Q` sends a ring sweeping
// out over the ground from where the walker stands, and whatever it
// passes lights up for a while with a label standing over it, seen
// faintly through whatever is in the way -- mushrooms worth picking,
// fly agaric that is not, and pairs of trees that would take the
// hammock. The labels are Slug text standing in the world
// (render::TextList::stand, Renderer::draw_world_labels).

#include <moppe/game/camp.hh>
#include <moppe/game/mushrooms.hh>
#include <moppe/gfx/math.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/text.hh>

#include <span>
#include <string>
#include <vector>

namespace moppe::game {
  // How far the ring goes, how fast, and how long what it finds stays lit
  // before fading.
  inline constexpr float scan_reach = 60.0f;
  inline constexpr float scan_speed = 24.0f;
  inline constexpr float scan_lit = 12.0f;
  inline constexpr float scan_fade = 1.5f;

  struct ScanFind {
    enum class Kind : std::uint8_t { mushrooms, poisonous, hammock };
    Kind kind = Kind::mushrooms;
    // Where the thing is: a group's middle on the ground, or the middle
    // of a hammock's span at strap height.
    Vec3 at {};
    // How many mushrooms of one kind the group holds.
    int count = 1;
    MushroomKind mushroom = MushroomKind::chanterelle;
    HammockSite hammock {};
  };

  // The unpicked mushrooms within reach of `origin`, a find for each group
  // of one kind standing together (within a few metres of the group's
  // first), the poisonous apart.
  [[nodiscard]] std::vector<ScanFind>
  scan_mushrooms (const MushroomPatch& patch, const Vec3& origin);

  [[nodiscard]] std::vector<ScanFind>
  scan_hammocks (std::span<const HammockSite> sites);

  class Scan {
  public:
    void start (const Vec3& origin, double now, std::vector<ScanFind> finds);

    // Whether anything of it still shows.
    bool showing (double now) const;
    // How far the ring has gone.
    float radius (double now) const;
    // How much a find shows, 0..1: none until the ring reaches it.
    float shown (const ScanFind& find, double now) const;
    // Seconds since the ring reached it, negative before.
    float since_found (const ScanFind& find, double now) const;

    const Vec3& origin () const {
      return m_origin;
    }
    const std::vector<ScanFind>& finds () const {
      return m_finds;
    }

  private:
    Vec3 m_origin {};
    double m_start = -1e9;
    std::vector<ScanFind> m_finds;
  };

  // The ring on the ground and the lines from each find up to its label,
  // as additive glow in world space.
  void draw_scan_glow (render::DrawList& dl,
                       const Scan& scan,
                       double now,
                       const Vec3& camera,
                       const GroundHeight& ground);

  // The labels: a mark over each find, seen through what stands in front,
  // and its name and distance from `walker` beside it. `font` sets them;
  // `marks` is the glyph set make_scan_marks made.
  struct ScanMarks {
    render::GlyphSetPtr set;
    render::SlugGlyph diamond, ring;
  };
  [[nodiscard]] ScanMarks make_scan_marks (render::Renderer& renderer);

  // The view the labels are laid out for, so that they keep out of one
  // another's way on screen: the nearest are named, and the rest, and
  // any that would cover a nearer one's label, show their mark alone.
  struct ScanView {
    Vec3 eye {}, right { 1, 0, 0 }, up { 0, 1, 0 }, forward { 0, 0, 1 };
    float tan_half_height = 0.6f;
    float width_pts = 1280.0f, height_pts = 720.0f;
  };

  void draw_scan_labels (render::TextList& text,
                         const render::Font& font,
                         const ScanMarks& marks,
                         const Scan& scan,
                         double now,
                         const Vec3& walker,
                         const ScanView& view);
}

#endif
