# RFC-0003: A world that reads as alive

Status: proposed

## Decision

Moppe's next campaign is not more simulation. It is **spending what the
generator already knows**: every visible surface — ground, plant, and water —
becomes a function of fields the world already computes, and the systems that
compute those fields stop discarding their own results.

The campaign has four movements, in order:

1. **The ground says where you are.** Material and ecology derive from
   hydrology rather than from noise or from altitude alone.
2. **Surfaces move like living things.** Wind becomes a hierarchy driven by a
   field, foliage responds to the rider, and water detail stops pulsing.
3. **Water acquires kinds.** Confluences, falls, cascades, shores, and the
   rider's contact with water each become a named thing with its own
   treatment, instead of one ribbon material stretched over all of them.
4. **The world remembers.** The merge tree becomes the one hydrological
   structure, storms become a display of the drainage graph, and riding
   leaves a trace the ground keeps.

Movements 1 and 2 are accepted work. Movement 3 is sequenced and scoped but
its later items wait on the first two. Movement 4 is named here so the earlier
work does not foreclose it; it is not decomposed yet.

The execution track is [`living-world`](../tracks/living-world/README.md).

## Why this, and why now

The engine-consolidation track closed with a correct, well-owned engine and an
empty backlog. The obvious next question — "what would make this world feel
alive?" — has a large answer already written down in `ideas/`, particularly
[`geometry-from-fields.md`](../../ideas/geometry-from-fields.md), and a large
supporting literature in the Sheaf library. What was missing was evidence
about which end of that answer to start from.

Riding the current world supplies it, and the finding is uncomfortable: **the
world already computes far more than it shows, and nobody was checking.**

- The terrain material bands compared a metre elevation against thresholds
  written for a normalized `0..1` range. On seed 123 the land runs from the
  50 m datum to 490 m, so the snow band saturated at the shoreline. Every
  generated world was a uniform snowfield; the grass and dirt layers were
  unreachable code. Fixed in `91f04cf`, and the before/after is the whole
  argument for this RFC: the geometry did not change, only what the ground
  was allowed to say about itself.
- `moppe/terrain/merge_tree.cc` is a complete, bit-exact, two-pass replacement
  for the priority flood. It is called only from its test.
- `WaterlineContour` polylines are extracted, used once for a distance
  transform, and dropped. Their header names four intended consumers; none
  exist.
- `StreamPowerEvolutionResult::channel_tangents` is produced every run and
  never fed back as `initial_channel_tangents`.
- `FractionalDrainage`'s `drainage_direction` and `channel_area_flux` columns
  are written and read only by tests.
- Moisture is proximity-to-water blended with drainage accumulation and has
  **no slope term**, so a flat valley floor and a steep gully with the same
  upstream area read equally wet.
- Vegetation wind is a single scalar with one world-space phase, so
  neighbouring trees sway in unison and nothing responds to the bike.
- WebGPU's `set_ocean` discards its texture argument: there are no lakes in
  the browser at all.

A campaign that added a new simulation on top of this would be building on
unspent credit. The readout comes first.

## Constraints

- **Readout before simulation.** Prefer displaying a structure the world
  already computes over computing a new one. A new field must name the
  consumer that forced it.
- **Motion before geometry.** A cheap correct motion cue beats an expensive
  static one. The library is unanimous that wind hierarchy, flow direction,
  and response-to-the-player carry more aliveness per hour than any amount of
  added polygons.
- **Field before object at the render boundary.** Moppe's confluence overlap
  is what happens when object projection leaks past the analysis layer into
  rendering. Where a per-pixel field over a shared domain will do, use it.
- **Fiat thresholds are named program data.** Fall-versus-cascade, water
  permanence, and the persistence granularity are worldbuilding decisions.
  They belong in inspectable recipe data and in TRACE, not in shader
  constants. This is the rendering cash-out of the hydrology-as-granular-
  partitions note (Sheaf `#AHZGJ5`, `#2KBHCL`).
- **Every band constant states its frame.** The bug this RFC opens with was a
  normalized fraction meeting a metre. Where a threshold is a fraction, it
  divides by the range it is a fraction of, in the same expression.
- **Both backends, or say so.** WebGPU may present less, but a feature that
  silently no-ops there is a lie in the atlas.
- **Judged by riding.** Each item states what a capture or a ride should show.
  The deterministic water and tree captures are the instrument; the human
  verdict is the test.

## Deliberate non-goals

- Ecosystem succession. Deussen's competition model runs as one analysis pass
  over the finished surface; FastFlow's own ecosystem figures take hours at
  monthly timesteps. Moppe simulates no plant lifetimes.
- Fluid simulation for rivers. Procedural Riverscapes measured the
  alternative at 165 hours of authoring iteration against one hour.
- A general weather system. Storms enter as a scalar sweeping the existing
  drainage graph, or not at all.
- Rewriting the terrain as objects. `ideas/rivers-first.md` remains a design
  essay; nothing here commits to it, and nothing here forecloses it.
- Wildlife. It was deleted for good reason and does not return until the
  places it would inhabit are worth inhabiting.

## Research grounding

The Sheaf library carries the technique for nearly every item. The load-bearing
sources, by movement:

1. Deussen et al., *Realistic Modeling and Rendering of Plant Ecosystems*
   (`#GBXEP3`) for vigor, wet/dry preference, and competitive self-thinning;
   FastFlow (`#NV2YRW`) for the topographic wetness index and for the
   multiple-flow-direction caveat — ecosystems want diffuse accumulation where
   channels want single-receiver.
2. *The Vegetation of Horizon Zero Dawn* (`#ABD2B8`) for the three-level wind
   hierarchy, per-object phase, camera tilt, ground hugging, and
   distance-faded animation; *Responsive Real-Time Grass* (`#PQ68ZH`) for the
   decaying collision-strength field that makes a crushed blade stay crushed;
   *Water Flow in Portal 2* (`#A2QB8L`) for flow-map advection, the
   noise-decorrelated phase that kills pulsing, and speed-scaled normal
   strength.
3. *Real-time Rendering of River Networks* (`#MVUJ8Z`) for junctions as a
   per-pixel curve-distance field with no junction mesh; *Procedural
   Riverscapes* (`#AK7NGE`) for the refined longitudinal profile that
   distinguishes a lip from a cascade, and for the primitive taxonomy;
   *Real-time Breaking Waves* (`#8SERGP`) for the falling sheet as advected
   particle lines and for volume-conserving body coupling; halftone foam
   (`#869NHK`) for foam that clumps and pops at under 3% cost.
4. Priority-Flood (`#MTDKDE`) and the merge-tree argument in
   `ideas/computed-together.md`.

The library also warns clearly, and those warnings are constraints: sums of
sines do not read as ocean (`#C4AY2M`); height fields cannot grow waterfalls
(`#AK7NGE`); Eulerian texture advection stretches or loses motion, pick your
failure (`#92XRH7`); and the analytical erosion multigrid can relocate valleys
when the age slider moves (`#DWXKYQ`), which would break deep-time scrubbing
before it is built.

## Completion

The RFC is realized when movements 1 and 2 are done, movement 3's first three
items have landed, and a ride through a fresh seed shows: ground whose cover
changes with wetness and altitude, trees that do not sway in unison and part
around the bike, a river that reads as one watercourse through its
confluences, and a fall with a lip. Movement 4 becomes its own RFC.
