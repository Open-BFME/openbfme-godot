# RW 0x7EDDB6 / 0x7EDECE — ThreatFinderUpdate, TheThreatFinderManager and the threat-finder script action (issue #73)

RotWK 2.01 (`RotWK/game.dat`, the S-001 provenance caveat applies). Port:
`engine/src/GameLogic/Module/ThreatFinderUpdate.cpp` (the module), `engine/src/GameLogic/System/ThreatFinder.cpp` (the
named areas and TheThreatFinderManager), `engine/src/GameLogic/ScriptEngine/ScriptActionsThreatFinder.cpp` (the action
RW 0x7C68AA), lane PORT-17. No data of the mod is reproduced here.

## Sources, and what each one establishes

The sandbox this note was written in has no route to the image: `tools/mod/rw.py` could not be run and
`workspace/RotWK/game.dat` could not be fetched. Everything below is marked with the source that carries it.

| Source | What it gives |
|---|---|
| issue #73 (`Port 17: ThreatFinderUpdate + script action`), addresses checked in `game.dat` before the task was issued (PROCESS.md 7б) | the registry: create RW 0x64F96A, createData RW 0x64F9A2, vtable RW 0xC07A30, registration site RW 0x65A133, interface mask 1, module type 0; the field table RW 0xC4C320; the map load's slot-45 call RW 0x694692; TheThreatFinderManager RW 0xDE8888; update RW 0x7EDDB6; xfer RW 0x7EDF70 with version (1, 1); the script action RW 0x7C68AA; the condition RW 0x608BCA; the manager's lookup RW 0x7EE260 and the answer RW 0x7EE1F4 |
| the repository's own readings: `GameLogic/SkirmishAI/AIThreatFinder.h` (lane AI-3), `docs/STOPS.md` S-1300, `docs/SAVES-RETAIL.md` row 0x7EDF70, `docs/FEATURE-MAP.md` row "Named map threat areas", the census (`engine/tests/data/object_model/*.tsv`) | the finder class's layout and its functions — ctor RW 0x7EDDFB (0x5BC bytes), records at + 0x000 (twenty of 0x48 bytes), name + 0x5A0, position + 0x5A4, radius + 0x5B0, last frame + 0x5B4, "no structures" + 0x5B8; the scan RW 0x7EE057, the query RW 0x7EE166, the value RW 0x68F0EC, the record add RW 0x7EDB72, the category RW 0x73C073 / 0x530, the AITarget's finder RW 0x6C6939; the rescan deadline RW 0xBDD42C; xfer 0x7EDF70 calls `FUN_007edc93` (UpdateModule::xfer); RW 0x7EDDB6 is a `rw-text.asm` function entry; 40 templates and 2 objects use the class |
| the BFME2 1.06 donor source (`Open-BFME-2`, `Code/GameEngine/Source/GameLogic/Object/Update/ThreatFinderUpdate*.cpp`, `Object/ThreatFinderManager.cpp`, `ScriptEngine/ScriptEngine_initActionTemplates.cpp`, `Rva003ECA69ElementClear.cpp`, `Rva003ECA81Add.cpp`) | the statement shape of each function, the class's older layout (0x56C bytes, 17 floats a record, name + 0x550, position + 0x554, radius + 0x560, frame + 0x564, flag + 0x568), the manager's three methods, and the action's parameter order and warning text |

**The finding that shapes the port**: RotWK has ONE finder class. The object the map module allocates (RW 0x7EDDFB,
0x5BC bytes) is the object the skirmish AI's AITarget holds (RW 0x6C6939) and the one lane AI-3 already ported as
`AIThreatFinder`. Its records, its scan (RW 0x7EE057), its query (RW 0x7EE166), its per-object value (RW 0x68F0EC) and
its category write (RW 0x7EDB72) are the same code, so the port reuses them instead of writing a second finder: the only
difference a map area has is the `noStructures` flag (+ 0x5B8 = 0, "count the structures standing in me").

The BFME2 donor has the same class **one release older and smaller** (0x56C bytes, 17 floats a record, the name at
+ 0x550). Its statement shape is the cross-check for every branch below; its offsets are BFME2's and are not used by the
port.

## The module

| What | Address |
|---|---|
| create / createData | RW 0x64F96A / RW 0x64F9A2 |
| vtable / registration site / mask / type | RW 0xC07A30 / RW 0x65A133 / 1 / 0 (Behavior) |
| field table | RW 0xC4C320: one row, `DefaultRadius`, parseReal RW 0x42ED00, `+ 0x08` |
| module data | RW 0x3ECC91 (the 0x0C-byte object: vptr, four bytes, the real — a shared fold with RadarUpdateModuleData RW 0x3ECC80) |
| module constructor / destructor | RW 0x7EDD3D / RW 0x7EDF5B |
| behaviour slot 45 (map load post-process) | RW 0x7EDECE, reached from RW 0x694692 |
| update (update interface slot 0) | RW 0x7EDDB6 |
| xfer | RW 0x7EDF70, version (1, 1) |

### Slot 45, RW 0x7EDECE (mapped to `onMapPlacement`, RW 0x694692)

1. With a null properties argument nothing happens — no finder, no name. RW 0x858A94 is that path (a structure built on
   a plot).
2. Otherwise a finder is made (RW 0x7EDDFB) with the object's name (`Object + 0x88`) and `noStructures` false, and
   registered in TheThreatFinderManager by name (BFME2 0x003ECDB7): a name that is the empty string is not registered.
3. The radius is the map object's `objectThreatFinderRadius` real when the properties hold that key, else the data's
   `DefaultRadius` (+ 0x08).

### Update, RW 0x7EDDB6

The owner's position (`Object + 0x38`) is copied into the finder's position when a finder exists; the answer is 1
(`UPDATE_SLEEP_NONE`, every frame).

### Xfer, RW 0x7EDF70

Version (1, 1), then `UpdateModule::xfer` (RW 0x850C60), then the finder (BFME2 0x003ECED5, 143 bytes). On a load the
finder is made again from the object's name and registered again — **except** that the port writes one bit first (see
"The port's deviations", 1). The finder's own xfer (BFME2 0x003ECBF8) is version (1, 1) and holds the twenty records,
the name, the position (three floats), the radius, the frame of the last scan and the flag.

## The finder: scan and answer

| What | Address |
|---|---|
| constructor | RW 0x7EDDFB (BFME2 0x003ECD60) |
| scan | RW 0x7EE057 (BFME2 0x003ECFBC .. 0x003ED0A8) |
| query (sum by relationship) | RW 0x7EE166 (BFME2 0x003ED0C4) |
| the answer the script asks | RW 0x7EE1F4 |
| rescan deadline constant | RW 0xBDD42C (3.0 s) |
| per-object value | RW 0x68F0EC |
| record add | RW 0x7EDB72 |
| category | RW 0x73C073 into template + 0x530 |

**Scan (RW 0x7EE057).** The twenty records are cleared; then ThePartitionManager's range query
(`iterateObjectsInRange(position, radius, FROM_CENTER_2D, {alive RW 0xC10E20}, ITER_FASTEST)`). An object counts when its
template's KindOf has any of CAN_ATTACK, HERO, SUPPORT and — when + 0x5B8 is 0 — STRUCTURE; never STRUCTURE when
+ 0x5B8 is 1. Its value (RW 0x68F0EC) is added to the record of its controlling player's index when the value is above
0, to the record's total (+ 0) and to its category slot (+ 4 + 4·category; the category -1, "no ThreatBreakdown", adds
to the total a second time). A structure counts 0 when it cannot attack; one that can and is FS_BASE_DEFENSE counts its
paid costs, else its ThreatLevel (150 when that is not above 100).

**Answer (RW 0x7EE1F4 → RW 0x7EE166).** When `(frame − last) * LOGICFRAMES_PER_SECOND >= 3.0` (RW 0xBDD42C) the records
are rescanned and the frame stored. Then: out = 0, and for i = 0 .. 19 in index order the player i's record is added
element-wise when the asking player's relationship to it (RW 0x6ACEAF) is ENEMIES (mode 0) or ALLIES (mode 1). Every
other mode returns the empty record. The script stores out[0].

## TheThreatFinderManager, RW 0xDE8888

A hash map at + 0x0C keyed by the name's NameKey. Add (BFME2 0x003ED265) registers only when the key is free, so a
second finder of the same name stays unregistered; remove (BFME2 0x003ED2A3) erases the key when it names this finder;
the lookup RW 0x7EE260 answers null for a name the key table never saw. The module's destructor (RW 0x7EDF5B) takes its
finder out of the manager first (BFME2 0x003ED0A8 / 0x003ECDB7).

## The script action, RW 0x7C68AA (ordinal 556)

The committed template table (`ScriptTemplateTables.inc:763`) gives four parameters `{4 COUNTER, 67, 66, 11 PLAYER}`.

1. The counter by name (RW 0x60817A): taken, created empty when absent.
2. Parameter 1: the area's name.
3. Parameter 2: exactly `"Enemies"` → 0, `"Allies"` → 1, anything else returns without touching the counter.
4. Parameter 3: the player by mask (RW 0x758F7C / 0x6A85B6): the ONE player whose bit equals the mask; a mask of
   several players or of none is no player and the action returns.
5. The finder (RW 0x7EE260). Missing: retail appends `WARNING - Threat Finder not found during script execution: <name>.`
   to its debug messages and leaves the counter alone.
6. Found: the counter's value becomes the answer's first float, truncated to an int.

The donor twin (`0x003C5E10`) matches this order statement for statement, including the warning text and the two exact
string compares.

## Not ported

* The condition RW 0x608BCA (its player lookup RW 0x7EE260), which reads the same finders: `ScriptConditions.cpp` is
  PORT 16's file and the committed template table has no condition entry for it. Stop **S-7184**,
  `ThreatFinderUpdate::stopLines`, reported in the retail world's acceptance stops.
* The donor's BFME2-only bits: `ThreatFinderUpdate` there has no `DefaultRadius` default and the manager's add takes a
  `ThreatLevel`-like argument — neither exists in the 2.01 reading of RW 0x7EDECE.

## The port's deviations (all reported, all deliberate)

1. RW 0x7EDF70's load makes a finder "without asking", which is only symmetric for a module that has one. A module built
   on a plot (RW 0x858A94) has none, so the port writes one bool (`hasFinder`) between `UpdateModule::xfer` and the
   finder's own xfer (OpenBFME native snapshot, S-4190). Without it, `save4b T4` (every ported behaviour class on an
   object of its own) fails: the load made a finder the save had not written.
2. The manager is a `std::map` keyed by the NameKey, not a hash map: every report and the finders' order stay stable.
   Nothing hashes it — the finders' state travels in each object's module block — so no state-hash section was added
   (`GameState::refusal` needs every section to own an always-present block, which a map with no areas could not).
3. The record category comes from `AIThreatFinder::threatCategory` (the ported RotWK reader RW 0x73C073), whose unknown
   `AIKindOf` behaviour is S-1300's, not this port's.
