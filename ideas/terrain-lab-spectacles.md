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

Historical note: an earlier Terrain Lab implemented this with the retired
hydraulic droplet model. A future version should trace the current drainage
graph without mutating terrain, so the lesson survives without reviving the
obsolete simulation.

### The mega-droplet

Make the meteorite idea physically honest with a purpose-built interactive
erosion brush. Instead of shaving off invisible fractions, a boulder-sized
flow gouges a trench in real time, slows in a valley, and lays down a broad
alluvial fan. Follow it with a chase camera so the player rides just behind
the canyon as it forms.

This makes erosion legible as an ordinary process repeated at enormous scale,
not as a mysterious terrain effect.

Likely reusable pieces are a drainage tracer, brush-scaled terrain commits,
live heightmap uploads, and a chase camera. **Estimated effort: large.**

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

This now needs a new process model rather than exposing an existing stage;
orogeny is deliberately not a rain-particle simulation. **Estimated effort:
large.**

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

## Further wings of the museum

### The birth of a world

Do not hide generation behind a loading screen. Play the pipeline as a cold
open: bare rock rises from a blank sea, rain works it for impossible years in
seconds, valleys deepen, the ocean floods in, and the first rivers appear.
The camera then comes to rest at the eventual spawn point.

This is the time scrubber and incremental rain presented as a short authored
sequence. It gives generation a beginning, middle, and arrival without
pretending that the world was made by hand. **Estimated effort: medium.**

### The great migration

Release a point of light from every cell at once. The lights hurry downhill,
merge into brighter strands, pool in lakes, and eventually stream out to sea.
For one breath, the entire drainage graph is visible without arrows or a
legend.

Unlike the pulse, which travels along already-legible reaches, this begins
with the whole land. It is a beautiful way to reveal that every river is the
sum of innumerable tiny decisions. **Estimated effort: medium.**

### Field guide, hunt, and atlas

Click a feature for a small card naming it and explaining what happened there:
a confluence, alluvial fan, river mouth, saddle, or ridge. A checklist of
discoveries turns the guide into a gentle hunt through one particular
landscape. Flip into an atlas view for contours, hillshade, watercourses, and
sparse generated names for peaks, bays, and rivers.

Moving between map and landscape turns cartography into a way of seeing the
same place. Labels must be grounded in the generated world, and uncertain
classifications should read as observations rather than false authority.
**Estimated effort: medium.**

### Documentary mode

Turn the waterfall tour into a quiet film. The camera visits exceptional
places while captions tell computed, world-specific facts: how much land a
river drains, how far a fall descends, or where a lake sits in its basin.
Optional spoken narration would make the world tell its own story.

The rule is that every claim must be derived from the world, never generic
copy with a number pasted in. **Estimated effort: medium.**

### Two sculptors: glacier and volcano

Let a valley receive two kinds of change. A slow, broad glacier grinds a river
V into a U, then melts to leave a small river wandering its inherited floor.
Elsewhere, a downhill-moving lava source deposits material, raising a cone
that redirects later flows. Turn on rain and watch drainage reorganize around
the new mountain.

Both experiments make landform rules visible: different erosion signatures in
the first, deposition as erosion's mirror in the second. **Estimated effort:
medium.**

### Small interventions, large consequences

Tap an over-steep hillside and let a local thermal pass rattle material into a
scree fan. Raise a mountain with a brush, then make it rain: gullies gather,
an old course may be captured, and a basin may become a lake. The sea-level
slider completes the set, turning coasts into fjords or land bridges.

The important feeling is not unlimited sculpting; it is that the terrain
explains the consequence of a small intervention. **Estimated effort: medium.**

### Ride the river, then leave a message

Choose a drainage path in the lab, then hand it to the game: place the rider
at its headwaters and glow the route to the sea. A small named object dropped
into that stream could later appear at its corresponding mouth or shore.

The object need not be a perfect fluid simulation. What matters is quiet
continuity between studying a world and inhabiting it. **Estimated effort:
medium.**

### Theater and artifacts

Tilt-shift treatment can make an orbit view read as a museum diorama: a narrow
in-focus band with softened foreground and distance. At the other extreme,
export a watertight, based heightfield mesh so the player can print a small
physical island.

The former is explicitly a theatrical mode, not a scientific view. The latter
needs clear choices about scale, base thickness, and water. **Estimated effort:
small to medium.**

## The long horizon

### The world ages while you play

Let an almost imperceptible amount of erosion continue during play. A gully is
a little deeper after a long ride; a creek has moved without asking to be
watched. The change may be too small to notice, but it would be true.

Persistence, reproducibility, and the cost of updating a living world make
this a systems project rather than a lab flourish. **Estimated effort: high.**

### Powers of ten and a civilization

One continuous camera gesture could travel from handlebar height through the
orbit view to the whole world suspended in space. Farther out, settlements
could respond to the same terrain evidence: homes favour flat, wet ground;
paths choose valleys; bridges look for narrow crossings.

Both extend the lab's premise into new systems: difficult projection and
representation changes for the first, social simulation for the second.
**Estimated effort: high.**

## A useful starting trio

Three experiments span spectacle, explanation, and process while sharing
their machinery:

1. **The great migration:** the clearest large-scale spectacle and a visual
   language for flow.
2. **Field guide and scavenger hunt:** knowledge made playful, rooted in
   specific computed places.
3. **The birth of a world:** generation turned into the game's first story.

The tracer can support the single drop, mega-droplet, raindrop race, and ride
the river. The incremental commit path can support both the mega-droplet,
rain, and the birth sequence. Feature work begun for the field guide can also
feed the documentary and atlas. Together they give Terrain Lab a compact
thesis: watch one cause, understand the whole system, then touch the process
yourself.
