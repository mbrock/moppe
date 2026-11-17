# Typed quantities: the conventions

Moppe's values carry their meaning in their types (mp-units). A length is
a `meters_t`, a place is a `position_t`, a share of the canopy is a
`map::forest_cover` proportion. A bare `float` says nothing, and that
includes dimensionless numbers: "it has no unit" is never a reason to
leave it raw. `tools/units-report` measures how far each area has come,
and CI fails when any area's typed share drops (`--check`).

## Where the types live

- `moppe/quantities.hh`: every quantity *spec* (what a number means),
  declared with `QUANTITY_SPEC`, never by hand.
- `moppe/gfx/math.hh`: the common aliases (`meters_t`, `seconds_t`,
  `radians_t`, `speed_t`, `proportion_t`, `grade_t`, ...), `Vec2`/`Vec3`,
  and the vector quantities (`position_t`, `displacement_t`,
  `velocity_t`, `direction_t`, the plan types below).
- `moppe/terrain/domain.hh`: the ground's (`SurfaceElevation`,
  `ElevationChange`, `StandingWaterDepth`, `WaterVelocity`,
  `ElevationField`, ...). Concrete aliases stay with the code that owns
  the concept.

## Which type to use

| Value | Type |
| --- | --- |
| A place in 3-D | `position_t` (a point: two do not add) |
| A difference of places, an offset | `displacement_t` |
| A place on the map, no height | `plan_position_t` (over `Vec2`, x then z) |
| Offset on the map | `plan_displacement_t` |
| The world's period, any plan size | `plan_extent_t` |
| A height of the ground or water | `terrain::SurfaceElevation` (a point) |
| Two elevations' difference | `terrain::ElevationChange` |
| A length, radius, reach | `meters_t` |
| An area / volume | `square_meters_t` / `cubic_meters_t` |
| A duration, dt | `seconds_t` (`duration_t` for long double clocks) |
| An age, years ago | `age_t` (Julian years); `julian_years_t` for spans |
| A heading round the up axis | `heading_t` |
| Pitch, lean, look above level | `inclination_t` |
| Any other angle | `radians_t`/`degrees_t` (never mix in floats) |
| Spin rate (scalar / vector) | `radians_per_second_t` / `angular_velocity_t` |
| A unit vector | `direction_t`, `plan_direction_t` |
| Speed / velocity | `speed_t` / `velocity_t`, `plan_velocity_t` |
| A length on the screen: HUD layout, a pointer, safe insets | `points_t` (`u::pt`, `display_points (x)`) |
| A length in a raster's pixels: a sheet or image being drawn | `pixels_t` (`u::px`) |

Screen points, raster pixels, and metres are three dimensions and never
convert into each other; how many pixels a point covers is the host's
render scale, a run-time `px/pt` quantity. The map sheet's line work
(`Ink::stroke` in `game/relief_map.cc`, its `pen`, `across`, `down`, and
dash lengths) is the first place for `pixels_t`.

A plan point moves by a plan displacement and two subtract to one
(`at + along (way, 3.0f * u::m)`, `to - from`). A 3-D `displacement_t`
does not move a plan point; take its `plan ()` first. `position_t` is
older and rooted in `isq::position_vector`, so moving it by a
`displacement_t` needs `quantity_cast<isq::position_vector> (d)`, as
`mov/glider.cc` does.

The plan's lanes are named axes (mp-units' `component_vectors`), each its
own kind: `const auto [x, z] = d;` gives an `x_displacement_t` and a
`z_displacement_t`, which neither add nor compare, and a displacement
built from them in the wrong order does not compile:

```cpp
const plan_displacement_t d = { x_displacement (4.0f * u::m),
                                z_displacement (-2.0f * u::m) };
const auto [east, south] = d;  // east + south does not compile
```

The torus is not a bounded point origin (`wrap_to_range`): what moves
on the world is never wrapped back into it (torus.hh), so wrapping is a
call, not a property of the point.

Crossing between plan and space: `plan (position_t)`, `at_height
(plan_position_t, meters_t)`, `terrain::at_elevation (plan, elevation)`.
Lanes as lengths: `x_of`, `z_of`. Distances: `length (plan_displacement_t)`
is `sqrt (x*x + z*z)`, `hypot` is `cr::hypot (x, z)`; keep whichever the
code used, since they round differently.

## Dimensionless values are typed too

mp-units keeps quantities of dimension one apart by *kind*. A kind
(`subkind`, which mp-units 2.6 calls what was `is_kind`) cannot be
added to, compared with, or assigned from another kind; crossing needs `quantity_cast`, which a reviewer sees. Pick the
narrowest that fits, or add a spec to `quantities.hh`:

| Meaning | Spec / alias |
| --- | --- |
| Share of a whole, 0..1 | `proportion` / `proportion_t`, or a child (`map::forest_cover`, `terrain::soil_wetness`, ...) |
| Chance | `probability_t` |
| A player's or autopilot's stick/throttle | `control_signal_t` |
| A noise sample | `noise_signal_t` |
| Rise over run of a path | `grade_t` (signed); ground steepness is `terrain::slope_t` |
| Lerp weight, smoothing step | `blend_weight_t` (what `smoothing_alpha` will return) |
| Scale, gain, multiplier | `scale_factor_t`; tree sizes `tree_size_factor` |
| Froude number | `froude_number_t` |
| Friction, drag, rolling resistance, efficiency | `isq::static_friction_coefficient`, `isq::kinetic_friction_factor`, `isq::drag_coefficient`, `isq::rolling_resistance_factor`, `isq::mechanical_efficiency` |
| Counts of things | `terrain::CellCount` and siblings; add a kind per countable |
| Whole turns | `isq::rotation` |

Example:

```cpp
// before
float maximum_grade = 0.08f;
float blend = smoothing_alpha (k, dt);
// after
grade_t maximum_grade = 0.08f * grade[one];
blend_weight_t blend = smoothing_alpha (k, dt) * blend_weight[one];
```

All of these, `proportion`, `probability`, `control_signal`, and
`noise_signal` included, are kinds, so a bare float never converts into
one. The idioms that keep the bits the same:

- A share of a length or an angle is named explicitly:
  `meters_t (share * length)`, `radians_t (share * angle)`.
- A product of shares goes back through `quantity_cast<proportion> (...)`;
  `1.0f - p` is `whole - p`.
- Crossing to another kind of one goes through `dimensionless`:
  `quantity_cast<probability> (quantity_cast<dimensionless> (x))`.
- A literal compared with a kind carries that kind:
  `p > 0.5f * proportion[one]`.
- Where a share feeds a raw `Vec3` lane, a `std::` random distribution, or
  another storage boundary, read it with `.numerical_value_in (one)`
  at that spot.

## Fields and callbacks

A function of a map point returns a typed value from a typed point:
`terrain::PlanField<V>` (`std::function<V (plan_position_t)>`), e.g.
`terrain::ElevationField`. New code never takes
`std::function<float (float x, float z)>`. `WorldGround::elevation_field
()` builds the ground's.

## The torus

`moppe/terrain/torus.hh` has typed overloads: `wrap (plan, period)`,
`minimum_image (plan_displacement, period)`, `torus_offset (from, to,
period)`, `nearest_image` for plan and 3-D positions, and the `meters_t`
versions of the scalar functions. The period is one `plan_extent_t`
(`WorldGround::period ()`), not `period_x`/`period_z` floats. Each typed
overload calls the float arithmetic lane by lane, so moving onto it
leaves the bits unchanged.

## WorldGround

Typed: `period ()`, `elevation (plan)`, `still_water (plan)`,
`water_depth (plan)`, `open (plan, room, relief)`, `open_near (...)`,
`on_road (plan, reach, decks)`, `elevation_field ()`. It has no float or
`Vec3` adapters; a caller with a bare `Vec3` takes its `plan` first.

## Trig and its rounding

`sin`, `cos`, and `tan` on a `radians_t` are correctly rounded
(`cr::`), the same on every platform: generation uses them.
`libm::sin`, `libm::cos`, `libm::tan`, `libm::atan2`, `libm::asin`,
`libm::acos`, and `libm::hypot` take and give typed values but round as
`std::` does, so presentation code (meshes, posing, the camera, the
renderer) that called `std::sinf`/`std::hypotf` keeps its bits when it
is typed. Pick the one the code already used.

## Where raw floats stay

Only true storage and API boundaries, with typed values at their edges
(`tools/units-report`'s `BOUNDARIES` table):

- GPU layout: `moppe/nhal/`, uniform/vertex structs mirrored in Luv
  shaders, `mat4.hh`, `render/types.hh`.
- Glyph outlines and font units: `render/slug.*`, `truetype.*`, `text.*`.
- Box3D's API: `mov/box3d_vec.hh` (`to_b3`/`from_b3`).
- The vector representations themselves: `gfx/vector.hh` (`Vec3`,
  `Vec2`, `Float3`, `Quaternion`), which the typed vectors in
  `gfx/math.hh` wrap.
- Noise internals (`terrain/noise.*`); what leaves it is `noise_signal_t`.
- Scalar math libraries (`correct_math.hh`, `gfx/signal.hh`).
- Platform hosts' OS input.
- Raster lanes behind a typed bundle or domain; wire and file formats
  (companion snapshot, world cache bytes), converted where read.

Everything else, including HUD inputs (convert to text at the
formatter) and dimensionless weights, is a gap.

## Converting at a boundary

- Into a type: `x * u::m`, `x * grade[one]`, `position (vec)`,
  `plan_position (x, z)`, `terrain::surface_elevation_point (h)`.
- Out of a type: the named escapes, `meters_value`, `seconds_value`,
  `radians_value`, `position_value`, `plan_value`, ... or
  `numerical_value_in (unit)`. Escape once, at the boundary, not per
  use.
- Keep a hot loop's arithmetic as it was: convert at the loop's edges,
  and keep operand order (no contraction, `-ffp-contract=off`), so the
  world stays bit-identical.

## Working in an area

1. `tools/units-report --files "AREA"` lists the area's worst files.
2. Type signatures and members first; locals follow.
3. Behaviour must not change: `ctest`, `world-fingerprint 257 play 123`
   against `tests/terrain/world-fingerprint-257.txt`, and for game code a
   pinned scene (`MOPPE_WALK`/`MOPPE_DEMO`) before and after.
4. `tools/units-report --update` and commit `tools/units-baseline.json`
   with the change. On a baseline conflict, merge the code and rerun
   `--update`.
5. Road and trail internals (`roads.*`, `road_*.cc`, `trail.*`, `bus.*`)
   wait for the bus thread to land.
