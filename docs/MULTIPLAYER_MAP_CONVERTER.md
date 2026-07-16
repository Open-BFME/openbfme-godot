# Multiplayer map compiler scope and requirement register

This milestone follows the project owner's required order: question requirements,
delete unnecessary scope, simplify what remains, accelerate its feedback cycle, and
automate only the resulting proven process.

## Accountable product requirements

| ID | Accountable owner | Requirement | Acceptance |
|---|---|---|---|
| `MPMAP-P1` | Jonathan, project owner | Support the official BFME2 1.06 multiplayer/skirmish map corpus. Online networking is not included. | Corpus membership comes from the retail registry and exact catalog resolution, not filename guessing. |
| `MPMAP-P2` | Jonathan, project owner | Convert every shipped map in that frozen corpus into a working OpenBFME map. | Every selected map deterministically supplies terrain, water, starts, required objects, passability, buildability, navigation, and standard skirmish completion behavior. |
| `MPMAP-P3` | Jonathan, project owner | Treat reliability as 100% coverage of the frozen corpus, not arbitrary or modified maps. | A missing required feature fails the corpus gate with a stable diagnosis; it is never silently dropped or substituted. |
| `MPMAP-P4` | Jonathan, project owner | Apply question/delete/simplify/accelerate/automate in that order. | No generalized converter subsystem is built until the census proves the corpus requires it. |

Repository safety and architecture constraints remain mandatory, but their source files
do not name a human owner. This document does not invent one. Retail bytes remain under
the ignored in-repository `.private` firewall, the runtime remains independent of donor
formats/tools, and the focused retail gates remain authoritative.

## Deleted or deferred scope

The current milestone excludes:

- online networking;
- campaign, tutorial, shell, custom-map, and War of the Ring mode behavior;
- a universal SAGE parser or universal map-script virtual machine;
- one handwritten profile per map;
- lifecycle animation for decorative scenery unless the census proves it interactive;
- guided installer/repair UX, performance optimization, and mass conversion before the
  census closes.

Complete retail extraction is shared infrastructure and is already in scope. Conversion
outside the first multiplayer/faction gate is sequenced after Men/Fords rather than
deleted; it must reuse the same deterministic converters instead of creating per-map
special cases.

Standard skirmish scripts or `map wor` payloads are not excluded merely because of their
filename. Registry membership decides scope.

## Frozen-corpus evidence

The authoritative local candidate is the exact catalog winner
`maps/mapcache.ini`. On the verified installation it contains 72 records: 51 are marked
multiplayer, 46 resolve to shipped map payloads, and five are stale registry records
with no payload. The shipped corpus contains eight `map mp` and 38 `map wor` maps:

- 18 two-player maps;
- five three-player maps;
- 13 four-player maps;
- one five-player map;
- six six-player maps;
- three eight-player maps.

The five absent payloads are reported as `registry-stale-missing-payload`; they are not
conversion failures because there are no source bytes to convert.

## First implementation: observe before converting

Invoke the census directly without Blender, Godot, extraction caches, or conversion:

```powershell
.private\retail-work\tools\python-3.12-env\Scripts\python.exe `
  tools\openbfme_import.py --json census-maps --install F:\BFME2
```

The report is written inside the ignored private workspace at:

```text
.private\retail-work\reports\multiplayer-map-census.json
```

It contains only neutral bounded facts: registry evidence, virtual paths, source hashes,
chunk/version signatures, dimensions, counts, set hashes, current strict-cook results,
and grouped rejection reasons. It contains no map payload, script payload, object
properties, coordinates, absolute install path, or timestamp.

The census is allowed to report unsupported semantic features. It is not allowed to
silently omit a selected map or accept malformed envelope/name-table/top-level framing.
Its output determines the next minimal parser or compiler feature; automation of asset
conversion begins only after that evidence is reviewed.

## Corpus result and first parser closure

Two complete runs produced the identical report SHA-256
`1e737edc32439e973cbb1129b650e07facb6d6e3cd873dc5fef7c4e440ce2f04`.
The observed corpus—not an estimate—now says:

- all 46 maps pass the current strict bounded structural cook;
- trigger-area v1 and standing-wave-area v2 records are fully parsed, bounded,
  fail-closed, and preserved as source-derived neutral IR;
- 10 maps contain standing waves, six contain trigger areas, and 11 contain nonempty
  player scripts;
- no map uses an unsupported version of the nine chunk families already recognized by
  the census;
- every map contains the same 11 currently unclassified top-level families:
  `BuildLists`, `CameraAnimationList`, `EnvironmentData`, `GlobalLighting`,
  `LibraryMapLists`, `MPPositionList`, `NamedCameras`, `PostEffectsChunk`, `SidesList`,
  `Teams`, and `WorldInfo`;
- the maps contain 84,939 placements in total, range from 31 to 171 unique object types,
  and reference 24 to 94 terrain symbols each;
- 33 maps contain standing water and 23 contain rivers.

This deletes several assumptions from the implementation plan. A fixed two-player
runtime, three named fords, 66 terrain textures, 91 object types, or a `map mp` filename
filter cannot be generalized.

Structural acceptance is not a working-map claim. Nonempty triggers are marked
`source-records-imported-runtime-pending` until the referenced scripts consume them,
and nonempty standing waves carry the same status until Godot renders their exact
source settings. No source record is silently dropped to obtain 46/46.

## Five-map completion set

The first complete full-Men milestone deliberately spans player-count and format
diversity rather than choosing five easy two-player maps:

| Map path | Players | Required coverage |
|---|---:|---|
| `maps/map mp fords of isen ii/map mp fords of isen ii.map` | 2 | Existing deepest terrain, water, routing, and runtime proof. |
| `maps/map wor rivendell/map wor rivendell.map` | 3 | Standing waves and a nonempty script list. |
| `maps/map wor mount doom/map wor mount doom.map` | 4 | Fifteen rivers, a trigger area, and four nonempty script lists. |
| `maps/map wor dagorlad/map wor dagorlad.map` | 6 | Fourteen standing-water areas plus high object and terrain diversity. |
| `maps/map wor mordor/map wor mordor.map` | 8 | Eight-start setup and large-scale routing. |

These paths are the authoritative registry-backed identities. Localized display strings
must be resolved separately rather than inferred from title-cased paths.

## Exact multiplayer setup and routing findings

The remaining setup formats have now been read-only validated across all 46 maps and
against pinned OpenSAGE commit `588ac477367a0022adf29f20a084e8873014e6ce`. They are
bounded SAGE assets, not hidden navmesh data.

`MPPositionList` v0 is a sequence of nested `MPPositionInfo` v1 assets until the outer
payload boundary. Each child is:

```text
u8 isHuman
u8 isComputer
u8 loadAIScript
u32 team
u32 sideRestrictionCount
sideRestrictionCount * ascii16
```

Every map has eight lobby-capability slots. All allow human and computer control, load
AI scripts, and have no side restrictions. Eight is therefore not the playable player
count.

`SidesList` v6 contains:

```text
u8 unknownBoolean
i32 playerCount
repeat playerCount:
  AssetPropertyCollection playerProperties
  u32 buildListCount
  repeat buildListCount:
    ascii16 buildingName
    ascii16 templateName
    f32 x, y, z
    f32 angle
    u8 initiallyBuilt
    u32 rebuildCount
    ascii16 script
    i32 health
    u8 whiner
    u8 unsellable
    u8 repairable
```

Observed player properties are typed name, human flag, faction, Unicode display name,
enemy/allied lists, and optional color. The 13-18 players in this chunk include neutral,
civilian, and script identities; they are not playable starts. Every shipped map has
zero embedded build-list entries.

`Teams` v1 is an `i32` count followed by that many typed property collections. Team
names are unique, every owner resolves to a `SidesList` player, and every player has one
matching default team. Preserve extra map-authored teams. Four corpus fields are not
modeled by the pinned OpenSAGE reader: `exportWithScript`,
`teamInitialIdleSeconds`, `teamUnitExperienceLevel1`, and `teamUnitUpgradeList1`.
The first two occur in the five-map set and must remain marked semantically unresolved
until runtime/oracle behavior is proven.

`LibraryMapLists` v1 is a sequence of nested `LibraryMaps` v1 assets, each holding a
`u32` count and that many `ascii16` map references. For every map, the library-list,
`SidesList` player, and player-script-list counts agree. Exactly one library reference
is nonempty. All five selected maps resolve to the same library map, which itself has
eight waypoints and two script lists, one nonempty. Application/merge semantics are not
yet proven, so the converter must content-address this dependency without flattening
its terrain or objects into the playable map.

`WaypointsList` v1 is only a count followed by `i32 startId, i32 endId` pairs. All 46
maps have zero edges, and none of their waypoint objects carries a path label. Player
starts instead come from contiguous `Player_1_Start` through `Player_N_Start` waypoint
objects; their positions independently agree with mapcache triplets within 0.0047 source
units. There is no authored AI routing graph or navmesh to extract. Working navigation
must be deterministically generated from terrain passability, water/crossing rules,
building footprints, and dynamic blockers.

The selected-map setup census is:

| Map | Declared players | MP slots | Scenario players | Teams | Extra teams | Waypoints | Nonempty local scripts |
|---|---:|---:|---:|---:|---:|---:|---:|
| Fords of Isen II | 2 | 8 | 14 | 16 | 2 | 14 | 0 |
| Rivendell | 3 | 8 | 15 | 17 | 2 | 27 | 1 |
| Mount Doom | 4 | 8 | 15 | 19 | 4 | 28 | 4 |
| Dagorlad | 6 | 8 | 16 | 24 | 8 | 34 | 0 |
| Mordor | 8 | 8 | 18 | 19 | 1 | 24 | 0 |

The next cook output is one `setup.json` containing source versions, declared player
count, lobby slots, typed scenario players/build lists, teams, unresolved semantic
fields, content-addressed library references, script-list count, and cross-checks.
`waypoints.json` must gain typed waypoint IDs/names/positions/path labels, exact player
start bindings, and validated edges. Reject wrong versions, invalid booleans or floats,
duplicate property keys/IDs/names, unresolved owners/default teams, count mismatches,
noncontiguous starts, missing edge endpoints, and unsafe/ambiguous/cyclic library
references. Recognized-but-unimplemented semantic fields keep playable/private-parity
status false.

The next minimal compiler work is therefore ordered by observed dependency:

1. implement the now-proven `MPPositionList`, `SidesList`, `Teams`,
   `LibraryMapLists`, and waypoint/start layouts as the neutral `setup.json` and extended
   `waypoints.json` IR;
2. classify `WorldInfo`, environment, lighting, post-effects, and camera chunks as
   required visual facts or proven irrelevant to a working skirmish;
3. inventory the 11 nonempty script payloads and connect trigger polygons only to
   source-proven script behavior;
4. render standing waves from their preserved settings without inventing source data;
5. build the corpus-derived INI Object/Draw dependency resolver;
6. derive buildability and deterministic building-aware navigation for the five-map
   completion set before widening runtime availability to the rest of the corpus.
