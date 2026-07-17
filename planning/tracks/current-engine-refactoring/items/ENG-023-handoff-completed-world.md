+++
id = "ENG-023"
title = "Hand completed worlds across one asynchronous boundary"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "backlog"
depends_on = ["ENG-022"]
order = 90
areas = ["world", "platform", "game"]
+++

# Hand completed worlds across one asynchronous boundary

## Outcome

Background construction produces a complete `GeneratedWorld`; the main thread
activates it and creates presentation resources without observing half-built
state.

## Scope

Keep progress reporting and cancellation policy concrete. Do not make general
async infrastructure.

## Acceptance

- The loading screen observes progress but does not own terrain state.
- Activation has one main-thread handoff point.
- Generation failures retain the current clear failure behavior.
