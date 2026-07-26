# Insjö

*A world simulation described whole, in the cold lake after the
sauna: the algebraic, accounting, ontological, and topological
paradigm of a game like this one, written as if founded fresh.*

Insjö is not a plan to rewrite Moppe. It is Moppe's reflection in
still water — the system this codebase keeps discovering itself to
be, stated as if it had been designed that way from the first
morning. The register is the Atelier's: as `atelier-tree.md` refounds
the old tree, this refounds the world.

## 0. The founding stance

A world is three things:

    World = Substrate × Seed × Journal

The substrate is topology and metric — what *can* be adjacent. The
seed implies everything timeless, by derivation. The journal records
everything history did, by posting. The state of the world at any
moment is a fold:

    state(t) = post*(genesis(Seed), Journal[0..t])

and nothing else is authoritative. Saves are seed plus journal.
Replays are seed plus input tape. Caches are memoized prefixes of
the fold, keyed by commitment.

Why this stance yields beauty is worth stating plainly, because it
answers a real question: why does a generated landscape feel like a
field of centers when the Mandelbrot set — infinitely detailed,
perfectly derived — does not. A fractal is all derivation and no
journal. Nothing in it ever happened; no part strengthened another
at cost; zoom is its only history. Alexander's centers strengthen
each other *irreversibly*: the valley deepens because water chose
it, and water chooses it because it deepened. Path-dependence is
mutual reinforcement written down, and written-down mutual
reinforcement is a ledger. The landscape is beautiful because it is
a readable record of its own becoming. Insjö therefore takes as its
first law: **whatever can strengthen must post, and whatever posts
is never erased** — netting is a view, not an operation on the past.

## 1. The substrate

The ground is a flat torus T = ℝ²/Λ, known to the code chiefly
through its universal cover: positions in play live on the cover
(winding numbers are real — the odometer of a lap around the world
is not zero), and quotienting to the fundamental domain happens at
addressing time only. One doctrine, stated once: *live in the cover,
post to the ledger, quotient at the boundary.*

Space is served as a category **D** of domains:

- **Lattices** L₀ ← L₁ ← L₂ ⋯ — quantizations of T at each
  granularity, related by measure-preserving quotients (the mip
  tower). Each lattice serves its metric: spacings, cell areas, a
  neighbourhood whose influence is the inverse square of spacing, so
  that folding influence against differences *is* the Laplacian,
  with the units to prove it.
- **Hives** — population domains: slots with generational handles,
  occupancy, birth and retirement. No adjacency unless earned.
- **Sheets** — embedded complexes (cloth, canopies, organisms):
  their own topology, an embedding morphism into T's space.
- **Quotient objects** — censuses: π₀ of a predicate field is a
  domain whose sites are fiat wholes, fibered over its base by a
  stored membership field.

The morphisms — quotient, inclusion, membership, embedding — carry
the four verbs of the field algebra, and domain identity is nominal:
a domain is an object with a derivation path, not a structural
value.

## 2. Magnitudes

Every number is a quantity: dimension, kind, unit, representation.
Kinds are severed deliberately — airspeed does not add to climb
rate; a count of sweeps does not multiply into a spread except
through its named pace. Three structural choices do most of the
work:

- **The affine law.** States are points; changes are displacements;
  only displacements add. Elevation is a point in a named vertical
  frame (the datum is an origin, not a zero); position is a point on
  the cover; energy content is a point the moment a ledger cares.
  Point minus point is the only exit from a frame, and it lands in
  the displacement kind.
- **The discrete axes have units.** A frame, a sweep, a geological
  step: each iteration axis is a kind, each step size a pace
  (time per iteration), and counts fall out of durations over paces
  by ceiling — no bare integer ever crosses a law.
- **Accumulators are T-accounts.** Any magnitude that can both grow
  and shrink is represented as an unreduced Pacioli pair — debit and
  credit kept separately, balance and turnover both readable,
  negation the swap of sides, equality the cross-sum. This holds
  componentwise in vectors (a displacement ledger whose magnitude
  nets while its components remember the walking) and in unsigned
  cursors (a ring's occupancy is a balance; its throughput is
  turnover).

Honest exits from the algebra are enumerated, few, and named:
extracting a count from a dimensionless ratio, raising to a runtime
power, formatting for display, and crossing a severed kind through
its declared parent.

## 3. Fields and their calculus

A field is a section: F(D, Q), columns of quantities over a domain's
sites, stored columnar, born in borrowed storage (the estate — see
§8). The calculus has six verbs:

    join   : F(D,Q₁) × F(D,Q₂) → F(D, Q₁×Q₂)      -- same domain
    slice  : F(D, Q₁×Q₂) → F(D, Q₁)                -- borrowing
    push   : F(D,Q) → F(D/~, Q)                    -- aggregate along quotient
    pull   : F(D,Q) → F(D′,Q)                      -- restrict along inclusion
    extend : (Focus D Q → R) → F(D,Q) → F(D,R)     -- the comonad
    ∫      : F(D,Q) → Q·Area                       -- integrate; ‖·‖₁, ‖·‖∞

Local laws are written once, at a focus, and `extend` runs them
everywhere; the double buffer is the codomain of `extend`, invisible
to the law. The Laplacian is a library fact, not a stencil: on a
metric lattice, Δ of an elevation field is elevation per area, and Δ
of a point field is made of displacements, so even affine columns
diffuse. Masks are indicator fields (algebra) or site-sets
(traversal) — the same object read twice; fixed sites are Dirichlet
data: read by neighbours, never written.

## 4. The books

Every process posts. A transaction is a row over the chart of
accounts carrying its own proof of balance:

    Tx = { f : Accounts → Pacioli  |  Σf = 0 }

Conserved magnitudes — mass of rock, momentum, energy — post
zero-sum by type: the posting API simply does not accept an
unbalanced row. Non-conserved flows go through *named* equity:
uplift is income from the mantle; the ocean is the residual claimant
of the mass balance; numerical dissipation is an expense account,
visible, never a leak. The trial balance is not a test that runs
sometimes; it is the closing entry of every period, and a failure
names its account.

Two linked books, per Smith: the SNAP book is the balance sheet —
the world's state at a close, which is what the renderer reads and
the checkpoint writes. The SPAN book is the journal — the stream of
postings, which is what replay replays and the profiler mines.
Reports are homomorphisms out of the books: rendering, the HUD, the
benchmark CSV — projections that commute with posting and therefore
cannot disagree with what happened, and cannot act.

## 5. The dynamics, process by process

**Orogeny.** Geology derives from the seed (§7); then deep time runs
on its own axis. Each geological step: (1) flood analysis finds
standing water as the allowable-balance region — depressions filled
to their spills; (2) the census takes π₀: lakes become accounts, the
solver's own fiat objects, so that routing relates to *lake k* and
not to cubic metres; (3) the implicit stream-power step solves
(I + W)h′ = h + u·dt by forward substitution along the drainage DAG
— and every incision is a *face flux*: posted on the edge between
donor and receiver, debited here, credited there, so mass
conservation is exact by construction rather than approximate by
tolerance; (4) hillslope diffusion runs (I + sΔ)ⁿ by `extend`, its
sweep count derived from the stability pace, its spread an area per
iteration; (5) the period closes: uplift income, incision expense,
ocean residual — books balanced, or a bug with a name.

**Water.** Rivers are reaches over the drainage quotient — a domain
whose sites are stretches between confluences, fibered over the
lattice. Lakes persist across steps through the registry (§7):
merges and splits are postings, identity is cadastral. Each lake is
a buffer in the strict sense — capacity to spill, occupancy,
overflow routing — a warehouse with a ledger.

**Trails.** The cadastre written in ground. Walkers post wear
(debit) where they pass; vegetation posts recovery (credit) on its
own slow pace; the trail *is the balance* of that Pacioli field, and
its turnover distinguishes a young busy path from an old faint one.
The loop closes exactly as Alexander requires: the balance steers
the walkers whose wear feeds the balance — a center strengthening
itself, implemented as a feedback ledger. Home bases are the same
field concentrated: dwelling as accumulated posting.

**Machines.** A vehicle is a port-Hamiltonian machine: a small graph
of energy accounts (kinetic, potential, elastic, fuel, heat) whose
couplings are power-conserving ports — every internal energy flow
posts equal and opposite, Newton's third law as double entry.
Contact with the ground is a port to the terrain; drag is an expense
to the air; boost is a draw on fuel equity. The integrator's virtue
is that its books close (symplectic = solvent); its vices are
booked, not hidden. Positions live on the cover — the motorcycle's
displacement ledger nets to zero after a lap while its odometer
holds the turnover and its winding number holds the homotopy class:
three readings of one journey, all true.

**Stars.** A resident population in a hive, identities derived
(m/stars/chunk/i), materialized on regard. Collection is a document
act: the first registry entry in a life that began as pure
derivation — the moment the world starts keeping a file on you.

## 6. Regard

The camera is a focus. Each frame, a granularity assignment — the
coKleisli arrow of the render comonad — tells every region which
floor of the quotient tower to be sampled from. Three laws govern
it:

- **Seams are fiat, therefore closable.** Where two floors meet,
  the boundary is one shared fiat edge, not two approximating ones;
  the renderer's discipline is to keep it single. Cracks are gluing
  failures, nameable before visible.
- **The far end changes register.** Towers do not merely coarsen;
  at distance, populations relax into the fields they were carved
  from — organisms into density, stars into luminosity. The near
  floor must bottom out in *things*, because the player's ontology
  is the folk ontology, and folk theory is object-based.
- **Unstreamed refinement is empty space** — a hole in knowledge,
  the starting-point of the streaming system's questions; spare
  hive slots are empty *cells*, respectable capacity. One emptiness
  is debt, the other is room.

Rendering itself is a report: effect-free, homomorphic, the books
regarded and never touched.

## 7. Identity

Two regimes, one boundary, drawn on purpose.

**By derivation:** everything the seed implies. One master seed; a
labeled derivation tree (`derive(seed, label)`, no anonymous XOR);
paths as identities for domains, populations, and individuals; lazy
materialization commuting with granularity; rendezvous agreement
without coordination. Public derivation keys delegate regions
without surrendering the master; commitments over derivation make
caches self-keying and replays verifiable.

**By registry:** everything history did. The census with its
cadastre; merges and splits as postings; generational handles as
document acts; the player's names and claims as entries in the same
book the solver opened. Content is derived until touched; touching
is a posting; the save file is exactly the difference between the
implied world and the lived one.

## 8. The estate

Storage is tenure, not scatter. The world's owner is a firm: it
holds the arenas from which every field borrows, and scopes within
it (a solve, a frame, a load) hold sub-estates whose lifetimes nest
by construction. Buffers are territories; holding cost is paid by
the layer that owns, affordances are lent downward; every
allocation is a traced event in the books of the program. Solvers
receive their working fields; they do not conjure them. The
generation pipeline is a firm of tasks whose progress is a feed,
whose completion is a deed, and whose cancellation is structured —
the loading screen reads a report, owns nothing.

## 9. The close

Each axis has its period and its closing entry: the sweep, the
geological step, the frame, the session. At every close, books
balance or name their discrepancy; the balance sheet becomes the
next period's opening; the journal streams to the archive in
columnar form, one vectored write, replayable without parsing.

That is Insjö: a substrate that can host adjacency, a seed that
implies a world, and a journal that makes it matter. The sauna is
where this becomes machinery — kinds and concepts and template
errors, storage refs and unity builds. The cold lake is this page:
the same system with its eyes closed, floating, entirely itself.
What the two have in common is the discipline that neither will
compromise: everything ends up somewhere, nothing true is erased,
and the world owes its beauty to the fact that its past is still
legible in its face.

## Relations to the codebase

Nearly every clause above exists in Moppe today, at least in embryo:
the typed quantities and affine elevations; the Laplacian served by
the domain; the census and its fiat lakes; the trail feedback; the
Pacioli representation; the derivation idiom awaiting its labels;
the quotient towers in mips and imposters; the estate discipline
arriving with borrowed bundles. What Insjö adds is not features but
*commitments*: face-flux erosion (conservation by construction),
ledgered energy in the machines, the cadastre as authority for
projected identity, and the closing entry as a first-class event on
every axis. Any of these can be adopted piecemeal; the essay's only
insistence is that they are one design, not eleven.
