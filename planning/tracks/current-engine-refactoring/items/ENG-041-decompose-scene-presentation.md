+++
id = "ENG-041"
title = "Decompose scene presentation around FrameView"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "backlog"
depends_on = ["ENG-040", "ENG-012"]
order = 150
areas = ["scene", "game", "render"]
+++
# Decompose scene presentation around FrameView

## Outcome

World, actors, effects, overlays, and HUD become focused presentation routines
that consume the same immutable frame reading.

## Scope

Move existing drawing paragraphs out of `MoppeGame::render`; retain the
game-shaped `Renderer` API until a concrete new need proves otherwise.

## Acceptance

- `MoppeGame::render` handles application-state selection and delegates.
- The renderer is exercised by equivalent captures before and after extraction.
