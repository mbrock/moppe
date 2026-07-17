+++
id = "ENG-040"
title = "Compose immutable FrameView values for presentation"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "backlog"
depends_on = ["ENG-031"]
order = 140
areas = ["scene", "render", "simulation"]
+++
# Compose immutable FrameView values for presentation

## Outcome

Camera, lighting, weather, actor poses, and mode-specific visible state are
composed before renderer encoding into a plain frame reading.

## Scope

Do not create a generic scene graph. This is the concrete presentation value
needed by Moppe's existing renderer.

## Acceptance

- Rendering reads a world, session, and frame view without mutating simulation.
- Camera shake, cinematics, and Terrain Lab views remain visibly equivalent.
