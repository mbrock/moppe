# Features and how to see them

The tour of what the game does, with the switches that stage each feature
for captures and recordings. AGENTS.md carries only what an agent needs to
build, test, and change the code; read this when working on a feature.

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
  `MOPPE_DEMO` still starts riding.
  The look and the way the walker goes turn with the mouse at once
  (`Walker::heading`); the body comes round after it through the rig
  (`Walker::facing`, `pose_avatar`): standing, the head and then the
  chest twist toward the look, and beyond some seventy degrees the feet
  step round on the spot after it (a fast spin keeps stepping the way
  it went); under way the body turns at a bounded rate and banks into
  the turn. Walking, the sandals roll from heel to toe along the slope;
  running, the heel folds up early and the foot comes back under the body
  to land. The free arm pumps while the left hand steadies the basket;
  a jump keeps the lead leg from takeoff. The other player sees the same.
- Walk into chest-deep water and the hiker starts swimming. Move as on
  foot for a front crawl, or stop to tread water. Hold `Shift` to dive;
  release it to swim level underwater. Hold `Space` to rise (on a
  controller, left trigger dives and right trigger rises). At the
  surface the swimmer follows the waves; in a river the current carries
  them. Swim into the shallows to stand again. `tools/record OUT.mp4 swim`
  follows the swimmer from beside the bank, down and back up;
  `MOPPE_WALK_CAMERA=eyes` records the same swim in first person.
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
  and eats. Eggs from a barn's nest boxes (`Basket::eggs`) fry too,
  after a fish and before the mushrooms (`Pan::eggs`), and mend as much
  as a mushroom each.
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
  line in (`Line::from_boat`, `angler` and `boat_drifting` in
  game_camp.cc).
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
  hour it starts at (`MOPPE_CLOCK=21.5` for night). 
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
- The cart roads (`moppe/terrain/roads.hh`): `form_roads` builds each
  into the ground on a profile held to a 10% grade, bridging rivers and
  crossing lesser water on embankments over culverts; a road planned
  out into water is cut back to its bank, the tread keeps half a metre
  over standing water beside it, and each end that joins no other road
  is a level turning place (`RoadTurning`, a ring of tread lying back
  from the end). `RoadSurface` is the exact tread the near ground, the
  road ribbon, and every wheel read: its height is continuous round a
  bend (measured between the mitres of each segment) and it ends
  rounded at a road's end. Near the camera the ground lies five
  centimetres under the ribbon, so the two never meet.
- The loop and the bus (`moppe/game/bus.*`): the planner lays one road
  once round the world through the landing or inland trailhead
  (`Road::loop`, the cheaper of the two ways round, by a search over two copies of the lattice
  joined at the seam), paved, with no turning place, and the
  trailhead's road and the rest branch from it; a world with no way
  round has no bus service. `plan_bus_line`
  stands a stop at the terminus, one just past each branch named for
  the place it leads toward ("Örnforsen vägskäl" when that is over
  600 m off), one by each farm near the loop, and more with names of
  their own (`name_bus_stop`: a -by, -backen, -kurvan, or a farm's
  -gården) wherever stops would lie over 1.4 km apart; each on the
  right-hand verge, off bridges, with a yellow hållplats sign and,
  where the verge is level, a falu-red shelter. The buses drive on the
  right, 0.9 m right of the road's line on its 6 m carriageway (all
  one way round, so they never meet), on a
  timetable from play's clock alone (gathering pace to 45 km/h,
  braking for each stop, standing 6 s, and 20 s at the terminus),
  with a target headway of eight minutes. Nothing of them is
  in `GameState` but how far their clock runs ahead of play's
  (`m_bus_lead`), which keeps two players' buses together. Near a stop, a board
  stands over its sign with its name and "Bus in 4 min" or "Bus here,
  leaves in 12 s" (`BusView::draw_board`). The map draws each bus as a
  red oblong along its way and the stops as yellow plates (named in
  the near view), the chart pins them while they are on it, and the
  buses knock the players' machines aside (`bus_body`, through the
  bumpers). The bus is a fifties country bus in post-coach yellow
  (`models/bus.blend` from `tools/figure/bus.py`, drawn through
  `render_car` with its twin rear wheels, a driver at the wheel, the
  wheels rolling and the front pair steering with the road, and its
  door's two leaves folding open at each stop). Riding it
  (`Doing::on_the_bus`): on foot by the door of a bus standing at a
  stop, the mount key boards (`board_bus`) and the rider sits in the
  front seat by the door (`bus_model::seat_01`), seen from behind the
  bus or, through their eyes, out of the windscreen; the prompt names
  the next stop, and the mount key asks to get off there (again, to
  stay on) or, while it stands at a stop, gets off at once, stepping
  down by the door (`leave_bus`, `ride_bus`). Two players see the same
  buses: each snapshot carries the sender's buses' clock (`bus_time`),
  the game behind by more than a second jumps ahead to the other's,
  and then the player with the smaller number follows the other's
  closely (`keep_bus_time`); so both can ride one bus, the second to
  board taking the seat beside the first (`bus_model::seat_02`), each
  seen in their seat by the other (companion wire version 18).
  `MOPPE_WALK=bus-stop[:N]` stands on the verge behind the Nth stop
  (the first, where the first bus stands as play begins),
  `MOPPE_WALK=bus-ride` boards that bus and rides it, and
  `MOPPE_MAP=whole|near` starts with the map unfolded; `moppe: bus:`
  lists the stops.
- The way back (`moppe/game/way_back.*`): `G`, at any time in play,
  takes whoever is lost or stuck to the road by the nearest bus stop
  (`roadside_near`), in the copy of the world nearest them. A land
  machine being driven (or the bike under the glider) comes along and
  stands on the road's left half a bus's length short of the stop,
  which the buses, all driving on the right, leave free; on foot, or
  from the boat or the jet ski, which stay afloat where they were, one
  stands on the verge behind the sign, where its board says when the
  next bus comes. When the view settles, the timetable advances until the
  next bus is five seconds away. On foot within 12 m of a sign, G skips
  the wait in place. A bus already at the stop or closer than five seconds
  keeps its schedule. A rope or a slung load is let go first. The view does
  not cut: it rises up and back out of play's view, flies over at a
  height clear of the ground between, and comes down into play's view
  at the stop, which it hands back as it ends (`WayBack`, five and a
  half to eleven seconds by the distance); the player is set down as it
  passes over the top (`set_down`), and play's controls rest meanwhile.
  Not on the bus, along in the other player's machine, or in the
  hammock. `tools/record OUT.mp4 --control 'wait 1' 'tap G'` films it;
  `moppe: way back to` names the stop.
- Waymarks: a small arrow with a name and a distance marks the other
  player (in their jacket's colour), and the bike, the jeep, the
  tractor, and the helicopter once they are more than some thirty metres off: over the thing when it is in
  view, at the frame's edge pointing the way when it is not
  (`HudState::Waymark`, placed by `add_waymarks` in heads_up.cc).
  Marks
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
  `add_chart` in heads_up.cc). The sheet is painted while the world
  loads.
- The map: `M` unfolds a sheet of the whole world over the view, `M`
  again looks closer at the 1.8 km round the walker, and a third (or
  `Esc`) folds it away; a small `M Map` keycap beside the chart says
  so. With the map folded, `Esc` asks whether to quit (`Esc` again
  quits, any other key plays on; `Hud::draw_quit_question`). It is painted once per world on the CPU
  (`moppe/game/relief_map.*`: relief shaded from the north-west, forest
  green, brown contours every 10 m, water, rivers by their catchments,
  a kilometre grid, the trail in red dashes) and set on paper with a
  scale, spot heights, and marks for the walker, bike, jeep, tractor,
  companion, camp, and trailhead (`moppe/game/map_sheet.*`).
- The cheat menu: `` ` `` (the key under Esc), on foot or at a wheel,
  lists every machine by its silhouette and name over the view, play's
  HUD put away; `W`/`S` choose, `Space` or `F` puts the player
  at its wheel wherever it stands (`take_over` in game_session.cc), and
  `Esc` or `` ` `` closes it (`Hud::draw_cheat_menu`). A
  `MOPPE_CONTROL_FILE` works it too: `` tap ` ``, `tap S`, `tap Space`.
- The desktop world is 10 km by 10 km with 4096 samples per side,
  giving 2.44 m spacing. `--world-size <KM>` accepts 2.5 to 10 and
  `--terrain-resolution` sets the sample count independently. Geology keeps
  its physical wavelengths as the world widens. On macOS local generation
  uses Metal. The CPU baker supplies shared landscapes.
  [Orogeny performance](orogeny-performance.md) measures both paths.
- The HUD is deliberately one quiet speedometer, the chart, and fading
  prompts. `H`
  toggles the diagnostic overlay (frame rate, stunt and score readings,
  trail map); `MOPPE_HUD=debug` starts with it on, and `MOPPE_HUD=none`
  draws nothing over the picture (film/record's footage).
- `P` captures the current frame to `screenshots/run-<timestamp>/shot-NNN.png`
  (gitignored, one directory per run). Start the game, walk around, press `P` at each view worth
  discussing, then point at the latest run directory. Each press also
  logs the camera as a pasteable `shot` line for the opening (`moppe:
  pose: shot shot-NNN eye X Y Z heading H pitch P fov F clock T sun S`),
  gathered in the run directory's `shots.txt`; on the Xbox,
  `tools/xbox-control send 'tap P'` then `log` reads it back.
- Videos: `tools/record OUTPUT.mp4 [SCENE] [OPTIONS]` records the game
  to an H.264 MP4 -- `tools/record /tmp/camp.mp4 camp` is the camp at dusk
  through to morning. Scenes are `play` (the default: on foot at the
  trailhead, no opening), `ride`, `forest`, `glide`, `walk`, `forage`,
  `camp`, `haul`, `bump`, `eggs`, `barn`, `opening`, `flight` (a
  canopy-cleared drone tour through the trailhead, peak sanctuary, saddle,
  lake, summit, waterfall, river valley, and arrival, paced to `--seconds`
  or 120 s by default and ending when the drone lands), and `river` (a
  drone's flight down the greatest
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
  `--start` lets pass goes by at twenty steps a drawn frame); the `flight`
  and `river` videos end when the drone lands. A window cannot be larger
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
- The vista: `tools/vista OUTPUT.mp4` records and cuts a calm vertical
  1080x1920 film of about a minute and a half through the land's kinds
  of country (coast, rivers, forest, heath and rock, high country,
  evening), held drone views of the default world in one reel per hour
  with the sun still, and a few autopilots, under a quiet HUD drawn as
  SVG in Geomini (chapter cards, and each view's name, note, latitude,
  longitude, height, and hour). Needs `rsvg-convert`; clips are kept in
  `build/vista`, so recutting `FILM` takes a couple of minutes.
- The film (`film/`, film/README.md): "one day at the trailhead", a
  vertical 40 s film made with fframes (Rust and SVG over the GPU).
  `nix develop -c film/record` has the game record its shots at the
  hours of the day they show; `film/` (in `nix develop .#film`) cuts
  them together under an arc of the sky carrying the sun and the moon
  by the hour: `cargo run --release -- render -o moppe-day.mp4`.
- The opening is authored: `data/opening.txt` lists a few held shots of the
  original 5 km world (seed 123, 2048 samples, play), each drifting gently
  (`push`/`truck`/`rise` metres, `pan`/`tilt`/`zoom` degrees, a breathing
  `sway`), joined by cuts or a dip through black (`fade`), with the title
  and a credit set in Slug fading over them (`caption`), ending with an
  `arrival` into the view play begins with, behind the hiker beside the
  parked bike.
  The grammar is in `moppe/game/opening.hh`. Author by framing a view in
  play and pressing `P`; try a list without
  rebuilding with `MOPPE_OPENING=path/to/list.txt` (or
  `MOPPE_ASSETS=<repo>`). Another world plays one generated still of the
  trailhead (`MOPPE_OPENING=generated` forces it); `MOPPE_OPENING=flight`
  flies the canopy-cleared scenic drone tour (`plan_cinematic_flight` in
  `game/cinematic_flight.*`, paced with `MOPPE_FLYOVER_SECONDS` when set)
  through the world's planned landmarks (trail, river valley, waterfall,
  lake, saddle, peak sanctuary, summit, arrival),
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
  `moppe: ... at` lines say where
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
- Command line: `--fullscreen` (the default, except for captures and the
  browser), `--windowed`, `--graphics-quality low|balanced|high`,
  `--render-scale <0.25..1>` (the 3D scene as a fraction of the drawable;
  half by default, temporally upscaled), `--window-size WIDTHxHEIGHT`,
  `--inactive` (keep a hand-started run behind the active app), `--seed`,
  `--world-size <KM>`, `--terrain-resolution`, and `--screenshot`. `moppe --help` lists them.
  - Finished worlds load from and save to a stable cache keyed by the
  complete world recipe. Beneath it, the land (the geology's output,
  docs/land.md) comes from the package's `worlds/`, the cache, or is
  generated and saved; `tools/fetch-land` downloads the published
  default land (`lands.json`), and `tools/publish-land` shares one.
  - Dynamic resolution (`moppe/render/dynamic_resolution.hh`): play
  holds 60 frames a second by drawing the scene smaller, down to three
  eighths of its ordinary width and height, while the GPU cannot keep
  up, and gives the size back when it can. An explicit render scale, a
  capture, and a recording never shed any: their pictures must not
  follow the machine's speed. `MOPPE_NHAL_TIMINGS=1` reports the
  scene's size with the passes.
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
  re-exports both, the jeep, the helicopter, the tractor and its
  trailer, and the Caddy (`MODELS_REBUILD=1` regenerates the .blend
  files; on Linux, `make models BLENDER="nix shell nixpkgs#blender -c
  blender"`). Each paint
  carries its material's whole Principled BSDF (`model_mesh::Paint`:
  metallic, roughness, clear coat, transmission, sheen, emission), set
  in each script's `FINISH` table (the Caddy's `MATERIALS`) through
  `shapes.material`: glossy plastics and bright alloys on the bike,
  flat army paint on the jeep, old enamel and cast iron on the
  tractor, aviation paint and a see-through canopy on the helicopter,
  sailcloth and aluminium on the glider.
- The Caddy (`mov::Car` on `caddy_rig`, `moppe/mov/car.*`,
  `models/caddy.blend` from
  `tools/figure/caddy.py`, drawn by `game/caddy_render.*`) is a
  first-series VW Caddy pickup under a fibreglass hardtop on Swedish
  plates, parked by the sea on level ground above the shore the boat
  lies at, clear of the other machines (`park_caddy` in game_machines.cc;
  the
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
- The jeep (`mov::Car` on `jeep_rig`, `moppe/mov/car.*`,
  `models/jeep.blend` from
  `tools/figure/jeep.py`, drawn by `game/jeep_render.*`) waits on a
  verge beside the nearest cart road some 300 m from the trailhead,
  facing along it (`park_jeep`; beside the bike where no road is near).
  Only the Trail 90 and the tractor stand at the trailhead; each other
  machine has a place of its own, logged as `moppe: ... parked`. On foot, `F` beside it drives it (nearer it
  than the bike; the prompt says which) and `F` again steps out; `W`/`S`
  drive and reverse, `A`/`D` steer, `Space` brakes, `Tab` cycles the
  view. It is life size and seats two: in multiplayer, `F` beside the
  passenger seat of the other player's jeep takes it
  (`Doing::riding_along (Machine::jeep)`), and the jeep's pose travels
  in the companion snapshot. Every machine that seats two rides the
  same way, by its `MachineKind::passenger_seat`: the jeep and the
  Caddy beside the driver, the helicopter beside the pilot (both
  behind its tinted glass), the boat on its fore seat, and the jet ski
  pillion behind its rider; one standing on land, or the boat or jet
  ski while the other player drives it (`seat_within_reach`). The
  bikes and the tractor seat one. Like the bike it is a
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
  `Doing::driving (Machine::mountain_bike)`). `MOPPE_DEMO=mountain_bike` (`tools/record OUT.mp4
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
  small front wheel), as `Doing::driving (Machine::long_john)`; `MOPPE_DEMO=long_john`
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
  game_machines.cc). On foot, `F` beside it takes the helm and `F` steps ashore
  over whichever side has the shallowest water; `W`/`S` drive and
  reverse, `A`/`D` swing the tail to steer, and `Space` (the right
  trigger) holds the governor tied back. The engine's thrust is the
  propeller's grip standing and its power under way, and the hull's
  hump of wave-making drag is added whole, so it does his figures:
  some 24 km/h alone (29 tied back) and 15 two-up, stuck at the hump
  (26 tied back). The boat and engine weigh 70 kg. In multiplayer, `F`
  beside the other player's boat while they are at its helm takes the
  fore seat (`Doing::riding_along ()`, `m_seat_boat`), and the helm's game carries
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
  and the autopilot follows the river down to
  the sea. Besides `side|front`, `MOPPE_RIDE_CAMERA` films the boat as
  `drone`, `aerial`, `orbit`, `top`, `fpv`, `hull`, `stern`, `helm`,
  or `flyby` (each with its own lens; `MOPPE_RIDE_CAMERA_ANGLE` swings
  the following ones round), and `tools/boat-promo OUT.mp4` records
  and cuts a 50 s 1080p promo from them (clips kept in
  `build/boat-promo`, so recutting `FILM` takes a minute).
- The Ripa 440 snowmobile stands on open high ground; find it on the map,
  or start riding with `MOPPE_DEMO=snowmobile`. `F` mounts or dismounts,
  `W` opens the throttle, `S` brakes then reverses, `A`/`D` turn the
  runners and Space brakes the track. The steel keels steer only under
  load and scrape on bare ground. On the snowy fells the belt kicks up
  a fan of powder and ice chips, leaving a ribbed trench between two
  narrow ski cuts. Boots and tyres compress the same snow. The near
  terrain reads the signed height of each cut and the powder pushed up
  at its edges; marked tiles stay in session memory outside the view.
  `tools/record OUT.mp4 snowmobile --start 3` films the ride.
- The jet ski (`moppe/mov/jet_ski.*`, `models/jet_ski.blend` from
  `tools/figure/jet_ski.py`, drawn by `game/jet_ski_render.*`) is a
  sit-down runabout of the early nineties: white glass fibre 2.9 m
  long and a metre in the beam, teal and magenta flashes down its
  flanks, a long saddle between two footwells, a cowl rising to the
  bars, and a 650 cc two-stroke twin (37 kW) turning a jet pump. It
  lies afloat beside the boat: at the landing, alongside the jetty's
  other side (`park_jet_ski` in game_machines.cc; the log line `moppe: jet ski
  afloat` says where). On foot, `F` beside it gets on and `F` gets off
  over whichever side has the shallowest water; `W` opens the
  throttle, `S` drops the reverse bucket, and `A`/`D` swing the
  nozzle. It is the boat's kind of body (one Box3D hull over its own
  ground patch) under the same water, panel by panel
  (`mov/hull_water.*`: `press_hull`, shared with `mov::Boat`), but
  decked, so it floats however it lies, with a vee bottom that planes
  within a few lengths, some 60 km/h flat out, and 285 kg with its
  rider. The pump pushes only while its intake grate is in the water
  (the engine races off a wave), and only the jet steers: with the
  throttle shut the bars do next to nothing. The rider balances it
  and leans it into a turn (`JetSki::lean`). Run up on the land, `F`
  takes up a rope to the eye in its stem and walking drags it back
  (`Machine::jet_ski`); the helicopter can hook it. It throws a plume
  from the nozzle and spray from the bow, and leaves the boat's kind
  of wake. `MOPPE_DEMO=jet_ski` (`tools/record OUT.mp4 jet_ski`) has
  the autopilot ride it out over open water, and
  `MOPPE_RIDE_CAMERA=side|front` stands beside it; the other player
  sees it (companion wire version 14).
- The bus's model (`models/bus.blend` from `tools/figure/bus.py`,
  exported as `moppe/game/bus_model.*` by `make models`) is a
  forward-control country bus of the fifties in post-coach yellow, 11 m
  by 2.5 m, its origin on the road under its middle: a body with the
  cabin in it, the glass to draw last, a front wheel and a rear pair,
  the door's two leaves (each about its hinge's point, opening outward),
  and the steering wheel, with points for the axle ends, the driver's
  seat, grips, and pedals, the 42 passenger seats, the door, the step,
  the lamps, and the blank destination sign.
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
  (`tools/record OUT.mp4 wheelie`) rides them on the autopilot
  (companion wire version 11).
- The bike (`models/bike.blend` from `tools/figure/bike.py`, drawn by
  `game/vehicle_render.*`, ridden as `Doing::driving (Machine::bike)`) is a 1972 Honda CT90
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
  tree within reach of its rotor (`park_helicopter` in
  game_machines.cc).
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
  mountainbike, the jeep, the tractor, the Caddy, the boat, or the jet
  ski) with the line in reach of it -- some metres up and up to eight
  to one side, no aiming needed -- `F` hooks it on a line under the
  helicopter (`moppe/game/sling.*`, `GameLogicState::m_sling`,
  `GameSession::hook_within_reach`), and `F` again lets it go, from any
  height, before `F` steps out. The line is a stiff spring from the
  hook to a ring over the load's centre of mass, pushed each step on
  both Box3D bodies; the flight computer lifts and leans against its
  pull (`Helicopter::set_load`), so a heavy load drags it down and
  slows its climb, and the Caddy, slung, damps its swinging and turns
  its nose the way it is carried (`Car::set_slung`).
  `MOPPE_RIDE_CAMERA=side|front` stands beside the hanging load. The
  other player sees the load fly but not the line.
- The tractor (`moppe/mov/tractor.*`, `models/tractor.blend` from
  `tools/figure/tractor.py`, drawn by `game/tractor_render.*`) is a
  little red farm tractor of the old kind, open, with a pan seat
  between big ribbed rear wheels. It stands out on the open ground
  ahead on the bike's other side from the jeep (`park_tractor` in
  game_machines.cc). On foot,
  `F` beside it climbs up to drive and `F` again climbs down; `W`/`S`
  drive and reverse, `A`/`D` steer, `Space` brakes, and off the
  throttle it comes to a stand by itself. Its frame's origin is on the
  ground between the axles. Like the jeep it is a Box3D assemblage, a
  rear-heavy chassis of three box hulls on four sphere wheels, but only
  the big rear pair drive it: hard and slowly, the engine governed to
  some thirty kilometres an hour, easing off if its nose lifts so that
  it does not rear over. The stack smokes while someone sits at the
  wheel. It tows a trailer everywhere (`models/trailer.blend` from
  `tools/figure/trailer.py`, drawn with it by `render_tractor`): a
  one-axle plank trailer whose sides, headboard, and tailgate are
  boarded in the rainbow's six colours, violet at the foot to red at
  the top, between navy stakes under a cream rail, with a ladder rack,
  a pine deck, pink wheels, and a yellow A-frame drawbar. It is part
  of the tractor's assemblage (`Tractor::trailer_*`): a box hull on two
  sprung sphere wheels, its drawbar's eye on a ball joint at the
  tractor's drawbar pin that lets it swing some 65 degrees either way,
  so it cuts corners, follows round, and jackknifes backing up. Its
  wheels roll free, brake with the tractor's, and lock when it is
  parked. Machines that meet it knock it aside by bumpers of its own
  (`towed_bumpers` in rescue.cc); the rope and the sling tie to the
  tractor. The other player sees it, its driver, and the trailer
  (companion wire version 15); `MOPPE_DEMO=tractor` (`tools/record
  OUT.mp4 tractor`) has the autopilot drive it.
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
- Animals (`moppe/game/animals.*`, drawn by `game/animal_render.*`;
  models `models/<species>.blend` from `tools/figure/animals.py`, the
  poultry from `tools/figure/hens.py`, `make animals`): North Swedish
  horses, fjällkor, and Gotland sheep
  graze the ground the farms, the landing, and the kept sanctuaries
  still graze (`grazing_pressure`), with a paddock of each a stone's
  throw from the trailhead (`Fold::Kind::trailhead`), and wild herds keep to their own
  country -- moose browsing young forest by the mires, roe deer at the
  forest's edges, reindeer on the highest of the fell. Each wanders,
  stands, puts its head down to crop (`Animal::head`; the muzzle
  reaches the grass), and walks on to fresh grass near its herd; someone
  coming near makes it look up and then move off with its herd, the wild
  ones from much further (`SpeciesTraits::wary`), a machine from
  further still. Nothing can hurt them and there are no guns: whatever
  reaches one shoves it aside and it runs. They are rigid parts swung
  about pivots (neck, head, two parts a leg drawn on both sides, tail),
  stepped within 700 m of the camera, not in `GameState` and not shared
  with the other player. `MOPPE_WALK=animals[:SPECIES]` starts beside
  the nearest herd (of stock, or of `cow`, `roe_deer`...), waits, and
  walks at it; `MOPPE_ANIMAL_CAMERA=<m>` follows that animal from the
  side; `tools/record OUT.mp4 animals` records it; The log's `moppe: animals:` lines say how many and
  where the nearest of each stand. Each barn lived in keeps two horses
  and two cows tied in their stalls with their heads to the manger and
  two or three Linderöd pigs loose in a pen (`Stall`, `Animal::indoors`,
  `keep_indoors`), standing on the floor (`floor_at`). Its poultry
  (`moppe/game/hens.*`: Skånsk blommehöns, a rooster, five hens, and a
  mother with her chicks at her heels) peck about the byre and the
  yard, in and out by the ramp, scurry from whoever steps among them,
  and at dusk go up to roost on the perches; they lay in the four nest
  boxes on the end wall (`GameLogicState::m_nests`, `lay_eggs`: up to
  three a box, one every few hours of the clock). On foot facing a box
  with an egg in it, `E` takes one into the basket, six at most. In the
  dark the barn's lantern is the lamp. `MOPPE_WALK=barn|byre|hens|eggs`
  starts before its doors, in the byre, facing the hens' corner, or by
  the boxes; `eggs` (`tools/record OUT.mp4 eggs`) takes three, walks out
  into the yard, and fries and eats them.
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
  a barn, left as stone plinths once long abandoned (each building
  exports at three levels of detail, `house`, `house_mid`, and
  `house_far`, and places.cc draws the finest whose smallest parts --
  5 cm, 10 cm, the massing -- still span a pixel, through
  `Renderer::scene_focal_pixels`; no two of its faces share a plane);
  with a landing
  stage on the lake to fish from. Farmsteads -- a house and a barn,
  without the mill -- also stand beside the roads and across the open
  country below the high fells, some 380 m apart at least, the first a
  few hundred metres from the trailhead (`plan_homesteads`, `Farm::
  homestead`; 13 in the default world). The barn (12 by 8 m, Falu red)
  opens by a pair of doors up a stone ramp onto a board floor under a
  hay loft, its walls limewashed over a tarred skirting, its floor laid
  in planks, lit by day from its doors (`light_barn` in game_places.cc) and
  furnished (`barn_things` in farm.py: hay racks, the horses' tack,
  tools by the door, a wheelbarrow, a feed bin, a milking stool and
  pails, a horseshoe over the door, a cat asleep on the sacks); it
  can be walked into, the camera keeping inside; round
  it the yard's grass is trodden short (`UndergrowthParams::yard_*`). The farmhouse can be walked into:
  its door stands open over a step, and inside, under a ceiling, are a
  board floor, a table and bench, a bed, and the chimney stack; the
  following camera keeps inside its walls (`ChaseCamera::confine`), and
  no grass grows under the floor of the house nearest the camera
  (`UndergrowthParams::bare_*`). `MOPPE_WALK=house` walks in through
  the door, `MOPPE_WALK=indoors` stands inside. By each place where
  two trees take a
  hammock, an old fire ring takes the camp's fire. `MOPPE_WALK=
  landing|sanctuary|farm|house|indoors|barn` starts on foot there; `moppe: places:` lines
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
- The scan (`moppe/game/scan.*`): on foot, `Q` sends a ring sweeping
  out 60 m over the ground, and what it passes lights up for twelve
  seconds with a label standing over it, faint through whatever is in
  front: mushrooms in groups of a kind (fly agaric in red, POISONOUS),
  and pairs of trees that would take the hammock, with how far apart
  they are. The labels are Slug text standing at points in the world
  (`TextList::stand`, `Renderer::draw_world_labels`: facing the camera
  at a fixed size), the nearest named and the rest a mark alone where
  names would crowd..
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
  goes gold and its bilberry crimson. Where heather leads, ling stands
  in the grass system as knee-high cushions with peaty gaps between
  them (shoots stiff, olive below, faded mauve, rust, or silver at the
  tip, as at the equinox: the higher, the more gone over), lower on the
  fjäll, where the grass gives out, and without meadow flowers
  (`ling-colour`, `ling-bloom` in world.lisp). Two readings come from the rock
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
  and solid), fewer the longer ago it burnt. No trunk or bough is
  drawn thinner than a pixel (`stem-floor` in world.lisp): one that
  would be is widened to a pixel and, by a hash of its own, left out in
  proportion, so a far stand of snags or birch keeps its bark's share of
  the picture in steady lines rather than flickering slivers. The
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
  rock, the trail, and snow on the highest level ground. There the powder
  settles into shallow wind-shaped drifts and rounds off the crowns of
  boulders, leaving their cleaved sides exposed. Small ice facets glint
  in direct sunlight, fading into the sheen as they get too small to
  resolve. Neither the snow nor its tracks rewrite the generated land.
  The underlying grain is read at four scales from one generated
  tiling sheet (`moppe/render/ground_detail.hh`: grain, its slope, a
  mosaic of stones), which also tilts the lit normal.
- Multiplayer (`moppe/game/companion.hh`, `moppe/game/companion_transport.*`,
  `moppe/game/companion_link.*`): up to eight players (one local + up to seven
  companions) share one world. `--multiplayer` (`MOPPE_MULTIPLAYER=1`)
  discovers players on the local network over UDP port 27960;
  `--join <host[:port]>` connects directly (with `"MOPP"` peer introductions
  forming a direct mesh among three or more players); and
  `--join ws[s]://host/room/<name>` or `--join room:<name>` (expanded via
  `MOPPE_RELAY_URL`, default `ws://127.0.0.1:27960`) relays over WebSocket for
  internet and browser play (`bun run tools/relay.ts` runs a lightweight
  reference relay). In play, `J` opens the multiplayer menu to toggle LAN,
  create or join a relay room, paste an address, inspect connected players and
  world mismatches, or disconnect. Neither player serves: each simulates its
  own body and fleet and sends compact delta-masked snapshots thirty times a
  second (wire version 20, keeping packets under 1,200 B), while peers render
  each other a moment behind (`CompanionTrack`, ~60 ms on a steady link) across
  torus seams — including hikers, all ten machines, tractor trailers, helicopter
  slings, fishing rods, hauling ropes, campfires, frying pans (with fish and
  eggs), snow tracks, boat wakes, and vehicle-to-vehicle collisions, plus
  riding along in any peer's two-seat machine or sharing the eight front seats
  of the bus. Picked mushrooms, beacons, the day clock, and the bus timetable
  are synchronized. Players are assigned distinct Nordic jacket colours in
  sorted player-ID order (`game/outfit.hh`, `game/figure.cc`: blue, green,
  rust, ochre, plum, teal, pine, berry). All players must share the same world
  (without `--seed`, multiplayer pins seed 123); mismatched seeds, resolutions,
  or extents are explained on the HUD and in the `J` menu. Two or more games on
  one machine find each other too (subsequent launches take any free UDP port).
- The weather changes (`moppe/game/weather.*`): spells of clear, fair,
  overcast, valley mist, drizzle, rain, and thunderstorm follow one
  another from the seed and the hour (`WeatherTimeline`, a few hours
  each, two-hour changes), so two players sharing a clock share it.
  Rain comes only out of an overcast (wet some 8% of the time), mist
  mostly at dawn, thunder mostly by night, lightning flashing the haze
  and the ground (`lightning_flash`). The first morning is fair, for
  the opening; the default world clears the first night and rains into
  a storm the second evening. The state is `GameLogicState::m_weather`
  and the log says `moppe: weather: rain from 15.1 h`.
  `MOPPE_WEATHER=clear|fair|overcast|mist|drizzle|rain|storm` holds one
  kind; a held sun keeps the drifting sky. The NHAL renderer draws the mist and the rain (1 a
  drizzle, more drops up to a downpour at 2.5).
