# One day at the trailhead

A vertical (1080x1920, ~40 s, silent) film of moppe made with
[fframes](https://github.com/dmtrKovalenko/fframes): shots the game
records of itself, from morning over the birches through a ride, a glide,
the helicopter, the river, the tractor and the Caddy to dusk on the coast
and the birches under the stars. A hairline arc of the sky carries the sun
(and after dusk the moon) across every cut by the hour each shot was
recorded at.

1. Record the footage, in the game's shell (it needs a built game,
   gamescope, and ffmpeg):

   ```sh
   nix develop -c film/record
   ```

   This writes `footage/NAME.mp4` (clips, `CLIPS` in `record`) and cuts
   the shots out of them into `footage/shots/` (`SHOTS`). Only missing
   clips are recorded; name clips or pass `--fresh` to record again.

2. Build and check the film, in its own shell (`nix develop .#film` from
   the repository root, see `shell.nix`), from `film/`:

   ```sh
   cargo run --release -- timeline
   cargo run --release -- inspect
   cargo run --release -- strip all -n 16 --columns 8
   cargo run --release -- preview 30s
   cargo run --release -- render -o moppe-day.mp4
   ```

The film is `src/lib.rs`: the shots (`SHOTS`, with the hour each begins
at, which must agree with `record`), the day's arc, the title, and the
end card. The face is the game's Geomini (`media/`, SIL OFL).
