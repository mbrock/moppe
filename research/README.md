# Moppe research shelf

This directory keeps a selective local shelf of primary papers and practical
manuals that bear directly on Moppe.  It is intentionally smaller than
`ideas/reading-map.md`: the reading map is a broad annotated bibliography,
while this shelf favors open copies worth returning to during implementation
and design.

The PDFs were fetched and checked in July 2026.  Each file was verified as a
parseable PDF with plausible page count and extractable title text.  A sample
from every subject group was also rendered for visual inspection.  Source
links below point to the copy archived here or to its stable landing page.

## Suggested first handful

For the strongest short route through the shelf:

1. `routes/helbing-1998-active-walker-trail-systems.pdf`
2. `terrain/genevaux-2013-hydrology-based-terrain.pdf`
3. `pcg/kapania-2019-racing-trajectories.pdf`
4. `pcg/gravina-2019-pcg-quality-diversity.pdf`
5. `alexander/alexander-2009-harmony-seeking-computations.pdf`
6. `alexander/jiang-2015-wholeness-hierarchical-graph.pdf`
7. `space/jakob-2015-instant-field-aligned-meshes.pdf`

## Routes and self-forming trails

- `helbing-1998-active-walker-trail-systems.pdf` — Dirk Helbing et al.,
  “Active Walker Model for the Formation of Human and Animal Trail Systems.”
  The central stigmergic model: movement modifies a trace field that attracts
  later movement. [arXiv](https://arxiv.org/abs/cond-mat/9806097)
- `helbing-1998-evolution-human-trail-systems.pdf` — Dirk Helbing, Joachim
  Keltsch, and Peter Molnar, “Modelling the Evolution of Human Trail Systems.”
  A shorter account focused on network evolution and feedback.
  [arXiv](https://arxiv.org/abs/cond-mat/9805158)
- `hata-2008-mountain-trail-active-walker.pdf` — Hata and Amemiya, “Mountain
  Trail Formation and the Active Walker Model.”  Takes the mechanism onto
  sloping terrain. [arXiv](https://arxiv.org/abs/0812.1536)
- `california-2019-trail-layout-design.pdf` — California State Parks,
  “Principles of Trail Layout and Design.”  A practical generative language
  for contour alignment, drainage, grade reversals, switchbacks, and fitting
  paths to landform. [source](https://www.parks.ca.gov/pages/1324/files/Chapter%205%20-%20Principles%20of%20Trail%20Layout%20and%20Design.FINAL.04.03.19.pdf)

The excellent review “Animal Transportation Networks” remains linked in the
reading map but is not archived here: its PMC record reports that the full
text is not open access.

## Physics-aware and collaborative procedural generation

- `kapania-2019-racing-trajectories.pdf` — Nitin Kapania, John Subosits, and
  J. Christian Gerdes, “A Sequential Two-Step Algorithm for Fast Generation
  of Vehicle Racing Trajectories.”  Alternates speed-profile estimation and
  path optimization under vehicle constraints.
  [arXiv](https://arxiv.org/abs/1902.00606)
- `todd-2026-momentum-runtime-validation.pdf` — Graham Todd et al., “Runtime
  Evaluation of Procedural Content Generation in an Endless Runner Game Using
  Autonomous Agents.”  Generate, simulate, and classify failures at runtime.
  [arXiv](https://arxiv.org/abs/2605.01783)
- `gravina-2019-pcg-quality-diversity.pdf` — Daniele Gravina et al.,
  “Procedural Content Generation through Quality Diversity.”  A strong frame
  for keeping several different good routes rather than optimizing one fun
  score. [arXiv](https://arxiv.org/abs/1907.04053)
- `guzdial-2020-extracting-platformer-physics.pdf` — Matthew Guzdial et al.,
  “Extracting Physics from Blended Platformer Game Levels.”  Movement physics
  as part of the meaning and transferability of level geometry.
  [CEUR](https://ceur-ws.org/Vol-2862/paper11.pdf)
- `alvarez-2020-friendly-mixed-initiative-pcg.pdf` — Alberto Alvarez et al.,
  “Towards Friendly Mixed Initiative Procedural Content Generation.”  Respect
  designer control, creative process, and existing work.
  [arXiv](https://arxiv.org/abs/2005.09324)
- `zhu-2018-explainable-mixed-initiative-cocreation.pdf` — Jichen Zhu et al.,
  “Explainable AI for Designers.”  Explanations for a person co-creating with
  a computational system. [author copy](https://sebastianrisi.com/wp-content/uploads/zhu_cig18.pdf)
- `summerville-2018-pcg-via-machine-learning.pdf` — Adam Summerville et al.,
  “Procedural Content Generation via Machine Learning.”  Useful for repair,
  critique, analysis, and co-creation as well as autonomous generation.
  [arXiv](https://arxiv.org/abs/1702.00539)

## Alexander, centers, and living structure

- `alexander-2009-harmony-seeking-computations.pdf` — Christopher Alexander,
  “Harmony-Seeking Computations.”  The primary computational statement of
  value-sensitive, structure-preserving transformation.
  [source](https://www.cs.york.ac.uk/nature/workshop/papers/Harmony-Seeking_Computation.pdf)
- `alexander-2002-structure-preserving-transformations.pdf` — Christopher
  Alexander, “Structure-Preserving Transformations: Further Discussion.”
  Compact worked diagrams of preservation and differentiation.
  [CES Archive](https://christopher-alexander-ces-archive.org/wp-content/uploads/2025/04/DOC.20250425.30157.2002.I.NoO_.2.2.2.pdf)
- `alexander-2005-generative-codes.pdf` — Christopher Alexander, “Generative
  Codes: The Path to Building Welcoming, Beautiful, Sustainable
  Neighborhoods.”  Transformation order as a source of coherent wholes.
  [CES Archive](https://christopher-alexander-ces-archive.org/wp-content/uploads/2025/04/DOC.20250425.31022.2005.I.C.1.pdf)
- `jiang-2015-wholeness-hierarchical-graph.pdf` — Bin Jiang, “Wholeness as a
  Hierarchical Graph to Capture the Nature of Space.”  Centers as nodes and
  supporting relationships as directed edges.
  [arXiv](https://arxiv.org/abs/1502.03554)
- `jiang-2021-third-view-of-space.pdf` — Bin Jiang, “Geography as a Science of
  the Earth's Surface Founded on the Third View of Space.”  Organismic space
  alongside absolute and relational accounts.
  [arXiv](https://arxiv.org/abs/2108.02493)
- `jiang-2021-structural-beauty.pdf` — Bin Jiang and Chris de Rijke,
  “Structural Beauty.”  A graph- and scaling-based attempt to quantify
  beauty. [arXiv](https://arxiv.org/abs/2104.11100)
- `jiang-2023-living-images.pdf` — Bin Jiang and Chris de Rijke, “Living
  Images.”  Recursive structural analysis applied to images.
  [arXiv](https://arxiv.org/abs/2301.01814)
- `jiang-2024-beautimeter.pdf` — Bin Jiang, “Beautimeter.”  GPT-based
  assessment through Alexander's fifteen properties; best treated as an
  experimental critic rather than an oracle.
  [arXiv](https://arxiv.org/abs/2411.19094)

## Cellular and example-based construction

- `bommes-2009-mixed-integer-quadrangulation.pdf` — David Bommes, Henrik
  Zimmer, and Leif Kobbelt, “Mixed-Integer Quadrangulation.”  Direction fields,
  singularities, and globally coherent quad layouts.
  [project page](https://www.vr.rwth-aachen.de/publication/0344/)
- `jakob-2015-instant-field-aligned-meshes.pdf` — Wenzel Jakob et al.,
  “Instant Field-Aligned Meshes.”  Practical orientation- and scale-field
  driven remeshing. [project page](https://igl.ethz.ch/projects/instant-meshes/)
- `merrell-2007-model-synthesis.pdf` — Paul Merrell, “Example-Based Model
  Synthesis.”  Large spatial arrangements generated from local neighborhoods
  in an example; an important ancestor of WFC.
  [author copy](https://paulmerrell.org/wp-content/uploads/2022/03/model_synthesis.pdf)

## Terrain and hydrology

See `terrain/README.md` for the terrain papers, their relationship to Moppe's
current implementation, and the experiments they have already informed.
