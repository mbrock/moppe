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
#include <moppe/terrain/domain.hh>

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace moppe::game {
  // What an older caller still reads the ground through; new code takes a
  // terrain::ElevationField.
  using GroundHeight = std::function<float (float x, float z)>;

  // How far apart the bark of two trees may be for the hammock, metres.
  inline constexpr meters_t hammock_least_span = 2.6f * u::m;
  inline constexpr meters_t hammock_most_span = 5.2f * u::m;

  // The pair of trees among `trunks` that someone standing at `feet` is
  // between, if they would take a hammock: the right distance apart, tall
  // and stout enough, their roots nearly level, and nothing standing or
  // rising between them.
  [[nodiscard]] std::optional<HammockSite>
  hammock_site_at (std::span<const mov::Trunk> trunks,
                   const position_t& feet,
                   const terrain::ElevationField& ground);

  // The fit pair nearest `near`, wherever its trees stand among `trunks`.
  // `shade` is how much farther off a pair counts for each unit of crown
  // standing over it -- a tree within `open_sky` weighs its crown's depth
  // over its distance -- so that a pair under open sky is preferred to a
  // nearer one deep in a stand.
  inline constexpr meters_t open_sky = 16.0f * u::m;
  [[nodiscard]] std::optional<HammockSite>
  nearest_hammock_site (std::span<const mov::Trunk> trunks,
                        const position_t& near,
                        const terrain::ElevationField& ground,
                        meters_t shade = 0.0f * u::m);

  // Every fit pair among `trunks` whose middle lies within `within` of
  // `near`, nearest first, no tree in two of them.
  [[nodiscard]] std::vector<HammockSite>
  hammock_sites_near (std::span<const mov::Trunk> trunks,
                      const position_t& near,
                      meters_t within,
                      const terrain::ElevationField& ground);

  // The middle of the hammock's span, at strap height.
  position_t hammock_middle (const HammockSite& site);
  // The level direction from its foot to its head, and across.
  direction_t hammock_along (const HammockSite& site);
  direction_t hammock_across (const HammockSite& site);
  // How near `feet` is to the line beneath the hammock.
  meters_t hammock_distance (const HammockSite& site, const position_t& feet);

  // How much of a sleeper's weight is in the hammock, easing in as they
  // lie down and out as they get up.
  proportion_t hammock_load (const Camp& camp, play_time_t now);

  // The cloth, its lines, and the straps round the trunks, recorded as
  // triangles; `load` deepens the sag and spreads the cloth, and `swing`
  // turns it about the line between its straps.
  void draw_hammock (render::DrawList& dl,
                     const HammockSite& site,
                     proportion_t load,
                     radians_t swing);

  // The hiker lying in it on their back, hands on their belly, looking
  // along `gaze`.
  AvatarSkeleton pose_resting (const HammockSite& site,
                               radians_t swing,
                               const direction_t& gaze,
                               seconds_t time);

  // Where a sleeper's eyes are, and the direction their gaze names: `yaw`
  // round from the foot of the hammock, `pitch` above the horizon.
  position_t resting_eye (const HammockSite& site, radians_t swing);
  direction_t
  resting_gaze (const HammockSite& site, radians_t yaw, inclination_t pitch);

  // How fiercely the fire burns: it catches over a few seconds and dies
  // quickly when put out.
  proportion_t fire_burn (const Camp& camp, play_time_t now);
  // The fire's light at a moment: its place, and its colour at full
  // strength, flickering.
  position_t fire_light_position (const Camp& camp);
  DisplayColor fire_light_color (proportion_t burn, seconds_t time);
  inline constexpr meters_t fire_light_reach = 17.0f * u::m;

  // How done what is in the pan is: rising to one, fried golden, over
  // the first nine seconds; held there for half a minute; then on to
  // two, burnt black, for anyone who forgets it.
  doneness_t pan_cooked (const Pan& pan, play_time_t now);
  inline constexpr meters_t pan_reach = 2.6f * u::m;

  // Where a cook standing at `feet` sets a pan on the hearth's stones:
  // on their side of the fire, its handle toward them.
  Pan pan_set_on (const position_t& hearth, const position_t& feet);

  // A small iron pan at `place`, its handle `yaw` round from +x toward
  // +z, and the mushrooms lying in it as done as `cooked`, beside `fish`
  // if there is one and as many fried `eggs`.
  void draw_pan (render::DrawList& dl,
                 const position_t& place,
                 radians_t yaw,
                 std::span<const MushroomSite> mushrooms,
                 doneness_t cooked,
                 const Fish* fish = nullptr,
                 int eggs = 0);

  // The ring of stones, the logs, and the bed of ash and embers.
  void draw_hearth (render::DrawList& dl,
                    const position_t& hearth,
                    proportion_t burn);
  // The flames and their glow: additive sprites facing `camera`, drawn
  // after the solid world.
  void draw_fire (render::Renderer& renderer,
                  const position_t& hearth,
                  const position_t& camera,
                  proportion_t burn,
                  seconds_t time);

  // A beacon: a fire on a sanctuary's platform (game/places.hh). Near,
  // a blaze of several camp fires' flames; from far off, a glow over it
  // that shows by night and a column of smoke leaning off on the wind
  // that shows by day, sized to be seen from the next summit.
  void draw_beacon (render::Renderer& renderer,
                    const position_t& hearth,
                    const position_t& camera,
                    proportion_t burn,
                    proportion_t daylight,
                    seconds_t time);
}

#endif
