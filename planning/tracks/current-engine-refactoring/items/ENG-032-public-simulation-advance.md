+++
id = "ENG-032"
title = "Advance GameSession through one public simulation operation"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "backlog"
depends_on = ["ENG-031"]
order = 120
areas = ["simulation", "game"]
+++

# Advance GameSession through one public simulation operation

## Outcome

Fixed-step gameplay advances through a small, testable operation taking a
world, a session, an input frame, and a typed duration.

## Scope

Extract ordinary playable simulation first. Loading, benchmark scheduling,
and application mode switching remain above the seam initially.

## Acceptance

- The central gameplay portion of `MoppeGame::tick` becomes delegation.
- Replay tests drive the same public advance path as live play.
