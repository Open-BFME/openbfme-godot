# Retail slice runtime

How a converted map becomes a playable slice: map selection and resolution order,
the exact terrain mesh build contract, ambient audio, navigation evidence, the
map-converter requirement register, and how the slice profile is composed from
generated inputs.

The navigation section is a **gap register** — it lists retail behaviours that are
still unproven and must not be guessed. Keep it.

> **Consolidation note.** This document absorbs several former standalone
> documents, listed under their own headings below and preserved verbatim.
> Counts, blocker totals and hashes inside those sections are snapshots taken
> when that investigation was written and are **not** current status; they are
> kept because the surrounding evidence depends on them. Live gate results
> live only in [STATUS.md](../STATUS.md).

---

<!-- merged from docs/RETAIL_SLICE_MAP_SELECTION.md -->

## Retail slice map selection

Owner: retail slice runtime (`game/src/retail_slice`).
Audience: the menu/integration step that exposes skirmish map choice.

### Selectable maps

The slice resolves one of these ids (`bfme2.map.<slug>`), fail-closed on
anything else:

| Map id | Display name | Player starts | Source |
| --- | --- | --- | --- |
| `bfme2.map.fords-of-isen-ii` | Fords of Isen II | 2 | selected faction pack `files.entryMap` |
| `bfme2.map.rivendell` | Rivendell | 3 | five-maps pack catalog |
| `bfme2.map.mount-doom` | Mount Doom | 4 | five-maps pack catalog |
| `bfme2.map.dagorlad` | Dagorlad | 6 | five-maps pack catalog |
| `bfme2.map.mordor` | Mordor | 8 | five-maps pack catalog |

Display names come from each cooked `maps/<slug>/map.json` (`displayName`),
mirrored in the pack catalog `data/maps.json`. The slice always seeds a
two-player match from `Player_1_Start`/`Player_2_Start`; extra authored starts
are retained as source data only.

### Selection mechanism

Resolution order in `retail_vertical_slice._resolve_slice_map_id()`:

1. `OPENBFME_SLICE_MAP` environment variable (always wins, like
   `OPENBFME_SLICE_FACTION`), e.g.
   `OPENBFME_SLICE_MAP=bfme2.map.rivendell`.
2. `GameState.retail_map_id` (String, written by the menu's skirmish setup;
   `game/src/core/game_state.gd`). Empty means "no menu choice".
3. Default: `bfme2.map.fords-of-isen-ii` (the historical slice map,
   `RetailVerticalSlice.MAP_ID`).

Anything that is not a well-formed `bfme2.map.<slug>` id fails closed, as does
an id that cannot be resolved to a cooked map document. The resolved id and
pack root are exposed on the slice as `map_id` and `map_pack_root`.

Definition resolution in `retail_vertical_slice._resolve_slice_map_definition()`:

- The default Fords map is always resolved from the selected faction pack's
  `pack.json` `files.entryMap`. This keeps the Fords boot byte-exact even when
  a supplemental pack registers the same map id (the five-maps pack also
  cooks Fords; ContentDB merge order would otherwise hand the slice the
  roads-less five-maps variant).
- Any other id is taken from `ContentDB.get_bundle_map(id)` first (the
  registered-content path), then from the five-maps pack catalog directly
  (verification path while the pack is unregistered).

### Content registration (integration step)

Add the five-maps pack to `supplementalPacks` in
`.private/content-packs/selection.json` (do not commit retail bytes):

```json
{
  "activePack": "bfme2-men-vslice/<leaf-hash>",
  "schema": "openbfme.pack-selection",
  "schemaVersion": 0,
  "supplementalPacks": [
    "bfme2-five-maps-106-private"
  ]
}
```

The pack directory `bfme2-five-maps-106-private/` holds `pack.json` directly
(no hash-leaf layer), so the plain pack id is the entry. Its `pack.json`
declares `files.mapCatalog = data/maps.json`, so registration surfaces all
five maps through `ContentDB.get_bundle_map()` for menu listing.

Note for the menu: after registration, `ContentDB.get_bundle_map(
"bfme2.map.fords-of-isen-ii")` returns the five-maps variant (pack priority
904 loads after the Men pack's 900 and wins the merge). The slice itself is
immune (see above), but a menu that wants the slice-exact Fords definition
should read the selected faction pack's `files.entryMap` the same way.

### Headless verification

- Fords byte gate (245 checks): `run_retail_slice.bat --test`
  (`res://tests/retail_slice_runner.gd`).
- Per-map slice boot (33 checks): set `OPENBFME_SLICE_MAP` and run
  `res://tests/retail_slice_map_runner.gd`.
- Data-layer gate across the four new maps (55 checks, no slice scene):
  `res://tests/retail_map_data_runner.gd`.

### Per-map runtime profiles

`retail_map_data.gd` `MAP_RUNTIME_PROFILES` holds the per-map contract the
cooked documents cannot declare:

- Fords: requires the three authored ford crossings (`ford1`/`ford2`/`ford3`),
  re-reviews one source-impassable cell, and applies the reviewed retail
  crossing rule (cooked water blocks navigation except at ford corridors).
- Other maps: navigate on the source-authored passability grid alone (their
  water renders and rasterizes for diagnostics but does not block). Mordor's
  moat gap and Rivendell's shores are source-passable by design; blocking
  cooked water severed Mordor's two start banks.

Navigation regions extend the declared playable border to cover player starts
authored just outside it (Rivendell's `Player_2_Start`); camera bounds still
follow the declared border.

### Known per-map gaps (pack data, not runtime)

- No cooked roads layer for the four new maps (`roads`/`roadMaterials`
  documents absent from the pack; road wire objects remain in `objects.json`
  as unconverted source facts). Fords' roads load from the Men pack as before.
- All object placements in the four new maps are binding-unresolved (no GLB
  bindings in the pack), so no retail props render; unresolved placements are
  retained as non-rendered diagnostics, same as Fords' unresolved types.
- No per-map environment documents (fog/sky/lights use the global retail
  environment.ini afternoon rig, like Fords).
- No navmesh/routing graphs in the pack (`routingGraphStatus:
  empty-no-authored-navmesh`); navigation is the runtime's bounded A* over the
  source passability grid.

---

<!-- merged from docs/RETAIL_EXACT_TERRAIN_RUNTIME.md -->

## Exact Fords terrain runtime

The private Fords battlefield now renders the complete cooked SAGE height grid.
The former `TERRAIN_STEP = 8` preview mesh is gone.

### Exact geometry contract

For the retail Fords of Isen II cook, the runtime requires and builds:

- grid: `415 x 353`;
- vertices: `415 * 353 = 146,495`;
- source quads: `414 * 352 = 145,728`;
- triangles: `145,728 * 2 = 291,456`; and
- passability-colored vertices: all `18,325` source-impassable samples.

Vertices are row-major and use the exact local position returned by
`RetailMapData.terrain_local_at(x, y)`. UVs remain integer source-grid
coordinates `(x, y)`, which is the coordinate system consumed by the retail
terrain shader's tile, primary-blend, three-way-blend, and cliff lookups. Each
quad uses the source diagonal:

```text
top-left, top-right, bottom-left
bottom-left, top-right, bottom-right
```

Normals average the two triangles in each source quad and then the four quads
surrounding each vertex. Missing boundary quads contribute an upward normal.
This keeps the full mesh smooth without changing any source height sample.

The mesh is pre-sized before construction. This avoids hundreds of thousands
of incremental packed-array reallocations; the focused headless result builds
the full terrain, bound props, roads, water, and diagnostics in under one
second on the current workstation.

### Fail-closed bounds

`RetailFordsBattlefield` rejects a grid before allocating geometry when any of
these facts are inconsistent:

- dimensions cannot form a quad;
- more than `262,144` vertices or `524,288` triangles;
- height bytes are not exactly two bytes per declared sample;
- passability stride/payload does not cover every row;
- tile, primary blend, three-way blend, or cliff grids do not contain exactly
  one record per height sample;
- terrain or local transform scales are non-finite/non-positive; or
- the validated passability popcount disagrees with its declared count.

These are private-slice runtime bounds, not a claim that larger future maps are
unsupported by the importer. A larger map must deliberately raise the runtime
budget and add a corresponding fixture instead of silently decimating it.

### No parity placeholders

Ford gates remain exact gameplay/source facts, but the former tan `BoxMesh`
strips are no longer rendered. Their names, edges, elevation, source river ID,
and source section index live in `ford_gate_diagnostics` and node metadata.
`ford_marker_count` remains as the slice controller's compatibility count; it
now means validated source gates, not visible marker meshes.

Likewise, unresolved vegetation and rock samples no longer create cylinders,
spheres, trunks, materials, or `MultiMeshInstance3D` nodes. The runtime keeps:

- the complete unresolved type ID list and placement count;
- the bounded diagnostic sample count;
- vegetation/rock sample counts; and
- non-rendered diagnostic nodes for existing audit consumers.

The diagnostic nodes deliberately contain no renderable child. Missing retail
art therefore remains measurable without appearing to be a converted asset.

### Focused acceptance

Once the composed main pack containing road materials is selected, run:

```powershell
$env:OPENBFME_CONTENT = (Resolve-Path '.private\content-packs').Path
Remove-Item Env:OPENBFME_TERRAIN_PACK -ErrorAction SilentlyContinue
& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' `
  --headless --path game --script res://tests/retail_full_terrain_runner.gd
```

During pre-composition integration, the selected `a88...` base pack has exact
terrain and road control points but not the road-material document required by
`RetailMapData`. The same runner can use the already-audited, non-published road
fixture while requiring its terrain and material documents to equal the
selected pack:

```powershell
$env:OPENBFME_CONTENT = (Resolve-Path '.private\content-packs').Path
$env:OPENBFME_TERRAIN_PACK = (Resolve-Path `
  '.private\retail-work\packs\bfme2-men-vslice-roads-private').Path
& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' `
  --headless --path game --script res://tests/retail_full_terrain_runner.gd
```

Verified pre-composition result:

```text
RETAIL_FULL_TERRAIN_RESULT passed=29 failed=0
RETAIL_FULL_TERRAIN_METRICS width=415 height=353 vertices=146495 quads=145728 triangles=291456 impassable=18325 build_ms=945 roads=71 unresolved=1239
```

The independent road runner remains `27/27`. The integrated slice runner now
asserts these full terrain counts and requires ford/unresolved diagnostics to
have zero visible placeholder geometry.

---

<!-- merged from docs/RETAIL_FORDS_NAVMESH.md -->

## Fords navigation and footprint contract

`retail_fords_navmesh.py` produces a deterministic evidence contract for the
private Fords of Isen II slice. It deliberately does **not** generate or bless a
Godot navmesh. The contract separates facts preserved from BFME2 retail data
from converter-derived diagnostics and behavior that still needs a BFME2
oracle.

### Generate the private contract

From the repository root:

```powershell
$env:PYTHONPATH = 'importer'
$python = '.private/retail-work/tools/python-3.12-env/Scripts/python.exe'
$map = '.private/content-packs/bfme2-five-maps-106-private/maps/fords-of-isen-ii'
$assets = '.private/retail-work/cache/effective-assets'

& $python -m openbfme_importer.retail_fords_navmesh `
  --map-dir $map `
  --effective-assets $assets `
  --runtime-source game/src/retail_slice/retail_map_data.gd `
  --output .private/scratch/fords-navmesh/contract.json
```

The output contains only derived JSON evidence. Keep it under `.private`
because its file hashes and exact retail declarations attest the local retail
inputs.

### Proven facts

For the current BFME2 1.06 Fords cook, the contract proves:

- a 415 by 353 source grid, 10 SAGE world units per sample;
- a 20-sample playable inset, inclusive grid bounds `(20,20)` through
  `(395,333)`;
- an exact LSB-first passability grid with 18,325 impassable samples and SHA-256
  `11e911c6ba50a0d8dcf7fc3a71242b013b5dfdce1169ae86c939d2ddd5e654b9`;
- exact player starts at rounded grid cells `(303,69)` and `(70,237)`;
- exact authored cross sections for `ford1`, `ford2`, and `ford3`;
- zero authored waypoint edges (`empty-no-authored-navmesh`);
- exact collision geometry declarations for the four first-gate Men units,
  their four hordes, and the five first-gate Men buildings;
- all authored `RankInfo` positions for those hordes, without guessing which
  alternate formation state the executable selects.

The passability-only diagnostic finds that both player starts are in the same
four-neighbor component. That is useful converter evidence, but it is explicitly
marked `converter-derived-not-sage-routing-oracle`. It does not prove that BFME2
would select the same route, neighbors, costs, clearance, tie-break, or smoothing.

### Footprint interpretation

The INI geometry is kept as exact declarations, including all named and
additional geometry pieces. It is not rasterized into the terrain grid.

- Individual infantry and cavalry objects declare cylinder collision geometry.
- Horde objects declare box geometry, authored formation positions, and
  `LARGE_RECTANGLE_PATHFIND` in `KindOf`.
- Buildings can have multiple geometry pieces selected by upgrade or lifecycle
  state. The farm inherits geometry from `FarmInterface`; the fortress uses
  separate foundation and citadel objects.
- A `TERRAIN_BOUNDS` geometry name is evidence, not by itself a proven
  buildability rule.

This distinction matters: collision geometry is not equivalent to the
placement mask, unit clearance, or the executable's dynamic pathfinder shape.

### Required runtime conformance

A future Godot routing implementation should consume the contract in layers:

1. Preserve every source impassability bit. Never clear a source-blocked cell.
2. Preserve the exact player-start and ford source geometry transforms.
3. Run the emitted exact passability vectors before route tests.
4. Treat water masking, ford traversal, geometry clearance, diagonal movement,
   smoothing, and dynamic obstacles as policy inputs whose retail parity is
   still unproven.
5. Add BFME2-recorded route and placement vectors before claiming 1:1 routing
   or buildability.

The optional current-runtime observation records that the checked implementation
uses `AStarGrid2D`, Manhattan/four-neighbor movement, water polygon
rasterization, and ford-corridor dilation. Those are implementation observations,
not retail evidence.

### Blockers to 1:1 routing/buildability

The generated contract fails closed on seven unresolved areas:

1. No authored source buildability grid is present.
2. Water and named-ford traversal semantics are not proven per unit class.
3. Unit/horde clearance and `LARGE_RECTANGLE_PATHFIND` rasterization are not
   proven.
4. Blocking map-object collision/lifecycle closure is incomplete.
5. Building geometry-state selection is not proven.
6. Construction, damage, destruction, and rebuild nav updates are not proven.
7. Route costs, neighbors, tie-breaks, and smoothing are not proven.

The shortest path to closing these gaps is a small BFME2 oracle corpus: fixed
start/destination route captures for infantry and cavalry; placement
accept/reject probes around terrain, water, other buildings, and map props; and
the same probes before, during, and after building destruction. Those results
can become behavioral vectors without changing the source-fact layer.

### Verification

```powershell
$env:PYTHONPATH = 'importer'
& .private/retail-work/tools/python-3.12-env/Scripts/python.exe `
  -m unittest importer.tests.test_retail_fords_navmesh -v

& C:/Users/Jonathan/AppData/Local/Programs/Python/Python312/python.exe `
  -m ruff check importer/openbfme_importer/retail_fords_navmesh.py `
  importer/tests/test_retail_fords_navmesh.py
```

Generate two outputs with the same command and compare their file SHA-256 values.
The focused test also performs this byte-identical CLI check.

---

<!-- merged from docs/RETAIL_FORDS_AMBIENT_AUDIO_PROFILE.md -->

## Retail Fords ambient-audio profile

`retail_fords_ambient_audio_profile.py` closes the seven ambient sound IDs used
by the retail Fords of Isen II object placement contract. It is a planner: it
reads verified retail data below `.private`, but writes only JSON rules,
logical definitions, parameters, paths, sizes, and hashes. It never copies
audio payloads.

### Exact closure

The sealed BFME2 1.06 closure is:

| Logical ID | Source kind | Body | Attack/decay | Source range | Control |
| --- | --- | ---: | ---: | --- | --- |
| `Amb_BirdsAmonHen1` | `AudioEvent` | 10 | 0 | `AMB_MIN_RANGE`..`AMB_MAX_RANGE` = 300..800 | `loop` |
| `Amb_BirdsAmonHen2` | `AudioEvent` | 10 | 0 | `AMB_MIN_RANGE`..`AMB_MAX_RANGE` = 300..800 | `loop` |
| `Amb_MTBirds1Loop` | `AudioEvent` | 6 | 2 | `AMB_MIN_RANGE`..`AMB_MAX_RANGE` = 300..800 | `loop` |
| `Amb_MTBirds2Loop` | `AudioEvent` | 8 | 2 | `AMB_MIN_RANGE`..`AMB_MAX_RANGE` = 300..800 | `loop` |
| `Amb_CritterDesert1` | `AudioEvent` | 3 | 0 | `AMB_MIN_RANGE`..`AMB_MAX_RANGE` = 300..800 | `loop` |
| `Amb_WaterRiver1Loop` | `AudioEvent` | 13 | 2 | 400..1000 | `loop` |
| `AmbientAmonHenForest1Stream` | `AmbientStream` | 1 | 0 | not authored | not authored |

That is 57 unique leaves: 56 WAV samples and one MP3 stream. There are no
`Multisound` nodes in this particular closure. Every stem must resolve to one
and only one winning effective-asset path. The planner rejects missing or
ambiguous stems, duplicate/cyclic logical definitions, changed placement
counts, changed Godot routing, and catalog misses.

The map-side contract contains 50 placements:

- 16 `Amb_BirdsAmonHen1`
- 11 `Amb_BirdsAmonHen2`
- 3 each of the two Ithilien bird object types
- 6 desert critter emitters
- 10 river emitters
- 1 Amon Hen forest stream emitter

The source object names and logical event names are distinct for the two
Ithilien bird emitters and for the stream. The planner verifies the exact
seven-row mapping already declared by `retail_slice_audio.gd`.

### Profile and runtime handoff

`profileFragment.resources` contains two conversion rules:

- one 56-pattern `audio` rule that cooks PCM WAV leaves under
  `assets/audio/ambient/`;
- one exact `copy` rule for the MP3 ambient stream.

`runtimeAudioRegistryAddition` is a complete, schema-compatible
`openbfme.audio-events` version 1 document for this subset. It retains source
parameter order. Numeric `MinRange` and `MaxRange` macros are resolved to 300
and 800 in the runtime rows, while `sourceDefinitions` preserves the authored
macro tokens and records both authored and resolved range values. Attack and
decay identifiers remain parameters and are also included in the exact sample
closure.

Deduplication is permitted only when the current complete profile already owns
the same virtual source path and therefore the same manifest hash. A matching
hash at a different path is not deduplicated. For the current Men-Fords
profile, all 57 leaves are new.

### Honest runtime boundary

This plan closes extraction, conversion inputs, registry data, source
parameters, and placement routing. It does not claim SAGE playback parity.
The current Godot runtime can instantiate the 3D emitters and select body
samples, but it explicitly reports unsupported SAGE behavior for attack/decay
envelopes, delayed loop scheduling, attenuation curves, priority/limit,
pitch/volume variation, and the ambient stream's distinct semantics. Those
diagnostics must remain until executable behavior is implemented and verified;
the audio payload closure alone is not a 1:1 audio gate.

### Generate and verify

From the repository root:

```powershell
$env:PYTHONPATH = "importer"
python -m openbfme_importer.retail_fords_ambient_audio_profile `
  --effective-assets-root .private/retail-work/cache/effective-assets `
  --manifest .private/retail-work/cache/effective-assets/.openbfme/manifest.json `
  --catalog .private/retail-work/catalog/bfme2.json `
  --map-objects .private/retail-work/packs/bfme2-men-vslice/maps/fords-of-isen-ii/objects.json `
  --complete-profile .private/retail-work/profiles/men-fords-v0-full.generated.json `
  --runtime-audio game/src/retail_slice/retail_slice_audio.gd `
  --output .private/scratch/fords-ambient-audio-profile/plan.json

python -m ruff check `
  importer/openbfme_importer/retail_fords_ambient_audio_profile.py `
  importer/tests/test_retail_fords_ambient_audio_profile.py
python -m unittest importer.tests.test_retail_fords_ambient_audio_profile -v
```

Run the generator twice and compare the complete JSON documents. The focused
private integration test also constructs an `ImportProfile`, resolves it
against the real BFME2 catalog, and requires 57 selected entries with zero
missing required resources.

---

<!-- merged from docs/MULTIPLAYER_MAP_CONVERTER.md -->

## Multiplayer map compiler scope and requirement register

This milestone follows the project owner's required order: question requirements,
delete unnecessary scope, simplify what remains, accelerate its feedback cycle, and
automate only the resulting proven process.

### Accountable product requirements

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

### Deleted or deferred scope

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

### Frozen-corpus evidence

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

#### Beyond the multiplayer corpus

The same registry also registers every non-multiplayer family, and the campaign lane
(`docs/CAMPAIGN_PLAN.md` phase A) now discovers all of them.
`generate-map-profile --map-set` takes a category (`skirmish`, `wotr-battle`,
`campaign`, `cinematic`, `tutorial`), a group (`playable`, `single-player`) or `all`;
`classify_map_directory` derives the category from the shipped directory name alone.

Only the map binary is required. `map.ini`, `_art.tga` and `_pic.tga` are optional and
recorded as present or absent, because 24 of the 68 BFME2 map directories and 60 of the
179 RotWK ones ship no preview. Non-lobby categories are parsed and cooked with the
existing `scenario` SAGE map profile, since campaign, cinematic, tutorial and shell maps
ship zero `Player_N_Start` waypoints by design; the sage-map resource declares that with
an explicit `options.profile`.

Every catalog row carries its `category`, and the runtime's `list_catalog_maps` offers
only `LOBBY_MAP_CATEGORIES` (`skirmish`, `wotr-battle`). A campaign or cinematic map is
cooked and catalogued but is never a skirmish menu offering.

Current planning result: BFME2 67 of 67 shipped maps and RotWK 2.01 128 of 128.

#### Automatic prop binding

`options.objectBindings` is no longer hand-authored. `map_prop_bindings.py` feeds each
map's own recorded placement types through the existing
`retail_visual_closure` -> static/hierarchical/animated planner chain and injects the
merged bindings plus the conversion resources they depend on. Pass
`--effective-assets <root>` to enable it; without it the lane declares no bindings
exactly as before.

- SAGE logical namespaces (`*Waypoints/Waypoint`, `*GenericAIObjects/GenericAIObject`)
  are declared logical directly — they are editor namespaces, never renderables.
- A target the static planner rejects for exactly one reason, `no-physical-model-w3d`,
  is proven non-renderable (definition resolved, inheritance complete, no diagnostics,
  zero geometry) and is declared logical from that evidence. This is how Fords' 26
  hand-authored ambient-emitter and skirmish-marker rows are derived rather than
  authored.
- One owner per retail texture source is kept across the whole profile, not per map, so
  a model resource shared by several maps is declared identically wherever it appears.
- A map whose animated bundle shape disagrees with an already-declared resource has its
  bindings dropped whole and recorded in `planning_evidence.propBindingFailures`; it is
  never half-applied.

### First implementation: observe before converting

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

### Corpus result and first parser closure

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

### Five-map completion set

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

### Exact multiplayer setup and routing findings

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

---

<!-- merged from docs/RETAIL_SLICE_PROFILE_COMPOSITION.md -->

## Retail slice profile composition

`retail_slice_profile.py` composes the private Men-versus-Men Fords of Isen II
conversion profile. It is a deterministic metadata operation only: it does
not extract assets, build a content pack, publish a pack, or change content
selection.

### Exact inputs

The current composition consumes six already-validated inputs:

1. `importer/profiles/men-fords-v0.json`
2. `.private/retail-work/profiles/men-fords-v0-roads.generated.json`
3. `.private/retail-work/profiles/men-command-leaves.generated.json`
4. `.private/retail-work/reports/retail-static-prop-plan-0d51ad8d31ca6e6c.json`
5. `.private/retail-work/reports/retail-hierarchical-prop-plan-0d51ad8d31ca6e6c.json`
6. `.private/retail-work/reports/retail-animated-prop-plan-0d51ad8d31ca6e6c.json`

It also requires the real BFME2 catalog. Composition without catalog
resolution is intentionally unsupported. Pattern uniqueness alone cannot
prove that two different wildcard or exact rules do not select the same
physical `(archive, name)` entry.

The generated profile has the explicit profile ID
`men-fords-v0-full-generated`, while preserving the selected pack ID
`bfme2-men-vslice`.

### Deletion before addition

The composer removes twenty obsolete base resources by exact ID and exact
source-pattern list. Any missing ID, renamed path, changed case, or changed
pattern list fails the operation instead of broadening the deletion:

- the old PTGrass15 model and texture rules, replaced by the static plan;
- four semantic groups wholly owned by the faction leaf profile;
- all six legacy Gondor Soldier voice groups, replaced by the enumerated
  faction audio closure;
- eight legacy portrait, command, and training-icon rules whose source atlases
  are owned by the faction UI closure.

`gondor-fighter-definitions` is narrowed from nine sources to the five not
owned by the faction semantic closure: `armor.ini`, `gamedata.ini`,
`locomotor.ini`, `music.ini`, and `weapon.ini`. The four removed paths are
checked against the exact faction resource patterns.

The six deleted audio rules contain wildcards, so literal pattern comparison
is insufficient. Their 35 real catalog selections are resolved first and each
must be owned by the faction audio profile before deletion is accepted.

### Roads and prop bindings

The Road profile must be the base profile plus exactly five Road texture
resources, one exact `road-materials.json` runtime document, and only the
`roadMaterials` addition to the Fords map metadata. The expected Road IDs are:

- `Footprints`
- `FtPrintDrkGr02`
- `FtPrintGrass02`
- `FtprintsDrk`
- `FtprintsDrk02`

The static plan contributes 53 resources and 38 exact type-name bindings. The
hierarchical plan contributes 16 planned resources and six more exact
bindings. Three hierarchical texture rules are deliberately not duplicated:

| Exact retail source | Reused owner |
| --- | --- |
| `art/compiledtextures/gb/gbbarracks_n.dds` | `men-structure-shared-material-textures` |
| `art/compiledtextures/gb/gbfarm.dds` | `men-farm-material-textures` |
| `art/compiledtextures/pr/prgrey.dds` | `static-prop-texture-prgrey-791db9ad131c` |

The affected hierarchical `inputResourceIds` are rewritten to those existing
owners. This keeps one physical catalog owner per source while still staging
the raw texture for every dependent model conversion. The hierarchy batch
therefore adds 13 resources, not 16. The meshless `WtrflHaze` and
`WtrRiplsSmall` HLODs remain explicit rejected diagnostics instead of empty
GLBs.

The animated plan contributes three exact W3D bundles plus four raw texture
resources for the only currently eligible animated map props:

- `Fish`: 13 exact placements;
- `CaptureFlag`: two exact placements;
- `Egret`: two exact placements.

`art/compiledtextures/sh/shadowi.tga` is already owned by
`static-prop-texture-shadowi-6536093a930f`. The animated shadow resource is
therefore removed and the Egret bundle dependency is rewritten to that static
owner. The animated batch adds six resources rather than seven while retaining
all 17 planned placements.

The Fords `objectBindings.models` list is replaced, not appended blindly. Its
final value is the exact 38 static rows, the exact six hierarchical rows,
and the exact three animated rows, with 47 case-insensitively unique type
names.

### Faction UI and runtime data

All 85 faction resources are retained exactly. The three faction runtime
documents are copied without semantic edits:

- `data/audio_events.json`
- `data/strings.json`
- `data/ui_manifest.json`

The base Soldier audio runtime document is intentionally replaced by the
faction document. The other two paths are new. `pack.files` receives the exact
`audioEvents`, `strings`, and `uiManifest` entries.

The four member objects receive paths from these exact mapped-image rows:

| Object | Portrait | Command icon |
| --- | --- | --- |
| `bfme2.object.gondor-fighter` | `UPGondor_Soldier` | `WOR_GondorSoldier` |
| `bfme2.object.gondor-archer` | `UPGondor_Archer` | `WOR_GondorArcher` |
| `bfme2.object.gondor-tower-guard` | `UPGondor_TowerGuard` | `WOR_GondorTowerGuard` |
| `bfme2.object.gondor-knight` | `UPGondor_Knight` | `WOR_GondorKnights` |

Each mapped-image path must be an actual crop output of the faction resource
that owns its exact compiled atlas.

The three faction documents currently carry `complete: false`. That value is
preserved deliberately. The documents are exact and usable for the scoped
roster, but their producer does not claim full-faction asset conversion or
original-game oracle parity. Composition must never turn that conservative
provenance flag into a completion claim.

### Collision and acceptance gates

Before writing, the composer enforces all of the following:

- authoritative `ImportProfile.load` acceptance;
- no case-insensitive resource-ID collision;
- no case-insensitive source-pattern collision;
- no case-insensitive declared or concrete output collision;
- no case-insensitive runtime path collision;
- no case-insensitive map-binding type collision;
- no resource output equal to a runtime document path;
- no duplicate resolved `(archive, name)` owner;
- no missing required pattern or expected-count mismatch;
- no more than 512 resources (the bounded ImportProfile ceiling);
- exactly five Roads and 47 prop bindings;
- exact preservation of the faction runtime documents and their completeness
  flags.

The current real-catalog result is:

- 222 resources;
- 1,954 unique selected retail files;
- 1,729 declared/concrete cooked outputs;
- zero duplicate catalog selections;
- zero source, output, runtime, or binding collisions.

### Reproduce the private profile

From the repository root, using the pinned private Python environment:

```powershell
$env:PYTHONPATH = 'importer'
.private\retail-work\tools\python-3.12-env\Scripts\python.exe `
  -m openbfme_importer.retail_slice_profile `
  --base importer/profiles/men-fords-v0.json `
  --roads .private/retail-work/profiles/men-fords-v0-roads.generated.json `
  --faction .private/retail-work/profiles/men-command-leaves.generated.json `
  --static-plan .private/retail-work/reports/retail-static-prop-plan-0d51ad8d31ca6e6c.json `
  --hierarchical-plan .private/retail-work/reports/retail-hierarchical-prop-plan-0d51ad8d31ca6e6c.json `
  --animated-plan .private/retail-work/reports/retail-animated-prop-plan-0d51ad8d31ca6e6c.json `
  --catalog .private/retail-work/catalog/bfme2.json `
  --output .private/retail-work/profiles/men-fords-v0-full.generated.json `
  --report .private/retail-work/reports/men-fords-v0-full-composition.generated.json
```

The report records every input digest, exact prune, dependency reuse, resource
count, runtime merge, icon remap, selected-file count, and the final profile
SHA-256. The generated profile/report stay under `.private`; neither contains
permission to publish or select a pack.

### Late completion overlay

`retail_fords_completion_profile.py` composes the exact late closures onto the
sealed full profile without reopening its earlier decisions:

- header-only/no-motion prop correction;
- three neutral structure lifecycles;
- Fords particle and FX definitions;
- seven Fords ambient-audio roots and 57 exact samples;
- nine Men building-lifecycle audio events and all 79 exact samples, adding
  only the eight previously absent construction-loop attack/decay leaves;
- the exact Men building FX/particle definition closure: 18 missing
  definition projections, six missing textures, and four missing FX-list
  records, while preserving all ten dual-family Men systems as unresolved;
- Gondor Archer streak, arrow model, impact mappings, and 100 audio leaves;
- exact Fords AFTERNOON lighting/fog/shadow/cloud/macro environment evidence;
- the 261-source Men HUD APT runtime bundle, including all four formerly
  missing unconditional external-movie archives, plus the exact retail Albertus MT
  font winner as a separate one-source byte-preserving resource;
- the 10-type/26-placement animated-prop runtime contract.

The current deterministic result has zero missing required inputs; volatile
resource, projection, and file counts remain in generated reports. The profile SHA-256 is
`0bc2e76708d3c13b0aeac45afe375e4f120acdf329344b79d683f42e5d667c9d`.
Two independent generations were byte-identical. Unsupported renderer/gameplay
semantics remain named blockers and `vertical_slice_complete` remains false.

Reproduce it from the repository root:

```powershell
$env:PYTHONPATH = 'importer'
$python = '.private\retail-work\tools\python-3.12-env\Scripts\python.exe'
& $python -m openbfme_importer.retail_fords_completion_profile `
  --base-profile .private\retail-work\profiles\men-fords-v0-full.generated.json `
  --base-report .private\retail-work\reports\men-fords-v0-full-composition.generated.json `
  --no-motion-plan .private\scratch\no-motion-prop-profile\plan.json `
  --neutral-plan .private\scratch\neutral-lifecycle-profile\plan.json `
  --particle-plan .private\scratch\fords-particle-profile\plan.json `
  --ambient-audio-plan .private\scratch\fords-ambient-audio-profile\plan.json `
  --archer-projectile-plan .private\scratch\archer-projectile-profile\plan.json `
  --fords-environment-plan .private\scratch\fords-environment-profile\plan-a.json `
  --hud-apt-plan .private\scratch\hud-external-movies-profile\plan-a.json `
  --animated-runtime-contract .private\scratch\animated-prop-runtime-contract\contract.json `
  --men-damage-audio-contract .private\scratch\men-damage-audio\contract-a.json `
  --men-damage-effects-contract .private\scratch\men-damage-effects\contract-a.json `
  --catalog .private\retail-work\catalog\bfme2.json `
  --output .private\retail-work\profiles\men-fords-v0-complete.generated.json `
  --report .private\retail-work\reports\men-fords-v0-complete-composition.generated.json
```

---

