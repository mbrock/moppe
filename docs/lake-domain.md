# Lake identity and relations

`LakeCensus` is the first production use of a discovered world domain. A flood
does not merely paint wet cells: it discovers a finite set of water bodies,
measures each body, and relates terrain cells and river reaches to those
identities.

The implementation starts with the smallest relation already shared by many
consumers:

- `WaterBodyDomain` is the dense set of identities discovered by one census.
- `WaterBodyMembership` is the checked, partial cell-to-body relation. A cell
  targets one identity in that domain or the distinguished `dry` value.
- `WaterBody` remains a compact debugger-readable row of physical and shape
  measurements. `LakeCensus::water_body(id)` checks the identity against the
  domain before reading the row.

The domain is finite and per generated world. A `WaterBodyId` is stable while
that world lives and deterministic for the same supported generation inputs;
it is not yet a persistent place identity across a regenerated or evolving
world.

## Existing relation consumers

| Relation or table | Current consumer | Why it exists |
| --- | --- | --- |
| cell -> body membership | waterline extraction and moisture | identify wet ground without borrowing body measurements |
| cell -> body membership | drainage and watercourse painting | distinguish dry routes, terminating bodies, and traversed flowing pools |
| body measurements | permanence, capture, cinematic planning | classify and select bodies by area, depth, volume, shape, and sea connection |
| body -> outlet cell and spill cell | wet drainage | route a whole flat body through its proven final exit |
| reach -> upstream/downstream body | river extraction and rendering | end an inlet at a standing body or continue the running field through a channel-like body |
| body -> inlet reaches | river inspection, derived today by reversing `downstream_body` | enumerate water entering a body |
| body -> downstream reach | cross-body river continuity, derived today from the spill cell | continue flow after a non-terminal body |

The first three are now represented by the domain, its membership relation,
and checked census rows. Outlet and spill cells remain named fields in each
body row because they already form one coherent, optional body-boundary
record. River extraction still owns reach connections.

When another consumer needs direct body-centric connection queries, the next
small types should be relation-specific tables:

```text
WaterBodyInlet { WaterBodyId body; RiverReachId reach; }
WaterBodyContinuation { WaterBodyId body; RiverReachId downstream; }
```

Those rows would be built with `RiverNetwork`, checked against its body and
reach domains, and replace the current reverse scans. They do not justify a
generic graph, property bag, or interchangeable index map.

## Construction and invariants

`census_lakes` first discovers membership and measurement rows locally. The
`LakeCensus` constructor then establishes one `WaterBodyDomain`, verifies that
every non-dry membership target belongs to it, and verifies that every row's
identity matches its domain position. Consumers either borrow
`membership()` or ask the census for `water_body(id)`; the old parallel public
vectors are gone.

This keeps the representation flat and cheap while making its meaning visible
in types and in the debugger. It also leaves the direct hydrology sequence
unchanged:

```text
FloodField -> LakeCensus -> DrainageGraph -> RiverNetwork
```

Persistent lake history, changing water levels, sedimentation, incision, and
body split/merge identity are later process work. They should be forced by an
actual evolving-world consumer rather than anticipated in this domain.
