# Terrain as Structure

*An ideas document on giving the mesh meaning: Morse–Smale complexes, closed worlds, connectedness, and tunnels.*

## The provocation

Character modelers on YouTube teach "good topology" as a craft discipline: quads, edge loops, pole placement. What they're really teaching is that a mesh is not a shape — it's a program. The vertices carry geometry, but the connectivity determines how the geometry *behaves* under subdivision and deformation. Edge loops around a shoulder are the axes along which the mesh will fold; the topology encodes anatomical knowledge. Two meshes can be geometrically identical and one animates beautifully while the other collapses, purely on connectivity.

Terrain, as usually practiced, is the degenerate case of this idea. A heightfield on a regular grid is a mesh whose topology carries no information at all: every vertex valence four, every quad the same quad, all meaning packed into one scalar per vertex. The connectivity is pure scaffolding. And yet a landscape *has* an anatomy — ridgelines, drainage networks, passes — which the grid samples right past, hitting the ridge crest at whatever phase it happens to hit it.

The proposal: stop treating the mesh as something generated in the last stage. Let the landscape's own structure be a first-class citizen from simulation through rendering.

## Four entities

The phrase "the terrain" smears together at least four distinct things. Naming them dissolves most of the confusion.

1. **The landscape itself** — a continuous surface, the mathematical ideal. Nobody ever holds it in their hands.
2. **The sample lattice** — the heightmap grid. A measurement apparatus: a way of pinning the continuous thing down at regular intervals so you can do arithmetic on it.
3. **The structure** — the Morse–Smale complex, the drainage forest, the ridge graph. A combinatorial object, finite and discrete, that captures what the landscape *is about*.
4. **The render mesh** — triangles handed to the GPU. A performance obligation, nothing more.

The standard pipeline's sin is conflating 1 and 2, then deriving 4 directly from 2, while 3 never gets built at all. The heightmap gets treated as if it *were* the landscape, when it's one anonymous sampling of it — anonymous in that its vertices fall where the grid says, indifferent to where the landscape's distinguished points are.

**Reframe:** the structure is the signal; the lattice is a channel it passed through. Simulation runs on the lattice for the same reason physics runs on instruments — discretization is how you compute. But the output of the computation isn't the grid of numbers; it's what the grid of numbers *witnesses*. Extracting the structure is not post-processing, it's the **readout**. Everything downstream — rendering, LOD, gameplay, ecology — consumes the readout, consulting raw samples only for local detail within a cell whose identity is already known.

## The Morse–Smale complex

The structure has a canonical definition. Take the heightfield as a Morse function. Its critical points are peaks (maxima), pits (minima), and passes (saddles). Trace steepest-ascent and steepest-descent lines out of each saddle: the ascending separatrices (saddle → peak) are the ridgelines; the descending ones (saddle → pit) are the valley lines. This is not a heuristic edge detector — it's a theorem-backed decomposition. The separatrices partition the entire terrain into cells, and the whole thing forms a graph whose nodes are critical points and whose edges are ridges and valleys.

The delightful bonus: the cells of a Morse–Smale complex are **quadrangular**. Each cell has one peak, one pit, and two saddles on its boundary. The terrain's own anatomy hands you a quad layout — the retopology the landscape would choose for itself, with the poles landing exactly at the critical points. This is the character-modeling insight transposed: good topology means the parametrization aligns with the anatomy, and here the anatomy computes its own parametrization.

Drainage fits the same frame from the other side. The river network is a forest of trees embedded in the valley-line structure; Horton–Strahler ordering gives trunk-versus-tributary hierarchy for the valleys, as persistence (below) gives it for the ridges.

## Persistence as governance

Raw simulation output is noisy: thousands of spurious little peaks and saddles. Persistence pairs each peak with the saddle that "kills" it as you flood the terrain from below; the height difference of the pair says how much that feature matters. Cancelling low-persistence pairs simplifies the complex gracefully — minor bumps dissolve into their parent slopes while the main ridgeline survives. It's a principled answer to "which ridges are real."

**Working rule:** fix a persistence threshold and treat the *simplified* complex as canonical. Structure above the threshold; texture below it. Sub-threshold wiggles live entirely in the sample layer. This is the clean way to demote the lattice without pretending you can discard it.

## Structural coordinates

The structure gives a parametrization, not just an annotation. Within a Morse–Smale cell you can lay coordinates that run "along the ridge" and "down the slope," so that position on the terrain becomes *(which cell, where in the cell)* rather than *(x, y)*.

Consequences:

- A shader knows it's near a ridgeline not by detecting curvature per frame but because its coordinate says so. *Rendering a ridge should be different from rendering a valley* — the analysis has already been done, once, and its verdict is addressable.
- A trail can be specified as "descend this valley edge of the graph." Paths become graph citations.
- Detail synthesis can be anisotropic in structural coordinates: striations along the slope direction, talus accumulating toward the pit corner, scree below the saddle.
- LOD can preserve the spine (high-persistence separatrices, silhouette edges) while freely decimating cell interiors.

## Two change disciplines

The representations differ in *how they change*, and that's a feature. The lattice changes every simulation tick, continuously, boringly. The structure changes only at discrete events: a saddle–peak pair annihilates when erosion wears a spur down; a river captures a neighbor's drainage; a lake overtops its sill. These events are rare and **narratable** — they are the moments when the landscape's story advances.

Kept live during simulation, geomorphology becomes legible as a sequence of graph rewrites rather than a movie of a heightfield. The world's history is a changelog of its structure. (This is "the world becomes believable when it remembers," applied to the ground itself: the structure *is* the memory of the erosion.)

## The closed world

The structural view *requires* a closed world to be well-defined. Morse theory is global: peaks − saddles + pits = χ, the Euler characteristic — 2 on a sphere, 0 on a torus. That's a conservation law for landmarks, a budget the geography must balance. On an infinite plane there is no such equation; the critical points are an unaccountable open-ended inventory, and "the drainage structure of this world" is a question the world never finishes being asked.

Hydrology makes the same complaint physically. On an unbounded plane, where do rivers go? Infinite terrain either invents an implicit sea level or lets water fall off the edge of the computed region. On a sphere or torus, drainage is conservative: every basin bottoms out somewhere, every drop is in the ledger, and water and sediment budgets *balance*. The closed world is honest in a bookkeeping sense; the infinite world runs on undeclared imports.

(The torus is what a wrapping heightmap already was — periodic boundaries are tileable noise wearing a trench coat. The choice is between admitting the torus and pretending it's a plane. The sphere is the more expensive honesty: no single chart covers it, and the parametrization poles of the cube-sphere echo the valence poles of the modeler. It pays back with genuine geography — antipodes, a farthest place, a finite area you can quote.)

Infinite procedural terrain is lazily evaluated: it exists only where a player has looked, so it isn't a *place*, it's a function yielding place-like material on demand. Nothing about it can be globally true. A finite closed world can be fully witnessed — complete ridge graph, all rivers named, *the* highest mountain a definite description. Smallness with completeness beats endlessness without identity.

## Connectedness

Connectedness is a property so basic it's easy to forget it could fail. On a closed connected surface it upgrades to path-connectedness with finite diameter: everything is within reach of everything, and a number bounds how far. "You can get there from here" is a theorem.

The structural layer inherits it: if the surface is connected, the Morse–Smale graph is connected — the ridge network is one network, every peak joined to every other by some chain of ridges and saddles. Watersheds are the counterpoint: drainage *partitions* the connected world into basins, but the partition boundaries are exactly the ridgelines, so division and connection are the same object seen from opposite sides. The divide is what you cross; passes matter because they are the minimal saddles where two basins communicate. (This is also where the partition-logic thread plugs in: the watershed labeling as inverse-image fibers, saddle merges as coarsening — the drainage partition and the ridge graph are dual presentations of one structure.)

Design corollary: fast travel quietly restores disconnected level-topology by making the path irrelevant. Honoring connectedness means the path is always part of the fact.

## Tunnels and bridges

A tunnel seems to break the heightmap, but the heightmap remains true. The heightmap describes a **surface** — the graph of a function — while the world it implies is a **solid**: everything below the surface is rock. The tunnel doesn't contradict the function; it amends the solid. Elevation stays true at every (x, y). What changed is a claim the heightmap never explicitly made: "below the surface is filled." So: heightfield as base assertion, plus a short list of **carvings** as amendments. The lattice stays innocent.

Topologically: take the terrain surface as the boundary of the solid. Drilling a tunnel adds a **handle** — genus +1; the sphere-world becomes, surface-wise, a torus. A bridge is a handle *added to* the solid where a tunnel is a handle *drilled through* it; the boundary surface can't tell the difference. Both buy a new class of path, both cost one handle, and the Euler landmark budget shifts by exactly 2 per handle.

But the tunnel really *lives* in the graph. Geometrically it barely exists — two portal disks and a swept tube, plausibly the straight dynamited segment from A to B. Its content is an **edge**: a new arc in the path structure that violently disagrees with the surface metric. Before the tunnel, crossing the range meant climbing to a saddle — the pass as the ridge graph's toll gate. The tunnel is an *artificial saddle* at an elevation the geology never offered: humanity editing the Morse–Smale complex directly. Metrically tiny, topologically loud — which is exactly why tunnels feel the way they do: nothing about the landscape changed, yet everything about distance did.

(This slots straight into the earthworks/second-author frame: cut-and-fill edits the function; tunnels and bridges edit the topology. Two escalating tiers of the Regional Association's powers.)

## Architecture sketch

- **Lattice layer:** the simulation substrate. Uplift/erosion, drainage routing, all the field algebra. Ticks continuously.
- **Structure layer:** persistence-simplified Morse–Smale complex + drainage forest, kept live; changes by discrete, narratable rewrite events; carries the landmark budget (Euler), the basin partition, and any artificial edges (tunnels, bridges, trails-as-graph-citations).
- **Parametrization layer:** structural coordinates per cell; the address system rendering and gameplay actually consult.
- **Realization layer:** render meshes, shaders, LOD — derived, disposable, and *informed* (a ridge shader that knows it is on a ridge).

Inverse authoring is the same architecture run backwards: sketch the ridge/river graph, solve for a landscape consistent with it. Graph as source code, lattice as compilation target.

## Open questions

- Incremental Morse–Smale maintenance under a running erosion sim: recompute per epoch, or genuinely incremental updates keyed to critical-point events?
- The right persistence threshold — fixed, scale-dependent, or itself a designed quantity (what counts as a "real" ridge is a worldbuilding decision)?
- How structural coordinates interact with the hex-cosmos strand: the Morse–Smale complex is itself a cellular complex draped on the terrain — is it the natural bridge between the heightmap world and the "world as cellular complex" cosmology (combinatorial source of truth, geometry as embedding)?
- Rivers as structure: the valley-line forest is where a tree-shaped river complex would naturally root.
- Does the landmark graph want names? A finite closed world with a complete, surveyable structure is an atlas waiting to be written.
