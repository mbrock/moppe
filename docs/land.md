# Land

A world's land is what the minutes of generation produce: the surface that
geology, erosion, the rivers' beds, and the trails' earthworks leave behind,
as four fields --
elevation, sediment, and the eroded and deposited material
(`moppe/game/land.hh`). Everything else in a world (normals, standing water,
lakes, drainage, rivers, readings, the forest plan) follows from the land in
seconds. For the default 2048 world on an M2 Pro the land takes about 56 s
to make; the rest takes about 7.

Worlds generate bit for bit the same on every platform
(docs/determinism.md), so land is a cache anyone can share. The game takes
the first of these it finds for its recipe, then derives the rest:

1. land packaged with the game, in its `worlds/` folder (`MOPPE_LANDS_DIR`
   at configure time; the Nix packages fill it from `lands.json`);
2. land in its cache folder (`~/Library/Caches/Moppe` on the Mac,
   `~/.cache/moppe` on Linux), downloaded by `tools/fetch-land`, baked by
   `tools/bake-land`, or saved by an earlier launch;
3. otherwise it generates the land and saves it there.

A world derived from land is the same as one generated from scratch, to the
byte: a fresh world finds its trail network in the formed surface just as a
loaded one must, rather than keeping the network trail formation built.

On top of the land, the finished-world cache (`moppe/game/world_cache.*`)
keeps the whole derived world, so a second launch skips even the seven
seconds.

## The file

`land-v2-play-2048-123-446e21d0.arrows` names its `LAND_VERSION`, profile,
resolution, seed, and a hash of the recipe's adjustable settings (extent,
datum, uplift years, transport parameters). It is an Arrow IPC stream of the
four columns over the terrain domain, 67 MB for 2048 (58 MB as the
published `.tar.zst`; the fields are noisy enough that zstd gains little).

`LAND_VERSION` counts changes to what land a recipe makes. When geology,
erosion, channel or trail formation change their output on purpose,
`tests/game/land_test.cc` fails; bump `LAND_VERSION` and pin the new hash.
Older land is then another world's: the game ignores it and prunes it from
the cache, and finished worlds record the `LAND_VERSION` they were derived
from, so they are rebuilt too.

## The rivers' beds

Erosion leaves each river a valley, and along the valley's floor a string of
hollows and sills a few samples long, which the flood analysis would fill
into a stair of ponds. Before the trails are laid, `map::form_river_channels`
(`moppe/terrain/channel.hh`) gives every visible river its bed. The long
profile is graded along the drainage graph between filling each hollow to
its sill and cutting the sill down to it, then eased along the stream, with
a lake's sill, the sea's shore, and a waterfall's lip left where they are.
Along the smooth alignment a parabolic bed is then cut and filled, as wide
as three quarters of the river's regime width where the valley gives it
room and deep in proportion (a quarter of a metre for a brook, over a metre
for the trunk), with banks cut back at a slope a walker climbs. Valley
walls are not moved, a lake's sill keeps its level, and toward the sea the
bed runs out below sea level.

A lake keeps its sill only if it is a lake by its river's measure: a
pond covering less than four squares of the regime width of the river
leaving it is that river's pool, and its sill is cut like any other. So
the trunk, forty metres wide at its mouth, cuts through the small lagoons
behind the beach bar and reaches the sea by an inlet.

The bed is cut on the terrain's lattice too. Where the course steps
diagonally from one sample to the next, the two samples beside the step
stand over it like a dam, and a surface read bilinearly between the
course's samples, the ground's or the water's, pinches shut there: a bed
narrower than a sample is a string of hollows touching at their corners,
and its water a string of pools with dry spots between. So at each such
step the lower of the two is cut to the bed between (unless it stands more
than three metres over it), and the bed runs on from sample to neighbouring
sample. The water painter does the same for its own sheet
(`terrain/watercourse.hh`).

On the default world that cuts 214,000 m3 of ground and lays down 27,000
along 33 km of river, half a metre at the median (the game logs it as
`moppe: river beds:` when it generates land). The share of river length
lying in flooded hollows falls from two thirds to a fifth, which is its
real pools and the backwater above its lakes, and the median fall of
running reaches from five in a hundred to two.

## Sharing it

```bash
tools/fetch-land                 # download the default world's land
tools/bake-land 2048 play 7      # bake another recipe's into the cache
tools/publish-land 2048 play 7   # bake, upload to the land-vN release
```

`tools/publish-land` uploads `<file>.tar.zst` to the repository's `land-vN`
GitHub release (creating it if need be) and records the URL and sha256 in
`lands.json`; commit that. `tools/fetch-land` and the Nix packages
(`moppe/game/lands.nix`) fetch by that hash.

## Checked

On 2026-10-09: the default land baked on an M2 Pro (macOS, 10 threads) and
on an i7-10700K (NixOS, 16 threads) has the same sha256 (LAND_VERSION 1 and
again at 2); a 513 world launched from scratch and again from its saved land
wrote byte-identical finished-world caches; and the default world launches
from its land in 8.7 s on the M2.
