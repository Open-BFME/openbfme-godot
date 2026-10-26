# CASTER-1 — caster checklist

OpenBFME spectator/replay presentation additions, not claims about retail UI parity.
Replays are the existing OpenBFME command recordings; retail `.BfME2Replay` decoding
is a separate lane.

## Milestone 1: vision and POV

- Open a replay through Load Replay, or join an authorized online spectator session.
- `0`: all vision. `1`–`8`: the corresponding occupied lobby slot, including AI.
- `T` / **Team vision**: union of the selected player's and their allies' vision.
- A player POV uses that player's fog, stealth look, radar and recorded selection.
  These are read-only presentation choices: they do not change the local player,
  issue selection commands, consume RNG or alter simulation hashes.
- Current recording/relay formats have no camera channel. The visible label says
  **Free camera — no recorded camera**. This is not camera-follow emulation.
- Active online players cannot enable the caster view. Public spectators retain
  the relay's authorization and delayed-data boundary.
- Keep the final replay frame on screen rather than switching to the recorder's
  defeat/score screen.

Validation: `CASTER-1 vision:*` in `test_online3_spectate.cpp` compares every fog
cell against the selected player, team union against its members, and hashes
before/after view changes. `godot/tests/caster_vision_test.gd` creates a real
recording, drives the caster hotkeys, and checks all 50 playback hashes.

## Milestone 2: recorded replay transport

- **Space** / Play-Pause, 0.25× / 0.5× / 1× / 2× / 4× / 8×, and **.** / Step.
  Step completes exactly one logic frame, including a partially paced frame.
- Scrub the full recorded duration; choose an entry in **Jump to event** for an
  exact marker jump. Controls remain available after the replay ends.
- Markers are indexed while simulation frames run, not fabricated from commands.
  **Indexed through** and the blue underline show coverage; unvisited future
  frames have no known markers until played or sought through.
- Damage groups use five-second windows, 300-world-unit cells and victim player.
  Hero/building deaths use the death callback, powers use the actual trigger,
  and capture markers are structure ownership changes (including transfers).
  Damage weights are the engine's actual-damage field, which includes overkill;
  they are not unit value, killed hit points or a retail engagement classifier.
- Backward seek destroys the old HUD before recreating the world. Previously
  indexed markers, POV, allied vision, free-camera position and playback speed
  survive. The progress overlay explains the restart fallback.
- Every seek frame requires a recorded hash. Incomplete logs, gaps and mismatches
  fail by name; a mismatch pauses playback. No simulation snapshots are claimed.
- At this milestone these controls covered finished **recordings**, including recordings of
  finished games. Live transport/rewind is still part of the received-edge and
  delay milestone below, not covered by this milestone's proof.

## Milestones outstanding after milestone 2 (historical)

- Live transport, received-edge rewind, default delay and go-live.
- Final end-to-end checklist, release note and full gate.

Both were subsequently delivered in the live-transport and final-gate sections
below; this list records the milestone-2 checkpoint, not current missing work.

## Rewind implementation decision

`LiveGame` deletes its copy constructor/assignment. Its `LogicSnapshot` is a
render-only picture, not a restorable simulation. Native saves now have a
separate `LiveGame::loadSnapshot` / `CommandLogLoad` path (SAVE-4e), but caster
rewind does not use it. Rewind restarts playback from the recorded initial state,
fast-forwards with a visible progress bar and compares recorded hashes. It must not
label render snapshots as deterministic keyframes. Measure a seek to minute 20
(6000 logic frames at 5 Hz), including restart/load time, before acceptance.

Generated replays, logs, captures and game assets stay outside commits.

## Verification ledger

The following numbers record the original milestone runs, not the current
suite's test counts or a fresh all-platform qualification.

Milestone 1: Release/Ninja engine + extension build passed. The focused vision
test passed 477 assertions, including a nonempty recorded selection. The earlier
vision + existing online-observer regression run passed 909 assertions. The
headless Godot fixture passed and matched all 50 recorded CRCs while switching
views. No runtime asset skips in these runs; no pin expectations changed.

Rendered inspection caught a paused-view minimap cache bug that the headless
test did not: its old cache key only used the world's shroud-edge count. The
renderer now also invalidates on player/allied-view changes.

Milestone 2: focused engine seek/timeline checks passed 2 cases / 541 assertions;
the vision regression passed 477 assertions. The seek tests cover worker mode,
partially completed six-tick frames, missing hashes, mismatches and continuation
after a seek. The Godot fixture also checks active-player refusal, speed bounds,
the finished edge and an exact single step after reconstruction.

Measured on this Linux VM with the Release build and headless Godot 4.7:
seek from a freshly loaded initial world to **frame 6001 = minute 20** took
**26,365 ms including reload and fast-forward** on the final milestone build,
with other renderer processes stopped. The earlier build/run, with background
renderers, measured 50,641 ms; neither run is a performance guarantee.
All **6000 recorded frame hashes**
were compared, with **zero mismatches**; the final hash matched uninterrupted
recording. This is headless timing, not a rendered FPS claim. The fixture's
`--measure-20` option reproduces the measurement, creating private runtime files.

The rendered `--caster-check=<capture-directory>` walk exercises keyboard pause
and stepping, the speed menu, timeline scrub, progress, finished playback and a
backward marker jump. It checks CRCs, exact frames, POV/team/speed/camera
restoration and both transport safe bounds; captures are runtime output, not
repository assets. It passed at 1920×1080 and 2560×1440 under Xvfb.

## Milestone 3 — director camera

`D` or **Director** enables the presentation-only action camera. **Tune director**
sets priorities (zero disables a category), minimum dwell, idle-return timeout,
ease duration, far-cut distance and camera height. Native camera limits still
apply; angle and pitch are preserved. Pan, rotation, zoom and minimap navigation
take over, then the director returns after the configured idle time. Rewind keeps
the preferences and restored camera, starting a fresh manual grace interval.

Defaults are six seconds dwell, twelve seconds idle return, a two-second
smoothstep transition, cuts only beyond 2500 world units, height 450 and a
15-simulation-second event lookback. These are **new caster policy defaults**,
not recovered retail camera constants. Hero deaths and base ownership changes
rank first, then base damage/loss, power uses, and fights/building losses. Within
a priority, damage events rank by actual damage, then recency, then object ID.
There is no invented spell-magnitude or retail engagement classifier. The feed
is omniscient observer information; all vision is recommended for directing.

The policy never issues commands or changes simulation identity. Wall-clock
easing/dwell are presentation timing, not a claim of identical render-frame
poses at different refresh rates. Both replay and authorized live observers can
use the event feed; live delay/rewind was still outstanding at this milestone
(delivered in milestone 6 below).

`caster_director_test.gd` checks deterministic ranking, tuning, expiry, dwell,
smoothstep versus far cuts, takeover and idle return. Its `--record-fight`
option produces a real two-human-slot replay: both players recruit Eomer through
their fortress hero roster, issue approach/attack commands and exchange damage.
No debug object spawning, money grants or injected damage are used. On this
fixture the director chose the recorded encounter; all **404** completed-frame
hashes matched the recording with the director active. Replay and metadata stay
in the user's runtime data directory, never Git.

`--replay=<caster-director.replay> --caster-director-check=<capture-directory>`
runs the rendered walk: fight selection, intermediate and final camera poses,
tuning, native manual takeover, idle return, paused-state hash invariance,
rewind preference restoration and safe layout. It passed at both target
resolutions. The capture walk uses the metadata generated by the fixture above.

Full-suite validation and the remaining milestones were still outstanding at
this checkpoint; see the later full-gate ledger.

## Milestone 4 — broadcast overlays and clean mode

Toggle **F1** player bars, **F2** production/upgrades, **F3** army composition,
**F4** graphs, **F5** heroes and **F6** the event ticker. **F9** hides the native
HUD, Palantir and caster controls, retaining only the chosen overlays. Camera
and caster hotkeys remain available. Escape restores the ordinary HUD before
opening quit; closing quit preserves an already-paused replay. Rewind preserves
overlay choices, graph choice and visited graph history, but never displays a
future counter or event at the rewound frame.

These are new, read-only caster statistics, not a recovered retail observer
score formula. `ObserverMetrics` runs on the simulation owner at completed-frame
publication, with an initial sample when observation begins. It stores owned
presentation data, not object pointers, and is not included in simulation hashes.
`GameWorld.caster_metrics` is restricted to replay/authorized online observation.

- **Money / command points / power points:** native money, command-point usage
  and limit, and unspent science purchase points.
- **Army value:** sum of retail base `BuildCost` for living mobile/combat units
  (AI movers, attackers, hordes and heroes). Excludes structures, workers/dozers,
  projectiles and battalion-member children. Composition counts battalions and
  independent units, including support units. This is not the amount actually
  paid: discounts, upgrades and resale value are not added. Summons retain their
  template's base cost, including an explicitly defined zero. Missing/untyped
  costs produce **unavailable**, not a fabricated zero total.
- **Income/min:** actual logged resource payments in the preceding 60 simulation
  seconds; before minute one, payments since the start, not an extrapolated rate.
  It is not gross money earned from every source. The native income log is
  bounded at 8192 payments. If entries in the current window may have been lost,
  income is **incomplete** for that window, including on the graph.
- **Units lost:** the native `ScoreKeeper::unitsLost()` counter, with its existing
  retail accounting semantics; not recomputed from the composition list.
- **Queues / heroes:** native production entries, including upgrade research and
  hero recruitment/revival; live hero health/rank and the player's dead/queued
  hero records. Unrecruited roster options are not represented as living heroes.
  Where no localized label is available, an actual template name is shown.
- **Ticker:** actual recorded deaths, powers and ownership changes; an ownership
  change is not silently labelled a verified capture.

Graphs store an initial point and one point every five logic frames (one
simulation second). Fast-forward collects the same samples without depending
on render callbacks. Unvisited/incomplete spans remain gaps; switching vision
does not filter these omniscient observer statistics. Casters wanting a strictly
player-information-only broadcast should hide the extra panels.

The focused native `CASTER-4*` test covers a retail battalion, hero and support
unit, source counters, hash invariance, completed-frame sample cadence and a
truncated income window aging out. Generate the extended private fixture with
`caster_director_test.gd -- --record-fight --fixture-name=caster-overlay --until-death`;
it recruits heroes, researches fortress banners and records the encounter to a
death. `caster_overlay_test.gd` checks native-source agreement, all recorded CRCs,
and exact metrics/history equality after threaded restart/seek. The rendered
walk is `--replay=<caster-overlay.replay> --caster-overlay-check=<directory>`.
Its captures, like the replay and fixture metadata, must remain outside Git.

Milestone 4 verification: Release/Ninja engine and extension build passed;
`CASTER-4*` passed 343 assertions and `CASTER-2*` passed 541 assertions. The income
window is `(frame - 300, frame]`: the frame advances before its payments run.
The current-frame payment and exact window-expiry assertions both failed with
the old endpoints, then passed with the fix. The extended encounter matched all
675 recorded hashes; threaded restart reproduced the metrics/history exactly.
Regenerating for the changed engine identity produced a byte-identical game and
record stream after the profile header. No pin expectations were edited.

Rendered walks passed at 1920×1080 and 2560×1440, including panel bounds, graph
selection, clean camera movement, hotkeys, rewind restoration, the actual quit
movie and retaining an already-paused replay on return. Rendered checks exposed
a Space delivery failure with root-level forwarding; caster input now runs ahead
of the native HUD. Overlays suspend while the quit movie is open. Headless vision and
overlay regressions and the 23-file commit gate passed. Full-suite validation
remains outstanding.

## Milestone 5 — pinned picture-in-picture

`F7` (or **PiP**) toggles a second view. Its first opening pins the current
location; move the main camera or enable the director to watch another location.
`Shift+F7` / **Pin here** repins it. **Swap** or a double-click on the picture
exchanges the two targets; the wheel zooms only the inset. Clean mode retains the
chosen picture without its buttons. Rewind preserves its target, zoom and toggle;
quit/seek suspend rendering and closing the inset disables its viewport.

This is a second camera on the **same presented world**, not a second simulation.
Both views share all/player/team vision, selections and camera angle; the label
names that vision. The parallel camera directions keep the W3D batches' shared
depth ordering valid. There is no independent PiP POV or rotation. A second
world-space frustum keeps animations current in either view while still culling
poses outside both; it does not disable culling for the whole map.

The right-hand safe column reserves space for PiP below hero status, without
covering other enabled overlays. The existing multi-batch transparency limitation
S-029 still applies; PiP does not claim to fix it.

Validation commands (Linux/macOS POSIX shell, from the repository root;
Godot 4.7 on PATH, the built GDExtension in `godot/bin/`, and `ROTWK_INSTALL`
and `BFME2_INSTALL` set to local installs). The rendered commands also require
a working desktop/display; no fixed X display number is assumed. Prepare the
`caster-overlay` recording with the fixture command below before the rendered
walks, and set `CASTER_REPLAY` to the `replay` path in its `DIRECTOR REPLAY`
output. Captures stay under ignored
`workspace/`; retail files remain local.

```sh
godot --headless --path godot --script res://tests/perf3_cull_test.gd
# Old single-camera behavior: the second-only pose assertion must fail.
godot --headless --path godot --script res://tests/perf3_cull_test.gd -- --caster-no-extra-frustum
godot --headless --path godot --script res://tests/caster_director_test.gd -- --record-fight --fixture-name=caster-overlay --until-death
godot --path godot --resolution 1920x1080 -- --replay="$CASTER_REPLAY" --caster-pip-check="$PWD/workspace/rebuild/proof/pip-1080"
godot --path godot --resolution 2560x1440 -- --replay="$CASTER_REPLAY" --caster-pip-check="$PWD/workspace/rebuild/proof/pip-1440"
```

The retail pose regression reproduced the old single-camera failure (all three
soldiers visible only in the inset had stale poses), then passed with the extra
frustum, while more than 30 off-screen poses still culled. Release extension build,
GDScript project parsing and the 50-frame vision/transport regression passed. The
rendered walks cover pin/toggle/swap/zoom, player/team fog, layout, clean mode,
quit suspension, rewind of both enabled and disabled insets, and all 675 encounter
CRCs. The camera's client-frame counter is excluded from the zoom-isolation
comparison; its position, angle, pitch and zoom must remain unchanged.

The engine identity includes presentation sources, so the encounter was generated
again under the new profile. Compared with the preserved M4 recording, only that
identity line changed: the game header, commands, all 675 hashes and end record
are byte-identical. No simulation pins changed or were edited. Captures and
recordings remain outside Git; the full gate was still outstanding at this
checkpoint (completed in the later full-gate ledger).

A follow-up fixes paused POV changes: refreshing terrain fog alone left object
visibility cached from the previous view until the next logic advance. Direct
player/team/all selection and Tab cycling now refresh the drawable views as well.
The retail red control removed only that refresh and exposed 34 of 79 shrouded
objects; restoring it leaves none exposed. The rendered PiP walk also checks the
actual shared-world visibility flags, rather than just the POV label. Regenerating
the encounter changed only the engine identity; its command/hash payload remained
byte-identical.

## Live transport (milestone 6)

The same controls operate on online spectators. The timeline ends at the highest
complete command frame already received from the relay, not the players' current
undelayed simulation. Pause stops the local playhead but continues fetching and
assembling the relay log. Step advances one received frame. **Go live [G]** seeks
to the received edge and resumes at the chosen speed; it never requests an
undelayed ticket or changes the relay's policy. A speed below 1x can subsequently
fall behind that edge again.

Backward seeking rebuilds from the original lobby seed and replays the retained
in-memory command log. The network session, spectator identity, signed delay and
already received bytes survive; the old world and its driver do not. Available
recorded CRCs are compared again on the new traversal. Later-arriving CRCs are
also compared while paused. A mismatch, malformed log or relay refusal stops
playback rather than silently continuing. Network log-size limits still apply;
this is not an unbounded DVR and no native snapshots are claimed.

The lobby defaults `setup.spectatorDelay` to **120 seconds of game time**, signed
into spectator tickets and enforced by the relay. Hosts can explicitly configure
0–600 seconds through the online setup API (`POST /v1/games/<id>/setup`, before
the game starts), or `tools/net/online_client.py host --spectator-delay SECONDS`.
The native staging-room movie does not have a delay editor; omitting the field
uses the server default. Spectators cannot lower the delay from the caster HUD.
The HUD states the signed delay and the additional lag behind the received edge.
When the source game is paused, the relay's game-frame frontier holds as well.

`caster_live_walk.gd` is the rendered regression against a real lobby, relay and
two peers. Launch the client with a signed spectator START and
`--caster-live-check=/outside/repo/live-1080` (repeat at 1440p). It checks paused
reception, exact stepping, received-edge rejection, restart/fast-forward hash
agreement, retained POV/PiP, Go live and clean-mode controls. The source fixture
must continue sending after the client has paused; a pre-finished static log
does not substitute for that test.

The final default-Vulkan real-relay walk passed 50 checks each at 1920×1080 and
2560×1440, including
received-edge rejection, exact paused rewind/step/fast-forward state hashes,
continued receiving without changing the paused world, and Go live while the
source advanced. Go live receipts matched their contemporaneous received edge;
checking a later moving edge would measure subsequent source progress instead.
Received CRCs matched without mismatches before the final rewind.
The signed START tickets encoded the omitted-field default of 120 seconds.
Captures include the visible restart progress bar, paused/receiving/behind/edge
states and clean mode; they remain outside Git. The immediate and late CRC-failure
regression stops subsequent acquisition rather than treating missing or bad
hashes as verified. All seven captures per resolution were inspected.
Software-rendered capture checks do not certify hardware/OBS frame pacing.

## Full gate (2026-10-16, Linux)

The full native suite ran once: **3,270 cases, 3,265 passed, five failed;
7,130,926 assertions, five failed; 20 doctest skips**. All five failures were
missing reference MP3 inputs, not decoder mismatches. The unchanged reference
deck previously supplied by the coordinator was then used for those exact five
cases: **five passed, 465 assertions**. No fixtures, goldens or simulation pins
were generated or changed. This is a full run plus targeted successful reruns,
not a claim that the original full-run command exited successfully.

Early-return skips are separate from doctest skips. The 34 `RW_GAME_DAT` skip
messages were cleared by a targeted run with the installed retail binary:
**83 cases, 40,278 assertions passed**. The ten Node/lobby-service skips were
cleared with the documented Node/server environment: **ten cases, 1,010
assertions passed**. Five Windows-only x87 oracle skips and one Windows-only
`shlwapi` oracle skip remain unexecuted on Linux. Windows-native execution is
not claimed; Linux build/test equivalents were used instead of `run_tests.bat`.

After replacing the observer code's bare arithmetic with the existing `SimMath`
facade, the incremental extension/tests/peer/relay build passed. Focused
`CASTER-*` tests passed **six cases / 1,373 assertions**, including signed-cell
floor boundaries and immediate/late CRC failure. The full AST audit passed:
**991 compile commands, zero new problems**, with 344 existing pinned findings
unchanged. Vision/director/overlay tests and smoke passed; the rebuilt encounter
fixture checked **675 recorded hashes**. The lobby suite passed 34 tests; all
nine CLI tests passed across the original run and the two Node-PATH reruns.

The documented default **Forward+ / Vulkan** shader test passed all 12 patch
assertions and the map readback checks on software llvmpipe after installing the
missing Vulkan driver. A separately forced **Compatibility / OpenGL** run failed
seven readback assertions (six blend/alpha checks and map colour diversity).
Those tested shaders, terrain-builder and test sources are unchanged from the
branch base; no pre-change binary control was run. This remains a
renderer-specific limitation, not a justification to change expectations or
claim that every renderer passes. No shader expectations or tolerances changed.

The final source gate also runs `tools/precommit.py --staged` and staged
whitespace checks. Captures, raw logs, reference fixtures, tickets, service keys
and retail files remain outside Git. This presentation feature does not close
the engine's existing retail-parity stops or integrate native save snapshots
into caster rewind. The candidate is
qualified for the Linux/default-Vulkan gate described here, not Windows-native
oracle coverage or an all-renderer pass.

Release-note: Added [Replays]: Caster vision and POV controls, replay transport
and deterministic restart-based rewind, action camera, broadcast overlays and
clean mode, pinned picture-in-picture, and delayed live spectator controls.

See [caster checklist](CASTER-CHECKLIST.md) for operational checks and limitations.

## Archive review (DEVPORT-5)

Brought into the archive by DEVPORT-5 (cherry-picked, authors kept). Review notes:

- Replays open on all vision, as retail: in playback the "ReplayObserver" side is the local
  player (RW 0x6282F0) and the whole map is revealed for it (RW 0x62F91A). The engine keeps the
  recording slot as the local player, so the caster drops the recorder's end-game requests and
  logs each one (`GAME END request not shown to the caster [S-4932]`).
- Team vision: RotWK already gives every player its allies' looks (the looker mask, RW
  0x6A8695(index, 3, 0)), so with mutual allies the team view equals each member's own view; it
  adds cells only for a one-sided alliance. The vision test asserts that retail fact.
- The quit menu's close unpauses a single player game unconditionally, as retail's
  `~AptQuitMenu` (RW 0x921904); only a caster's replay paused on the transport bar stays paused.
- Stops: S-4930 (no recorded camera: POV keeps the free camera), S-4931 (rewind is restart and
  fast-forward, no keyframes), S-4932 (replay end for the observer side not verified).
- The Godot tests (`caster_vision_test.gd`, the director fight recording + `caster_overlay_test.gd`,
  `caster_end_fixture.gd`) run in the gate's pytest step through `tools/caster/test_caster_godot.py`;
  the rendered `--caster-*-check` walks remain manual (they need a display).
