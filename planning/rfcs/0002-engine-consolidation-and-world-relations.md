# RFC-0002: Consolidate the engine and make world relations explicit

Status: realized (decision accepted)

## Decision

Moppe will close the migrations opened by the post-RFC-0001 terrain
harmonization before beginning another architectural campaign. The engine will
remain the current direct C++ game: experiments may prove a local mechanism,
but they do not become replacement engines.

The consolidation has four immediate outcomes:

1. the canonical documentation describes the code that exists after the
   seamless-domain, authoritative-bundle, and direct-generation work;
2. typed ground and water readings cross the renderer boundary by borrowing
   their source and writing once into backend storage;
3. deprecated numerical-exit helpers disappear after internal arithmetic
   retains quantities as long as practical; and
4. Moppe is again the ordinary repository build, while Atelier, Lavoir, and
   Étalon remain explicit workshops.

After those close, the first new semantic slice is lake identity. Water bodies,
their cells, inlets, spills, outlets, and downstream reaches will become
explicit domains and relations with typed properties. This is the first
production client of the graphs-as-domains direction; it is not permission to
introduce a generic graph manager, RDF store, or ontology framework.

The execution track is
[`engine-consolidation`](../tracks/engine-consolidation/README.md).

## Constraints

- Preserve `WorldLoading -> GeneratedWorld -> GameSession -> FrameView` as the
  readable ownership and frame narrative.
- Keep source directories and direct CMake source groups; do not recreate an
  internal library graph.
- Prefer one named domain value and relation-specific typed storage over
  adapters, property bags, or interchangeable integer IDs.
- Treat Lavoir and Étalon as evidence. Harvest only mechanisms with a concrete
  Moppe consumer.
- Verify the desktop game, test suite, source analysis, and WebGPU compile
  path as the consolidation changes land.
- Let the lake forcing case establish the minimum useful relational
  vocabulary before generalizing it.

## Why lakes first

The current hydrology already discovers water bodies, drainage, fractional
channels, and river reaches, but their identities and connections remain
embedded in separate result structures. A lake is the smallest world entity
that forces the engine to say all of the following clearly:

- which wet cells constitute the same body;
- which measurements belong to that body;
- where water enters, spills, and continues downstream;
- which identity persists while levels and quantities change; and
- which processes may later fill, drain, incise, or otherwise transform it.

If those relations cannot remain typed, inspectable, and useful to rendering
and erosion, a broader world ontology would only hide the problem.

## Deliberate non-goals

- Rewriting Moppe in Zig.
- Replacing mp-units with the Étalon experiment.
- Moving all bundle storage into Lavoir's page-aligned allocator.
- Splitting algorithmic functions or `MoppeGame` merely to improve line-count
  metrics.
- Implementing persistent world history, a complete process ontology, or
  lake evolution in the first lake-domain item.

## Completion

The RFC is realized when the consolidation items are done and the first
lake-domain value is used by existing hydrology consumers without a
compatibility adapter. Later process and persistence work may build on that
domain in separate RFCs.
