# Land

A world's land is what the minutes of generation produce: the surface that
geology, erosion, and the trails' earthworks leave behind, as four fields --
elevation, sediment, and the eroded and deposited material
(`moppe/game/land.hh`). Everything else in a world (normals, standing water,
lakes, drainage, rivers, readings, the forest plan) follows from the land in
seconds. For the default 2048 world on an M2 Pro the geology takes about
100 s; the rest takes about 7.

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

`land-v1-play-2048-123-446e21d0.arrows` names its `LAND_VERSION`, profile,
resolution, seed, and a hash of the recipe's adjustable settings (extent,
datum, uplift years, transport parameters). It is an Arrow IPC stream of the
four columns over the terrain domain, 67 MB for 2048 (58 MB as the
published `.tar.zst`; the fields are noisy enough that zstd gains little).

`LAND_VERSION` counts changes to what land a recipe makes. When geology,
erosion, or trail formation change their output on purpose,
`tests/game/land_test.cc` fails; bump `LAND_VERSION` and pin the new hash.
Older land is then another world's: the game ignores it and prunes it from
the cache.

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

On 2026-10-09: the default land baked on an M2 Pro (macOS) and on an
i7-10700K (NixOS) has the same sha256; a 513 world launched from scratch
and again from its saved land wrote byte-identical finished-world caches;
and the default world launches from its land in 8.7 s on the M2.
