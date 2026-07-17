# Planning

This directory is the repository's durable planning surface. It has three
different kinds of writing:

- An RFC explains a proposed or accepted architectural decision and its
  constraints. It is not a loose idea bucket.
- A track turns an accepted RFC into a dependency graph of small work items.
- A work item is the only place where implementation status is recorded.

The current tracker uses Markdown with TOML front matter. The prose remains
pleasant to edit in any editor; the front matter gives `tools/plan` one source
of truth for identifiers, statuses, ordering, and dependencies.

## Commands

```sh
make plan        # validate identifiers, dependencies, ready items, and cycles
make plan-graph  # print the graph as Mermaid Markdown
./tools/plan status
```

## Work-item contract

Every item in `tracks/<track>/items/` has these fields:

```toml
+++
id = "ENG-000"
title = "A short outcome, written as a change"
rfc = "RFC-0001"
track = "current-engine-refactoring"
status = "backlog"
depends_on = ["ENG-001"]
order = 10
+++
```

Statuses are deliberately few:

- `backlog` — accepted direction, not yet available to start.
- `ready` — scoped and unblocked; a good next implementation commit.
- `active` — being implemented now.
- `done` — accepted with its stated evidence.
- `dropped` — intentionally not pursued; retain the reason in the prose.

An item should state its outcome, narrow scope, and acceptance evidence. It
should describe one independently reviewable commit or a very small sequence
of commits. New possibilities begin in `ideas/`; a proposal becomes an RFC
only when it is ready to be judged, and an RFC gets a track only when it is
accepted as work.
