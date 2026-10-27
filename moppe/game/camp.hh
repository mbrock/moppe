#ifndef MOPPE_GAME_CAMP_HH
#define MOPPE_GAME_CAMP_HH

// Making camp: a hammock hung between two trees a few metres apart, to lie
// in and watch the sky turn, and a small fire.
//
// The hiker carries one hammock. Standing between two trees that will take
// it -- far enough apart to lie between, bare of branches to above head
// height (birches mostly: a spruce keeps its skirts), on ground level
// enough that neither strap is out of reach -- they hang it; lying
// in it they look up through the crowns, and the day passes quickly. The
// fire is a ring of stones and a few logs, lit a step ahead of where the
// hiker stands; it lights what is around it (the renderer's lamp).

#include <moppe/color.hh>
#include <moppe/game/avatar.hh>
#include <moppe/game/camp_state.hh>
#include <moppe/game/mushrooms.hh>
#include <moppe/gfx/math.hh>
#include <moppe/mov/trunk_field.hh>
#include <moppe/render/draw.hh>
#include <moppe/render/renderer.hh>

#include <functional>
#include <optional>
#include <span>

namespace moppe::game {
  using GroundHeight = std::function<float (float x, float z)>;

  // How far apart the bark of two trees may be for the hammock, metres.
  inline constexpr float hammock_least_span = 2.6f;
  inline constexpr float hammock_most_span = 5.2f;

  // The pair of trees among `trunks` that someone standing at `feet` is
  // between, if they would take a hammock: the right distance apart, tall
  // and stout enough, their roots nearly level, and nothing standing or
  // rising between them.
  [[nodiscard]] std::optional<HammockSite>
  hammock_site_at (std::span<const mov::Trunk> trunks,
                   const Vec3& feet,
                   const GroundHeight& ground);

  // The fit pair nearest `near`, wherever its trees stand among `trunks`.
  // `shade` is how many metres farther off a pair counts for each unit of
  // crown standing over it -- a tree within `open_sky` metres weighs its
  // crown's depth over its distance -- so that a pair under open sky is
  // preferred to a nearer one deep in a stand.
  inline constexpr float open_sky = 16.0f;
  [[nodiscard]] std::optional<HammockSite>
  nearest_hammock_site (std::span<const mov::Trunk> trunks,
                        const Vec3& near,
                        const GroundHeight& ground,
                        float shade = 0.0f);

  // The middle of the hammock's span, at strap height.
  Vec3 hammock_middle (const HammockSite& site);
  // The unit horizontal direction from its foot to its head, and across.
  Vec3 hammock_along (const HammockSite& site);
  Vec3 hammock_across (const HammockSite& site);
  // How near `feet` is to the line beneath the hammock, metres.
  float hammock_distance (const HammockSite& site, const Vec3& feet);

  // How much of a sleeper's weight is in the hammock, 0..1, easing in as
  // they lie down and out as they get up.
  float hammock_load (const Camp& camp, double now);

  // The cloth, its lines, and the straps round the trunks, recorded as
  // triangles; `load` deepens the sag and spreads the cloth.
  void draw_hammock (render::DrawList& dl,
                     const HammockSite& site,
                     float load,
                     float swing);

  // The hiker lying in it on their back, hands on their belly, looking
  // along `gaze`.
  AvatarSkeleton pose_resting (const HammockSite& site,
                               float swing,
                               const Vec3& gaze,
                               float time);

  // Where a sleeper's eyes are, and the direction their gaze angles name.
  Vec3 resting_eye (const HammockSite& site, float swing);
  Vec3 resting_gaze (const HammockSite& site, float yaw, float pitch);

  // How fiercely the fire burns, 0..1: it catches over a few seconds and
  // dies quickly when put out.
  float fire_burn (const Camp& camp, double now);
  // The fire's light at a moment: its place, and its colour at full
  // strength, flickering.
  Vec3 fire_light_position (const Camp& camp);
  DisplayColor fire_light_color (float burn, float time);
  inline constexpr float fire_light_reach = 17.0f;

  // How done what is in the pan is: rising to one, fried golden, over
  // the first nine seconds; held there for half a minute; then on to
  // two, burnt black, for anyone who forgets it.
  float pan_cooked (const Pan& pan, double now);
  inline constexpr float pan_reach = 2.6f;

  // Where a cook standing at `feet` sets a pan on the hearth's stones:
  // on their side of the fire, its handle toward them.
  Pan pan_set_on (const Vec3& hearth, const Vec3& feet);

  // A small iron pan at `place`, its handle along `yaw`, and the
  // mushrooms lying in it as done as `cooked`, beside `fish` if there is
  // one.
  void draw_pan (render::DrawList& dl,
                 const Vec3& place,
                 float yaw,
                 std::span<const MushroomSite> mushrooms,
                 float cooked,
                 const Fish* fish = nullptr);

  // The ring of stones, the logs, and the bed of ash and embers.
  void draw_hearth (render::DrawList& dl, const Vec3& hearth, float burn);
  // The flames and their glow: additive sprites facing `camera`, drawn
  // after the solid world.
  void draw_fire (render::Renderer& renderer,
                  const Vec3& hearth,
                  const Vec3& camera,
                  float burn,
                  float time);

  // A beacon: a fire on a sanctuary's platform (game/places.hh). Near,
  // a blaze of several camp fires' flames; from far off, a glow over it
  // that shows by night and a column of smoke leaning off on the wind
  // that shows by day, sized to be seen from the next summit.
  void draw_beacon (render::Renderer& renderer,
                    const Vec3& hearth,
                    const Vec3& camera,
                    float burn,
                    float daylight,
                    float time);
}

#endif
