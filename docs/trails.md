# Trail system

Moppe currently makes one deliberate leisure circuit. It is not a natural
track network extracted from drainage, and its continuous alignment is not
placed independently of the terrain. The system first chooses a plausible
home base and a scenic reason for a loop, searches for two distinct rides
around that feature, refines the discrete route into a smooth designed
alignment, and then reconciles that alignment with the heightmap using bounded
cut and fill.

The result has four related but separate meanings:

- a discrete plan and directed circuit that records the searched topology;
- a continuous plan-view alignment used by grading and presentation;
- shaped terrain where construction was needed and possible;
- continuous trail and home-base influence fields used by materials and
  surface queries.

This distinction is important: the trail material covers the complete planned
circuit. Trail influence is not created only where earthworks changed the
heightmap.

## Place in world generation

The default world program runs trail formation after geological evolution:

```text
geological source
  -> orogeny and erosion
  -> drainage and standing-water readings
  -> trail planning
  -> continuous alignment fitting
  -> bounded trail grading and cross-section stamping
  -> refreshed heightmap and normals
  -> trail/home-base surface fields and renderer texture
```

`TrailFormation` is a `TerrainTransform`, so the game, Terrain Lab, tests, and
`terrain-pipeline-demo` execute the same operation. The main implementation is
in [`trail.cc`](../moppe/terrain/trail.cc), with its public values and result
types in [`trail.hh`](../moppe/terrain/trail.hh).

## Parameters and units

The defaults describe the first leisure path:

| Parameter | Default | Meaning |
| --- | ---: | --- |
| minimum catchment | 5,000 m2 | Lower end of the useful valley-floor range |
| maximum catchment | 100,000 m2 | Upper end of the useful valley-floor range |
| minimum height above sea | 1.5 m | Keeps work off the waterline |
| path width | 3 m | Requested compacted core width |
| shoulder blend | 4 m | Soft transition from core to natural ground |
| maximum cut | 2.5 m | Most any sample may be lowered from original terrain |
| maximum fill | 1.5 m | Most any sample may be raised from original terrain |
| designed grade | 5% | Ordinary leisure-route target and search preference |
| maximum grade | 12% | Formation target for constrained local exceptions |
| grading iterations | 24 | Repeated relaxation passes around the circuit |
| home base to water | 90 m | Preferred water distance during site selection |
| home-base pad radius | 18 m | Compacted clearing around the circuit origin |
| desired circuit radius | 900 m | Scale for the scenic focus and loop |
| highland preference | 180 m above sea | Height where pioneer routes begin avoiding ascent |
| alpine avoidance | 285 m above sea | Height where the quadratic avoidance cost is strong |

Horizontal distances, cut, fill, and circuit size are physical lengths. Grade
is dimensionless rise divided by horizontal run.

## What grade means

Grade is measured **along the path**:

```text
grade = absolute elevation change / horizontal distance along the segment
```

A 5% grade climbs 5 metres over 100 horizontal metres. A path following a
contour line has 0% longitudinal grade even when the hillside under it is
steep from side to side.

Cross-slope is a different concern. The formation stamp pulls the riding
surface toward the centerline elevation, which benches the path across a
sidehill. Cut and fill limits bound that operation, so a sufficiently steep
sidehill may retain some cross-slope.

The designed and maximum grades are not equivalent:

- `designed_grade` tells route search what an ordinary comfortable segment
  looks like. Exceeding it becomes progressively expensive.
- `maximum_grade` is used when estimating feasibility and when relaxing the
  chosen centerline. It is the desired local ceiling after available
  earthworks, not permission for route search to routinely use 12% slopes.

Because every height sample is limited to 2.5 m of cut and 1.5 m of fill, the
finished path can still contain grade exceptions. The report counts these
segments instead of pretending the maximum was achieved.

## 1. Planning grid and terrain readings

Planning uses the materialized `TerrainView`, `DrainageGraph`, and
`FloodField`. A planning grid samples the full heightmap at approximately
16-metre spacing. It retains physical elevation, distance, local slope,
catchment area, standing-water occupancy, and bounded-versus-toroidal
topology.

Every coarse edge is inspected at full source resolution. Its profile records
mean grade, maximum local grade, and the best grade that could remain after
the available combined cut/fill allowance. This prevents a coarse edge from
hiding a severe wave between its endpoints.

## 2. Home-base expeditions

The system does not commit to the single highest-scoring start. It keeps up to
eight separated home-base candidates and attempts a complete expedition from
each one.

A candidate must be dry, above the minimum water clearance, and not
extraordinarily steep. A later expedition succeeds only when the complete
circuit stays on routable land. The candidate score prefers:

- a useful distance from water;
- locally flat ground;
- nearby shelter rather than total exposure;
- distant relief that gives the place a view;
- lower elevations over unnecessarily alpine camps.

On a toroidal world, the base also stays away from the arbitrary map seam so
the circuit reads as one loop on the minimap.

Each expedition chooses a scenic focus at roughly the desired circuit radius.
Water and strong local relief make good focuses, but elevation is priced
separately: a mountain may provide the view without requiring the trail to
visit its summit. Four control sites then state the intended topology: home
base, left flank, far side, and right flank. Their placement also prefers the
lower available ground around the feature.

## 3. Route search

The two halves of the circuit are heading-aware A* searches over eight-neighbor
terrain states:

```text
home -> left flank -> far side
home -> right flank -> far side
```

The second half is reversed and joined to the first, producing one directed
cycle. Avoidance masks discourage the second half from reusing the first
except near their shared endpoints.

An edge is rejected when it is wet, outside the allowed corridor, or remains
far too steep even after the available earthwork allowance. Edges that remain
feasible are priced by:

- longitudinal grade relative to the 5% design target;
- absolute earthwork required beyond that target;
- any residual excess over the 12% maximum;
- abrupt heading changes;
- unnecessarily large detours;
- departure from the preferred catchment range;
- highland and alpine exposure;
- proximity to the arbitrary torus chart edge.

The elevation policy is deliberately progressive rather than a hard wall.
Below 180 metres above sea it contributes no cost. Above that point it rises
quadratically, reaching a strong avoidance weight at 285 metres and continuing
to increase beyond it. The default world's gently sloped snow begins at about
308 metres above sea, so an ordinary pioneer circuit should turn toward a low
pass before entering persistent snow. A high saddle remains possible when the
geography offers no credible lower connection; generation does not fail merely
because the terrain is mountainous.

Construction capacity and route desirability are deliberately separate.
Maximum cut plus fill determines whether an edge might be buildable, but a
larger construction budget does not discount the cost of choosing a steep
edge. This keeps added engineering capability from making contour-following
routes less attractive.

Completed expeditions are scored as whole loops. The best successful loop is
expanded back to source-grid cells. Expansion must produce a continuous
cycle of neighboring cells; an overlap that would create a jump is treated as
a failed expedition rather than silently entering the network.

The chosen planning loop also becomes a periodic cubic Hermite alignment in
physical world coordinates. Its damped tangents round the eight-way A*
corners without the larger overshoot of an unconstrained interpolating spline.
The curve is sampled at approximately two-metre spacing and kept in an
unwrapped coordinate chart, so the toroidal seam cannot introduce a false
bend. A* therefore chooses the buildable corridor; the alignment defines the
trail that is intentionally built inside it.

## 4. Plan and network

`TrailPlan` retains the decisions:

- `home_base` and `scenic_focus`;
- the four control sites;
- the ordered circuit cells.

`TrailNetwork` materializes those decisions as one connected directed cycle.
Each circuit cell has one `receiver`, and following receivers once around the
component returns to the home base. `component_by_cell` supports membership
queries; the current design always produces one component. The network also
owns `TrailAlignment`, the densely sampled continuous loop and its physical
arc length.

The discrete graph remains useful independently of rendering. The alignment
supplies circuit length, spawn heading, the minimap curve, and the opening
cinematic route.

## 5. Grading and earthworks

Formation samples the pre-trail terrain along the continuous alignment. For
24 iterations, neighboring arc-length samples whose elevation difference
exceeds the 12% target move halfway toward compliance. Every sample remains
clamped to its own original elevation plus the fill allowance or minus the cut
allowance.

Each nearby heightmap sample then finds its nearest point on the alignment and
interpolates the relaxed vertical profile there. The compacted core targets
that centerline height; the shoulder smoothly blends back to the original
surface. Standing-water cells are never shaped. The result retains a physical
`earthwork_delta_m` layer relative to the pre-trail terrain while also
materializing the composed heightmap used by the current renderer and physics.

At coarse preview resolutions, the effective half-width is at least slightly
more than one terrain cell. This preserves a continuous ribbon, but it also
means preview images may show a path wider than the nominal 3 metres.

`TrailFormationReport` records what actually happened:

- centerline and shaped-cell counts;
- connected components and circuit length;
- cut and fill volumes;
- mean and maximum centerline grade;
- number of segments above the maximum-grade target;
- maximum centerline height above sea;
- mean and maximum absolute terrain change.

## 6. Influence fields and materials

Planning samples two continuous scalar fields independently of whether a
cell's height changed:

- `trail_influence` covers the full circuit with a solid core and soft
  shoulders;
- `home_base_influence` is a separate radial field for the origin clearing.

Trail influence is the authored cross-section evaluated from distance to the
continuous alignment, rather than a union of disks around raster circuit
cells. The construction stamp may still widen its geometric core to one
heightmap cell at coarse resolutions for collision continuity; the material
footprint retains the requested physical width.

These fields are stored on `TrailNetwork`, expanded across the duplicated
torus render seam, and materialized as typed columns in `map::Surface`. The
Metal renderer packs them into the R and G channels of one `RG16Float` terrain
texture.

The terrain fragment shader interprets trail influence as a formed
cross-section. It adds a warm crushed-stone base, world-space aggregate
variation, darker shoulders, paired wear bands, a subtle compacted center,
and close-range detail normals. Broad color and shoulder contrast remain at
overview distance. Fine aggregate and micro-normal frequencies fade according
to their screen-space footprint, so shallow viewing angles cannot turn them
into moire or temporal sparkle.

Snow and standing water suppress the trail material. They do not delete the
underlying circuit or influence field; the path can remain part of gameplay
while being visually covered.

The shader has only a scalar distance-like influence, not a tangent or signed
cross-track coordinate. Its paired wear bands are therefore contours within
the mask, not simulated wheel tracks with independent direction data. Keeping
the path entirely in the terrain material also gives it the terrain's exact
depth and sampling behavior; a separate translucent ribbon produced subpixel
sparkle and could leak through distant terrain.

## Runtime consumers

After generation, the game uses the trail system in several places:

- The bike spawns at the home base facing the alignment tangent.
- A flag marks the base clearing.
- The lower-left minimap draws the smooth alignment, home base, player
  position, and heading on sufficiently large displays.
- The opening drone cinematic begins with a high oblique trail reveal, sweeps
  across the first half of the circuit, and then continues to the valley,
  waterfall or lake, saddle, peak, and arrival beats.
- Terrain Lab can edit trail parameters and inspect the formation report.

The relevant runtime code is in
[`game.cc`](../moppe/game/game.cc), while the flyover planner lives in
[`cinematic_flight.cc`](../moppe/game/cinematic_flight.cc). The trail material
is in [`terrain.metal`](../moppe/shaders/metal/terrain.metal).

## Inspection and verification

Run the shared world pipeline and print the trail report:

```sh
./build/terrain-pipeline-demo /tmp/trails.png 257 123 combined world
```

The game logs the number of centerline cells, circuit length, component count,
home base, scenic focus, and cinematic landmark order during startup.

Useful interactive and deterministic checks are:

```sh
./build/moppe.app/Contents/MacOS/moppe --terrain-lab
tools/capture-cinematic /tmp/cinematic.mp4 18
ctest --test-dir build --output-on-failure
```

Trail tests cover deterministic bounded earthworks, connected graph and
material footprints, dry-water behavior, control-site reachability, circuit
continuity, and the trail-first cinematic.

## Current boundaries

- There is one authored leisure circuit, not a hierarchy of paths or a dense
  network.
- Grade is longitudinal. Cross-slope is reduced by benching but is not
  separately constrained or reported.
- Maximum grade can have reported exceptions when cut/fill limits cannot
  reconcile the terrain.
- Alpine avoidance is a strong preference, not an absolute elevation ban.
- The influence mask marks the complete planned path; the separate earthwork
  delta encodes the amount and sign of construction.
- Wet cells are excluded from shaping and influence stamping, so the system
  does not build bridges, causeways, or snow-specific trails.
- The route search is terrain-aware but does not yet simulate a motorcycle to
  score detailed ride dynamics such as suspension load, jump risk, or sight
  distance.

These boundaries are intentional enough to keep the first circuit legible,
but they also identify the natural extension points: explicit crossfall,
stronger guarantees around local grade exceptions, a dedicated ribbon shader
that makes fuller use of its along/across coordinates, a feature-local
collision surface, bridges, and multiple path classes.
