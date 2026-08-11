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

## The naive look comes first

Before any instrument: render the thing and look at whether it looks
like the thing. One MOPPE_LOOK frame — a camera placed to answer one
compositional question — settled in seconds what a day of profilers had
missed: the "large grass field" was a green fringe around the camera on
a brown world, because the terrain substrate and the grass geometry had
never agreed about the colour of grass. Every instrument had been blind
to it by construction: texture profiles are high-pass (a colour
mismatch is low-frequency), motion residuals measure change (a colour
mismatch is state). Instruments answer only the question they encode.
So the order is fixed: first the naive wide look, judged like a person
judges a picture; then instruments, to quantify what the look already
agrees is there or to catch what looks cannot (subpixel, temporal,
cross-build drift). A measurement may never overrule structural
knowledge or a plain sight of the frame; when they disagree, the
instrument is the suspect.

The naive look must also expose the thing being judged. For vegetation LOD,
make a neutral cross-lit diagnostic with bloom, exposure adaptation, lens
flare, light shafts, and motion blur disabled before looking at the beauty
pass. Those effects are valid parts of the final image, but sun glare can hide
the exact boundary under discussion. `MOPPE_GAZETTEER_DISABLE` forwards a
comma-separated feature list and records it in `capture.txt`; always return to
the ordinary effects-on game after the representation itself is legible.

## Look with instruments, not with impressions

Judging rendered frames by eye has a specific, recurring blind spot:
distance. The mid- and far-field representation of vegetation has now
several times passed still-frame inspection ("the gradient looks
smooth") while the first minute of actual play revealed something
obviously and blatantly missing — grass assembling metres in front of
the rider, drifts that end where the blades do, far ground that is
paint. A still frame invites the eye to rest on the composed near
field; what is absent at distance is exactly what an impression skims
past, and an agent reasoning over downsampled screenshots is worse at
this than a human, not better.

The remedy is the same one this project applies to terrain pathologies:
build the instrument before drawing the conclusion. Distance-rendering
claims should be made from per-distance-band frame statistics —
high-frequency energy, gradient-direction energy, temporal energy
between consecutive ride frames — plotted against distance and compared
across builds (GFX-043 in ideal-dream-graphics.org specifies the tool).
A representation that dies at 40 metres is a cliff in a curve; no one
has to notice it. Until that tool exists, treat "the frames look right"
as a hypothesis, and treat riding the actual game as the test that has
repeatedly falsified it.

And measure in the lab, not in postcards. The gazetteer's composed
views and the demo ride exist for judging the whole picture; they are
assemblages — vehicle, HUD, particles, trail, glare, relief — and a
measurement taken through them attributes everything to everything.
When the question is about one system, isolate it: the grass laboratory
(MOPPE_GRASS_LAB) for the world, and a gazetteer glide (MOPPE_GLIDE)
for a bare moving camera. The day this rule was written, isolation
flipped a conclusion's sign: a demo-ride "measurement" showed a
confident 2.5x appearance-residual wall at the vegetation window edge;
the clean glide showed smooth decay and no wall — the wall had been
HUD popups, dust, the bike, and a capture stride that defeated the
motion compensation. Step zero of any measurement is to actually look
at what is in the frames being measured.

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

Readings remain distinct from the surface geometry. Analysis passes never
mutate their input. Important derived structures should be inspectable through
focused captures, tests, or lightweight built-in instrumentation. Ledgers are
permanent instruments, not debug prints:
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
