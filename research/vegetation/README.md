# Vegetation rendering research

This shelf collects papers and production material for dense vegetation that
looks alive without making the renderer revolve around it. It is deliberately
matched to Moppe's current split:

- grass is a field-driven, per-blade mesh-shader system and the largest measured
  GPU feature cost;
- trees are distinct Atelier organisms, recruited into a habitat-driven,
  mixed-age forest and baked into one retained mesh;
- moisture, elevation, slope, shore clearance, and tree line already provide
  ecological placement fields;
- grass and tree vertices already share a continuous wind vocabulary.

The most useful conclusion is not one representation for every distance. Keep
individual geometry where its silhouette, parallax, interaction, or identity is
visible; progressively turn it into filtered coverage and canopy appearance as
it becomes subpixel.

## Start here

1. `wohllaib-2021-ghost-grass.pdf` is the closest production analogue to the
   current grass renderer: tile-local GPU generation, field sampling, culling,
   per-blade variation, animation, and LOD.
2. `kuth-2025-gpu-tree-generation.pdf` is the clearest recent destination for
   Atelier trees: generative tree descriptions expanded by mesh work, with
   continuous LOD, animation, pruning, and seasonal organs.
3. `sanders-2018-horizon-vegetation.pdf` connects placement, ground hugging,
   coherent wind, material response, translucency, interaction, and shadow
   policy into one shipped system.
4. `wyman-2019-improved-alpha-testing.pdf` addresses the disappearing and
   scintillating coverage that otherwise makes leaf cards and LOD dissolves
   visibly synthetic.
5. `bruneton-2012-forest-rendering-lighting.pdf` gives the strongest account of
   what a distant forest must preserve: canopy volume, view-light correlation,
   ground shadow, silverlining, and a seamless transition to a filtered
   aggregate.
6. `deussen-1998-plant-ecosystems.pdf` treats distribution, individual form,
   approximate instancing, and rendering as one ecosystem pipeline. It is old,
   but the separation of concerns is still excellent.

## Grass: geometry nearby, coverage far away

- `wohllaib-2021-ghost-grass.pdf` - Eric Wohllaib, "Procedural Grass in
  *Ghost of Tsushima*," GDC 2021. Individual blades are generated from a tile
  grid in compute, jittered, classified from world data, culled by distance,
  frustum, density, and occlusion, then compacted into indirect draw work.
  Blade shape and color are regenerated from small per-blade values rather than
  stored geometry. This is the first paper to compare against Moppe's current
  mesh path. [GDC session](https://gdcvault.com/play/1027033/)
- `jahrmann-2017-responsive-grass.pdf` - Klemens Jahrmann and Michael Wimmer,
  "Responsive Real-Time Grass Rendering for General 3D Scenes," I3D 2017.
  Represents blades as tessellated geometry over arbitrary surfaces, adapts
  subdivision and density with distance, and supports local deformation. The
  geometry stage is dated, but its blade parameterization and response model
  transfer naturally to Metal mesh shaders.
  [TU Wien](https://www.cg.tuwien.ac.at/research/publications/2017/JAHRMANN-2017-RRTG/)
- `boulanger-2006-dynamic-lit-grass.pdf` - Kevin Boulanger, Sumanta Pattanaik,
  and Kadi Bouatouch, "Rendering Grass in Real Time with Dynamic Light Sources
  and Shadows," INRIA RR-5960. Uses actual blade geometry nearby, volume slices
  at medium range, and a surface representation far away. Density weights make
  the transitions continuous; the paper also covers alpha-to-coverage,
  approximate self-shadowing, and filtered dynamic lighting. Its exact slice
  representation is optional; its three-scale argument is highly relevant.
  [HAL](https://inria.hal.science/inria-00087776v2)

The immediate Moppe experiment suggested by these three sources is deliberately
small: retain the current near blades, reduce blade count continuously while
widening or clustering survivors to preserve projected coverage, and hand the
outer band to a much cheaper aggregate. Compare command-buffer GPU time, not
only grass encoder spans, because the existing feature cube shows strong
workload interactions.

## Trees and forests across scale

- `kuth-2025-gpu-tree-generation.pdf` - Bastian Kuth et al., "Real-Time GPU
  Tree Generation," HPG 2025. Encodes trees as compact generative programs and
  expands them through GPU work graphs and mesh nodes. The reusable ideas for
  Metal are organ-specific expansion, frame-specific continuous tessellation,
  culling before emission, and topology-aware animation; the DirectX/Vulkan
  work-graph plumbing is not portable to the current renderer.
  [Eurographics](https://diglib.eg.org/items/93fc78c0-71fa-4511-8564-a7e5268bf27a)
- `decoret-2003-billboard-clouds.pdf` - Xavier Decoret et al., "Billboard
  Clouds for Extreme Model Simplification," SIGGRAPH 2003. Fits a small set of
  textured, partially transparent planes to complex disconnected geometry.
  Because it preserves some parallax and accepts normal maps, it remains a good
  middle or far representation for branch-and-leaf crowns that simplify poorly
  as ordinary meshes.
  [Yale author copy](https://graphics.cs.yale.edu/sites/default/files/bc03_0.pdf)
- `decaudin-2004-real-time-forest-scenes.pdf` - Philippe Decaudin and Fabrice
  Neyret, "Rendering Forest Scenes in Real-Time," EGSR 2004. Packs forest
  samples into edge-compatible volumetric prisms, distributes them with
  aperiodic tiling, and filters the result through LOD. The exact 3D-texture
  method is less attractive now than GPU-generated geometry, but it is a useful
  warning that a distant forest is a volume with parallax, not a green terrain
  decal.
  [Eurographics](https://diglib.eg.org/items/3c60ca3a-63b0-4a34-8e07-3bd2908aff7b)
- `bruneton-2012-forest-rendering-lighting.pdf` - Eric Bruneton and Fabrice
  Neyret, "Real-time Realistic Rendering and Lighting of Forests," *Computer
  Graphics Forum* 31(2), 2012. Combines per-tree z-fields nearby with a
  view-, light-, and location-dependent shader-map at long range. The
  precomputed representation is memory-heavy, but its lighting decomposition
  is an excellent target for a cheaper Moppe-specific canopy aggregate.
  [HAL](https://inria.hal.science/hal-00650120)
- `ladavac-2019-four-million-acres.pdf` - Alen Ladavac, "Four Million Acres,"
  GDC 2019. A shipped large-world account from *Far Cry 5*: bare terrain,
  forest impostors, near trees, miscellaneous plants, and geometric grass are
  separate rendering subsystems. Elevation, slope, convexity, and density drive
  materials and distributions, which closely matches Moppe's surface bundle.
  [GDC PDF](https://media.gdcvault.com/gdc2019/presentations/Ladavac_Alen_Four_Million_Acres.pdf)

For Moppe, the natural ladder is: full intrinsic organism near the player;
organ-aware mesh expansion or instanced representative organs in the middle;
billboard-cloud or depth-bearing canopy representation farther away; then a
filtered forest term in terrain-scale shading. The transition should preserve
coverage and lighting rather than merely swapping triangle counts.

## Leaves, lighting, and stable coverage

- `wang-2005-plant-leaves.pdf` - Lifeng Wang et al., "Real-Time Rendering of
  Plant Leaves," SIGGRAPH 2005. Separates low-frequency environment light from
  high-frequency sunlight and precomputes transport over a tree. It is useful
  for understanding why independently Lambert-lit leaf clusters look flat,
  even if Moppe adopts a smaller approximation.
  [Yale](https://graphics.cs.yale.edu/publications/real-time-rendering-plant-leaves)
- `habel-2007-leaf-translucency.pdf` - Ralf Habel, Alexander Kusternig, and
  Michael Wimmer, "Physically Based Real-Time Translucency for Leaves," EGSR
  2007. Models a leaf as a thin scattering slab with front and back albedo,
  thickness, normal, and translucency data, then compresses the expensive term
  into a directional basis suitable for real-time evaluation.
  [TU Wien](https://www.cg.tuwien.ac.at/research/publications/2007/Habel_2007_RTT/)
- `wyman-2019-improved-alpha-testing.pdf` - Chris Wyman and Morgan McGuire,
  "Improved Alpha Testing Using Hashed Sampling," *IEEE TVCG*, 2019. Replaces
  one fixed alpha cutoff with a stable spatial hash so minified foliage retains
  expected coverage instead of vanishing. It also explains the interaction
  with temporal AA and alpha-to-coverage. Use this when leaf geometry becomes
  cards or when two LOD representations dissolve into one another.
  [NVIDIA Research](https://research.nvidia.com/labs/rtr/publication/wyman2019improved/)
- `habel-2010-vegetation-rendering-animation.pdf` - Ralf Habel, "Real-Time
  Rendering and Animation of Vegetation." A compact overview tying leaf
  reflectance and translucency, canopy self-shadowing, branch mechanics, and
  stochastic fine motion together. Read it before choosing a single isolated
  shading or animation trick.
  [TU Wien](https://www.cg.tuwien.ac.at/research/publications/2010/Habel_RAV_2010/)
- `sanders-2018-horizon-vegetation.pdf` - Gilbert Sanders, "Between Tech and
  Art: The Vegetation of *Horizon Zero Dawn*," GDC 2018. A production bridge
  from the papers to a deferred renderer: a local global-wind force field,
  baked spring categories, terrain-height ground hugging, interaction,
  two-sided material data including translucency, and explicit shadow budgets.
  [GDC PDF](https://media.gdcvault.com/gdc2018/presentations/gilbert_sanders_between_tech_and.pdf)

The smallest attractive tree-shading proof is not a full leaf BSSRDF. Give
foliage a two-sided normal policy, a cheap back-light transmission lobe, crown
or organ-level occlusion, and stable cutout coverage. Preserve normal maps and
transmission parameters when baking any middle-distance cards. That should
produce a larger visual gain than adding more polygonal leaf spheres.

## Distribution and believable form

- `deussen-1998-plant-ecosystems.pdf` - Oliver Deussen et al., "Realistic
  Modeling and Rendering of Plant Ecosystems," SIGGRAPH 1998. Treats terrain,
  ecological distribution, procedural individuals, approximate instancing, and
  rendering as stages of one system. Competition and self-thinning keep a
  population from looking like independent random samples.
  [Stanford copy](https://graphics.stanford.edu/papers/ecosys/ecosys.pdf)
- `palubicki-2009-self-organizing-trees.pdf` - Wojciech Palubicki et al.,
  "Self-organizing Tree Models for Image Synthesis," SIGGRAPH 2009. Realistic
  crowns emerge from competition among buds and branches for light and space,
  regulated by internal signals. This is more relevant to Atelier's intrinsic
  organism than to the renderer, but it explains which structural differences
  should survive into a tree LOD.
  [Algorithmic Botany](https://algorithmicbotany.org/papers/selforg.sig2009.small.pdf)

Moppe already has a stronger placement basis than generic noise: habitat is a
typed field derived from water, moisture, slope, and tree line. The first
population prototype now adds clustered recruitment, age/scale mixtures, and
size-dependent self-thinning while keeping individual tree state separate from
the grass density field. The next ecological step is persistence: succession,
disturbance, and renewed recruitment over world time rather than only during
startup.

## Open articles worth keeping beside the PDFs

- AMD GPUOpen,
  ["Procedural grass rendering"](https://gpuopen.com/learn/mesh_shaders/mesh_shaders-procedural_grass_rendering/)
  - concise mesh-shader code for Bezier blades, patch emission, distance-based
  blade reduction, width compensation, root darkening, and softened normals.
- Renaldas Zioma,
  ["GPU-Generated Procedural Wind Animations for Trees"](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-6-gpu-generated-procedural-wind-animations-trees)
  - a stochastic, branch-aware motion model over a shared wind field.
- Tiago Sousa,
  ["Vegetation Procedural Animation and Shading in Crysis"](https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-16-vegetation-procedural-animation-and-shading-crysis)
  - separates whole-plant bending from leaf detail, uses painted stiffness and
  phase, and gives a pragmatic two-sided translucency approximation.
- NVIDIA,
  ["Next-Generation SpeedTree Rendering"](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-4-next-generation-speedtree-rendering)
  - leaf-card self-shadowing, two-sided lighting, alpha-to-coverage, and LOD
  cross-fades.
- Sucker Punch,
  ["How stunning visual effects bring Ghost of Tsushima to life"](https://blog.playstation.com/?p=345372)
  - a readable account of shared gust fields and damped local grass
  displacement across grass, shrubs, trees, cloth, and particles.

## Concrete questions for the current renderer

- Which grass cost dominates after patch culling: emitted geometry, fragment
  coverage, shadow work, or interaction with full-resolution scene rendering?
- Can blade density fall continuously with projected size while width, color,
  and root occlusion preserve the apparent sward?
- At what distance does a filtered terrain or canopy term become visually
  indistinguishable from individual grass or leaves?
- Can Atelier topology be expanded in a Metal mesh stage without duplicating
  the intrinsic growth model or turning every plant into one storage class?
- Which organ-level values must survive tree LOD: branch direction, crown
  density, material seed, flexibility, transmission, and seasonal state?
- Can one wind field drive terrain grass, branch flexibility, leaf flutter,
  dust, water, and cloth at different temporal frequencies without phase-locked
  repetition?
- Can hashed alpha or alpha-to-coverage keep leaf cards and LOD dissolves stable
  under Moppe's motion blur and render-scale changes?
- How much forest structure can come directly from the existing habitat field,
  and what requires an explicit population process such as recruitment and
  self-thinning?
