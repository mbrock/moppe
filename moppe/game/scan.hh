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
#include <moppe/game/game_clock.hh>
#include <moppe/game/mushrooms.hh>
#include <moppe/gfx/math.hh>
#include <moppe/mov/trunk_field.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/text.hh>

#include <span>
#include <string>
#include <vector>

namespace moppe::game {
  // How far the ring goes, how fast, and how long what it finds stays lit
  // before fading.
  inline constexpr meters_t scan_reach = 60.0f * u::m;
  inline constexpr speed_t scan_speed = 24.0f * u::m / u::s;
  inline constexpr seconds_t scan_lit = 12.0f * u::s;
  inline constexpr seconds_t scan_fade = 1.5f * u::s;

  struct ScanFind {
    enum class Kind : std::uint8_t { mushrooms, poisonous, hammock };
    Kind kind = Kind::mushrooms;
    // Where the thing is: a group's middle on the ground, or the middle
    // of a hammock's span at strap height.
    position_t at = position (Vec3 ());
    // How many mushrooms of one kind the group holds.
    int count = 1;
    MushroomKind mushroom = MushroomKind::chanterelle;
    HammockSite hammock {};
  };

  // The unpicked mushrooms within reach of `origin`, a find for each group
  // of one kind standing together (within a few metres of the group's
  // first), the poisonous apart.
  [[nodiscard]] std::vector<ScanFind>
  scan_mushrooms (const MushroomPatch& patch, const position_t& origin);

  [[nodiscard]] std::vector<ScanFind>
  scan_hammocks (std::span<const HammockSite> sites);

  class Scan {
  public:
    void start (const position_t& origin,
                play_time_t now,
                std::vector<ScanFind> finds);

    // Whether anything of it still shows.
    bool showing (play_time_t now) const;
    // How far the ring has gone.
    meters_t radius (play_time_t now) const;
    // How much a find shows: none until the ring reaches it.
    proportion_t shown (const ScanFind& find, play_time_t now) const;
    // The time since the ring reached it, negative before.
    seconds_t since_found (const ScanFind& find, play_time_t now) const;

    const position_t& origin () const {
      return m_origin;
    }
    const std::vector<ScanFind>& finds () const {
      return m_finds;
    }

  private:
    position_t m_origin = position (Vec3 ());
    play_time_t m_start = play_time (-1e9);
    std::vector<ScanFind> m_finds;
  };

  // The ring on the ground and the lines from each find up to its label,
  // as additive glow in world space.
  void draw_scan_glow (render::DrawList& dl,
                       const Scan& scan,
                       play_time_t now,
                       const position_t& camera,
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
    position_t eye = position (Vec3 ());
    direction_t right = unit_direction (Vec3 (1, 0, 0));
    direction_t up = unit_direction (Vec3 (0, 1, 0));
    direction_t forward = unit_direction (Vec3 (0, 0, 1));
    float tan_half_height = 0.6f;
    float width_pts = 1280.0f, height_pts = 720.0f;
  };

  void draw_scan_labels (render::TextList& text,
                         const render::Font& font,
                         const ScanMarks& marks,
                         const Scan& scan,
                         play_time_t now,
                         const position_t& walker,
                         const ScanView& view);

  // The scan as play keeps it: started at the walker's feet with what is
  // there to find, and drawn over the scene while it shows.
  class Scanner {
  public:
    // A ring out from `feet` from `now`, finding the mushrooms and the
    // trees fit for a hammock within reach.
    void start (const position_t& feet,
                play_time_t now,
                const MushroomPatch& mushrooms,
                const mov::TrunkField& trunks,
                const GroundHeight& ground);

    // The glow and, once the HUD's `font` is up, the labels as seen in
    // `view`, named by their distance from `walker`.
    void draw (render::Renderer& r,
               const render::Font* font,
               play_time_t now,
               const ScanView& view,
               const position_t& walker,
               const GroundHeight& ground);

  private:
    Scan m_scan;
    ScanMarks m_marks;
    render::DrawList m_dl;
    render::TextList m_text;
  };
}

#endif
