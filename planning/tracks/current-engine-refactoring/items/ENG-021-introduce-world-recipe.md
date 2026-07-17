+++
id = "ENG-021"
title = "Represent world construction with an explicit WorldRecipe"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "backlog"
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
