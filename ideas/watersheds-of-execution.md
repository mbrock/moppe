# The watersheds of execution

*Notes toward reading a program as a landscape of paths, catchments, and
moving attention.*

## The resemblance

Moppe's terrain machinery and its source-code analysis have arrived at nearly
the same object from opposite directions.

The hydrology begins with a field, derives a drainage graph, accumulates
contributing area, finds catchments and confluences, and asks where water is
likely to travel.  The program begins with source files, derives a call graph,
accumulates structural importance, finds communities and bottlenecks, and asks
where execution is likely to travel.

This is not only a convenient metaphor.  Both are directed systems whose
local structure produces larger paths:

```text
terrain                         program

cell or drainage node          function
downhill connection            call
catchment                       entrypoint reachability region
contributing area              transitive callers
confluence                     high fan-in
discharge                      call frequency or PageRank
sediment load                  carried complexity
watershed divide               module or community boundary
closed depression              recursive component
ocean                          return from the outermost entrypoint
```

A river is not bad because it carries a great deal of water, and a function is
not bad because it has high PageRank.  A confluence may be exactly what the
landscape requires.  The interesting places are where high accumulated flow,
steep local structure, fragile material, and consequential downstream
relations coincide.

## The motorcycle and the program counter

The static call graph is the terrain of possible movement.  The program
counter is a motorcycle traveling through it.

At any instant the machine occupies one instruction, but its journey is not a
flat walk from node to node.  A call descends into another region while
remembering the point from which it must return.  Calls nest.  Returns climb
back through suspended approaches.  The resulting path goes in and in and in,
then out and sideways and in again.  It has a profile.

The stack supplies this profile with altitude or depth.  Two visits to the
same function are the same graph location but not necessarily the same place
in the journey: their callers, depths, arguments, and continuations differ.
Recursion revisits one piece of terrain on another turn of the mountain.

Ordinary return is a retracing of the most recently suspended edge.  An
exception is more like crossing or collapsing several ridgelines at once.  It
unwinds valleys without following their ordinary exits.  Tail calls, coroutines,
callbacks, and asynchronous continuations each produce their own departures
from the simple mountain profile.  Threads place several riders in the same
landscape at once.

The ocean is return from the outermost entrypoint.  A frame, task, request, or
program run is a complete expedition from a source back to that sea.

## Static terrain and lived terrain

The static graph says where travel *could* occur.  Runtime observation says
which country is actually inhabited.

A static edge can be:

- a busy road crossed millions of times per second;
- a seasonal stream used only during loading or failure;
- a dry channel reachable in principle but never seen in a recording;
- one branch of a virtual or indirect call whose destination depends on the
  current world;
- an emergency spillway used only by an exception.

Sampling profilers give weighted observations of nodes and stack prefixes.
Instrumented tracing can give actual directed edges, counts, and durations.
Hardware counters add something like ground condition: cache misses, stalls,
branch prediction, and memory pressure.  Allocation profiles describe material
picked up and released along a route.

Several runtime meanings follow naturally:

```text
observed traversals per second   discharge
inclusive time                   water retained by a catchment
exclusive time                   local residence time
allocation volume               transported material
cache misses and stalls          rough ground or congestion
rare extreme latency             flash flood
many interchangeable hot paths   braided river
statically reachable, unobserved dry riverbed
```

A flame graph already resembles a geological section.  A flame chart adds
time, making it possible to draw an execution mountain profile: horizontal
position is the journey, vertical position is stack depth, color is subsystem,
and width or brightness is cost.  Calls and returns form slopes.  Exception
unwinds become cliffs.

## Complexity as transported load

Local complexity belongs to a function, but the difficulty of understanding a
behavior belongs to a path.

Cyclomatic and cognitive complexity can therefore be treated as material
attached to graph nodes.  A walk through the program encounters, carries, and
accumulates that material.  Several measurements then acquire vivid meanings:

- **global discharge** — PageRank on the call graph;
- **source-specific discharge** — personalized PageRank from an entrypoint;
- **complexity exposure** — expected complexity encountered by that directed
  random walk;
- **erosion pressure** — discharge multiplied by local complexity;
- **complex bottleneck** — betweenness multiplied by complexity;
- **downstream burden** — discounted complexity reachable from a function;
- **complex lake** — total complexity inside a strongly connected component;
- **hairball proximity** — shortest distance to a highly complex node;
- **watershed burden** — aggregate complexity, traffic, and centrality within
  a community or entrypoint catchment.

The current call-graph analysis already produces the first versions of these
instruments.  Global PageRank identifies broadly depended-upon channels.
Personalized PageRank asks where a walk beginning at `MoppeGame::tick`, world
generation, rendering, Terrain Lab, or the Metal frame ending is likely to
spend its attention.  Multiplying visitation probability by node complexity
estimates the contribution of each downstream function to the entrypoint's
complexity exposure.

This is more interpretable than inventing a universal badness score.  It says:

> Starting from this activity, which difficult parts of the program am I
> likely to encounter, and how much does each contribute?

## Catchments at several scales

A program has more than one useful watershed.

An entrypoint catchment contains what one activity can reach.  A reverse
catchment contains everything which may eventually arrive at a selected
function.  A module boundary proposes a watershed drawn by the programmer.  A
graph community proposes one inferred from actual relationships.  A runtime
trace draws a temporary watershed around one frame, task, or interaction.

These partitions should be allowed to disagree.  If a graph community crosses
several source directories, the code's lived drainage may not respect its
administrative map.  If two functions share a static catchment but never appear
in the same runtime recordings, a condition or mode may form a real divide not
expressed by the directory tree.

The disagreement is useful evidence:

- module boundaries with heavy cross-flow may be false divides;
- modules with little internal flow may be collections rather than systems;
- a small bridge between large communities may be an important pass;
- a central low-complexity helper may be a healthy river channel;
- a complex high-betweenness function may be an unstable gorge;
- a frightening static hairball may be a dry basin and a lower immediate
  priority than a modest function carrying constant traffic.

## Instruments, not verdicts

The landscape vocabulary should sharpen judgment, not replace it.

PageRank is sensitive to edge direction and graph construction.  Static calls
do not completely resolve virtual dispatch, callbacks, Objective-C messaging,
or function pointers.  Sampling observes only the executions which happened
while the instrument was present.  Cognitive complexity measures control-flow
shape, not the difficulty of domain concepts.  Communities are provisional
partitions, not discovered natural kinds.

Even runtime traffic is ambiguous.  A frequently called function may be a
beautiful, stable abstraction.  A rarely called recovery path may deserve
more care precisely because it is difficult to exercise.  A single large
function can sometimes express a coherent procedure better than a delta of
tiny functions whose relationships must be reconstructed mentally.

The right instrument exposes several accounts which can disagree:

- static possibility;
- observed frequency;
- inclusive and exclusive cost;
- local cognitive and cyclomatic complexity;
- global and source-specific centrality;
- module and community structure;
- code churn and age;
- test coverage and failure history.

The useful question is not simply *which function is worst?*  It is more like:

- Which channels carry the most consequential flow?
- Where does runtime traffic meet difficult local terrain?
- Which catchments cross the boundaries we thought the architecture had?
- Which hairballs are dry, and which flood every frame?
- Which confluences are healthy abstractions, and which have become gorges?
- What path must a reader travel to understand this behavior?

## A possible runtime atlas

The next layer would join profiler evidence to the USR-based static graph.
A sampling or tracing importer could reduce recordings to observations such
as:

```csv
timestamp,thread,depth,function
```

or aggregated directed edges:

```csv
caller,callee,samples,inclusive_ns,exclusive_ns
```

Symbolication would connect observed frames to source functions.  Exact USRs
may not survive into runtime symbols, so qualified names, mangled names, image
offsets, and source locations would participate in the join.  Uncertain joins
should remain marked as uncertain.

The combined atlas could show:

- static channels colored by observed discharge;
- complexity exposure for a typical rendered frame or world-generation run;
- representative high-cost river profiles through the stack;
- dry static tributaries and unexpected runtime channels;
- catchments whose cost or complexity changed between revisions;
- flash floods: rare stacks responsible for long frames;
- sampled paths responsible for most of a subsystem's transported complexity.

Moppe would then contain two related landscape instruments.  One reads water
moving through generated terrain.  The other reads execution moving through
the program which generates it.  Each can lend the other vocabulary, methods,
and perhaps eventually even visual forms.
