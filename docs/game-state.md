# Game state and replay

Moppe separates the generated world and resident resources from the mutable
logical state that advances each tick. The latter is intended to become a
copyable checkpoint: restore a value, replay the same fixed-step inputs, and
observe the same sequence of game states while rendering it under different
graphics settings.

`game::GeneratedWorld` now owns the stable renderer-free terrain, surface,
water, and analysis artifacts outside that checkpoint; its construction and
borrowing rules are documented in [Generated worlds](generated-world.md).
World construction is single-flight: a worker completes an owned candidate,
then one main-thread activation transitions the world/session pair while
preserving terrain-borrow ordering. The loading screen observes status text,
not candidate terrain, so neither it nor `GameState` owns an in-progress
world.

`game::GameSession` is the concrete owner of running gameplay state. It owns
`game::GameLogicState` (the clock, weather and camera effects, player mode and
inputs, health/scoring values, gameplay timers, and effects RNG) along
with the bike, the glider, walker, chase camera, stars, and dust.
`game::GameState` is its copyable checkpoint value: it combines that logic
with snapshots of those mutable subsystems. Restoring it changes only mutable
state; immutable terrain and trunk-field references, bike physical parameters,
and renderer resources remain attached to live objects on the same completed
world.

`game::FrameView` is deliberately outside `GameState`: it is derived anew for
each presentation frame from a completed world, a session reading, the active
camera, and graphics settings. Its frozen visible poses and camera/light
readings make rendering independent of later simulation mutation, but they are
not replay checkpoint state.

Ordinary playable simulation has one public fixed-step operation:
`game::advance_game_session(world, surface, session, input, seconds_t,
trunks)`. Its inputs supply only the completed world's parameters,
geometry, and optional trunk-collision field; it does not expose
`GeneratedWorld`, loading, platform, or renderer types. The operation applies
the `InputFrame`, advances actors and effects, updates score, camera, and FOV,
and reports the small set of application-side effects it cannot realize
itself.
`MoppeGame::tick` selects live or recorded input, continues the global clock
and weather through the paused cinematic mode, then delegates
ordinary play through that operation.

Ordinary play converts the platform's presentation interval into phase-locked
120 Hz steps before calling that operation. A slightly early display callback
therefore remains one simulation step rather than producing a zero-step frame
followed by two steps. Delayed callbacks can catch up by at most six steps;
excess elapsed time is discarded instead of feeding one unstable large step
into vehicles, the glider, or the chase camera. Graphics-benchmark replay and
deterministic cinematic capture retain their deliberate contract of one
logical step per rendered frame.

A completed-world handoff retires the old session before its old generated
world, then constructs a fresh session against the new world. In particular,
pressing `N` begins a new world session rather than carrying a player, score,
or effect history across incompatible terrain. A `GameState` checkpoint is
therefore portable between sessions prepared against the same world, not
between generated worlds.

The rigid bike (`mov::RigidBike`) owns a private, single-threaded Box3D
world. Its checkpoint, `Vehicle::State::rigid`, holds the three bodies'
poses and velocities about their centres of mass, the ground patch origin,
and the last step's contact readings and rider lean. `restore` destroys and
rebuilds that world in one canonical order, so every restore of a checkpoint
replays identically; Box3D's warm-start impulses and cached contacts are not
captured, so a restored ride tracks the uninterrupted one closely but not bit
for bit. The graphics benchmark therefore restores before every epoch,
including the first. The vehicle's coarse `position`, `velocity`, and
`heading` are derived from the snapshot; a state whose coarse pose was edited
moves the whole assemblage rigidly to match on restore. The session marks the
bike `parked` whenever nobody rides it.

The jeep (`mov::Car`, as is the Caddy) is the same kind of thing with five bodies: its
checkpoint, `GameState::jeep`, holds the chassis and the four wheels, the
ground patch origin, the controls, and the last step's wheel contacts, and
`restore` rebuilds its private world in one canonical order.

This is the first replayable slice, not yet a claim of complete determinism.
Renderer history is not in `GameState`. World generation,
terrain analysis,
uploaded meshes and textures, window state, and asynchronous loading state do
not belong there. Renderer history should get its own reset boundary rather
than being copied into logical game state.

The intended experiment loop is:

1. Generate and prepare one fixed world.
2. Advance with a fixed timestep to an interesting point and copy
   `GameState`.
3. For each graphics combination, restore the state, reset renderer history,
   replay the same fixed-step input tape through `advance_game_session`,
   discard settling frames, and measure the remaining frames.
4. Tag every timing sample with the graphics-feature mask, replay epoch, and
   logical frame number.

The built-in graphics benchmark that implemented this loop (and its Gray-code
partition of the graphics features, `--graphics-benchmark`, and the analysis
tools) was removed as dead code: the NHAL renderer never wrote its results.
`GameState` restore and replay remain what a future benchmark would build on.

Subsystem state structs should remain plain values. When another mutable
system joins the checkpoint, it should expose `state()` and `restore()` while
keeping configuration and resource ownership outside the returned value.

Dust is deliberately represented by emission events rather than mutable
particles or an RNG stream. Particle variation is a counter hash of emission
id, particle index, and property index; position, rotation, size, and lifetime
are evaluated from event age. Metal expands those values into billboard quads
with a mesh shader and retains an instanced vertex fallback. Rewinding dust
therefore restores logical time and the small event log, not hundreds of
individually integrated particles.
