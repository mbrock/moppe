#ifndef MOPPE_GAME_FINGERPOSTS_HH
#define MOPPE_GAME_FINGERPOSTS_HH

#include <moppe/render/draw.hh>
#include <moppe/render/text.hh>
#include <moppe/terrain/roads.hh>

#include <cstddef>
#include <functional>
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
      float metres = 0.0f;
    };

    struct Arm {
      // The way the arm points across the ground from the post, a unit
      // vector.
      float direction_x = 1.0f;
      float direction_z = 0.0f;
      terrain::Road::Kind kind = terrain::Road::Kind::Road;
      // The way it points along is no longer used (roads.hh).
      bool abandoned = false;
      // The nearest first, at most two.
      std::vector<Board> boards;
    };

    // Where the post stands, beside the junction and off the tread, and
    // the junction itself; both within one period of the world.
    float x_m = 0.0f;
    float z_m = 0.0f;
    float junction_x_m = 0.0f;
    float junction_z_m = 0.0f;
    std::vector<Arm> arms;
  };

  // Reads the network as a graph -- cut where a way's end meets another
  // way (the planner lays each new way from or to an old one, so that is
  // where they all meet) and where each place lies on it -- and
  // plans a post at each junction that is not itself one of the places
  // (they have their own landmarks). The periods are the world's.
  [[nodiscard]] std::vector<Fingerpost> plan_fingerposts (
    const terrain::RoadNetwork& network, float period_x, float period_z);

  // A board's distance as a Swedish signpost gives it: metres to the
  // nearest ten under a kilometre ("320 m"), and beyond, kilometres as a
  // bare figure with a decimal comma ("2,4").
  [[nodiscard]] std::string fingerpost_distance (float metres);

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
  // Renderer::create_world_text.
  struct FingerpostSigns {
    render::DrawList mesh;
    render::TextList lettering;
  };

  [[nodiscard]] FingerpostSigns
  make_fingerposts (render::Renderer& renderer,
                    const render::Font& font,
                    std::span<const Fingerpost> posts,
                    std::span<const std::string> names,
                    const std::function<float (float x, float z)>& ground);
}

#endif
