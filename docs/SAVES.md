# Saves: retail's snapshot, a command-log save, and how they fit

Lane SAVE-1, 2026-10-12. The owner accepted every recommendation the same
day. **Current implementation:** single-player saves write a command log and
a native snapshot (SAVE-4e); campaign-menu saves are also built (CAMPSAVE-1).
Game-at-frame saves still require the same engine compatibility id, even with
a snapshot. Multiplayer saves and retail import are not built. See section 8,
"What is built".

The owner's direction: "for saves we probably need to support both ideally for
a faithful port but we might want to design it in a way for the sdk to work
maybe a backwards compatibility mode". "Both" means:

- **A. Retail's save system, faithfully.** RotWK 2.01's Xfer snapshot of the
  whole game (GameState, XferSave / XferLoad, every class's `xfer`, `crc` and
  `loadPostProcess`), its file format and the Save / Load screens.
- **B. A command-log save.** The game's START plus every command so far.
  Loading replays the log at full speed. This reuses ONLINE-3's catch-up.

The accepted design in one paragraph: **one save container with three parts, a header,
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
the historical ARCH-1 design (lane/arch1, f4597acf; not included in this tree).
Current profile settings are in `docs/UBER.md`. RW addresses are
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
  Ring map save's load (types 4 and 6) it calls `InitRandom(0)` and suspends
  the logic (GameState::doLoadGame, the BFME2 decomp's byte-matched
  0x6DD91A; RotWK's is the same shape; WOTR2 milestone 3 does the reseed, see
  section 8). The mode 4 load reads list 0, the mode 6 one list 3, mode 1
  list 4. A failure shows "GUI:ErrorLoadingGame" and returns to the shell.
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

### Where our engine stood at SAVE-1 (2026-10-12)

These are the original pre-SAVE-2 observations, not the current implementation
status. Current saves and replay versions are described in section 8.

- (Before SAVE-2 / SAVE-4a.) There was no Xfer, no Snapshot interface and no save container: section 8 lists what is built now.
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
  and 60 minute games catch up in 9, 48 and 106 s on the reference PC.

## 2. The design: one container, three parts

Sections 2-7 preserve the accepted SAVE-1 design, proposed tests and phase
order. They are not a list of shipped features: multiplayer save/resume,
snapshot rejoin, cross-build game-state migration and SDK-state migration
remain planned. Section 8 records the implementation and its limits.

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
- **WotR saves**: OpenBFME's War of the Ring map saves exist (section 8,
  WOTR2 milestone 3); reading retail's `.BfME2WotR` would need retail's
  CHUNK_LivingWorldLogic object graph (RW 0x6BC653), which the strategic model
  does not keep in that shape.

**Writing (export) is harder, and only partly testable here.** Retail loads a
file only if every block it reads is byte-correct for its version, so export
needs the full retail state for every registered block, including the client
blocks retail expects. **(to verify)** Whether retail accepts a save with
client blocks left out. The only oracle is retail itself, loading our file on
Windows. That means the reference PC's desktop under the owner's retail-run rules
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
| **SAVE-A0** (in parallel) | **The retail inventory.** A read-only retail save reader (`tools/save/`, Python): the container, blocks, versions and sizes. The table of every RotWK `xfer` with its RW address, its (earliest, current) version constants and its tier, and every class's field order where the decomp has it. Questions settled: Object::xfer's module skip, GameStateMap's embedded map, the 'DSCR' marker, CHUNK_VENDOR, the meaning of modes 0-6, whether a multiplayer skirmish can be saved. Needs sample retail saves (the owner's own, or made on the reference PC under the retail-run rules). | 1 | owner: sample saves (D9) |
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
   lane make fresh ones on the reference PC under the retail-run rules?
10. **File naming.** Use our own extension (`.obfmesave`, recommended, so
    retail never lists a file it can't read) in the same `Save` folder
    layout, with retail files shown as "import" entries on the Load page?

## 8. What is built

The lane notes below are chronological. SAVE-4e supersedes SAVE-4a..d's
staged snapshot refusals and deferred integration checks; their measurements
and review counts describe those runs, not fixed counts for every build.

### SAVE-2: the command-log save for single player (lane SAVE-2)

- **The container** (`Common/System/SaveGame/SaveGameFile.h`): `.obfmesave`,
  magic `OBFMESAV`, container version 1, three parts in order (`HEAD`, `LOG `,
  `SNAP`), each with its length and SHA-256. The header holds the build
  version, the engine id, the commit, the profile identity text, the execution
  profile, the game type (1 Campaign, 2 Skirmish), F, the state hash at F, the
  name, the map, the date and time, the command count, for a campaign
  mission its campaign, mission and difficulty. Execution-profile switches
  are records in the log, not fields of the header. The log is the running
  replay (currently format 7; formats 5 and 6 still read) up to F, ending with
  the end record (F, hash). Since SAVE-4e the snapshot part holds the native
  snapshot at F by default; `snapshot: false` explicitly requests a log-only
  save for the catch-up tests. A refused snapshot fails the save.
- **The campaign menu save point** (lane CAMPSAVE-1, header version 2; version
  1 headers still load): after the campaign, mission and difficulty fields,
  `u32` save point (0 a game
  at F, 1 CampaignMenu.apt between missions) and four `u8` flags, each 0 or 1:
  won, ready, advanced (the campaign's + 0xD0 / + 0xD2 / + 0xD3,
  `GameClient/LinearCampaign.h`) and auto-save (retail's IsAutoSaveOrNot). A
  save point 1 file is a campaign (game kind 1) with no game: frame 0, no
  commands, empty `LOG` and `SNAP` parts; a game's save carries no flags. The
  campaign's auto-save after a won mission (RW 0x6DE8F1: "Auto Save for
  <map>", listed in SaveLoad.apt's AutoSaveList) and CampaignMenu.apt's Save
  write it (`GameWorld.save_campaign_menu`); loading one shows the menu with
  that state. Having no log to replay, it ignores the engine compatibility id,
  but still requires the same data/rules identity and a valid campaign,
  mission and difficulty (an update between missions can keep the campaign).
- **Saving reuses the recorder.** Every skirmish's `ReplayWriter` keeps what
  it wrote in memory, and `logUpTo(F, hash)` returns that log with the end
  record. It refuses, with a named reason, a recording that lost a command or
  failed to write, a batch at or after F, or a missing hash of F
  (`GameWorld.save_game`).
- **Loading** (`GameClient/CommandLogLoad.h`, `GameWorld.check_save` /
  `prepare_save_load` / `start_save_load` / `save_load_step`): a game-at-frame
  header is checked against this build. The log's game is started like a
  replay. With a snapshot, the state at F is restored directly (SAVE-4e,
  case 1 of section 2). Without one (case 2), `ReplayPlayback` runs the log
  to F at full speed on the main thread, comparing every recorded hash, with
  no game view; catch-up progress is drawn on the saved-game loading screen.
  The state hash at F must equal the header's. The game is then handed to the player with a
  recording that continues the saved log (`ReplayWriter::openContinuing`), so
  a later save, and the game's replay, hold the whole game.
- **Bounded reads:** a file is read part by part; every part's declared size
  is checked against the file's real size and the part's limit (header 1 MiB,
  log and snapshot 256 MiB each) before part payloads are allocated, and the Load page's listing reads
  only the header part and the parts' heads.
- **Every frame's hash:** a log must hold the state hash of every frame from
  the loaded game's first frame to F, once each, and nothing after F (replay
  format 6 also refuses a second command or hash record of a frame).
- **The recording goes on or the load fails:** if the continued recording
  cannot be opened, the load fails with that error instead of letting the game
  go on unrecorded (`CommandLogLoad::handOver`).
- **Refusals** are named and tested (T5): not a save, truncated anywhere, a
  part's digest, another container or header version, a part out of order,
  bytes after the end, a malformed snapshot, an unknown game type, another build ("this
  game was saved by OpenBFME <version> (engine <id>) ..."), other data (the
  differing identity lines), a log that ends elsewhere than its header says,
  a log of another identity, a log without its end, and a log that does not
  replay the same (its first differing frame).
- **Faithful and Uber** (decision 7, with UBER-1 merged): a save of the other
  execution profile loads in single player. Its snapshot restores the saved
  profile; on the log-only path its log is replayed under the profiles it ran,
  with every hash compared. Then the game switches to the
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
  Lane SAVE-UI1 (the owner's "stuck on this menu" report): the quit menu
  shows the in-game background and hides it when it closes (RW 0x92262A /
  0x921A96, the front-end one after Exit); the save page shows the front-end
  background only when none is up and hides what it showed (+ 0x2A4, RW
  0x818BC2 / 0x818501); Escape closes the page (RW 0x816627); the saved box's
  Ok closes the page and the quit menu (state 10, RW 0x816879 / 0x8184C0); the
  quit menu under a screen has its movie unloaded and loaded again as RotWK's
  window layout does (RW 0x812A37 / 0x812913, S-4980), so it comes back with
  every button; the menus take the keyboard before the game's hot keys, and
  what the game held when one opened (a key, a mouse button, a scroll) ends
  with it (S-4982). `test_saveui1.cpp`, `godot/tests/saveui1_test.gd`.
- **Files:** the retail Save folder (the user data folder's `Save\`, where
  Create-a-Hero keeps its heroes); the game argument `--save-dir=DIR` after
  Godot's `--` overrides it (see the developer command below).
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
  `godot --path godot -- --auto --save-test` (save in a game, load it back through the
  screens, the game at F).
- **Campaign missions** (S-4002): a mission is recorded in memory only (the
  replay format starts a game from a NewGameMessage, a mission from its
  campaign); its save's header carries the campaign, mission and difficulty,
  which start it again before the catch-up, and the campaign flow goes on from
  there. Saving between missions is built by CAMPSAVE-1's campaign-menu save
  point above; it is not a mission snapshot.
- **Stops:** S-4000 to S-4005 (`docs/STOPS.md`).
- **For ONLINE-3:** nothing was copied from its `CommandLogCatchUp`. A save's
  catch-up drives the archive's `ReplayPlayback`, so a rejoin or a resume from
  a saved log needs only the relay log assembled into a `ReplayFile`; its
  `rejoinStep` loop (`advance(0.2)` within a time budget) is the same shape as
  `CommandLogLoad::step`.

### WOTR2 milestone 3: the War of the Ring map's save

- **The file:** container version 2 adds a fourth part, `LWLD`, after `SNAP`
  (version 1 files, three parts, still load). A War of the Ring map save is
  game kind 8 at save point 2: retail's mode 4 (RW 0x6DCFDF: the strategic
  map up, single player). Its `campaign` field names the scenario
  (LivingWorldCampaign), its state hash is the strategic game's, and it has
  no frame, no log and no snapshot. `LWLD` holds two length-prefixed blobs:
  the strategic game (`LivingWorldLogic::saveState`, GameState's stream of
  the block `obfme:LivingWorld`), then the host's session (the lobby game
  the battles start from, each player's lobby slot, the battle count:
  Godot's `var_to_bytes`, read back with `bytes_to_var`, which refuses
  objects).
- **The block:** retail's CHUNK_LivingWorldLogic (RW 0x6BC653, the shared
  state RW 0x6BBD55, version 7) xfers its object graph (LivingWorldPlayer,
  ArmySummary, the regions, the beacons). OpenBFME's model keeps the same
  state as players, regions (buildings, queues), armies (members) and
  battles by index, so the block is native (`obfme:`). It follows retail's
  order where the model has the field. It holds everything the state hash
  covers, plus the event log. The data the world rebuilds (ranks, the RTS
  templates' rank fields, ResourceMultiplierLimit) comes from the running
  world at load. Every name and index is checked against the data, and a
  mismatch is a named refusal.
- **Loading:** like a campaign menu save, no log is replayed, so another build
  may load it when the data identity matches and the scenario exists
  (`GameWorld::wotrSaveVerdict`). The loaded state must hash as the header
  says. Then the generator starts again from 0 (retail's `InitRandom(0)`
  above): two loads of one save play the same war; the unsaved game's dice
  are not repeated, as in retail.
- **Screens:** Esc on the map opens QuitMenu.apt with retail's War of the Ring
  branch (+ 0x114 == 0): Save / Load open SaveLoad.apt's War of the Ring page
  (type 8, RW 0x92179F), whose rows are RW 0x6E04AD's for filter 8 (file
  name, blank, time, date; a map save gold). The main menu's Load Game
  offers the same page (flags 0xB).
- **Not built:** saving inside a War of the Ring battle (retail's mode 3:
  the battle's own save is refused by name), reading retail `.BfME2WotR`,
  the multiplayer War of the Ring save (mode 6). Stop S-1332.

### SAVE-4a: the Xfer framework, GameState and the core blocks (lane SAVE-4a)

- **The framework** (`Common/Xfer.h`, `XferSave.h`, `XferLoad.h`, `XferCRC.h`, `Snapshot.h`): one `xfer` per class
  saves, loads and checksums. Retail's byte layout: a version is one byte, checked on load against the class's
  (earliest, current) window with retail's two messages (RW 0xA1F5B9); a string is a u8 length or 0xFF + u32
  (RW 0xA1FB84); a block is "BLOK" + the absolute end offset (RW 0xA2111A / 0xA206EF), skipped by it (RW 0xA203D6);
  XferCRC is the RW 0xA211DF step per transfer. Untagged only. Every load read is bounded (a stream, a block, a
  string length, a list count) and every failure is a named `XferException`.
- **GameState** (`Common/GameState.h`): the block registry with retail's list numbering (0 save / load, 1 the deep
  logic CRC), the "ALAE" "2STR" stream with tokens and "SG_EOF" (RW 0xA20DD0 / 0x6DDF2F), the post-process pass in
  queue order (RW 0x6DD65C). **No partial snapshots:** every simulation subsystem declares its block with the lane
  that ports it and the state hash sections it holds; `GameLogic::saveSnapshot` is refused naming every block
  without an xfer and every hash section no block declares (`settings` is declared as rebuilt from the START and the
  data). A load refuses an unknown, missing or repeated block, a block read short or long, a version outside a
  class's window and trailing bytes (stricter than retail, S-4122).
- **The core blocks** (OpenBFME native layouts, S-4121; registered by `GameLogic/GameLogicSnapshot.cpp`):
  `obfme:TeamFactory` (the id counters; the prototypes, checked against the game's; every team in creation order,
  created with its saved id when the game lacks it; each team's state; the members as ids, linked again by the
  post-process, which requires each object's own team to agree), `obfme:Players` (each Player, its identity
  checked, with Money, ScoreKeeper, CommandPoints, PlayerScience, the hero list, upgrades by name, cost modifiers
  and discounts, the relation maps in retail's layout RW 0x6AE9B7 / 0x7A56B6, selection and control groups),
  `obfme:GameLogic` (frame, frame-advance switch, difficulty, game rules, Legendary Heroes, execution profile, next
  object id) and `obfme:Random` (the logic generator's seed array and base seed; another algorithm is refused).
- **The container's SNAP part** is read and digest-checked like the others (at most 256 MiB); a save that carries
  one is refused at load until the snapshot loads (`CommandLogLoad`). No build writes one before that.
- **Measured** (Deck, Release, `test_save4a_retail.cpp`): a 4-player skirmish (four Medium AIs on Fall Back 8p) at
  frame 3,000 with 891 objects: the core is 204,255 bytes (obfme:TeamFactory 5,743; obfme:Players 198,349;
  obfme:GameLogic 86; obfme:Random 54), written in 0.36 ms (best of 20). The players' size is almost all the score
  keepers' per-frame statistics (about 15 bytes per player per frame, which retail keeps too): it grows with the
  game's length (about 1.4 MB for a 60-minute 5-player game).
- **Tests:** `test_save4a.cpp` (the byte layouts, the version window, every load failure by its text, XferCRC, the
  stream and its post-process order, the registry as the exact inventory (T4), the core round trip on a synthetic
  game whose changes only the core holds, checked section by section, with the next random numbers and later
  frames equal (T1 as far as the core allows) and the same bytes saved again (T2), the refusals of another game's
  snapshot (T5), the same bytes and refusal under Uber at 1, 4 and 16 threads (T8 as far as the core allows), the
  container's part); `test_save4a_retail.cpp` (T4 on a real 4-player skirmish: the exact refusal, every hash section
  declared, the pathfinder and shroud included; T2 on the real game at frame 3,000: the core loaded into a second
  identical game keeps its state hash and its 891 team members, and saves the same bytes; the measurement).
- **Stops:** S-4120 .. S-4122.

### SAVE-4b: the objects, their modules, the scheduler and the object-side subsystems (lane SAVE-4b)

- **obfme:Objects** (`GameLogic/GameLogicObjectSnapshot.cpp`, `Object::xfer`): a table of every object in list order (id, template,
  team, status), then each object's block. A load removes the game's objects, makes every saved object with its id, team and status
  (its create modules and initObject run, ZH GameLogic::xfer's way, without the BuildVariations draw) before any of them loads; then
  each object loads, its ids looked up in the saved table only, its containment links (container, riders, members, crew) staged; the
  objects the creations made on their own are removed; both sides of every staged link are checked, then the links are installed;
  and the core blocks are loaded again from a copy (a creation changes its player and draws the logic RNG), keeping the teams' member
  ids for TeamFactory's post-process (S-4191).
- **Object::xfer**: the object's fields (the crc's and what it leaves out), its ExperienceTracker, its ObjectWeapons (the weapon set
  the flags select, each Weapon, the FiringTracker), then retail's module loop (RW 0x697F28: a u16 count, per module its tag and a
  block). The modules must be the object's own, in order (strict where retail skips).
- **Every module runtime class** outside the AI update family has an xfer of its own, declared with `OBFME_MODULE_XFER`; Object::xfer
  refuses an instance whose class has none or would inherit its base's, naming the class. The base chain is retail's framing
  (ObjectModule RW 0x7082E2, BehaviorModule RW 0x8CD4AA, UpdateModule RW 0x850C60, UpgradeMux RW 0x8D27E8, UpgradeModule
  RW 0x8D284A); TunnelTracker (RW 0x8FA569), DamageFilteredCreateObjectDie (RW 0x88927E) and DeletionUpdate (RW 0x88B863) keep retail's
  layout; the other classes are native (S-4190). The AI update family (AIUpdateInterface and its subclasses) is SAVE-4c's (S-4193).
- **obfme:UpdateScheduler**: the four phase vectors and the sleeping vector in order, each entry an (object id, module index) with its
  next call frame, and the destroy list. **obfme:TunnelTracker** (every player's network in retail's layout), **obfme:Combat**,
  **obfme:SellList**, **obfme:Experience** (the delayed level grants, the level by name), **obfme:Invisibility** (each entry with the
  copies of its nuggets, as retail's list nodes hold them) and **obfme:DamageOverTime**.
- **Rebuilt, named** (S-4192): the per-template caches, the target finder's cell index, the KindOf masks and module interface pointers,
  the report statistics; the client is not told the loaded model conditions (the step that turns the snapshot on in the game must
  resynchronise the drawables).
- **Measured** (a CI host, Release, `test_save4b_retail.cpp`): the 4-AI Fall Back game (seed 4190) at frame 3,000 with 713 objects: SAVE-4b's
  blocks are 2,061,840 bytes (obfme:Objects 1,954,492; obfme:UpdateScheduler 99,103; obfme:Invisibility 7,772; the others below 400),
  written in 3.7 ms (best of 20), without the AI update family's modules. About 2.7 KB per object: the fixed-size masks (model
  conditions, upgrades, status) and the module records dominate.
- **Tests:** `test_save4b.cpp` (T4: every one of the registry's 149 ported behaviour classes on an object of its own saves, loads into
  another game and saves the same bytes; T1 / T2 on a synthetic game whose objects were made, changed, destroyed and made again, loaded
  into the same START's game holding other objects, then both run on equal; each subsystem block with non-default values; T5: the
  version window, ids, a missing template, another module list, a class without its own xfer, a tunnel nemesis; T8 at 1, 4 and 16
  threads; the stops), `test_save4b_retail.cpp` (the 4-AI game at frame 3,000 loaded into the same START's game at frame 2,000: 129
  objects made again and 40 removed, every object hash (without the AI update family's modules) and SAVE-4b's sections equal, the bytes
  saved again equal; T4 on the real game: the exact refusal (12 blocks of SAVE-4c and 4d), the exact inventory of its module classes;
  the measurement).
- **Stops:** S-4190 .. S-4193.
- **Not done here, T3** (the same snapshot bytes on Linux / GCC and Windows / MSVC, and a Linux save loading on Windows): deferred to
  SAVE-4e (the coordinator, 2026-10-13: the reference PC is paused, the Windows tests cannot run natively now; a cross-compile alone does not
  establish it).
- **For SAVE-4c:** the AI update family's classes declare their xfers with `OBFME_MODULE_XFER(Class)` (Common/Module.h) and start
  with `xferUpdateModule(x)` (the scheduler block restores their vector positions); `Object::xfer` then saves them inside each
  object's module loop, and the tests' `setSnapshotModuleFilter(isAIUpdateFamily)` goes (test_save4b_retail.cpp).
### SAVE-4d: the script engine, victory, weather and the other subsystem blocks (lane SAVE-4d)

- **The blocks** (`GameLogic/GameLogicSnapshot4d.cpp`, in the registry's order). They follow the rules for xfer authors below: every container size
  goes through `xferCount` / `xferVectorSize` with its entries' minimum encoded size; a load reads into temporaries, checks every value against its
  closed set (the weather 0 .. 4, the affect kind, the victory rule, a cell state, a record version, the gate's two states, ...) and every name and
  index against this game's (players, sides, scripts, team prototypes, templates, ModifierLists, the resource grid and its blocked cells), and only
  then changes the subsystem; obfme:ScriptEngine and obfme:Economy stage what they loaded and take it in their post-process, after every block
  loaded, once the ids they keep are checked (the sequential scripts' teams, the claimants' objects), so a refusal there leaves them unchanged. The
  script tree is checked script by script and group by group by name (the same counts in another order are another tree); the resource cells'
  active entries (a SHARED cell's, an OWNED cell's owner) are tombstones or claimants with a player of this game, an owned cell's owner complete
  (a FREE cell's stale entries are kept as they are); a tree's bucket link is the START's; the hero dirty flags lie in 0x2FF; a queued science is
  one of the data's:
  - `obfme:ScriptEngine` (`ScriptEngine/ScriptEngineXfer.cpp`, lists 0 and 1): the tree's runtime flags through retail's Script (RW 0x7B3063, (1, 2))
    and ScriptGroup (RW 0x7B2F8A, (1, 1)) records, the sides and every list's counts checked; the generic script copies; the counters and flags; the
    object type lists; the named object cache (ids); the sequential scripts in retail's record (RW 0x60331E, (1, 1)), found again by side and name; the
    acquired-science queues' cursors; HAS_FINISHED_AUDIO's timers. Retail's own ScriptEngine::xfer (RW 0x60A45B, (1, 5)) is not identified (S-4256).
    The Lua state is not saved: retail saves none either (S-4256).
  - `obfme:Victory`: native (retail does not save VictoryConditions, S-4254); the peer's own slot is not in the bytes.
  - `obfme:Weather`: retail's GlobalWeatherSystem order and window (RW 0x71A2E3, (1, 4); the AntiCategory mask by name, RW 0x71A1A7) without the
    client's audio handle, the filter in the port's layout, the burn decay in a trailer (S-4251).
  - `obfme:Economy`: the GameData settings' hash checked, the game context, the TerrainResourceManager in retail's DoXfer layout (RW 0x75C7AE, (1, 3))
    plus the complete flags retail drops (S-4253).
  - `obfme:CreateAHero`: retail's record (RW 0x80B4F5, (1, 8)) per player with the game's validity and flags, and the choices (S-4255).
  - `obfme:TerrainTrees`: the records changed since the START in retail's record layout (RW 0x680753, (1, 2)); the client's tree events are not
    saved (S-4252).
  - `obfme:LargeGroupAudio`: the gate, + 0x38 and + 0x3C (S-4250).
- **Framework addition:** `Xfer::xferUnicodeString` (RW 0xA1FC7E: the AsciiString's length prefix counting UTF-16 units, the units in one transfer).
- **Measured** (Deck, Release, `test_save4d_retail.cpp`, on the tree with archive 70281823 merged): four Medium AIs on Fall Back 8p at frame 3,000
  (703 objects, 56 resource claimants, 486 trees): the seven blocks are 817,901 bytes (obfme:ScriptEngine 175,996, of which about 132,000 are the
  names of every script and group of the sides' trees and AI libraries, checked at load; obfme:Victory 146; obfme:Weather 60; obfme:Economy 641,557;
  obfme:CreateAHero 35; obfme:TerrainTrees 48; obfme:LargeGroupAudio 36; each with its token and block header), written in 1.07 ms (best of 20). The economy's size is the resource grid in retail's layout (every cell, at least
  8 bytes): almost all of it is the same at every frame of a map.
- **Tests:** `test_save4d.cpp` (the wide string's layout and CRC transfer; per block a game with non-default state saved and loaded into a new game of
  the same START: its state hash section takes the saved value, the fields read back, the same bytes are saved again (T2); retail's byte layouts of the
  weather, the resource manager and the hero record; T5 refusals by text: an unknown AntiCategory, a version above / below the window, a player the
  game lacks, other GameData, other map scripts, a game without scripts; the record windows; T4: the registry's SAVE-4d blocks and the refusal of the
  13 left; T8 at 1, 4 and 16 threads under Uber; the hostile streams: an oversized count (victory events, resource cells, script counters), a
  player cached twice, a claimant listed twice or whose object does not exist, a hero's player listed twice or absent, an out-of-range rule,
  weather, affect kind, cell state, record version and gate, a moved blocked cell, a sequential script of a team and a unit, a team that does not
  exist: each refused by its text with the block's state unchanged; review r1 (Sol): the script tree in another order, a cell naming a player or
  object it may not, an incomplete owner, a staged claimant or team that does not exist (the subsystem unchanged afterwards), hero dirty flags beyond
  0x2FF, a science the data lacks, a hook count other than 32, the counts of every remaining container (generic prototypes and hooks, flags, object
  lists and their types, named objects, sequential chains and records, a queue's sciences, audio timers, claimants, a cell's entries, heroes, bling,
  choices, changed trees); the round trips of a non-default science queue and of generic script copies),
  `test_save4d_retail.cpp` also: changed tree records of version 1 load, a self-link and an oversized count are refused, `test_save4d_retail.cpp` (the real skirmish at frame 3,000: game B's script engine, victory, weather,
  economy and audio gate changed, A's blocks loaded: those sections equal A's, the other sections unchanged, the whole hash equal and 100 more frames
  equal (T1 for these blocks), the same bytes again (T2); a real tree toppled and one harvested load, a second load refused; the measurement).
- **Stops:** S-4250 .. S-4256.
- **Not verified (deferred to SAVE-4e):** T3, the same bytes on Linux / GCC and Windows / MSVC. The reference PC is paused and the Windows tests cannot run
  natively on the Deck or the CI VMs; the Windows build compiles the blocks (the gate's windows build), nothing compares their bytes yet.

### Rules for xfer authors (SAVE-4a review r1; SAVE-4b, 4c and 4d follow them)

1. **One function per class.** `void xfer(Xfer &x)` saves, loads and checksums. It starts with its own
   `XferVersion v{ earliest, current }; x.xferVersion(v);` and branches on `v.current` for older layouts. A 1:1 port of
   a retail class uses retail's field order and window (`docs/SAVES-RETAIL.md` section 6); anything else is a native
   block named `obfme:<Name>`, with a stop that says so (see S-4121).
2. **Never read a destination while loading.** The typed transfers already load into zeroed temporaries. Your own
   code does the same: build a save-side value from the member when `!x.isLoading()`, transfer it, then assign it.
   Never compute the value to transfer from a member that the load is about to overwrite.
3. **Every container size goes through `x.xferVectorSize(v, minEntryBytes)`** (vectors) **or
   `x.xferCount(&n, minEntryBytes, entryMemory)`** (maps, sets, anything else; use `sizeof(value) + Xfer::kNodeBytes`
   for a tree node). `minEntryBytes` is the fewest bytes one entry encodes to: its fixed fields, with every string and
   nested container empty (a string is 1 byte, a count 4, a version 1). Never allocate from a raw count. The load then
   refuses a count that its block cannot hold, and keeps the encoded and allocation budgets across containers.
4. **Validate before you link.** A post-process (`GameState::addPostProcessSnapshot`, run after every block has
   loaded) first checks every saved reference: the id exists, it is listed once, and the object agrees on its side of
   the link. Only then does it change anything (`TeamFactory::loadPostProcess` is the pattern). An invalid stream must
   leave no half-linked lists behind.
5. **Validate every loaded value with a closed set.** That covers enums, flags with meaning and identities that the
   START made (names, template names, counts). The check throws an `XferException` with a message naming the value.
   Nothing is clamped or defaulted.
6. **Declare what you hold.** Each block lists the state hash sections it saves, in the list it is registered in
   (coverage is checked per list, on save and on load). State rebuilt from the START or the data is
   `declareRebuiltSection` with the reason. Turning a declared block into a real one shrinks the T4 refusal test by
   exactly its token.
7. **Test it like the core.** Cover a round trip whose changed state only your blocks hold, checked section by
   section, then the same bytes saved again. Add the refusals of a foreign stream by their text, and a malformed count
   for every container whose entries are large.

### SAVE-4c: the AI, pathfinder, partition, emotion, shroud and skirmish AI blocks (lane SAVE-4c)

- **Five blocks with their xfer** (OpenBFME native layouts, decision D3: none of these classes is a 1:1 port; retail's counterparts and windows are in
  S-4220), registered in `GameLogic/GameLogicSnapshot.cpp` in place of their declarations:
  - `obfme:Partition` (lists 0 and 1; `PartitionManager::xfer`): the region and scale, the entry list (newest first), the dirty list and every node's count
    and list **in their order** (re-inserting would reverse equal-cell orders, which decide every range query's hits). A load reads into a pending record;
    the post-process (every block loaded, the objects there) makes the entries again and links each to its object by id.
  - `obfme:Emotions` (list 0; `EmotionWorld::xfer`): the SCARY / HERO list in its order (RW 0x835B0A moves the last id into a removed one's place) and the
    report's counters; the post-process checks every id. The per-object nuggets are the EmotionTrackerUpdate module's (SAVE-4b).
  - `obfme:AIWorld` (list 0; `AIWorld::xfer`): the AI modules' object ids first (another set is refused before anything changes), the pathfinder
    (`Pathfinder::xfer`: the configuration and extents as a hash the game's must match, the cells' class words and zones in runs, every cell info with its
    obstacle and its occupant chains **in order**, the zone manager, both request rings, every unit's goal and position slots), every pathfinder view's
    layer, then **every AI update module in object id order, each in a BLOK** (`AIUpdateInterface::xferAIState`, with HordeAIUpdate's and DozerAIUpdate's
    own fields): the module's fields, the turret (TurretAI with its machine), the mover (AIMover with its path, `Path::xfer`: the nodes and their optimised
    links as indices), the locomotor set made again from its id and each locomotor, and the state machine with **every defined state's xfer** (retail keeps
    the state objects; `AIState::xfer` is pure, so every state class says what it keeps). Sub machines (an attack's, a hunt's, a turret's, PLAY-4's
    guard machine and its attacks) are made again by their kind (the attack's generic, melee or horde melee / ranged machine) and loaded the same way; last, since version 2, the next formation id (lane BACKLOG-8; a module's formation march since its version 2). The block holds the "objects" section's AI part
    as well as "pathfinder and AI movers": obfme:Objects (SAVE-4b) carries the AI modules as their objects made them, and this block fills them.
  - `obfme:Shroud` (lists 0 and 1; `ShroudManager::xfer`): the grid and settings, per player and channel a run-length plane of the cells, the unlook queue
    in order, the records by slot and the free slot stack, registration and dirty lists; the cells' covering lists are rebuilt from the records.
  - `obfme:SkirmishAI` (lists 0 and 1; `SkirmishAIManager::xfer`): the data's identity (SkirmishAIStore::crc, another data set is refused), the player info
    records, every build request **once** in a table the lists refer to by index (the farm site pool, the base slots, the queues and the builders share
    requests), the groups with every AI's builders and brain, the errors and counters. Data pointers are found again: the army by its side, a base's
    template by its index among its side's templates.
- **The rules for xfer authors** (above; SAVE-4a review r1, merged at b40daf2e) hold for all five blocks: every container size goes through
  `xferVectorSize` / `xferCount` with its fewest encoded bytes; a load reads into temporaries (a pending record for the partition and the emotions, a
  fresh manager for the shroud and the skirmish AI, decoded cells, infos, zones, rings and reservations for the pathfinder) and changes nothing until
  every reference is checked: objects of the loaded game (entries, records, views, obstacles, occupants, reservations that hold a slot), each named
  once, links that agree (an entry in exactly the node its cells give, node counts, the shroud's slot lists, the skirmish AI's players, groups,
  enemies and tactic targets); every enum-like value against its closed set (layers, cell types, statuses, goal types, command sources, attitudes,
  guard modes, dozer tasks and states, turret targets, AITARGETs, tactic prototypes, request states, phases). Sub machines, moves, paths and locomotor
  sets are made and loaded complete before they are attached. The AI update modules are filled in place (their objects own them), so
  obfme:AIWorld and each module's `xferAIState` load as one **transaction** (`AIXfer::transaction`, review r2): before anything changes, the
  current state is saved by the same xfer; a refusal anywhere (the last module included) loads that capture back and then raises the named error,
  so the live game's state hash is unchanged after any refusal (tested). Object ids an AI keeps and looks up when it uses them (victims, goals,
  requesters, a build request's producer) may name units that died since, so their closed set is the ids the game has issued (`AIXfer::issued`:
  below the next object id). A path's optimised links run forward only (a cycle could not be walked out of). The grid sizes (the pathfinder's
  cells, the shroud's cells) are the map's: a snapshot of another grid is refused, never allocated. The tests run UBSan-clean.
- **Review r3** (the four classes, audited over every block): (a) a block that commits inside its xfer checks that it was read to its end before the
  commit (`AIXfer::blockReadToEnd`, inside obfme:AIWorld's transaction and before obfme:SkirmishAI's commit; the partition, emotions and shroud commit
  in their post-process, after GameState's own check); (b) a pathfinder view's presence is state: a load makes the views exactly the saved ones, so a
  refused load leaves the next save's bytes as they were (the tests compare save bytes, not only hashes, around every refusal); (c) a live object an AI
  acts through (unit and dozer factories, free dozers, tactic team members, dispatched units, the info records' objects) must be its player's, a
  factory that changes owner leaves the old owner's unit builder (RW 0x8F0C69 -> 0x9A051D), and every pathfinder occupant must be a reservation its
  unit holds; (d) every member of every saved class is in its xfer or on this list of what is left out on purpose: references and owners (hosts,
  worlds, the pathfinder, the store), the template's and module data's settings (combat settings, turret data, dozer rates, the bird's data, movement
  infos, the pathfinder's configuration and extents, which a load checks by hash), caches (partition geometry, zone tables, the cells' covering lists,
  the unit index), a search's scratch (the open and closed lists, an info's costs and parent, the shortcut counter, the hierarchical corridor, the
  search memo), per-frame scratch (the collision arrays, the move-allies depth, the partition's walk switch), diagnostics (peaks, the collision pair
  count, the event trace), test switches, and the client and audio side (the shroud's local player, callback, levels and display switch, an AI's move
  loop flag). The audit found the flight locomotor's four fields (now saved) and nothing else unsaved.
- **Review r4** (a fifth class: a legal game producing a snapshot the load refuses): every ownership or range check of rounds 2 to 4 was set against
  the events that change its field (a capture or other change of owner, a death, a player leaving, a team merge). A change of owner runs the old
  owner's object-left path (`Object::joinTeam`: RW 0x6A99CB, then RW 0x6A9D9C), and the old owner's AI now lets the object go as retail does: the unit
  builder erases its factory entries and computes the composition again (RW 0x9A051D, + 0x2C at RW 0x9A055D) (since r5 in the leave event itself, and a barracks
  captured back registers again in its add event, RW 0x8F06BF -> 0x9A0838), and drops it from its free dozers and tactic teams; the dozer
  factories and the info records already followed it (RW 0x8F0C47, RW 0x8E480C). A dispatched unit is checked as an issued id only (the dispatch
  history orders nothing). The emotion list's KindOf check holds (the list is added to and taken from by KindOf, RW 0x693E79 / 0x835B0A, and an object's template never changes). Deaths keep
  the S-416 stale ids (issued-id checks); no AI, group or team is ever removed (players leaving are disabled AIs), and the targets list never shrinks,
  so the target, hero, timer and group checks hold. This is a simulation change for an object changing owner.
- **Review r5:** the unit builder's registration is retail's event, not a scan: an FS_FACTORY enters the factory map in its object event (RW 0x6C7779
  -> 0x8F069A -> 0x9A0838: synchronously, with no other filter, whether unit building is disabled or not; a structure is listed from its placement,
  under construction, and the lookup RW 0x9A0705 skips it while its factory is disabled) and leaves it in its leave event, at its death or change of
  owner (RW 0x8F0C47 -> 0x9A051D, the composition computed again). The port's scan and its registration memory are gone. This changes the simulation
  of every skirmish (a factory is listed from its placement, in event order, and a dead one leaves at its removal). The audit adds a player defeated,
  surrendering or leaving: until its AI sees the defeat (its next update) the events clean its lists as for any death or owner change; after it the
  AI is shut down (RW 0x6C7981), gets no more object events and runs only its brain, so its builder lists (free dozers, dozer and unit factories)
  keep ids of objects gone or given away since, as retail's state does: they are checked as issued ids only, and as live objects of the player while
  the AI runs. Its tactic team members stay checked as live objects of the player (the brain still orders them; an owner change takes a member out of
  every AI's teams, a death leaves an issued id); the info records follow every event whatever the AI's state; no AI, group, target or hero list
  shrinks at a defeat, so the remaining checks hold.
- **Review r7** (the merge of the archive 593e83cd): two members the archive added to saved classes are saved: an AI module's kept state machine
  (RW AI + 0x38, lane BACKLOG-4: `beginDefectedStateMachine`, hashed: a present flag, then the machine saved like the live one, made and loaded complete
  before it replaces the kept one) and a base's fortress rebuild task (RW AIBase + 0x28, lane BACKLOG-6: a request reference by the block's request table,
  plus the task counter). What the archive added to the object (the recorded original team, the teleport gather) is obfme:Objects' (SAVE-4b). The
  coverage test's garrison failed on the merged tree because its keep stood on the neutral Amon Hen structures (placed 80 east of the first Men unit, whose
  place moved with the game's changed state at frame 3,000): the order was refused by RW 0x82CBD5's path test, so the rider went idle; the test now
  looks for open ground around that unit (reachable corners and entry point) and takes a rider the path test accepts.
- **A load never runs a state's exit:** a machine the load replaces (an attack's sub machine, a hunt's) goes under `AIStateMachine::LoadGuard`, which drops
  its states without their exits (they would change the object's model conditions and statuses the load is restoring).
- **Diagnostics are saved once:** the units' stop lines (a mover's, a locomotor's, an AI's notes) go through a string table of the block (each sentence
  once, then by index).
- **No simulation change** (but review r4's change of owner, above): no state hash input, update or draw changed (the attack machine's and the hunt machine's construction moved into functions
  of their own, in the same order; sim_audit ok).
- **Measured** (Deck, Release, `test_save4c_retail.cpp`, review r5 with archive 98750dad merged): the 4-AI game at frame 3,000 (818 objects, 558 AI
  modules): the five blocks are 1,362,387 bytes (obfme:Partition 24,846; obfme:Emotions 76; obfme:AIWorld 1,158,366; obfme:Shroud 155,075; obfme:SkirmishAI
  24,001), written in 9.3 ms (best of 20). Most of obfme:AIWorld is the AI modules: saved one by one, each with its own stop lines, they are 1,274,993 bytes (the
  block's string table writes each line once); the pathfinder's grid is 247,500 cells. A load also captures the block first (the transaction), about
  one more write.
- **Tests:** `test_save4c_retail.cpp` (T1 / T2: the five blocks of frame 3,000 load into an identical game whose blocks first held another state (an
  earlier frame's partition, AI world and skirmish AI (since r5 the target is the first frame from 3,000 on with such a frame among its 50
  before: 3,002, with 3,001's state, in the last run), 302 AI modules' own state of frame 2,700, a shrouded map, an empty emotion
  list: each of the six sections
  differs before the load), give every state hash section of the saved game, save the same bytes again and the same hash on every one of the next 300
  frames; T4: the refusal names exactly SAVE-4b's and 4d's 15 blocks; T5: each block's version window with retail's two messages, another moment's AI
  modules, a partition entry of a missing object, a synthetic game's absent AI world and shroud; T8: the same bytes under Uber at 1, 4 and 16 threads;
  the measurement; the hostile inputs: per block an oversized count, a dangling and a duplicate reference and an out-of-range value, each refused by
  its text with the blocks' sections unchanged; nested counts (a partition node's list, a shroud record's coverage, a skirmish record's army list)
  and the allocation budget; review r2's cases: a module refused late, a whole block whose last module is malformed, the skirmish AI's best target,
  waypoint steps, producers and assist timers, a path's backward and self links, each with the live hash unchanged; review r4's captures (a factory, a
  dispatched unit, a barracks captured back that queues units again) saved, loaded over an older skirmish AI and 150 frames equal to the uninterrupted game, the barracks making a GondorFighterHorde; review r5's (Sol's probes, kept): the registration in
  the event with unit building disabled and a named horde made, a defeat with the assets given to an ally, a surrender, a DominateEnemy conversion of a
  team member, each saved and loaded at frames 50 and 200; the coverage test: units
  attacking, moving, guarding, hunting and entering a garrison saved mid-order and compared unit by unit, the skirmish plans and timers AI by AI,
  the shroud player by player), `test_save4c.cpp` (the registry entries, the helpers' container layouts and their duplicate refusals,
  a synthetic game's blocks saved again byte for byte); the
  SAVE-4a pins (`test_save4a*.cpp`) and the save stop list (`test_save2.cpp`) follow.
- **Stops:** S-4220 .. S-4222. **Remaining refusal list:** empty since SAVE-4e merged SAVE-4b and 4d (see below).
- **For SAVE-4b:** Object::xfer must leave the AI update family's state to obfme:AIWorld (the modules exist and are filled by it, in object id order);
  their `UpdateModule` part (the next call frame, the phase) is the scheduler's.

### What SAVE-4b, 4c and 4d must cover (the registry's declared blocks)

Each lane gives its blocks an xfer (`GameState::provideSnapshot`, or replace the declaration in
`GameLogicSnapshot.cpp`), extends T1 / T2 / T4 / T5 to them, and keeps the refusal test exact (it shrinks by its
blocks). The three can run in parallel: their blocks do not overlap, and each block is tested on its own with
`GameState::saveBlocks` / `loadBlocks`. 1:1 retail ports transfer in retail's field order and versions (D3,
`docs/SAVES-RETAIL.md` section 6 lists every RotWK xfer with its window).

| Lane | Block | Holds (state hash section) | Notes |
|---|---|---|---|
| SAVE-4b | `obfme:Objects` | every object and its modules ("objects"; the object count of "frame, object ids, RNG") | the object list order and ids (`m_objectsById`), objects created after the START made again and the START's destroyed ones removed; each object's team set before the TeamFactory post-process runs (it checks it); modules keyed by tag inside a BLOK, as retail's Object::xfer (RW 0x697F28, (1, 29)) does; the module runtimes' xfers, in retail order where they are 1:1 ports (the AI update family is 4c's); the per-template caches (`m_templateInfo`, `m_crushInfo`) are rebuilt, named |
| SAVE-4b | `obfme:UpdateScheduler` | "scheduler" | the four phase vectors and the sleeping vector in their order, each module's next call frame (the order is state: it decides module order) |
| SAVE-4b | `obfme:TunnelTracker` | part of "objects" | Player + 0x308 (retail TunnelTracker::xfer RW 0x8FA569, (1, 1)): its tunnels and riders as ids |
| SAVE-4b | `obfme:Combat`, `obfme:SellList`, `obfme:Experience`, `obfme:Invisibility`, `obfme:DamageOverTime` | "combat", "sell list", "experience", "invisibility", "damage over time" | the invisibility manager's state changes every frame even without objects (seen by the core test) |
| SAVE-4c | `obfme:Partition` | "partition manager" | the trees' link order is state |
| SAVE-4c | `obfme:Emotions` | "emotion system" | the scary / hero list in order |
| SAVE-4c | `obfme:AIWorld` | "pathfinder and AI movers" | the pathfinder's queues and the AI movers; with the AIUpdate module family and its state machines (AIUpdateInterface::xfer RW 0x667416, StateMachine and the AI states, all inventoried) |
| SAVE-4c | `obfme:Shroud` | "shroud" | the shroud cells per player |
| SAVE-4c | `obfme:SkirmishAI` | "skirmish AI" | AIPlayer / AISkirmishPlayer (RW 0x8F5756 / 0x8F3F6C), the builders, tactics, target chooser, groups |
| SAVE-4d | `obfme:ScriptEngine` | "script engine" | flags, counters, timers, completed and sequential scripts, the acquired-science queues' cursors (the players' notice history is saved by obfme:Players); Lua client state is not saved |
| SAVE-4d | `obfme:Victory`, `obfme:Weather`, `obfme:Economy`, `obfme:CreateAHero`, `obfme:TerrainTrees`, `obfme:LargeGroupAudio` | "victory", "weather", "economy", "create-a-hero", "terrain trees", "large group audio" | the weather changes every frame (seen by the core test); the economy's settings part is configuration (declare it rebuilt), its context and terrain resource cells are state |

Every lane also checks the state its subsystems keep **outside** the hash that a later frame reads (SAVES.md
section 6's rule), for example GameLogic's AI command sink and pending producer. SAVE-4e turned
the snapshot on in the game: `GameWorld.save_game` writes it, `CommandLogLoad` (case 1 of section 2) starts the
log's game, loads the snapshot at F and checks the hash there; no log frame runs on this path. T1 in a fresh process
and T3 (Linux and Windows bytes equal) cover it.

### SAVE-4e: the snapshot turned on: saves load instantly (lane SAVE-4e)

- **Merged:** SAVE-4a (b40daf2e), 4b (6a97a143), 4c (15b58db5) and 4d (d1ed16d9). **The refusal list is empty:** every block of the registry has
  its xfer (`save4a: the game's block registry is the exact inventory ...` and the real games' T4 cases save a whole snapshot). The AI update family's
  classes got their module-list xfer (their versions and UpdateModule's part; the AI state stays in obfme:AIWorld, S-4193). A merge fix: OpenContain's
  xfer saves SHIP-1's two ship exit counters instead of the count they replaced.
- **Save** (`GameWorld.save_game`): the header, the log to F and the snapshot at F (`GameLogic::saveSnapshot`). A snapshot the game refuses fails
  the save with a named error; there is no log-only fallback or `snapshot_refused` success result. Option `snapshot: false` explicitly writes a log-only save (tests).
- **Load** (`CommandLogLoad`, case 1 of section 2): the log is parsed and checked as before (it is the replay so far, continued by `handOver`); the
  game its log records is started; `LiveGame::loadSnapshot` loads the snapshot over it, checks the frame and the header's state hash, and is ready
  at once: no frame of the log runs. **Transactional:** the started game's own snapshot is kept first; a refused block, frame or hash puts it back
  (checked by its state hash) and the load fails named ("the save's snapshot is refused: ..."); a game that cannot be put back is closed (no frame
  runs any more) and the host discards it, never a half-loaded game. The client is told every object's model conditions (S-5011).
- **Verify mode** (`CommandLogLoad::setVerifyLog`, `openbfme_peer --verify-log`; off by default): the save is caught up from its log as an old save
  is, and at F the caught-up game's snapshot must equal the save's byte for byte (T2 across the two load paths).
- **Log-only saves** catch up as before (case 2), provided the engine compatibility id and data/rules identity still match. A snapshot does not
  bypass the same-engine-id check either; cross-build game-state migration (section 5) is not implemented.
- **The loading screen** (`save_flow.gd`, `campaign_flow.gd`, `AptMenuPlayer::set_still_load_progress`): retail's screen for a saved game
  (GameLogic::getLoadScreen RW 0x626A87 with a save loading): the picture screen with a film still (StillImage_Film01 .. 21), the ring and
  GUI:Loading, for a campaign mission and a skirmish (S-5012). SAVE-2's grey Godot panel is gone; an old save's catch-up draws its progress text and
  a bar on that screen in its font and colour.
- **Tests:** `test_save2_retail.cpp`: T1 in a fresh process from the snapshot (`openbfme_peer --load-save`, report `load_mode snapshot`,
  `catch_up_hashes 0`) for the 4-AI skirmish's named scenarios and an 11-minute save, and for the Angmar mission with a script timer running, each
  played to F + 3000 with every frame's hash compared; the 11-minute save and the mission also through the verify mode; the in-process load
  (ready at once, the header's frame and hash, every drawable showing its object's model conditions, the game played on equal, the verify mode);
  the transactional refusals (a changed byte, a truncated snapshot, another game's snapshot: each named, the started game's frame, state hash and
  drawables as before, the game still running; a header whose hash its log does not end with; the verify mode naming the first differing byte);
  the legal but unusual events (a capture, a temporary defection, a death, a player's every object killed, saved right after them and 30 frames
  later, loaded into another world and played 300 frames equal, saved again byte for byte); T3 with a Windows peer (OPENBFME_WINDOWS_PEER).
  Lane SAVE2SPLIT-1: the fresh-process T1 runs are a test case per save and load mode (`save2 retail T1 (SAVES.md) 4-AI skirmish <save>
  ...`, `save2 retail T1 (SAVES.md) a script waiting on a timer <mode> ...`), each its own gate job; a process records each game once.
  `godot --path godot -- --auto --save-test` loads a snapshot save and a log-only save through the screens (prerequisites and isolated folder below).
- **Measured** (Deck, Release, `openbfme_peer --load-save` in a fresh process, the 4-AI Fall Back game of the `save2 retail T1 (SAVES.md) 4-AI skirmish ...` cases): the
  11-minute save (frame 3,301, a 4,240,033-byte snapshot) loads from its snapshot in 100 ms; the same save caught up from its log (the earlier
  alphas' path, `--verify-log`) takes 27.8 s. The Angmar mission at frame 151: 385 ms from the snapshot, 4.5 s caught up. The in-process load of a
  1.8 MB snapshot (frame 601): 114 ms. Through the screens (`--save-test`, a CI host): 32 ms for a skirmish at frame 150, 141 ms for the mission.
- **T3 measured** (S-5013): the release's llvm-mingw openbfme_peer.exe under Proton 11.0's Wine on the Deck, the same profile identity as the Linux
  peer: it caught the Linux 11-minute save up from its log and saved the same snapshot bytes at F (the verify mode compares all 4,240,033), then
  loaded the Linux snapshot and played 3,000 frames with every hash equal (final 0xD552CA8C) in 105.8 ms of load.
- **Red then green** (the review fixes of 4a, 4b and 4d): with seven checks mutated away (a team member listed twice, the damage and death type
  sets, a horde's free slot, a script's name, a cell's player, a tree's bucket link) their four test cases fail on exactly those refusals; the
  real tree passes them.
- **Review r1 (Sol) fixes:**
  - *The load is transactional for every block.* `GameState::checkFraming` walks the whole stream before any block loads (the blocks in this build's
    order, each inside the stream, the end token, nothing after it): a truncated, reordered or hostile stream never touches the game. A block refused
    later is undone by loading the game's own snapshot back, which now works from any state: obfme:TerrainTrees restores the map's records first (it
    kept them before the first change) and obfme:TeamFactory loads a snapshot with fewer teams (the later teams are removed by its post-process once
    empty); the put-back is checked by the state hash and by the snapshot's bytes. Review r2: the trees' first records are kept before the first
    topple or harvest touches one (not after), and a save made while a trim of teams is pending (the Objects block saves and reloads the core
    blocks mid-load) holds the snapshot's teams only, so a refused snapshot with fewer teams than a game that made three since is put back; tests
    compare the trees' records themselves and the team count, not only the hash and bytes. Tests: Sol's three probes (a toppled tree saved, then a hostile end
    token, a late truncation, LargeGroupAudio's version refused), a refusal inside each of the 24 blocks into a game whose own trees changed, the
    framing refusals, and the probe test below.
  - *The menu keeps the running game.* `GameWorld.check_save` checks the snapshot's framing before the running game goes; `keep_running_game` keeps
    its own save in memory, and when the chosen save is refused later `prepare_kept_load` loads it back: the player is on the quit menu's load page
    over the same game (its frame and state hash) with the refusal (`--save-test` loads a hostile save over a running game). A game that cannot be
    kept (its own snapshot refused) is not left: the load stops before the saved game is prepared and the page shows why (review r2;
    `tools/save/test_save_flow.py`).
  - *Closed sets.* `tools/save/closed_sets.py` lists every integer and mask transfer of the snapshot code (988 in review r2) and puts each in a class: closed (a
    mask, flag set, enum, index, player, side, team, rank by name) and ids need their check after the transfer in the same function, object
    references (`xferObjectID`, the id list helpers) are looked up when used (S-5014), the open ones (frames, counters, quantities) are named by rule,
    and every other one (a name of an open and a closed word both among them) has an entry in `tools/save/closed_sets.json` with its reason or the
    place that checks it. Review r2: the discovery reads each file's code as one text (comments and strings blanked), so a transfer over several lines
    or through any receiver is found; a helper of the snapshot namespaces, an Xfer method, a `using` or a macro that would move an integer unseen
    fails the audit until it is placed. Its pytest mutates the real tree (an unchecked multiline or other-receiver transfer of the weapon bonus mask
    must fail) besides the empty, missing, unchecked, unlisted, stale and unreasoned inputs. The audit added the checks it found missing
    (the objects' status, model, placement, upgrade, armor set, weapon bonus, weapon set, script status and experience masks; the players' upgrade
    records, masks, sciences, ranks, relation keys and indices; the teams' id counters; the skirmish AI's target / tactic / attack ids and KindOf bits;
    the object filters; the Create-a-Hero records; the invisibility nuggets; the contain, body and projectile masks; the horde's flank index; a dozer's
    idle player and transition depth; the pathfinder's queued and ignored ids; the Legendary Heroes factor). `Common/SnapshotProbe.h` arms the n-th
    check of a load: `test_save4e.cpp` loads a real 4-AI game's snapshot (frame 1,500, a tree toppled) once per kind of check it runs through (151
    kinds, 110,159 checks), each with an out-of-set value at that place, and every load is refused naming the check, with the game it was loaded into
    left byte for byte as it was. The probe exercises only the checks that exist and that this snapshot reaches; a missing check is the audit's to
    find, not the probe's.
  - *A refused snapshot fails the save.* `CommandLogLoad::takeSnapshot`: the save is refused, named, in SaveLoad.apt's result box (retail shows a
    failed save's error there too); no log-only fallback.
- **War of the Ring battles:** not supported by this build (no War of the Ring battle start), so not in T1.
- **Stops:** S-5010 .. S-5014.

### BFME2CAMP1: the campaign, tutorial and rebuild-hole state (lane BFME2CAMP1, PR #137)

Every new field is versioned, and a block writes its new version only when the state it adds exists: games without that state keep their old bytes and hashes. Every older version loads, with the new field at its default.

| Where | Block / class | Version | Field (type) | An older save loads with |
|---|---|---|---|---|
| Save header (`SaveGameFile`) | header | 4 | `campaignCarryover` (string: `CampaignCarryover::State::serialize`, "" for none); refused in a skirmish save or when it does not parse | versions 1 to 3: "" (no carry-over step at the mission's start) |
| `GameLogicSnapshot.cpp` | obfme:GameLogic | 3 (written with a buildability override or a carry-over state) | the script buildability overrides (count; each: template name string, status int 0..3); the campaign carry-over state (bool present; the serialized state string) | versions 1 and 2: no overrides, no carry-over state |
| `Object.cpp` | Object | 2 | the campaign carry-over bit (bool, Object + 0x458 bit 4) | version 1: false |
| `Team.cpp` | the team prototypes | 2 (written when a prototype names an attack priority set) | each prototype's attack priority set name (string) | version 1: no name |
| `ScriptEngineXfer.cpp` | the script engine | 5 (always: CASTLEAREA-1's run-time trigger list extent at 4, MODCOMPAT-AI's completed upgrades at 5), 6 (written once a script edited the attack priorities) | the attack priority sets (bool; count; each: name string, default int, the template priorities as name / int pairs) | versions 2 and 3: the table as the map's INI made it |
| `AIUpdateSnapshot.cpp` | AIUpdateInterface | 3 (an area guard or an attack info override), 4 (a team guard) | v3: the area to guard (int, checked against the map's areas), the attack info override (bool + name string); v4: the team to guard (team id) | versions 1 and 2: no area, no override; version 3: no team |
| `AIUpdateSnapshot.cpp` | DozerAIUpdate | 3 | the rebuild flag (bool, + 0x4D0) | versions 1 and 2: false |
| `AIStateMachineSnapshot.cpp` | the AI state machine | 2 (written with a goal squad) | the goal squad (bool; the member ids) | version 1: no squad |
| `AIGuard.cpp` | the guard machine | 2 (an area guard), 3 (a team guard) | v2: the area (int, checked), its centre (coord) and centre flag (bool), the last area scan frame (uint); v3: the guarded team (team id) | version 1: no area; version 2: no team |
| `RebuildHoleModules.cpp` | RebuildHoleExposeDie | 1 | the die module's part only | new class: an older save has none |
| `RebuildHoleModules.cpp` | RebuildHoleBehavior | 1 (retail version 3's field order, RW 0x886669) | the update module's part; the worker, rebuilt structure and spawner ids (object ids); the frames to wait (uint); the worker and rebuild templates (name strings, a missing one refuses the load); the spawner's active flag (bool); its paid cost (real) | new class: an older save has none |

The headless check (`openbfme_peer --load-save`) starts a campaign mission's save the way a real load does: the header's carry-over (or, for a first mission without one, the campaign's fresh records) and the mission's delayed list.

### Developer commands (current implementation)

Linux / POSIX shell, from the repository root: the screen test needs a built
GDExtension in `godot/bin/`, Godot 4.7 on `PATH`, a graphical session, and
`ROTWK_INSTALL` / `BFME2_INSTALL` pointing to the retail installs. It writes
and deletes test saves; use an isolated folder, not an existing Save folder.
This is a UI test, not a headless unit test.

```sh
godot --path godot -- --auto --save-test --save-dir="$(pwd)/workspace/save-tests"
```

Linux / POSIX shell, from the repository root: the headless continuation
check needs the matching build's `engine/build/openbfme_peer`, the two install
variables above, a same-engine-id `.obfmesave` and the uninterrupted game's
full `.replay` recording. Keep those inputs under ignored `workspace/`.
`--verify-log` forces log catch-up and compares the snapshot at F; omit it
to exercise direct snapshot loading. The peer then plays the remaining replay
commands and checks their hashes. On Windows use the Windows build's `.exe`
and native paths instead; the command below is the Linux form.

```sh
engine/build/openbfme_peer --load-save workspace/saved.obfmesave --replay workspace/full.replay --verify-log --report workspace/save-check.txt
```
