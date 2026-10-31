# Uber performance mode

The owner's bar (2026-10-11): **144 fps or more, sustained, with 10,000 to
40,000 units alive and fighting** (horde members count as units), the 1% low
also at or above 144 (a frame under 6.9 ms), no hitches, never a frame that
waits on a logic step. Reference machine: the owner's PC (RTX 4090, 24 CPU
threads, Windows). The Steam Deck's numbers are reported next to it.

This file is the design for getting there, the budget per system, the phases,
and the recorded measurements from UBER-1 / RENDERWAIT-1. The dated numbers below
are historical measurements, not a fresh benchmark of this checkout. Lane UBER-1 delivered phase U1;
U2-U5 remain planned work.

## What the mode is

Retail RotWK runs its logic frame on one thread. OpenBFME's default
(**Faithful**) simulation ports retail's. Retail replay compatibility is a goal,
not a supported input format here: the recorder reads OpenBFME replay versions
5, 6 and 7. The measurements below used about one core, not a fixed core limit.

**Uber** is a second simulation profile (`Common/SimulationProfile.h`), off by
default:

- **The switch:** Options → OpenBFME → Performance → "Uber performance mode"
  (`Options.ini OpenBFMEUberPerformance = yes`). Its help line warns that it
  isn't compatible with the original game, and it takes effect from the next
  game. The setting is locked while inside an online or LAN room; leave the
  room to change it.
- **Same rules, different computation.** Uber plays the same game: same INI
  data, modules and rules. But it may compute things differently from retail:
  a different order of independent work, snapshot semantics for queries, and
  per-thread scratch. That lets the work spread over every core and later the
  GPU.
- **Deterministic among Uber machines.** Every Uber peer computes bit-identical
  state whatever its core count, OS or compiler, with the same numeric facade
  rules as Faithful (no FMA, no CRT maths, no pointer or hash-order
  dependence). So Uber players can play each other in lockstep.
- **Never mixed.** The profile is in:
  - the game setup (`SkirmishGameInfo::simulationProfile`);
  - the LAN options and start;
  - the replay (current writer: format 7; readers accept 5, 6 and 7);
  - the profile identity (`profile uber`, compared at every lobby join and
    start and by replay playback);
  - the state hash.

  A mismatched running peer or replay is refused, never mixed into the game.
  Joining a room can first adopt that room's profile; the locked setting then
  reflects the room, rather than the previous preference.
- **Faithful's results don't move for Uber; some of its code paths did.**
  What is unchanged:
  - a Faithful game's state hash values (the PERF-1 per-frame vectors,
    `perf1: the big battle ... hash as the build before PERF-1 did`, the PERF-2
    matrix and the AI-vs-AI pins pass unchanged);
  - its profile identity text (`profile enhanced`, no new line).

  What did change:
  - the hash now goes through UBER-1's staging code: the sections are listed
    first, then fed in the same order into the same chain;
  - UBER-1 introduced replay format 5 (the setup's profile field); the writer
    now uses 7 (slot handicaps), and reads 5 / 6 / 7, not 4;
  - the LAN wire's GameInfo carries one more field.

  Uber has its own pin set (`test_uber1.cpp`, `tests/data/uber1/`).

What Uber may change, phase by phase, is listed under each phase below. Every
rule change is opt-in through the profile and leaves Faithful's results alone;
a shared code path (like the hash staging) is allowed only when the Faithful
pins prove its results unchanged.

## The benchmark (the Uber gate)

Linux / POSIX shell, from the project root, after building the C++ test target
into `engine/build` and setting `ROTWK_INSTALL` and `BFME2_INSTALL` to the game
folders (without both, this benchmark skips):

```sh
./engine/build/openbfme_tests -tc="uber bench*" --no-skip
```

Windows: use `engine\build\openbfme_tests.exe` from a command prompt with the
same environment and arguments. The implementation is `engine/tests/test_uber1_bench.cpp`:

- the setup: 8 players on map mp fall back 8p, players 1-4 Men against 5-8
  Mordor, as human slots, so no computer player acts;
- the armies: rows of fighter, tower shield guard and archer hordes facing
  each other across the middle of the map, made until there are at least N
  living troops (N = 10k / 20k / 40k). Troops are counted by
  `GameClient/FrameCensus.h`: living INFANTRY, CAVALRY or MONSTER objects, so
  every horde member counts and the horde controllers don't;
- the battle: two settling frames, then every horde attacks the enemy horde
  facing it. 50 warm-up frames (10 game seconds) let the armies march into
  contact; then the measured frames are the battle itself.

Environment variables: `OPENBFME_UBER_UNITS`, `OPENBFME_BENCH_FRAMES`,
`OPENBFME_UBER_WARMUP`, `OPENBFME_UBER_PROFILE` (faithful / uber / both) and
`OPENBFME_LOGIC_THREADS`. Defaults: unit counts `10000,20000,40000`, 50 measured
frames, 50 warm-up frames, both profiles; the thread override sizes the logic pool.

Reported per battle:

- the living troops: made, at the start of the window, the sustained mean over
  the window, the minimum and the end;
- **the tick**: logic plus the state hash, which every recorded game (every
  skirmish and LAN game) computes after each frame. Its mean, p99 and max, and
  the ticks over the 200 ms of the 5 fps logic clock;
- the logic frame's wall time and each of its six phases;
- the state hash's time and its own job-pool dispatches, counted apart from
  the frame's (PERF-2's collision jobs);
- the cores used (process CPU / wall, unavailable in this benchmark on Windows)
  and the hash chain.

**The game gate** is the same battle in the real client: the logic on its
worker, replay recording with its per-frame hash, drawables, terrain, HUD:

Linux / POSIX shell, from the project root, with Godot 4.7.2, the matching built
GDExtension, a working graphical session, and both game-folder environment
variables above. Set `OpenBFMEUberPerformance = yes` (or `no`) in the options
file; `--options-file=F` can select a separate file.

```sh
godot --path godot -- --auto --map="maps/map mp fall back 8p/map mp fall back 8p.map" --ai=2 \
  --uber1=10000 --uber1-seconds=20 --vsync=off --perf
```

`scripts/uber1_player.gd` makes the armies through `GameWorld.create_object`
until `GameWorld.get_census()` counts the troops, then orders every horde to
attack the horde facing it through the player command path
(`GameWorld.order_attack`). It times the render frames for the window and
prints:
- the average fps, the 1% low (the fps of the 99th percentile frame) and the
  worst frame;
- the logic frames run per second (5 = the logic kept its clock);
- the living troops at the start and end of the window;
- whether the owner's 144 fps bar was met.

One count per run.

The instancer's own bench isolates N animated soldiers through the GPU-skinned
W3DInstancer. With the same Linux graphical prerequisites, from the project root:

```sh
godot --path godot res://scenes/w3d_viewer.tscn -- --bench=10000
```

## Where a frame goes today

### The lane machine

Every number below is from the lane machine (the "Deck" of the briefs).
Godot reports its GPU as "AMD Steam Machine (RADV NAVI33)"; it has 12
hardware threads and was idle apart from other lanes' light work (load average
0.5-1.2). The reference PC: see the end of this file.

### Logic: the tick (logic + hash), the battle after 10 game seconds of marching

| Troops made (sustained in the window) | Faithful tick | Uber tick | Logic | Hash, Faithful → Uber | Ticks over 200 ms, Faithful → Uber |
|---|---|---|---|---|---|
| 10,079 (9,393) | 209 ms (p99 258) | 192 ms (p99 244) | 173-179 ms | 35.9 → 13.2 ms | 20 → 11 of 30 |
| 20,069 (19,510) | 364 ms (p99 489) | 322 ms (p99 444) | 295-298 ms | 69.0 → 23.2 ms | 30 → 30 of 30 |
| 40,049 (39,512) | 647 ms (p99 850) | 561 ms (p99 769) | 514-520 ms | 132.3 → 41.5 ms | 20 → 20 of 20 |

Details:
- 30 measured frames (40k: 20), 12 logic threads.
- The 40k phases (Uber) were 17 / 111 / 176 / 81 / 104 / 32 ms. Phase 2 is
  the partition update and collisions; phases 3-5 are the module updates and
  the AI row.
- The Uber hash ran 2 dispatches per frame, with 5,016 / 9,267 / 11,870 of
  their chunks on worker threads.
- Cores used: 1.1 Faithful, 1.6-1.7 Uber. The 40k peak RSS is 1.5 GB.
- Right after the mass order (the first frames, before the warm-up) the
  logic's p99 is 420-1290 ms: the path burst (PATHPERF-1).

### The game gate (the real client, 1280x800, vsync off, 20 s window)

| Troops | Profile | Average fps | 1% low | Worst frame | Logic frames/s (5 = on clock) |
|---|---|---|---|---|---|
| 10,000 | Faithful | 5.5 | 3.1 | 321 ms | 3.1 |
| 10,000 | Uber | 6.1 | 3.5 | 297 ms | 3.3 |
| 40,000 | Uber | 1.6 | 0.9 | 1,174 ms | 1.1 |

So the owner's bar is missed by about 24x at 10k and 90x at 40k on this
machine.

`--perf` breaks the 10k Uber frame down:
- the main thread's CPU is 51 ms a frame: the HUD update 8.5, the drawable
  sync 6, the pose and upload 4, and the logic hand-over 27;
- the frame takes 164 ms.

So about 110 ms of every frame is spent waiting, most likely for the logic
worker to finish its frame (the LiveGame accessors wait for the worker), and
the logic itself runs below its clock (3.3 of 5 frames a second). At 40k the
main thread's CPU is 166 ms and the frame 642 ms.

**Finding (UBER-1): the render did wait on the logic.** RENDERWAIT-1's wait
log (below) named the one site: the spell ring's per-frame HUD query
(`spell_target_ring.gd` -> `get_spellbook_state` -> `InGameHud::enterContext`
-> `LiveGame::waitIdle`), 12.8 s of waits in the 20 s window. Since
RENDERWAIT-1 the per-frame render reads use the client window; see "The
render and the logic".

Where the logic frame goes (perf on the lane machine, one thread, Faithful, 20k
units, the first 12 frames after the order, share of the logic frame):

| System | Share | Notes |
|---|---|---|
| Pathfinding (`processPathfindQueue` -> `internalFindPath`) | 33% | the order's 2,400 horde paths and their members; occupancy checks (`occupantCost`, `isParked`) dominate the search |
| Module updates, AI side (`AIUpdateInterface`, state machines, attack states) | 19% | per unit, serial, reads and writes neighbours |
| Collisions (`processCollisions`: snapshot, pair search, `onCollide`) | 16% | the snapshot and pair search are already parallel (PERF-2); the pair application is serial |
| Taunt / alert scans (`EmotionTrackerUpdate::tauntScan` -> `getClosestObject`) | 12% | a partition query per idle unit every few frames |
| Target scans (`TargetFinder`), large group bonus, horde member passes | 14% | partition range queries |
| Shroud (`ShroudManager`) | 3% | per looker, per frame |
| Rest (partition update, scheduler, players) | 3% | |

Over the whole run the logic grows about linearly with the troops (13-18 µs per
troop per frame on the lane machine), plus the path burst after a mass order.
So 40k troops are 2.8x over the 200 ms tick, before any client work.

### The soldiers alone (lane machine GPU, 1280x800, instancer bench)

| Animated soldiers | Average | fps | CPU pose evaluation | Palette upload |
|---|---|---|---|---|
| 1,000 | 1.13 ms | 887 | 0.4 ms | 0.3 ms |
| 10,000 | 8.64 ms | 116 | 3.9 ms | 3.9 ms |
| 40,000 | 37.2 ms | 27 | 15.8 ms | 17.3 ms |

The bench runs on 4 pose worker threads. At 40k the CPU computes and uploads
every bone of every soldier every frame: 40k x 23 bones = 2.76 M palette
texels. That cost, not the GPU's triangles, is what sets the frame. A game adds
the drawables' snapshot sync (`DrawableManager`, parallel since PERF-2), terrain,
water, particles, shroud and the HUD.

### The render and the logic (state today)

SMOOTH-1 runs the logic on its own worker thread (`LiveGame` `logicThread`)
and the presentation interpolates behind a presentation delay. When a logic
frame is late, the presented time stops at the end of the last completed frame
(S-1140).

**RENDERWAIT-1: per-frame live reads use the client window.**

Accessors outside the window can still wait; one-off teardown and mission
starts deliberately drain the worker. The static per-frame test excludes these
one-off operations, not normal HUD reads.

- **The wait log.** Every wait of a non-worker thread on the logic worker is
  logged by site: each `LiveGame` accessor passes its caller's file and line,
  the device layer adds the GDScript caller (or the native offsets of an
  engine callback), and contended locks of the publication mutex are timed.
  `GameWorld.get_logic_wait_sites()`; `--perf` prints `GAME PERF WAIT` lines
  per 5 s window, the Uber battle `UBER1 wait site` lines.
- **The client window.** The render thread reads and commands the live logic
  only in its render frame's client window: from SceneTree `process_frame`
  until `LiveGame::advance` has queued the frame's logic frames (at the latest
  `frame_pre_draw`), and only while the worker is between frames (it starts
  no frame while the window is open). That covers the HUD's update and queued
  input, its movie presses, the audio, the effects' logic part, the tree
  events and the per-frame scripts. A render frame that finds the worker
  inside a frame skips that work; the worker holds after its frame (at most
  50 ms) for the next render frame's window. The accessors return at once in
  the window. Per-frame scripts consult `GameWorld.is_logic_held()` before a
  live read (the pytest `tools/perf3/test_renderwait1_per_frame.py` enforces
  it). The logic's frames run whole, in order, with the same commands; only
  their start times move (`test_renderwait1.cpp` pins the state hash against
  the single-threaded run).
- **Input keeps its frame (review r2).** Commands issued in a window while
  older frames are still queued wait as pending input and run in the first
  frame queued after them, exactly as when every accessor drained the worker
  first; in a driver game (recording, lockstep) the input is pumped only with
  the worker idle and no batch is acquired until then. The campaign's script
  presentation (`campaign_cine.gd`) reads published data (the followed
  object, the script requests) and defers its live reads and the requests'
  audio to a held frame.
- **What is not bounded.** While a window is open the worker cannot start a
  frame: the render thread is reading the live logic. The window lasts for
  the render thread's work before `advance` (25.6 ms at most in the 10k
  battle, 62.7 ms in a 4-player QA game with the scripted player); a window
  no advance ended is ended at the next render frame's start. Stop S-4280.
- **What it costs.** When the logic keeps its clock, nothing (the worker was
  idle anyway: 1.5 ms held per 5 s in the 4-player skirmish). When it falls
  behind, the HUD's live work runs once per logic frame instead of every
  render frame (stop S-4280), and the worker loses the wait for the next
  render frame plus the window: 41.5 ms per logic frame at 25 fps rendering
  in the 10k battle (less at higher render rates: 12 ms per frame of 250 ms in
  the 60 Hz doctest).

The 10k Uber battle in the real client on the Deck (1280x800, vsync off, 20 s
window):

| | Before (UBER-1's site) | After |
|---|---|---|
| Render-side waits on the worker | 78 waits, 12,832 ms | 0 (a few contended publication locks, 0.1-0.2 ms) |
| Average fps / 1% low / worst frame | 7.2 / 3.5 / 290 ms | 30.5 / 15.7 / 122 ms (run 1); 24.5 / 11.4 / 180 ms (run 2, other lanes busier) |
| Logic frames per second | 3.88 | 4.65 (run 1); 3.20 (run 2) |
| Back-to-back, both under other lanes' builds (load ~12) | 2.9 fps, logic 1.92/s | 13.6 fps, logic 1.99/s |

Per second of the window, render frames against logic frames (the worker's
frames take 180-370 ms; before and run 1):

| Second | Before: render frames, worst frame | After: render frames, worst frame | Logic frames (before / after) |
|---|---|---|---|
| 2 | 7, 273 ms | 30, 66 ms | 4 / 4 |
| 6 | 9, 203 ms | 30, 59 ms | 5 / 4 |
| 10 | 8, 215 ms | 31, 64 ms | 4 / 4 |
| 14 | 7, 226 ms | 31, 61 ms | 4 / 3 |
| 18 | 8, 221 ms | 32, 57 ms | 4 / 3 |

Before, every render frame lasted as long as a logic frame; after, the render
runs at its own rate (here the 10k soldiers' CPU work bounds it) while the
logic frames take 200-370 ms. Making the logic finish inside its tick is
Uber's other half.

## The budget at 40,000 units

**Logic.** On the reference PC the logic tick must finish in its 200 ms (5 fps
clock) with room to spare. The target is 100 ms of wall time on 24 threads,
which leaves room for a slower peer: lockstep runs at the slowest machine's
pace. The Deck's target is 200 ms at 20k.

The "today" column splits the measured 40k Uber logic (520 ms) by the 20k
profile's shares and adds the measured hash.

| System | Today, 40k Uber on the lane machine | Target at 40k on the reference PC (unvalidated) | How (phase) |
|---|---|---|---|
| Pathfinding | ~170 ms, bursts of seconds after a mass order | 15 ms | parallel A* with per-thread scratch (U2), flow fields for groups (U3) |
| Module updates, AI side (state machines, attack states) | ~100 ms | 35 ms | parallel read / serial commit per module class (U3), SoA horde members (U3) |
| Collisions | ~85 ms | 10 ms | parallel pair application by conflict-free batches (U3) |
| Taunt, target and range scans, horde member passes | ~135 ms | 15 ms | snapshot queries on the job pool with per-thread partition buffers (U2) |
| Shroud and visibility | ~15 ms | 3 ms | parallel per player (U2), compute shader for the client side (U4) |
| Serial glue (partition update, scheduler, destroy list, players) | ~15 ms | 18 ms (commits added) | |
| State hash | 42 ms measured (Faithful: 132 ms) | 4 ms | parallel sections (U1, done) |
| **Total** | **~562 ms (measured tick: 561 ms)** | **~100 ms** | |

**Render.** The frame has 6.9 ms on the reference PC, with the logic off this
thread and no waits on it. These are targets, not measurements.

| System | Target at 40k | How |
|---|---|---|
| Snapshot interpolation and drawable sync | 1.0 ms (parallel) | already parallel (PERF-2); SoA snapshot (U3) |
| Animation | 0.3 ms CPU | GPU pose evaluation: animation data in textures, bones computed in the vertex or compute shader from (clip, time, blend) per instance; only the per-instance state is uploaded, 40k x 32 bytes (U2) |
| Culling and LOD | 0.3 ms CPU | GPU frustum culling and LOD selection (compute, indirect draws), impostors at distance (U3) |
| GPU: skinned instances | 3.0 ms | LOD so that 40k visible soldiers stay under ~25 M triangles a frame (U3) |
| Particles | 0.5 ms | GPU particle simulation (compute), CPU only spawns (U3) |
| Decals, selection rings, health bars | 0.3 ms | one instanced batch each (U3) |
| Terrain, water, shroud | 1.0 ms | shroud texture from a compute pass (U4) |
| HUD, UI | 0.5 ms | |

**What stays on the CPU for determinism.** Everything the simulation reads:
- the positions and paths, the targets, the damage and the shroud the logic
  uses;
- anything that decides a game event.

GPUs differ in their float results and their scheduling, so the GPU only draws
and simulates what never reaches the logic: particles, the drawn animation,
culling, decals and the client shroud texture. A GPU flow field would have to
be integer-exact and verified across vendors before the logic could read it.
That's a research item, not a plan.

## How determinism is proven

- **Across thread counts:** `uber1: the Uber battle hashes as its pin vector
  in every frame with 1, 4 and 16 logic threads and in a repeated run`:
  - the battle: at least 3,000 living troops, 8 players, 40 frames, a hash
    after every frame;
  - each run is compared with the stored Uber pin vector
    `tests/data/uber1/battle3000_40.hashes`;
  - the hash's own dispatches are counted apart from the frame's, and with
    more than one thread its chunks ran on workers;
  - a Faithful hash dispatches once (lane PERF-HD: its words are recorded on
    the pool, the objects' and the other sections', and chained in order on
    the calling thread: the serial walk's value).

  Every phase adds its own battle and threads to this test.
- **Across machines, with every parallel simulation change (not only at U5):**
  a change lands only after its Uber pin vector matched on at least two
  machines with different CPUs, and (from U2 on) in a native Windows run of
  the suite. UBER-1's evidence:
  - the pin vector made on the reference PC (Ryzen 9 5900X, WSL, 24 threads) passes
    in the remote gate's suite on a CI host (Ryzen 7 5800H, 12 threads);
  - the bench's hash chains (Faithful and Uber, 10k and 20k, 30 frames) are
    equal on the lane machine (12 threads) and the reference PC (24 threads).

  Not yet covered: the gate cross-builds the Windows game but doesn't run its
  suite. A native Windows run of the Uber pin test is the first item of U2's
  checklist.

  U5 automates it (Deck, the reference PC Windows native and WSL, a Mac, 1 / 4 / 16 /
  24 threads) and adds LAN peers with different `--logic-threads`
  (`openbfme_peer --uber`), as PERF-2 did for Faithful.
- **Rules every Uber change follows:**
  - fixed partitioning (`JobSystem::parallelFor`: chunk k is always
    [k * grain, (k + 1) * grain));
  - results written to the slots of the items a chunk owns, combined in item
    order on the calling thread;
  - no logic RNG in a parallel body (U3 gives each object its own stream,
    derived from the seed, the frame and the object id);
  - no object creation or destruction in a parallel body (requests are queued
    and committed in order);
  - the numeric facade's floating-point environment checked per chunk, as
    PERF-2 does.
- **Faithful results unchanged:** the PERF-1 per-frame vectors (400-frame
  battle, 6000-frame 4-player skirmish), the PERF-2 matrix and the AI-vs-AI
  pins (see "What the mode is" for the code paths and formats that changed).

## Phases

### U1: the switch, the plumbing, the benchmark, the first speed-up (UBER-1, done)

- The option and the profile everywhere it must be:
  - setup and wire: `SkirmishGameSetup`, `GameInfo`, `NetPacket`;
  - LAN: `LANAPI` (the host's game runs its identity's profile; options or a
    start of another profile are refused) and `LANLobby`;
  - replay format 5 at U1 (now writer 7, readers 5 / 6 / 7) and `ProfileIdentity` (`profile uber`); a replay header
    whose identity and setup name different profiles is never written, never
    read, never played (UBER-1 review);
  - `GameLogic` (hashed only when Uber);
  - `GameWorld.set_simulation_profile`: `prepare_new_game` and
    `start_new_game` refuse a game of another profile;
  - `game.gd` applies the option before a lobby, a game or a replay;
  - the load screen line and `openbfme_peer --uber`.
- The benchmark and the game gate above.
- **The speed-up: the Uber state hash.** Every section of the state hash on
  its own on the logic job pool, the objects section one object per slot, then
  the section values chained in their fixed order. The hash is 2.7-3.2x faster
  at 10k-40k (132 → 42 ms at 40k), 8-13% of the tick. The
  only rule Uber changes so far is the hash function, so its simulation is
  otherwise the Faithful one.
- Why the hash first: pathfinding is the biggest hotspot, but parallel A* needs
  per-thread search scratch inside the pathfinder that PATHPERF-1 is reworking
  right now (the retail request queue), and a second lane in the same code
  would collide. The hash was the biggest change that is safe on its own: a
  pure function of the state, on every frame of every recorded game.

### PATHPERF-4: the path queue's limits (done)

Retail's path queue holds 511 units: a unit refused by a full queue waits for
a path that never comes until its next order. It also serves only
`MaxPathfindCellsPerFrame` cells a call, so a mass order is served over
seconds (150 hordes: 4.6 s, 300 single soldiers: 9.6 s, `pathperf1 bench`).

- **Uber rule change:** the two request rings hold 16,384 units
  (`Pathfinder::setUberLimits`), and during a burst the cell budget is 4 times
  retail's: a burst starts at 120 waiting requests (a mass order) and lasts
  until 40 or fewer wait. A sustained battle's repaths keep 60-90 waiting
  (10,000 troops) and never start one; an always-4x budget only spent its
  time on that steady trickle (logic 232 instead of 197 ms a frame at 10,000
  troops, one thread). The 150 hordes' order is served in 1.6 s, the 300
  soldiers' in 4.6 s (`PATHPERF1_PROFILE=uber`). The burst flag is in the
  Uber state hash; the Uber pin vector was regenerated.
- **Uber rule change:** the pathfinder's cell-info pool grows past its 60,000
  records up to 1,000,000 (`Pathfinder::setUberLimits`), instead of failing
  searches and reservations in a big battle. The Faithful pool keeps its
  60,000 and fails when full, as before: RotWK 2.01's own limit has not been
  read from the binary (BFME1 keeps 0x200 fixed slots plus an overflow list,
  Open-BFME-1 `PathfindCellInfoPoolReset.cpp`), so the growth is the profile's.

### UBER-2A: the "what is near me" queries (done)

The 10k Uber profile had the taunt scans (`tauntScan` -> `getClosestObject`)
at 10% of the logic frame, nearly all of it the exact x87 distance of every
partition entry the search visited, plus the combat range checks recomputing
the bounding radii (6%), the large group bonus scans (4%) and `TargetFinder`
(4%, most of it rebuilding its grid about a dozen times a frame).

- **Both profiles (the same answers):** `getClosestObject` skips the exact
  distance of a candidate the double-precision bound puts beyond the best
  distance so far (the range walk's pre-reject, now inlined), and keeps its
  heap storage between calls. The Faithful pins and the Faithful bench hash
  chain are unchanged. (The bounding radii are UBER-2B's cache.)
- **Uber rule changes:** the taunt scan, the dual weapon switch scan and
  `TargetFinder` walk only the layers of the players the seeker sees as
  enemies (`Team::enemyLayerMask`), the large group bonus only its owner's;
  `TargetFinder` builds its grid once a frame and appends the objects made
  later in the frame instead of rebuilding. What a player can notice: nothing
  (of two candidates at exactly the same distance another may win; an object
  whose owner changed this frame is found on its old layer until the next
  partition update).
- Result (`uber bench` 10k, Uber, 30 frames, back to back on a busy PC): logic
  wall about 173 -> 137 ms, phases 3 / 4 about 33 / 30 -> 23 / 19 ms; the
  taunt scan went from 10% to 1.5% of the logic frame. The Uber bench hash
  chain did not change either (no tie or owner change met in that battle).

### UBER-3P: the battle's path searches that cannot succeed (done)

In the 30,000-troop clash the path queue runs at its budget in every one of a
logic frame's six calls, and each call is mostly one search: a repath toward
a contact point in the press whose goal cannot be reached (an approximate
search: the goal is in another zone or not a valid movement position; or its
goal cell was rejected). Such a search ends with the path to its closest cell
(or none), but kept expanding up to the slack or `MaxCellsFindPathLimit`: the
calls averaged 23,000 cells against a budget of 4,000. Of 2,094 searches in
that battle, a failed search's closest cell was final after about 10 cells
and never replaced 250 cells later; 1,182 of the 1,644 unreachable searches
found no closest cell at all and gave no path, after 72% of all their cells.

- **Uber rule change:** `internalFindPath` ends an unreachable search once its
  answer has held for 256 cells (the closest cell, or with none yet the
  candidate key that would test one) and gives that answer. A tunnelling
  search's pause starts when it leaves its obstacle. What a player can
  notice: nothing; the paths are served sooner (in that battle 45 of the
  unreachable searches tested a candidate after such a pause, the only ones
  whose answer can differ). The Uber pin vector was regenerated.
- Result (`uber bench` 30k, Uber, 30 frames, back to back, two rounds): logic
  wall 203 / 221 -> 175 / 181 ms (default threads), 240 -> 215 ms (one
  thread); the 15 clash frames 220 / 242 -> 179 / 184 ms, p99 316 / 363 ->
  240 / 261 ms. The queue's cells per call 23,000 -> 8,600, the horde repaths
  served in the 14 clash frames 170 -> 209. The 10k battle is unchanged within
  the noise (83-89 -> 86-94 ms default threads, 103-107 -> 106 ms one thread).
- **Uber rule change (second step):** `findAttackPath` with no endpoint yet
  ends once its closest cell has held for 256 examined cells and gives the
  fallback the exhausted search gives (the path to that cell). In the clash
  126 of 200 attack searches found no endpoint and spent 75% of their cells
  after such a pause; no endpoint of the other 74 came after one. Result on
  top of the first step (30k, 30 frames, a busy PC): 174 -> 161 ms default
  threads (clash frames 182 -> 154), 254 -> 237 ms one thread; the 10k battle
  within the noise. The Uber pin vector was regenerated again.

### PERF-HD: the queue's searches capped at the call's budget (done)

On Helm's Deep (11,700 objects) the profile ran 1.9x slower than Faithful with
logic frames of up to 1.4 s: PATHPERF-4's burst budget never ended in the
sustained melee (the queue sits at 300-450 requests), and retail's cell limit
counts only cells that passed the movement checks, so a search in a press
(nearly every neighbour occupied: a record already there, closed uncounted)
closed 200,000+ cells, plus up to 25,000 of the shortcut walk.

- **Uber rule change:** a request the queue serves is capped at the cells the
  call has left of its budget (at least 2,000), counted in every cell the
  search closes or walks, in every search kind (the main loop, the attack path,
  the hierarchical block search, the patch, the safe and the move-away
  searches); the capped search gives the exhausted search's answer. The cell
  search after a block search (in `findPath` and `findAttackPath`) and the
  attack path's fallback search each count from zero, so a block search that
  spent the cap still leaves them a closest cell. The burst
  scale is 2. What a player can notice: a unit in a press gets a partial path
  sooner. The Uber pin vector was regenerated.
- **Uber rule change:** a contained object's EmotionTrackerUpdate sleeps until
  it leaves its container (Faithful calls it every frame for every horde
  member; the wake frames are in the state hash).
- Result (Helm's Deep, seed 7, 400 frames, headless): logic 180 -> 88 ms a
  frame (Faithful 95), the worst frame 1,417 -> 384 ms, the melee's
  pathfinding over 100 -> 24-28 ms a frame. In the client the profile's mean
  frame is 4 % under Faithful's and its tail still worse: it plays the heavier
  battle (every path request served; Faithful's 511-slot ring refuses many).
- Result (the uber bench, map mp fall back 8p, 8 players, 16 threads, 50
  frames, mean tick of two runs, without and with this rule): 10k troops
  132 -> 92 ms, 20k 204 -> 133 ms, 40k 345 -> 217 ms; ticks over the 200 ms
  clock at 20k: 23-30 -> 0-1 of 50, at 40k: 49-50 -> 25-29.

### U2: parallel queries and paths, GPU animation (next)

- **Snapshot queries.** Before each module phase, the queries a module will
  ask (taunt scans, target acquisition, the large group bonus, the
  `TargetFinder` scans) are answered in parallel against the phase-start state.
  The partition manager gets per-thread hit buffers (its `m_hitPool` is shared
  today). Modules then read their answers in the serial order. Uber rule
  change: a query sees the phase start, not the moves of the modules that ran
  earlier in the same phase.
- **Parallel pathfinding.** The frame's queued requests are solved in parallel
  against the frame-start grid, each search with its own open list and cell
  scratch, and the paths are committed in queue order. Built on PATHPERF-1's
  merged queue. Uber rule change: every request of a frame sees the same grid.
  - What bounds the time: a request count doesn't, since one search can expand
    the whole map. A wall-clock budget would be non-deterministic. So the
    bound has to be a work budget counted in expanded cells, the same on every
    machine: a search that runs out continues in the next frame, as retail's
    queue does.
  - Even that bounds the tick only if the budget times the slowest supported
    machine's cost per cell fits; that is measured per machine class, not
    assumed. U3's flow fields then cut the work of group moves at the source.
- **No main-thread waits on the logic** (with PATHPERF-1): the client reads
  only published snapshots, so a slow logic frame never stops a render frame.
- **GPU animation.** The pose evaluation moves to the GPU: clips baked into
  textures, per-instance clip, time and blend uploaded, bones evaluated in the
  shader. This is Faithful as well (presentation only). The 40k bench's 33 ms
  of CPU pose and upload go to about 0.5 ms.
- The game gate runs on the reference PC (idle) and the Deck at
  10k / 20k / 40k.
- **Target (unvalidated until idle reference-PC measurements exist):** 10k at
  144 fps on the reference PC; 10k logic inside the tick on the Deck.

### U3: data-oriented movement, flow fields, GPU culling

- **Flow fields** for group moves: one integration field per destination and
  locomotor class, shared by every horde and member that goes there, computed
  in parallel tiles (integer costs).
- Horde members as structure-of-arrays (position, velocity, slot, target),
  updated in parallel with a serial commit.
- **Collisions:** pairs applied in conflict-free batches (no object in two
  pairs of a batch; the batches in fixed order).
- **Per-object RNG streams** for the parallel bodies that draw randomness.
- **GPU:** culling and LOD selection, indirect draws, impostors at distance,
  GPU particles, instanced decals and selection rings.
- **Target (unvalidated):** 20k at 144 fps on the reference PC; 40k logic inside
  the tick on the reference PC.

### U4: AI on threads, parallel module phases, GPU shroud

- Each skirmish AI player plans on its own thread against the frame snapshot.
  Its commands are merged in player order into the next frame's command list.
- Module phases split by spatial tiles with a halo: a tile's modules run in
  parallel when their reads and writes stay inside the tile, and the cross-tile
  work commits serially in object order.
- Shroud and visibility: per player in parallel for the logic, a compute
  shader for the client texture.
- **Target (unvalidated):** 40k at 144 fps sustained on the reference PC.

### U5: the gate in CI

- The Uber benchmark on every CI host: logic tick at 10k / 20k / 40k and the
  windowed run where a GPU exists, with average fps, 1% low and worst frame.
- Cross-machine hash vectors automated: Deck, the reference PC (Windows native and
  WSL), a Mac; 1 / 4 / 16 / 24 threads. (Each parallel change already checks
  two machines and Windows by hand, see above.)
- Uber LAN soaks with mixed thread counts, Uber replays.

## What's realistic when

Every row after "Today" is an unvalidated target until the game gate has
measured it on an idle reference PC.

| Milestone | 10k units | 40k units |
|---|---|---|
| Today (U1), lane machine | tick 192 ms (on the edge of the 200 ms); the game 6.1 fps (1% low 3.5); the soldiers alone 116 fps | tick 561 ms (2.8x over); the game 1.6 fps (1% low 0.9); the soldiers alone 27 fps |
| After U2 | 144 fps on the reference PC; the Deck at 60 fps with the logic inside the tick | logic about 2x over the tick on the Deck; render limited by the GPU, not the CPU |
| After U3 | 144 fps with margin; the Deck near 90 fps | logic inside the tick on the reference PC; 144 fps with LOD where the camera sees part of the army |
| After U4 | | 144 fps sustained on the reference PC, the 1% low included; the Deck at 30-60 fps at 40k (its 4 Zen 2 cores and 8 CUs are the limit) |

These are estimates from today's profile, not promises. The game gate's 6 fps
at 10k shows how far the client is from the bar: the waits on the logic and
the client's own per-object work have to go before any GPU work shows. The
risks:
- serial dependencies in the module updates that tiles can't break: horde
  members that reach across the map, garrisons and transports;
- the pathfinder's occupancy checks, which read live unit positions;
- the GPU's triangle load at the RTS camera when 40k are on screen;
- Amdahl: today 70% of the frame is serial module and path work.

## the reference PC numbers

The logic bench on the reference PC (Ryzen 9 5900X, 24 threads, WSL), 2026-10-12.
Conditions:
- the machine idle: no CI unit, load average 1-2 before and after;
- the desktop lock held for the run;
- run as a CI lane job: nice 10, capped at 16 cores. That is not the normal
  priority a reference measurement asks for. Pausing the CI would have
  stopped the lane job itself.

| Troops | Faithful tick | Uber tick | Uber logic | Hash, Faithful → Uber |
|---|---|---|---|---|
| 10k | 285 ms | 256 ms | 242 ms | 44.6 → 13.6 ms |
| 20k | 516 ms | 452 ms | 428 ms | 86.4 → 24.5 ms |
| 40k | 931 ms | 785 ms | 741 ms | 173 → 44 ms |

The serial logic runs about 40% slower here than on the lane machine. That
points to the WSL and lane-job conditions rather than the CPU, so these are not
the reference PC's numbers.

Every hash chain (Faithful and Uber, 10k / 20k / 40k) is equal to the lane
machine's: the same simulation on another CPU and thread count.

Still to do with the owner's desktop (a GPU job, one
job at a time):
- the reference measurement proper: the game gate (`--uber1`) in the native
  Windows build on the RTX 4090, idle, at normal priority;
- a native Windows run of the Uber pin test.
