+++
id = "ENG-021"
title = "Represent world construction with an explicit WorldRecipe"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "done"
depends_on = ["ENG-020"]
order = 70
areas = ["world", "terrain", "game"]
+++

# Represent world construction with an explicit WorldRecipe

## Outcome

World extent, topology, seed, water datum, terrain program, and generation
profile travel as one immutable input value.

## Scope

Extract current configuration from `MoppeGame` without changing generated
terrain or creating a broad configuration registry.

## Acceptance

- The normal game, Terrain Lab, and command-line generation describe worlds
  through the same recipe vocabulary.
- Recipe construction and terrain evaluation can be tested without a renderer.

## Evidence

`terrain::WorldRecipe` is a renderer-free immutable value containing extent,
resolution, topology, seed, water datum, generation profile, and the calibrated
terrain program. `MoppeGame` owns that value for cache selection, map creation,
generation, hydrology, Terrain Lab entry, and regeneration; the previous
game-local program builder is gone. `terrain-pipeline-demo ... world` now
constructs the same recipe before evaluating it. The focused
`tests/terrain/world_recipe_test.cc` constructs and evaluates a recipe through
`map::TerrainEvaluator` without a renderer. Its isolated executable passed
(1 test), as did direct `-fsyntax-only` checks for the recipe, pipeline tool,
focused test, and game implementation. A renderer-free
`terrain-pipeline-demo ... 33 123 combined world` run completed the canonical
orogeny-and-trails pipeline successfully.
