# Working practices

How changes are made here, distilled from the ones that went well.

## Goldens are the constitution

Bitwise-reproducible world generation is the target invariant that makes
everything else safe. Before migrating or refactoring anything that
touches generation: capture golden outputs of the current behavior;
migrate behind tests; verify byte-identical results when behavior is meant to
stay fixed. The repository has deterministic replay
and seam tests, but not yet a complete cross-machine golden corpus.
When behavior must change, re-bless goldens as a conscious, named act
in its own commit. Hunt determinism leaks proactively: floating-point
contraction (FMA is a *semantic
node*, not an optimizer detail), standard-library distribution/shuffle
policies (several generators still use the standard library and are not
yet pinned across implementations), thread scheduling (prefer exact
integer accumulation for parallel reductions), container iteration
order, priority-queue ties (break deterministically, e.g. by cell
index). Anything the world's identity depends on must be either
explicit law or removed.

## Readings before laws; measurement before enforcement

When a pathology appears (example: erosion *increasing* the sink
count), the sequence is: build the instrument, run the sweep,
attribute causes, and only then legislate — and legislate the *weakest
rule that prevents the harm*, with a counter on how often it binds (a
constantly-firing clamp is masking a calibration bug, not fixing a
pathology). Report falsifications plainly and separate conclusions
("the sediment ledger is definitely broken; the rill/sink pathology is
a second problem") rather than letting one fix claim credit for
everything. Record experiments as runnable artifacts and research notes, not
just as chat summaries. Distinct symptom populations get a census and a taxonomy
before a cure; steady-state survivor rates matter more than birth rates.
Retired experiments remain documented under `research/`, but their obsolete
executables do not stay in the active build.

## Semantics must be explicit, never accidental

If a data structure *happens* to have a useful interpretation (a flood
forest that happens to route depressions; outlet seeds that resemble
per-lake spills), either promote that interpretation to a named,
documented, tested contract or refuse to rely on it. Silent
load-bearing accidents are the project's defined enemy. The same rule
governs defaults, thresholds, and rendering filters: the former
four-centimeter water cutoff was an accidental policy. The named
`WaterPermanence` value is the first correction, but it is not yet
serialized into a recipe; the shader cutoff remains only as an
anti-z-fighting tolerance. Move magic numbers into parameters; move
parameters toward the world column.

## Observation is sacred and separate

Readings color the surface; geometry stays terrain. Analysis passes
never mutate. Every important derived structure earns an overlay in
the Terrain Lab (Field Algebra Tycoon) so the human evaluator can
*see* it, and a scalar or census in the readings panel so trends are
watchable. Ledgers are permanent instruments, not debug prints:
eroded/deposited/lost, death causes (water cutoff, flat, boundary,
capped), sink counts, lake censuses. A number on screen with a history
is worth ten
assertions in a test.

## Composition discipline

New capability arrives as *values in the terrain language* — a new
node, transform, or reading — never as special-cased code paths in
consumers. One small family of nodes at a time, each earning its place
by an experiment that needs it. Pipelines are data: selectable,
reorderable, and copyable now; stable serialization remains planned.
The lab, the game, the CLI, and the tests must all consume the same
evaluation path so they cannot drift. Backends are interpretations of
one syntax (the expression DAG
is an IR; emitters are pretty-printers); a derived fast path must be
*generated from* the same value the lab edits, never written beside
it.

## Style

Plain names in code; conceptual vocabulary in comments and
docs. Enum-valued semantics (`SpatialScope`, `EvaluationOrder`) over
boolean flags. GNU-ish formatting per the existing code. Small commits
with strata-quality messages — each message should read, years later,
like a line in the world's biography ("Remove the random world's
boundary"). When estimating, remember the ambient fact of this era:
implementation is cheap and fast; *discernment* — knowing what is
worth wanting and judging whether the built thing is alive — is the
scarce resource. Spend accordingly.
