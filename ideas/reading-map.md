# A reading map for inhabited procedural worlds

*Papers, books, talks, tools, and playable arguments around Moppe's longer
roads.*

## How to use this map

This is not a literature review and certainly not a syllabus.  It is a map of
nearby intellectual country: work that might sharpen Moppe's terrain system,
its paths and riding, and the more speculative idea of a game about tending an
inhabited world.

The categories overlap.  Christopher Alexander leads toward graph theory;
graph theory leads toward cellular meshes; meshes lead toward Townscaper;
Townscaper leads toward mixed-initiative creation; a motocross jump leads from
level design into optimal control.  Those crossings are part of the point.

Entries marked **begin here** are especially good first encounters.  A paper
is not included merely because its method should be implemented.  Some are
useful as vocabulary, some as provocations, and some because they expose a
productive difference between their problem and ours.

Links were checked in July 2026.  Where a stable DOI or author-hosted copy was
available, it is preferred over an aggregator; books without a useful open
edition are named without pretending that a product page is a scholarly
source.

## A short path through the collection

For one compact route, begin with these:

1. Christopher Alexander's [*A City Is Not a Tree*][city-not-tree] for the
   difference between a living overlap of systems and a tidy hierarchy.
2. Alexander's [*Harmony-Seeking Computations*][harmony] for the thought that
   generation can be a sequence of value-sensitive transformations.
3. Bin Jiang's [*Wholeness as a Hierarchical Graph*][wholeness-graph] for one
   serious attempt to operationalize centers.
4. Genevaux et al.'s [hydrology-based terrain generation][hydrology-terrain]
   for terrain whose rivers are structural inputs rather than decoration.
5. Smith, Cha, and Whitehead's [platform-level design paper][platform-design]
   for rhythm, paths, and physics-aware difficulty.
6. Liapis, Smith, and Shaker on [mixed-initiative content
   creation][mixed-initiative] for making the generator a collaborator.
7. Oskar Stalberg's [Townscaper talk][townscaper-talk] for the pleasure and
   leverage of a forgiving cellular construction language.
8. Amit Patel's [Mapgen4][mapgen4] for an unusually lucid example of fields,
   graphs, hydrology, rendering, and direct manipulation meeting in one tool.

## 1. Alexander: centers, wholeness, and unfolding

This is the conceptual center of the collection.  Alexander's strongest claim
is not that good places contain a checklist of motifs.  It is that a whole has
an objective, recursively organized structure; that making should respond to
the whole already present; and that successful transformations differentiate
and strengthen it without destroying it.

- **Begin here:** Christopher Alexander,
  [*A City Is Not a Tree*][city-not-tree].  Short, forceful, and foundational:
  real cities are overlapping semilattices, not cleanly partitioned trees.
  This is immediately relevant to places that simultaneously belong to a
  watershed, route, settlement, view, and remembered ride.
- Christopher Alexander et al., *A Pattern Language*.  Best read as a large
  collection of connected observations about human situations, not as an
  asset catalogue or architectural style guide.
- Christopher Alexander, *The Timeless Way of Building*.  The most welcoming
  account of the quality he was trying to name and of design as participation
  in an unfolding order.
- Christopher Alexander, *The Nature of Order*, Books 1--4.  The full
  statement: centers, the fifteen properties, structure-preserving
  transformations, generative process, and the relation between geometry and
  feeling.  Books 1 and 2 are the closest to the current Moppe ideas.
- **Begin here:** Christopher Alexander,
  [*Harmony-Seeking Computations*][harmony].  A concise bridge from the later
  architectural theory to computation.  The provocative question for Moppe
  is whether each terrain or construction operation can be judged as a change
  to an existing whole, rather than as production from an empty specification.
- Christopher Alexander,
  [*The Origins of Pattern Theory*][origins-pattern].  Alexander's own account
  of what software culture inherited from patterns, and what it largely left
  behind: concern for the moral and geometric quality of the resulting whole.
- Nikos Salingaros, [*The Structure of Pattern Languages*][salingaros].  A
  compact attempt to describe how patterns connect across scale and why a
  language is more than a bag of reusable solutions.
- Michael Mehaffy,
  [reassessing Alexander's theory of urban morphogenesis][morphogenesis].  A
  recent overview that places centers and structure-preserving transformation
  in relation to contemporary complexity and urban thought.
- Helmut Leitner, [*Pattern Theory*][leitner].  A broad, sympathetic guide to
  Alexander's work and its extensions.  Useful when the four-volume primary
  source feels too large to enter at once.

## 2. Operationalizing centers and living structure

These works take the dangerous but interesting next step: turning wholeness
into graphs, measures, algorithms, or computational judgments.  They should
not be treated as proof that beauty has been solved.  Their value is that they
make propositions precise enough to argue with and experiment upon.

- **Begin here:** Bin Jiang,
  [*Wholeness as a Hierarchical Graph to Capture the Nature of
  Space*][wholeness-graph].  Centers become nodes; support among centers
  becomes directed links; recursive importance becomes a graph property.
- Bin Jiang, [*A Complex-Network Perspective on Alexander's
  Wholeness*][complex-wholeness].  Develops differentiation and adaptation as
  two principal structure-preserving transformations.
- Bin Jiang, [*Living Structure Down to Earth and Up to
  Heaven*][living-structure].  A readable defense and explanation of living
  structure as a mathematical and physical idea rather than a stylistic one.
- Bin Jiang and Chris de Rijke, [*Structural Beauty*][structural-beauty].
  Connects perceived beauty with recursively nested scaling structure.  It is
  directly relevant to a possible Beautimeter, but also worth resisting:
  topology may be a contributor to beauty without exhausting it.
- Bin Jiang and Chris de Rijke, [*Living Images*][living-images].  Applies the
  same line of thought to images and offers a useful precedent for comparing
  alternative visual wholes.
- Bin Jiang, [*Beautimeter*][beautimeter].  The deliberately bold experiment:
  use a multimodal language model to compare degrees of living structure.
  More interesting as a critic or pairwise conversation partner than as an
  oracle that returns a scalar reward.
- Bin Jiang,
  [*Geography Founded on the Third View of Space*][third-space].  Contrasts
  absolute and relational accounts with an organismic view in which space is
  constituted by nested living structures.  This most directly meets Moppe's
  distinction between metric carrier space and articulated space.
- Gary Black and Bin Jiang,
  [*Alexander's Wholeness as the Scientific Foundation of Sustainable Urban
  Design and Planning*][sustainable-wholeness].  A concise synthesis of the
  graph account and its intended design consequences.
- Stephen Marshall,
  [*Street Network Studies: From Networks to Models and Their
  Representations*][street-networks].  Not Alexandrian in the same sense, but
  valuable for understanding what is gained and lost when lived streets
  become graph models.

## 3. Space as relation, topology, and overlapping centers

The cellular tissue proposed for Moppe needs more than coordinates.  These
references offer languages for adjacency, enclosure, reachability,
centrality, boundaries, and overlapping organization.  They are useful even
when their scientific ambitions or architectural conclusions differ from
Alexander's.

- Bill Hillier and Julienne Hanson, *The Social Logic of Space*.  The
  foundation of space syntax: represent spatial configuration in ways that
  allow relations among rooms, streets, movement, and social use to be
  studied.
- Bill Hillier, *Space Is the Machine*.  A more developed and freely
  [available account][space-machine] of configuration as an active component
  of architecture and cities.
- Alan Penn, [*Space Syntax and Spatial Cognition*][space-cognition].  A useful
  route into the relation between configurational descriptions and how people
  perceive and navigate environments.
- Stephen Marshall and Luca D'Acci,
  [*Bill Hillier, Christopher Alexander and the Representation of Urban
  Complexity*][hillier-alexander].  Particularly relevant because it compares
  two intellectual families that are often cited separately.
- Kevin Lynch, *The Image of the City*.  Paths, edges, districts, nodes, and
  landmarks are not a generative algorithm, but remain excellent primitives
  for asking whether a world is imaginable and memorable.
- James J. Gibson, *The Ecological Approach to Visual Perception*.  The source
  for affordances: what the environment offers a particular embodied animal.
  A slope is not simply steep; it affords ascent, flight, danger, concealment,
  or rest differently to different bodies.
- Tim Ingold, *Lines: A Brief History*.  A wide and beautiful meditation on
  walking, wayfaring, drawing, traces, threads, and the difference between a
  lived line and a connection between predetermined points.
- Christopher T. Allen,
  [*A Simple and Complete Discrete Exterior Calculus on General Polygonal
  Meshes*][polygonal-dec].  A technical glimpse of how continuous field-like
  quantities can be represented on irregular cells, edges, and vertices.
  This is mathematical infrastructure, not a theory of place, but the
  distinction is exactly why it belongs here.

## 4. Terrain as geology, hydrology, and an editable process

Moppe already works in this country.  The especially relevant papers do not
merely synthesize plausible noise.  They make ridges, drainage, uplift,
erosion, and user constraints part of a legible process.

- **Begin here:** Eric Galin et al.,
  [*Procedural Generation of Roads*][procedural-roads].  Roads are optimized
  over terrain using costs and geometric constraints, then converted into
  geometry.  It is a useful baseline for the road as least-cost object—and
  therefore a clear foil for roads that also arise from centers and history.
- **Begin here:** Jean-David Genevaux et al.,
  [*Terrain Generation Using Procedural Models Based on
  Hydrology*][hydrology-terrain].  River networks become primary modeling
  features from which terrain is constructed.  This inversion is deeply
  compatible with treating relations as prior to their material realization.
- Guillaume Cordonnier et al.,
  [*Large Scale Terrain Generation from Tectonic Uplift and Fluvial
  Erosion*][uplift-erosion].  Couples a drainage graph and uplift with
  landform synthesis; strong background for terrain with large-scale causal
  structure.
- Hugo Schott et al.,
  [*Large-Scale Terrain Authoring through Interactive Erosion
  Simulation*][interactive-erosion].  Particularly close to Terrain Lab:
  interactive control, fluvial consistency, inverse reconstruction, and
  blending of terrain patches.
- Ondrej Stava et al.,
  [*Interactive Terrain Modeling Using Hydraulic Erosion*][stava-erosion].
  An older but still illuminating example of erosion processes becoming
  responsive modeling tools rather than a final offline filter.
- Bedrich Benes et al.,
  [*A Survey of Methods for Terrain Synthesis*][terrain-survey].  A useful
  map of procedural, simulation-based, and example-based families.
- Eric Galin et al.,
  [*A Review of Digital Terrain Modeling*][terrain-review].  A broad modern
  review organized around landform variety, geomorphological and perceptual
  realism, scale, user control, and performance.  Its comparison criteria are
  nearly a research agenda for Terrain Lab.
- Hugo Lochner et al.,
  [*Interactive Authoring of Terrain Using Diffusion
  Models*][diffusion-terrain].  Sketch-conditioned terrain authoring with
  attention to drainage.  Worth reading less as a mandate for ML than as a
  contemporary example of negotiating user intent and geological plausibility.
- Amit Patel, [*Mapgen4*][mapgen4], with its
  [source repository][mapgen4-code].  An exceptionally transparent playable
  notebook: polygonal dual meshes, editable elevation, moisture, rivers,
  biomes, and rendering remain visibly connected.
- Sebastian Lague's
  [hydraulic erosion project and code][lague-erosion].  Not a research paper,
  but an approachable implementation companion when equations need to become
  an experiment.

## 5. Paths, roads, racing lines, and physically meaningful jumps

A path can be a topological connection, a piece of civil engineering, a
record of repeated motion, and a timed sequence of bodily demands.  This
cluster helps prevent the path generator from stopping at a pretty spline.

- **Begin here:** Gillian Smith, Mee Cha, and Jim Whitehead,
  [*A Framework for Analysis of 2D Platformer Levels*][platform-analysis] and
  [*Procedural Level Design for Platform Games*][platform-design].  These make
  rhythm, repetition, connectivity, paths, and a physics-based difficulty
  model explicit.  Replace the avatar's jump with the motorcycle's state and
  the questions remain remarkably current.
- Noor Shaker et al.,
  [*The 2010 Mario AI Championship: Level Generation
  Track*][mario-track].  A classic setting for generators whose output must be
  playable by an embodied agent, not merely satisfy tile adjacency.
- Nitin Kapania, John Subosits, and J. Christian Gerdes,
  [*A Sequential Two-Step Algorithm for Fast Generation of Vehicle Racing
  Trajectories*][racing-trajectories].  Optimizes speed profile and path under
  vehicle-dynamics and friction constraints.  Useful for evaluating a track
  from the moving body's side.
- Alexander Liniger, Alexander Domahidi, and Manfred Morari,
  [*Optimization-Based Autonomous Racing of 1:43 Scale RC Cars*][mpcc].
  Model-predictive contouring control links track geometry, feasible motion,
  and progress.  It suggests a practical automated rider-critic for generated
  segments.
- Jonathan Rowell and Denis Ulici,
  [*Procedural Content Generation for Racetracks*][racetrack-dissertation].
  A recent dissertation centered specifically on assembling, simulating, and
  evaluating generated tracks.  It is close enough to Moppe's problem to be
  read critically for definitions of character and fun.
- Ubisoft, [*Using Algorithms to Create Riders Republic's Trail
  Network*][riders-republic].  An industry account of large terrain, trail
  graphs, spline fitting, and designer iteration.  Especially useful as a
  production-scale comparison.
- Julian Togelius et al.,
  [*Search-Based Procedural Content Generation: A Taxonomy and
  Survey*][search-pcg].  The general recipe behind many track generators:
  choose a representation, mutate candidates, simulate or measure them, and
  search according to fitness.  The hard and creative part is the judgment.
- Mike Cook et al., [*Danesh: Helping Bridge the Gap Between Procedural
  Generators and Their Output*][danesh].  A reminder that understanding the
  expressive range and failure modes of a generator matters as much as
  producing more samples.
- Amit Patel, [*Introduction to A\**][astar].  A superb interactive account
  of cost-guided pathfinding.  For Moppe, the interesting extension is not the
  algorithm but the cost field: slope, soil, approach, views, existing traces,
  construction effort, and the quality of the resulting ride.

## 6. Procedural cities, buildings, and vernacular growth

The classic computer-graphics literature is exceptionally good at producing
large, coherent-looking artifacts and less consistently good at producing
places that feel historically necessary.  It offers powerful construction
languages while clarifying what an Alexandrian second author must add.

- Yoav Parish and Pascal Muller,
  [*Procedural Modeling of Cities*][procedural-cities].  The foundational
  L-system city paper: terrain and constraints guide street growth, parcels,
  and buildings.
- Pascal Muller et al.,
  [*Procedural Modeling of Buildings*][procedural-buildings].  Introduces CGA
  shape grammar and context-sensitive rules for detailed architectural
  shells.
- George Stiny, [*Introduction to Shape and Shape Grammars*][shape-grammar].
  Shape grammars operate directly on spatial forms rather than only symbolic
  strings.  They are a natural formal neighbor to pattern languages, though
  the two should not be conflated.
- Paul Merrell and Dinesh Manocha,
  [*Model Synthesis: A General Procedural Modeling
  Algorithm*][model-synthesis].  Generates large arrangements from local
  neighborhoods observed in an example.  This is an important ancestor of
  the family now commonly discussed as Wave Function Collapse.
- Jerry Talton et al.,
  [*Metropolis Procedural Modeling*][metropolis].  Optimizes grammar
  productions against high-level specifications.  Useful for the question:
  how can a powerful generative language remain controllable?
- Chen et al.,
  [*Interactive Procedural Street Modeling*][interactive-streets].  Combines
  tensor fields, user sketches, and procedural completion.  Its field-guided
  orientations are relevant to a locally directed cellular tissue.
- Stefan Greuter et al.,
  [*Real-Time Procedural Generation of “Pseudo Infinite”
  Cities*][infinite-cities].  A useful early systems paper on determinism,
  streaming, and generating urban structure only where it is needed.
- Citybound's [design notes and source][citybound].  An ambitious open-source
  city simulation whose experiments with road geometry, parcels, and emergent
  urban systems make good code-reading even where its goals differ from
  Moppe's.

## 7. Cellular tissues, irregular grids, and local compatibility

The grid is valuable here not because reality is square, but because a modest
discrete rhythm makes construction tractable.  These sources range from
practical tile systems to deeper mesh machinery.

- **Begin here:** Oskar Stalberg,
  [*Organic Towns from Square Tiles*][townscaper-talk].  The central practical
  reference: an irregular quad grid, hand-authored local pieces, and automatic
  contextual selection produce a construction toy that feels generous rather
  than technical.
- Tommy Thompson, [*How Townscaper Works*][townscaper-works].  A clear guided
  reconstruction of the lineage from marching squares and Wave Function
  Collapse to Townscaper.
- Boris the Brave, [*Townscaper Grid*][townscaper-grid].  Concrete algorithms
  for generating the characteristic irregular quad topology, with code in the
  broader [Sylves grid library][sylves].
- Maxim Gumin, [WaveFunctionCollapse][wfc].  The influential small repository
  that made local-constraint model synthesis widely accessible to game
  developers.
- Boris the Brave, [*Editable Wave Function Collapse*][editable-wfc].  More
  relevant than one-shot generation for Moppe: preserve most of an existing
  result while repairing the neighborhood of a human edit.
- Paul Merrell, [*Comparing Model Synthesis and Wave Function
  Collapse*][merrell-wfc].  Helpful historical and technical clarification of
  the relationship between the methods.
- David Bommes et al.,
  [*Mixed-Integer Quadrangulation*][miq].  A major reference for constructing
  quad meshes aligned to a direction field.  It is much heavier machinery
  than an initial prototype needs, but precisely articulates orientation,
  singularities, and globally coherent local grids.
- Jakob et al., [*Instant Field-Aligned Meshes*][instant-meshes], with
  [source][instant-meshes-code].  A practical route from local orientation and
  scale fields to triangle or quad-dominant meshes.
- Jonathan Richard Shewchuk,
  [*Delaunay Refinement Algorithms for Triangular Mesh
  Generation*][delaunay-refinement].  A lucid classic on adaptive meshing and
  quality.  Triangles may be the hidden computational substrate even when the
  visible or semantic cells are quad-like.
- Keenan Crane et al.,
  [*Digital Geometry Processing with Discrete Exterior
  Calculus*][dec-course].  A broad technical course on putting differential
  quantities on meshes.  Useful if water, circulation, curvature, or other
  fields eventually live directly on the cellular tissue.

## 8. Human and generator as collaborators

The deepest game-design proposal is that Terrain Lab and riding are not
separate activities.  In research language this touches mixed initiative,
co-creativity, interactive evolution, and experience-driven PCG.  Those terms
are narrower than “tending the world,” but they bring useful design questions:
who acts, who evaluates, how intent is communicated, and whether revision is
pleasant enough to become play.

- **Begin here:** Noor Shaker, Julian Togelius, and Mark Nelson,
  [*Procedural Content Generation in Games*][pcg-book].  The online book gives
  a common vocabulary.  Chapters on experience-driven PCG, mixed initiative,
  representations, and evaluation are closest to this project.
- **Begin here:** Antonios Liapis, Gillian Smith, and Noor Shaker,
  [*Mixed-Initiative Content Creation*][mixed-initiative].  A compact map of
  systems in which human and generator both have agency.
- Michael Mateas and Andrew Stern,
  [*Procedural Authorship: A Case-Study of the Interactive Drama
  Facade*][procedural-authorship].  Although about narrative, “procedural
  authorship” is a useful way to think about designing a possibility space
  rather than generating finished content.
- Georgios Yannakakis and Julian Togelius,
  [*Experience-Driven Procedural Content Generation*][edpcg].  Makes player
  experience part of the generator's model and objective.  For Moppe, riding
  itself can provide evidence without reducing experience to engagement.
- Gillian Smith et al.,
  [*Tanagra: A Mixed-Initiative Level Design Tool*][tanagra].  One of the
  canonical systems: the designer edits while a constraint solver completes
  and repairs a playable level.
- Antonios Liapis et al.,
  [*Sentient Sketchbook*][sentient-sketchbook].  A co-creative strategy-map
  tool that continuously generates alternatives and critiques properties of
  the current design.
- Alberto Alvarez et al.,
  [*Towards Friendly Mixed Initiative Procedural Content
  Generation*][friendly-pcg].  Three excellent requirements from production
  practice: respect designer control, respect the creative process, and
  respect existing work.
- Jichen Zhu et al.,
  [*Explainable AI for Designers: A Human-Centered Perspective on Mixed-
  Initiative Co-Creation*][explainable-cocreation].  Relevant to a Terrain Lab
  where operations need to reveal why they suggest, reject, or alter a place.
- Kate Compton and Michael Mateas,
  [*Casual Creators*][casual-creators].  Perhaps the closest design spirit to
  Townscaper: systems made for the pleasure of making, with no requirement
  that users become professional content creators.

## 9. Games and artifacts to study by playing

Games are arguments in executable form.  These do not each instantiate the
whole Moppe thesis; each isolates a useful relationship among traversal,
construction, ecology, persistence, and algorithmic assistance.

- [Townscaper][townscaper] — every click is both authored and generously
  interpreted; local rules turn a tiny vocabulary into surprising wholes.
- [Death Stranding][death-stranding] — traversal gradually writes a shared
  infrastructure into difficult land.  Desire paths, bridges, signs, and
  cooperative construction make movement and world-making inseparable.
- [Wurm Online][wurm] — slow, persistent terraforming and settlement make the
  altered landscape a record of collective labor.
- [Eco][eco] — construction occurs inside an ecological simulation whose
  feedback makes care and unintended consequences mechanically legible.
- [Terra Nil][terra-nil] — restoration itself is the spatial puzzle, though
  its goal of eventual withdrawal differs from persistent inhabitation.
- [Wildmender][wildmender] — water, planting, and long-term ecological tending
  turn repair into the primary loop.
- [Cloud Gardens][cloud-gardens] — a casual creator about finding a satisfying
  balance between human remnants and plant growth.
- [Minecraft][minecraft] — the indispensable proof that a rigid constructive
  meter can enable extraordinary vernacular diversity and legibility.
- [Tiny Glade][tiny-glade] — a contemporary study in forgiving, contextual
  construction: walls, paths, arches, and buildings repair themselves around
  direct manipulation.
- [Dorfromantik][dorfromantik] — landscape composition as a calm tile game;
  useful for adjacency, continuation, and the difference between scoring a
  match and sensing a place.
- [The Legend of Zelda: Tears of the Kingdom][totk] — construction remains
  embodied, provisional, and tested immediately through action and physics.
- [Trackmania][trackmania] — a mature culture in which driving, track
  construction, sharing, and iterative mastery form one ecology.
- [Riders Republic][riders-republic-game] — useful both as a huge traversable
  landscape and as a production example of algorithmically assisted trail
  creation.

## 10. Repositories and small things to run

These are good antidotes to reading indefinitely.  Each can produce a small
experiment, reveal a representation, or supply a concrete implementation to
argue with.

- [mxgmn/WaveFunctionCollapse][wfc] — the canonical compact WFC examples.
- [BorisTheBrave/sylves][sylves] — abstractions and algorithms for many grid
  topologies, including Townscaper-like grids.
- [wjakob/instant-meshes][instant-meshes-code] — field-aligned remeshing.
- [redblobgames/mapgen4][mapgen4-code] — editable polygonal terrain and
  hydrology.
- [Citybound/citybound][citybound] — roads, parcels, and city simulation.
- [marian42/wavefunctioncollapse][wfc-3d] — a readable 3D WFC example.
- [Sebastian Lague/Hydraulic-Erosion][lague-erosion] — a compact GPU erosion
  implementation and accessible explanation.
- [Red Blob Games' pathfinding pages][redblob-pathfinding] — executable,
  inspectable versions of graph search and cost-field ideas.

## 11. Desire paths, animal trails, and networks that make themselves

This was one of the richest trails in the original research conversation and
deserves its own section.  A path need not begin as a solved route between two
fixed endpoints.  It can emerge through stigmergy: movement leaves a trace;
the trace makes later movement easier; repeated use consolidates some lines;
unused lines recover.  Human and animal trail research adds limited
perception, different bodies, route redundancy, and environmental change.

- **Begin here:** Dirk Helbing et al.,
  [*Active Walker Model for the Formation of Human and Animal Trail
  Systems*][active-walker].  This may be the closest paper to the “pilgrims
  whose gravity is desire” idea.  Walkers are attracted toward destinations
  while modifying a field that influences subsequent walkers; trace creation
  and decay shape the resulting network.
- Dirk Helbing, Joachim Keltsch, and Peter Molnar,
  [*Modelling the Evolution of Human Trail Systems*][human-trails].  A shorter
  account focused on the feedback between walkers, landscape, and network
  topology.
- Hata and Amemiya,
  [*Mountain Trail Formation and the Active Walker Model*][mountain-trails].
  Especially relevant because it takes the active-walker mechanism onto
  sloped terrain rather than an abstract flat plane.
- **Begin here:** Perna et al.,
  [*Animal Transportation Networks*][animal-networks].  A delightful review
  of networks created by repeated physical improvement, external signals such
  as scent, and explicit construction.  It frames infrastructure as a balance
  among construction cost, travel efficiency, and redundancy.
- Devin White,
  [*The Basics of Least Cost Analysis for Archaeological
  Applications*][least-cost-archaeology].  A clear baseline and a useful
  warning: least-cost models commonly assume rational travelers with complete
  knowledge.  A generated route should name those assumptions.
- Joseph Lewis,
  [*Explaining Known Past Routes, Underdetermination, and the Use of Multiple
  Cost Functions*][route-underdetermination].  Several plausible cost models
  can explain the same observed route.  Slope-only routing can look objective
  while silently choosing one hypothesis among many.
- Ko, Shim, and Jung,
  [*Multiplicity and Path Dependency in Historical Routes*][path-dependency].
  Adds hillslope position—bench, shoulder, valley margin—to ordinary slope
  cost and models how earlier routes bias later ones.
- Pinto and Keitt, [*Beyond the Least-Cost Path*][beyond-lcp].  In landscape
  ecology, connectivity may be a bundle of near-good corridors whose
  redundancy matters, not one optimal pixel-wide line.
- Atsushi Tero et al.,
  [*Rules for Biologically Inspired Adaptive Network Design*][physarum].  The
  famous Physarum work is valuable here as adaptive conductance: useful flow
  strengthens edges while weak connections decay, balancing efficiency,
  total material, and resilience.
- David Levinson and Bhanu Yerra,
  [*Self-Organization of Surface Transportation Networks*][self-organizing-roads].
  A human-transport counterpart in which demand, cost, revenue, and
  reinvestment produce a road hierarchy rather than receiving one in advance.

Together these suggest an experiment more interesting than A* followed by a
spline.  Give movers imperfect knowledge and body-specific costs; let passage
slightly improve a trace; let weather and disuse erase it; and preserve a
corridor of near-good alternatives before declaring any single centerline.

## 12. Paths belong to water, landform, and history

Trails are small earthworks.  Their alignment determines whether they shed
water, become muddy, widen, incise into gullies, or disappear.  The practical
trail literature is unusually valuable because it joins geomorphology,
embodied travel, construction, maintenance, and long observation.

- **Begin here:** Meadema et al.,
  [*The Influence of Layout on Appalachian Trail Soil Loss, Widening, and
  Muddiness*][trail-soil-loss].  Direct-ascent alignments tend to collect
  surface flow and incise; rolling side-hill alignments allow water to cross
  the tread.
- The US Forest Service,
  [*Trail Construction and Maintenance Notebook*][forest-trails].  A
  surprisingly generative document about rolling contour alignment, grade
  reversals, drainage crossings, switchbacks, and fitting a trail to the
  landform.
- California State Parks,
  [*Principles of Trail Layout and Design*][california-trails].  Another strong
  procedural manual: a sequence of local decisions constrained by soil,
  sideslope, drainage, users, and desired experience.
- Bill Hillier et al. on
  [metric and topo-geometric street properties][street-properties].  Locally,
  distance matters; at larger scales, directional continuity, topology, and
  turns can dominate.  A visually continuous route can become important
  without being the shortest.
- Penn et al., [*Natural Movement*][natural-movement].  Street configuration
  affects movement; movement attracts land uses; those uses become
  destinations and intensify the network.  Centers can therefore be effects
  of paths as well as their causes.
- Lake, Ortega, and Saalman,
  [*A Least-Cost Network Neutral Landscape Model of Human Sites and
  Routes*][sites-and-routes].  A particularly close model in which sites arise
  from least-cost catchments and a route network is then developed among
  them.

A useful Moppe reading falls directly out of these sources:

```text
trail vulnerability =
    traffic
  × trail grade
  × alignment with downhill flow
  × intercepted contributing area
  × soil erodibility
```

That reading can act as a hydrological veto without insisting that every
vulnerable path vanish.  A muddy fall-line shortcut may remain compelling
while a durable contour road develops beside it.  The two lines then possess
different biographies.

## 13. Physics-first routes, jumps, and diverse kinds of fun

The earlier game-oriented search found a second missing cluster.  The central
lesson is simple: generate a trajectory—or a family of trajectories—before
committing to geometry.  A line is playable because an embodied state can
move through it, not because its spline looks road-like from above.

- **Begin here:** Douglas Gregory,
  [*Path-First Platformer Generation*][path-first].  The system samples a
  trajectory using the game's movement physics and then constructs geometry
  that supports it.  For Moppe, substitute a bike state containing velocity,
  heading, drift, boost, grounded state, and landing condition.
- Graham Todd et al., [*Momentum*][momentum].  Autonomous agents, parameter
  sweeps, and structured crash reports validate runtime-generated endless-
  runner content.  The genre differs, but “generate, drive, and classify the
  failure” transfers directly.
- Kapania, Subosits, and Gerdes's
  [racing-trajectory algorithm][racing-trajectories] is already listed above;
  its separation of speed-profile estimation from line optimization is a
  useful critic for a route corridor after the corridor exists.
- Lucas et al.,
  [*Search-Based Procedural Content Generation for Race
  Tracks*][search-racetracks].  Evolves parameterized tracks under several
  objectives.  It offers representations and evaluation machinery while also
  demonstrating why “more curvature variety” is not yet a theory of fun.
- Cardamone et al., [*TrackGen*][trackgen].  Interactive evolution of playable
  TORCS racetracks: an important precedent for a human choosing among
  generated alternatives rather than accepting one automatic optimum.
- Volz et al.,
  [*Extracting Physics from Blended Platformer Game Levels*][extract-physics].
  A useful conceptual precedent for inferring the movement model that makes
  level geometry meaningful.
- Daniele Gravina et al.,
  [*Procedural Content Generation through Quality
  Diversity*][pcg-quality-diversity].  Instead of one scalar “fun” optimum,
  retain good routes across dimensions such as airtime, speed, drift, risk,
  intervention, hydrological fit, and number of viable lines.
- Antonios Liapis et al.,
  [*Constrained Novelty Search*][constrained-novelty].  Search for meaningful
  difference while preserving hard validity constraints: physics first,
  interesting variation second.
- Georgios Yannakakis and Julian Togelius's
  [experience-driven PCG paper][edpcg-paper].  The eventual critic should
  include actual play: braking, takeoffs, landings, crashes, turnarounds,
  retries, informal shortcuts, and places where someone stops to look.

For jump generation, these references point toward searching backward from a
generous landing region.  Choose acceptable approach and impact envelopes,
integrate candidate flights, find terrain capable of providing the takeoff,
and then test many imperfect riders.  The desired result is not one exact
successful input sequence but a tube containing a slow safe line, an intended
line, a risky fast line, recoveries, and failures interesting enough to invite
another attempt.

## 14. Hydrology, landform readings, and executable landscapes

The first research pass was grounded in Moppe's immediate terrain work.  Much
of this material also disappeared from the shorter bibliography.  It belongs
here because paths and centers can only respond to land if the land has
legible causal structure.

- **Begin here:** Richard Barnes, Lehman, and Mulla,
  [*Priority-Flood*][priority-flood].  The foundation beneath depression
  filling and watershed labeling; the flow-direction and watershed variants
  matter as much as the filled elevation surface.
- Guillaume Cordonnier et al.,
  [*Hierarchical Watersheds for Procedural Stream
  Networks*][hierarchical-watersheds].  Treats river-network hierarchy as a
  controllable procedural object while preserving watershed logic.
- Richard Barnes,
  [*Parallel Priority-Flood Depression Filling for Trillion Cell
  DEMs*][parallel-priority-flood].  Over-scale for Moppe, but clarifies which
  parts of depression routing are local and which require global agreement.
- Jain et al., [*FastFlow*][fastflow].  Recasts depression routing as a
  minimum-spanning-tree problem and accumulation as a parallel tree scan.
- Braun and Willett,
  [*A Very Efficient, Implicit and Parallel Method to Solve the Stream Power
  Equation*][fastscape-paper].  The conceptual root of FastScape-style
  incision and a good precursor to an explicit age/uplift/erodibility
  transform.
- Hergarten, [*The Stream Power Law*][stream-power-law].  A conceptual review
  of what its common exponents mean and where the model ceases to be
  physically convincing.
- Perron, Kirchner, and Dietrich,
  [*Formation of Evenly Spaced Ridges and Valleys*][ridge-spacing].  A
  beautiful demonstration that drainage competition can select visible
  characteristic spacing.
- Bonetti et al., [*Channelization Cascade in Landscape
  Evolution*][channelization].  Channel networks emerge through instabilities
  and interactions among scales, rather than decorative river placement.
- Jasiewicz and Stepinski, [*Geomorphons*][geomorphons].  Horizon comparisons
  identify ridges, valleys, shoulders, spurs, hollows, slopes, and flats
  without relying only on noisy local derivatives.
- Kirmse and de Ferranti,
  [*Calculating the Prominence and Isolation of Every Mountain in the
  World*][mountain-prominence].  Peak-saddle hierarchy is useful far beyond a
  list of summits: it supplies nested terrain centers.
- Funke, Huning, and Sanders,
  [*A Sweep-Plane Algorithm for Calculating the Isolation of
  Mountains*][mountain-isolation].  A modest lonely hill may be a stronger
  destination than a higher subordinate summit.
- Haverkort and Toma,
  [*Visibility Computation on Massive Grid Terrains*][terrain-visibility].
  Rotating-ray and horizon methods give a basis for viewshed and reveal-point
  readings.
- [Landlab][landlab] and its [SPACE example][landlab-space] form the best
  executable encyclopedia in this cluster: grid, flow routing, depression
  handling, incision, sediment, diffusion, vegetation, and shallow water as
  distinct components.
- [fastscapelib][fastscapelib] is useful for its composable process boundaries;
  [RichDEM][richdem] for focused C++ terrain algorithms; [HighMap][highmap] for
  a contemporary C++ heightfield library; and
  [terrain-erosion-3-ways][erosion-three-ways] for a small visual comparison
  among simulation, drainage-first construction, and learning.

## 15. More primary Alexander material recovered from the conversation

The main Alexander section contains the core reading route, but the earlier
search also found unusually useful primary-source archives and operational
studies.

- The [Christopher Alexander CES Archive][ces-archive] now makes lectures,
  drafts, project records, and hard-to-find source texts directly available.
- The archive's excerpt on
  [structure-preserving transformations][spt-excerpt] uses simple diagrams
  that make differentiation and preservation concrete.
- Alexander's [*Generative Codes*][generative-codes] emphasizes that the order
  of operations is as important as the ingredients.  This is one of his
  closest approaches to executable procedural generation.
- *A New Theory of Urban Design*, with an
  [overview and bibliographic entry][new-urban-theory], records a group
  incrementally making a model city under seven rules.  David Seamon's
  [analysis of those rules][seamon-seven-rules] is a helpful guide.
- The CES Archive's [Oregon Experiment collection][oregon-experiment]
  documents a long attempt to replace fixed master planning with organic
  order, diagnosis, participation, coordination, and piecemeal growth.
- Dawes and Ostwald,
  [*The Mathematical Structure of Alexander's A Pattern
  Language*][pattern-language-graph], analyzes 253 patterns and more than
  1,800 relationships as a network.
- Alice Rauber and Romulo Krafta,
  [*Alexander's Theories Applied to Urban Design*][alexander-urban-design]
  attempts to formulate centers and harmony-seeking computation in an urban
  analytical model.
- David Seamon,
  [*Christopher Alexander's Theory of Wholeness as a Tetrad of Creative
  Activity*][seamon-tetrad], is a readable explanation of centers as regions
  of intensified physical and experiential order.
- [*Architecture and the Self*][architecture-self] experimentally studies
  Alexander's paired-image judgments with 192 participants.  Pairwise choice
  remains a promising human instrument when a scalar Beautimeter becomes too
  confident.
- [*Living Geometry, AI Tools, and Alexander's 15
  Properties*][living-geometry-ai] explores computer vision, eye tracking,
  and generative AI as diagnostic instruments.  It is interesting precisely
  where caution is needed: detecting a thick boundary is not equivalent to
  understanding what that boundary supports.

## 16. Legibility, revelation, and attachment over time

The original search also distinguished a good-looking candidate site from a
place that becomes important through approach, use, and memory.  This matters
because no terrain generator can install attachment in one pass.

- Kevin Lynch's [*The City Image and Its Elements*][lynch-elements] develops
  paths, edges, districts, nodes, and landmarks as components of an imaginable
  city.  They transfer surprisingly well to a landscape and suggest a blunt
  but useful test: can someone explain how to return without coordinates?
- Appleton's prospect–refuge theory is a familiar account of environmental
  preference.  A [meta-analysis][prospect-refuge] finds stronger evidence for
  prospect than refuge.  For Moppe, the richer unit may be the sequence from
  enclosure to exposure: a path that climbs through confinement and suddenly
  releases a view.
- Leila Scannell and Robert Gifford,
  [*Defining Place Attachment: A Tripartite Organizing
  Framework*][place-attachment] separates person, psychological process, and
  place.  It helps prevent “place quality” from becoming a property of
  geometry alone.
- Kyle et al.,
  [*Linking Place Preferences with Place Meaning*][trail-attachment], studies
  Appalachian Trail users and distinguishes place identity from place
  dependence.  The same physical setting can matter differently to different
  users.
- A recent [affordance-oriented place-attachment study][attachment-affordance]
  connects naturalness, spatial definition, variety, views, focal points, and
  places to sit with the experiences they afford.  The feature does not cause
  attachment directly; a history of meaningful activity mediates it.

This implies at least three separate records: candidate affordances in the
land, encounter history, and event or social history.  Crashes, discoveries,
storms, repeated approaches, shared paths, and names can promote a candidate
center into an irreplaceable place.

## 17. Implementation side roads found in the first project survey

These sources are less about the speculative world and more about preserving
Moppe's unusually clear technical language while it grows.  They were part of
the original research because architecture determines which ideas remain
inspectable enough to become play.

- Ragan-Kelley et al.,
  [*Halide: Decoupling Algorithms from Schedules*][halide-paper], with the
  [Halide repository][halide].  The meaning of a field computation and the
  strategy used to execute it are separate values.  The important warning is
  that global graph algorithms and historical transforms should not be
  disguised as pointwise scalar expressions.
- Hu et al., [*Taichi*][taichi-paper], with the [Taichi repository][taichi].
  A small field-oriented language grows to encompass iterative simulation,
  reductions, sparse storage, differentiation, and several backends.
- Conal Elliott, [*Compiling to Categories*][compiling-categories].  A more
  abstract but lovely account of compiling a compositional language by
  interpreting it into different semantic categories.
- Losasso and Hoppe, [*Geometry Clipmaps*][geometry-clipmaps], alongside the
  [GPU Gems implementation][geometry-clipmaps-gpu].  Reusable regular meshes,
  height textures, nested LOD, and parent-surface morphing are close relatives
  of Moppe's terrain renderer.
- Filip Strugar, [CDLOD][cdlod].  A direct implementation reference for
  distance-dependent terrain LOD, morph intervals, visibility ranges, and
  crack prevention.
- Eric Bruneton and Fabrice Neyret,
  [*Precomputed Atmospheric Scattering*][atmospheric-scattering].  More
  physical machinery than Moppe necessarily wants, but good background for
  deciding which relationships in sky and distance should remain coupled.
- Sebastien Lagarde and Charles de Rousiers,
  [*Moving Frostbite to Physically Based Rendering*][frostbite-pbr].  Useful
  lighting, exposure, and material vocabulary without implying that Moppe
  should surrender its art direction to generic PBR.
- [Inigo Quilez's articles][iquilez] remain exceptionally useful practical
  reading on analytical noise derivatives, terrain normals, atmosphere, and
  procedural texturing.
- [InfiniteDiffusion][infinite-diffusion] is an instructive contrasting answer
  to terrain authorship: learned refinement of coarse procedural controls.  It
  is valuable partly because its tradeoffs around reproducibility,
  explainability, and geological history differ sharply from Moppe's.

## 18. Rendering water that belongs to the terrain

Once channels and water depth exist, water rendering becomes a structured
problem rather than a translucent ribbon problem.  The most relevant sources
form a progression from cheap flow-map animation through local velocity
fields to physically richer waves and foam.

- **Begin here:** Alex Vlachos,
  [*Water Flow in Portal 2*][portal-water].  The enduring production technique:
  distort two phases of detail with a flow map and cross-fade them to hide
  resets.  A second flow can carry dirt, debris, or foam.
- **Begin here:** Adrien Peytavie et al.,
  [*Procedural Riverscapes*][procedural-riverscapes].  The full pipeline:
  hydrological trajectories, carved beds, water elevation, blended flow
  primitives, rapids, hydraulic jumps, foam, and carried leaves.
- Qizhi Yu et al.,
  [*Scalable Real-Time Animation of Rivers*][scalable-rivers].  Locally solves
  velocity around boundaries and obstacles, then carries screen-space sampled
  wave details over very large rivers.
- Qizhi Yu et al.,
  [*Lagrangian Texture Advection*][lagrangian-advection].  Maintains texture
  spectrum while details follow the velocity field; relevant to normal maps,
  color, foam, and debris.
- Tim Burrell et al.,
  [*Advected River Textures*][advected-rivers].  Couples a coarse fluid model
  to procedural, advected surface detail for long real-time rivers.
- Stefan Jeschke et al., [*Water Surface Wavelets*][water-wavelets].  A richer
  real-time wave representation for large domains, fine detail, and moving
  obstacles.
- Jesus Ojeda and Antonio Susin,
  [*Real-time Rendering of Enhanced Shallow Water Fluid
  Simulations*][enhanced-shallow-water].  Advected foam and detail together
  with screen-space reflection, refraction, and caustics.
- Florian Bagar et al.,
  [*A Layered Particle-Based Fluid Model for Real-Time Rendering of
  Water*][layered-water].  Treats water and foam as separate depth-bounded
  volumes for efficient attenuation.

The locally archived copies and a Moppe-specific implementation order are in
`research/water/README.md`.

## Questions to carry back into Moppe

The bibliography becomes useful when it changes the questions asked of a
prototype.  A few recurring ones are:

- Is this operation placing an object, or strengthening a relation already
  latent in the terrain?
- Does the representation preserve overlapping memberships, or force the
  world into a tree?
- What does a path afford to this vehicle at this speed, in this direction,
  and after this approach?
- Can a generated jump be evaluated as an entire phrase—commitment, takeoff,
  flight, landing, recovery—rather than as isolated geometry?
- Does the cellular rhythm make construction easier while allowing local
  orientation, scale, and deformation?
- When a person edits one cell, can the system repair and enrich the
  neighborhood without erasing the history of the whole?
- Is the generator producing finished scenery, proposing transformations, or
  helping the player see opportunities?
- Can riding be one of the world's measuring instruments?
- What should remain rough, quiet, empty, or unresolved?

The most promising synthesis is not a universal algorithm.  It is a world in
which fields make land, centers make situations intelligible, cells make
construction tractable, physics makes paths answerable to bodies, and play
allows human and procedural judgment to continue the same unfolding.

<!-- Reference links -->
[astar]: https://www.redblobgames.com/pathfinding/a-star/introduction.html
[active-walker]: https://arxiv.org/abs/cond-mat/9806097
[advected-rivers]: https://web.cs.dal.ca/~sbrooks/projects/waterRendering/AdvectedRiverTextures.pdf
[alexander-urban-design]: https://www.mdpi.com/2413-8851/2/3/86
[animal-networks]: https://pmc.ncbi.nlm.nih.gov/articles/PMC4191085/
[attachment-affordance]: https://doi.org/10.1016/j.jenvp.2026.102977
[atmospheric-scattering]: https://inria.hal.science/inria-00288758
[architecture-self]: https://escholarship.org/content/qt2h86h5cg/qt2h86h5cg_noSplash_6d7a6a0e4af76c64493cacd79e3355a3.pdf
[beautimeter]: https://arxiv.org/abs/2411.19094
[beyond-lcp]: https://hero.epa.gov/reference/8026222/
[casual-creators]: https://computationalcreativity.net/iccc2015/proceedings/10_2Compton.pdf
[california-trails]: https://www.parks.ca.gov/pages/1324/files/Chapter%205%20-%20Principles%20of%20Trail%20Layout%20and%20Design.FINAL.04.03.19.pdf
[cdlod]: https://github.com/fstrugar/CDLOD
[ces-archive]: https://christopher-alexander-ces-archive.org/
[channelization]: https://doi.org/10.1073/pnas.1911817117
[city-not-tree]: https://www.patternlanguage.com/archive/cityisnotatree.html
[citybound]: https://github.com/citybound/citybound
[cloud-gardens]: https://www.noio.nl/cloud-gardens-presskit/
[complex-wholeness]: https://doi.org/10.1016/j.physa.2016.08.038
[compiling-categories]: http://conal.net/papers/compiling-to-categories/
[constrained-novelty]: https://direct.mit.edu/evco/article/23/1/101/979/Constrained-Novelty-Search-A-Study-on-Game-Content
[danesh]: https://www.gamesbyangelina.org/wp-content/uploads/2018/03/danesh.pdf
[death-stranding]: https://store.steampowered.com/app/1850570/DEATH_STRANDING_DIRECTORS_CUT/
[dec-course]: https://www.cs.cmu.edu/~kmcrane/Projects/DDG/
[delaunay-refinement]: https://people.eecs.berkeley.edu/~jrs/papers/2dj.pdf
[diffusion-terrain]: https://doi.org/10.1111/cgf.14941
[dorfromantik]: https://www.toukana.com/dorfromantik
[eco]: https://play.eco/
[edpcg]: https://pure.itu.dk/en/publications/experience-driven-procedural-content-generation/
[edpcg-paper]: https://yannakakis.net/wp-content/uploads/2019/02/EDPCG.pdf
[editable-wfc]: https://www.boristhebrave.com/2022/04/25/editable-wfc/
[erosion-three-ways]: https://github.com/dandrino/terrain-erosion-3-ways
[explainable-cocreation]: https://sebastianrisi.com/wp-content/uploads/zhu_cig18.pdf
[friendly-pcg]: https://arxiv.org/abs/2005.09324
[extract-physics]: https://ceur-ws.org/Vol-2862/paper11.pdf
[fastflow]: https://www-sop.inria.fr/reves/Basilic/2024/JKGFC24/FastFlowPG2024_Author_Version.pdf
[fastscape-paper]: https://doi.org/10.1016/j.geomorph.2012.10.008
[fastscapelib]: https://github.com/fastscape-lem/fastscapelib
[forest-trails]: https://www.fs.usda.gov/t-d/pubs/pdfpubs/pdf11232804/pdf11232804dpi100.pdf
[frostbite-pbr]: https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/course-notes-moving-frostbite-to-pbr-v2.pdf
[generative-codes]: https://christopher-alexander-ces-archive.org/wp-content/uploads/2025/04/DOC.20250425.31022.2005.I.C.1.pdf
[geomorphons]: https://doi.org/10.1016/j.geomorph.2012.11.005
[geometry-clipmaps]: https://hhoppe.com/geomclipmap.pdf
[geometry-clipmaps-gpu]: https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry
[harmony]: https://www.cs.york.ac.uk/nature/workshop/papers/Harmony-Seeking_Computation.pdf
[hierarchical-watersheds]: https://doi.org/10.1145/3306346.3322993
[highmap]: https://github.com/otto-link/HighMap
[halide]: https://github.com/halide/Halide
[halide-paper]: https://people.csail.mit.edu/jrk/halide12/
[hillier-alexander]: https://discovery.ucl.ac.uk/id/eprint/10148515/
[hydrology-terrain]: https://www.cs.purdue.edu/cgvlab/www/resources/papers/Genevaux-ACM_Trans_Graph-2013-Terrain_Generation_Using_Procedural_Models_Based_on_Hydrology.pdf
[human-trails]: https://arxiv.org/abs/cond-mat/9805158
[infinite-cities]: https://doi.org/10.1145/1101389.1101396
[instant-meshes]: https://igl.ethz.ch/projects/instant-meshes/
[instant-meshes-code]: https://github.com/wjakob/instant-meshes
[interactive-erosion]: https://doi.org/10.1145/3592787
[interactive-streets]: https://doi.org/10.1145/1399504.1360702
[infinite-diffusion]: https://github.com/xandergos/terrain-diffusion
[iquilez]: https://iquilezles.org/articles/
[landlab]: https://landlab.github.io/
[landlab-space]: https://landlab.readthedocs.io/en/latest/tutorials/landscape_evolution/space/SPACE_user_guide_and_examples.html
[lagrangian-advection]: https://inria.hal.science/inria-00536064/document
[layered-water]: https://www.cg.tuwien.ac.at/research/publications/2010/bagar2010/bagar2010-draft.pdf
[lague-erosion]: https://github.com/SebLague/Hydraulic-Erosion
[least-cost-archaeology]: https://www.osti.gov/pages/biblio/1311302
[leitner]: https://patterntheory.org/wiki.cgi
[living-images]: https://arxiv.org/abs/2301.01814
[living-structure]: https://doi.org/10.3390/urbansci3030096
[living-geometry-ai]: https://doi.org/10.1016/j.foar.2025.01.002
[lynch-elements]: https://www.taylorfrancis.com/chapters/edit/10.4324/9780429261732-67/city-image-elements-kevin-lynch
[mapgen4]: https://www.redblobgames.com/maps/mapgen4/
[mapgen4-code]: https://github.com/redblobgames/mapgen4
[mario-track]: https://julian.togelius.com/Shaker2011The.pdf
[merrell-wfc]: https://paulmerrell.org/wp-content/uploads/2021/07/comparison.pdf
[metropolis]: https://vladlen.info/publications/metropolis-procedural-modeling/
[minecraft]: https://www.minecraft.net/
[mixed-initiative]: https://www.antoniosliapis.com/articles/pcgbook_mixedinit.php
[miq]: https://www.vr.rwth-aachen.de/publication/0344/
[model-synthesis]: https://paulmerrell.org/model-synthesis/
[momentum]: https://arxiv.org/abs/2605.01783
[mountain-isolation]: https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.ESA.2023.51
[mountain-prominence]: https://doi.org/10.1177/0309133317738163
[mountain-trails]: https://arxiv.org/abs/0812.1536
[morphogenesis]: https://doi.org/10.1007/s43762-025-00193-x
[mpcc]: https://arxiv.org/abs/1711.07300
[origins-pattern]: https://www.patternlanguage.com/archive/ieee/ieeetext.htm
[natural-movement]: https://www.sciencedirect.com/science/article/pii/S1353829218306737
[new-urban-theory]: https://books.google.com/books/about/A_New_Theory_of_Urban_Design.html?id=nOtUAAAAMAAJ
[oregon-experiment]: https://christopher-alexander-ces-archive.org/research/master-planning-as-a-dynamic-process-organic-order-and-piecemeal-growth/
[parallel-priority-flood]: https://arxiv.org/abs/1606.06204
[path-dependency]: https://snu.elsevierpure.com/en/publications/multiplicity-and-path-dependency-in-the-modeling-of-historical-ro/
[path-first]: https://dmgregory.github.io/path-first.html
[pattern-language-graph]: https://doi.org/10.1177/2399808318761396
[pcg-book]: https://www.pcgbook.com/
[pcg-quality-diversity]: https://arxiv.org/abs/1907.04053
[platform-analysis]: https://expressiveintelligence.github.io/papers/smith-sandbox-08.pdf
[platform-design]: https://ojs.aaai.org/index.php/AIIDE/article/view/18755
[place-attachment]: https://docslib.org/doc/2163388/defining-place-attachment-a-tripartite-organizing-framework
[polygonal-dec]: https://doi.org/10.1016/j.cagd.2021.102002
[procedural-authorship]: https://electronicbookreview.com/publications/writing-facade-a-case-study-in-procedural-authorship/
[procedural-buildings]: https://doi.org/10.1145/1179352.1141931
[procedural-cities]: https://doi.org/10.1145/383259.383292
[procedural-roads]: https://doi.org/10.1111/j.1467-8659.2010.01751.x
[procedural-riverscapes]: https://www.cs.purdue.edu/cgvlab/www/resources/papers/Peytavie-Computer_Graphics_Forum-2019-Procedural_Riverscapes.pdf
[portal-water]: https://alex.vlachos.com/graphics/Vlachos-SIGGRAPH10-WaterFlow.pdf
[prospect-refuge]: https://link.springer.com/article/10.1186/s40410-016-0033-1
[physarum]: https://doi.org/10.1126/science.1177894
[priority-flood]: https://arxiv.org/abs/1511.04463
[racetrack-dissertation]: https://etd.ohiolink.edu/acprod/odb_etd/etd/r/1501/10?clear=10&p10_accession_num=osu175441440371678
[racing-trajectories]: https://arxiv.org/abs/1902.00606
[redblob-pathfinding]: https://www.redblobgames.com/pathfinding/
[riders-republic]: https://news.ubisoft.com/en-gb/article/7Jdttfpxq3rykQWpGsVFDa/using-algorithms-to-create-riders-republics-trail-network
[riders-republic-game]: https://www.ubisoft.com/game/riders-republic
[ridge-spacing]: https://doi.org/10.1038/nature06092
[richdem]: https://github.com/r-barnes/richdem
[salingaros]: https://patterns.architexturez.net/doc/az-cf-172638
[scalable-rivers]: https://inria.hal.science/inria-00345903/document
[search-pcg]: https://julian.togelius.com/Togelius2011Searchbased.pdf
[search-racetracks]: https://www.ijeei.org/docs-1804133866588e9f62ade3b.pdf
[seamon-seven-rules]: https://christopher-alexander-ces-archive.org/wp-content/uploads/2024/06/DOC.20240508.13678.2019.I.D.1-1.pdf
[seamon-tetrad]: https://www.mdpi.com/2413-8851/3/2/46
[self-organizing-roads]: https://doi.org/10.1287/trsc.1050.0132
[sentient-sketchbook]: https://www.antoniosliapis.com/papers/sentient_sketchbook.pdf
[shape-grammar]: https://doi.org/10.1068/b070343
[space-cognition]: https://discovery.ucl.ac.uk/id/eprint/2111/
[space-machine]: https://discovery.ucl.ac.uk/id/eprint/3881/
[sites-and-routes]: https://link.springer.com/article/10.1007/s10980-024-01836-w
[spt-excerpt]: https://christopher-alexander-ces-archive.org/wp-content/uploads/2025/04/DOC.20250425.30157.2002.I.NoO_.2.2.2.pdf
[stava-erosion]: https://dcgi.fel.cvut.cz/publications/2008/stava-sca-erosion/
[stream-power-law]: https://doi.org/10.1016/j.geomorph.2020.107142
[street-properties]: https://discovery.ucl.ac.uk/18583/
[street-networks]: https://doi.org/10.1016/j.jtrangeo.2015.08.002
[structural-beauty]: https://arxiv.org/abs/2104.11100
[sustainable-wholeness]: https://arxiv.org/abs/1909.11755
[sylves]: https://github.com/BorisTheBrave/sylves
[taichi]: https://github.com/taichi-dev/taichi
[taichi-paper]: https://yuanming.taichi.graphics/publication/2019-taichi/
[tanagra]: https://doi.org/10.1145/1822348.1822376
[terrain-visibility]: https://arxiv.org/abs/1810.01946
[enhanced-shallow-water]: https://web.mat.upc.edu/toni.susin/files/hybrid_lbmsw_rend_new.pdf
[terrain-review]: https://doi.org/10.1111/cgf.13657
[terrain-survey]: https://doi.org/10.1111/cgf.12084
[terra-nil]: https://www.terranil.com/
[third-space]: https://arxiv.org/abs/2108.02493
[tiny-glade]: https://pouncelight.games/tiny-glade/
[totk]: https://www.nintendo.com/us/store/products/the-legend-of-zelda-tears-of-the-kingdom-switch/
[townscaper]: https://www.townscapergame.com/
[townscaper-grid]: https://www.boristhebrave.com/docs/sylves/1/articles/tutorials/townscaper.html
[townscaper-talk]: https://compaec.github.io/news/2020/06/29/StalbergTalk.html
[townscaper-works]: https://www.gamedeveloper.com/game-platforms/how-townscaper-works-a-story-four-games-in-the-making
[trackmania]: https://www.trackmania.com/
[trackgen]: https://www.sciencedirect.com/science/article/pii/S1568494614005705
[trail-soil-loss]: https://www.sciencedirect.com/science/article/pii/S0301479719317049
[trail-attachment]: https://www.sciencedirect.com/science/article/pii/S0272494404000052
[route-underdetermination]: https://www.repository.cam.ac.uk/items/82c84f62-f367-4023-8154-8fb4aa37f873
[uplift-erosion]: https://doi.org/10.1111/cgf.12820
[wfc]: https://github.com/mxgmn/WaveFunctionCollapse
[water-wavelets]: https://research-explorer.ista.ac.at/download/134/5744/2018_ACM_Jeschke.pdf
[wfc-3d]: https://github.com/marian42/wavefunctioncollapse
[wholeness-graph]: https://arxiv.org/abs/1502.03554
[wildmender]: https://wildmender.com/
[wurm]: https://www.wurmonline.com/
