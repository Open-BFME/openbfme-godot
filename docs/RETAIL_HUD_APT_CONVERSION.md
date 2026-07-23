# Retail Men HUD APT conversion foundation

This foundation replaces the unsatisfiable `SGCommandBar` mapped-image
assumption with the exact BFME2 HUD composition shipped by the retail install.
It is intentionally a bounded converter/runtime contract, not a universal
Flash runtime and not a claim of full HUD parity.

## Authoritative source closure

The Men/Fords gate is sealed to nine complete winning APT archive groups plus
the authored command-bar WND layout. The rows below are provenance groups
inside one production bundle resource, not redundant profile resources:

| Profile resource | Retail archive | Files | Bytes | Role |
|---|---|---:|---:|---|
| `men-hud-apt-palantir` | `apt/palantir.big` | 97 | 2,700,505 | root scene |
| `men-hud-apt-palantir-export` | `apt/palantirexport.big` | 21 | 2,075,384 | frame library |
| `men-hud-apt-side-command-bar` | `apt/ingamesidecommandbar.big` | 3 | 17,496 | side-bar scene |
| `men-hud-apt-spell-book` | `apt/ingamespellbook.big` | 4 | 30,453 | unconditional loaded scene |
| `men-hud-apt-help-box` | `apt/ingamehelpbox.big` | 13 | 9,377 | unconditional loaded scene |
| `men-hud-apt-hero-select` | `apt/ingameheroselect.big` | 27 | 2,282,905 | unconditional loaded scene |
| `men-hud-apt-planning-mode` | `apt/ingameplanningmode.big` | 28 | 1,083,153 | unconditional loaded scene |
| `men-hud-apt-image-library` | `apt/libingameimagesmain.big` | 54 | 2,110,754 | image library |
| `men-hud-apt-ui-library` | `apt/libingameui.big` | 13 | 100,376 | UI symbol library |
| `men-hud-controlbar-wnd-layout` | winning `window/controlbar.wnd` | 1 | 289,881 | 800x600 layout |

The nine APT groups are exactly 260 files and 10,410,403 bytes. With the WND,
the converter input is exactly 261 files and 10,700,284 bytes. The planner
checks their per-file hashes, archive identity, case-insensitive winner
identity, group manifest digest, aggregate counts, and private-tree bytes. The
single `men-hud-apt-runtime-bundle` resource selects 261 unique catalog
winners with zero missing. The `sage-apt-runtime` bundle converter revalidates
the exact source aggregate, emits 24 deterministic hash-suffixed PNG atlases,
and writes `data/ui/palantir/scene-contract.json`. Albertus MT is a separate
one-source byte-preserving resource and a closed converter binding; it is not
duplicated inside the 261-source runtime bundle.

Retail bytes and converted assets stay below `.private`. The public parser
tests build miniature repository-authored records in memory.

## Static reader boundary

`sage_apt.py` implements these bounded readers:

- CONST header, entry-offset, typed constant table, bounded CP1252 strings;
- APT root movie identity, dimensions/rate, character identities, import and
  export tables, root frame tables, and frame-item kind identities;
- DAT image-to-atlas assignments;
- RU geometry grammar, primitive counts, image references, and bounds;
- uncompressed 32-bit TGA identity, dimensions, orientation, and alpha bits;
- WND version 2 window hierarchy, 800x600 rectangles, status/style bits, and
  callback identities.

Every offset, count, string, file size, geometry number, texture dimension, and
tree depth is bounded. Imported movie and callback identities use restrictive
syntax. Filesystem paths and network loads are never followed from APT data.

The reader does **not** execute ActionScript. Frame items and character kinds
whose complete runtime semantics are not converted retain an exact
`[start,end)` byte range, byte count, and SHA-256 handoff. Duplicate exports are
preserved in source order and explicitly marked unresolved because retail
libraries contain both identical and conflicting repeats. This is deliberate:
unknown behavior fails closed rather than acquiring guessed semantics.

## Deterministic scene contract

`retail_hud_apt_profile.py` emits a private canonical plan with these composite
output identities:

| Runtime ID | Planned output |
|---|---|
| `bfme2.ui.palantir.symbols` | `data/ui/palantir/symbols.json` |
| `bfme2.ui.palantir.timelines` | `data/ui/palantir/timelines.json` |
| `bfme2.ui.palantir.imports` | `data/ui/palantir/imports.json` |
| `bfme2.ui.controlbar.layout.800x600` | `data/ui/palantir/controlbar-layout.json` |
| `bfme2.ui.palantir.runtime-bindings` | `data/ui/palantir/runtime-bindings.json` |
| `bfme2.ui.palantir.atlases` | `assets/ui/palantir/atlases/` |

The production bundle emits the atlas directory and one bounded runtime scene
contract. Symbols, timelines, imports, WND callbacks, and executable behavior
that are not supported appear in the contract's blocker inventory. This
avoids mistaking 261 source winners or 25 cooked outputs for a parity-ready HUD.

The assembled scene ID is `bfme2.ui.palantir`. The exact exported frame IDs
are `.frame.good.single`, `.frame.good.double`, `.frame.evil.single`, and
`.frame.evil.double`. Main node identities cover command UI, side command bar,
resource bar, radar, and globe. The completion profile exposes the generated
contract through `pack.files.palantirScene`; the Godot binder rejects the
planner schema, rejects non-parity by default, and never reintroduces
`SGCommandBar` as a synthetic raster.

The dependency contract proves:

```text
Palantir
  -> PalantirExport
  -> libInGameUI -> libInGameImagesMain
  -> libInGameImagesMain
  -> InGameSideCommandBar.swf
       -> libInGameUI
       -> libInGameImagesMain
```

The side command-bar movie is required for the first gate. Spellbook, help-box,
hero-select, and planning-mode SWFs remain explicit inactive edges and must
fail closed if requested before conversion. Full Men UI parity requires those
edges to be closed and exercised.

## Native callback policy

The plan contains a finite whitelist of the executable-observed
`AptPalantir::*`, `PalantirCommandUI::*`, and
`OnAptInGameSideCommandBar*` identities needed by the known composition.
Unknown `FSCommand` values are data, never host commands. WND callback names are
preserved as observed identities but are not bound by this foundation. Runtime
work must map every enabled callback explicitly and reject unknown names.

The static contract may retain retail virtual paths as provenance evidence.
Its runtime binding section references only cooked PNG outputs and resource
IDs; it marks source virtual paths evidence-only and leaves symbol/timeline
binding fail-closed until those cooked outputs exist.

## Generate and verify

From the repository root:

```powershell
$env:PYTHONPATH = 'importer'
python -m openbfme_importer.retail_hud_apt_profile `
  --effective-assets .private\retail-work\cache\effective-assets `
  --manifest .private\retail-work\cache\effective-assets\.openbfme\manifest.json `
  --catalog .private\retail-work\catalog\bfme2.json `
  --oracle-report .private\scratch\hud-apt-oracle\REPORT.md `
  --output .private\scratch\hud-apt-profile\plan.json `
  --profile .private\scratch\hud-apt-profile\profile.json

python -m unittest `
  importer.tests.test_sage_apt `
  importer.tests.test_retail_hud_apt_profile -v
python -m ruff check `
  importer/openbfme_importer/sage_apt.py `
  importer/openbfme_importer/retail_hud_apt_profile.py `
  importer/tests/test_sage_apt.py `
  importer/tests/test_retail_hud_apt_profile.py
```

Generate twice and require byte-identical JSON and the same declared aggregate
SHA-256. Writing is atomic. The plan and report contain identifiers, sizes,
hashes, structural metadata, and bounded handoffs only—never retail payload.

## Remaining conversion work

Source discovery, atlas conversion, bounded RU triangulation, the runtime
contract, and the fail-closed Godot binder are implemented. Next work must, in
order:

1. prove whether `_goodSingle`/evil is reachable in the declared Men-v-Men
   slice; delete that slice requirement if exact reachability evidence says it
   is not, otherwise bind it;
2. close only the 8 remaining source-inventoried ActionScript programs and the
   one unload event that are reachable in this slice;
3. recover remaining retail timeline scheduling, any reachable `FadeOut`, and
   external movie load/unload ordering without adding a general ActionScript VM;
4. bind the active `ControlBar.wnd` companion callbacks whose exact native
   semantics are already sealed;
5. render the original and converted HUD through matched text, composite,
   lifecycle, and resource-flash captures.

No graphical-parity claim is justified until those runtime and visual gates
pass.

## Declarative runtime subset (2026-07-13)

`retail_hud_apt_convert.py` now turns the sealed five-movie plan into schema
`openbfme.retail-hud-apt-runtime`. It independently rechecks every APT and RU
source hash, decodes fixed `PlaceObject`, `RemoveObject`, background, and frame
label records, resolves unambiguous cross-movie imports/exports, composes the
retail transforms, and flattens supported solid/textured RU triangles. DAT
image IDs select only the exact cooked atlas paths already attested by the
plan. It executes only the typed timeline-control subset described below.

The private BFME2 closure now applies one deliberately narrow, byte-proven
selection: retail `InitialSetup` passes `_good`, character 105 resolves that
label to frame 19, and frame 19 imports
`PalantirExport::PalantirFrame_GoodDouble`. This changes the static planner
draws from 26 all-solid triangles to 28 triangles (26 solid and 2 textured).
It does not execute an ActionScript VM and does not generalize the result to
any other state.

The converter now reconstructs every cumulative display list for the 22
multi-frame child timelines reached by the bounded composition: 640 authored
frames across 51 placed instances. Place, move, replace, remove, transforms,
color transforms, ratios, names, clip depths, labels, background colors, and
action-record identities are preserved deterministically. No
`additional-timeline-frames-not-converted` blocker remains for this closure.

The converter decodes all 74 unique ActionScript records reachable from the
bounded root and child-timeline closure. Every instruction has an exact
opcode, source/next offset, typed operand, nested function body when present,
validated branch target, byte-range hash, and stack contract. The executable
subset is now 64 programs: the original 53 exact `stop; end` and four exact
`goto-label; play; end` programs, three source-pinned `MinLOD` programs, the
exact command-point resource-flash program, and three exact typed
side-command topology programs. The latter group implements the retail
placement-before-queued-action order, neighbor truth table, next-then-prior
updates, Button1..15 order, and absent Button12..15 no-op semantics. The
resource-flash program rewinds the one
`CommandPointsFlash` instance to frame 8 / `_go` and exposes the exact
`Gui_PalantirResourceBarFlash` audio intent; it does not invent the native
counter trigger or mixer overlap policy. `RetailHudAptRuntime` refuses every
other program ID. Malformed alignment, function bounds, branch targets, stack
underflow, target identity, and external-input shape fail closed.

The remaining 10 programs retain exact instruction inventories and
`action-script-unsupported-opcodes` blockers. No generic
`action-script-not-executed` blocker remains. The resource-flash program's
former generic row is replaced by two narrower capture gates for the native
trigger counters and same-event mixer overlap/voice handling.

Playback is deliberately not guessed. `timeline-playback-not-bound` keeps the
converted frame data fail-closed until the retail scheduler order is proved.
The other blockers cover the 13 remaining programs, the one exact unload clip
event, the `_goodSingle`/evil choices, the external side-command-bar `FadeIn`
call, external-movie lifecycle capture, WND callbacks, and the narrow rendered
text/resource-flash capture gates.
The side bar remains at its authored frame-0 hidden/offscreen position. The
current contract is therefore `staticSubsetReady=true` and
`parityReady=false`.

### Exact clip-action closure (2026-07-13)

The converter now parses the 24-bit APT clip-event mask, key code,
next-event field, instruction pointer, and counted event-table ordering from
every reachable `PlaceObject` clip-action record. The bounded selected HUD
contains 28 reachable bindings/events backed by six unique retail source
records and six unique bytecode programs. Twenty-seven events are exact
`Initialize` (`0x000001`) events and one is exact `Unload` (`0x040000`); every
record has key code zero and next-event offset zero. Each contract row retains
the list/event/program byte offsets and SHA-256 identities, target instance
path, resolved target clip/character, event order, dispatch order, and the
typed unsupported-opcode inventory.

Five of the six unique clip programs are now source-pinned typed initialize
programs. They cover the local `SetFlashEffectState` method, twelve exact local
visibility assignments, and the three live text bindings. Twenty-seven of the
28 events execute with exact target and byte identity. The sole remaining
event is the exact `Unload` (`0x040000`) handler, retained as
`clip-action-lifecycle-dispatch-not-bound` until removal ordering is captured.

`RetailHudAptRuntime.execute_clip_action` can execute only a source-proven
`Initialize` event in that exact typed set, with a resolved target and zero
key/next-event fields. Lifecycle events, non-initialize ordering, and
unresolved target semantics are refused. The private retail contract reports
27 executable clip events and preserves one blocked unload event.

`RetailHudAptRuntime` binds only `pack.files.palantirScene` and requires scene
ID `bfme2.ui.palantir`. Its default mode rejects any non-empty blocker
inventory. An explicit static-subset opt-in can render converter inspection
output, remains marked non-parity, and never enables synthetic fallback.

Generate and run the focused gates:

```powershell
$env:PYTHONPATH = 'importer'
$python = '.private\retail-work\tools\python-3.12-env\Scripts\python.exe'
& $python -m openbfme_importer.retail_hud_apt_convert `
  --plan .private\scratch\hud-apt-profile\plan.json `
  --asset-root .private\retail-work\cache\effective-assets `
  --output .private\scratch\hud-apt-runtime\scene-contract.json
python -m pytest importer\tests\test_retail_hud_apt_convert.py -q
python -m ruff check `
  importer\openbfme_importer\retail_hud_apt_convert.py `
  importer\tests\test_retail_hud_apt_convert.py
& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' `
  --headless --path game --script res://tests/retail_hud_apt_runtime_runner.gd
```

The Godot binder requires this exact bounded selection contract, validates all
timeline/frame/display-list and instance counts, and requires the explicit
playback and selection blockers. It rejects a nonsequential/incomplete
timeline, `_goodSingle`, a side-bar fade, or a dropped blocker unless a future
converter proves and declares those states. The remaining parity-critical work
is retail scheduler/state selection, the 10 blocked ActionScript programs, the
one unload event, rendered text capture, WND callback/render binding, exact
external-movie lifecycle, resource-flash trigger/mixer capture, and side-bar
state.
Private parity mode continues to fail closed while those blockers exist.

### Exact reachable text and button closure (2026-07-13)

The three reachable text definitions and three reachable button instances are
now typed contract data rather than opaque character blockers. The bounded
decoder independently validates fixed record lengths, offsets, counts,
booleans, finite numbers, triangle indexes, state flags, action flags, strings,
and definition hashes. The runtime validates every font/text/button definition,
instance transform, transformed text bounds/color, and button hit mesh before
accepting static inspection mode. Malformed font glyph counts, missing font
references, invalid triangle indexes, unknown text alignment codes, and missing
button hit states fail closed.

The exact reachable text definitions are:

| Character | Bounds | Alignment code | Color | Height | Placeholder | Variable |
|---:|---|---:|---|---:|---|---|
| 130 | `[-2,-2,50.2,21.15]` | 0 | RGBA `0,204,255,255` | 14 | `999999` | `stringName` |
| 132 | `[-2,-2,25.5,21.15]` | 2 | RGBA `0,204,255,255` | 14 | `x99` | `stringName` |
| 134 | `[-2,-2,58.95,21.15]` | 1 | RGBA `0,204,255,255` | 14 | `999/999` | `stringName` |

All three are read-only, single-line, and no-wrap. Their parent instances and
Initialize clip bytecode prove these initial assignments:

| Parent instance | Dynamic source token |
|---|---|
| `Resources` | `$PalantirResources` |
| `ResourceMultiplier` | `$PalantirResourceMultiplier` |
| `CommandPoints` | `$PalantirCommandPoints` |

The contract preserves the instruction/program/clip hashes and exact
`push-this-variable`, `push-string stringName`, `set-string-member`, `end`
sequence. The runtime loads the exact copied Albertus MT file and formats the
three BFME2 live values from explicit match state. No placeholder or Arial
fallback is allowed. Seven device/raster/compositing capture gates remain in
the one `text-rendered-parity-capture-not-passed` row.

All three button instances reuse Palantir character 129. It is not a visible
button skin: it declares `isMenu=false`, local bounds `[-50,-50,50,50]`, four
vertices, triangles `0,1,2` and `2,3,0`, one exact `StateHit` record at depth 1
referencing character 128 with an identity transform, no Up/Over/Down visual
records, and zero button actions. The placements have no instance names and no
clip-action lists. The converter therefore emits three exact transformed hit
meshes with empty event bindings; it does not invent art, callbacks, or hitboxes.

Font character 63 names `Albertus MT` and has `glyphCount=0`, proving that the
APT expects an external font rather than embedded APT glyph shapes. The exact
retail winner is a separate private profile resource:

| Private resource | Winner | Bytes | SHA-256 | Cooked output |
|---|---|---:|---|---|
| `men-hud-font-albertus-mt` | `_patch103.big` / `albertusmt.otf` (precedence 0, entry offset 2510) | 24,712 | `6a1990e17f14ce5be199dde10f56dac3efd66aaa8e91d46119952cf55a9d9ba0` | `assets/ui/palantir/fonts/albertusmt-6a1990e17f14.otf` |

The profile puts the exact output path and source SHA in the bundle converter's
closed `externalFonts` option. The generated runtime contract repeats that
binding, the production bundle copies the font byte-for-byte, and Godot loads
that exact file after SHA/length validation. Arial, generated glyphs, and other
substitutes remain forbidden.

The four newly sealed movies are all source-load reachable because
`Palantir.InitialSetup` loads them unconditionally. They remain non-root child
movies and now bind atomically into the exact authored `SpellBookUI`,
`helpBox`, `HeroSelectUI`, and `planningModeUI` slots. The four old attachment
blockers are gone. One lifecycle-capture blocker retains async completion,
Hero initial visibility, unload/removal ordering, and the alternate HelpBox
anchor as explicit traces. `InGameSideCommandBar` remains the already-bound
load.

The complete HeroSelect parser also encounters one exact retail PlaceObject at
APT offset `166756`, flags `0xb6`, whose clip-action flag is set while its
pointer is zero. Only the exact `(InGameHeroSelect.apt, 166756, 0xb6)` identity
is accepted; every other flagged-null pointer is rejected. Its 64-byte record
SHA-256 is `7cf6432cbd91629acd5252c69aa957a08cadffd61214ae49ed0e078dec99a135`,
and it remains the explicit `source-flagged-null-clip-action-pointer` blocker.

### Compact production bundle API

The production path does not embed or stage the large planner JSON.
`convert_hud_apt_bundle(sources, output_directory,
expected_source_aggregate_sha256=..., external_fonts=...)` accepts the exact
case-preserved `{virtualPath: Path-or-bytes}` mapping. It reparses all nine APT/CONST/DAT
triplets, derives the referenced RU and TGA closure from those bytes, requires
`window/controlbar.wnd`, rejects extras, and therefore proves the exact
261-source / 10,700,284-byte closure itself.

The compact production options are the expected canonical source aggregate and
the one exact external-font binding. Unknown font rows or field drift fail.
Atlas paths derive from movie ID, texture ID, and the first twelve source-hash
characters. The bundle emits 24 deterministic Pillow 12.2.0 RGBA PNG files,
the exact Albertus MT font, and `data/ui/palantir/scene-contract.json` (26 files
total). After the bounded `_good` selection and exact child-timeline
reconstruction, the production summary is 28 draws / 31 display items, 22
timelines / 640 frames / 51 instances, 74 typed frame ActionScript programs
(66 executable and 8 blocked), six typed clip-action programs / 28 exact event
bindings (27 executable), one font / three text definitions, one button / three
button instances, four external child attachments, one exact selection-driven
Men/Fords `FadeIn` runtime, and 19 blockers.

The measured production blocker categories are: 8 unsupported ActionScript
programs; one each for unload lifecycle, external-movie lifecycle capture,
nondefault frame selection, the exact flagged-null source record, rendered text
capture, and timeline playback; two narrow
resource-flash gates; and three precise WND callback/service gates.

Two production conversions under
`.private/scratch/hud-men-fords-fadein-runtime/bundle-{a,b}` emitted 26
byte-identical files with source aggregate SHA-256
`f62347fb78065726715618ed9c73f152c678fec5646ddf7b0855825d1cb23599`,
aggregate contract SHA-256
`3e108f701d214427ee8b635cea950ccdde23c5647ffc4c8c868907e30fc9909c`,
and scene-file SHA-256
`7b812cfb3c3d25f9b2807670081ca3465b41cb9c7a7cc501167bd87a5d1572ff`.
(Those two contract hashes predate the raw-byte field below and are
superseded by any conversion after 2026-07-23.)

### Raw-byte execution contract (2026-07-23)

Every supported program (the 66 executable frame scripts and the five
executable clip-action programs, including the measured handler bodies
`palantir:169224`, `palantir:169256`, and
`ingamesidecommandbar:clip-event:13680`) now additionally carries an
additive, versioned `vmBytecode` field: the exact raw byte range from the
source APT (base64, per-segment SHA-256), its absolute byte-space offset,
every out-of-range operand segment the VM addresses (strings, constant-index
tables, function name/parameter tables), and the movie CONST identity. The
CONST tables themselves are emitted once per movie in the top-level
`vmConstants` section. `summary.vmBytecodeProgramCount` is 71 and
`summary.vmConstantsMovieCount` is 3 for the production closure. Existing
consumers that ignore the new fields are unaffected; the schema stays
`openbfme.retail-hud-apt-runtime` / version 0.

`RetailHudAptRuntime` executes raw-byte programs through the real
AptVm + AptRuntimeHost with no bytecode synthesis, after re-verifying every
segment hash and the per-program SHA-256 (tampered bytes fail closed at
configure time). Rows-only documents keep the tier-3 synthesis lane, and any
clean-contract failure still falls back to the legacy declarative path.
Counters distinguish the lanes: `vm_raw_byte_executed_program_count`,
`vm_synthesized_executed_program_count`, `vm_raw_fallback_program_count`,
and `legacy_executed_program_count`. Script-defined handler families
register from the retail bytes (named DefineFunction2 at clip scope and the
retail anonymous-function member-assignment form) and dispatch through the
real bytecode bodies; the private runtime gate proves raw-vs-synthesized
state equality and retail-byte handler registration/dispatch for all three
measured handler programs.
