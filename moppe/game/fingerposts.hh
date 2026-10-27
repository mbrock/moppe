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

  // A board's distance as a Swedish signpost gives it: "2,4 km".
  [[nodiscard]] std::string fingerpost_distance (float metres);

  // The posts made: weathered square timber with a board for each place
  // an arm leads to, stacked down the post and pointed at the tip. Cart
  // roads' boards are white with black letters, footpaths' brown with
  // cream, and those along ways no longer used grey and faded. The
  // lettering is set in the HUD's face and rasterized from its Slug
  // outlines into one texture of boards (`boards`), so it is lit,
  // shadowed, and hidden like the rest of the world; `mesh` is recorded
  // in world space, to be appended to the world's draw list.
  struct FingerpostSigns {
    render::TexturePtr boards;
    render::DrawList mesh;
  };

  [[nodiscard]] FingerpostSigns
  make_fingerposts (render::Renderer& renderer,
                    const render::Font& font,
                    std::span<const Fingerpost> posts,
                    std::span<const std::string> names,
                    const std::function<float (float x, float z)>& ground);
}

#endif
