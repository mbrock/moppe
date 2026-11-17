#ifndef MOPPE_GAME_FINGERPOSTS_HH
#define MOPPE_GAME_FINGERPOSTS_HH

#include <moppe/gfx/math.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/text.hh>
#include <moppe/terrain/domain.hh>
#include <moppe/terrain/roads.hh>

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace moppe::game {
  // A fingerpost where ways meet: at each junction of the road network
  // (roads.hh) and where a footpath leaves a road, a post beside the way
  // with an arm along each way, carrying the names of the places that way
  // is the shortest to and how far they are along the network.
  struct Fingerpost {
    struct Board {
      // Into network.places.
      std::size_t place = 0;
      // How far along the network.
      meters_t distance {};
    };

    struct Arm {
      // The way the arm points across the ground from the post.
      plan_direction_t direction = plan_unit_direction (Vec2 (1.0f, 0.0f));
      terrain::Road::Kind kind = terrain::Road::Kind::Road;
      // The way it points along is no longer used (roads.hh).
      bool abandoned = false;
      // The nearest first, at most two.
      std::vector<Board> boards;
    };

    // Where the post stands, beside the junction and off the tread, and
    // the junction itself; both within one period of the world.
    plan_position_t at {};
    plan_position_t junction {};
    std::vector<Arm> arms;
  };

  // Reads the network as a graph -- cut where a way's end meets another
  // way (the planner lays each new way from or to an old one, so that is
  // where they all meet) and where each place lies on it -- and
  // plans a post at each junction that is not itself one of the places
  // (they have their own landmarks). The period is the world's.
  [[nodiscard]] std::vector<Fingerpost>
  plan_fingerposts (const terrain::RoadNetwork& network, plan_extent_t period);

  // A board's distance as a Swedish signpost gives it: metres to the
  // nearest ten under a kilometre ("320 m"), and beyond, kilometres as a
  // bare figure with a decimal comma ("2,4").
  [[nodiscard]] std::string fingerpost_distance (meters_t distance);

  // The posts made: weathered square timber with a board for each place
  // an arm leads to, stacked down the post and pointed at the tip, as
  // long as its lettering, after Sweden's vägvisare. Cart roads' boards
  // are an enskild väg's yellow with black letters, footpaths' a hiking
  // trail's brown with white, and those along ways no longer used grey
  // and faded. `font` is the signs' face (Geomini SemiBold). `mesh`,
  // recorded in world space to be drawn as a mesh, is the timber and the
  // boards; `lettering` is what is painted on each face of a board -- the
  // name and distance set in the HUD's face, and a border following the
  // board's edge -- as Slug glyphs placed in world metres, for
  // Renderer::create_world_text. `ground` is the ground's elevation, roads
  // and all, under a point within one period.
  struct FingerpostSigns {
    render::DrawList mesh;
    render::TextList lettering;
  };

  [[nodiscard]] FingerpostSigns
  make_fingerposts (render::Renderer& renderer,
                    const render::Font& font,
                    std::span<const Fingerpost> posts,
                    std::span<const std::string> names,
                    const terrain::ElevationField& ground);

  // A world's posts, planned with it and made the first time they are
  // drawn, when the renderer and the HUD's face are up.
  class RoadSigns {
  public:
    // Plans the posts of a world just activated, to be made anew.
    void plan (const terrain::RoadNetwork& network, plan_extent_t period);

    // Makes the posts if they are not yet made and `hud_font` is up,
    // naming the places `names`, then draws them.
    void draw (render::Renderer& r,
               const render::Font* hud_font,
               std::span<const std::string> names,
               const terrain::ElevationField& ground);

    // The timber and boards in the sun's shadow.
    void cast_shadow (render::Renderer& r) const;

  private:
    std::vector<Fingerpost> m_posts;
    bool m_made = false;
    render::MeshPtr m_mesh;
    render::WorldTextPtr m_lettering;
    // The signs letter in the face's heavier cut, which carries further.
    std::unique_ptr<render::Font> m_font;
  };
}

#endif
