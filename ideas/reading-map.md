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
[beautimeter]: https://arxiv.org/abs/2411.19094
[casual-creators]: https://computationalcreativity.net/iccc2015/proceedings/10_2Compton.pdf
[city-not-tree]: https://www.patternlanguage.com/archive/cityisnotatree.html
[citybound]: https://github.com/citybound/citybound
[cloud-gardens]: https://www.noio.nl/cloud-gardens-presskit/
[complex-wholeness]: https://doi.org/10.1016/j.physa.2016.08.038
[danesh]: https://www.gamesbyangelina.org/wp-content/uploads/2018/03/danesh.pdf
[death-stranding]: https://store.steampowered.com/app/1850570/DEATH_STRANDING_DIRECTORS_CUT/
[dec-course]: https://www.cs.cmu.edu/~kmcrane/Projects/DDG/
[delaunay-refinement]: https://people.eecs.berkeley.edu/~jrs/papers/2dj.pdf
[diffusion-terrain]: https://doi.org/10.1111/cgf.14941
[dorfromantik]: https://www.toukana.com/dorfromantik
[eco]: https://play.eco/
[edpcg]: https://pure.itu.dk/en/publications/experience-driven-procedural-content-generation/
[editable-wfc]: https://www.boristhebrave.com/2022/04/25/editable-wfc/
[explainable-cocreation]: https://arxiv.org/abs/1806.04029
[friendly-pcg]: https://arxiv.org/abs/2005.09324
[harmony]: https://www.cs.york.ac.uk/nature/workshop/papers/Harmony-Seeking_Computation.pdf
[hillier-alexander]: https://discovery.ucl.ac.uk/id/eprint/10148515/
[hydrology-terrain]: https://www.cs.purdue.edu/cgvlab/www/resources/papers/Genevaux-ACM_Trans_Graph-2013-Terrain_Generation_Using_Procedural_Models_Based_on_Hydrology.pdf
[infinite-cities]: https://doi.org/10.1145/1101389.1101396
[instant-meshes]: https://igl.ethz.ch/projects/instant-meshes/
[instant-meshes-code]: https://github.com/wjakob/instant-meshes
[interactive-erosion]: https://doi.org/10.1145/3592787
[interactive-streets]: https://doi.org/10.1145/1399504.1360702
[lague-erosion]: https://github.com/SebLague/Hydraulic-Erosion
[leitner]: https://patterntheory.org/wiki.cgi
[living-images]: https://arxiv.org/abs/2301.01814
[living-structure]: https://doi.org/10.3390/urbansci3030096
[mapgen4]: https://www.redblobgames.com/maps/mapgen4/
[mapgen4-code]: https://github.com/redblobgames/mapgen4
[mario-track]: https://julian.togelius.com/Shaker2011The.pdf
[merrell-wfc]: https://paulmerrell.org/wp-content/uploads/2021/07/comparison.pdf
[metropolis]: https://vladlen.info/publications/metropolis-procedural-modeling/
[minecraft]: https://www.minecraft.net/
[mixed-initiative]: https://www.antoniosliapis.com/articles/pcgbook_mixedinit.php
[miq]: https://www.vr.rwth-aachen.de/publication/0344/
[model-synthesis]: https://paulmerrell.org/model-synthesis/
[morphogenesis]: https://doi.org/10.1007/s43762-025-00193-x
[mpcc]: https://arxiv.org/abs/1711.07300
[origins-pattern]: https://www.patternlanguage.com/archive/ieee/ieeetext.htm
[pcg-book]: https://www.pcgbook.com/
[platform-analysis]: https://expressiveintelligence.github.io/papers/smith-sandbox-08.pdf
[platform-design]: https://ojs.aaai.org/index.php/AIIDE/article/view/18755
[polygonal-dec]: https://doi.org/10.1016/j.cagd.2021.102002
[procedural-authorship]: https://electronicbookreview.com/publications/writing-facade-a-case-study-in-procedural-authorship/
[procedural-buildings]: https://doi.org/10.1145/1179352.1141931
[procedural-cities]: https://doi.org/10.1145/383259.383292
[procedural-roads]: https://doi.org/10.1111/j.1467-8659.2010.01751.x
[racetrack-dissertation]: https://etd.ohiolink.edu/acprod/odb_etd/etd/r/1501/10?clear=10&p10_accession_num=osu175441440371678
[racing-trajectories]: https://arxiv.org/abs/1902.00606
[redblob-pathfinding]: https://www.redblobgames.com/pathfinding/
[riders-republic]: https://news.ubisoft.com/en-gb/article/7Jdttfpxq3rykQWpGsVFDa/using-algorithms-to-create-riders-republics-trail-network
[riders-republic-game]: https://www.ubisoft.com/game/riders-republic
[salingaros]: https://patterns.architexturez.net/doc/az-cf-172638
[search-pcg]: https://julian.togelius.com/Togelius2011Searchbased.pdf
[sentient-sketchbook]: https://www.antoniosliapis.com/papers/sentient_sketchbook.pdf
[shape-grammar]: https://doi.org/10.1068/b070343
[space-cognition]: https://discovery.ucl.ac.uk/id/eprint/2111/
[space-machine]: https://discovery.ucl.ac.uk/id/eprint/3881/
[stava-erosion]: https://dcgi.fel.cvut.cz/publications/2008/stava-sca-erosion/
[street-networks]: https://doi.org/10.1016/j.jtrangeo.2015.08.002
[structural-beauty]: https://arxiv.org/abs/2104.11100
[sustainable-wholeness]: https://arxiv.org/abs/1909.11755
[sylves]: https://github.com/BorisTheBrave/sylves
[tanagra]: https://doi.org/10.1145/1822348.1822376
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
[uplift-erosion]: https://doi.org/10.1111/cgf.12820
[wfc]: https://github.com/mxgmn/WaveFunctionCollapse
[wfc-3d]: https://github.com/marian42/wavefunctioncollapse
[wholeness-graph]: https://arxiv.org/abs/1502.03554
[wildmender]: https://wildmender.com/
[wurm]: https://www.wurmonline.com/
