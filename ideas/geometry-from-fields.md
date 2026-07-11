# Geometry from fields

A proposal for the next tier of visual richness: gravel, kicked-up dirt,
full-geometry grass, living water, and terrain detail — built not as
authored assets but as GPU-evaluated functions over the field data the
world generator already produces. Solid wins first, speculative ideas
after. Nothing here requires heroic effort; most items are one focused
session, a few are two or three.

## The premise

Moppe's world is already a set of rasters before it is a set of meshes:
heights, normals, standing-water levels, drainage direction and
contributing area, slope, water-body identity and class, carved channel
beds, waterfall lips and feet. The renderer's best recent moments came
from *reading those fields directly on the GPU* — terrain vertex-pulls
the height texture, the water grid pulls levels and per-body wave
amplitude, and surfaces that share source data cannot disagree with each
other.

The proposal is to finish that thought: treat small-scale geometry —
grass blades, pebbles, debris, spray, reeds, waterline bands — as
**functions evaluated per frame over the field textures**, not as
buffers we build and manage. The hardware for this is instancing (works
everywhere), tessellation (works everywhere, including the iPhone
target), and Metal mesh shaders (Apple silicon / A14+, and the dev
machine speaks Metal 4). The general pattern:

- an *object stage* (or the CPU, in the instanced version) walks world
  tiles, culls them, and assigns a detail budget;
- a *mesh stage* (or instanced vertex shader) turns indices into
  geometry: hash for individuality, field textures for placement and
  character, uniforms for time, wind, and the bike.

Geometry generated this way costs no memory, never desynchronizes from
the terrain, and can vary every frame — count, shape, and topology
included — because nothing is stored to become stale.

One shared piece of infrastructure serves nearly everything below: a
**field atlas** texture (or a couple of them) packing per-cell control
data the fragment/vertex stages want. First candidates: moisture
(derived from distance-to-water and contributing area), flow direction
and speed for wet cells, material weights, and a small dynamic
**wear/wetness** texture the game writes into at the wheels. Most rows
below read two or three channels of this atlas.

## Solid wins

### 1. Full-geometry grass driven by hydrology (M)

The hexaquo/Ghost-of-Tsushima recipe: a ~12-vertex blade, tens of
thousands of instances in rings around the camera, tip-bend by
`pow(h,2)`, ambient-occlusion gradient from root to tip, normals blended
straight-up at the tips for specular, patch-noise clumping. Every
ingredient is already in the codebase in some form: deterministic hash
placement (`vegetation.cc` near-grass), wind (`moppe_wind`), patch noise
(`moppe_value_noise`), and root placement by sampling the height texture
in the vertex shader — blades glued to terrain by construction.

The Moppe twist: clump, height, and color come from the **moisture
field**, not arbitrary noise. Lush tall green along rivers and shores,
sparse yellow on dry ridges, reeds in the wet margins. The ecology
becomes readable from the saddle, and it is the hydrology's own shape.

Instanced first; the mesh-shader port (per-blade topology LOD,
tile-budget amplification, horizon-scale fields) is a contained upgrade
that reuses all the shading and placement math. (Mesh-shader port: L,
optional, later.)

### 2. Wheel debris: dirt, stones, and grass clippings (S/M)

The dust system already emits camera-facing sprites at the wheels. The
upgrade is *material-aware solid debris*: sample the terrain material at
the contact point (the same blend weights the terrain shader uses —
grass, dirt, rock, snow) and the wear texture, then emit a handful of
small instanced tetrahedra/clods with real tumbling rotation, ballistic
arcs, and a bounce. Drifting on dirt kicks brown clods and a dust sheet;
on gravel/rock, darker chips with a sharper rattle of specular; on
grass, green clippings that flutter (higher drag, lower mass); in
shallow water, the existing spray. A dozen live pieces per wheel reads
as violence; these are trivially instanced meshes with a per-piece hash,
no mesh shaders required.

### 3. Riverbed pebbles and wet banks (S/M)

Now that river water is transparent, the single highest-value close-up
upgrade is what's *under* it. Parallax-occlusion-map a pebble layer onto
terrain cells inside carved channels (a small procedural height/normal
tile is enough; 8–16 march steps, faded out beyond ~20 m). Add a
**wet-darkening band** where `|ground − water_level|` is small: darker,
slightly specular banks and shore stones. Together these give "mountain
stream" at a glance for fragment-shader money. Depth-tinted pebbles
shifting behind the refraction wobble is half the charm of real shallow
water.

### 4. Water on the terrain lattice (M) — then mesh-shader water (L)

Already planned; restated here because it is the water half of this
whole program. Build river and lake surfaces from the terrain's own
sample lattice, vertex-pulling the same height and water textures, with
the waterline traced through the cells where level crosses ground.
Kills the hover/tear/sawtooth artifact class permanently and unifies
rivers with the paused fine-shoreline-band project. The later
mesh-shader version walks wet tiles in the object stage and emits
water only where water exists — retiring the 300² ocean grid that
mostly rasterizes dry land today.

### 5. Near-camera terrain tessellation (M)

Metal tessellation with factors from projected edge length; bicubic
height sampling plus one octave of detail noise so new vertices land on
a smooth surface instead of popping. Banks, channel lips, ridgelines,
and shorelines gain curvature within ~50 m of the bike; physics stays
on the authoritative grid. Pairs naturally with the POM gravel above —
tessellation carries the metre scale, parallax the centimetre scale.

### 6. Waterfall sheets and plunge pools (M)

The drainage analysis already exports every waterfall's lip cell, foot
cell, drop, and slope. Emit a curved falling sheet between lip and foot
(a mesh-shader ribbon or a small CPU mesh — a dozen cross-sections bent
by a parabola), foam-textured with the existing advected noise, plus a
spray particle burst and a ring of agitated water at the foot. The
inspection cameras make every candidate reviewable one by one. This
retires the "steep terrain-following ribbon" placeholder with the
single most photogenic feature the hydrology finds.

## Speculative and novel

### 7. Desire paths: the world remembers riding (M, novel)

A persistent world-space **wear texture**: every wheel contact splats a
small stamp; the field decays over minutes. Grass blades read it and
flatten/shorten (they already read moisture — one more channel); the
terrain shader darkens repeated lines into visible dirt tracks. Ride
the same ridge three times and there is a path there — your path. This
is the Helbing active-walker trail model from the research shelf
(`research/routes/`) arriving in the renderer through a texture rather
than a simulation, and it connects directly to the second-author ideas
about paths and world memory. Later, the same field could feed back
into gameplay (packed dirt = more grip), making trails
self-reinforcing exactly as in the trail-formation literature.

### 8. Storms that fill the drainage network (M/L, novel)

The wet drainage graph knows every runnel, including the thousands
below the visible-river threshold. During rain, sweep a "saturation"
parameter upward and let sub-threshold channels progressively awaken as
ephemeral streams — glassy threads exactly where the carved runnels
already are — then drain away after the storm, lingering longest in
the highest-area cells. Puddle classification (census already grades
puddle/pond/lake) gives rain puddles in hollows for free. No new
simulation: it is a *display* of the existing graph, animated by one
scalar. Weather becomes a lesson in how the landscape drains.

### 9. Scree slopes and talus aprons (S/M)

Thermal erosion already encodes the talus angle. Where slope sits near
it (and moisture is low), scatter instanced angular stones — density by
slope excess, size by a hash, orientation tumbled downhill. Cliff bases
grow aprons of rubble; riding one should sound and feel loose (ties
into the debris system's material sampling). A purely read-only use of
fields we already compute.

### 10. Sediment plumes at mouths (S, novel)

Each river mouth knows its discharge. Tint the receiving water body
with a fan of sediment color — a milky-brown lobe fading over a few
hundred meters, oriented by the mouth's flow direction, subtly animated
by the value noise. One texture splat per mouth into the water color
path. Estuaries stop being a hard material boundary and start telling
the story of what the river carried — which, given the erosion
pipeline, is literally true.

### 11. Valley mist sheets and morning pools (M, speculative)

Fog already has a valley-mist term by altitude. The drainage basins
know where cold air would pool: low contributing-area hollows and lake
surfaces. At dawn (sun-height uniform), emit a few large translucent
mist planes over basin floors and calm water, drifting with the wind
function. Cheap, enormous mood; the hydrology decides *where*, which is
what keeps it from looking like a screen-space filter.

### 12. Life as field decoration (S each, speculative)

Motes over the world, spawned by the same tile-walk machinery:
mayflies and glints over calm water at dusk (body class + wind), pollen
drifting off dry grass in gusts (moisture low + wind high), fireflies
in wet meadows at night. Each is a dozen lines of emission logic over
fields that already exist, and each makes a biome legible at a glance.

## Suggested order

1. Water on the lattice (4) — closes the current artifact class.
2. Grass (1) + wheel debris (2) — the near-field, where the rider looks.
3. Pebbles/wet banks (3) + tessellation (5) — the ground itself.
4. Waterfall sheets (6) — the postcard shot.
5. Desire paths (7) and storm drainage (8) — the novel identity pieces.
6. Mesh-shader ports (grass, then water) once the instanced/CPU versions
   have settled the look and the tile-walking skeleton is worth building
   once for all three.

The common thread throughout: the generator's fields are not just
inputs to a heightmap — they are the world's self-description, and
every system that reads them directly, on the GPU, gets correctness
(nothing floats, nothing disagrees) and character (everything visibly
grows from the same geology) at the same time.
