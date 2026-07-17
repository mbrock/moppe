# Water rendering research

This shelf collects papers and production material for making Moppe's water
belong to its terrain and move convincingly through it.  The emphasis is on
rivers and real-time rendering, with a smaller set of wave, foam, and optical
references that also apply to lakes and oceans.

The papers suggest a useful progression rather than one giant water system:

```text
continuous trajectory and coherent cross-section
  -> flow coordinates and velocity field
  -> advected normals, color, foam, and debris
  -> local procedural waves around banks and obstacles
  -> optional dynamic waves and breaking effects
  -> depth-dependent reflection, refraction, absorption, and foam
```

## Start here

1. `vlachos-2010-water-flow-portal-2.pdf` for the cheapest convincing
   flow-map technique.
2. `peytavie-2019-procedural-riverscapes.pdf` for the complete terrain,
   channel, surface, flow, and effects pipeline.
3. `yu-2009-scalable-real-time-rivers.pdf` and
   `yu-2010-lagrangian-texture-advection.pdf` for scalable detail that follows
   a velocity field without destructive stretching.
4. `jeschke-2018-water-surface-wavelets.pdf` when actual wave dynamics become
   more important than advected appearance.

## Implemented slice (2026-07-16)

Moppe now follows the common structure without adopting a full fluid solve:
dense cubic trajectories and area-driven cross-sections from Riverscapes;
two reset-and-handoff advection phases from Vlachos; and a shared local flow
frame recovered along the curved mesh. The river's global arc coordinate is
continuous through confluences. A seven-row ribbon carries depth, bank
feather, rapid, and waterfall signals, then fades beneath standing water at
the mouth. Yu's richer 2D velocity fields remain the next escalation if a
centerline tangent cannot explain bank or junction flow.

## Implemented slice (2026-07-17)

The ribbon's measured water column now drives a compact Beer-Lambert-like
optical layer instead of selecting between two colors with a depth threshold.
Shallow water remains clear over its bed, increasing depth preferentially
attenuates red light, and dry interpolated ribbon fragments are discarded
before they can claim the overlap stencil. Cross-channel position and depth
also shape advection speed, giving the existing flow texture a cheap
bank-confined velocity profile in the spirit of Yu without introducing a 2D
solver. Shore foam is limited to the shallow contact band rather than being
generated at every nominal mesh edge.

The benchmark comparison is also a useful negative result: improved optics do
not conceal the planar bridges at confluences and mouths, or turn a steep
terrain-following strip into falling water. Those are surface-construction
problems. The next geometric slice should form a single junction boundary and
triangulation before adding a richer junction velocity field.

## Production flow maps

- `vlachos-2010-water-flow-portal-2.pdf` — Alex Vlachos, “Water Flow in
  Portal 2,” SIGGRAPH 2010 course presentation.  The practical game-rendering
  reference: two offset phases of flow-map-distorted texture are blended to
  hide periodic resets, with a separate color flow for debris and dirt.
  [author copy](https://alex.vlachos.com/graphics/Vlachos-SIGGRAPH10-WaterFlow.pdf)

This is the closest short-term fit for Moppe.  Reach tangents, downstream
distance, slope, shore distance, and discharge already provide most of the
coordinates and masks needed to replace screen-space sine streaks with detail
that visibly travels downstream.

## Rivers as terrain plus animated surface

- `peytavie-2019-procedural-riverscapes.pdf` — Adrien Peytavie et al.,
  “Procedural Riverscapes,” *Computer Graphics Forum* 38(7), 2019.  Begins
  with a bare heightfield, extracts river trajectories, carves parameterized
  beds, constructs a water surface, and generates a blend-flow tree with
  primitives for meanders, rapids, hydraulic jumps, foam, and floating leaves.
  [author copy](https://www.cs.purdue.edu/cgvlab/www/resources/papers/Peytavie-Computer_Graphics_Forum-2019-Procedural_Riverscapes.pdf)
- `hendrickx-2010-real-time-river-networks.pdf` — Quintijn Hendrickx, Ruben
  Smelik, and Rafael Bidarra, “Real-time Rendering of River Networks,” I3D
  2010 poster.  A compact treatment of flow through joins and splits in a
  network rather than one isolated ribbon.
  [institutional copy](https://publications.tno.nl/publication/17006495/cJodKI/hendrickx-2010-realtime.pdf)
- `mukai-2008-real-time-river-wave-lod.pdf` — Nobuhiko Mukai, Yasuhiro Kato,
  and Makoto Kosugi, “Real-time River Representation by Dynamic Control of
  Data on Waves.”  Separates near-field detailed waves and reflections from
  far-field directional motion.
  [open journal copy](https://www.jstage.jst.go.jp/article/itej/62/12/62_12_2063/_pdf/-char/en)

Riverscapes is the key counterpart to the terrain shelf's Genevaux paper.
Genevaux makes terrain from hydrological structure; Riverscapes carries that
logic through explicit bed inscription and animated water.

## Flow fields and texture advection

- `yu-2009-scalable-real-time-rivers.pdf` — Qizhi Yu et al., “Scalable
  Real-Time Animation of Rivers,” *Computer Graphics Forum* 28(2), 2009.
  Computes a local steady velocity field around banks and obstacles, then
  carries wave-texture particles uniformly in screen space so detail cost
  follows visibility rather than river length.
  [HAL copy](https://inria.hal.science/inria-00345903/document)
- `yu-2010-lagrangian-texture-advection.pdf` — Qizhi Yu et al., “Lagrangian
  Texture Advection: Preserving both Spectrum and Velocity Field,” *IEEE
  TVCG* 17(11), 2011.  Advects deformable texture patches while controlling
  stretching and retaining local spectrum.  Applicable to normals, foam,
  debris, color, and small displacement.
  [HAL copy](https://inria.hal.science/inria-00536064/document)
- `burrell-2009-advected-river-textures.pdf` — Tim Burrell, Dirk Arnold, and
  Stephen Brooks, “Advected River Textures,” *Computer Animation and Virtual
  Worlds* 20, 2009.  Couples a 2D solver and adaptive pseudo-3D flow
  information to animated procedural wave texture over rivers tens of
  kilometers long.
  [author copy](https://web.cs.dal.ca/~sbrooks/projects/waterRendering/AdvectedRiverTextures.pdf)

Vlachos is the cheap fixed-flow version of this family. These papers become
valuable if the alignment's local tangent cannot represent bank effects,
obstacles, confluences, or locally varying velocity.

## Dynamic waves and shallow water

- `jeschke-2018-water-surface-wavelets.pdf` — Stefan Jeschke et al., “Water
  Surface Wavelets,” *ACM Transactions on Graphics* 37(4), 2018.  A real-time
  wavelet representation that combines large domains and fine detail with
  moving-obstacle interaction.
  [open institutional copy](https://research-explorer.ista.ac.at/download/134/5744/2018_ACM_Jeschke.pdf)
- `thuerey-2007-real-time-breaking-waves.pdf` — Nils Thuerey et al.,
  “Real-time Breaking Waves for Shallow Water Simulations.”  Adds particle
  wave patches, spray, and foam where a heightfield shallow-water model can no
  longer represent overturning motion.
  [author copy](https://cgl.ethz.ch/Downloads/Publications/Papers/2007/Thue07b/Thue07b.pdf)
- `ojeda-2013-enhanced-shallow-water-rendering.pdf` — Jesus Ojeda and Antonio
  Susin, “Real-time Rendering of Enhanced Shallow Water Fluid Simulations.”
  Adds advected foam, small-scale detail, screen-space reflection and
  refraction, and caustics to a shallow-water simulation.
  [author copy](https://web.mat.upc.edu/toni.susin/files/hybrid_lbmsw_rend_new.pdf)

These are longer-term references. They should not block trajectory-aligned
detail or depth-based optical treatment, which are cheaper and more important
to the current river scale.

## Foam, volume, and optical layering

- `bagar-2010-layered-particle-water-foam.pdf` — Florian Bagar, Daniel
  Scherzer, and Michael Wimmer, “A Layered Particle-Based Fluid Model for
  Real-Time Rendering of Water,” *Computer Graphics Forum* 29(4), 2010.
  Generates foam from a physical threshold and renders water and foam as
  separate depth-bounded volumes, allowing efficient attenuation.
  [author copy](https://www.cg.tuwien.ac.at/research/publications/2010/bagar2010/bagar2010-draft.pdf)
- `yingst-2011-fast-ocean-foam-halftoning.pdf` — Mary Yingst, Jennifer Alford,
  and Ian Parberry, “Very Fast Real-Time Ocean Wave Foam Rendering Using
  Halftoning.”  A deliberately inexpensive persistence and dissipation model
  for foam coverage.
  [author copy](https://ianparberry.com/techreports/LARC-2011-05.pdf)
- `darles-2011-ocean-simulation-rendering-survey.pdf` — Emmanuelle Darles et
  al., “A Survey of Ocean Simulation and Rendering Techniques in Computer
  Graphics.”  Broad background on spectra, wave models, foam, spray, and
  light-water interaction.
  [arXiv](https://arxiv.org/abs/1109.6494)

For Moppe, depth-bounded absorption and shoreline fading matter sooner than
volumetric particle foam.  Foam should arise from named structure—banks,
obstacles, high slope, curvature, compression, falls, and hydraulic jumps—so
that it explains the flow instead of covering it with generic noise.

## Concrete questions for the current renderer

- Can two-phase flow advection replace all world-space sine scribbles without
  visible pulsing or texture swimming?
- Is one reach tangent sufficient, or should a low-resolution velocity field
  bend detail around banks and confluences?
- Can water depth be reconstructed from carved bed elevation and water
  surface height, then drive Beer-Lambert-like absorption and shore alpha?
- Which existing readings should generate foam: shore distance, slope,
  discharge, knickpoints, waterfalls, flow convergence, or curvature?
- Which signals belong in the mesh, which in small textures, and which can be
  evaluated procedurally in the fragment shader?
- At what scale do actual dynamic waves become visible enough to justify a
  simulation instead of advected normal detail?
