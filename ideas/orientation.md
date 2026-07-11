# Orientation: what this is and how to be here

You are working on **moppe**, a motocross game with a procedurally
generated world. That description is accurate and insufficient. Moppe
is also: a terrain laboratory, a working geomorphology model, an
educational instrument, a father-and-son project, and a long
experiment in whether software can be grown the way landscapes grow —
by many small transformations, each of which preserves and deepens the
structure that is already there. Take the game seriously and the rest
follows; take only the game seriously and you will make changes that
are locally correct and globally dead.

## Provenance, because it matters here

The codebase began as an OpenGL/GLUT game written on midsummer eve
2008, slept for most of two decades, and was revived — ported to
Metal, extended, and re-founded — by its author working
conversationally with coding agents. The author's son plays it,
art-directs parts of it, and dictates wishes to agents. The revival is
deliberately *not* a rewrite: the project's own method is the subject
of the project. Old bones, new deposits. Commit messages are strata;
read `git log` as a geological column and you will understand the
codebase faster than by reading the code.

## The one-sentence constitution

**The world should become a reproducible value.** A `TerrainProgram` —
geological recipe, transform sequence, seeds, parameters —
deterministically produces the world within a supported evaluator and
toolchain. Unit tests enforce repeated-run determinism and seam laws,
but cross-machine bitwise golden artifacts do not yet exist. Every
accident of the code's birth environment (fused multiply-adds,
standard-library shuffle order, thread interleavings, priority-queue
tie-breaking) must be found and
either *enshrined* as explicit law or *renounced* — never left as
silent load-bearing behavior. When a refactor lands, the proof that it
preserved meaning should be byte-identical terrain where that contract
exists. Once golden fixtures exist, deliberate semantic changes should
re-bless them consciously and name the change. This discipline is not
bureaucracy; it is what lets structural revolutions happen safely and
often.

## Division of labor

Agents implement, measure, and propose; the human is the **wholeness
evaluator** — the judge of whether a change made the world more alive,
a test that cannot be formalized but can be run by riding the
motorcycle. Respect this loop. Build things *inspectable* (overlays,
readings, experiment harnesses) so the human's judgment has
instruments to work with. Prefer falsifiable claims and report
falsifications plainly; this project's culture treats a clean negative
result ("lifetime alone does not cure the sink explosion") as a
first-class contribution.

## Taste

Plain operational names in code (`FloodField`, `spill_receiver`,
`materialize`); the abstract vocabulary (comonads, group completions,
structure-preserving transformations) lives in docs and comments where
it explains *why*. Enum-valued semantics over boolean capability
flags. Small tested slices over big bangs. Values over opaque
artifacts: anything important — recipes, pipelines, readings, censuses
— should move toward a serializable, diffable, overlay-renderable value
in the terrain language. Stable program serialization is still an open
boundary. No fossilized cathedral dependencies; the portable
core builds with a C++ compiler and zlib. When in doubt, do the
smallest transformation that leaves the whole more alive, and keep
honest books.

## Where to go next

- `../docs/project.org` — current state, priorities, and progress log.
- `../docs/terrain-expressions.md` — the terrain language: sources,
  fields, transforms, readings.
- `theory-of-the-world.md` — the conceptual model underneath the code.
- `../docs/working-practices.md` — the methods that keep changes safe and
  honest.
- `second-author.md` — the long-range design: centers, paths, movers,
  settlements.

The project's acceptance test, stated once so you know the direction
of "better": a generated world passes when one of its places could
make someone want to return to it — when a paragraph about it would be
worth writing, and finding it again would feel like arriving.
