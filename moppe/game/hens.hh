#ifndef MOPPE_GAME_HENS_HH
#define MOPPE_GAME_HENS_HH

// The farmsteads' poultry and their eggs. About each barn lived in
// (game/places.hh) go a rooster and his hens -- Skånsk blommehöns, the
// Scanian landrace, speckled black, ochre, and white -- and a hen who has
// hatched a brood, her chicks at her heels. By day they scratch and peck
// about the byre and out in the yard before its doors, wandering in and
// out up the ramp; someone stepping among them sends them scurrying; at
// dusk they go in and up onto the perches in the hens' corner, the
// mother settling on the floor below with her chicks under her, and at
// dawn they come down again. They are drawn from models/hen.blend,
// rooster.blend, and chick.blend (tools/figure/hens.py) and, like the
// other animals, are not part of the shared game state.
//
// The hens lay in the row of nest boxes on the barn's end wall
// (tools/figure/farm.py's BARN_NESTS). A box holds what has been laid in
// it since it was last emptied, up to three eggs, a hen laying in it
// every five hours of the clock or so; the eggs are taken, one at a
// time, into the walker's basket, and fried for supper (game/camp.hh).

#include <moppe/game/animals.hh>
#include <moppe/game/basket.hh>
#include <moppe/gfx/mat4.hh>
#include <moppe/gfx/math.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace moppe::game {
  // One barn's poultry: its frame (game/places.hh's building_frame), so
  // that where they go can be said in the barn's own measures.
  struct Coop {
    Mat4 frame;
  };

  struct Bird {
    enum class Kind : std::uint8_t { hen, rooster, chick };
    Kind kind = Kind::hen;
    std::uint32_t seed = 0;
    std::uint32_t coop = 0;
    // Where its feet stand and which way it faces, radians about the
    // vertical from +z toward +x.
    Vec3 position {};
    float heading = 0.0f;
    enum class Doing : std::uint8_t {
      pecking,
      walking,
      standing,
      scurrying,
      roosting
    };
    Doing doing = Doing::pecking;
    float left = 0.0f;
    // The way to where it is going, in the barn's frame: up to three
    // points, the last the goal.
    std::array<Vec3, 3> way {};
    int ways = 0;
    // Out in the yard rather than in the barn.
    bool outside = false;
    float speed = 0.0f;
    // How far its stride has gone; how far its head is down, 0..1; and
    // how far it is up on its perch, 0..1.
    float gait = 0.0f;
    float peck = 0.0f;
    float perch = 0.0f;
    // Where on the perches it roosts, in the barn's frame, and its
    // footing below them.
    Vec3 roost {};
    // A chick's mother, an index into the birds.
    int mother = -1;
  };

  // The poultry of each coop: a rooster, his hens, and a brood.
  [[nodiscard]] std::vector<Bird> plan_birds (std::span<const Coop> coops,
                                              std::uint32_t seed);

  class Hens {
  public:
    void rebuild (std::vector<Coop> coops, std::vector<Bird> birds);

    // Advances those within `reach` of `focus` by `dt` seconds at the
    // clock's `hour` (0..24); `ground` is the floor-aware ground the
    // animals stand on.
    void step (const AnimalGround& ground,
               std::span<const Presence> presences,
               const Vec3& focus,
               float reach,
               float hour,
               float dt);

    std::span<const Bird> birds () const noexcept {
      return m_birds;
    }
    std::span<const Coop> coops () const noexcept {
      return m_coops;
    }

  private:
    void choose (Bird& bird, const AnimalGround& ground, bool night);
    float rand (std::uint32_t& state) const;

    std::vector<Coop> m_coops;
    std::vector<Bird> m_birds;
    std::vector<std::uint32_t> m_random;
  };

  // The birds within `reach` of the camera and before it.
  void render_birds (render::Renderer& r,
                     std::span<const Bird> birds,
                     const Vec3& camera,
                     const Vec3& forward,
                     float time,
                     float reach,
                     std::uint64_t motion_base);

  // -- eggs ---------------------------------------------------------

  // A nest box: where its eggs lie on the straw.
  struct Nest {
    Vec3 at {};
    // The way out of the box, into the barn.
    Vec3 out {};
  };

  // The boxes' first eggs, laid before the walker came: one or two each.
  [[nodiscard]] std::vector<NestState>
  first_eggs (std::size_t nests, double hours, std::uint32_t seed);

  // Lays what the hens have laid by `hours`.
  void lay_eggs (std::span<NestState> states, double hours, std::uint32_t seed);

  // The nest box with an egg in it that a walker standing at `feet`,
  // facing `heading`, can reach into.
  [[nodiscard]] std::optional<std::size_t>
  nest_within_reach (std::span<const Nest> nests,
                     std::span<const NestState> states,
                     const Vec3& feet,
                     const Vec3& heading);

  // An egg, its long axis along `along`, `cooked` 0 raw to 2 burnt black
  // (camp.hh's pan_cooked); a fried one lies broken open, white and yolk.
  void draw_egg (render::DrawList& dl,
                 const Vec3& at,
                 const Vec3& along,
                 std::uint32_t seed);
  void draw_fried_egg (render::DrawList& dl,
                       const Vec3& at,
                       float yaw,
                       float cooked,
                       std::uint32_t seed);

  // The eggs in the nest boxes near `camera`.
  void draw_nest_eggs (render::DrawList& dl,
                       std::span<const Nest> nests,
                       std::span<const NestState> states,
                       const Vec3& camera);
}

#endif
