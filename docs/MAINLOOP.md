# The BFME2 main loop: how the game refreshes frames and positions

How BFME2 1.06 (`game.dat`) runs its main loop, read from the decompilation at
[Open-BFME/Open-BFME-2](https://github.com/Open-BFME/Open-BFME-2) commit `d7b143eb97`
(2026-10-17). This is the reference for rebuilding OpenBFME's frame, update and
position logic.

Paths are relative to `Code/GameEngine/Source/` in that repository unless they say
otherwise. RVAs are image-relative (VA = RVA + 0x400000).

Each statement is labelled:

- **C** (confirmed): seen in a byte-matched source body, or read from `game.dat`'s
  disassembly.
- **C†**: confirmed by a parallel trace during the analysis, and spot-checked but
  not re-read line by line.
- **I** (inferred): strong evidence, not proven.
- **U** (unknown): not established.

Several names in the decompilation are placeholders (`rvaXXXXXXXX`, `m_unk`,
`g_00XXXXXX`). Some are misleading; where that matters it is called out.

> **Do not reimplement movement from these files.**
> `GameLogic/Object/Locomotor.cpp`, `GameLogic/Object/Update/PhysicsUpdate.cpp`,
> `GameLogic/Object/Update/AIUpdate.cpp` (`update`, `privateMoveToPosition`) and
> `GameLogic/Object/PartitionManager.cpp` are verbatim Zero Hour ports marked
> `present-unmatched`. They are not BFME2 code. Section 5 describes what the retail
> binary actually does.

---

## 1. The short version

1. **Logic runs at 5 Hz and drawing at 30 Hz.** `BfmeLogicFramesPerSecond = 5` and
   `BfmeClientFramesPerSecond = 30` (C, `Common/ColdGlobalDwordGetters.cpp:249`,
   `GameClient/LivingWorldEyeTowerProcessItems.cpp:16`). One logic frame lasts 200 ms
   and spans 6 client frames.
2. **One logic frame is 6 calls to `GameLogic::update(phase)`, phases 1 to 6, one
   per client frame** (C, `GameLogic/System/GameLogicInit.cpp:4645`, RVA 0x24555A).
   The work is spread across the phases. Commands and scripts run in phase 1,
   partition and collision in phase 2, AI and movement modules in phases 3 to 6.
3. **There is no real-time clock in the simulation.** Game speed is the loop rate,
   held at 30 iterations per second by a busy-wait frame limiter. With the limiter
   off (replay fast-forward), the game runs as fast as the CPU allows (C).
4. **Everything in logic is measured in per-frame units at 5 Hz.** INI values are
   converted when they are loaded:
   - speeds ×0.2;
   - accelerations ×0.2²;
   - turn rates in deg/s → rad/frame;
   - durations in ms ×0.005 → frames.

   Logic code never sees seconds (C).
5. **Movement is kinematic.** Each locomotor keeps a scalar speed in units per frame
   and moves the unit along its path polyline by that much each frame. It turns the
   heading separately at a clamped turn rate. There is no force, mass or velocity
   integration for normal movement (C).
6. **Rendering interpolates; drawables do not snap to the logic state.** Each
   drawable blends between the logic positions of two consecutive frames:
   - rotation: quaternion slerp;
   - position: a Catmull-Rom spline through four points, including a planned "next
     position" lookahead;
   - blend factor: `clientFramePeriod / 6`.

   As a result, **what you see lags logic by one logic frame (200 ms)** (C).
7. **Animations run on a client-frame clock of 33 ms per client frame, not wall
   time.** They stop when the client frame does not advance (C).
8. **Partition, collision and shroud updates are deferred.** Moving an object only
   marks it dirty. The spatial index and collisions catch up in phase 2 of the
   *next* logic frame, so collision response lags movement by one logic frame (C/I).
9. **Single player has no network object** (`TheNetwork == NULL`). Multiplayer is a
   host-authoritative lockstep at 5 Hz. The host ("packet router") stamps every
   command for `frame + 1`. Clients advance only when the host has announced that
   frame and all its commands have arrived (C).

---

## 2. Timeline of one logic frame

With the shipped rates every engine iteration does the same two things:

1. the **client half**: input, drawable updates, render;
2. one **logic phase**.

| Client tick | Client half (draw uses blend ratio) | Logic phase that follows |
|---|---|---|
| 1 | ratio 6/6 of the previous frame (lands exactly on its end state) | **phase 1**: frame counter +1, scripts, commands dispatched, drawables rebuild their interpolation endpoints |
| 2 | ratio 1/6 | **phase 2**: partition and collision managers drain their dirty lists; per-object transform snapshot |
| 3 | ratio 2/6 | **phase 3**: update list 0 (AI), first half |
| 4 | ratio 3/6 | **phase 4**: update list 0, second half |
| 5 | ratio 4/6 | **phase 5**: update lists 1 and 2 (physics and normal modules), `TheAI->update()`, shroud and others, **destroy list** |
| 6 | ratio 5/6 | **phase 6**: update list 3 (final) |

The client half runs *before* the logic phase in each tick. The draw therefore uses
the ratio computed in the previous tick, and the six renders of a logic frame see
1/6 … 6/6 (C, `Common/GameEngineFrameTiming.cpp:117-170`).

**Where a position comes from (I, from the phase order):**

| Logic frame | What happens |
|---|---|
| N, phases 3–6 | Modules move the object |
| N+1, phase 1 | The drawable rebuilds its spline from "end of N−1" to "end of N" |
| N+1, phase 2 | The partition and collision managers see the end-of-N position |
| N+1, all 6 client frames | The render glides from end of N−1 to end of N |

---

## 3. Outer loop and timing

### 3.1 Startup

| Step | Function (RVA) | Source |
|---|---|---|
| WinMain | `_WinMain@16` 0x2C39 | `Main/WinMain.cpp:1269` (command line, mutex, splash, window, then `GameMain`) |
| GameMain | 0x237D19 | `Main/WinMain.cpp:1036-1042`: `TheGameEngine = CreateGameEngine(); init(); execute();` |
| GameEngine constructor | 0x22DA66 | `Common/GameEngineInit.cpp:724-753`: period `+0x34 = 0`, counter `+0x38 = 1`, `timeBeginPeriod(1)` |
| `GameEngine::init` (vslot 14) | 0x22E2E4 | `GameEngineInit.cpp:755ff` |
| `GameEngine::execute` (vslot 15) | 0x22D290 | `Common/GameEngineExecuteRva0022D290.cpp:415-590` |

Relevant parts of `init` (C):
- `m_maxFPS = 45`, then overwritten with `GlobalData::FramesPerSecondLimit` after INI
  load.
- The shipped `GameData.ini` has `UseFPSLimit = Yes` and
  `FramesPerSecondLimit = 30` (I: data file, not code).
- `TheNetwork = 0`.
- At the end: `period = 0`, `counter = 30/5 = 6`, then a watchdog is created.
- Subsystem creation order: … MessageStream → GameClient → AI, ScriptEngine,
  LuaScriptEngine, TeamFactory, PlayerList → **GameLogic** → Recorder, Radar …

### 3.2 `GameEngine::execute`: the loop and the frame limiter (C)

```text
while (!m_quitting) {
    update();                                   // vslot 10 = Win32GameEngine::update
    // ---- frame limiter ----
    limit = useFpsLimit
    if (TacticalView.timeMultiplier > 1 || ScriptEngine.isTimeFast()) limit = false
    if (GlobalData.TiVOFastMode /*+0xBBD, replay fast-forward*/)    limit = false
    LOGIC_SCALE = 1 in single player (multiplayer: see 7.3)
    if (LOGIC_SCALE != 1) limit = true
    if (SavedClientFrame + 6 > GameClient.frame) limit = false        // catch-up after a skipped frame
    if (limit) {
        periodMs = (int)(1000 / (m_maxFPS * LOGIC_SCALE))             // 30 FPS -> 33 ms
        while (timeGetTime() - prev < periodMs) Sleep(0)              // busy-wait
        prev = now                                                    // no debt carried over
    } else prev = timeGetTime()
    watchdog.heartbeat()
}
```

- `Win32GameEngine::update` (0x41EC3, `Main/Win32GameEngineUpdate.cpp:81-104`) calls
  `GameEngine::update()`.
- While the window is minimised it sleeps 5 ms in a loop.
- It then pumps Win32 messages (`serviceWindowsOS`, 0x42121). **Mouse input is
  buffered here, at the end of the iteration**, and consumed by the next
  iteration's `GameClient::update` (C).

**Timers (C):**
- `timeGetTime` with `timeBeginPeriod(1)`: frame limiter, timed ops, FPS statistics.
- `QueryPerformanceCounter`: only the network router's pacing, and the display's
  FPS average.

### 3.3 `GameEngine::update` (vslot 10, 0x225DA9; `Common/GameEngineFrameTiming.cpp:117-170`)

| # | Step |
|---|---|
| 1 | `Rva0134FAA0->update()`: singleton created in W3DDisplay init; probably an asset registry (U) |
| 2 | Script-debug DLL hooks; inert without the debug DLL |
| 3 | **Client half**: vslot 39 `_bfme_updateClientSubsystems` (§3.4) |
| 4 | If the script debugger froze the game: `GameClient.advance = 0`; return |
| 5 | `GameClient.advance (+0xC8) = 1` |
| 6 | `++period`; `ratio (+0x3C) = clamp(period / counter, 0, 1)` |
| 7 | If `period > 6`: `period = 1`; **logic half** vslot 38 with phase 1. If logic did not run (not admitted), restore the old period so phase 1 is retried next iteration |
| 8 | Else: logic half vslot 38 with `phase = period` (2..6) |

Notes:
- **Logic stalls never skip phases.** Phase 1 is retried until it is admitted;
  meanwhile the period keeps counting past 6.
- `GameEngine::rva00225D38` (`Rva00225D38Check.cpp:23-30`) is the
  "first client frame of a logic frame" predicate (`period == 1`). It is used in 10
  places, including `GameClient::update`. The reconstruction names its result
  `shroudOn`, which is wrong.

### 3.4 Client half: `_bfme_updateClientSubsystems` (0x225C12; `Common/GameEngineClientSubsystems.cpp:168-219`), C

1. `TheGameLogic->deleteLoadScreen()`
2. If `GameClient.advance`: `GameClient.frame += 1`. The client frame counter only
   moves after a tick in which logic ran.
3. `TheAptPlayer->update()`: the Apt UI runs even on skipped frames.
4. If `shouldSkipClientFrame()` (multiplayer catch-up only, §7.3): count the skip and
   **return**.
5. Optional desync check: a logic CRC is taken before and after the client update;
   on mismatch it reports "GameLogic changed outside of GameLogic::update()!".
6. `TheRadar->update()`
7. **`TheGameClient->update()`**: input, drawables, render (§6)
8. **`TheMessageStream->propagateMessages()`**: translators → `TheCommandList` (§7.1)
9. Timed ops and input lock
10. `TheAudio->update()`
11. `TheNetwork->liteupdate(0)` if a network exists: receive, relay, keep-alives,
    send

### 3.5 Logic half: `_bfme_updateNetworkAndLogic(phase)` (0x22634F; `Common/GameEngineNetworkHelpers.cpp:248-305`), C

```text
ready = true
if (phase == 1) {
    ready = !GameLogic.isGamePaused()
    if (TheNetwork) {
        if (TheNetwork->getFrameAdvanceCount() <= 0 && !GameLogic[+0x9D]) ready = false
        else { TheNetwork->update(inGame); ready = true }       // multiplayer cannot pause
    }
}
if (ready) {
    TheGameLogic->update(phase)
    if (phase == 1) TheLivingWorldLogic->update()               // War of the Ring map
    // A loop that runs several phases per call exists for client/logic < 6;
    // it is dead code at the shipped 30/5 rates.
} else GameClient.advance = 0
```

- **Only phase 1 is gated** by pause and network. Phases 2–6 always run once their
  client tick comes.
- Frame-rate setter `Rva00238895SetFrameRates` (0x238895, `Common/FrameRateConfiguration.cpp:13-30`)
  clamps the client rate to at most 6× logic. It has no callers, so the rates are
  fixed (C).

### 3.6 Pause, speed and freeze

| Feature | Behaviour |
|---|---|
| Pause (single player) | 0x23CD9E sets `GameLogic+0x124`, disables input and pauses audio. Phase 1 is not admitted, so the frame counter stops. `GameClient::update` treats pause as freeze for drawables (C). Refused in multiplayer |
| Replay fast-forward | CommandXlat message 0x9C toggles `GlobalData+0xBBD`. The limiter turns off and `W3DDisplay::draw` renders 1 frame in 30 (C) |
| Script or camera time multiplier > 1 | Limiter off; the display renders every Nth call. No extra logic stepping (C) |
| Script or camera freeze | Phase 1 sees `freeze` and returns before incrementing the frame, unless message 0x1D is pending (C) |
| Single-step | Only through the script-debug DLL hooks; no retail single-step found (U) |

---

## 4. Inside one logic frame: `GameLogic::update(int phase)`

Source: `GameLogic/System/GameLogicInit.cpp:4645-4910`, RVA 0x24555A, 2437 bytes, C
unless marked.

### 4.1 Ordered steps

| # | Phase | Line | Action |
|---|---|---|---|
| 0 | all | 4647 | FP guard: `setFPMode()` on first entry (§8) |
| 1 | 1 | 4650-4654 | Script debug bookkeeping. Then `freeze = (view time-frozen && camera move unfinished) \|\| script frozen` |
| 2 | 1 | 4655-4656 | **`++frame` (`GameLogic+0x40`)** if in game, not loading, `!+0x9D` and not frozen. Happens at the *start* of the frame (ZH: at the end). Reset to 0 at game start, so the first frame is 1 |
| 3 | 1 | 4657-4664 | If frozen: unfreeze if message 0x1D is pending; otherwise `GameClient.advance = false` and **return** |
| 4 | all | 4667 | Clear the melee re-target queue `m_164` |
| 5 | all | 4669-4676 | War of the Ring strategic map (`+0x125`, single player): phase 1 only dispatches commands; **return** |
| 6 | all | 4678-4680 | `m_17c = phase`; "in update" latch |
| 7 | 1 | 4685-4692 | Frame 2 of a War of the Ring battle: every moving AI drops its path |
| 8 | **all** | 4694 | **`Pathfinder::ProcessPathfindQueue()`**: runs 6× per logic frame (§5.1) |
| 9 | all | 4695 | `setFPMode()` |
| 10 | 1 | 4697-4703 | `ScriptEngine`, `LuaScriptEngine`, `TerrainLogic`, `VictorySystem` → `update()` |
| 11 | 1 | 4705-4745 | Multiplayer CRC every `GameInfo+0x0C` frames (§8) |
| 12 | 1 | 4747-4755 | `StatsCollector`, `Recorder`, `TheFireLogicSystem` (trigger manager), `GlobalWeatherSystem`, `DOTManager`, invisibility sweep |
| 13 | 1 | 4756-4758 | **Commands:** every message in `TheCommandList` → `logicMessageDispatcher(msg)`; then `reset()` |
| 14 | 1 | 4759-4762 | **Logic → client hand-off:** every drawable rebuilds its interpolation endpoints, `Drawable::rva002747F9(0)` (§6.2) |
| 15 | 2 | 4765-4767 | `ThePartitionManager->update()`, `TheCollisionManager->update()` (§5.6) |
| 16 | 2 | 4768-4773 | Per object not yet snapshotted this frame: `Object::rva0023D3AF(frame)`, the transform snapshot (§5.8) |
| 17 | 3 | 4794-4817 | Update list 0, indices `[0, size/2)` |
| 18 | 4 | 4794-4831 | Update list 0, `[size/2, size)`; compact list 0 |
| 19 | 5 | 4794-4831 | Update lists 1 and 2, each compacted |
| 20 | 5 | 4835-4836 | **`TheAI->update()`**, which runs `ProcessPathfindQueue()` again, then `ThePlayerList->update()` |
| 21 | 6 | 4794-4831 | Update list 3; compact |
| 22 | all | 4838 | Run the melee queue: each queued object picks a melee victim |
| 23 | 5 | 4840-4853 | `Shroud`, `Taint`, `BuildAssistant`, `LargeGroupAudio`, **`processDestroyList()`**, `WeaponStore`, `LocomotorStore`, `VictoryConditions`, `DelayedExperience`, War of the Ring battle runner, `SkirmishAIManager`, `MineshaftPortalNetwork`, `TeamFactory` |
| 24 | all | 4856-4859 | Copy-protection check at frame 0x400 |
| 25 | 1 | 4861-4870 | Per object: expire timed disabled states and status timers; tick the per-object delayed-effect list (`remaining -= 1`, fire when < 0) |
| 26 | 1 | ~4906 | **`GameClient.advance = true`** |

### 4.2 The update-module scheduler

Data layout (C):
- `m_updates[4]` (`+0xC8`) holds one vector per phase list; `m_sleeping` (`+0xF8`)
  holds modules asleep forever. This replaces ZH's single sleepy-update heap.
- Each `UpdateModule` has `m_nextCallFrame` (`+0x14`), `m_indexInLogic` (`+0x18`) and
  `m_phase` (`+0x1C`, -1 when in `m_sleeping`).
- `UPDATE_SLEEP_NONE = 1`, `UPDATE_SLEEP_FOREVER = 0x3FFFFFFF`.

**Update phase of a module**, `BehaviorModule` vslot 12, read from the `game.dat`
vtables (C):

| List | Modules | Runs in |
|---|---|---|
| 0 INITIAL | AI updates: DozerAI, WorkerAI, HordeWorkerAI, SupplyTruckAI, DeployStyleAI, GiantBirdAI, …, EmotionTracker | phases 3 + 4 (split in halves) |
| 1 PHYSICS | None found, probably empty (I) | phase 5 |
| 2 NORMAL | Default for about 166 module types, **including PhysicsBehavior** (vtable 0x819F74 slot 12 → `push 2`) | phase 5 |
| 3 FINAL | FiringTracker, `Rva004DF8A9` | phase 6 |

**Registration** (`GameLogic::registerObject` 0x242AE9, `GameLogic/System/GameLogicRegisterObject.cpp:131-170`):
1. It runs at the end of the `Object` constructor.
2. The object is **appended** to the object list, so the list is oldest first (ZH
   prepends).
3. Each update module with `nextCallFrame == 0` gets `now`.
4. A module whose `nextCallFrame` is FOREVER goes to `m_sleeping`; any other goes to
   the back of `m_updates[phase]`.

**Running one list:**

```text
for i in range:                              // re-reads size every iteration
    u = list[i]; if (!u || u.nextCallFrame > frame) continue      // sleepers stay in place
    sleep = NONE
    if (!obj.disabled || obj.disabled ∩ u.getDisabledTypesToProcess()):
        sleep = obj.isDestroyed ? FOREVER : max(u.update(), 1)
    u.nextCallFrame = min(frame + sleep, FOREVER)
if phase > 3:                                // compaction, back to front
    for j = size-1..0: if list[j].nextCallFrame >= FOREVER: swap-with-back, pop, move to m_sleeping
```

Consequences (I unless marked):
- Order within a list starts as creation order. Swap-removal and wake-ups
  (push_back) then shuffle it.
- A module registered during a list's pass is appended with wake frame `now`, so it
  runs in the same pass.
- If list 0 changes size between phases 3 and 4, `size/2` moves. Modules near the
  midpoint can then run twice or be skipped in that frame.
- Modules of destroyed objects are forced to FOREVER and never update again (C).

**Waking** (`friend_awakenUpdateModule` 0x24297F, `GameLogic/System/GameLogicAwakenUpdate.cpp:236-272`), C:
- Ignored for the module currently updating.
- Ignored when the wake frame does not change.
- Ignored when `next == now && when == now+1` (same rule as ZH).
- A forever-sleeper woken to a finite frame moves from `m_sleeping` to the back of
  its phase list.

### 4.3 Object destruction

1. `GameLogic::destroyObject` (`GameLogic/System/GameLogicDestroyObject.cpp`) does the
   following:
   - calls `onDestroy` on each destroy module;
   - sets status DESTROYED;
   - clears the locomotor goal and the path;
   - queues the object on the destroy list.
2. `processDestroyList` (0x2413DF) runs **only in phase 5**, after module updates and
   `TheAI->update()`. For each queued object it:
   - removes all its update modules;
   - removes it from the pathfind map, the object list and the lookup table;
   - deletes it.
3. Objects destroyed in phase 6 or in phases 1–4 stay in the world, flagged
   destroyed, until the next phase 5 (I).
4. The client side is in §6.3.

---

## 5. Positions and movement

### 5.1 From click to path

1. **Dispatcher** `logicMessageDispatcher` (0x377613, `GameLogic/System/GameLogicDispatch.cpp`), C:
   - For messages 1000–1999 it builds an `AIGroup` from the player's selection.
   - Move is `case 1071 / 1090` (`:458-476`). Attack-move is 1072, force-move 1073,
     formation drag-move 1124.
   - With `PlanningModeEnabled` (off by default, I), orders are queued in
     `TheAiOrdersManager` instead.
2. **Group move** `AIGroup::rva00372571` (`GameLogic/AI/AIGroupRva00372571.cpp:457`), C:
   - **BFME2 block formation**: tried first for multi-unit player moves with
     `AIData.UseFormations` on (`privateGatherAndMoveInFormationToPosition`
     0x3724B8).
   - **Click-to-gather "tighten"** for small groups.
   - **Explicit formation slots** (`FormationAssistant`) when a formation type is
     given.
   - **Otherwise**, units are sorted near to far and each gets an individual
     destination through `aiMoveToPosition`.
   - Stealth-capable units add a random delay from the **logic RNG**.
3. **Formation moves (C†)**: `computeGroundPath` (0x36D3A3) runs **one synchronous**
   `Pathfinder::FindGroundPath` for a leader. `moveFormationToPos` gives each unit a
   row/column-offset copy of that path (state 54 `AIFollowPathAsTeamState`) with no
   per-unit pathfinding.
4. **State machine (C†)**: `aiMoveToPosition` → `aiDoCommand` →
   `privateMoveToPosition` (0x26B487) → `setState(1)` (AIMoveToState). After
   arriving the unit goes 1 → 78 (going idle) → 16 (guard).
   `AIInternalMoveToState::onEnter` (0x34C146) reserves the destination and requests
   a path.
5. **Path request** `requestPath` (0x26893F), C:
   - Flyers that can quick-path get a path immediately.
   - If the last path is less than 2 frames old, the request is delayed 5 frames.
   - Otherwise the unit joins a 512-entry de-duplicated ring. Requests are silently
     dropped when it is full (C†).
6. **`ProcessPathfindQueue`** (0x2F0F07, `GameLogic/AI/PathfinderProcessPathfindQueue.cpp:211`), C:

   ```text
   cumulative = 0                                  // fresh budget on EVERY call
   quota = GlobalData.MaxPathfindCellsPerFrame     // default 4000
   if frame < 5*LogicFPS: quota *= 100             // first 5 seconds
   while cumulative < quota/2 && priorityQueue: doDetour()     // blocked units (§5.6)
   while cumulative < quota   && queue:         doPathfind()   // -> computePath -> setLocomotorGoalPositionOnPath
   ```

   It is called in every phase and again in `AI::update`, so up to 7× the quota per
   logic frame.

### 5.2 Per-frame AI update

- `AIUpdateInterface::update` (0x26E267, disassembly, C) first replays any command
  deferred while waiting for a path.
- Horde members that follow their horde run `quickHordeUpdate` (0x26C4EB, not
  decompiled).
- Everyone else runs the main body `rva0026A05D` (`GameLogic/Object/Update/Rva0026A05DUpdate.cpp:239`).
  In order:
  1. state machine;
  2. movement-complete cleanup;
  3. path-queue timer;
  4. turret;
  5. dead lock;
  6. **`doLocomotor()`**.
- `AIInternalMoveToState::update` (0x347460, `GameLogic/AI/AIInternalMoveToStateUpdate.cpp:677`), C:
  - waits while a path is pending;
  - sets the locomotor goal "on path";
  - re-paths every 5 frames if the goal moved by more than 10% of the remaining
    distance;
  - succeeds within `closeEnoughDist`, and ground units must also be within 40 units
    of the path end.
  - ZH's "blocked and stuck → repath" block is absent.

### 5.3 `doLocomotor` (0x26A3A0, `GameLogic/Object/Update/AIUpdateInterface_doLocomotor.cpp:431`), C

```text
chooseGoodLocomotorFromCurrentSet(); blockedFrames = isBlocked ? blockedFrames+1 : 0; isBlocked = false
switch goalType:
 ON_PATH (1):
   no path yet: if speed > 25% max && QuickDoesPathExist(ahead): keep going straight
   info = path->pointAhead(loco.speed)                        // m_40 units ahead of the path cursor
   terrain segment (portal / climb / exit): setPosition(next) directly
   else: speed = min(desired, max)
         bump limiter: blocked -> speed = curMaxBlockedSpeed, limit *= 0.95
                       else limit = max(limit, 0.3*speed)*1.4; speed = min(speed, limit)
         loco->moveTowardsPosition(obj, info.pos, extraDist + info.remaining, blocked ? 0 : speed)
 EXPLICIT (2) / EXPLICIT_HORDE (4):
   loco->moveTowardsPosition(obj, goal, |pos-goal| + pathExtraDistance, min(desired, max))
   type 4 and step invalid (IsValidHordeMemberRegularMovement):
       revert; path = GetHordeUnitPath(); no path -> setPosition(goal)    // teleport fallback
 ANGLE (3): loco->moveTowardsAngle(obj, angle)
 NONE (0): if doFinalPosition: within 0.5 -> snap; else step 4 units/frame toward it
           requiresConstantCalling = loco->maintainCurrentPosition(obj)
```

### 5.4 Locomotor math (all per logic frame)

**Entry** `locoUpdate_moveTowardsPosition` (0x1E9083, disassembly), C:
1. Copy the object transform into the locomotor's private matrix `m_68`.
2. Cap the desired speed.
3. Run a wall-climbing pre-step (I).
4. Dispatch on `Appearance`. The four main ones:
   - `TWO_LEGS` / `FOUR_LEGS_HUGE` / `HUGE_TWO_LEGS` → legs (0x1E6B8B)
   - `FOUR_WHEELS` / `TREADS` → steering (0x1E8C1B)
   - `HORDE` → horde (0x1E756D)
   - `WINGS` / `OTHER` → turn + core
5. Other appearances:
   - `HOVER` → its own routine (0x1E7C4C);
   - `GIANT_BIRD` → nothing here; its AI drives 0x1E9A00 directly;
   - `SHIP` → 0x1E68D7, not analysed (U).
6. Z handling.
7. **One commit**: `obj->setTransformMatrix(&m_68)`.

**Inputs** (`Common/Rva001E46E1Ratios.cpp`), C:

```text
maxSpeed  = 0.2 * baseSpeed * (SpeedDamaged if damaged | ChargeSpeed if charging | 1)
            * crewFactor * SPEED attribute modifier * RiverModifier (on river cells), capped by m_28 / m_2C
accel     = min(maxSpeed / AccelerationFrames, m_30)
brake     = min(maxSpeed / BrakingFrames,      m_34)
maxTurn   = min(2π / TurnTime (or TurnTimeDamaged), m_38)                      // rad per frame
```

**Turn** (0x1E56DC), C:
1. `pivot = pos + forward * TurnPivotOffset`.
2. `amount = clamp(normalize(atan2(goal - pivot) - heading), ±maxTurn)`, halved for
   "turn slowly" AI.
3. Yaw the matrix by `amount`.
4. Set turn animation conditions when `|amount| > TurnThreshold`.

**Legs** (0x1E6B8B), C:
1. Turn toward the goal. Backing up is allowed for `CanMoveBackwards` units.
2. Turn harder when accelerating from slow, and not at all above half speed.
3. Only "huge" types slow down in turns:
   `goal = (1 - min(|rel|*4/π, 0.9)) * desired`.

**Wheels and treads** (0x1E8C1B, `GameLogic/Object/LocomotorRva001E8C1BSteering.cpp:123`), C:
- Steer toward a point 1000 units out.
- Speed in turns: `goal = (1 - min(|rel|/(π/4), 1)) * desired`.

**Core speed and position step** (0x1E6007, constants verified in `game.dat`), C:

```text
d        = speed - MinSpeed
slowDist = d > 0 ? (d/brake + 1) * (0.5*d + MinSpeed) * 1.05 : 0      // braking distance
if onPathDist > 3*slowDist: release braking latch
goal = desired; if speed - brake > desired: goal = max(speed - brake, brake)
if slowDist > onPathDist: latch braking; goal = max(speed - brake, brake)    // brake, then creep
if goal > speed && !braking: speed += accel
if speed > goal:             speed = max(speed - brake, goal)
if on a path:
    p = path->pointAhead(speed); path->DoAdvanceAlongPath(p)          // position = point ON the polyline
    obj.nextPosition = path->pointAhead(speed); obj.hasNext = 1       // lookahead for rendering (§6.2)
else:
    p = pos + normalize(goal - pos) * min(max(speed, 2.0), |goal - pos|)
p.z = obj.z; m_68.translation = p
animation conditions: accelerating / decelerating / walk vs run start
```

**Z** (0x1E7ECA, I from a 0.975-score draft):
- Ground units snap to the layer height.
- In water deeper than `0.25 × AIData+0x98`, units use the bottom height and set
  wading or swimming.
- Other modes damp toward the preferred height.
- Ground units get no terrain-normal tilt in logic.

### 5.5 PhysicsBehavior: precomputed trajectories, not a physics integrator

`PhysicsBehavior::update` (0x39114B, `GameLogic/Object/Update/PhysicsBehaviorTrajectory.cpp:207`), C:

```text
tick stun timer (stand-up animation at the end)
if all trajectory points used: landing (FX, optional bounce at 35% height / 85% speed, rest, kill or weapon)
cur = points[index]
OrientToFlightPath ? setTransformMatrix(build(cur, cur - prev)) : setPosition(cur)
setNextPosition(points[index+1] or extrapolated); AdjustLayer; ++index
return points left || stunned ? SLEEP_NONE : SLEEP_FOREVER
```

- The trajectory is a cubic Bezier over the terrain, with
  `max(3, ceil(length/speed)+1)` points, one per frame (C†).
- It has no velocity, mass, friction or gravity integration.
- It is used for flings: thrown riders, slow-death throws, random-force OCLs, horde
  and transport ejection.
- It runs in phase 5 (list 2).

### 5.6 Transform setters, spatial index and collisions

- **`Thing::setTransformMatrix`** (0x30A2B7) stores the matrix and caches position
  and Z angle. **`setPosition`** (0x30AA80) and **`setOrientation`** (0x30AB9D) work
  the same way. All three call `reactToTransformChange` (C†).
- **`Object::reactToTransformChange`** (0x292D49), C/C†:
  - destroys the object on NaN;
  - pushes the transform to the drawable **synchronously** and stamps
    `drawable.lastMovedFrame`;
  - if the object moved or turned by more than 0.01, **only marks dirty lists**:
    shroud, collision, partition;
  - updates trigger areas, the fire-logic registration and the off-map flag.
- **Partition** (C†): a loose quadtree (depth 7), not ZH's cell grid. Its `update()`
  drains the dirty list in **phase 2** and relinks only nodes whose cell changed.
- **Collision** (C†), also in phase 2:
  - broad phase: sweep-and-prune on 3 axes;
  - persistent contacts are kept in a hash;
  - every frame, each contact gets a narrow test (`GeometryInfo::CollidesWith`), then
    `onCollide` on both objects;
  - crush is handled by collide modules (`AODCrushCollide`, `SquishCollide`,
    `CrushDie`).
- **Bump response** (`GameLogic/Object/Update/AIUpdateInterfaceCollisionResponse.cpp:235`), C†:
  - **Priority:** same formation → idle vs moving → heading and speed → priority →
    object ID.
  - **Idle units** that get bumped step aside.
  - **The loser** is marked blocked. Periodically it goes on the priority pathfind
    ring for a detour.
  - **After 2 s blocked**, it ignores collisions for 2 s.
  - Ground units are never physically pushed apart.

### 5.7 Hordes

1. **The horde object** runs `HordeAIUpdate::update` (0x49AB10, `.../HordeAIUpdateUpdate.cpp:188`)
   in list 0:
   - formation refresh;
   - **`hordeContain->update()`**, which sets the members' goals;
   - its own path move with the `HORDE` locomotor.
2. **Member goal** = horde position + rotated formation offset of its slot (C†,
   0x472620).
3. **`HordeContain::_updatePosition`** (0x471464), C†:
   - Each member's `pathExtraDistance` is set to the horde's remaining path, so
     members brake together with the horde.
   - Members very close to their slot stop, or snap if the horde is idle.
   - Otherwise members get an explicit goal (type 4 or 2).
   - An overlap check spreads out members that target the same spot.
4. **Members do not pathfind.** They walk straight with their own locomotor, and each
   step is validated. On failure a member reverts, gets a short path, or teleports to
   its slot.
5. **Horde locomotor** (0x1E756D):
   - If the turn exceeds `MaxTurnWithoutReform`, the horde **snaps its yaw** and
     reforms (C).
   - Otherwise it turns normally (C).
   - The AOD horde variant places members kinematically, with step ≤ 1.25·maxSpeed
     (C†).
6. **Update order between a horde and its members** within list 0 is vector order.
   If members run first they chase last frame's slots (U).

### 5.8 Snapshot fields that logic keeps for rendering

| Field | Written by | Meaning |
|---|---|---|
| `Object+0x158` (Matrix3D) | `rva0023D3AF`, phase 2 | Transform at phase 2 of this frame = end of the previous frame |
| `Object+0x18C` (Vector3) | `rva0023D3AF`, phase 2 | Position one snapshot earlier |
| `Object+0x188` | `rva0023D3AF` | Frame of the snapshot |
| `Object+0x198` / flag `+0x1A6` | `setNextPosition`: locomotor, physics, horde, projectiles | Planned next position (lookahead). Cleared by every snapshot, so it must be set again each frame |

Getters: `0x2722AA` (snapshot or live matrix), `0x2722BD` (previous or live
position), `0x1E434A` (planned or live position).

Consumers:
- the client interpolation (§6.2);
- a logic velocity estimate `Object::rva0028E8FD`, used by the crush warning;
- the locomotor.

A teleport snapshots twice and forces a drawable resync, so the render snaps instead
of gliding (C).

---

## 6. The client and render frame

### 6.1 `GameClient::update()` (0x23BEE8; `GameClient/GameClientDrawableTOC.cpp:1739-1890`), C

| # | Step |
|---|---|
| 1 | Camera scroll from mouse, keys and edges (`LookAtTranslator`; called directly, no `MSG_FRAME_TICK`); a second camera translator (campaign map, I) |
| 2 | War of the Ring map client update, if active |
| 3 | Weather and ambient visuals: snow, clouds, cloud breaks, fire; `Anim2DCollection` |
| 4 | **Keyboard** update → stream messages; EVA; **Mouse** update → stream messages; Apt message boxes |
| 5 | Intro path: draw and return |
| 6 | Window manager, video player, release queued COM objects |
| 7 | `freeze = view frozen \|\| script frozen \|\| paused \|\| (lastFrame == clientFrame)`; `boundary = (period == 1)` |
| 8 | If not frozen: on the boundary, update orphaned ghost objects. For **every drawable**: on the boundary, refresh "obscured by shroud"; then **`updateDrawable()`** (§6.3) |
| 9 | Animation sound modules; buff effects (track interpolated positions; logic update on the boundary) |
| 10 | `TerrainVisual->update()`, `Display->update()` (movie step, time-of-day light fade) |
| 11 | **`TheDisplay->draw()`**: vslot 12 = `W3DDisplay::draw` 0x4BEF2 (§6.6). The reconstruction calls this slot `drawViews`, which is wrong |
| 12 | Display strings; Shell; apply a pending display-mode change |
| 13 | **`InGameUI::update()`**: control bar, floating text, then keyboard rotate, zoom and scroll on the tactical view. That runs after the draw, so it shows next frame |
| 14 | **Delete drawables queued for destruction this frame** |

### 6.2 Interpolation: how a drawable gets its position, C

**Push.** Every logic transform change (`reactToTransformChange`) pushes the raw
transform into the drawable's own matrix, and stamps `drawable+0x3A4 = logicFrame`
("last moved").

**Rebuild endpoints** `Drawable::rva002747F9(force)` (0x2747F9, `GameClient/DrawableInterpolationRebuild.cpp:99-148`).
With `force = 0`, it runs for every drawable in phase 1 (§4.1 step 14):

```text
M0 = object.snapshotMatrix (+0x158)          // end of frame N-1
M1 = object.transform                        // end of frame N
V0 = object.prevPos (+0x18C)                 // end of frame N-2
V1 = translation(M0)
V2 = object.pos
V3 = object.hasNext ? object.nextPos : object.pos     // lookahead into frame N+1
```

With `force = 1` (teleport, explicit set), all endpoints are set to the current
transform (snap), and pending model-condition flags are applied immediately.

**Evaluate** `Drawable::getTransformMatrix` (0x27628E, `GameEngineDevice/.../Drawable/BFMERopeDrawableGetPosition.cpp:183-224`):

```text
if no object:                          return own matrix         // client-only drawables don't interpolate
if computed this client frame:         return cache
if lastMovedFrame < logicFrame - 2:    cache = object.transform; return cache   // idle 2+ frames: exact
f = TheGameEngine.ratio (+0x3C)        // period / 6, clamped 0..1
cache = Matrix3D::Lerp(M0, M1, f)      // 0x713AB0: quaternion slerp, scale preserved
cache.translation = D3DXVec3CatmullRom(V0, V1, V2, V3, f)
```

- `Drawable::getPosition` (0x2763E6) gives the same Catmull-Rom position, without
  the per-frame cache. It is used by camera follow, lasers and buff FX.
- Frustum culling uses a cheaper linear lerp from V1 to V2 (0x276470).
- Each segment starts where the previous one ended, so motion is continuous, with
  200 ms display lag (I).

### 6.3 Drawable lifetime and `updateDrawable`

**Create** (`GameLogic/System/GameLogicRva0023FABE.cpp:63-75`), C:
1. `Object::initObject` calls **`GetGameLogicRandomValue(1,999)`**. Drawable
   creation consumes the synchronised logic RNG.
2. `ThingFactory::newDrawable` creates the drawable. It spins `Sleep(0)` while assets
   load (I).
3. The object and drawable are bound, and Lua object event 0xC fires.

**Destroy** (C):
1. `Object::~Object` → `sendObjectDestroyed` → `GameClient::destroyDrawable`
   (0x239F40).
2. That unlinks the drawable and queues it.
3. It is deleted at the end of `GameClient::update` (§6.1 step 14).

A drawable also expires itself when `expires <= logicFrame`.

**`Drawable::updateDrawable`** (0x27566B, `Drawable_rva00275598.cpp:330-457`), once
per advanced client frame, C:
1. **Model condition, deferred to the boundary:**
   `Drawable::replaceModelConditionState` (0x2791E7) only stores flags and sets a
   dirty bit. On the boundary frame the flags go to the draw modules, which is what
   picks the animation.
2. **Client update modules:** all of them if the drawable was in the frustum last
   cull; otherwise only the modules that must run off-screen.
3. **Opacity fades, decal fades and colour flash:** stepped per *client* frame.
4. **Expiry:** checked in *logic* frames.
5. **Tint envelopes and stealth pulse:** use the client frame rate.
6. **Decal position:** follows the object.

### 6.4 Animation timing, C

`W3DDisplay::draw` advances one animation clock:

```text
delta = GameClient.frame - lastFrame;  if !GameClient.advance: delta = 0
syncTime += TheW3DFrameLengthInMsec (= 1000/30 = 33) * delta;  WW3D::Sync(syncTime)
```

- WW3D animations compute their frame lazily at render time:
  `frame += FrameRate * multiplier * direction * (SyncTime - LastSyncTime) * 0.001`
  (`AnimatableFrameProgress.cpp:40-56`).
- Modes: loop, once, ping-pong, clamp-to-target, backward loop, backward once
  (`docs/reconstruction/animation-frame-modes.md`).
- `W3DScriptedModelDraw::processAnimations` (0x0BFDFE) blends 3 animation slots.
  When an animation completes, `adjustAnimation` picks the next one at random from
  the current model condition.
- It runs only from `doDrawModule`, so only for drawables inside the frustum (I).
- One logic frame is 6 × 33 ms = 200 ms of animation, so animation stays locked to
  game speed, not the wall clock.
- Animation stops when the client frame does not advance (pause) or the window is
  minimised.

### 6.5 Camera: `W3DView::update` (0x8C3AC, `W3DViewRva0008C3AC.cpp:554-731`), C

Called at the start of the render pass, in this order:
1. Terrain centre update (uses the previous camera).
2. **Camera lock and follow**: follows the drawable's **interpolated** position, with
   a ramped follow factor.
3. **Scripted camera moves** (`updateCameraMovements`): stepped by a fixed 33 ms per
   call, not wall time.
4. Shake decays ×0.75 per update.
5. Terrain-height following and auto-zoom.
6. Zone camera offset blends 8% per update.
7. `setCameraTransform()` if anything changed.
8. **Frustum cull + `Drawable::draw`** for each visible drawable:
   - interpolated matrix × instance matrix × visual "physics" pitch, roll and bounce
     (computed per client frame);
   - `W3DModelDraw::doDrawModule`: process animations, set the render-object
     transform, turret, recoil.

### 6.6 `W3DDisplay::draw` (0x4BEF2, disassembly) and the render order

```text
if window minimised: return
update FPS average; LOD
advance the animation clock (6.4)
replay fast-forward: render 1 frame in 30 (otherwise only camera moves + clock)
time multiplier > 1: render every Nth call
renderPass(now)  (0x4A462, W3DDisplayRva0004A462.cpp:352-491)
```

The only limiter inside the display is a 29 ms busy-wait. It only throttles when the
time multiplier is above 1; normal pacing is the outer loop's (§3.2).

**Render pass**, in this order. A lost D3D device skips all of it, including the
view, particle and animation updates.
1. `updateViews()`: camera update, cull, drawable submit (§6.5); horde model
   batching.
2. **`TheParticleSystemManager->update()`**: particles step **once per client
   frame**, inside the render.
3. Shroud texture update, taint render, shadow map, water reflection.
4. `WW3D::Begin_Render`, then `drawViews()`.
5. **Scene flush order** (`RTS3DSceneFlush.cpp:174-273`):
   1. occluded objects into stencil
   2. water
   3. decal shadows
   4. terrain
   5. volume shadows
   6. opaque meshes
   7. terrain extra passes
   8. translucent pass 1
   9. particles
   10. translucent pass 2
   11. smudges
6. Per view, after the scene: `drawIconUI` health bars and icons are queued into 10
   icon layers, then flushed in a batch.
7. UI: `InGameUI::draw` → Apt windows → `InGameUI::draw`; floating text; mouse;
   movie overlay; letterbox fade.
8. **`End_Render(true)`** presents the frame. The vsync / present interval used by
   BFME2 is U.

---

## 7. Input, commands and multiplayer

### 7.1 Input → commands, C

1. **Mouse:** `WndProc` buffers Win32 mouse events during the message pump at the
   end of the previous iteration. **Keyboard:** DirectInput, polled.
2. `GameClient::update` turns both into raw messages on `TheMessageStream`.
3. `propagateMessages` (0x310FF5) runs the translators in priority order. Survivors
   are appended to `TheCommandList`. Translators, in order:

   | Priority | Translator |
   |---|---|
   | 4, 5, 10 | window-related (10 is ZH's WindowTranslator, I) |
   | 20 | MetaEvent |
   | 25 | HotKey |
   | 27 | `Rva0042CBB6` |
   | 30 | PlaceEvent |
   | 35 | Formation |
   | 40 | GUICommand (I) |
   | 50 | Selection |
   | 60 | LookAt |
   | 70 | CommandXlat |
   | 100 | HintSpy |
   | last | GameClientMessageDispatcher |

4. **Single player:** commands wait in `TheCommandList` until the next logic phase 1,
   which dispatches them all. Input latency is therefore up to about 6 client frames
   (≤ 200 ms) plus the 200 ms render lag.
5. **Multiplayer:** `Network::update` first removes the game commands (types
   1000–1999) and sends them to the host. The commands scheduled for this frame come
   back in through the relay, and logic dispatches them.

### 7.2 Lockstep (multiplayer only), C

`TheNetwork` is created only by the LAN and GameSpy start paths. Skirmish and
campaign skip every network branch.

- **Command scheduling:**
  - Clients send commands to the host unscheduled (`executionFrame = -1`).
  - The host stamps each one with **`hostFrame + 1`**, stores it in that frame's
    data, and relays it (`relayCommand` 0x4CF578, unmatched, read from disassembly).
  - Commands older than `frame - NetworkRunAheadSlack` are dropped.
- **Frame info:** the host totals the command count for `frame + 1`, broadcasts
  FRAMEINFO, and raises its ceiling (`sendFrameInfo` 0x4D0AA9).
- **Admission** (`getFrameAdvanceCount` 0x25E014):
  - **Clients:** advance while `ceiling - frame > 0` and every command for
    `frame + 1` has arrived (the counts match).
  - **Host:** paced by a `QueryPerformanceCounter` accumulator at exactly 5 frames per
    second. It stalls when a peer falls more than `NetworkRunAheadSlack` frames
    behind: 3 until frame 5, then the INI value (10 in the shipped data, I).
- **Run-ahead:** the host itself has zero run-ahead, and clients trail it by their
  latency. There is no ZH-style per-peer run-ahead negotiation (I).

### 7.3 Catch-up and speed control (multiplayer), C

- **`LOGIC_SCALE`** (VA 0xDBA2F8): a client near the host's ceiling stretches its
  frame period. The scale is 0.6–1.0, smoothed, and never above 1.
- **`shouldSkipClientFrame`** (0x225BA6): a client that has fallen behind skips the
  whole client update (no draw), or stretches the logic frame by one client frame.
- **After a skip**, the limiter stays off for 6 client frames.
- **No catch-up loop:** there is never more than one logic phase per iteration.

---

## 8. Determinism, C

- **RNG:** three seed sets (audio, client, logic) of 6 words each, using ZH's
  add-with-carry generator (`Common/RandomValue_randomValue.cpp`, 0x233EC3).
  - **Integer** (`GetGameLogicRandomValue(lo,hi)`): `rv % (hi-lo+1) + lo`.
  - **Real** (`GetGameLogicRandomValueReal`): `rv * (1/4294967295.0f) * (hi-lo) + lo`.
  - **Seed:** GlobalData `-randomSeed` overrides it.
  - **Logic RNG consumers to keep in sync:** drawable creation (§6.3), stealth move
    delays (§5.1), aerial jitter (§5.4).
- **CRC:** in multiplayer, every `GameInfo+0x0C` frames in phase 1, `getCRC`
  (0x23CB2C) writes the following through a `BFMECRCWriter`:
  1. every object (world-list order);
  2. the logic seed;
  3. partition, collision, shroud, taint and skirmish-AI managers;
  4. players;
  5. AI;
  6. War of the Ring logic.

  The seed CRC is `crc = (crc<<1) + byte + (crc>>31)`. The result is sent as message
  0x44A through the normal command path.
- **Floating point:** `setFPMode` sets x87 to 24-bit precision with round-to-nearest.
  It runs on every phase and in `getCRC`. Many logic units are compiled with
  `/arch:SSE`. A reimplementation should use IEEE single precision with
  round-to-nearest throughout (I).

---

## 9. Differences from Zero Hour

| Topic | Zero Hour | BFME2 |
|---|---|---|
| Logic rate | 30 Hz, one `update()` per frame | 5 Hz, 6 `update(phase)` calls spread over 6 client frames |
| Rendering | Logic and client 1:1, drawables snap | 30 Hz client, interpolated (slerp + Catmull-Rom with lookahead), 200 ms lag |
| Frame counter | Incremented at the end | Incremented at the start of phase 1 |
| Module scheduler | One sleepy heap by `(frame<<2)\|phase` | 4 per-phase vectors + a sleeping vector; destroyed objects' modules forced to sleep |
| Physics phase | PHASE_PHYSICS, rigid-body-ish integrator | NORMAL, Bezier trajectory player only |
| Object list | Prepend (newest first) | Append (oldest first) |
| Partition | Cell grid, immediate | Loose quadtree, deferred to phase 2 |
| Pathfinding | Once per frame | Every phase + in `AI::update` |
| Model-condition changes | Applied immediately | Deferred to the logic boundary |
| Lockstep | Peers schedule at `frame + runAhead` | Host stamps `frame + 1`, QPC-paced host, slack-based stall |
| Client tick | `MSG_FRAME_TICK` drives the camera | Direct scroll call; Apt UI; batched icon layers; deferred drawable deletion |
| New subsystems | — | Lua, VictorySystem, FireLogic, DOT, invisibility, taint, skirmish-AI manager, mineshaft network, delayed XP, War of the Ring hooks, melee queue, per-object delayed effects |

---

## 10. What a faithful rebuild needs

1. **The tick.**
   - Run a fixed 30 Hz client tick. Each tick does the client half first (input →
     drawable updates → camera → render), then exactly one logic phase.
   - The phase cycles 1..6. Phase 1 is gated by pause and network and is retried
     until admitted.
   - Pace with a 33 ms limiter and no catch-up.
2. **The logic frame.** Implement the six phases in the order of §4.1, with the
   per-phase update lists of §4.2 (list 0 split across phases 3 and 4) and the
   destroy list only in phase 5.
3. **Units.**
   - Convert all INI motion and duration values to per-5-Hz-frame units when they
     are loaded.
   - Run movement in per-frame units. Never multiply by delta time in logic.
4. **Movement.** Kinematic locomotor (§5.4):
   - speed ± accel/brake with the braking-distance rule;
   - position = `pointAhead(speed)` on the path;
   - separate clamped turning;
   - one transform commit per frame;
   - `setNextPosition` every frame.
5. **Deferred spatial work.** Mark partition, collision and shroud dirty on
   transform change, and process them in phase 2.
6. **Rendering.**
   - Snapshot the transform in phase 2 and rebuild drawable endpoints in phase 1.
   - Each client frame, evaluate slerp + Catmull-Rom (V0..V3) at `period/6`.
   - Return the exact transform for objects idle 2+ logic frames.
   - Snap on teleport.
7. **Model conditions and animation.** Defer model-condition changes to the logic
   boundary. Advance animations by 33 ms per advanced client frame.
8. **Determinism.** Keep the logic RNG call order, including drawable creation.
   Use single-precision floats with round-to-nearest.

---

## 11. Open questions

**Movement and logic**
1. Locomotor bodies 0x1E9083, 0x1E6007, 0x1E6B8B, 0x1E756D, 0x1E7ECA, 0x1E84E5, the
   AI update wrapper 0x26E267 and `quickHordeUpdate` 0x26C4EB are not byte-matched.
   Their formulas above come from disassembly. `SHIP` (0x1E68D7) is not analysed.
2. `Path::pointAhead` (`rva003642DF`) is not decompiled: exact cursor semantics are
   U.
3. Meaning of `GameLogic+0x9D`, `+0x125` (War of the Ring map / single-player hold),
   `+0x38` (network timestamp), and object status 0x25/0x26/0x4A.
4. Who fills the melee queue `GameLogic::m_164`.
5. Whether update list 1 is truly empty.
6. What happens in retail when list 0 changes size between phases 3 and 4.
7. Update order between a horde and its members.
8. `BFMECRCWriter`'s per-value hash.

**Network**
9. `relayCommand` (0x4CF578) is unmatched; the reading above comes from disassembly.
10. The shipped INI values (FPS limit 30, run-ahead slack 10): confirm that no patch
    `.big` overrides them.

**Rendering**
11. The units of drawable fade durations and of particle lifetimes (client frames
    vs logic frames).
12. Where the shroud is composited in the scene. The D3D present interval / vsync.
13. Identity of the singleton updated first every iteration (VA 0xE099F8).
