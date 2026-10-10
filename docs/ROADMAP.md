# OpenBFME roadmap to a 1:1, mod-compatible RotWK 2.01

This is the work left between the current rebuild branch and a game that plays
exactly like retail RotWK 2.01 from the player's own files, runs retail mods
unmodified, and can eventually play against retail clients. `docs/PLAN.md` holds
the standing rules and gates; `docs/STOPS.md` holds every open evidence gap.
This file orders the work.

Status as of 2026-10-10 (the first automatic preview, v0.3.0-preview.1). Lane
names in brackets are merged (M) or running (R).

## What "done" means

A milestone is done only when its gates pass, not when the code exists.

1. **Retail-compatible profile:** retail 2.01 LAN replays play back with
   `MSG_LOGIC_CRC` parity from frame 0 to the end (PLAN "Oracles and gates").
   This is the hard proof of 1:1 simulation.
2. **Visual and audio parity:** side-by-side captures of the same retail states
   (menus, a skirmish base, a battle, a cinematic) match within agreed
   tolerances, with every remaining difference a registered stop.
3. **Content coverage:** every map (181 in pure 2.01, 122 in `mapcache.ini`),
   all 7 factions, every one of the 4,657 templates, and every one of the 245
   module classes retail uses (329 registered) is loaded, created and exercised
   by a test.
4. **Mods:** a mod that runs in retail runs here unmodified (PLAN rule 6),
   proven on a mod corpus. Edain first, after the base game.
5. **Multiplayer:** the eight cross-play gates in PLAN, in order.
6. **Zero unexplained stops:** every stop in `docs/STOPS.md` is either
   resolved or explicitly accepted as a profile difference.

## Where we are

| Layer | State |
|---|---|
| Retail mount, BIG archives, MD5-verified policy | done [INI-1 M] |
| INI system: lexer, macros, includes, field parsers, checksum inputs | done [INI-1 M] |
| Oracle harness for real retail functions (Windows) | done [ORACLE-1, FOLLOW-1 M] |
| W3D models, hierarchies, animations, pose evaluation, GPU instancing | done [W3D-1, W3D-2 M] |
| Maps: every chunk of 181 maps, terrain renderer, water, rivers, roads | done [MAP-1 M]; colour grade and map fog [RENDER-4 M] |
| Shroud, fog of war, stealth | done [VIS-1, STEALTH-1, STEALTH-2 M]; ghost buildings in the fog (S-561) |
| Object model: 4,657 templates, 329 module classes, inheritance, map.ini overrides | done [OBJ-1 M]; module coverage in `docs/module-coverage.md` |
| Live objects, scheduler, players, production, movement, pathfinding | done [LOGIC-1, PATH-1, PROD-1, MOVE-1, MOVE-2, SMOOTH-1..3, EXIT-1, IDLE-1 M]; horde spacing and hostile hordes passing through each other [MOVE-3 R] |
| Combat: weapons, damage, death, horde melee, crush and knockback, projectiles, FX | done [WEAPON-1, HORDE-2, COMBAT-1..4, ANIM-1, FX-3 M] |
| Building, economy, upgrades, experience, garrisons and transports | done [BUILD-1..4, CASTLE-1, ECON-1, UPGRADE-1, XP-1, GARRISON-1..3, MODULES-1..3 M] |
| Spell book, powers, heroes, Create-a-Hero records in the game | done [SPELL-1, SPELL-2, HERO-1, HERO-2 M]; the hero builder screen not ported (S-1226) |
| Skirmish AI | plays and finishes games [AI-1..3 M]; base, unit and tactical layers carry inferences (S-413, S-415, S-417) |
| APT menus, shell, skirmish setup, options, score screen | [APT-1..4, UI-1, UI-2, END-1, END-2, FB7-1 M]; the advanced options page and save / load pages open |
| In-game HUD, input, hotkeys, control groups, camera | [HUD-1..4, QA-1, PLAY-1, INPUT-1 M]; health bars and other HUD items [HUD-5 R] |
| Audio: events, voices, music, footsteps, stereo image | [AUDIO-1..5 M]; damage sounds and scripted music open (S-1242, S-246) |
| Map scripts, campaign flow, movies | [SCRIPT-1..3, CAMP-1H, CAMP-2 M]; no save game or campaign menu (S-1360) |
| Lua 4.0.1 runtime, bindings, event dispatch | [LUA-1 M] |
| LAN multiplayer: lockstep, disconnects, replays, cross-OS determinism | [MP-1, MP-2, WIN-1 M]; online play not started |
| Performance: job pool, parallel loading, presentation culling | [PERF-1..3 M] |
| Release: tester packages, logs, crash reports, launcher with signed updates | [RELEASE-1, WINCRASH-1, LAUNCH-1 M]; automatic preview releases [AUTOREL-1 R] |

Skirmish is playable from the real menu with all 7 factions, against the
skirmish AI or over LAN between Windows and Linux. The QA lanes played 71
scripted games through the real input paths with no crash or stall, and every
replay matched bit for bit. The campaign plays mission by mission with its
movies and narration. Not there yet: save / load, War of the Ring, the
Create-a-Hero builder, online play and mods; team colours are drawn with an
inferred blend (S-119). The README's
status table is the player-facing summary of this one.

## Milestones

Priority order set by the owner: a complete, fully working game, not demos.
**Skirmish first, then multiplayer, then extreme performance, then campaigns,
War of the Ring and everything else.** Each lane is roughly one Sonnet
implementation pass plus Sol review; big systems take several.

### M1 - Live units, production and movement

State (2026-10-10): done. Units train, leave by their exits and walk in all 7
factions' games; MOVE-3 is still working on horde spacing (FB-0006, FB-0012).

Gate (tests, not a video): for every faction, every unit and horde trains at
each building whose CommandSet offers it, with retail build time and cost,
exits by the retail door/exit path and walks to the rally point; verified by a
retail test over all 7 factions.

- LOGIC-1: module runtime, Object/Drawable, creation order, the BFME scheduler
  (six-tick logic frame at 5 fps), destruction, PlayerTemplate, players, teams,
  money, Godot live-world view. [R]
- PATH-1: pathfinder grid, A*, zones, reservations, AI move state. [R]
- LUA-1: merge, then wire OnCreated and the event sites of live objects. [R]
- PROD-1: CommandSet/CommandButton, ProductionUpdate (queue, build time, cost,
  doors), QueueProductionExitUpdate, rally points, horde production.
- MOVE-1: AIUpdateInterface move/idle states on live objects, locomotor
  integration, HORDE runtime checklist steps 7-9 (member pass, cadence,
  reform-vs-wheel).

### M2 - Combat

State: done (WEAPON-1, PROJ-2, HORDE-2, AI-1, PHYS-1, FX-2 / FX-3, AUDIO-1..5,
COMBAT-1..4, ANIM-1); details are matched to community reports as they come.

Gate: two armies fight on a retail map; damage, death and horde melee look and
time like retail captures.

- WEAPON-1: Weapon/WeaponSet/Armor INI and runtime, damage types, the damage
  pipeline (ActiveBody/StructureBody/HighlanderBody...), death modules
  (SlowDeathBehavior, DestroyDie, KeepObjectDie, ...).
- PROJ-1: projectiles (BezierProjectileBehavior, missile/arrow flight, collision).
- HORDE-2: horde attack machine, MeleeBehavior (Amoeba, HoldGround, Swarm),
  member attack/re-acquire, crush/trample (SquishCollide), banner carriers,
  formation swap (horde spec §5 steps 10-15).
- AI-1: AIUpdateInterface attack/guard/hunt/flee states, targeting, stances
  (StancesBehavior), HordeAIUpdate.
- PHYS-1: PhysicsBehavior, knockback/fling, collision and separation.
- FX-1: FXList, ParticleSystem.ini and the particle simulator, emitters on
  bones, TransitionDamageFX, hit reactions.
- AUDIO-1: AudioEvents, sound/voice/EVA playback, music, LargeGroupAudioUpdate.

### M3 - Economy, construction and upgrades

State: done for what skirmish uses (BUILD-1..4, CASTLE-1, ECON-1, UPGRADE-1,
XP-1, MODULES-1..3); module coverage is tracked in `docs/module-coverage.md`.

Gate: a human-controlled base builds, earns and upgrades exactly as retail
(build times, costs, command points, income rates).

- BUILD-1: builders, build plots/foundations, GettingBuiltBehavior,
  BuildingBehavior, castles and castle expansions (CastleMemberBehavior),
  walls and wall hubs, repair.
- ECON-1: resources (TerrainResourceBehavior, farms/supply), income, command
  points, unit caps, bounty.
- UPGRADE-1: Upgrade INI and every upgrade module class (SubObjectsUpgrade,
  WeaponSetUpgrade, ArmorUpgrade, CommandSetUpgrade, ModelConditionUpgrade,
  StatusBitsUpgrade, LevelUpUpgrade, AttributeModifierUpgrade, ...).
- XP-1: experience, levels, veterancy effects.
- Remaining behaviours by census usage until all 245 used classes are ported
  (EmotionTrackerUpdate, AttributeModifierAuraUpdate, FlammableUpdate,
  FireSpreadUpdate, LifetimeUpdate, AutoHealBehavior, PickupStuffUpdate, ...).
  The other 84 registered classes follow for mod parity (M10).

### M4 - In-game interface and a playable skirmish

State: done. A skirmish starts from the real menu and is played with the real
HUD to the score screen (QA-1, QA-2, PLAY-1, INPUT-1); HUD-5 is running.
Team colours are drawn with an inferred blend (S-119); the retail combine
is not recovered (S-022).

Gate: start a skirmish from the real menu, play it with the real HUD, and win
or lose.

- APT-4: shell, window manager, gadgets, skirmish setup -> GameInfo. [R]
- HUD-1: Palantir HUD callbacks, command bar, unit portraits, tooltips,
  radar/minimap, cursors, selection and control groups, rally points, hotkeys
  (CommandMap), messages (GameMessage 1001-1147 per PLAN rule 4).
- CAM-1: tactical camera (retail limits, zoom, pitch), camera INI.
- VIS-1: shroud/fog of war, stealth/detection, visibility.
- SHELL-2: View3D shell map behind the menus, BinkMovie (intro and menu
  movies), loading screen (LoadScreen.apt), score screen, options, profiles.
- RENDER-1: shadows, decals, terrain post effects (S-031), exact water/river,
  weather, time-of-day, house colour combine (S-119), LOD rules.

### M5 - Full skirmish 1:1, all base-game factions

State: in progress. All 7 factions play whole games against the skirmish AI;
the AI's base, unit and tactical layers still carry inferences (S-413, S-415,
S-417).

Gate: full skirmishes against the skirmish AI on every map type, with every one
of the 7 factions, play like retail; every unit, hero, power and upgrade works.

- SPELL-1: sciences, the spellbook, power points, special powers and their
  modules (SpecialPowerModule, SpecialAbilityUpdate, OCLSpecialPower, ...).
- HERO-1: heroes, revive, hero abilities, level-ups.
- AIP-1: the skirmish AI player (AIData.ini, build lists, attack waves,
  difficulty levels), Wild and Angmar AI included.
- FACTION-1..7: per-faction exhaustive tests (every unit trains, fights,
  upgrades, uses its abilities), including RotWK's Angmar and the inn/creep
  structures.
- GAMEMODES-1: King of the Hill, capture the flag and the other RotWK modes.

### M6 - Multiplayer that is really good

State: LAN works. MP-1 / MP-2 lockstep with disconnects, drops and replays;
WIN-1's Windows / Linux lockstep games stay in sync. Online play (the relay
service below) is not started.

Gate: automated 2-8 player games (AI-driven clients) across Windows and Linux
run for hours with zero desyncs; human games over LAN and the internet are
smooth at realistic latency and packet loss.

- DET-1: cross-platform determinism. The simulation must give bit-identical
  results with MSVC on Windows and GCC/Clang on Linux: every simulation float
  operation goes through the numeric facade (PLAN rule 3) with fixed
  operation order and no FMA contraction, CRT functions (sqrt, sin, cos,
  atan2, ...) reproduced bit-exactly in software, no uninitialised reads, no
  container-order or pointer-order dependence. Tested by running the same
  match on both platforms and comparing per-frame state hashes.
- CRC-1: the logic CRC and INI checksum exactly as retail (PLAN rule 5), sent
  every interval; desync detection with an automatic per-object bisector that
  names the first diverging field.
- NET-1: lockstep command pipeline (GameMessage), frame pacing and input delay
  adapted to latency, the network clock, retail's command batching.
- NET-2: lobby and matchmaking UI through the real multiplayer APT screens,
  LAN discovery, direct connect, an OpenBFME relay/rendezvous service with NAT
  traversal for internet play, reconnect after transient drops, host
  migration where retail semantics allow.
- NET-3: replays of OpenBFME games (enhanced profile format, PLAN), observer
  mode, game result reporting.
- NET-4: network test harness: simulated latency, jitter, loss and
  reordering; soak tests in CI.

### M7 - Retail parity and cross-play with retail clients

State: not started (needs a clean `game.dat` and retail replays).

Gate: retail 2.01 LAN replays play back with CRC parity; then PLAN's eight
cross-play gates in order.

- REPLAY-1: `.BfME2Replay` playback through the command pipeline, CRC
  comparison per interval, divergence bisector.
- NUM-2: retail-exact numerics on every simulation path (x87/SSE operation
  order, PC24, MSVCR71 CRT results) where DET-1 only required self-consistency.
- PROV-1: resolve S-001 and S-080 with a clean 2.01 `game.dat` (or retail
  recordings); re-verify every binary fact read from the patched image.
- XPLAY-1..8: retail packet encoding, LAN discovery and lobby, version/INI/exe
  checks, map transfer, initial RNG/player/hero state, frame-command
  completeness and pacing, then a live game with a retail client.
- SAVE-1: save/load (xfer).

### M8 - Extreme performance

State: started early (PERF-1..3: the benchmark harness, a job pool on every
core, parallel loading, presentation culling).

Starts once skirmish and multiplayer work; performance hygiene applies from day
one (no lane may regress the measured budgets below).

Targets: the largest retail battles (several thousand units) at a locked high
frame rate on the Steam Deck and far beyond on desktop GPUs; logic frames well
inside the 200 ms budget at the 5 fps rule clock so the client never stalls;
fast map loads.

- PERF-1: profiling harness and benchmark scenes (big battles, every map, many
  particles), budgets enforced in CI.
- PERF-2: simulation: data-oriented layouts for hot modules, pathfinding and
  collision acceleration, multithreaded work that keeps deterministic order
  (parallel compute, serial commit), incremental updates.
- PERF-3: rendering: GPU-driven instancing for every draw class, culling and
  LOD, particle batching, terrain chunk streaming, shader permutation control,
  frame pacing and smooth interpolation between logic frames.
- PERF-4: loading and memory: archive caching, parallel asset decode, startup
  and map-load times.

### M9 - Campaigns, War of the Ring and the rest

State: the campaign plays mission by mission (SCRIPT-1..3, CAMP-1H, CAMP-2:
scripts, narration, VP6 movies); no save game or campaign menu (S-1360).
Create-a-Hero records play in a game (HERO-2), the builder screen does not
exist yet. War of the Ring not started.

Gate: every campaign mission and the tutorials complete as in retail; War of
the Ring and Create-a-Hero work.

- SCRIPT-1..3: map script engine (SCB conditions and actions, teams, waypoints,
  timers, counters), camera scripts and cinematics, EVA and objectives.
- CAMPAIGN-1: BFME2 good/evil campaigns, RotWK Angmar campaign, epilogue.
- WOTR-1: War of the Ring (living world map, StrategicHUD family), including
  multiplayer War of the Ring.
- CAH-1: Create-a-Hero.
- MOVIE-1: Bink playback for campaign movies.

### M10 - Mods (designed in from the start)

State: not started as a lane; every lane follows PLAN rule 6.

Gate: Edain runs unmodified with its own assets; then further mods.

- MOD-1: retail `-mod` semantics (PLAN rule 6): directory or archive, file
  precedence flag (0xDEC490), mount order, loose files inside explicitly mounted
  mods (PLAN rule 7), profile identity hashing.
- MOD-2: mod corpus gate. Put Edain on the developer machine (never commit
  it); every INI/W3D/APT/map/Lua/audio file loads; every template creates;
  every error matches what retail reports for the same file.
- MOD-3: the 84 module classes retail data never uses, plus every INI field
  and block retail accepts but retail data never exercises (mods use them).
- MOD-4: mods that patch `game.dat` (2.02, Age of the Ring): reimplement each
  executable change as a pinned profile feature.
- MOD-5: WorldBuilder-made user maps and map-folder Lua/XML overlays.

Mod compatibility is not a late add-on: every lane above already follows PLAN
rule 6 (full registries, retail-exact parse acceptance and rejection, no
hard-coded retail content), and reviewers check it.

## Cross-cutting work

- **Stop burn-down.** 541 open stops on 2026-10-10 (rows of `docs/STOPS.md` not
  marked closed). Each lane closes the
  stops it owns; M7 closes the ones that need a clean binary or recordings.
- **Windows parity.** The Windows build is cross-compiled with llvm-mingw
  (`tools/release/build_windows.sh`, `docs/WINDOWS.md`); its suite and the
  launcher tests run natively on a Windows machine. The MSVC `build.bat` path
  is still to be confirmed (S-1541).
- **Performance.** Keep the current budget: 1,000 animated soldiers at more
  than 300 fps on the Deck GPU, the largest map with all objects at more than
  200 fps; add simulation budgets (logic frame under 10 ms with 2,000 units).
- **Follow-ups log.** `workspace/rebuild/FOLLOWUPS.md` (not committed) tracks
  small items found in review.

## What only the owner can provide

These unblock gates that no amount of code can pass on its own:

1. **A clean, unpatched RotWK 2.01 `game.dat`** (original disc or installer).
   The installed one carries community patches (S-001), including the RNG
   (S-080). Every binary fact read so far carries that caveat.
2. **Retail 2.01 LAN replays and recordings:** multiplayer replays with CRC
   messages, plus a few frame-counted video captures of known states, for M7
   and the visual gates.
3. **A Windows machine** for MSVC builds and the retail-function oracles
   (available since 2026-10-09 for the native suite).
4. **Mod files** (Edain first) for M10.

## Rough size

From the current state: about 35-45 more lanes to M5 (a full 1:1 skirmish),
about 10-15 for multiplayer (M6), then about 25-35 for M7-M10. Lanes run in parallel where dependencies allow
(three to five at a time has worked). The order above is fixed by dependencies;
the pace is set by parallelism and review capacity.

## Community feedback intake (FB items, updated 2026-10-10)

Reports from the project's Discord feedback channel are evidence to reproduce, not
diagnoses: each one is checked against retail data and the binary before work, and
counts as fixed only when merged with a test that fails on the old code. Gameplay /
balance redesigns or compatibility-breaking changes go to the owner. The live status
is kept in the community service (`bfme-community feedback-status`).

| Item | Report (short) | State | Where |
|---|---|---|---|
| FB-0001 | units run in place | fixed: RotWK's update phase order (melee treadmill 770 -> 0 frames in a 2v2), ram crews | ANIM-1, EXIT-1, IDLE-1 (merged) |
| FB-0002 | Grond moves without its trolls, wheels locked | fixed: crew pushes, wheels turn | COMBAT-3, COMBAT-4 (merged) |
| FB-0003 | cavalry charges don't throw infantry | fixed: crush throw measured against retail, RamPower hit ported | COMBAT-3, COMBAT-4 (merged) |
| FB-0004 | troll hit timing, troll clubs don't launch units | fixed: shockwaves / MetaImpactNugget | ANIM-1, COMBAT-4 (merged) |
| FB-0005 | units stuck around buildings | fixed (the barracks exit); reopen if seen elsewhere | EXIT-1 (merged) |
| FB-0006 | two hordes on identical coordinates | in progress | MOVE-3 |
| FB-0007 | main-menu button text not vertically centred | fixed: APT text placed as RotWK's display string | FB7-1 (merged) |
| FB-0008 | no ring animation on victory / defeat | fixed | UI-2 (merged) |
| FB-0009 | jerky archer firing cycle | fixed | ANIM-1 (merged) |
| FB-0010 | menu / spell book opacity | fixed (binary-derived) | UI-2 (merged) |
| FB-0011 | corpses vanish too fast (SlowDeathBehavior timing) | fixed: RotWK's SlowDeathBehavior; the fast vanish did not reproduce | COMBAT-4 (merged) |
| FB-0012 | hostile hordes pass through each other | in progress | MOVE-3 |
| FB-0013 | rear ranks run in place at the melee leash | fixed | MOVE-2 (merged) |
| FB-0014 | archers teleport out of towers | fixed | GARRISON-3 (merged) |
| FB-0015 | Rohirrim shooting animations / arrows from the body | fixed | ANIM-1, HUD-4 (merged) |
| FB-0016 | multiplayer lobby layout | fixed | UI-1, UI-2 (merged) |
| FB-0017 | no campaign voice lines | fixed: narrated intros and script voice lines | CAMP-1H (merged) |
| FB-0018 | unit jitter, wandering infantry | fixed | MOVE-2 (merged) |
| FB-0019 | walls rise all at once | fixed: the rise and player wall placement | BUILD-4, QA2-FIX (merged) |

## Online multiplayer direction (owner, 2026-10-08)

Online play goes through an OpenBFME relay service: lobby and server browser
(replacing GameSpy, accounts required), a turn relay so players never see each
other's addresses, a server-side game log (replays, reconnect by catching up,
spectators) and an optional headless verifier running the same deterministic
simulation (snapshots for fast rejoin and multiplayer saves, desync and tamper
detection, trusted results). The client protocol is part of this repository; the
hosted service and its anti-cheat components are operated separately. Retail 2.01
cross-play stays the separate, best-effort goal behind PLAN's cross-play gates.
Phases: relay MVP (lobby + relay + log) after the closed playtest, then the
verifier and snapshots, then the browser, accounts and spectators, then the retail
gateway.
