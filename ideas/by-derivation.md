# By derivation

*Notes on the other regime of identity: hierarchical deterministic
derivation, the seed as master key, and the save file as a journal of
deviations.*

Sibling to `by-fiat.md`. That essay followed identities sustained by
registers — the cadastre, the census, the kept book. This one follows
the identities the game already mints by the million without keeping
any book at all, and argues that the two regimes are a single design
with a boundary that deserves to be drawn deliberately.

## The derivation tree already in the code

A world begins with one seed. Geology derives its own seed from it;
the forest derives stand noise from `seed ^ 0x4b1d9e37` and break
noise from `seed ^ 0x91e10da5`; `next_seed` steps sideways; the
hash-based tree covering asks, at any site, whether a tree stands
there by hashing the site against the seed. Nothing is enumerated,
nothing is stored, and yet every answer is stable: return to the
place and the same tree stands there. The identity of that tree is
not a row anywhere. It is a *path*: this world, the forest strand,
this site. The XOR constants are crude path labels; the whole
generation stack is an ad-hoc derivation tree from one master value.

Bitcoin formalized exactly this shape and called it the hierarchical
deterministic wallet (BIP32): one master seed, child keys derived by
labeled paths (`m/44'/0'/0'/0/17`), an unbounded lazy tree of
accounts that needs no registry because *the path is the identity*.
That an account exists costs nothing until someone relates to it —
the economy of relating again, run in the opposite direction from
the census. Where the fiat register makes identity affordable for
what the dynamics threw up, derivation makes identity free for
everything the seed implies.

## Why derivation composes with near and far

Derived identity has a property no registry can match: it commutes
with laziness and with granularity. Any subtree of the derivation
tree can be materialized on demand — a chunk of forest, one tree,
one branch — without enumerating the rest, and the same subtree
rematerializes identically whenever regard returns. This is why the
forest's two floors agree: the far density field and the near
organisms derive from the same strand of the same seed, so the
imposter and the tree it stands for are sections of one derivation.
In distributed systems the same trick is called rendezvous hashing:
a deterministic hash is a *coordination-free choice function*,
letting every observer agree on where everything lives without a
registry or a conversation. The renderer, the physics, and a future
network peer can all derive the same world because agreement was
baked into the naming scheme rather than negotiated.

Domain identifiers slot straight into this. Once domains are
identities (`multitudes.md`), the natural identifier is a derivation
path: this world, the forest population, chunk (i, j). Stable across
sessions by construction, lazily materializable, and totally ordered
for free. The domain of domains gets its naming scheme from the same
tree the content comes from.

## The boundary between the regimes

Neither regime survives alone. Derivation cannot survive dynamics:
the moment erosion runs, the terrain is no longer what the seed
implies, and the lakes that emerge are nobody's path — they must be
registered, which is where `by-fiat.md` begins. Registration cannot
survive scale: no cadastre can hold every grass blade. So every
open world engineers a boundary between the two, usually ad hoc:
content is *derived until touched*, and touching moves it into a
register. Minecraft stores only modified chunks. The save file, seen
clearly, is exactly this: **seed plus journal of deviations** — the
derivation is the genesis block, the register is the chain of
postings, and the world at any moment is the deterministic part
replayed plus the ledger applied. The replay machinery already
practices this creed: a session is a recipe (seed) and an input
tape, nothing more.

The interesting design act is drawing the boundary on purpose. The
terrain crosses it at generation time (derived geology, then evolved
— which is why the cache exists). A tree crosses it when chopped or
burned. A lake is born across it. Player acts — naming, building,
claiming — are register entries layered over derived substrate. The
rule of thumb: derivation for what the seed implies, registry for
what history did, and the save file is precisely the difference.

## What the cryptography adds

Hashing gives derivation; cryptography adds two capabilities that
become interesting the moment worlds are shared.

**Delegation.** BIP32's deep trick is that public child keys can be
derived from a parent's *public* key: you can hand a watcher the
ability to enumerate and verify a subtree without the ability to
mint elsewhere or learn the master. The game analog: hand a client,
a mod, or a spectator the derivation key of a region — they can
materialize exactly that subtree of the world, and nothing else.
Deterministic content with scoped disclosure.

**Commitment.** The terrain cache already keys itself by everything
that determines its content, "so a stale cache is impossible by
construction" — content addressing, discovered independently.
Merkleize the derivation-plus-journal structure and the world state
carries its own integrity: two peers sync by comparing subtree
hashes; a replay is *verifiable* (this input tape on this seed
yields this world hash — speedrun verification as a checksum); a
shared world region is a commitment its receiver can check. This is
Bitcoin's actual lesson, separable from currency: a ledger whose
integrity lives in its hash structure rather than in an authority's
say-so — the trial balance upgraded from arithmetic to commitment.

## A small concrete step

The scattered `seed ^ 0x4b1d9e37` idiom is the derivation tree
without its discipline: XOR constants are unlabeled, collision-prone
edges. One small register would make the tree explicit:
`derive (Seed, label)` — a labeled hash step — with the existing
constants becoming named labels ("forest.stands", "forest.breaks").
Cheap now, and it is the foundation everything above stands on:
paths need labels before they can be names.

## Sources

- BIP32, *Hierarchical Deterministic Wallets* — the formalization of
  path-derived identity trees; hardened versus public derivation.
- `by-fiat.md` — the register regime this essay pairs with.
- The rendezvous-hashing literature — deterministic placement as
  coordination-free agreement.
