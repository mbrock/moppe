# Terrain Lab spectacles

*Interactive demonstrations with a truth inside them.*

Terrain Lab can make the processes which shape the world visible, immediate,
and playful.  The best experiments should work twice: first as a spectacle
worth touching, then as an explanation which does not need a lecture.  These
are speculative directions rather than descriptions of implemented features.

## One drop: the hero's journey

### The raindrop

Click to release one drop, then watch it find its way to the sea.  Its trail
is orange where it scrapes material away and blue where it deposits it.

The lesson is simple: water follows slope, and it carries things.

Implemented: Terrain Lab's raindrop tool now places an ordinary droplet from
the hydraulic erosion model, commits its path-monotone erosion and deposition,
draws the signed material trail in world space, and follows the moving drop
with the inspection camera.  The palette can replace its temporary text
treatment independently.

### The mega-droplet

Make the meteorite idea physically honest: use the same droplet model, but
turn up the erosion constants and give it a broad brush.  Instead of shaving
off invisible fractions, a boulder-sized drop gouges a trench in real time,
slows in a valley, and lays down a broad alluvial fan.  Follow it with a chase
camera so the player rides just behind the canyon as it forms.

This makes erosion legible as an ordinary process repeated at enormous scale,
not as a mysterious terrain effect.

Likely reusable pieces are a droplet tracer, brush-scaled terrain commits,
live heightmap uploads, and a chase camera.  **Estimated effort: medium.**

### Raindrop race

Release three or five colored drops close together along a ridge crest and
follow their races.  Neighbors only a meter apart may end in different seas.
The apparently insignificant crest becomes an invisible border deciding the
fate of every drop.

This teaches drainage divides.  Once a tracer exists, the experiment is
mostly several simultaneous tracers and distinct trails.  **Estimated effort:
small.**

## Many drops: weather

### Make it rain

Hold a control to release a continuous shower: thousands of simulated drops
per second, live terrain updates, theatrical rain streaks, and a counter such
as `about 400 years of rain`.  Gullies organize themselves in front of the
player.  Nobody draws them; they emerge from the repeated local rule.

This is the existing erosion stage presented incrementally instead of hidden
behind a loading screen.  It needs per-frame batches, incremental commits, and
live heightmap uploads.  **Estimated effort: medium.**

## The water map: understanding the flow graph

### Watershed countries

Color every terrain cell by its eventual destination: a river mouth, lake, or
other sink.  The map becomes a collection of colored countries whose borders
follow ridgelines.  Let the player click any two points and ask whether they
belong to the same country.

This reveals the flow graph without having to name it.  The drainage analysis
already carries much of the required destination information; the main new
piece would be an overlay and palette.  **Estimated effort: small.**

### The pulse

Light up the river network and send luminous pulses down all its reaches at
once.  Fine capillaries join larger arteries; tributary pulses combine; falls
flash as the wave drops over them.

The display makes accumulation and tributaries visible: many small flows
become one large flow.  It can build on extracted river reaches, waterfalls,
and animated geometry along known polylines.  **Estimated effort:
small to medium.**

## Deep time

### The time scrubber

Drag through retained pipeline checkpoints, from newborn lumps to aged
valleys.  The player should be able to move in both directions and compare
stages directly rather than watching a single canned transformation.

The checkpoints already exist.  **Estimated effort: small.**

### The ghost mountain

Render the pre-erosion terrain as a translucent ghost suspended above the
present terrain.  The gap shows the volume which disappeared from the ridges.
Following the valley reveals where some of it returned as fans and flats.

Paired with the time scrubber, this turns conservation into a spatial object:
mountains do not merely shrink; their material relocates.  The old heights can
come from the initial checkpoint, with a second alpha-blended terrain pass.
**Estimated effort: medium.**

## Meddling: the sandbox instinct

### Build a dam

Give the player a brush which raises a wall, then rerun flood analysis.  A lake
swells behind the dam, the downstream river thins, and sufficiently high water
overtops or finds a new route.

The interaction teaches that water seeks a level and turns local obstruction
into consequences across a basin.  It needs terrain editing, repeated flood
analysis, and live rebuilding of water surfaces.  **Estimated effort: medium.**

### Sea-level slider

Raise the ocean and watch valleys become fjords, hills become islands, and
rivers shorten.  Lower it to reveal land bridges and extended drainage
courses.  Coastlines become visibly contingent on one number.

The water level is already a parameter; the experience needs responsive flood
analysis and water-surface rebuilding.  **Estimated effort: small to medium.**

### Waterfall tour

Add a postcard control which flies between the world's best waterfall,
largest lake, widest river mouth, strongest confluence, and other exceptional
features.  Existing feature-selection machinery used by water captures could
provide the destinations; the visible addition is a pleasing camera flight.

This teaches that a generated world contains particular places, not merely
terrain categories.  **Estimated effort: small.**

## A useful starting trio

Three experiments span spectacle, explanation, and process while sharing
their machinery:

1. **Mega-droplet with chase camera:** the immediate spectacle and the reason
   to build tracing and live terrain updates.
2. **Watershed countries:** a high ratio of insight to machinery, centered on
   one strong overlay.
3. **Make it rain:** the erosion controls transformed into recognizable
   weather and emergent change.

The tracer can support the single drop, mega-droplet, and raindrop race.  The
incremental commit path can support both the mega-droplet and rain.  Overlay
work begun for watershed countries can become a visual language for later
hydrology experiments.  Together they give Terrain Lab a compact thesis:
watch one cause, understand the whole system, then touch the process yourself.
