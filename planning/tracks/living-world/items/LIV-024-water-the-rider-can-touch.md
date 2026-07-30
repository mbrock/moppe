+++
id = "LIV-024"
title = "Make water something the rider can enter"
rfc = "RFC-0003"
track = "living-world"
status = "backlog"
depends_on = ["LIV-023"]
order = 160
areas = ["gameplay", "water", "simulation"]
+++
# Make water something the rider can enter

## Outcome

Lakes are wet. Entering water makes a wake, and the water pushes back.

## Scope

Physics believes in exactly one water plane: `mov::Vehicle` carries a scalar
set from the recipe datum, and the underwater grade fires below that datum. A
lake two hundred metres up is visually water and physically air. This is the
single largest divergence between what the world renders and what it
simulates, and it is more embarrassing than any shading defect.

Two pieces:

- **Depth from the sheets.** The water elevation column and the ground
  elevation share a domain and a datum; subtracting the two is the depth.
  Wading drag, the underwater grade, and drowning should all read that, not a
  constant.
- **A wake with memory.** Store displaced volume per cell and distribute the
  change to neighbours — volume-conserving for any distribution factor, and it
  handles a fully submerged body, which naively pushing columns down does not.
  Contact foam is a single-channel time-to-live texture advected by the
  velocity field Moppe already uploads.

## Acceptance

- Riding into a mountain lake behaves like riding into water.
- A wake persists behind the bike and drifts with the current.
- Replay determinism is preserved.

## Research

*Real-time Breaking Waves* (Sheaf `#8SERGP`) §7 for displaced-volume coupling
and the force back on the body. *Enhanced Shallow Water* (Sheaf `#CWC7H9`)
§3.2 for the advected time-to-live foam texture.
