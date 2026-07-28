# Retail HUD conversion and runtime contract

What Open BFME's converter and Godot runtime actually do with the retail HUD
evidence: the APT source-closure and static-reader boundary, the scene-contract
schema, the raw-byte VM contract, the typed host-bridge surface, and the WND
companion runtime.

The matching retail evidence lives in
[RETAIL_HUD_APT_ORACLE.md](RETAIL_HUD_APT_ORACLE.md) and
[RETAIL_CONTROLBAR_WND_ORACLE.md](RETAIL_CONTROLBAR_WND_ORACLE.md).

**Read the counts here as history, not status.** These sections were written at
different dates and state mutually contradictory blocker totals, supported /
unsupported ActionScript counts, and "N of 21 callbacks implemented" figures.
None of them is authoritative. The authoritative numbers are whatever the
converter emits and the runners assert.

> **Consolidation note.** This document absorbs several former standalone
> documents, listed under their own headings below and preserved verbatim.
> Counts, blocker totals and hashes inside those sections are snapshots taken
> when that investigation was written and are **not** current status; they are
> kept because the surrounding evidence depends on them. Live gate results
> live only in [STATUS.md](../STATUS.md).

---

<!-- merged from docs/RETAIL_HUD_APT_CONVERSION.md -->

## Retail Men HUD APT conversion foundation

This foundation replaces the unsatisfiable `SGCommandBar` mapped-image
assumption with the exact BFME2 HUD composition shipped by the retail install.
It is intentionally a bounded converter/runtime contract, not a universal
Flash runtime and not a claim of full HUD parity.

### Authoritative source closure

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

### Static reader boundary

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

### Deterministic scene contract

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

### Native callback policy

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

### Generate and verify

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

### Remaining conversion work

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

### Declarative runtime subset (2026-07-13)

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

#### Exact clip-action closure (2026-07-13)

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

#### Exact reachable text and button closure (2026-07-13)

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

#### Compact production bundle API

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

#### Raw-byte execution contract (2026-07-23)

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

---

<!-- merged from docs/RETAIL_HUD_APT_CONTRACT.md -->

## Retail Men HUD source contract

The private Men/Fords HUD profile has two deliberately separate resources:

- `men-hud-apt-runtime-bundle` selects the exact 261-source APT, TGA, RU,
  DAT, CONST, and WND closure, including all five unconditional InitialSetup
  movie loads, for conversion into the Palantir runtime contract and atlases.
- `men-hud-font-albertus-mt` copies the exact retail Albertus MT winner to
  `assets/ui/palantir/fonts/albertusmt-6a1990e17f14.otf`.

The font winner is `albertusmt.otf` from `_patch103.big`, precedence `0`, entry
offset `2510`, with 24,712 bytes and SHA-256
`6a1990e17f14ce5be199dde10f56dac3efd66aaa8e91d46119952cf55a9d9ba0`.
The profile planner checks all of those fields against the effective-assets
manifest, rereads and hashes the private file, and requires the catalog winner
to agree before it emits the resource.

Palantir font character `63` names `Albertus MT` and has no embedded glyph
payload. The HUD plan therefore binds `palantir:63` to this exact font resource;
no operating-system font, generated glyph set, or substitute is permitted.
Runtime `FontFile` loading and dynamic text-value providers are separate gates
and remain fail-closed until their own implementation and rendered proof pass.

The deterministic HUD fragment contains 2 resources and selects 262 sources:
261 in the runtime bundle and the separate Albertus font source. The four newly
sealed movie archives contribute exactly 72 files and 3,405,888 bytes; the
runtime bundle is 10,700,284 bytes with source aggregate
`f62347fb78065726715618ed9c73f152c678fec5646ddf7b0855825d1cb23599`.
Two private planner runs produced identical files: plan SHA-256
`b2478347d3e0dd9fe634649ad86abb4e17e4c017df14e2894152ffc4e141bc2f`,
profile SHA-256
`1cc78a8b3a2c08b655862387e569301ec592908379d66f543b8f49919fd84914`,
and canonical plan aggregate SHA-256
`d8850c6033b8ae3041e044246ab216550b6eabb1d8cce0397006f936066c36c4`.
The `F:\BFME2` import plan resolves all 374 completion resources with zero
missing required inputs. The completion profile remains 374 resources / 2,532
unique retail files and now has SHA-256
`cc5af254e0787cf135bd1cf8574b94dd19991741a6eda6ccc346aa304b78c588`.
This is source/converter-closure proof, not a claim that the runtime renderer
callbacks or external-movie target attachment are complete.

---

<!-- merged from docs/RETAIL_HUD_HOST_BRIDGE.md -->

## Retail HUD host-bridge oracle

This document records the bounded host bridge required by the BFME2 1.06
Men/Fords Palantir closure. It is an oracle and API proposal only. It does not
enable arbitrary ActionScript, FSCommand, WND callbacks, URLs, or native
renderer callbacks.

### Exact closure

The deterministic private contract accounts for every remaining bridge item:

- 17 blocked ActionScript programs, each with source offset, byte-range hash,
  exact strings, functions, state paths, host calls, and a mapping or precise
  unresolved reason;
- six unique clip-action programs expanded to 28 target-specific events: 27
  initialize events and one unload event;
- 87 `controlbar.wnd` controls and all 21 distinct non-null callback
  identities, including the callback kind and exact named control for every
  binding;
- five exact privileged render callbacks: `AptPalantir::ClipRadar`,
  `RenderGlobe`, `RenderMovie`, `RenderRadar`, and `RenderRadarViewBox`.

The payload-free contract is generated at
`.private/scratch/hud-host-bridge/contract-a.json`. A second generation is
byte-identical. Retail bytes remain under `.private`.

### Script mapping

| Programs | Exact responsibility | Proposed binding |
|---|---|---|
| `ingamesidecommandbar:3392`, `:6264`, `:6272`, `:6368`, `:7296` | Side-bar functions, 15 button states, neighbor state, load/unload notifications | Exact local clip operations plus allowlisted lifecycle calls |
| `libingameui:37332` | `CreateContent` / `DeleteContent`, placeholder sizing, parent frame lifecycle | Exact converted-movie allowlist; dynamic unknown content fails closed |
| `palantir:152912`, `:333872`, `:334840` | `MinLOD` stop/visibility behavior | Deterministic configured HUD state |
| `palantir:167296`, `:169224`, `:169256` | Skill buttons, six command-child lifecycle callbacks, overlays/glass | Exact clip methods/properties and callback argument schemas |
| `palantir:332504` | Resource flash and `Gui_PalantirResourceBarFlash` | Cataloged retail audio-event adapter |
| `palantir:95848`, `:95856`, `:95872` | Globe/command/rank/resources/radar/button facade and initial setup | Deterministic game/HUD state; radar projection remains typed and unresolved |
| `palantir:95864` | `GameCode`, `PlaySound`, and five external HUD movie loads | Closed FSCommand and exact movie-target allowlists |

The full function lists and every exact string/property name are retained in
the generated contract rather than summarized away here.

### Exact host arguments

The bridge must preserve the authored argument expressions:

- Side bar load/unload: `GetFullName(this)`.
- Side button loaded:
  `index=this._name.substr(namePrefixLength)&name=clip.toString()`.
- Side button unloaded: `index=this._name.substr(namePrefixLength)`.
- Palantir frame loaded: `index=clip._name&name=clip.toString()`; unloaded:
  `index=clip._name`.
- Submenu loaded: `clip._name.substr(7)&name=clip.toString()`; unloaded:
  `clip._name.substr(7)`.
- Toggle-flash loaded: `clip._name.substr(11)&name=clip.toString()`; unloaded:
  `clip._name.substr(11)`.
- The nine exact `AptPalantir` lifecycle calls receive `clip.toString()`,
  except `OnInitialized`, which receives the empty string.
- `PlaySound` receives a retail audio event ID. The proven direct call in this
  closure is `Gui_PalantirResourceBarFlash`.

No generic callback name, query string, or argument coercion is permitted.

### Clip-event ordering

All 27 initialize records carry the converter-proven dispatch point
`after-target-create-and-name-before-display-list-insert`. The 12 repeated
side-frame events define `SetFlashEffectState`; 12 nested frame events set
Flash property 7 (`_visible`) false; three Palantir text clips bind
`$PalantirResources`, `$PalantirResourceMultiplier`, and
`$PalantirCommandPoints` to `stringName`.

The one unload record targets `layer:1:Palantir/40` and sets `_type` to
`AptPalantir::RenderMovie`. Its retail timing relative to display-list removal
is not proven by the static contract. A runtime implementation must keep this
as a gate instead of choosing an order silently.

### WND boundary

`controlbar.wnd` contains names, control types, rectangles, styles, status,
and callback identities. It does not author numeric command IDs. Therefore the
named control is the only source-proven control identity; numeric IDs must not
be synthesized.

The 21 callback identities include control-bar system/input, observer and
beacon handlers, default input/system/tooltip, pass-to-parent, and the W3D draw
callbacks. Local OpenSAGE code is useful observation evidence but does not
close this gate: its available `ControlBarSystem` implementation is explicitly
Generals-specific, several BFME Palantir callbacks are stubs, and the renderer
callbacks require engine state. WND dispatch remains typed and fail-closed.

More importantly, WND activation itself is not proven for the BFME2 skirmish
HUD. The retail INI closure statically names `controlbar.wnd` controls, so the
file is not random payload noise. However, the local OpenSAGE BFME2 definition
selects `AptControlBarSource`, whose `AddToScene` loads `Palantir.apt`; there is
no `controlbar.wnd` load reference in its BFME/BFME2 source paths. Under the
project's delete-before-automate rule, the 21-callback WND implementation is
therefore deferred as candidate-dead for this vertical slice until a retail
runtime trace proves the WND is active. The complete callback map remains in
the oracle so it can be restored without rediscovery if that trace proves it
necessary.

### Minimal runtime surface

The proposed Godot boundary has eight operations: exact state read, exact clip
property write, exact clip method call, lifecycle notification, retail audio
event playback, converted HUD movie load, typed renderer binding, and typed WND
message dispatch. Each operation accepts an allowlisted identity and typed
arguments; there is no eval, reflection, arbitrary URL, filesystem path, or
generic native callback.

Godot can deterministically supply faction/alignment, selection and command
set, resources, command points, button state, exact clip lifecycle, and the
configured minimum-LOD flag. The remaining gates are:

1. typed implementations of the five `AptPalantir` render callbacks;
2. verified converted closure for the five external HUD movies and their exact
   targets;
3. authoritative world-to-radar projection and view-box state;
4. BFME2-specific semantics for the 21 WND callback identities;
5. a runtime oracle for the unload/removal ordering.

### Verification

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_host_bridge_oracle.py -q
python -m ruff check importer/openbfme_importer/retail_hud_host_bridge_oracle.py importer/tests/test_retail_hud_host_bridge_oracle.py
```

The focused gate is three passing tests plus clean Ruff. Two private CLI runs
must produce byte-identical contracts; the byte count and aggregate are
recorded in the private report.

---

<!-- merged from docs/RETAIL_HUD_TYPED_INITIALIZE.md -->

## Retail HUD typed initialize effects

This slice closes only the 24 source-proven initialize events identified by
the BFME2 1.06 Palantir host-bridge oracle. It does not add an ActionScript VM,
general property access, reflection, host callbacks, or guessed lifecycle
ordering.

### Exact executable closure

Two unique retail byte programs are allowlisted:

| Program | Retail bytes | Repeated events | Typed effect |
|---|---:|---:|---|
| `ingamesidecommandbar:clip-event:13680` | 59 bytes, SHA-256 `782d8458e3a04ea8fc4a0563665053035b92d6bfd14e978f6e4b6d1f72873fbc` | 12 | Define local `SetFlashEffectState(state)` as the exact ancestor-indexed `flashEffects[_name].gotoAndPlay(state)` binding. |
| `libingameui:clip-event:56252` | 13 bytes, SHA-256 `0e6307bff26d6ffca7483353a04501e18d7acc5ee2dc60a50e5a75160ae81bb6` | 12 | Set Flash property 7 (`_visible`) on the empty-string/current target to `false`. |

Every event retains its exact record hash, target path, target clip identity,
program byte range, decoded instructions, and dispatch point:
`after-target-create-and-name-before-display-list-insert`.

The Godot runtime validates the exact program ID, movie, offsets, byte length,
SHA-256, and complete typed effect shape before accepting either program. It
then writes only bounded target state:

- `localMethods.SetFlashEffectState` receives the typed method body; it is data,
  not evaluated code.
- `_visible` becomes `false` for the property-7 program.

The three Palantir text initialization programs and the one unload/lifecycle
program remain blocked. No work from their separate oracles is folded into
this slice.

### Measured private result

The planner-backed conversion is byte-identical across two runs:

- contract file SHA-256: `22d2ad2ae72018cb7e97044dc9fd5a2438dd4d250934dcadd617659dd56d1766`
- contract aggregate: `7be22993067f5a88f11e1147051eae0f60b88ef20741237bc846b676ce536e12`
- blockers: 28 (the planner path does not add the WND observation blocker)

The sealed 189-source bundle conversion is also byte-identical:

- scene-contract file SHA-256: `3b1468f6b24cd06779d61efd44479cf9518806b995e04663f971aad2cdbe7016`
- contract aggregate: `5c848017922533e6ef7e90339066bbf27e88d0deae42564fb107728d3ad6018b`
- blockers: 29
- clip programs: 6 total, 2 executable
- clip events: 28 total, 24 executable, 4 blocked

Private artifacts live under `.private/scratch/hud-typed-initialize` and remain
gitignored.

### Verification

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_apt_convert.py -q
python -m ruff check importer/openbfme_importer/retail_hud_apt_convert.py importer/tests/test_retail_hud_apt_convert.py

& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' `
  --headless --path game --script res://tests/retail_hud_apt_runtime_runner.gd

$contract = (Resolve-Path '.private\scratch\hud-typed-initialize\bundle-a\data\ui\palantir\scene-contract.json').Path
$pack = (Resolve-Path '.private\scratch\hud-typed-initialize\bundle-a').Path
& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' `
  --headless --path game --script res://tests/retail_hud_apt_runtime_runner.gd -- `
  "--retail-hud-apt-contract=$contract" "--retail-hud-apt-pack-root=$pack"

& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' `
  --headless --path game --editor --quit
```

The private runtime gate also mutates the allowlisted byte hash and the typed
property shape independently and proves that both changes fail closed.

---

<!-- merged from docs/RETAIL_HUD_LIVE_TEXT_RUNTIME.md -->

## Retail Palantir live-text runtime

The bounded HUD converter and Godot runtime now execute the three BFME2 1.06 Palantir live-text bindings with the exact copied Albertus MT retail font. This is a usable private implementation, not a rendered-parity claim: one mandatory blocker retains all seven opaque font-backend/GPU capture gates.

### Exact live values

| APT variable | Runtime inputs | Exact formatting |
| --- | --- | --- |
| `$PalantirResources` | `resources` | decimal integer; one space when negative |
| `$PalantirResourceMultiplier` | `resourceMultiplier` | `x%g`; one space when exactly `1.0` |
| `$PalantirCommandPoints` | current and cap | `current/cap`; current only when cap is negative; one space when current is negative |

`RetailHud.set_resources()` forwards resources, current command points, cap, and an explicit normal-slice multiplier of `1.0`. Placeholders `999999`, `x99`, and `999/999` are never runtime fallbacks. Missing targets and non-finite multipliers fail closed.

The byte-identity-bound initialize programs are:

- `palantir:clip-event:375628` → `$PalantirResources`
- `palantir:clip-event:375640` → `$PalantirResourceMultiplier`
- `palantir:clip-event:375652` → `$PalantirCommandPoints`

Together with the two existing typed initialize programs, this produces 5 supported clip programs out of 6 and 27 executable initialize events out of 28. The one remaining event is the exact unload lifecycle blocker.

### Exact font containment

The converter resolves `albertusmt.otf` beside the sealed 261-source APT tree without adding it to that already-sealed source aggregate. It verifies 24,712 bytes and SHA-256 `6a1990e17f14ce5be199dde10f56dac3efd66aaa8e91d46119952cf55a9d9ba0`, then copies it to:

`assets/ui/palantir/fonts/albertusmt-6a1990e17f14.otf`

Godot resolves that contained path through the mounted pack, verifies the same length and SHA-256, and loads it with `FontFile.load_dynamic_font`. The contract additionally pins Albertus MT Regular / `AlbertusMT`, CFF, 1,000 units per em, 298 glyphs, and zero bitmap strikes. There is no Arial, placeholder, or synthetic-glyph fallback.

### Layout and display order

The converter assigns one traversal-derived `displayOrder` across both triangles and text leaves. The runtime validates uniqueness, merges the inventories, and renders in that order. For the selected retail frame the exact traversal produces 31 items: triangle orders 0–27 followed by resource, multiplier, and command-point text orders 28, 29, and 30. This ordering follows the selected APT display-list depths; it is not a runtime append policy.

The runtime preserves the statically proven authored bounds, transforms, color, alignment mapping (`0=right`, `1=center`, `2=left`), vertical-center calculation, and integer truncation. Font size, glyph baseline, hinting, final blend, ancestor clipping, final composite pixels, and live backend font winner still require rendered evidence.

### Mandatory capture blocker

Exactly one `text-rendered-parity-capture-not-passed` blocker remains. It contains all seven required retail-vs-Godot gates:

1. Font-size device mapping.
2. Baseline and glyph origin.
3. Antialiasing and CFF hinting.
4. Final color, alpha blend, and gamma.
5. Ancestor clipping.
6. Final composite order pixels.
7. Runtime font winner.

The production contract therefore has 25 blockers and keeps `parityReady=false`. Removing, duplicating, weakening, or marking this blocker passed is rejected by the runtime.

### Verified result

- Source closure: 261 files, 10,700,284 bytes, unchanged aggregate.
- Output closure: 26 files: 24 atlases, one exact OTF, and one scene contract.
- Contract aggregate: `b5b3760858a51cb478ce4f9f26d68e4c71fcc24d66ea74c99e37245d93b1b794`.
- Serialized contract SHA-256: `3666f4732f2e2a211e5ee4cc7975399469573011c2ebfb7660755ecf98969ecd`.
- A/B bundle identity SHA-256: `478d5e6282650c46fc820e8490e7c19636fe8b4ad4e838caef5e710e27f2b52e`.
- Converter tests: 20 passed; Ruff clean.
- Godot legal runtime: 43/43.
- Godot private runtime: 70/70.
- Four-unit HUD forwarding: 61/61.
- Godot 4.7 editor compile: clean.


---

<!-- merged from docs/RETAIL_HUD_RESOURCE_FLASH_RUNTIME.md -->

## Retail HUD resource-flash runtime

The bounded HUD converter and Godot runtime execute the exact BFME2 1.06
Palantir resource-flash entry without adding a general ActionScript VM.

The only public runtime input is `PlayCommandPointEffect()`. It rewinds the one
placed `CommandPointsFlash` instance (`palantir:309`) to label `_go`, frame 8,
then executes the byte-identity-bound `palantir:332504` entry. That 26-byte
program first plays the current timeline and then exposes one exact audio-event
intent for `Gui_PalantirResourceBarFlash` through `FSCommand:PlaySound`.
Retriggering rewinds the same visual instance and emits another intent; it does
not allocate a parallel visual effect.

The runtime deliberately does not auto-trigger from resources or command-point
values. The two stripped native counter aliases that reach
`PlayCommandPointEffect()` have not been named dynamically. It also does not
invent coalescing, overlap, or voice-stealing behavior for repeated audio
requests. Those unknowns replace the former generic unsupported-action blocker
with two narrow capture blockers:

- `resource-flash-native-trigger-capture-not-passed`
- `resource-flash-mixer-overlap-capture-not-passed`

Static identities retained in the generated contract include:

- entry script SHA-256
  `0b966556e6fc10d1eaa5c129999f31e185b634425298b7bdaf21b6dd26aeb999`
- trigger-body SHA-256
  `a5b9a91b9ad21d12bced1a7d9f94c803d2abbb5fe542646356fdc90663f47788`
- 58-frame timeline SHA-256
  `f2254f867b5f59070284fd2f028d5f4e4d787f09af9f59220491559053b069d6`
- placement-record SHA-256
  `6673eea4c330f20d073788d1f1bc36f50ba4b456a73a7ff1e40477da6b93c527`
- audio leaf SHA-256
  `f2d3aff531ecfd3616069d53551823f92aee92f009382d3bf39d4ec8e2eca350`

The conversion changes the production inventory from 60 to 61 supported
ActionScripts and from 14 to 13 unsupported ActionScripts. Because one generic
blocker is replaced by two evidence gates, the production blocker total changes
from 22 to 23. This is a more precise fail-closed contract, not a parity claim.

Verification uses a freshly generated deterministic A/B private bundle, the
focused converter tests, the legal-safe Godot runner, the private-contract
runner, and a headless Godot editor compile. Private payload stays under
`.private/scratch/hud-resource-flash-runtime`.

---

<!-- merged from docs/RETAIL_HUD_SIDE_COMMAND_RUNTIME.md -->

## Retail HUD side-command runtime

This slice binds three byte-exact `InGameSideCommandBar` ActionScript programs
to a typed Godot topology adapter. It does not introduce a general
ActionScript virtual machine.

### Supported programs

- `ingamesidecommandbar:6272` updates adjacent button frames in authored
  `next`, then `prior` order.
- `ingamesidecommandbar:6368` updates the current frame, then adjacent frames.
- `ingamesidecommandbar:7296` requires the boolean `InGame` input and, when it
  is true, sends `_show` to `Button1` through `Button15` in ascending order.

Support is conditional on the exact retail APT hash, the three program hashes,
all six helper-body hashes, the `Button0` through `Button11` local placement
topology, and the authored sprite-18 frame records. A mismatch falls back to
the existing unsupported-opcode path during conversion or fails contract
validation in Godot.

### Runtime semantics

`make_side_command_state()` creates the sealed twelve-button local topology.
Executing program 7296 performs each existing button's frame-10 placements
before its queued program 6368 effect. `Frame` is placed before `ButtonGlass`,
then the label is selected from the helper truth table:

| Next frame | Prior frame | Label |
| --- | --- | --- |
| absent | absent | `_topbottom` |
| present | absent | `_top` |
| absent | present | `_bottom` |
| present | present | `_middle` |

The local topology has no `Button12` through `Button15`. Their four authored
calls remain in the dispatch order and are consumed as no-ops, matching the
sealed native undefined-receiver behavior. `unresolvedRuntimeTraceCount` is
therefore zero.

### Conversion contract

The converter emits `sideCommandTopology`, sets
`renderPolicy.exactSideCommandTopologyBound`, and reports
`summary.typedSideCommandActionScriptCount = 3`. Those three scripts move from
unsupported to supported; the helper-library script at source offset 6264
remains blocked because it is evidence for the typed adapter, not executable
general bytecode.

For the private 261-source retail closure this changes the ActionScript totals
from 61 supported / 13 unsupported to 64 supported / 10 unsupported and
removes exactly three `action-script-unsupported-opcodes` blockers. Other HUD
blockers, including resource-flash and external-attachment gates, are unchanged.

### Acceptance

Run the focused converter test and Ruff checks, generate two fresh private
bundles, compare their bytes and hashes, then pass each bundle through
`game/tests/retail_hud_apt_runtime_runner.gd`. Finish with a headless Godot
editor import/compile check. Retail inputs and converted outputs stay under
`.private`.

---

<!-- merged from docs/RETAIL_HUD_PALANTIR_COMMAND_RUNTIME.md -->

## Retail Palantir command registration runtime

The private retail HUD converter recognizes exactly two byte-identical
`Palantir.apt` ActionScript programs:

- `palantir:169224` declares six lifecycle callbacks.
- `palantir:169256` loops over numeric button frames `0` through `5` and
  installs three local methods on each frame.

Both programs are registration-only. Running either program does not invoke a
registered callback or method. The converter does not contain a generic
ActionScript virtual machine; any source hash, instruction shape, function
body, constant pool, operand, or authored placement change returns the program
to the existing unsupported-opcode blocker.

### Runtime contract

`palantir:169224` registers these functions in authored order:

1. `OnMovieClipFrameLoaded`
2. `OnMovieClipFrameUnloaded`
3. `OnCommandButtonSubMenuLoaded`
4. `OnCommandButtonSubMenuUnloaded`
5. `OnCommandButtonToggleFlashLoaded`
6. `OnCommandButtonToggleFlashUnloaded`

The contract preserves the exact native host intent and argument expression
for each function, but it does not dispatch those host calls during
registration. Native retained-slot and converted clip-handle ownership remain
behind the `command-child-lifecycle-host-result` retail trace gate.

`palantir:169256` registers, for buttons `0`, `1`, `2`, `3`, `4`, and `5`, in
that order:

1. `SetAutoAbilityOverlayState(state)` targets
   `this._parent._parent.AutoAbilityOverlays[this._name]`.
2. `SetFlashEffectState(state)` targets
   `this._parent.FlashEffects[this._name]`.
3. `SetGlassState(state)` targets
   `this._parent['glass' + this._name]`.

Each method performs the sealed `target.gotoAndPlay(state)` dispatch. The
runtime exposes only those three typed dispatches.

### Placement and scheduling

The `palantirCommandTopology` contract binds character `114` (`CommandButtons`)
and its exact `_hide = 0`, `_show = 9` labels, imports, root placements, numeric
button frames, glass targets, flash-effect children, and auto-ability overlay
children. The raw show-frame action record appears before the placement records
in the APT source. Retail scheduling defers that action until after the frame
record traversal, so the Godot runtime applies all authored frame placements
before executing `palantir:169256`.

### Deliberate blocker

`palantir:167296` remains unsupported. Its known call order is not enough to
execute it because the effects of `_root.UpdateSkillUpgradeButton` and
`_root.SetCommandButtonState` are absent from the converted APT closure and the
native name evidence. The retained gate is
`skill-upgrade-root-method-effects`; resolve it with a retail trace of one
`CommandUI` `_show` entry while `InGame` is true. Do not replace it with guessed
root mutations.

### Verification

Use the focused importer test and the HUD runtime runner. Fresh private A/B
conversion outputs must be byte-identical. The expected delta is two fewer
`action-script-unsupported-opcodes` blockers: 66 of 74 ActionScript programs
supported, 8 unsupported, and 20 total production blockers when the exact WND
companion is present.

---

<!-- merged from docs/RETAIL_HUD_MEN_FORDS_FADEIN_RUNTIME.md -->

## Retail Men/Fords side-command FadeIn runtime

The private HUD now binds the exact BFME2 1.06 `InGameSideCommandBar.FadeIn`
route to the live Men/Fords selection state. This is a typed adapter, not a
general ActionScript virtual machine or generic timeline player.

### Converted contract

The converter emits `sideCommandFadeRuntime` only after validating:

- `InGameSideCommandBar.apt` SHA-256
  `84d58c67c5cab9a3bf690125cbf1a0cbf3f4bc58ccc29ffa33b992a924eca6ef`;
- FadeIn body `[8836,9009)`, completion program `[9404,10086)`, and settled
  stop `[10088,10090)` by exact byte hash;
- the retail `commandset.ini`, `commandbutton.ini`, and six Men object INIs;
- all four battalion and five structure object-to-command-set declarations;
- a nonempty authored `InPalantir=Yes` command closure for every selection;
- shared `OK_FOR_MULTI_SELECT` Toggle Stance, Attack Move, and Stop commands
  for mixed battalion selection.

The nine selectors are Soldier, Tower Guard, Archer, and Knight battalions,
plus Fortress, Farm, Barracks, Archery Range, and Stable structures.

### Live typed input

`RetailVerticalSlice._refresh_hud()` passes the existing state without a second
selection model:

- sorted `simulation.selected_ids`;
- mutually exclusive `selected_structure_id`;
- `simulation.entities` and `simulation.structures` rows;
- `simulation.winner` and fixed local team 0.

`RetailHud` forwards that context to `RetailHudAptRuntime`. The evaluator
requires positive unique sorted IDs, exclusive selection kinds, a living local
row, `winner == -1`, a declared roster selector, and at least one authored
eligible command. Empty, enemy, dead, post-match, and outside-roster selections
do not dispatch FadeIn. Malformed input fails closed and hides the private APT
surface.

### Exact state and frame behavior

The loaded side-command movie starts in native state 1 at one-based frame 1.
On the first eligible selection outside native states 2 and 3:

1. Current frames outside `[32,42)` target frame 12.
2. Current frames inside `[32,42)` target `12 + 42 - currentframe`, reversing
   an in-progress FadeOut. The sealed examples are `32 -> 22`, `37 -> 17`, and
   `41 -> 13`; boundary frames 31 and 42 target 12.
3. Dispatch writes native state 2 and advances at the authored 33 ms interval.
4. Frame 22 invokes `OnAptInGameSideCommandBarFadeInComplete`, changing state
   2 to settled-visible state 3.
5. Playback continues through the unchanged settled frames and stops on frame
   31.

No FadeOut behavior is guessed. The generic `timeline-playback-not-bound`
blocker and the `palantir-nondefault-frame-selection-not-bound` blocker remain.
The broad `side-command-bar-fade-runtime-not-bound` blocker is removed. The
single `side-command-native-row-alias-trace` gate remains metadata-only: it
blocks a claim about exact native field aliases, not this typed implementation.

### Acceptance

- Focused converter tests and Ruff pass.
- The legal-safe fixture HUD runner passes 67 checks.
- Each fresh private A/B bundle passes 129 runtime checks sequentially.
- A/B scene contracts have the same aggregate hash.
- Godot headless editor import/compile passes after the live HUD binding.

The production contract has 19 blockers, 66 supported ActionScript programs,
8 unsupported programs, and one typed Men/Fords side FadeIn runtime.

---

<!-- merged from docs/RETAIL_HUD_EXTERNAL_ATTACHMENT_RUNTIME.md -->

## Retail HUD external attachment runtime

The 261-source retail HUD converter now binds the four source-proven external
movies as replacements for the exact authored Palantir frame-0 child slots.
They are not independent HUD roots and do not require a general ActionScript
VM. `InGameSideCommandBar` remains the existing root-bound layer.

### Exact child-slot contract

All four placeholders are Palantir character 41. The runtime creates direct
`RetailHudAptRuntime` children with the authored name, depth, transform, and
identity color transform (`tint=[1,1,1,1]`, `additive=[0,0,0,0]`). It stages
all four before adding any child, so a malformed or colliding slot binds none.

| Load | Movie and typed interface | Exact path | Depth | Matrix; translation | Root frames, labels, initial stop | Default |
| ---: | --- | --- | ---: | --- | --- | --- |
| 0 | `InGameSpellBook` / `RetailSpellBookSlot` | `Palantir.root.frame0/SpellBookUI` | 3 | `[0.9998626708984375,0,0,0.9999847412109375]`; `(0,0)` | 18; `_hide=0`, `_show=9`; 8 | hidden, dormant |
| 2 | `InGameHelpBox` / `RetailHelpBoxSlot` | `Palantir.root.frame0/helpBox` | 176 | identity; `(585,607)` | 1; no root labels; 0 | hidden, dormant |
| 3 | `InGameHeroSelect` / `RetailHeroSelectSlot` | `Palantir.root.frame0/HeroSelectUI` | 174 | identity; `(375,700)` | 29; `_hide=0`, `_fadein=9`, `_show=19`; 8 | hidden pending captured show result |
| 4 | `InGamePlanningMode` / `RetailPlanningModeSlot` | `Palantir.root.frame0/planningModeUI` | 180 | identity; `(512,30)` | 27; `_init=0`, `_open=9`, `_close=19`; 8 | hidden, closed and dormant |

Initial setup remains the source order SpellBook, SideCommandBar, HelpBox,
HeroSelect, PlanningMode. The typed callback pairs are respectively
`OnAptInGameSpellBookLoaded/Unloaded`,
`AptPalantir::OnHelpBoxLoaded/Unloaded`,
`AptPalantir::OnHeroSelectLoaded/Unloaded`, and
`AptPalantir::OnPlanningModeUILoaded/Unloaded`. The converter records them but
the runtime does not dispatch them until lifecycle capture is complete.

Native evidence remains explicit: HeroSelect, HelpBox, and PlanningMode use
owner handles `+0xC4`, `+0xC8`, and `+0xCC`; the proven native reset order is
HeroSelect -> HelpBox -> PlanningMode. SpellBook is a separate FSCommand path.
Reset removes all four direct children atomically without guessing asynchronous
completion or unload order. The HeroSelect flags-`0xB6` null clip-action pointer
at source offset 166756 remains a separate diagnostic and is never fabricated
into a callback.

### Remaining blocker

The former four `external-movie-target-attachment-not-bound` blockers are gone.
Exactly one `external-movie-lifecycle-capture-not-passed` blocker remains and
carries these four unresolved retail traces:

- completion order for the five no-wait APT loads;
- HeroSelect visibility immediately around `ShowHeroSelectInterface()`;
- unload callback and child-removal order during Palantir teardown;
- HelpBox clip and alternate-anchor runtime coordinates.

No HeroSelect show state, asynchronous completion order, or unload order is
guessed. WND, timeline playback/selection, side-bar fade, rendered-text capture,
and the other existing bounded blockers are unchanged. Production counts remain
74 action scripts (60 supported), 22 timelines / 640 frames / 51 instances, 28
draws / 31 display items, and 28 clip actions / 28 events (27 executable). The
blocker count is now 22.

### Deterministic evidence and verification

Private A/B bundles are under
`.private/scratch/hud-external-attachment-runtime/bundle-a` and `bundle-b`.
Both contain 26 byte-identical outputs from 261 inputs. Their contract aggregate
SHA-256 is
`910238c83b07269b2637bb4bfb9247c14407d781f632e61673400c0eb2d2fa6b`;
the canonical source aggregate is
`f62347fb78065726715618ed9c73f152c678fec5646ddf7b0855825d1cb23599`.

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_apt_convert.py -q
python -m ruff check importer/openbfme_importer/retail_hud_apt_convert.py importer/tests/test_retail_hud_apt_convert.py
python -m ruff format --check importer/openbfme_importer/retail_hud_apt_convert.py importer/tests/test_retail_hud_apt_convert.py

& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' --headless --path game --script res://tests/retail_hud_apt_runtime_runner.gd
& 'C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe' --headless --editor --path game --quit
```

The private runtime gate uses the same runner plus
`--retail-hud-apt-contract=<bundle-a contract>` and
`--retail-hud-apt-pack-root=<bundle-a root>` after Godot's `--` separator.

---

<!-- merged from docs/RETAIL_HUD_WND_RUNTIME.md -->

## Retail HUD WND companion runtime

`retail_hud_wnd_runtime.gd` is an isolated, typed companion for the active
BFME2 1.06 `ControlBar.wnd`. It is not bound into `retail_hud.gd` or the Apt
runtime and does not claim the WND blocker closed.

The runtime validates all 21 callback identities and every exact callback-to-
control binding before executing anything. It implements only four statically
complete boundaries:

- `ControlBarInput`: always unhandled, no effects;
- `GameWinBlockInput`: exact `0x15`/`0x18` returns and atomic five-call service
  batch for message `0x06`;
- `PassSelectedButtonsToParentSystem`: forwards only the six proven messages
  through explicit parent-resolver and forwarder callables;
- `W3DNoDraw`: returns no commands and performs no effects.

The other 17 identities remain named typed blockers. There is no execution by
callback string, generic callable dictionary, fallback image, OpenSAGE or
Generals substitution, or partial mutation when validation/service commit
fails.

Legal-safe verification:

```powershell
Godot_v4.7-stable_win64_console.exe --headless --path game --script res://tests/retail_hud_wnd_runtime_runner.gd
```

An optional private check can append:

```text
-- --retail-hud-wnd-contract=C:\...\hud-wnd-callback-oracle\contract-a.json
```

That check reads but never edits the private oracle contract.

---

<!-- merged from docs/RETAIL_HUD_WND_DRAW_RUNTIME.md -->

## Retail HUD WND draw runtime

`RetailHudWndRuntime` now validates the hash-pinned BFME2 1.06 WND draw oracle
and exposes typed command emitters for all ten sealed draw callbacks. It does
not render, resolve live game services, synthesize images, or bind the WND into
the live HUD.

Implemented draw callbacks:

- `W3DCommandBarBackgroundDraw`
- `W3DCommandBarForegroundDraw`
- `W3DCommandBarGenExpDraw`
- `W3DCommandBarGridDraw`
- `W3DCommandBarTopDraw`
- `W3DGadgetPushButtonImageDraw`
- `W3DLeftHUDDraw`
- `W3DNoDraw`
- `W3DPowerDraw`
- `W3DRightHUDDraw`

Every entry requires both the exact callback-control binding contract and the
draw-semantics contract. The latter pins all ten handler ranges and SHA-256
identities, authored control bindings, image literals, and ordered semantics.
Malformed controls, non-finite rectangles, over-four-cell grid state, missing
branch inputs, changed hashes, or a changed contract aggregate fail closed.

The emitters return ordered command dictionaries with
`renderingCommitted = false`. Exact retail addresses and vslots are preserved,
including the distinct background/foreground pairs, grid cell helper, three
push-button branches, radar service boundary, power image vslots, and the
RightHUD default-helper branch. `W3DNoDraw` remains a strict empty command list.

Seven draw handlers still carry bounded dynamic gates:

- background blend and parameter aliases;
- foreground mapped-image indirection;
- general-experience progress-source and blend aliases;
- grid cell semantic names and material parameters;
- top-tail draw parameters;
- left-HUD radar object plus blend/clipping structures;
- power counters and image blend/state aliases.

`W3DGadgetPushButtonImageDraw`, `W3DRightHUDDraw`, and `W3DNoDraw` have no
remaining draw-oracle dynamic gate. These gates do not prevent exact typed
command emission, but they do prevent claiming rendered parity or binding a
guessed rendering service.

The runtime now implements 13 of the 21 callback identities: the ten draw
callbacks plus the three existing typed message callbacks. Eight identities
remain unimplemented:

- `BeaconWindowInput`
- `ControlBarObserverSystem`
- `ControlBarSystem`
- `GameWinDefaultInput`
- `GameWinDefaultSystem`
- `GameWinDefaultTooltip`
- `LeftHUDInput`
- `W3DGameWinDefaultDraw`

The private oracle contract remains under `.private`; no retail payload or
contract was copied into the public tree.

---

<!-- merged from docs/RETAIL_HUD_WND_MESSAGE_RUNTIME.md -->

## Retail HUD WND message runtime

`RetailHudWndRuntime` now validates the exact BFME2 1.06 WND message-semantics
oracle and exposes typed methods for all five callbacks declared necessary for
the Men-versus-Men slice:

- `ControlBarInput`
- `GameWinBlockInput`
- `PassSelectedButtonsToParentSystem`
- `LeftHUDInput`
- `ControlBarSystem`

The first three existing implementations are preserved. `LeftHUDInput` and
`ControlBarSystem` now follow their hash-pinned predicate and return-state
boundaries and emit ordered, address-pinned effect dictionaries for the opaque
branches. They do not use generic callback dispatch or assign guessed semantic
names to stripped retail types, controls, camera branches, or services.

The runtime requires both the exact 21-callback identity contract and the
five-handler message contract. It validates the private message-contract
aggregate, handler address ranges and SHA-256 values, typed interfaces, return
contracts, control bindings, state/dependency inventories, and unresolved-gate
counts. Changed handlers and malformed dynamic branch inputs fail closed.

### Current slice status

All five declared Men-versus-Men message callbacks have typed implementations.
The required-unimplemented count for this slice is therefore zero. This does
not mean every opaque semantic alias or retail service has been recovered.

The full WND runtime now implements 15 of 21 callback identities: ten sealed
draw callbacks and five slice-required message callbacks.

Two descriptor-known callbacks are retained outside the current slice rather
than being counted as Men-versus-Men requirements:

- `BeaconWindowInput`: event-dormant
- `ControlBarObserverSystem`: observer-only, outside declared player-versus-player
  play

Four runtime built-ins remain unresolved and are not claimed:

- `GameWinDefaultInput`
- `GameWinDefaultSystem`
- `GameWinDefaultTooltip`
- `W3DGameWinDefaultDraw`

### Remaining message alias and capture gates

`GameWinBlockInput` retains the stripped class names behind globals
`0x00e03220`, `0x00dfea3c`, and `0x00dfedf0`; the implementation uses only the
exact addresses and vslots.

`LeftHUDInput` retains three gates:

- semantic aliases for object field `+0x14` values
  `{0x0a,0x18,0x20,0x26}`;
- the selected-object branch choosing command ID `0x42f` or `0x430`;
- aliased selection/camera branches shared by messages `0x0011`, `0x0012`, and
  `0x0018`.

`ControlBarSystem` retains three gates:

- semantic names of the eight cached control handles;
- matched-service aliases for message `0x4031`;
- the message `0x400b` cached-control rejection and selected-button fallback
  branches. Their retail ordering is retained explicitly.

No generic dispatch, live HUD binding, or guessed built-in behavior is included.
Private oracle contracts remain under `.private`.

---

<!-- merged from docs/RETAIL_HUD_WND_PRODUCTION_BINDING.md -->

## Retail HUD WND production binding

The production HUD bundle now embeds a compact, typed companion for the exact
active BFME2 `window/controlbar.wnd`. The companion is derived during the same
transaction as the APT scene contract, and the Godot APT runtime owns the WND
runtime only after the complete scene contract validates. A rejected companion
leaves no partially configured WND runtime behind.

### Frozen identity

- WND SHA-256: `a509730457224a111af8022df6d0ef373fcaa5d91a102bc15bccf5fc1a54ced6`
- Callback oracle aggregate: `ad97b6c02ed6a46eec745adda4434264b84dcc969b7c46115f6a8a6458d33662`
- Message oracle aggregate: `238e9de43c8ebae4a22de1f7b04c4ced3933dbe3328c83ffa44d805b5336274c`
- Draw oracle aggregate: `748ad63a218497f9ff9565b1b8078a165c90fd75dc7d39335d46a6edd4f3c484`
- Frozen closure: 87 windows and 21 callback identities

The compact companion carries every exact callback-to-control binding. It
declares 15 typed local callbacks, including all five Men-versus-Men message
callbacks and all ten sealed draw callbacks. `BeaconWindowInput` and
`ControlBarObserverSystem` remain explicitly outside the declared slice. The
four unresolved engine built-ins remain unimplemented:

- `GameWinDefaultInput`
- `GameWinDefaultSystem`
- `GameWinDefaultTooltip`
- `W3DGameWinDefaultDraw`

### Blocker delta

The former broad `wnd-layout-callbacks-not-bound` blocker is removed. It is
replaced by three independently validated blockers:

1. `wnd-unresolved-runtime-builtins-not-bound` for the four callbacks above.
2. `wnd-dynamic-draw-service-capture-not-passed` for seven named draw/service
   gates.
3. `wnd-live-dispatch-render-services-not-bound` for the seven named message
   alias gates and the absent live dispatch/render services.

That changes the production contract from 20 to 22 blockers: one broad blocker
was removed and three narrower blockers were added. The plan-only conversion
still has 19 blockers because it does not have the exact active WND source and
therefore does not claim a production companion.

The binding does not invent a generic callback dispatcher, live render service,
fallback visual, or native service alias. All four live-binding flags remain
false and are validated fail closed by both runtimes.

### Verification

The fresh private A/B output under
`.private/scratch/hud-wnd-production-binding` is byte-identical across all 26
files.

- Production scene-contract aggregate: `da2df9b410a6dded1ba247091a7a3cf8277ad96712e3130d396e18588a055b07`
- Production source aggregate: `f62347fb78065726715618ed9c73f152c678fec5646ddf7b0855825d1cb23599`
- Production action subset: 64 supported of 74 total
- Production WND subset: 15 typed of 21 total, 5 of 5 required message
  callbacks, 4 unresolved built-ins

Focused gates used for this binding:

```powershell
$env:PYTHONPATH = "importer"
python -m pytest importer/tests/test_retail_hud_apt_convert.py -q
python -m ruff check importer/openbfme_importer/retail_hud_apt_convert.py importer/tests/test_retail_hud_apt_convert.py

& "C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe" --headless --path game --script res://tests/retail_hud_wnd_runtime_runner.gd
& "C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe" --headless --path game --script res://tests/retail_hud_apt_runtime_runner.gd
& "C:\Users\Jonathan\Downloads\godot47\Godot_v4.7-stable_win64_console.exe" --headless --editor --path game --quit-after 1
```

The private variants add the three frozen oracle paths to the WND runner and
the fresh `bundle-a` contract plus pack root to the APT runner. Retail payloads
and private contracts remain under `.private`.

---

