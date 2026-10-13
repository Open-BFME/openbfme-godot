# Saves: retail's snapshot, a command-log save, and how they fit

Lane SAVE-1, 2026-10-12. The owner accepted every recommendation the same
day. Phase SAVE-2 (the command-log save for single player) is built: see
section 8, "What is built".

The owner's direction: "for saves we probably need to support both ideally for
a faithful port but we might want to design it in a way for the sdk to work
maybe a backwards compatibility mode". "Both" means:

- **A. Retail's save system, faithfully.** RotWK 2.01's Xfer snapshot of the
  whole game (GameState, XferSave / XferLoad, every class's `xfer`, `crc` and
  `loadPostProcess`), its file format and the Save / Load screens.
- **B. A command-log save.** The game's START plus every command so far.
  Loading replays the log at full speed. This reuses ONLINE-3's catch-up.

The answer in one paragraph: **one save container with three parts, a header,
the command log and an optional snapshot, and one Xfer framework that writes
the snapshot.** Ship B first. It needs almost no new engine code, because every
skirmish is already recorded as a replay. Then port retail's Xfer framework
and give every simulation class an `xfer` beside the `crc` it already has.
That snapshot is what makes long games, multiplayer resume, fast rejoin (O4)
and old saves work. Reading and writing retail's own save files becomes a
separate mode on top of the same framework. It is worth having, but it is the
most expensive part and the least needed, so it comes last.

Sources: the RotWK binary through Ghidra and `tools/re/rw2decomp.py`, the
Open-BFME-2 decomp (BFME2 1.06), Zero Hour's `GameState.cpp`,
`GameStateMap.cpp` and `Object.cpp` as the donor, `docs/ONLINE.md` and the
ONLINE-3 lane (`lane/online3`, not merged at cb2ae2c7), and
`docs/ARCHITECTURE-MODS-UBER.md` (lane/arch1, f4597acf). RW addresses are
RotWK 2.01 VAs. Read their caveat: S-001 (the analysed `game.dat` carries
community-added sections). **(inference)** marks a conclusion that is not a
binary fact. **(to verify)** marks something nobody has checked yet.

## 1. What retail does (A, the facts)

### The subsystem list

`GameState::init` (RW 0x6DF73E, tier B, same shape as the decomp's
byte-matched BFME2 0x6DE641) registers 54 (token, subsystem, list) triples in
five snapshot lists. ZH has two lists. BFME2 and RotWK have five.

| List | Used for | Blocks, in file order |
|---|---|---|
| 0 | a normal save (skirmish, campaign mission, War of the Ring battle) | `CHUNK_LivingWorldLogic`, `Audio`, `GameState`, `Campaign`, `Palantir`, `GameStateMap`, `TerrainLogic`, `TeamFactory`, `Players`, `GameLogic`, `Radar`, `ScriptEngine`, `SidesList`, `TacticalView`, `GameClient`, `InGameUI`, `Partition`, `Shroud`, `Collision`, `ParticleSystem`, `TerrainVisual`, `GhostObject`, `VictorySystem`, `TaintManager`, `WeatherSystem`, `SkirmishAISystem`, `MineshaftPortalNetworkManager`, `AiOrdersManager`, `SpellStore`, `ObjectivesMenu`, `MissionObjectives`, `FireLogicSystem` (32 blocks) |
| 1 | the deep logic CRC (ZH `SNAPSHOT_DEEPCRC_LOGICONLY`) | `TeamFactory`, `Players`, `GameLogic`, `ScriptEngine`, `SidesList`, `Partition`, `Shroud`, `Collision`, `SkirmishAISystem` |
| 3 | the multiplayer War of the Ring save (mode 6) | `LivingWorldLogic`, `Audio`, `GameState`, `Campaign`, `Palantir`, `GameStateMap`, `GameLogic` |
| 4 | the save between campaign missions (mode 1) | the list-3 set without `Palantir` |
| 2 | unused | |

The block names are all present as strings in RotWK. `CHUNK_VENDOR` is in
the binary too, but it is a KindOf name, not a save chunk (SAVE-A0,
`docs/SAVES-RETAIL.md` section 3).

### The file

The byte layout is read from the decomp's Xfer library, which is tier A
(identical code) to RotWK:

```
"ALAE" "2STR"        u32 magic 0x45414C41, u32 0x52545332  (XferSave::Open RW 0xA20DD0; XferLoad::Open RW 0xA200A3 also accepts "1STR", which has no version word)
u32 version          1; the load dispatcher (RW 0x6DEA25) refuses a value above 1
u32 tagged flag      when set, every transfer carries a type tag and every block a name (retail writes 0)
then, per block of the chosen list:
  str  token         "CHUNK_..."     (GameState::xferSaveData RW 0x6DDF2F)
  "BLOK" [name] u32 end, payload, ["EBLK"]   (XferSave::beginBlock RW 0xA2111A, endBlock RW 0xA206EF)
str "SG_EOF"
```

SAVE-A0 corrected two details (`docs/SAVES-RETAIL.md` section 1): the word
after BLOK is the absolute file offset of the block's end, not a size, and
EBLK (like the name) is written only in tagged files.

- **Loading skips unknown blocks.** An unknown token is skipped by its size
  (`findBlockInfoByToken` RW 0x6DDECA, `skipBlock` RW 0xA203D6). A block
  whose xfer throws is skipped as a "BAD block" (RW 0xA201CA), and the
  post-process records it registered are pruned (RW 0x6DD5E9).
- **Versions are per class, one byte.** `Xfer::operator==(Version&)`
  (RW 0xA1F5B9, tier A) writes the current version. On load it accepts any
  version from the class's *earliest supported* to its *current* one, and
  throws "Found version %i but the earliest supported is %i" or "Don't know
  how to load version %i (most recent is %i)" otherwise. So **retail already
  has migrations**: each class's xfer reads every version in its window.
- **Some xfers carry a class tag.** RW 0x98B896 writes the state name
  ("HordeMeleeHoldGround") and throws "Xfer data saved by %s is now being
  loaded by %s" on a mismatch.
- **Inside an object, modules are keyed by their tag** (ZH `Object::xfer`,
  donor): a count, then per module its tag name and a BLOK. On load, a module
  the object no longer has is skipped, with the comment "we have removed it
  from the object definition in a future patch". RotWK's Object::xfer
  (RW 0x697F28, version window (1, 29)) keeps it (SAVE-A0).
- **The map travels with the save** (ZH `GameStateMap::xfer`, donor): the
  map file is embedded, and loading writes it out and starts a new game on it
  before the other blocks overwrite the state. RotWK's GameStateMap
  (RW 0x82B8E4, (1, 2)) does this, with the map's `.wak` sibling as well;
  nothing is embedded on the War of the Ring strategic map (SAVE-A0).
- **The 'DSCR' marker.** beginBlock skips a length-prefixed record that
  starts with a 0x44534352 marker. It is a field label that XferSave writes
  in tagged files only (RW 0xA20B26); retail's saves are untagged (SAVE-A0).

### Files, modes and screens

- `GameState::determineCurrentGameSaveFileMode` (RW 0x6DCFDF, tier A) gives
  a mode from 0 to 6. RW 0x6DCDE9 maps the mode to an extension: 2
  `.BfME2Skirmish`, 3 and 4 `.BfME2WotR`, 6 `.BfME2WotRMP`, anything else
  `.BfME2Campaign`. The files go in the user data folder's `Save\`. The
  campaign's automatic save is `__AUTO#SAVE__` (S-1360). What each mode
  means (mode 5, a multiplayer War of the Ring battle, falls into the
  `.BfME2Campaign` arm): `docs/SAVES-RETAIL.md` section 3.
- **Saving** is GameState::saveGame (RW 0x6DE498, tier A), entered through
  RW 0x6DEF4E. A disk-space check comes first (RW 0x6DD795).
- **Loading** (RW 0x6DF4D1 wraps the dispatcher RW 0x6DEA25) goes in this
  order: reset the engine, xfer the blocks of the mode's list, run every
  queued `loadPostProcess` (RW 0x6DD65C: pointers are saved as IDs and
  resolved here), refresh the shroud, then restart the AI. After a War of the
  Ring load (modes 4 and 6) it calls `InitRandom(0)` and suspends the logic
  **(meaning to verify)**. A failure shows "GUI:ErrorLoadingGame" and returns
  to the shell.
- **Screens.** SaveLoad.apt (`AptSaveLoad::*`) is shared with the replay
  list, which MP-2 already ported (`SaveLoadInfo`). Its game-type flags are 1
  Campaign, 2 Skirmish, 4 Replay, 8 War of the Ring single player, 0x10
  multiplayer. The campaign menu between missions is CampaignMenu.apt
  (S-1360).

### Retail's multiplayer save

RotWK has one. It is a two-player confirmation flow handled by GameLogic
(message 0x1E, RW 0x629750, tier A):

1. A player asks to save (kind 1). The other player's game refuses
   automatically while that player is busy ("The other player is busy and
   cannot save now"), or when their disk is full
   (`APT:SaveFileDiskFullMultiplayer`). Otherwise it asks them, with
   `APT:ConfirmMultiplayerSave` / `...WithOverwrite`.
2. A refusal (kind 2) is shown on the asker's save screen
   (`MultiplayerSaveGameDenied`, RW 0x816715).
3. Acceptance (kind 5) goes into the command stream. **Every peer saves its
   own copy on the same logic frame** through TheGameState. Then
   "APT:MultiplayerGameSaved: Do you want to leave the game for now? The
   other player has to agree."
4. Resume happens in the lobby. The host picks the save
   (`AptLanLobby::OnLoadGameBttn`, `AptOnline::CustomMatch::LoadGame`), and
   the game list shows `AptLobbyResumeSavedGame` with "You have previously
   played and saved this game. The host of the game is ready for you to
   join". That icon depends on a 16-byte value in the game info (+0xCC, RW
   0x800A78). **(inference)** It is an id of the saved game that each
   joiner matches against its own copy.

This is the War of the Ring multiplayer save. SAVE-A0 settled it from the
save page: in a multiplayer skirmish the in-game Save opens the page in
Replay mode and writes a `.BfME2Replay`; the confirmation flow runs only on
the WOTRMP page (`docs/SAVES-RETAIL.md` section 3). A multiplayer skirmish
cannot be saved as a game in retail.

Retail's design is the lockstep one: **nobody sends state.** The save is a
command, every peer writes the same snapshot at the same frame, and resuming
means every peer loads its own file. Our multiplayer save follows the same
idea (section 4).

### Where our engine stands

- There is no Xfer, no Snapshot interface and no save container.
- There are about 160 one-way `crc(StateHasher&)` walks (Module, weapons,
  locomotors, AI states, economy, victory, weather, ...) behind
  `GameLogic::hashState`. Their step is retail's CRC step (RW 0xA211DF), but
  they hash OpenBFME's own state, not retail's xfer stream (S-725).
- **Every skirmish is already recorded** (`GodotGameWorld`, lane MP-2: a
  `ReplayWriter` behind every game) in OpenBFME's replay format v4: the
  identity, the NewGameMessage, every frame's commands and every frame's
  state hash.
- ONLINE-3 (not merged yet) adds `CommandLogCatchUp`: START + log to the
  state at frame F, comparing the players' CRCs on the way. Measured: 10, 30
  and 60 minute games catch up in 9, 48 and 106 s on JonathanPC.

## 2. The design: one container, three parts

```
OpenBFME save (.obfmesave)
  header     magic, container version, engine build id, ProfileIdentity text (data, mods, SDK, rules, execution profile),
             game kind (skirmish / campaign / WotR / multiplayer), frame F, state hash at F, description, map, date, preview
  log        the START (NewGameMessage, RNG algorithm, slots) + every frame's commands 0..F + the state hashes   (replay v4 records)
  snapshot   optional: the Xfer snapshot at frame F (or at an earlier frame K, with the log carrying K..F)
  each part: length + SHA-256
```

How a save loads:

1. **Same build and identity, snapshot present:** load the snapshot. That is
   seconds even for long games. Check the state hash against the header.
2. **Same build, no snapshot:** catch up from the log (B). Every recorded
   hash is compared on the way, so a corrupt or foreign log is named at its
   first differing frame.
3. **Different build:** load the snapshot through its versioned blocks (the
   migrations, section 5). If the snapshot is too old to migrate and only the
   log is usable, the loader says which build made it (section 5,
   "Replay with the version that made it").
4. **Anything else** (identity mismatch, missing mod, bad hash, truncated
   file) is a named refusal on screen and in the report. It is never a silent
   default.

Why both parts are kept, even when the snapshot exists: the log costs little
(ONLINE-1 measured an 8-player 3,000-frame replay at 107 KB, 36 KB
gzipped), it is the replay of the game so far,
and it is the fallback that needs no migration code with the matching build.
The snapshot is what keeps loading fast and lets saves survive engine
changes. A player never chooses between them: Save writes both once the
snapshot exists, and only the log before that.

**One Xfer framework, two dialects.** Port retail's Xfer classes (XferSave,
XferLoad, XferCRC, the BLOK / EBLK blocks, `Version` with earliest and
current, `beginBlock` / `skipBlock`) and the Snapshot interface
(`xfer` / `crc` / `loadPostProcess`, GameState's block lists and
post-process list). Then:

- **Native dialect.** OpenBFME's own snapshot inside the container. It writes
  what our classes hold, with OpenBFME version numbers.
- **Retail dialect.** Retail's `.BfME2*` files, with retail's field order and
  retail's version numbers per class.

**For each class that is a 1:1 port of a retail class, the `xfer` is written
in retail's field order with retail's version byte from the start.** Then the
two dialects are the same code for that class, and retail import costs
nothing extra there. Where our state differs (classes we built differently,
OpenBFME-only state such as LogicSnapshot inputs, the numeric facade, Uber
caches), the class gets a native-only block, plus a translator later if
retail compatibility needs it. This is decision D3.

## 3. A: can we read and write retail saves?

**Reading (import) is feasible, class by class.** At least 378 RotWK
functions map to a decomp `::xfer`: 316 tier A (the decomp code is RotWK's),
52 tier B, and 10 attempts or tier C / D. There are also 18
`loadPostProcess`. The full inventory is the first job of SAVE-A0
(section 7). To import, we need:

- **every block retail writes, at every version it can contain, parsed into
  our state.** Unknown top-level blocks can be skipped, as retail does. For
  example, `ParticleSystem`, `TerrainVisual`, `TacticalView`, `GameClient`
  and `InGameUI` are client state, and the client can rebuild a look from the
  logic. Their loss is visual, not a desync, **(inference)** to be confirmed
  for drawables;
- **every field of every logic block mapped to an OpenBFME field.** This is
  the blocker. 108 of the 245 module classes that retail data uses have no
  runtime here (`docs/module-coverage.md`). Their blocks can be skipped by
  size, but the game then continues without that state. Import is therefore
  **complete only when the logic port is**. Until then, it is a named,
  per-module list of what was dropped;
- **the embedded map and the object IDs** taken as saved. Pointers are
  resolved in `loadPostProcess`, as retail does;
- **WotR saves** need War of the Ring, which isn't built (ROADMAP).

**Writing (export) is harder, and only partly testable here.** Retail loads a
file only if every block it reads is byte-correct for its version, so export
needs the full retail state for every registered block, including the client
blocks retail expects. **(to verify)** Whether retail accepts a save with
client blocks left out. The only oracle is retail itself, loading our file on
Windows. That means JonathanPC's desktop under the owner's retail-run rules
(backup and restore of the profile, no UAC). Our state hash is not retail's
CRC (S-725), so "the same game" can only be checked by playing on in both and
comparing what happens.

What blocks a byte-exact round trip, in short:

1. All of retail's per-class field orders and versions (the inventory).
2. The unported modules and subsystems (their state doesn't exist here).
3. The client blocks.
4. Numerics: Faithful mode stores retail-exact floats. Enhanced and Uber
   saves are never exported.
5. No shared CRC to prove equality.

**Recommendation:** import first (it lets players continue a retail campaign
or WotR save), and export last and optional (decision D4). Following PLAN
("Two profiles"), a retail-format save never carries OpenBFME data. Anything
extra goes in a sidecar next to it.

## 4. Multiplayer: save, resume and fast rejoin

### Save

Take retail's flow and keep its principle:

- **Save is a frame command.** The request and confirmation work as in
  retail (RW 0x629750: kind 1 asks; kind 2 refuses with a reason; kind 5
  saves), generalised from "the other player" to "every other human". When
  kind 5 executes at frame F, **every peer writes its own container** (log
  0..F, and the snapshot at F once it exists).
- **Peers prove they saved the same game.** At F, each peer adds the SHA-256
  of its snapshot bytes to the CRC exchange that follows. The state hash
  alone is a 32-bit rotate-add, which is fine for desyncs but not for
  integrity. Snapshot bytes must therefore be identical across OS, which is
  test T3. Until snapshots exist, the log plus the state hash at F are
  enough: every peer's log is the same command stream.
- **LAN:** each peer's file is its own copy, as in retail.
- **Online:** the relay already has the log (ONLINE-2/3). It records the
  save point (frame F, the agreed state hash and snapshot digest) and keeps
  the game's log under a saved-game id, stored like replays (R2 in the
  ONLINE-1 plan: small). One peer, the packet router, may upload its
  snapshot. The others' digests at F prove it. Retention is decision D6.

### Resume

- **The lobby offers "resume"** for a saved game whose players are all
  present, as retail's icon does. The resume START carries the saved-game
  id, F, the agreed state hash, the snapshot digest and the original slot
  map. Players take their old slots by account (online) or by name (LAN).
- **Every peer reaches frame F on its own.** It loads its local container if
  the digest matches. Otherwise it fetches the snapshot from the relay (the
  digest proves it) or catches up from the relay's log (B). It then reports
  the state hash at F. The game starts at F+1 only when every hash matches.
  Nothing else is sent: lockstep resumes as at a normal start.
- **The relay continues the same log** from F+1, linked to the saved game,
  so the finished game's replay covers both sessions.
- **A missing player.** Retail needs every player back. Proposal: the same
  for now, with "give the slot to an AI" as a later option (decision D5).

### Fast rejoin for long games (ONLINE.md phase O4)

Once snapshots exist, a rejoin doesn't need frame 0:

- every 5 minutes of game time (1,500 frames), the packet router issues an
  automatic snapshot command. Every peer computes the snapshot digest at that
  frame and exchanges it with the CRC. The router uploads its snapshot to the
  relay, and the relay keeps the latest one or two;
- a rejoiner fetches the latest agreed snapshot plus the log after it. It
  loads, checks the digest, catches up at most about 1,500 frames (a few
  seconds at the measured 170-330 frames per second) and joins as ONLINE-3
  does today;
- the cost is one snapshot per peer every 5 minutes. Its write time and size
  are measured in SAVE-4, with the budget that it may not cause a visible
  hitch (**to measure**: a 60-minute 8-player game). If it can, the write
  moves off the logic thread after a copy at the frame boundary;
- the same snapshots start a late spectator's catch-up (S-3902) and give a
  future verifier its starting points.

## 5. Mods, the SDK, profiles and old saves

### Identity and mods

- **The header carries the full ProfileIdentity text**: archive hashes,
  `mod <name> <hash>` lines, `sdk <version>`, the extension files, rules and
  execution profile (ARCH-1 sections 1 and 3). By default a load needs the
  same data identity, because templates are referenced by name and every INI
  value can change the game. A mismatch is refused with the differing lines
  named.
- **A mod at a different version** (single player only, by an explicit "load
  anyway"): the data is read by name, as retail does (templates by name,
  modules by tag). A template or module the new data no longer has is
  dropped. Its block is skipped by size, which is retail's own patch rule
  (ZH `Object::xfer`). Every dropped thing is listed in a load report on
  screen. **A missing mod** is a refusal that names the mod and its hash.
- **SDK extension state:** a namespaced module or system writes its own
  blocks, named `<ns>:<name>`, with its own (earliest, current) version
  bytes. Those versions are declared in the mod's manifest, and the identity
  carries them. A Luau simulation script's state is serialized by the
  runtime in creation-counter order (ARCH-1's deterministic iteration). The
  same `migrate(oldState, oldVersion)` hook that ARCH-1 proposes for hot
  reload upgrades an older script state. A namespace that is missing on load
  is a refusal, never a skip, because a script's state can't be dropped
  safely. All of this is SDK-2's work, with this framework under it.
- **Retail-format saves never carry mod or SDK state** (PLAN). A modded
  game's retail export, if it is ever built, is refused when SDK extensions
  are active.

### Faithful and Uber

Both profiles run over the same objects and modules. Uber changes the order
of computation, not the data (ARCH-1 section 2). So:

- a save records its execution profile, and a multiplayer resume needs the
  same profile on every peer (it does today for any game);
- a Faithful save loaded in Uber, or the reverse, is possible in single
  player: the state blocks are the same. The game then continues under the
  other profile's rules. Uber's own caches (snapshot grids, flow fields) are
  rebuilt after load, never saved, so that the snapshot bytes don't depend on
  the thread count. Whether to offer cross-profile loads is decision D7.

### Backwards compatibility: saves from older OpenBFME builds

There are three mechanisms, in the order the loader tries them:

1. **Versioned blocks (retail's mechanism).** Every native `xfer` starts with
   retail's one-byte version: the earliest it can read and the current one
   (RW 0xA1F5B9). A change to a class's saved state raises `current` and adds
   the read branch for the old layout in that xfer. Raising `earliest` is a
   deliberate, documented break. This covers most engine changes.
2. **A migrated save plays on with the new rules.** The state loads, but the
   game continues with the current build's behaviour. That is what players
   expect from a patched game, and it is what retail's patch rule assumes.
   Hash continuity with the old build is not promised across builds.
3. **Replay with the version that made it.** When the snapshot is missing or
   too old, the log can still give the exact game, but only with the build
   that recorded it. The engine build id in the header names that build. The
   launcher's release updater can fetch that release's headless peer (all
   releases are kept on GitHub). It catches up from the log and writes a
   native snapshot at F, and the current build then migrates that snapshot
   (1). This works for every build that has snapshots, so it only helps from
   SAVE-4 on. Saves made by builds before that are log-only and need the old
   build to load. Whether to build this fetch is decision D8: it is real
   plumbing.

Retail save import (section 3) is the fourth, separate mode. Its versions are
retail's own, so a retail save is a migration source like any old version.

## 6. Determinism: a loaded game must equal the saved one

**The rule.** Anything that a later frame reads and that can't be recomputed
bit-exactly from the saved state must be saved. That covers the frame
counter, the object-ID counter, both RNG generators' states, the scheduler's
sleep vectors, every module's timers, the AI and pathfinder queues, partition
and shroud cells, script engine state (flags, counters, timers, completed
scripts, sequential scripts), teams, players, money and command points. Caches
that are recomputed deterministically may be rebuilt in `loadPostProcess`
instead, and each one must be named. Pointers are saved as IDs and resolved
after load, as in retail. Floats are saved as their bit patterns, containers
in their defined order, never by pointer or hash order. These are the rules
the state hash already follows.

**The tests** (each pins an effect in the world, by name, with no
thresholds; the testing standard):

- **T1, continuation.** A recorded game on a retail map (4 AI, plus the
  existing QA scenarios by name: mid-combat, mid-construction, a special
  power in flight, a script waiting on a timer, a horde mid-march) is saved
  at frame F, loaded in a fresh process and run on with the same commands to
  F+3000. **Every frame's state hash equals the uninterrupted run's**, which
  is the replay's recorded hashes. This catches state that is hashed but not
  saved (the hash at F differs) and state that is used but neither hashed nor
  saved (a later frame differs). The first differing frame and subsystem come
  from the existing desync breakdown.
- **T2, idempotence.** `save(load(save(F)))` is byte-identical to `save(F)`.
- **T3, cross-OS.** For the same game and frame, the snapshot bytes are
  identical on Linux/GCC and Windows/MSVC. A Linux save loads on Windows and
  passes T1, and the reverse. This runs on the CI hosts (the Windows build
  plus the Linux suite).
- **T4, coverage inventory.** The exact list of registered snapshot blocks,
  and of module classes with a runtime, each with an `xfer`, checked by name.
  A class with a `crc` and no `xfer` fails the test with its name, or is a
  named stop.
- **T5, failure modes.** A truncated file, a bad digest, an unknown block, a
  version above `current` or below `earliest`, a different identity, a
  missing mod: each one gives its named error on screen and in the report,
  and the test asserts the text.
- **T6, multiplayer.** Every peer's digest at F is equal. A resumed game's
  first CRC exchange passes, and a snapshot rejoin passes the next one. These
  run in the existing online harnesses.
- **T7, migration.** Saves made by each released build that has snapshots
  are kept on the CI hosts (never in git: they embed map data). Each one
  loads in the current build and runs 3,000 frames on two machines with
  equal hashes. Continuity with the old build is not asserted (section 5).
- **T8, Uber.** A save taken under Uber at 1, 4 and 16 threads is
  byte-identical (UBER-1's rule, applied to snapshots).

## 7. Phases

Sizes are in lanes (one implementation pass plus review), as in the ROADMAP.

| Phase | Content | Size | Depends on |
|---|---|---|---|
| **SAVE-2** | **B for single player.** The container with header and log only (the running replay up to F, with the identity and the build id). Load means replay playback to F at full speed, then live control. SaveLoad.apt's Save and Load pages for skirmish (and campaign missions, S-1360). Same build only: a different build is a named refusal. Tests: T1 (the log variant), T5. | 1 | nothing new (the replay recorder exists) |
| **SAVE-A0** (in parallel) | **The retail inventory.** A read-only retail save reader (`tools/save/`, Python): the container, blocks, versions and sizes. The table of every RotWK `xfer` with its RW address, its (earliest, current) version constants and its tier, and every class's field order where the decomp has it. Questions settled: Object::xfer's module skip, GameStateMap's embedded map, the 'DSCR' marker, CHUNK_VENDOR, the meaning of modes 0-6, whether a multiplayer skirmish can be saved. Needs sample retail saves (the owner's own, or made on JonathanPC under the retail-run rules). | 1 | owner: sample saves (D9) |
| **SAVE-3** | **Multiplayer save and resume with the log.** The save request, confirmation and command (retail's kinds 1, 2 and 5, all humans), each peer's container, the relay's save point and stored log, the lobby's resume entry, and the resume START with a hash check at F. LAN and online. Tests: T6 (log variant), the e2e online scripts. | 1-2 | SAVE-2, ONLINE-2/3 merged |
| **SAVE-4** | **The Xfer framework and the native snapshot**, by class group: (a) the framework, GameState block lists, post-process, container part, GameLogic core, players, teams, RNG; (b) objects and the 137 module runtimes, in retail field order where they are 1:1 ports; (c) AI, pathfinder, partition, shroud; (d) script engine, victory, weather and the other subsystems. Tests T1-T5 and T8 grow with each group, and T4 is the gate. | 4-8 | SAVE-2; numeric facade unchanged |
| **SAVE-5** | **Snapshots in multiplayer:** the snapshot digest at the save point, snapshot fetch on resume, and periodic snapshots for fast rejoin and late spectators (O4). | 1-2 | SAVE-3, SAVE-4 |
| **SAVE-6** | **The version discipline:** the `current` bump rule in review, T7's archive of release saves on the CI hosts, and the old-build catch-up converter (if D8 says yes). | 1 | SAVE-4, first release with snapshots |
| **SAVE-7** | **Retail import:** retail-dialect reads for every block the inventory lists, the skip report for unported state, and campaign and skirmish first (WotR when it exists). | 3-6 | SAVE-A0, SAVE-4, the logic port's coverage |
| **SAVE-8** | **Retail export (optional):** retail-dialect writes, checked by retail loading our files on Windows. | 2-4 | SAVE-7, D4, the owner's desktop |
| (SDK-2) | Mod and SDK state in saves, per namespace (ARCH-1's SDK-2 owns it) | +0.5 | SAVE-4 |

Recommended order: **SAVE-2 and SAVE-A0 now**, side by side. SAVE-2 gives
players saves this month and costs about one lane. SAVE-A0 is research only.
Then SAVE-3 once the online lanes are merged, then SAVE-4. SAVE-4 is the big
one, and everything long-term (fast rejoin, migrations, mods, retail import)
waits on it. Retail import and export come after the logic port covers the
modules they need.

**Limits of the log-only phases (2 and 3):** a load takes as long as the
catch-up (about 1.8 minutes for a 60-minute game on a fast PC, longer on a
Deck), and a save stops loading as soon as the build changes. That is
acceptable for a first release when the load screen says so. It is not
acceptable as the final answer, which is why SAVE-4 follows.

## Decisions for the owner

1. **Order.** Ship the log-only save (SAVE-2) first, with the retail inventory
   (SAVE-A0) in parallel, then multiplayer resume, then the Xfer snapshot
   (SAVE-4, 4-8 lanes)? Or go straight to the snapshot?
2. **Log-only saves break on update.** Is it acceptable that saves made before
   SAVE-4 refuse to load in a newer build (with a message naming the build
   that made them)?
3. **Native layout.** Write each 1:1-ported class's xfer in retail's field
   order and version numbers from the start (recommended: retail import
   becomes cheaper), or use a free OpenBFME layout and translate later?
4. **Retail compatibility scope.** Import retail saves (recommended,
   SAVE-7), export to retail as well (SAVE-8, needs your desktop for retail
   runs), or neither?
5. **Multiplayer resume with a missing player.** Require every original
   player (retail's rule, recommended for now), or allow an AI to take the
   slot?
6. **The relay keeps saved games.** How long (for example 30 days), and is
   a snapshot upload by the packet router acceptable?
7. **Cross-profile loads.** Allow loading a Faithful save in Uber, and the
   reverse, in single player?
8. **Old-build catch-up.** Build the launcher path that fetches an old
   release's headless peer to convert a log-only or too-old save, or simply
   refuse those saves?
9. **Sample retail saves for SAVE-A0.** May we use saves from your own RotWK
   `Save` folder (kept off git, on the Deck and CI hosts only), or should a
   lane make fresh ones on JonathanPC under the retail-run rules?
10. **File naming.** Use our own extension (`.obfmesave`, recommended, so
    retail never lists a file it can't read) in the same `Save` folder
    layout, with retail files shown as "import" entries on the Load page?

## 8. What is built

### SAVE-2: the command-log save for single player (lane SAVE-2)

- **The container** (`Common/System/SaveGame/SaveGameFile.h`): `.obfmesave`,
  magic `OBFMESAV`, container version 1, three parts in order (`HEAD`, `LOG `,
  `SNAP`), each with its length and SHA-256. The header holds the build
  version, the engine id, the commit, the profile identity text, the execution
  profile, the game type (1 Campaign, 2 Skirmish), F, the state hash at F, the
  name, the map, the date and time, the command count, for a campaign
  mission its campaign, mission and difficulty, and the execution profile
  switches (below). The log is the
  running replay (format 4) up to F, ending with the end record (F, hash). The
  snapshot part is empty, and a non-empty one is refused until SAVE-4.
- **Saving reuses the recorder.** Every skirmish's `ReplayWriter` keeps what
  it wrote in memory, and `logUpTo(F, hash)` returns that log with the end
  record. It refuses, with a named reason, a recording that lost a command or
  failed to write, a batch at or after F, or a missing hash of F
  (`GameWorld.save_game`).
- **Loading** (`GameClient/CommandLogLoad.h`, `GameWorld.check_save` /
  `prepare_save_load` / `start_save_load` / `save_load_step`): the header is
  checked against this build (case 2 of section 2). The log's game is started
  like a replay. `ReplayPlayback` runs the log to F at full speed on the main
  thread, comparing every recorded hash, with no game view: the load screen
  stays, under a progress box that says it is catching up. The state hash at F
  must equal the header's. The game is then handed to the player with a
  recording that continues the saved log (`ReplayWriter::openContinuing`), so
  a later save, and the game's replay, hold the whole game.
- **Bounded reads:** a file is read part by part; every part's declared size
  is checked against the file's real size and the part's limit (header 1 MiB,
  log 256 MiB) before anything is allocated, and the Load page's listing reads
  only the header part and the parts' heads.
- **Every frame's hash:** a log must hold the state hash of every frame from
  the loaded game's first frame to F, once each, and nothing after F (replay
  format 6 also refuses a second command or hash record of a frame).
- **The recording goes on or the load fails:** if the continued recording
  cannot be opened, the load fails with that error instead of letting the game
  go on unrecorded (`CommandLogLoad::handOver`).
- **Refusals** are named and tested (T5): not a save, truncated anywhere, a
  part's digest, another container or header version, a part out of order,
  bytes after the end, a snapshot, an unknown game type, another build ("this
  game was saved by OpenBFME <version> (engine <id>) ..."), other data (the
  differing identity lines), a log that ends elsewhere than its header says,
  a log of another identity, a log without its end, and a log that does not
  replay the same (its first differing frame).
- **Faithful and Uber** (decision 7, with UBER-1 merged): a save of the other
  execution profile loads in single player. Its log is replayed under the
  profiles it ran, with every hash compared, then the game switches to the
  player's profile at the saved frame (`GameLogic::setSimulationProfile`). The
  switch is a record of the continued replay itself (replay format 6), so that
  replay and every later save replay exactly too.
  Multiplayer refuses such a save (S-4001).
- **Screens:** SaveLoad.apt's Campaign and Skirmish pages through the retail
  movie (AptSaveLoad, see its header for the RW addresses): the quit menu's
  Save and Load, the main menu's Load Game, the name entry with
  GUI:DefaultSaveFileName, the new-save row, overwrite and delete
  confirmations and the load-over-a-game question through the movie's
  showMessageBox, the result box, and refusals under GUI:ErrorLoadingGame.
  Retail saves in the Save folder are grey import entries (S-4005).
- **Files:** the retail Save folder (the user data folder's `Save\`, where
  Create-a-Hero keeps its heroes); `--save-dir=DIR` overrides it.
- **Tests:** `test_save2.cpp` (SHA-256, the container, T5 with the bounded
  reads, the load rules, the recorder hooks, the hash coverage, the profile
  switch record, the stops), `test_save2_retail.cpp` (T1 as section 6 asks: a
  human and four Medium AIs on Fall Back 8p, saved at the first frame from
  start + 600 where every named scenario is in flight at once (a unit
  attacking, a structure being built, a special ability running, a horde
  moving, a script countdown timer; the campaign test's mission is saved with
  a script timer running), loaded in a fresh process (`openbfme_peer
  --load-save`) and played on to F + 3000 with the uninterrupted game's
  commands, every hash equal; the in-process T1 with live control after the
  load; T5 on the log; the cross-profile loads; the campaign mission),
  `test_save2_menu.cpp` (the pages through the retail movie), and
  `godot -- --auto --save-test` (save in a game, load it back through the
  screens, the game at F).
- **Campaign missions** (S-4002): a mission is recorded in memory only (the
  replay format starts a game from a NewGameMessage, a mission from its
  campaign); its save's header carries the campaign, mission and difficulty,
  which start it again before the catch-up, and the campaign flow goes on from
  there. Saving between missions (CampaignMenu.apt, S-1360) is not built.
- **Stops:** S-4000 to S-4005 (`docs/STOPS.md`).
- **For ONLINE-3:** nothing was copied from its `CommandLogCatchUp`. A save's
  catch-up drives the archive's `ReplayPlayback`, so a rejoin or a resume from
  a saved log needs only the relay log assembled into a `ReplayFile`; its
  `rejoinStep` loop (`advance(0.2)` within a time budget) is the same shape as
  `CommandLogLoad::step`.

