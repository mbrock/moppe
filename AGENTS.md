# Moppe Development Guidelines

## Build Commands
- Configure: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo`
  (without a build type the project defaults to Debug, whose world
  generation is many times slower)
- Build everything: `cmake --build build`
- Unit tests: `cmake --build build --target moppe-tests && ctest --test-dir
  build --output-on-failure` (the test binary is excluded from the default
  build, so plain `ctest` otherwise runs a stale one)
- Run the game: `./build/moppe.app/Contents/MacOS/moppe`
  (or `open build/moppe.app`) on macOS, `./build/moppe` on Linux. Both are
  the same SDL3 host (`moppe/platform/sdl/`) drawing through the NHAL
  renderer, on Metal 4 or Vulkan; on Linux, run cmake inside `nix develop`.
  - Game controller: left stick drives and steers; right stick looks around
    (on foot it turns the hiker, riding, driving, or gliding it swings the
    view about the way ahead, which it comes back round to once let be and
    under way; the mouse does the same); right trigger boosts; `A`
    deploys the glider or restarts; `B` mounts/dismounts; `X` cycles the camera;
    and `Y` boosts, flares, or skips the opening. On foot the left shoulder
    hangs the hammock and the right lights the fire. The D-pad navigates
    Terrain Lab. Xbox, PlayStation, and compatible MFi controllers use this
    layout.
  - The game starts on foot beside the parked bike, seen from behind: the
    mouse looks around (captured; `L` lets it loose), `W`/`S` walk, `A`/`D`
    strafe, `Shift` runs, `Space` jumps, `Tab` cycles to the view from in
    front and then through the walker's eyes, and `F` near the bike mounts
    it (and steps off again). Walking and riding each keep the view last
    chosen for them with `Tab`.
    `MOPPE_DEMO`, benchmarks, and the gazetteer still start riding.
    The look and the way the walker goes turn with the mouse at once
    (`Walker::heading`); the body comes round after it through the rig
    (`Walker::facing`, `pose_avatar`): standing, the head and then the
    chest twist toward the look, and beyond some seventy degrees the feet
    step round on the spot after it (a fast spin keeps stepping the way
    it went); under way the body turns at a bounded rate and banks into
    the turn. The walk bobs, sways, and counter-rotates; the run is its
    own cycle, with a flight phase, a longer stride, a high knee and heel
    kick, a deeper lean, and bent arms pumping. The other player sees the
    same (companion wire version 10).
  - The following camera (`game/chase_camera.*`, placed for each way of
    getting about by `GameSession::camera_rig`) is a boom swung about a
    point on the subject. Looking up lowers it below the subject and in
    close over the ground (the ground shortens the boom rather than lifting
    the camera), looking down lifts it nearly overhead. Riding, driving, or
    gliding, the look swings it all the way round and it stays where it is
    put, beside the machine at any speed; once the look is let be (sooner
    after a glance than a long look aside) and while the machine is under
    way, it eases back behind (`settle_look` in game_session.cc).
    `W`/`A`/`S`/`D`, `E`, and `F` are QWERTY key positions on every layout
    (on Dvorak, `,` `A` `O` `E`, `.`, and `U`); the other letters go by
    what they type.
  - Camp (`moppe/game/camp.hh`): on foot, standing between two trees 2.6 to
    5.2 m apart whose trunks are bare to above head height (birches mostly;
    a spruce keeps its skirts) on nearly level ground, `R` hangs the
    hammock, and `R` beside it takes it down. `E` beside it lies down and
    `E` (or `F`, or `Space`) gets up; lying there the view looks up through
    the crowns, the mouse looks about, `A`/`D` rock it, and after a few
    seconds the hours run forty times as fast (one every three seconds; the
    clock shows over the prompt) until you get up -- holding `S` keeps them
    at their ordinary pace. `Tab` gives a view from outside that the mouse
    walks round. `T` lights a fire a step ahead on dry, fairly level
    ground, and `T` near it puts it out; it throws sparks and smoke and
    lights what stands round it (the renderer's one point light,
    `FrameParams::lamp_*`). The camp is `GameLogicState::m_camp`.
  - Supper: on foot within a couple of steps of a burning fire (one's
    own or the other player's) with mushrooms in the basket, `E` sets a
    pan of up to four on the hearth's stones (`Camp::pan`); they fry in
    nine seconds, keep for half a minute, and then burn. `E` by the pan
    eats them, which mends the walker (less so burnt). The fried ones
    leave the basket (`Basket::used`) but stay picked. `MOPPE_WALK=supper`
    (`tools/record OUT.mp4 supper`) forages three, lights a fire, fries,
    and eats.
  - Fishing (`moppe/game/fishing.*`): on foot facing open water at least
    a third of a metre deep within eleven metres, `E` (controller `A`)
    casts a float (`Line`, in `GameLogicState::m_fishing`). It rides the
    water, drifting with a river's current, until a fish nibbles and then
    takes it under: `E` then strikes, and the fish is fought in, lifted
    out, and goes in the creel; wait a moment too long and it gets away.
    `E` while waiting winds the line in, as does walking off. What bites
    follows the water: perch, roach, and pike in lakes, arctic char in
    the high ones, brown trout and grayling in running water, herring and
    cod in the sea. A fish fries in the pan before the mushrooms (two of
    them beside it) and mends more. `MOPPE_WALK=fish` (`tools/record
    OUT.mp4 fish`) starts on the nearest bank, lands two, and fries one.
    From the boat too: at the helm of one that only floats (no throttle,
    hardly moving through the water), `E` casts the way the view looks,
    over the bow or a side but not back over the long-tail; the seated
    helmsman holds the rod, the float drifts with the boat on a river's
    current, and opening the throttle or getting under way winds the
    line in (`Line::from_boat`, `angler` and `boat_drifting` in game.cc).
    `MOPPE_WALK=boat-fish` (`tools/record OUT.mp4 boat-fish`) takes the
    boat out, drifts, lands two, and drives off.
  - The day turns (`moppe/game/daylight.hh`): the world lies at 60 degrees
    north on the autumn equinox with north along -Z, which makes the sun
    the game was always lit by a quarter-past-nine morning sun, so play
    begins there and the opening is lit as authored. The sun rises due
    east at six and sets due west at eighteen; at night the moon (waxing
    gibbous on the first day, down by half past two) is the key light that
    shades and shadows the world, the stars turn about the pole, and the
    Milky Way shows in the dark hours. `MOPPE_DAY=<minutes>` is how long a
    day takes (48; `0` holds the sun still) and `MOPPE_CLOCK=<hours>` the
    hour it starts at (`MOPPE_CLOCK=21.5` for night). The gazetteer,
    benchmarks, water captures, and a launch that sets `MOPPE_SUNHEIGHT`
    without either keep the fixed sun they were composed under.
  - Scripted walks for captures:
    `MOPPE_WALK=walk|run|jump|tour|turn|forage|camp|haul|bump`
    starts on foot (even with `MOPPE_DEMO=forest`) and drives a deterministic
    script (`forage` starts beside the mushrooms nearest the spawn and picks
    its way from one to the next; `camp` starts by the nearest pair of
    hammock trees under open sky, hangs the hammock, lights a fire, lies
    down, and after a look from outside watches the sky through the
    sleeper's eyes; `haul` hauls the tractor, marked stuck, away on a rope;
    `bump` drives the jeep into the tractor's side twice; `turn`, also
    `tools/record OUT.mp4 turn`, looks round standing, steps round, spins
    twice round, and swings sharply walking and running);
    `MOPPE_WALK_CAMERA=chase|front|side|eyes` picks the view (`side` locks a
    camera beside the figure to judge the gait). Pair with
    `MOPPE_RIDE_CAPTURE_DIR` for consecutive frames.
  - Names and fingerposts (`moppe/game/place_names.*`,
    `moppe/game/fingerposts.*`): each place of the road network
    (`terrain::RoadNetwork`) has a Swedish name rolled from the seed and
    its kind and index (the trailhead a -säter, the landing a -vik, an
    outlet a -kvarn or -fors, a sanctuary a -berget or -varden), computed
    game-side so the cache does not change. At each junction of the
    network, and where a footpath leaves a road, a fingerpost planned at
    activation from the network (`plan_fingerposts`) carries a board per
    way for the nearest places it leads to, with their distances along
    the network, after Sweden's vägvisare: an arrow board as long as its
    lettering, yellow with black letters for cart roads (an enskild
    väg's), brown with white for footpaths, weathered grey for ways no
    longer used, with a border inset along its edge. The lettering, in
    Geomini SemiBold, is Slug text in the world (`Renderer::
    draw_world_text`), exact at any distance.
    The map names the landing (an anchor), the outlets (a mill wheel), and
    the sanctuaries, and draws abandoned ways dashed in grey-violet; the
    chart pins them, and a waymark names the nearest place within 1.2 km.
    `MOPPE_START=fingerpost[:N]` starts on foot before the Nth-nearest
    fingerpost to the trailhead, for captures.
  - Waymarks: a small arrow with a name and a distance marks the other
    player (in their jacket's colour), and the bike, the jeep, the
    tractor, and the helicopter once they are more than some thirty metres off: over the thing when it is in
    view, at the frame's edge pointing the way when it is not
    (`HudState::Waymark`, placed by `add_waymarks` in game.cc). Marks
    whose markers would meet on the screen share one, which lists the
    names and the machines' silhouettes with their nearest-to-furthest
    distance, and opens out as they draw apart, crossfading
    (`group_waymarks` in hud.cc, with hysteresis so it does not
    flicker). On the map, marks that huddle share one label listing
    each with its symbol, set where it covers least, on a leader line
    when it must stand off; spot heights give way to names.
  - The chart and compass: a round chart in the lower left (upper right
    with the diagnostics on, or on a touch screen) shows the map sheet
    round the subject, turned so the way the camera looks is up, in a
    smoked bezel whose ticks and N/E/S/W keep to the world's quarters,
    with the heading over it and the bike, the machines, the camp, the
    companion, and the trailhead as dots (pinned to the rim when off it).
    It shows some 220 m round a walker, widening with speed and from the
    air, and dims with the light (`Hud::draw_chart`, filled by
    `add_chart` in game.cc). The sheet is now painted while the world
    loads; `MOPPE_CHART=none` leaves the chart out.
  - The map: `M` unfolds a sheet of the whole world over the view, `M`
    again looks closer at the 1.8 km round the walker, and a third (or
    `Esc`) folds it away; a small `M Map` keycap beside the chart says
    so. With the map folded, `Esc` asks whether to quit (`Esc` again
    quits, any other key plays on; `Hud::draw_quit_question`). It is painted once per world on the CPU
    (`moppe/game/relief_map.*`: relief shaded from the north-west, forest
    green, brown contours every 10 m, water, rivers by their catchments,
    a kilometre grid, the trail in red dashes) and set on paper with a
    scale, spot heights, and marks for the walker, bike, jeep, tractor,
    companion, camp, and trailhead (`moppe/game/map_sheet.*`). `MOPPE_MAP=whole|near`
    starts with it open, for captures.
  - A wider world: `--world-size <KM>` (5 by default, up to 10) makes the
    world that many kilometres across, with more country rather than the
    same country stretched (`terrain/geological.cc` counts its noise in
    cycles per five kilometres). It keeps the ordinary 2048 samples, so
    the ground is coarser; `--terrain-resolution 4096` buys the grain
    back: generating that takes three and a quarter minutes on an M5 and
    peaks at 5 GB (7.5 GB in the game, which fits a 16 GB machine with
    nothing else heavy beside it), and the world then loads from its
    cache. `MOPPE_FINGERPRINT_COST=1 world-fingerprint 4096 play 123 10`
    says what each stage costs (docs/orogeny-performance.md).
  - The HUD is deliberately one quiet speedometer, the chart, and fading
    prompts. `H`
    toggles the diagnostic overlay (frame rate, stunt and score readings,
    trail map); `MOPPE_HUD=debug` starts with it on, and `MOPPE_HUD=none`
    draws nothing over the picture (film/record's footage).
  - `P` captures the current frame to `screenshots/run-<timestamp>/shot-NNN.png`
    (gitignored, one directory per run; `MOPPE_SCREENSHOT_DIR` overrides the
    base). Start the game, walk around, press `P` at each view worth
    discussing, then point at the latest run directory. Each press also
    logs the camera as a pasteable `shot` line for the opening (`moppe:
    pose: shot shot-NNN eye X Y Z heading H pitch P fov F clock T sun S`),
    gathered in the run directory's `shots.txt`; on the Xbox,
    `tools/xbox-control send 'tap P'` then `log` reads it back.
  - Videos: `tools/record OUTPUT.mp4 [SCENE] [OPTIONS]` records the game
    to an H.264 MP4 -- `tools/record /tmp/camp.mp4 camp` is the camp at dusk
    through to morning. Scenes are `play` (the default: on foot at the
    trailhead, no opening), `ride`, `forest`, `glide`, `walk`, `forage`,
    `camp`, `haul`, `bump`, `opening`, and `river` (a drone's flight down the greatest
    watercourse, from the top of its catchment to the sea: seven minutes,
    or fitted into `--seconds`; with `MOPPE_FLYOVER_SECONDS=440` set,
    `--start` and `--seconds` cut a stretch out of the whole); `--seconds`, `--start`, `--speed N` (a
    time-lapse), `--fps 30|60`, `--size WxH`, and `--clock HOURS` shape it,
    `--sheet` adds a contact sheet beside it, and `--control 'hold W 2'
    'tap T' 'wait 4' 'look 300 0'` drives the game with remote-control
    commands (`moppe/platform/input.hh`) on the video's own clock, so a
    feature can be shown without writing a script in C++. The environment
    passes through (`MOPPE_WEATHER=mist tools/record ...`). The game
    records itself (`MOPPE_VIDEO=<file>`, with `MOPPE_VIDEO_SECONDS`,
    `_START`, `_FPS`, `_SPEED`, `_HEIGHT`; `moppe/game/video.hh`): the
    world advances a sixtieth of a second per rendered frame and the
    frames go down a pipe to ffmpeg, so the video is even however fast the
    machine is. On an M5 it records some forty frames a second at 1080p
    (frames leave the HDR drawable through a lookup table, and the time
    `--start` lets pass goes by at twenty steps a drawn frame); the river
    flight's video ends when the drone lands. A window cannot be larger
    than the screen, so on a 2x laptop `MOPPE_VIDEO_HEIGHT=1080` with
    `--size 1232x693` gives 1080p, and `MOPPE_VIDEO_HEIGHT=1920` with
    `--size 486x864` a vertical 1080x1920. On Linux it runs with no window
    at all, in a headless gamescope of the size asked for (`--window` uses
    the game's own);
    ffmpeg and gamescope come from `nix develop`, which the tool enters by
    itself when ffmpeg is not on PATH (`MOPPE_FFMPEG` names another).
    In play, `V` starts a video of what is on screen and `V` again ends
    it: `clip-NNN.mp4` in the run's screenshot directory.
  - The trailer: `tools/trailer OUTPUT.mp4` records a dozen clips with
    `tools/record` (views of the default world from an opening shot list,
    the scripted walks and rides, and the jeep with a second game riding
    along) and cuts them into a vertical 1080x1920 film of under a minute
    with captions in the bundled face. The clips are kept in
    `build/trailer`, so a change to the cut (`FILM` in the script) takes a
    few minutes; `--fresh` records them again, `--url ""` drops the
    address from the end card, and `--draft` encodes quickly.
  - The film (`film/`, film/README.md): "one day at the trailhead", a
    vertical 40 s film made with fframes (Rust and SVG over the GPU).
    `nix develop -c film/record` has the game record its shots at the
    hours of the day they show; `film/` (in `nix develop .#film`) cuts
    them together under an arc of the sky carrying the sun and the moon
    by the hour: `cargo run --release -- render -o moppe-day.mp4`.
  - The opening is authored: `data/opening.txt` lists a few held shots of the
    default world (seed 123, 2048 samples, play), each drifting gently
    (`push`/`truck`/`rise` metres, `pan`/`tilt`/`zoom` degrees, a breathing
    `sway`), joined by cuts or a dip through black (`fade`), with the title
    and a credit set in Slug fading over them (`caption`), ending with an
    `arrival` into the view play begins with, behind the hiker beside the
    parked bike.
    The grammar is in `moppe/game/opening.hh`. Author by framing a view in
    play or `tools/spectate` and pressing `P`; try a list without
    rebuilding with `MOPPE_OPENING=path/to/list.txt` (or
    `MOPPE_ASSETS=<repo>`). Another world plays one generated still of the
    trailhead (`MOPPE_OPENING=generated` forces it); `MOPPE_OPENING=flight`
    flies the old drone route through the planned landmarks,
    `MOPPE_OPENING=river` flies down the greatest river from its divide to
    the sea (`plan_river_flight` in `game/cinematic_flight.*`, low over
    open water and clear of the crowns where trees stand by it), and
    `MOPPE_OPENING=none` begins in play. `Space` skips.
  - Posed stills: `--screenshot` skips the opening unless `MOPPE_OPENING`
    names a shot list, which it then plays, so a list of one long `shot`
    (`hold 60 sway 0`) is a still from any exact pose without walking or
    flying there: `MOPPE_OPENING=/tmp/close.txt MOPPE_HUD=none
    MOPPE_SCREENSHOT_FRAMES=12 ./build/moppe --windowed --window-size
    1280x720 --render-scale 1 --screenshot /tmp/close.png`. The log's
    `moppe: ... at` lines (and `MOPPE_START=fingerpost:N`'s) say where
    things stand to aim at.
  - Hang glider: boost the bike into the air and press `E` once the deploy
    prompt appears. `A`/`D` bank, `W`/`S` select airspeed, and `Space` flares;
    the Trail 90 stays tethered beneath the wing. Press `E` again to drop it
    and reduce wing loading; otherwise touching down folds the wing and
    continues on the bike. On iOS, the mount/dismount corner deploys the wing
    while airborne and drops the attached bike while gliding.
    The wing (`moppe/mov/glider.*`) is lift and drag on a point mass in a
    moving air mass, so a dive buys speed and a pull-up spends it on height;
    a held flare rounds the path out level and brakes until the wing stalls.
    Under it the rigid bike stays a live Box3D assemblage that the wing's
    pull flies (`Vehicle::hang`), so it lands on its wheels with the glide's
    speed and meets trunks on the way; a pilot alone lands running.
  - Modes: `--fullscreen` (the default, except for captures,
    benchmarks, and the browser), `--windowed`,
    `--graphics-quality low|balanced|high`
    - Finished worlds automatically load from and save to a stable cache keyed
      by the complete world recipe. `--world-cache-key <name>` selects an
      additional developer cache namespace;
      `--refresh-world-cache` replaces its selected entry, and
      `--no-world-cache` bypasses finished-world caching for one launch.
      Beneath it, the land (the geology's output, docs/land.md) comes from
      the package's `worlds/`, the cache, or is generated and saved;
      `tools/fetch-land` downloads the published default land
      (`lands.json`), and `tools/publish-land` shares one.
    - `--render-scale <0.25..1>` selects the 3D scene as a fraction of the
      drawable (`MOPPE_RENDERSCALE` is its environment equivalent); the
      scene defaults to half the drawable, temporally upscaled.
      `--scene-megapixels <0..64>` controls the desktop scene-area safety
      cap; zero disables it. An explicit quality preset replaces the
      ordinary graphics baseline.
    - Dynamic resolution (`moppe/render/dynamic_resolution.hh`): play
      holds 60 frames a second by drawing the scene smaller, down to half
      its ordinary width and height, while the GPU cannot keep up, and
      gives the size back when it can. `--target-fps <0..240>`
      (`MOPPE_TARGET_FPS`) names another rate, and `0` never sheds any;
      nor does an explicit render scale, nor a capture, recording, or
      benchmark, whose pictures must not follow the machine's speed.
      `MOPPE_NHAL_TIMINGS=1` reports the scene's size with the passes.
    - Override Boolean graphics features with comma-separated
      `--graphics-enable <names>` and `--graphics-disable <names>` lists.
    - `--window-size WIDTHxHEIGHT` picks the windowed size, and `--inactive`
      keeps a hand-started run behind the active app. Together they profile a
      large surface without taking over the display.
  - Deterministic opening video: `tools/capture-cinematic
    /tmp/opening.mp4` (it stops when the opening ends; a second argument
    caps the seconds). Set `MOPPE_SEED`, `MOPPE_TERRAIN_PROFILE`,
    `MOPPE_CINEMATIC_CAPTURE_FPS`, or `MOPPE_OPENING` to override the
    defaults.
  - Temporal-stability verification of the riding experience:
    `tools/ride-judge /tmp/ride-judge` captures a deterministic autopilot
    ride as consecutive frames (`MOPPE_RIDE_CAPTURE_DIR`, with `_START` and
    `_FRAMES` overrides), encodes `ride.mp4`, and -- when `GEMINI_API_KEY`
    is set -- asks a video-capable model to rate whether trees morph or
    restructure in motion. Still frames cannot verify this; only video (or
    a human) judges temporal behaviour.
  - Tree laboratory: `tools/tree-lab [--windowed]` rides the real game on a
    rolling plain with the forest replaced by a spruce, a birch, and a tall
    spruce ahead of the spawn (`MOPPE_TREE_LAB=1` with `--uplift-years 0`).
  - Spectator: `tools/spectate [--windowed]` (`MOPPE_SPECTATOR=1`) starts
    a free camera with no rider in the world's densest conifer stand: WASD
    moves (by QWERTY position, so any layout works), Space/Tab
    rise and sink, and the captured mouse or the arrow keys look around; L
    lets the mouse loose (e.g. for a ⌘⇧5 window recording) and takes it
    back.
  - Temporal inspection cameras: `MOPPE_PAN=<seconds>` stands at the rider
    and sweeps the view left and right; `MOPPE_ORBIT=<radius>,<height>,
    <seconds>` circles the loneliest tall tree near the rider. Both, and
    `MOPPE_RIDE_CAPTURE_DIR` captures, advance exactly 1/60 s of world time
    per rendered frame so captured motion is even.
  - Representative still survey along the cinematic drone route:
    `tools/capture-terrain-survey /tmp/terrain-survey 12`. This writes the
    individual frames, a contact sheet, and the deterministic capture settings.
  - Frozen landscape gazetteer from gameplay, habitat, landform, freshwater,
    coast, and aerial viewpoints:
    `make gazetteer GAZETTEER_OUT=/tmp/moppe-gazetteer`. Unlike the route
    survey, this composes each camera directly over one finished world without
    advancing a demo. It writes named PNGs, dimensional `gazetteer.csv`, a
    contact sheet, and an HTML atlas. The four `grass-gradient-*` frames are
    deliberately uncomposed diagnostics: five-metre-high views across the
    world's most ordinary open turf, headed sunward, crosslit, antisun, and
    steeply down, so the whole grass representation gradient (blades,
    clumps, canopy material) is inspectable at known lighting angles.
  - Grass laboratory: `make grass-lab GRASS_LAB_OUT=/tmp/moppe-grass-lab`
    captures the same program on a rolling plain (`--uplift-years 0`) with
    grass cover saturated everywhere the medium can root and no trees
    (`MOPPE_GRASS_LAB=1`), so the grass-gradient frames show the pure LOD
    gradient of the grass system without habitat confounds. Overrides are `MOPPE_SEED`,
    `MOPPE_TERRAIN_PROFILE`, `MOPPE_GAZETTEER_GRAPHICS`,
    `MOPPE_GAZETTEER_WINDOW`, and `MOPPE_GAZETTEER_SETTLE`.
    `MOPPE_GAZETTEER_ENABLE` and `MOPPE_GAZETTEER_DISABLE` forward
    `--graphics-enable` and `--graphics-disable` lists.
  - The forest is the trunk forest (`moppe/nhal/renderer/shaders/world.lisp`):
    tiered spruce and birch with leaf clumps on branches, at mature-stand
    heights with long clear trunks in closed stands. Autumn reaches the
    uplands first: birch there turns, crown tops first and clump by
    clump, along a ramp from green through lime and lemon to gold, amber,
    and for a few russet (`autumn-leaf` and `tree-palette`), drops a leaf
    carpet
    (the `leaves` program adds falling leaves), and the high heath turns.
    Trees are drawn by instanced vertex pulling: a compute pass culls every
    individual and counts each (species, detail tier) class's indirect
    draw, and each class shares one index buffer.
  - The on-foot hiker is Krisjanis, dressed and proportioned after a
    photogrammetry scan of him (pale blue button-down, denim shorts,
    sandals, navy cap, short beard), a rigged model, `models/hiker.blend`: smooth
    subdivided parts, each a separate object with its modifiers and skin
    weights, exported to `moppe/game/figure_mesh.cc` (generated; do not
    edit) and skinned on the CPU from `pose_avatar`'s skeleton
    (`game/figure.*`). `make figure` re-exports it and renders a pose
    lineup to `FIGURE_PREVIEW` (default `/tmp/hiker.png`);
    `make figure-model` regenerates the .blend from
    `tools/figure/build.py`, discarding hand edits. Bone joints and lengths
    must match `avatar_size`.
  - The bike and glider are modelled the same way, `models/bike.blend` and
    `models/glider.blend` (scripts `tools/figure/bike.py`, `glider.py`), as
    rigid assemblies: empties marked `moppe_mesh` export their children in
    the empty's frame, and empties marked `moppe_point` export model-space
    points (axles, pivots, grips, pegs, saddle, harness) that the game
    places parts and the rider by. `tools/figure/export_model.py` writes
    the generated `moppe/game/{bike,glider}_model.{hh,cc}`; `make models`
    re-exports both, the jeep, the helicopter, the tractor, and the Caddy
    (`MODELS_REBUILD=1` regenerates the .blend files; on Linux, `make
    models BLENDER="nix shell nixpkgs#blender -c blender"`). Each paint
    carries its material's whole Principled BSDF (`model_mesh::Paint`:
    metallic, roughness, clear coat, transmission, sheen, emission), set
    in each script's `FINISH` table (the Caddy's `MATERIALS`) through
    `shapes.material`: glossy plastics and bright alloys on the bike,
    flat army paint on the jeep, old enamel and cast iron on the
    tractor, aviation paint and a see-through canopy on the helicopter,
    sailcloth and aluminium on the glider.
  - The Caddy (`moppe/mov/caddy.*`, `models/caddy.blend` from
    `tools/figure/caddy.py`, drawn by `game/caddy_render.*`) is a
    first-series VW Caddy pickup under a fibreglass hardtop on Swedish
    plates, parked by the sea on level ground above the shore the boat
    lies at, clear of the other machines (`park_caddy` in game.cc; the
    log line `moppe: Caddy parked` says where). On foot, `F` beside it drives it and
    `F` steps out of the driver's door; `W`/`S`, `A`/`D`, and `Space` as
    in the jeep. It is a Box3D assemblage like the jeep but a road car:
    front-wheel drive, soft springs, some 140 km/h flat out (its wheels
    may spin faster than Box3D's quarter turn a step). What the mount key
    takes on foot is the nearest vehicle within its reach
    (`GameSession::mount_within_reach`). `MOPPE_DEMO=caddy`
    (`tools/record OUT.mp4 caddy`) has the autopilot drive it.
  - Model materials reach the shaders: each streamed vertex carries a
    `render::Surface` (metallic, roughness, clear coat, sheen), set from
    the exported Principled BSDF by `model_mesh::record`, and the uber
    program shades such vertices with GGX sun highlights, the sky
    reflected (the water's `water-sky`) and blurred with roughness, and a
    clear coat; see-through paints (glass) draw blended with Fresnel.
    Draw lists without a material keep the old shading. Vehicles lend
    their meshes to the sun's near shadow cascade
    (`Renderer::begin_shadow_casters`, the `mesh-shadow` program), so they
    shade the ground and their own insides.
    Ridden, the bike carries the hiker posed by `pose_holding` (hands on
    the grips, feet on the pegs), at life size like the bike; the
    glider's pilot hangs prone at the basebar.
    `MOPPE_DEMO=glide` deploys the wing on the autopilot's first high leap,
    and `MOPPE_RIDE_CAMERA=side|front` locks a capture camera beside or
    ahead of the bike, the glider, or the driven jeep, tractor, or Caddy.
  - The jeep (`moppe/mov/jeep.*`, `models/jeep.blend` from
    `tools/figure/jeep.py`, drawn by `game/jeep_render.*`) waits on a
    verge beside the nearest cart road some 300 m from the trailhead,
    facing along it (`park_jeep`; beside the bike where no road is near).
    Only the Trail 90 and the tractor stand at the trailhead; each other
    machine has a place of its own, logged as `moppe: ... parked`. On foot, `F` beside it drives it (nearer it
    than the bike; the prompt says which) and `F` again steps out; `W`/`S`
    drive and reverse, `A`/`D` steer, `Space` brakes, `Tab` cycles the
    view. It is life size and seats two: in multiplayer, `F` beside the
    other player's jeep takes the passenger seat (`M_PASSENGER`), and the
    jeep's pose travels in the companion snapshot. Like the bike it is a
    Box3D assemblage: a chassis of two box hulls on four sphere wheels,
    each on a wheel joint that springs, drives, brakes, and (in front)
    steers it, over its own streamed height-field patch and trunk
    capsules (`mov::GroundPatch`, which the bike shares). Its frame is the world's, x to the driver's left.
    `MOPPE_DEMO=jeep` has the autopilot drive it (`tools/record OUT.mp4
    jeep`), and `MOPPE_RIDE_ALONG=1` takes the other player's passenger
    seat as soon as they appear, for captures.
  - The mountainbike (`models/mountain_bike.blend` from
    `tools/figure/mountain_bike.py`, drawn by `game/mountain_bike_render.*`)
    is a classic early-nineties hardtail in deep green and cream at life
    size, left at the top of the trail's climb facing down it, or at the
    highest lookout near the trailhead (`park_mountain_bike`). On foot, `F`
    beside it rides it and `F` gets off; `W` pedals (the crank turns at a
    cadence through the gears, still while freewheeling), `S` brakes,
    `A`/`D` steer, and `Space` bunny-hops. It is the rigid bike's
    assemblage on another `mov::BikeRig` (`mountain_bike_rig` in
    `mov/rigid_bike.hh`; the Trail 90 is `trail90_rig`), and the session
    holds it as a second `mov::Vehicle` (`GameSession::mountain_bike`,
    `M_MOUNTAIN_BIKE`). `MOPPE_DEMO=mountain_bike` (`tools/record OUT.mp4
    mountain_bike`) pedals it on the autopilot; the other player sees it
    (companion wire version 7).
  - The Long John (`models/long_john.blend` from `tools/figure/long_john.py`,
    drawn by `game/long_john_render.*`) is Krisjanis's homemade cargo
    bike: a donor 26-inch rear triangle in worn red welded to his raw-steel
    box and hoop, a 20-inch front wheel 1.894 m ahead, cable steering,
    38/18 single speed. Its geometry comes from
    `tools/figure/long_john_source.py`, the procedural twin solved from
    his photos, run without weld beads or chain links. It stands outside
    the nearest farmhouse (`m_places`), or at a lake outlet or the
    landing, within 3 km (`park_long_john`); `F` by its saddle rides it, `W` pedals (with its
    hub motor's help, some 25 km/h), `S` brakes, `Shift` skids. It rides
    on `mov::long_john_rig` (`BikeRig::front_wheel_radius` gives it its
    small front wheel), as `M_LONG_JOHN`; `MOPPE_DEMO=long_john`
    (`tools/record OUT.mp4 long_john`) pedals it, and the other player
    sees it (companion wire version 12).
  - The boat (`moppe/mov/boat.*`, `models/boat.blend` from
    `tools/figure/boat.py`, drawn by `game/boat_render.*`) is Krisjanis's
    hard-chined plywood skiff, to his figures: 3.93 m long, 0.95 m wide,
    0.4 m deep, of Latvijas Finieris film-faced birch under glass and
    epoxy, lately painted white (the dark film shows where it is worn),
    its sheets joined by CNC-cut serpentine joints, with a blunt narrow
    bow, a carrying handle and a blue painter there, and two moulded seats
    in tandem. Its long-tail (a Honda GX200, 4.1 kW, under a GX160's
    sticker as on his) hangs on a gimbal centred on the transom: a grey
    shaft to a red three-blade propeller behind a red sickle skeg, and a
    long black handle with an hour meter, offset to starboard.
    It lies moored at
    the shore of the sea or a big lake nearest the spawn (`park_boat` in
    game.cc). On foot, `F` beside it takes the helm and `F` steps ashore
    over whichever side has the shallowest water; `W`/`S` drive and
    reverse, `A`/`D` swing the tail to steer, and `Space` (the right
    trigger) holds the governor tied back. The engine's thrust is the
    propeller's grip standing and its power under way, and the hull's
    hump of wave-making drag is added whole, so it does his figures:
    some 24 km/h alone (29 tied back) and 15 two-up, stuck at the hump
    (26 tied back). The boat and engine weigh 70 kg. In multiplayer, `F`
    beside the other player's boat while they are at its helm takes the
    fore seat (`M_PASSENGER`, `m_seat_boat`), and the helm's game carries
    the passenger's 80 kg (`Boat::set_passenger`). Run up on the land,
    `F` takes up a rope to its bow handle and walking drags it back to
    the water (`Machine::boat` in rescue.hh); the helicopter can hook it
    too. The water acts on the hull
    panel by panel (pressure at depth, ram and suction, skin friction, all
    relative to the current, so rivers carry it), which floats, heels,
    trims, and planes it; the propeller drives only while in the water,
    in the stern wave that rises round it at speed. The helmsman's 80 kg
    comes aboard and steps off with them. The sea heaves under waves
    (`moppe/terrain/sea_waves.hh`: six Gerstner trains before the wind,
    a metre or so trough to crest) that move the water program's surface
    itself and break white in the shallows; `water_surface_at` reads the
    same waves on the CPU at the game's time, so the boat rides, the
    walker wades, and the wake lies on what is drawn (`still_water_at` is
    the level without them). They scale by the sheet's wave lane squared
    and die over the last four metres of depth, so lakes stay all but
    calm and rivers flat. The boat is moored on the nearest open shore
    of the sea (`MOPPE_BOAT=lake` takes the nearest big lake instead). It draws a Kelvin wake, a
    rooster tail, and bow spray; `MOPPE_DEMO=boat` (`tools/record OUT.mp4
    boat`) has the autopilot take it out (companion wire version 9),
    weaving over open water clear of the shallows; `MOPPE_BOAT=river`
    moors it on the greatest river some 1.2 km above its mouth
    (`MOPPE_BOAT_RIVER_M`) and the autopilot follows the river down to
    the sea. Besides `side|front`, `MOPPE_RIDE_CAMERA` films the boat as
    `drone`, `aerial`, `orbit`, `top`, `fpv`, `hull`, `stern`, `helm`,
    or `flyby` (each with its own lens; `MOPPE_RIDE_CAMERA_ANGLE` swings
    the following ones round), and `tools/boat-promo OUT.mp4` records
    and cuts a 50 s 1080p promo from them (clips kept in
    `build/boat-promo`, so recutting `FILM` takes a minute).
  - Skids, on both bikes: riding, `Shift` (the controller's left trigger)
    stamps on the rear brake. The rear wheel locks, its tyre's grip drops
    to a sliding friction, and the bars swing the tail out about the
    front wheel, so a held skid with the bars over brakes the bike round
    into a new heading, throwing a fan of dirt; let go, the tyre grips
    again gradually (`RigidBikeControls::skid`, `Vehicle::set_skid`).
  - Wheelies, on the Trail 90: riding, hold `R` (the controller's left
    shoulder) to lean back and open the throttle: the rider's weight goes
    back, the traction control lets the front come up, and the throttle
    rolls on progressively, so the drive lifts the front wheel and the
    Box3D bike pivots on the rear (`RigidBikeControls::wheelie`). Up there
    the rider balances it with their body as far as it reaches; more
    throttle lifts it, `Shift` (covered, not stamped on) brings it down,
    and a throttle held open rears it past the vertical to fall over
    backwards. The rider hangs back off the saddle on straight arms
    (`pose_rider` in vehicle_render.cc). The ground covered on the back
    wheel counts over the speedometer and scores two points a metre past
    five (`m_wheelie_m` in game_session.cc); `MOPPE_DEMO=wheelie`
    (`tools/record OUT.mp4 wheelie`) rides them on the autopilot. The
    classic point mass has none (companion wire version 11).
  - The bike (`models/bike.blend` from `tools/figure/bike.py`, drawn by
    `game/vehicle_render.*`, ridden as `M_BIKE`) is a 1972 Honda CT90
    Trail 90 at life size, which Krisjanis named as the classic to clone:
    Candy Orange pressed body and swingarm, silver shroud over the spine,
    the engine lying flat under it, the exhaust up the right side in a
    corrugated shield to a perforated guard at seat height, a big chrome
    rack, telescopic fork in gaiters, high braced bars with mirrors, 17-
    inch spoked wheels 1.22 m apart (`trail90_rig`: 5.2 kW, 175 kg with
    its rider, some 80 km/h flat out). Only the jump jets under it are
    no Honda's.
  - The helicopter (`moppe/mov/helicopter.*`, `models/helicopter.blend`
    from `tools/figure/helicopter.py`, drawn by `game/helicopter_render.*`)
    stands on a pad of its own a few hundred metres from the trailhead,
    the levellest open ground there, raised where it can be, with no
    tree within reach of its rotor (`park_helicopter` in game.cc).
    On foot, `F` beside it takes the pilot's seat; the rotor spools up for
    a few seconds before it will lift. `W`/`S` fly ahead and back, `A`/`D`
    turn, `Space` (right trigger) climbs, and `Shift` (or holding `E`,
    controller `A`) descends; `F` steps out once it is down. It is one
    Box3D body on skids whose rotor thrust acts along its up, so it goes
    where it leans, but a flight computer flies the attitude and the
    collective: let go and it holds its height and comes to a hover, low
    and fast it keeps clear of the ground ahead, and descending it slows
    for the touchdown. `MOPPE_DEMO=helicopter` (`tools/record OUT.mp4
    helicopter`) has the autopilot take it up, hook the jeep parked
    beside it, and fly it off. Flying over a machine (the bike, the
    mountainbike, the jeep, the tractor, or the Caddy) with the line in
    reach of it -- some metres up and up to eight to one side, no aiming
    needed -- `F` hooks it on a line under the helicopter
    (`moppe/game/sling.*`, `GameLogicState::m_sling`,
    `GameSession::hook_within_reach`), and `F` again lets it go, from any
    height, before `F` steps out. The line is a stiff spring from the
    hook to a ring over the load's centre of mass, pushed each step on
    both Box3D bodies; the flight computer lifts and leans against its
    pull (`Helicopter::set_load`), so a heavy load drags it down and
    slows its climb, and the Caddy, slung, damps its swinging and turns
    its nose the way it is carried (`Caddy::set_slung`).
    `MOPPE_RIDE_CAMERA=side|front` stands beside the hanging load. The
    other player sees the load fly but not the line.
  - The tractor (`moppe/mov/tractor.*`, `models/tractor.blend` from
    `tools/figure/tractor.py`, drawn by `game/tractor_render.*`) is a
    little red farm tractor of the old kind, open, with a pan seat
    between big ribbed rear wheels. It stands out on the open ground
    ahead on the bike's other side from the jeep (`park_tractor` in
    game.cc). On foot,
    `F` beside it climbs up to drive and `F` again climbs down; `W`/`S`
    drive and reverse, `A`/`D` steer, `Space` brakes, and off the
    throttle it comes to a stand by itself. Its frame's origin is on the
    ground between the axles. Like the jeep it is a Box3D assemblage, a
    rear-heavy chassis of three box hulls on four sphere wheels, but only
    the big rear pair drive it: hard and slowly, the engine governed to
    some thirty kilometres an hour, easing off if its nose lifts so that
    it does not rear over. The stack smokes while someone sits at the
    wheel. The other player sees it and its driver (companion wire
    version 5); `MOPPE_DEMO=tractor` (`tools/record OUT.mp4 tractor`)
    has the autopilot drive it.
  - Getting unstuck (`moppe/game/rescue.*`): each machine stands in a
    Box3D world of its own, so they meet as a few spheres each instead,
    and machines that meet -- the other player's too -- are knocked apart
    like bumper cars by equal and opposite blows, each game pushing only
    its own (`find_bumps`, `GameSession::push_machine`). A machine whose
    driver holds the throttle three seconds without its getting three
    metres on is stuck (`Trouble`, in `GameLogicState::m_trouble`);
    driven and stuck, it heaves itself the way the driver wants every
    second and a half, harder each time. Left stuck, `F` beside it takes
    up a rope to its nearer end instead of getting in: walking away hauls
    it out after the walker, slowly, walking at it pushes it, and `F` lets
    go; moved three metres it is free and `F` gets in again. `tools/record
    OUT.mp4 haul` and `bump` show both.
  - Places at the ends of the roads (`moppe/game/places.*`, planned
    from `GeneratedWorld::roads()` when a world activates, before the
    trees and rocks, which keep out of their clearings): the landing has
    a plank jetty out to water deep enough to moor, and the skiff lies
    alongside it (`park_boat` moors it there before trying the nearest
    open shore); each peak sanctuary has a stone fire platform inside a
    low enclosure wall, with paving out through its gap down the
    footpath, tumbled the longer it has been left (`RoadPlace::
    abandoned_years_ago`). On foot at the platform, `T` lights its
    beacon and `T` puts it out (`GameLogicState::m_beacons`); it burns
    as a blaze, can be cooked on, burns for the other player too
    (companion wire version 13), and from afar shows as a glow by night
    and a column of smoke by day (`draw_beacon` in camp.cc), from the
    sanctuaries on its sightlines and anywhere else that sees the
    summit. Each lake outlet has a farm, `models/farm.blend` from
    `tools/figure/farm.py` (`make models`): a timber watermill with its
    undershot wheel turning in the outlet stream, a Falu-red house, and
    a barn, left as stone plinths once long abandoned; with a landing
    stage on the lake to fish from. By each place where two trees take a
    hammock, an old fire ring takes the camp's fire. `MOPPE_WALK=
    landing|sanctuary|farm` starts on foot there, and `MOPPE_BEACONS=lit`
    starts every beacon burning, for captures; `moppe: places:` lines
    say where everything stands.
  - Mushrooms (`moppe/game/mushrooms.cc`) grow in arcs around a few
    percent of the trees, more near the trailhead: chanterelles and funnel
    chanterelles under spruce, porcini under both, fly agaric under birch,
    drawn larger than life so they show over the forest floor. On foot,
    `E` (controller `A`; `F` too when the bike is out of reach) picks the
    one at hand into the basket the hiker carries in their left hand, which
    fills visibly; fly agaric is poisonous and stays. The picked indices
    live in `GameLogicState::m_basket` (`game/basket.hh`), and the ground
    follows them, meshed in 16 m tiles near the camera.
  - Vegetation (`map::analyze_vegetation` in `moppe/map/surface.cc`) is
    not drawn as zones. Hidden site fields -- soil water, soil depth, peat,
    cold (height, north aspect, frost hollows), wind exposure, and bedrock
    (a limestone sheet over a crystalline basement, cut through where
    erosion went deeper than it) -- meet each species' tolerance curve, and
    the species compete: spruce, Scots pine (`ForestForm::pine`), birch,
    and fell birch for the canopy; in the light they leave, lichen,
    heather and crowberry, bilberry, sphagnum, dwarf birch, sedge, and the
    alvar's turf on limestone pavement. The heath, the bog, the alvar, and
    the fjäll are where those overlap. Each species' share is a reading;
    the forest plan draws each tree's species from the canopy's shares
    (shrinking spruce to krummholz among the fell birch, bog pines to a
    person's height; the fell birch is its own form,
    `ForestForm::fell_birch`, a low tree of three to six white stems
    that sweep out low from one base and bend up at a knee, each with a
    forked twig and small clumps at the tips), and the field
    layer reaches the ground and grass as
    two sheets at texture slots 5 and 4. In ruska the fjäll's dwarf birch
    goes gold and its bilberry crimson. Two readings come from the rock
    itself (`bedrock_exposure`, `spring_fen_abundance`, a sheet at slot
    12): granite whalebacks where the ice scoured the basement bare on
    knolls and ridges with no soil left (pink-grey, polished, jointed,
    lichen-crusted; only pine roots there), and the spring fen, bright
    green with ochre runnels, below the limestone's foot on a hillside,
    where the water that sank through the sheet comes out over the
    basement in a line of seeps. The log line `moppe: vegetation:` says
    how much dry ground each species leads, and how much is bare rock
    (some 4% and 2% fen in the default world, near
    (4023, 703) and (4096, 683) east of the trailhead).
  - Disturbance (`moppe/game/disturbance.*`, `game::disturb_surface`)
    is the vegetation's last hidden field: what has happened to the
    ground. After the roads are planned over the undisturbed vegetation
    and built into the ground,
    lightning fires run through the dry pine country (downwind and uphill,
    stopped by water, bog, and bare rock, up to some 260 years old), each
    place the roads join grazes its pastures while it is lived in (the
    sanctuaries their upland slopes) and is walked back into once it goes
    quiet, and roads in use keep their verges cut. That gives three
    readings, `grazing_pressure`, `stand_recovery`, and `burn_recency`, and
    the vegetation is read again over them: fireweed (`fireweed_abundance`)
    takes a fresh scar, birch comes first after it, pine later, spruce
    last, grazing leaves heath on poor ground and turf on good, and the
    forest plan grows no older trees than the ground has had time for,
    while the stand a fire killed still stands over its burn as snags
    (`ForestForm::snag`: grey, bare, broken-topped, charred at the foot,
    and solid), fewer the longer ago it burnt. The
    ground and grass read them from a third sheet (texture slot 11):
    fireweed wine-red with white seed, char on a young burn, cropped
    pasture. The log line `moppe: disturbance:` counts the fires and
    pastures; in the default world the youngest fire, ten years old, is
    at about (2760, 1925), and a living sanctuary's pasture lies some 1.3 km
    north of the trailhead.
  - Boulders (`moppe/game/boulders.cc`, the `boulders` program)
    are planned from the surface fields when a world activates -- talus,
    scree, shore cobbles, upland erratics -- drawn as faceted flat-shaded
    rocks, and the larger ones collide; `--graphics-disable boulders` hides
    them but keeps their colliders. Running water lays its own
    (`game/river_stones.*`) by what its spate can move, a Shields
    competence of depth times grade: keystone steps and boulders in steep
    headwaters, cobbles on riffles and on bars inside bends and below
    confluences, nothing where it is slow, the odd great rock; the same
    competence makes the bed and the bars beside it (`read_river_bed`, in
    the ground's deposition and shore lanes) sand or cobbles. Where a
    stream is shallow and narrow a row of solid stepping stones crosses
    it, some 80 in the default world (the log names the nearest); the
    walker steps across dry, and wading -- slower the deeper, pushed by
    the current -- and riding through rivers are held back
    (`water_surface_at` in walker.hh).
  - The ground (`ground-surface` in world.lisp; docs/nhal.md) is surfaced
    from the world's fields: turf, heath and stony fell above it, forest
    litter, soil, cobbles, sand along the water and silt on its bed, bedded
    rock, the trail, and snow on the highest level ground. Everything finer
    than the terrain's samples is read at four scales from one generated
    tiling sheet (`moppe/render/ground_detail.hh`: grain, its slope, a
    mosaic of stones), which also tilts the lit normal. The gazetteer's
    `trail-scenic-turn`, `highland-vista`, `eroded-slope`, `coastline`, and
    `confluence` frames show most of it.
  - Feature-targeted water capture: `tools/capture-water /tmp/mouth.png mouth`.
    Feature names are `stream`, `river`, `confluence`, `mouth`, `waterfall`,
    and `lake`;
    set `MOPPE_SEED` and `MOPPE_TERRAIN_PROFILE` for reproducible comparisons.
    `MOPPE_WATER_VIEW=bank` stands on the bank at eye height instead. Running
    water stands and moves as its discharge has it (`terrain/watercourse.hh`:
    a spate of 2.5 m3/s from each square kilometre of catchment, of which
    it carries four tenths on the day seen, a steady profile
    through the terrain's own cross-sections, critical over sills and
    backed up behind them), so a brook is a stair of pools and a trunk river
    drowns the same sills. Rivers stream along that current (flow-mapped
    ripples, foam where the water descends fast, drifting specks), and water
    reflects the scene by a screen-space march (docs/nhal.md). Each river
    has a bed, cut into the land before the trails are laid
    (`terrain/channel.hh`, docs/land.md): its long profile graded through
    the hollows and sills erosion leaves, and a parabolic bed with banks
    as wide as its water makes it -- where it falls gently, bars and a
    planed strand along its banks, most on the inside of a bend, and a cut
    bank on the outside; where steep, whatever banks its valley gives it --
    running on from cell to neighbouring
    cell where it steps diagonally so that no stream pinches shut at a
    corner. To judge the flow, capture consecutive frames:
    `MOPPE_SCREENSHOT_FRAMES=100000 MOPPE_RIDE_CAPTURE_DIR=/tmp/w
    MOPPE_RIDE_CAPTURE_START=60 ./build/moppe --water-screenshot river
    /tmp/unused.png` writes `/tmp/w/ride-NNNN.png` at 30 fps. To find where
    rivers run dry, `cmake --build build --target river-census` (with
    `-DMOPPE_BUILD_DEVELOPER_TOOLS=ON`) and `./build/river-census
    /tmp/rivers [RESOLUTION SEED KM]` walks every river reading the water
    as the renderer does: the dry length and its gaps, how the reaches
    begin and end, the greatest river's gaps, and a map (~2 minutes for the
    default world, the geology kept for the next run).
  - Automated screenshots and graphics benchmarks keep their windows
    inactive, so repeated captures do not steal focus from the current app.
  - Partitioned hot-feature GPU benchmark (32 configurations by default;
    prefix with `MOPPE_DEMO=1` so it measures a ride rather than the spawn
    point):
    `./build/moppe.app/Contents/MacOS/moppe --graphics-benchmark /tmp/gpu.csv
    --windowed --seed 123 --terrain-quality fast`. Development overrides are
    `--benchmark-prelude`, `--benchmark-settle`, `--benchmark-frames`,
    `--benchmark-partition detailed` for the 128-configuration refinement, and
    `--benchmark-pass-timing` for pass columns.
    Analyze a completed CSV with
    `tools/graphics-benchmark-analyze INPUT.csv [OUTPUT_DIR]`. The
    measurement lived in the retired Metal renderer; until the NHAL renderer
    implements `write_benchmark_results` from its pass timings, the replay
    runs but writes nothing.
  - Two players (`moppe/game/companion.hh`): `--multiplayer` on two
    machines of one network (`MOPPE_MULTIPLAYER=1`) puts both players in
    one world, and `--join <host[:port]>` names the other machine where
    broadcasts do not carry. Neither serves: each simulates its own player
    and sends its poses thirty times a second over UDP port 27960
    (`game/companion_link.*`), and the other draws them a moment behind
    (some sixty milliseconds on a steady link, more on an uneven one;
    `CompanionTrack`) as a second hiker, bike, jeep, or glider, with their
    camp;
    picked mushrooms and the hour of the day are shared (the clock that
    is ahead leads), and nothing collides between the two. So that they
    can be told apart, the player with the greater number wears the green
    jacket and orange cap (`game/outfit.hh`; the paints are swapped by name
    in `game/figure.cc`). Both must launch
    the same world, so without `--seed` multiplayer plays seed 123. Two
    games on one machine find each other too (the second takes a free
    port); `MOPPE_MULTIPLAYER_PORT` picks the port. A firewall must let
    UDP 27960 in, or each side must `--join` the other.
  - Weather is authored, not simulated: `MOPPE_WEATHER=clear|mist|drizzle`
    (`moppe/game/weather.hh`) sets the sky, fog, and light, and the NHAL
    renderer draws valley mist and a fine rain for it.
  - Dev env vars: `MOPPE_ASSETS=<repo>` (asset override), `MOPPE_DEMO=1`
    (autopilot for screenshots; use `MOPPE_DEMO=forest` to start the same
    rider at the world's selected forest-floor site),
    `MOPPE_SUNHEIGHT=<0..1>` (a fixed sun: 0.5 on the horizon, 1 overhead),
    `MOPPE_NOSHADOW=1`,
    `MOPPE_CONTROL_FILE=<path>` (a remote-control timeline, as
    `tools/xbox-control` writes; see `moppe/platform/input.hh`),
    `MOPPE_FPS_REPORT=1` (the frame rate and simulation steps),
    `MOPPE_FRAME_CLOCK=host` (step by the loop's time rather than the
    display's; docs/nhal.md's frame pacing),
    `MOPPE_RENDERSCALE=<0.25..1>`, and `MOPPE_SCENEPIXELS=<megapixels>`
    (the scene-resolution budget; `0` restores the point-relative rule
    alone)
  - The desktop scene resolution is the smaller of the point-relative rule and
    `scene_megapixel_budget`, so a display attached at 1x — a 7680x2160 one
    asks for twice a 4K frame — costs resolution rather than frame rate.
- Renderer smoke test: `./build/moppe-testbed` (configure with
  `-DMOPPE_BUILD_DEVELOPER_TOOLS=ON`)
- NHAL, the renderer's hardware layer (Metal 4, Direct3D 12 for Xbox,
  Vulkan on Linux, WebGPU in the browser; `docs/nhal.md`): `cmake --build
  build --target nhal-demo`, then `./build/nhal-demo`, or
  `./build/nhal-demo --capture /tmp/nhal.png --frames 30` to write one frame
  without taking focus. On Xbox: `nix build .#nhal-xbox` and
  `UWP_DEVICE_URL=https://xbox.whale-justice.ts.net nix run .#deploy-nhal-xbox`;
  the app writes `nhal.txt` (and one captured `nhal.tga`) to its LocalState,
  readable through Device Portal's file API.
- Linux (NHAL on Vulkan, in an SDL3 window): `nix develop`, then the usual
  configure and `cmake --build build --target moppe`, and `./build/moppe`
  (the same controls as the Mac). `./build/nhal-demo --capture
  /tmp/nhal.tga --frames 30` renders the demo without a window.
  `MOPPE_VULKAN_VALIDATION=1` turns on the Khronos validation layer. The
  device is `moppe/nhal/vulkan/`; see docs/nhal.md. With no GPU or display
  (an Amp orb, set up by `.agents/setup`), Mesa's lavapipe
  (`VK_DRIVER_FILES`) and SDL's offscreen video
  (`SDL_VIDEO_DRIVER=wayland,x11,offscreen`) run the real game headless on
  the CPU: `--screenshot`, captures, and `tools/record` all work, slowly.
- Browser (NHAL on WebGPU, the SDL host built by Emscripten; docs/web.md):
  `make web-serve` builds `build-web/moppe.html` and serves it at
  <http://localhost:8080> (`make web` only builds; `brew install emscripten
  bun`). The page's address carries the command line and switches:
  `?MOPPE_FPS_REPORT=1&MOPPE_NHAL_TIMINGS=1&args=--graphics-quality+low`,
  reporting to the browser's console. The device is `moppe/nhal/webgpu/`,
  the page `moppe/platform/web/`. The page has one thread, so it starts
  from baked land and never generates by default. On a phone the page
  draws touch controls (`touch.js`: a stick under the left thumb, look
  under the right, and a button for each thing the game says can be done
  just now, `platform::offer_actions`). WebGPU validates what the
  other devices let pass, so a renderer change is worth a look at the
  console there. Every push to master is published at
  <https://moppe.swa.sh> (a timer on swa running `tools/publish-web`) and
  on GitHub Pages (`.github/workflows/pages.yml`).
- World generation is bit-identical across platforms (docs/determinism.md):
  the project builds with `-ffp-contract=off`, generation code calls
  `moppe::cr::` (correctly rounded CORE-MATH) instead of `std::sin`,
  `std::pow`, `std::hypot` and the like, and `Vec3`'s reductions are spelled
  out. `world-fingerprint` (a developer tool) hashes every stage so two
  machines can be diffed line by line.
- Linux package and Steam Deck: `nix build .#moppe` (x86_64-linux) builds
  the game with its own Mesa Vulkan driver for hosts without NixOS's;
  docs/steam-deck.md installs it on a Deck (`nix profile install
  github:mbrock/moppe#moppe`, a Steam shortcut) and tests it Deck-like on
  chapel (an RDNA2 GPU) under headless gamescope at 1280x800.
- Windows: `nix build .#moppe-windows` (x86_64-linux) cross-builds the
  desktop game (`-DMOPPE_WINDOWS_DESKTOP=ON`: the SDL host on Vulkan,
  through volk) with Zig's Clang for MinGW-w64 into
  `result/moppe-windows-x86_64.zip`, a `moppe.exe` importing only
  Windows' own DLLs with its assets and land built in
  (`moppe/platform/sdl/windows.nix`). In an orb it runs under Wine
  (`wineWow64Packages.stable`, Xvfb, `WINEDLLOVERRIDES="mscoree,mshtml="`)
  on lavapipe.
- Releases (`.github/workflows/release.yml`): pushing a `v*` tag builds
  the Linux tarball (`moppe-steamdeck`), the Windows zip and its lone
  `moppe.exe`, and the macOS app (Developer ID signed, notarized, and carrying
  Sparkle updates; docs/mac-releases.md) and publishes them as a GitHub release;
  running the workflow
  by hand keeps them as artifacts. Each executable carries the assets and
  the default land (`-DMOPPE_EMBED_ASSETS=ON` with `MOPPE_LANDS_DIR`;
  `tools/pack-assets` compresses them, and the SDL host's `asset_path`
  unpacks one into the cache's `assets/` folder when the disk has none).
- The game's core on Xbox, before NHAL renders it: `nix build
  .#moppe-core-xbox` and `UWP_DEVICE_URL=https://xbox.whale-justice.ts.net
  nix run .#deploy-moppe-core-xbox -- --hold 600` (package
  `Moppe.core-xbox`) build the whole game except a renderer for x86_64 UWP
  from CMakeLists.txt (`moppe/platform/uwp/`). It runs the core probe
  (`moppe/game/core_probe.hh`): generates the default world from scratch,
  reloads it from the world cache, walks and rides it, then drives
  `game.cc` against `tests/recording_renderer.hh`, writing
  `LocalState/report.txt` (phase timings, peak memory) and `log.txt`; the
  screen turns green on a pass. Read them with `curl
  "$UWP_DEVICE_URL/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=<PFN>&path=%5C%5CLocalState&filename=report.txt"`.
  `LocalState/environment.txt` (`NAME=VALUE` lines) stands in for the
  environment UWP lacks; game code reads switches through
  `moppe::environment` (`moppe/environment.hh`), never `getenv`. For
  comparison, `cmake --build build --target moppe-core-probe && ./build/moppe-core-probe`
  runs the same probe on macOS. Incremental Xbox builds: `nix develop
  .#moppe-core-xbox`, then `cmake -B build-xbox -G Ninja
  -DBUILD_TESTING=OFF` and `cmake --build build-xbox`.
- iOS (simulator): `cmake -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS
  -DCMAKE_OSX_SYSROOT=iphonesimulator` then build the `moppe-ios` target
  with `CODE_SIGNING_ALLOWED=NO`
- tvOS (simulator): `cmake -B build-tvos -G Xcode -DCMAKE_SYSTEM_NAME=tvOS
  -DCMAKE_OSX_SYSROOT=appletvsimulator` then build the `moppe-tvos` target
  with `CODE_SIGNING_ALLOWED=NO`
- Xbox Series (Developer Mode, via nixbox): `nix run .#deploy-moppe-xbox`
  builds the game with the NHAL renderer on Direct3D 12, installs it under
  the shared console lease, and launches it (see docs/nhal.md);
  `nix run .#deploy-moppe-core-xbox` runs the headless core probe.
  `tools/deploy-xbox [DEPLOY ARGS]` (or `make xbox`) is the same deploy
  with the default 2048 world baked on the Mac (`tools/bake-world`, kept
  in `~/Library/Caches/Moppe/baked/` until the terrain code changes) in
  the package, so the console starts in seconds instead of generating
  for minutes; the pure `nix run` ships none and generates a 1024 world.
  `tools/xbox-control send 'tap Space' 'tap F' 'stick 0 1 1'`, `shot`, and
  `log` drive the running game, take its screen, and read its log.
- Apple TV (build, install, launch): `make tv`
  - Pair the Apple TV in Xcode's Device Hub first. Overrides:
    `MOPPE_TVOS_DEVICE`, `MOPPE_TVOS_TEAM`, `MOPPE_TVOS_CONFIGURATION`, and
    `MOPPE_TVOS_BUILD_DIR`.
- iPhone (build, install, launch): `make phone` (Release by default, on
  the first connected iPhone). Touch: left thumb walks/steers, dragging on
  the right looks around, the bottom-right button jumps or boosts.
  - The paired phone must be unlocked and reachable; its Personal Hotspot
    works when other Wi-Fi networks isolate clients.
  - Overrides: `MOPPE_IOS_DEVICE`, `MOPPE_IOS_TEAM`,
    `MOPPE_IOS_CONFIGURATION`, and `MOPPE_IOS_BUILD_DIR`.

## Visual Validation: Look for the GPU First
- Agents run either on the developer's machines (a Mac, or a Linux runner
  with a GPU) or in Amp orbs: Linux VMs with no GPU and no display, where
  Mesa's lavapipe (llvmpipe) draws every frame on the CPU (see the Linux
  entry above). Before choosing how to see a change, find out which:
  `uname`; on Linux, no `/dev/dri/renderD*` or a `VK_DRIVER_FILES` naming
  `lvp_icd` means the CPU, and the game's first line says for certain
  (`moppe: NHAL renderer on Vulkan (llvmpipe ...)`).
- On the CPU, frames are dear: a 640x360 still of the cached default world
  takes about half a minute, a 1280x720 one about a minute, and two
  seconds of 640x360 video nearly two minutes, so a scene's default
  `tools/record` would take hours. There, as a rule of thumb:
  - Settle what can be settled without pixels: unit tests,
    `world-fingerprint`, logs, and analysis of the fields and state.
  - Keep to the default world (seed 123, 2048 samples, play): its land
    comes from `tools/fetch-land` and the finished world loads from the
    cache after the first launch. Generate another, or pass
    `--refresh-world-cache` or `--no-world-cache`, only when generation is
    what changed. Pin the scene (`MOPPE_OPENING=none`, `MOPPE_CLOCK` or
    `MOPPE_SUNHEIGHT`, a `MOPPE_WALK` or `MOPPE_DEMO` script) so a before
    and after differ only by the change.
  - Take a few targeted stills, small: `--screenshot` with `--window-size
    640x360` (into a directory that exists: a missing one is reported
    written but nothing is saved), `--water-screenshot`,
    `tools/capture-water`; for the renderer alone, `./build/nhal-demo
    --capture` takes seconds.
  - Render one thing at a time: lavapipe already fills every core, so
    parallel captures only slow each other. Run long ones in the
    background with a timeout.
  - Record video only when motion is the question, and then briefly:
    `tools/record OUT.mp4 SCENE --size 640x360 --seconds 2` (it skips
    gamescope without a GPU), with `--start` to reach the moment cheaply
    and `--sheet` for a contact sheet to read.
- With a GPU the workflows above run at their ordinary speeds: full-length
  recordings at 720p or 1080p, the gazetteer, `tools/ride-judge`, and the
  trailer are reasonable ways to check work.
- These are defaults, not rules. When only a long or full-size capture can
  answer the question and there is no GPU, say what it would cost, or ask
  for it to be run on a machine with one.

## Research Library
- The hosted Sheaf research library is available through `tools/sheaf`; start
  with `tools/sheaf help`, then use `search`, `read`, and `note`.
- The helper defaults to `https://m.sheaf.less.rest` and obtains its token over
  `ssh igloo` when `SHEAF_TOKEN` is not already set.

## Architecture (see docs/renderer-design.md)
- `moppe/quantities.hh` is the registry of every quantity specification, in
  namespace sections. Declare new ones there with the `QUANTITY_SPEC` macro,
  never by hand-writing the struct: mp-units switches between a CRTP and a
  deducing-this formulation per toolchain and only the macro writes both. The
  concrete `quantity`/`quantity_point` aliases built on a spec stay with the
  code that owns the concept, since those need units and vectors the registry
  has no dependency on.
- Text and HUD vector shapes render with Slug, straight from quadratic
  outlines: `render/truetype.*` reads TrueType glyphs, `render/slug.*` builds
  the band/curve buffers (and a CPU coverage mirror for tests),
  `render/text.*` lays out a `Font` into a `TextList`, and the NHAL
  renderer's `slug-text` program draws it via `Renderer::draw_hud_text`. The
  bundled face is Geomini (`fonts/Geomini-Regular.ttf`, and SemiBold for
  the signs; SIL OFL, licence beside them), a geometric sans after
  Swedish road signs, kerned from its GPOS. Lettering in the world
  (the fingerposts) is the same `GlyphQuad`s placed in metres
  (`TextList::place`), uploaded once by `Renderer::create_world_text` and
  drawn by the `world-text` program into the scene, depth-tested, lit,
  and fogged (`Renderer::draw_world_text`).
- `moppe/render/` — the game-shaped renderer API (DrawList immediate mode,
  MeshBuilder-baked meshes, the Renderer interface); no GPU API types.
  `moppe/nhal/renderer/` implements it over NHAL (`moppe/nhal/`), the one
  renderer on every platform, with its shaders in Luv's Lisp
  (`moppe/nhal/renderer/shaders/world.lisp`) lowered by luv-shaderc to MSL,
  HLSL, SPIR-V, and WGSL.
- `moppe/platform/` — the Game interface, input, assets, speech. `sdl/` is
  the desktop host (macOS and Linux) and the browser's: an SDL3 window, its
  input, and the NHAL device (`device_metal.mm`, `device_vulkan.cc`,
  `device_webgpu.cc`). `ios/` and `tvos/` are UIKit hosts over NHAL's Metal
  device, `uwp/` the Xbox's, `apple/` services the Apple hosts share, and
  `web/` the browser's page.
- `moppe/game/` — the game systems, one file each (terrain, forest, water,
  dust, HUD, vehicle rendering; glue in game.cc).
  Mutable replay state is gathered incrementally in `game/game_state.hh`; see
  `docs/game-state.md` for the checkpoint boundary and remaining systems.
- The on-foot body is `mov::Character` (`moppe/mov/character.*`): a
  kinematic capsule floating a step above the feet, moved with Box3D's mover
  planes against `TrunkField` obstacles and heightfield planes, with a ground
  probe for snapping and stepping, a walkable-slope limit, coyote time, and
  jump buffering. `game::Walker` turns controls into its intent and counts
  the stride phase; `game/avatar.*` solves the articulated figure's skeleton
  (two-bone leg IK, planted stance feet) and `walker_render.cc` draws it.
- `moppe/mov/` is simulation only; `moppe/map/` is terrain generation.
  The bike is a Box3D assemblage by default (`mov/rigid_bike.*`: chassis,
  two sphere wheels on wheel joints, a streamed height-field ground patch,
  trunk capsules, arcade assists); `--bike-physics classic` selects the old
  point mass in `mov/vehicle.cc`. An unridden bike is parked (wheels locked,
  held still once settled). Vendored Box3D carries one local fix
  (`third_party/box3d/VENDOR.md`).
  Both are GL-free and portable.
- `moppe/terrain/` owns finite terrain algorithms and typed analysis values;
  see `docs/terrain-expressions.md`.
- `moppe/spatial/` contains finite typed quantity bundles and generic local or
  interpolated sampling operations. `moppe/map/surface.*` is the whole of
  `moppe/map/`: the ground surface's typed quantities, the generation passes
  that fill them over one shared surface domain, the readings analysed back
  out, and the surface cache. `terrain::WaterSheets` carries a distinct water
  bundle in the same elevation frame. Game-side presentation bridges are the
  only place those quantities become renderer texture lanes.
- Terrain renders by vertex-pulling from an R32F height texture +
  RG16Snorm normals; physics keeps the authoritative CPU heightmap.
- A jittered, reversed-Z scene at a fraction of the drawable, a temporal
  resolve to the drawable, occlusion, shafts, bloom, and the tonemap, then
  the HUD in point coordinates (docs/nhal.md).
- World generation runs on a background thread behind a loading screen.

## Code Style Guidelines
- **Namespaces**: Use nested namespaces (`moppe::render`, `moppe::game`)
- **Function names**: Use snake_case (`render_directly()`)
- **Member variables**: Prefix with `m_` (`m_width`)
- **Indentation**: 2 spaces
- **Braces**: Opening brace on same line for functions
- **Line Length**: Keep under 80 characters
- **Includes**: Group in order: 1) Project headers 2) STL 3) External libraries

## Error Handling
- Use exceptions for error conditions
- Catch in main function or event handlers
- Use `std::cerr` for error messages
- Graceful exit on errors with code -1

## Version Control
- After completing a task or request, generally commit and push proactively as
  a checkpoint unless the user asks not to. This is a single-developer repo;
  commits are cheap save points and do not need to represent a final design.
- When asked to "commit and push," commit all non-ignored changes in the
  worktree, including unrelated work, and push the current branch with plain
  `git push`.
- Keep generated files and build products out of commits; add appropriate
  ignore rules when necessary.
- Write clear commit messages. Split changes into multiple commits when there
  is a useful, natural separation and the changes are not entangled, but do
  not over-optimize for a pristine commit history.
- Finish with a clean worktree synchronized with its upstream branch.
- Do not create a pull request or use a publishing workflow unless explicitly
  requested.

## C++ Features
- C++23 standard; newer C++26 features may be enabled per target when the
  active Apple and CI toolchains support them without compatibility shims
- RAII for resource management
- Enable compiler warnings (-Wall)
