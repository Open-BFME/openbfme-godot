# Retail APT HUD oracle

Reverse-engineered evidence about the **retail** BFME2/RotWK in-game HUD: the APT
(Flash-derived) Palantir movie, its ActionScript programs, its text and audio
host calls, and the external movies attached into it.

This is oracle material. It describes how the original game is authored and how
its executable behaves — measured from the retail binary and the retail data
files, not from Open BFME. It is expensive to re-derive and cheap to re-read, so
it is kept in full. What Open BFME *does* with these findings is a separate
document: [RETAIL_HUD_RUNTIME.md](RETAIL_HUD_RUNTIME.md).

> **Consolidation note.** This document absorbs several former standalone
> documents, listed under their own headings below and preserved verbatim.
> Counts, blocker totals and hashes inside those sections are snapshots taken
> when that investigation was written and are **not** current status; they are
> kept because the surrounding evidence depends on them. Live gate results
> live only in [STATUS.md](../STATUS.md).

---

<!-- merged from docs/RETAIL_HUD_FRAME_SELECTION.md -->

## Retail HUD frame selection

`retail_hud_frame_selection.py` is the bounded oracle for the BFME2 Men HUD
frame choice. It does not execute ActionScript and it does not guess a frame.
It validates all 188 files in the sealed five-bundle HUD plan, validates
`window/controlbar.wnd`, and then decodes only the finite timeline and
bytecode ranges needed for the choice.

### Proven choice

The authored `InitialSetup` function passes `_good` to
`PalantirFrame.gotoAndPlay`. Character 105, the placed `PalantirFrame` sprite,
maps that label to frame 19. Frame 19 places local imported character 102,
which imports `PalantirExport::PalantirFrame_GoodDouble`; that export resolves
to character 19 in `PalantirExport.apt`.

The same exact sprite contains the other finite choices:

| State | Frame | Local import | Retail symbol | Export character |
|---|---:|---:|---|---:|
| `_good` | 19 | 102 | `PalantirFrame_GoodDouble` | 19 |
| `_goodSingle` | 9 | 101 | `PalantirFrame_GoodSingle` | 22 |
| `_evil` | 39 | 104 | `PalantirFrame_EvilDouble` | 13 |
| `_evilSingle` | 29 | 103 | `PalantirFrame_EvilSingle` | 16 |

`SetPalantirFrameState` accepts those four states and passes its original
`state` parameter, held in register 4, to `PalantirFrame.gotoAndPlay`. The
contract records the exact function body ranges and SHA-256 values as
provenance. Unknown state handling is fail-closed.

The APT bytes do not decide whether a later engine call should use `_good` or
`_goodSingle`. They prove both mappings and prove that the authored default is
`_good` (Good Double). The engine-side Men/alignment and single-versus-double
policy remains an explicit runtime integration requirement.

### Bounded production integration

`retail_hud_apt_convert.py` consumes only the proven authored default. Its
runtime contract declares policy `bounded-retail-initial-setup-only`, selects
character 105 frame 19, and flattens the resolved
`PalantirFrame_GoodDouble` geometry. This increases both planner and production
contracts from 26 to 28 draws; the two new draws are exact textured retail
triangles. The planner blocker count changes from 130 to 131 and the production
bundle count changes from 131 to 132 because each retains explicit blockers
for non-default Palantir states and the side-bar fade.

The Godot binder validates every identity in that chain. It rejects a contract
that substitutes `_goodSingle`, claims the side bar has faded in, or omits
either bounded-selection blocker. This is not a general APT timeline executor.

### Side command bar initial state

`InGameSideCommandBar.apt` begins at frame 0 with `ButtonSet` translated to
`[1048.300048828125, 361.29998779296875]`, outside the authored 1024-pixel
width. Frame 1 is labeled `_hide`; frames 1 through 10 do not mutate the
display list; frame 10 executes `Stop`. Therefore the authored initial state is
hidden/offscreen. Frame 11 is `_fadeIn`, and reaching it requires the runtime
to call `FadeIn` (normally after a unit selection). The WND contract agrees:
`ControlBar.wnd:CommandWindow` starts `ENABLED+HIDDEN+SEE_THRU`.

### Deterministic output

Run from the repository root:

```powershell
$env:PYTHONPATH = 'importer'
python -m openbfme_importer.retail_hud_frame_selection `
  --apt-plan .private/scratch/hud-apt-profile/plan.json `
  --asset-root .private/retail-work/cache/effective-assets `
  --output .private/scratch/hud-frame-selection/contract.json
```

The output is payload-free JSON. It records source hashes, byte ranges,
timeline identities, exact resolved symbols, the default selection, and the
remaining runtime blockers. It deliberately reports `parityReady: false`
until the external Men state call and selection-driven side-command visibility
are bound and tested.

Acceptance checks:

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_frame_selection.py -q
python -m ruff check `
  importer/openbfme_importer/retail_hud_frame_selection.py `
  importer/tests/test_retail_hud_frame_selection.py
```

The finite APT record layout was cross-checked against OpenSAGE commit
`588ac477367a0022adf29f20a084e8873014e6ce`, but the emitted result is derived
from and attested to the local BFME2 retail bytes. OpenSAGE is not a runtime
dependency of this oracle.

---

<!-- merged from docs/RETAIL_HUD_TIMELINE_PLAYBACK_ORACLE.md -->

## Retail HUD timeline-playback oracle

This oracle narrows `timeline-playback-not-bound` to the exact production HUD
closure. It does not implement a clock and does not import generic Flash
defaults into the retail contract.

The BFME2 1.06 closure contains 22 reachable timeline definitions, 640 authored
frames, and 51 placed instances. Twelve side-button instances share
`ingamesidecommandbar:18`, twelve nested frames share `libingameui:23`, six
command children share `palantir:121`, and the remaining definitions have one
or two instances. Runtime state must therefore belong to the exact instance
path, not the shared definition.

All three owning movies author a 33-ms frame interval. The 66 frame-action
bindings split into 52 exact `stop` programs, four exact
`goto-label; play` programs, and ten typed blocked programs. The four goto
targets are fully resolved:

- `palantir:257` frame 34 -> `_show` at frame 0
- `palantir:293` frames 35 and 51 -> `_up` at frame 10
- `palantir:309` frame 57 -> `_stop` at frame 0

Only `palantir:309` has an explicit frame-zero `stop`. The other 21 definitions
contain no frame-zero timeline-control opcode. The retail engine's default
new-instance playing state is not encoded in APT and remains a trace gate.

No production segment needs a guessed last-frame wrap. Every authored route
reaches a stop or explicit goto before implicit wrapping would be required.
The first safe implementation should therefore fail closed at an unexpected
end instead of adding a generic modulo operation.

The oracle also preserves each source frame-item order. Eleven frames mix an
action with display-list mutation. The strongest discriminator is
`palantir:309` frame 57: source order is one removal, the goto/play action, then
nine placements. Source order is exact; whether the retail scheduler executes
actions in that order or in a separate post-mutation phase is not. The same
trace must establish when target-frame actions run after goto and whether a
delayed update catches up multiple 33-ms frames.

The implementation-ready static slice is intentionally small:

1. keep immutable timeline definitions and independent state per exact path;
2. resolve only inventoried labels;
3. apply explicit stop, play, and goto-then-play effects atomically in authored
   effect order;
4. reuse the already-exact cumulative display list for an explicitly selected
   frame;
5. do not start an automatic clock, recursively execute target-frame actions,
   or add wrapping until the narrow scheduler trace passes;
6. fail closed before any of the ten blocked frame actions unless its typed
   handler is present.

Verification:

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_timeline_playback_oracle.py -q
python -m ruff check importer/openbfme_importer/retail_hud_timeline_playback_oracle.py importer/tests/test_retail_hud_timeline_playback_oracle.py
```

The private CLI additionally generates byte-identical contracts from
`hud-261-source-conversion/plan-a.json`, its bundle-A scene contract, and the
hash-pinned effective assets. No converter, runtime, profile, build, or retail
payload file is modified.

---

<!-- merged from docs/RETAIL_HUD_MINLOD_ACTIONS.md -->

## Retail HUD MinLOD actions

This gate executes only three byte-exact BFME2 1.06 Palantir ActionScripts.
It is not a general ActionScript VM and does not grant permission to any other
program with similar opcodes.

| Program | Instruction bytes | Typed true branch |
|---|---:|---|
| `palantir:152912` | 46 at `366952`, SHA-256 `069d12e949c2bcd03d523f73f6d26d5606ffd9486e920eae18b1e26b22b037d4` | Stop the current timeline only when `this._name == "GlobeSwirlRender"` |
| `palantir:333872` | 37 at `370784`, SHA-256 `93db87938ba572d0652d77922f052fd66c6cf85e09394c708ddcd1beed97b5ba` | Set `effect1._visible` and `effect4._visible` to `false` |
| `palantir:334840` | 37 at `370840`, SHA-256 `0206dc32f71abc3c28ec488db2aaad3d0b6ba17da58f15a69ce6bac0b86951db` | Set `effect2._visible` and `effect3._visible` to `false` |

Every program emits one `conditional-min-lod` effect with an exact
`required-boolean-input` condition named `MinLOD`, an explicit `whenTrue`
array, and an explicit empty `whenFalse` array. Converter acceptance requires
the movie, source offset, instruction offset, byte length, SHA-256, opcode
sequence, branch targets, constants, target names, and property names to match.
The other 14 unsupported frame ActionScripts remain blocked.

The Godot boundary requires callers to pass `{"MinLOD": bool}` explicitly.
It never invents a default. Both branches validate the exact target-state
shape before execution. The false branch succeeds without mutation. The true
branch applies the typed mutation atomically; missing input, a non-boolean
input, missing named targets, wrong property types, or changed effect evidence
fails closed.

The deterministic private conversion remains 261 sources / 10,700,284 bytes,
24 atlases, 28 draws, 22 timelines / 640 frames / 51 instances, 74 frame
ActionScripts, 28 clip events with the existing 24 executable initialize
events, and all prior external-font/load/null evidence. The only delta is:

- supported frame ActionScripts: 57 to 60;
- unsupported frame ActionScripts: 17 to 14;
- production blockers: 34 to 31.

Two conversions at `.private/scratch/hud-minlod-actions/bundle-{a,b}` are
byte-identical. Their runtime contract aggregate is
`26cc23159f7f59e30022eb01b78233c2f03ec3a128e47eb3a33e737708be8cb5`.

Verification:

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_apt_convert.py -q
python -m ruff check importer/openbfme_importer/retail_hud_apt_convert.py importer/tests/test_retail_hud_apt_convert.py
Godot_v4.7-stable_win64_console.exe --headless --path game --script res://tests/retail_hud_apt_runtime_runner.gd
Godot_v4.7-stable_win64_console.exe --headless --path game --editor --quit
```

The legal-safe Godot gate passes 40 assertions. The private-contract gate
passes 64 assertions. Editor compilation succeeds. This does not close
external movie attachment, WND callbacks, live text/font rendering, timeline
playback, frame selection, or side-bar fade.

---

<!-- merged from docs/RETAIL_HUD_SIDE_COMMAND_ACTIONS.md -->

## Retail HUD side-command actions

`retail_hud_side_command_oracle.py` seals the three remaining small
`InGameSideCommandBar` ActionScripts against the BFME2 1.06 retail
APT/CONST/DAT triplet. It emits hashes and typed effects only; no retail bytes
leave `.private`.

### Exact behavior

- `ingamesidecommandbar:6272` calls `UpdateNeighborFrameStates()` with no
  arguments when a button frame enters `_hide` (frame 0).
- `ingamesidecommandbar:6368` calls `UpdateFrameState()` and then
  `UpdateNeighborFrameStates()` when a button enters `_show` (frame 10).
- `ingamesidecommandbar:7296`, when `_global.InGame` is truthy, visits
  `Button1` through `Button15` in ascending order and invokes
  `gotoAndPlay("_show")`.

The definition program at source offset 6264 is also sealed. A button's prior
and next neighbors are `_parent.Button(n-1)` and `_parent.Button(n+1)`, derived
from its `ButtonN` name. A neighbor is visible only when both the neighbor and
its `Frame` child exist. `UpdateFrameState` selects `_middle` when both neighbor
frames exist, `_top` for next only, `_bottom` for prior only, and `_topbottom`
for neither; a missing local `Frame` makes it a no-op. Neighbor updates run in
the authored next-then-prior order.

### Reachability and native closure

The root places the one-frame `ButtonSet` character at frame 0. That character
statically places `Button0` through `Button11`, each using the 21-frame button
character. The show script nevertheless addresses `Button1` through
`Button15`, so `Button12` through `Button15` are not present in the local APT
closure.

The BFME2 1.06 `game.dat` closes the missing-receiver behavior. Its opcode
table maps `EA_CallNamedMethodPop` (`0xB2`) to `0x00B09200`. The named-call core
checks the receiver at `0x00B081AC`; an undefined receiver branches to
`0x00B087A5`, consumes the argument count, receiver, method, and arguments,
pushes `undefined`, and returns. The pop-form handler then discards that value.
There is no ActionScript-error branch on this path. Consequently, the authored
Button12-through-Button15 calls are ordered no-ops.

The same binary also closes frame scheduling. Frame 10 stores script 6368 as a
raw type-1 record and the `Frame`/`ButtonGlass` placements as raw type-3
records. The native frame-record pass at `0x00B0F370` applies type-3 placements
immediately while leaving type-1 actions deferred. The selector at
`0x00B0F680` subsequently sends type-1 actions to the action queue through
`0x00AE4B80`. Both seek and ordinary frame-advance paths preserve that order,
and the outer tick runs the queue at `0x00AE6540` only after movie timeline
traversal. Therefore both placements exist when script 6368 runs, and
`UpdateFrameState` selects the label from the sealed truth table above.

All three scripts are now fully static typed programs. No retail capture and no
generic ActionScript VM are required for this side-command closure. The oracle
fails closed unless both the exact HUD triplet and exact BFME2 1.06 `game.dat`
match their sealed identities and native code-range hashes.

### Verification

```powershell
$env:PYTHONPATH = "importer"
& "C:\Users\Jonathan\AppData\Local\Programs\Python\Python312\python.exe" -m pytest importer/tests/test_retail_hud_side_command_oracle.py -q
& "C:\Users\Jonathan\AppData\Local\Programs\Python\Python312\python.exe" -m ruff check importer/openbfme_importer/retail_hud_side_command_oracle.py importer/tests/test_retail_hud_side_command_oracle.py
```

To emit the payload-free contract directly:

```powershell
$env:PYTHONPATH = "importer"
python -m openbfme_importer.retail_hud_side_command_oracle `
  .private/retail-work/cache/effective-assets `
  F:/BFME2/game.dat `
  --output .private/scratch/hud-side-command-oracle/contract.json
```

---

<!-- merged from docs/RETAIL_HUD_PALANTIR_COMMAND_ACTIONS.md -->

## Retail HUD Palantir command actions

`retail_hud_palantir_command_oracle.py` seals the three small remaining
Palantir command-interface scripts against the BFME2 1.06 retail sources and
`game.dat`. It emits hashes and typed effects only. Retail payloads stay under
`.private`, and the result does not require a general ActionScript VM.

### Script 167296: skill-upgrade show entry

Character 86 (`CommandUI`) runs this 68-byte program when it reaches `_show`
at frame 9. Authored order is:

1. `_root.UpdateSkillUpgradeButton()` always runs.
2. When `_global.InGame` is truthy, `_root.SetCommandButtonState` receives:
   `(1, "_up")`, `(2, "_disabled")`, `(4, "_up")`, and
   `(5, "_disabled")` in that order.

The call shape is static, but neither root method is defined anywhere in the
converted APT closure or named in `game.dat`. The script is therefore safe as
a typed host-call intent, not yet as a completed runtime effect. One native
observation—entering `_show` once while `InGame` is true—is sufficient to bind
both root methods' return/error and mutation behavior.

### Script 169224: declaration-only lifecycle registrations

Character 114 (`CommandButtons`) runs this 484-byte program at frame 0. It
only defines six functions; it does not invoke their bodies during the
declaration:

| Function | Host notification | Argument |
| --- | --- | --- |
| `OnMovieClipFrameLoaded` | `PalantirCommandUI::OnButtonFrameLoaded` | `index=clip._name&name=String(clip)` |
| `OnMovieClipFrameUnloaded` | `PalantirCommandUI::OnButtonFrameUnloaded` | `index=clip._name` |
| `OnCommandButtonSubMenuLoaded` | `PalantirCommandUI::OnSubMenuLoaded` | `index=clip._name.substr(7)&name=String(clip)` |
| `OnCommandButtonSubMenuUnloaded` | `PalantirCommandUI::OnSubMenuUnloaded` | `index=clip._name.substr(7)` |
| `OnCommandButtonToggleFlashLoaded` | `PalantirCommandUI::OnToggleFlashLoaded` | `index=clip._name.substr(11)&name=String(clip)` |
| `OnCommandButtonToggleFlashUnloaded` | `PalantirCommandUI::OnToggleFlashUnloaded` | `index=clip._name.substr(11)` |

The imported `libInGameUI` symbols `MovieClipFrame`,
`CommandButtonSubMenu`, and `CommandButtonToggleFlash` have sealed frame-0
programs that call these functions on load and through their `onUnload`
closures. The declaration program itself is implementation-safe as a typed
registration and need not execute callback bodies while registering them.

`game.dat` statically registers all six host names to handlers at
`0x00929698`, `0x009297A0`, `0x009297DD`, `0x009298E0`, `0x0092991E`, and
`0x00929A21`. Its shared parser accepts indices 0 through 5; the handlers
retain or clear button-frame, submenu, and toggle-flash slots per index. One
CommandButtons show-hide trace remains the smallest way to bind converted
clip-handle ownership and confirm the six host results in runtime order.

### Script 169256: per-button method registrations

Character 114 runs this 293-byte program at `_show` frame 9. It visits the
numeric `MovieClipFrame` children `0` through `5` and installs three methods on
each child, in this order:

- `SetAutoAbilityOverlayState(state)` calls
  `this._parent._parent.AutoAbilityOverlays[this._name].gotoAndPlay(state)`.
- `SetFlashEffectState(state)` calls
  `this._parent.FlashEffects[this._name].gotoAndPlay(state)`.
- `SetGlassState(state)` calls
  `this._parent["glass" + this._name].gotoAndPlay(state)`.

This is another registration program: none of the three bodies execute during
registration. The same frame places all six numeric frames, all six glass
targets, four submenu targets, four toggle-flash targets, and `FlashEffects`.
The sealed native frame processor applies raw type-3 placements immediately,
queues the raw type-1 frame action, and runs the queue after timeline
traversal. Every registered method target therefore exists before script
169256 executes. The root also statically places the six-entry
`AutoAbilityOverlays` collection. This script is fully implementation-safe as
a typed local-method registration with no trace gate.

### Implementation decision

- `palantir:169224`: safe to promote as declaration-only typed lifecycle
  registration. Host callback consumption remains a separate lifecycle gate.
- `palantir:169256`: safe to promote as typed per-button method registration.
- `palantir:167296`: promote only as a typed host-call intent until the one
  root-method trace is captured or an authoritative host adapter is bound.

No script needs a generic VM. The oracle retains two narrow traces: one
skill-upgrade show entry and one command-child show-hide lifecycle.

### Verification

```powershell
$env:PYTHONPATH = "importer"
python -m pytest importer/tests/test_retail_hud_palantir_command_oracle.py -q
python -m ruff check importer/openbfme_importer/retail_hud_palantir_command_oracle.py importer/tests/test_retail_hud_palantir_command_oracle.py

python -m openbfme_importer.retail_hud_palantir_command_oracle `
  .private/retail-work/cache/effective-assets `
  F:/BFME2/game.dat `
  --output .private/scratch/hud-palantir-command-oracle/contract.json
```

---

<!-- merged from docs/RETAIL_HUD_LIBINGAMEUI_ACTION_ORACLE.md -->

## Retail HUD libInGameUI MovieClipFrame action oracle

`retail_hud_libingameui_action_oracle.py` seals the 294-byte BFME2 1.06
program `libingameui:37332`. It reads the exact private retail APT/CONST/DAT
triplets and `game.dat`, then emits hashes and typed semantics only. No retail
payload is copied outside `.private` and no generic ActionScript VM is added.

### Exact program

The program is frame 0 of exported `libInGameUI.MovieClipFrame` character 6.
Its authored order is:

1. Install `CreateContent(contentType, contentName)`.
2. Install `DeleteContent()`.
3. Test `Boolean(initialized)`.
4. On first entry, install an `onUnload` closure.
5. Call `_parent.OnMovieClipFrameLoaded(this)`.
6. Set `initialized = true`.

It is not declaration-only: the first-entry path invokes the parent callback.

`CreateContent` calls `attachMovie(contentType, contentName, 0)`, resolves the
new child as `this[contentName]`, and branches when that lookup is undefined.
For a defined child it copies `placeholder._x`, `_y`, `_width`, and `_height`
in that order. It then writes the child path to
`extern[String(this) + "_ContentName"]`.

`DeleteContent` calls `contentClip.removeMovieClip()` only when `contentClip`
is defined. The unload closure calls
`_parent.OnMovieClipFrameUnloaded(this)`.

The three exact branch inputs are:

| Offset | Input | True path |
| --- | --- | --- |
| `53760` | `contentClip == undefined` | End `CreateContent` at `53824` |
| `53858` | `contentClip == undefined` | End `DeleteContent` at `53869` |
| `53873` | `Boolean(initialized)` | Skip first-entry work at `53949` |

### Parent and placement closure

Two retail parent contexts are source-proven:

- `InGameSideCommandBar` imports the symbol as character 1. Its root frame 0
  places `ButtonSet`; that sprite places `Button0` through `Button11`; each
  button places the imported `MovieClipFrame` as `Button` at its frame 0.
  Parent load/unload calls become
  `OnAptInGameSideCommandBarButtonFrameLoaded` and
  `OnAptInGameSideCommandBarButtonFrameUnloaded`.
- `Palantir` imports the symbol as character 108. Root frame 0 places
  `CommandButtons`; its `_show` frame 9 places six instances in authored order
  `1, 2, 3, 4, 5, 0`. Parent calls become
  `PalantirCommandUI::OnButtonFrameLoaded` and
  `PalantirCommandUI::OnButtonFrameUnloaded`.

The oracle pins both parent ActionScript bodies, all four native callback
handlers, both callback registries, native frame scheduling, and the native
content adapter. The native adapter reads
`_level%u.%s_ContentName`, calls `CreateContent`, retains the resulting clip
handle, calls `DeleteContent` during teardown, and clears its initialized
state in that order.

### Implementation decision

The program is implementation-safe as a bounded typed adapter. A generic
ActionScript VM and a new runtime trace are not required for these semantics.
The fail-closed implementation rule is:

- Resolve `contentType` only through the converted retail movie/export
  allowlist.
- If it is absent, preserve retail's undefined branch: do not copy geometry,
  do not register an extern path, and do not invent fallback content.

One concrete-content gate therefore remains: a requested `contentType` cannot
render until that exact retail export exists in the converted closure. This
oracle adds static evidence only. It does not implement runtime support,
authorize profile-blocker removal, or claim any concrete dynamic child is
already converted.

### Verification

```powershell
$env:PYTHONPATH = "importer"
python -m pytest importer/tests/test_retail_hud_libingameui_action_oracle.py -q
python -m ruff check `
  importer/openbfme_importer/retail_hud_libingameui_action_oracle.py `
  importer/tests/test_retail_hud_libingameui_action_oracle.py

python -m openbfme_importer.retail_hud_libingameui_action_oracle `
  .private/retail-work/cache/effective-assets `
  F:/BFME2/game.dat `
  --output .private/scratch/hud-libingameui-action-oracle/contract.json
```

---

<!-- merged from docs/RETAIL_HUD_LIBINGAMEUI_CONTENT_ORACLE.md -->

## Retail HUD `libingameui:37332` content oracle

`retail_hud_libingameui_content_oracle.py` closes the concrete `contentType`
question left by the typed `CreateContent(contentType, contentName)` adapter. It
is a static, payload-free BFME2 1.06 oracle. It does not convert assets, add a
generic ActionScript VM, modify profiles, or add runtime support.

### Men/Fords result

The exact allowlist is one row:

| Movie | Export source index | Symbol | Character | Kind |
| --- | ---: | --- | ---: | --- |
| `libInGameUI` | 647 | `CommandButton` | 49 | sprite |

The export record is at APT offset `5912` with SHA-256
`1781bccfd68f6ab4a96a49b97b5ae596568ec44c9eeced147e8212b87619700e`.
Character 49 begins at offset `18020`; its 16-byte header SHA-256 is
`f2bf1b6b03e4fb18fd13bdffdc6184aaec0b285772f73bca1eb359b44c0dbfbb`.

Only two movies in the nine-movie Men/Fords closure import the
`libInGameUI::MovieClipFrame` host:

- `InGameSideCommandBar` imports it as character 1. Sprite 18 places it as
  `Button`, and sprite 21 creates `Button0` through `Button11` on root frame 0.
- `Palantir` imports it as character 108. Sprite 114 places six instances in
  `_show` frame 9, in source order `1,2,3,4,5,0`.

The native command paths at `0x009286D2` and `0x009295AE` converge through
`0x009C3697` and use `contentType="CommandButton"` with
`contentName="Button"`. The generic create dispatch at `0x009C329B` has exactly
three direct callers:

| Caller | `contentType` | Men/Fords classification |
| --- | --- | --- |
| `0x009C7DDC` | `CommandButton` | reachable |
| `0x009E1402` | `StrategicCommandButton` | excluded StrategicHUD path |
| `0x009FCBBB` | `icon` | excluded unit-icon path |

`StrategicHUD` is pinned only as an excluded comparison. Its sole export is
`StrategicCommandButton`, source index 0, character 12. It is not in the current
nine-movie closure. No exact `icon` export exists in the current closure or that
comparison movie.

The only authored `CreateContent` call is in `palantir:95872`:

```text
_root.CommandButtons["0"].CreateContent("CommandButton", "Bttn")
```

That entire test setup is guarded by `Boolean(_global.InGame)` and branches to
the end when true. Therefore it is skipped in the actual in-game Men/Fords
scene; it is not an additional live consumer and does not change the native
`contentName="Button"` contract.

If an exact export is undefined, retail `attachMovie` leaves
`this[contentName]` undefined. `CreateContent` then skips placeholder geometry
copies and extern registration and renders no concrete child. The integration
must preserve that no-op and must never substitute generic art.

No runtime trace is needed to integrate this allowlist. The one remaining gate
is implementation: bind the converted `libInGameUI` export registry so it can
instantiate character 49 and its converted timeline/visual closure. This oracle
does not perform that binding and does not authorize profile blocker removal.

### Pinned movie triplets

All values are `byte length / SHA-256` in `.apt`, `.const`, `.dat` order.

| Scope | Movie | `.apt` | `.const` | `.dat` |
| --- | --- | --- | --- | --- |
| current | `InGameHelpBox` | `5512 / 520e5a1ff4aac288d7957a8c76818a3ceaff72b395167ccb660fa301447178e7` | `2176 / 2e6e635242e77d2bdd392001f7136c8dea61fa7a13e554850be7c702a97c71de` | `50 / 892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1` |
| current | `InGameHeroSelect` | `174324 / dc155d39f7b8dde5c2ca7ec09407918b3e914d61a5adc59a194e0a33268e3cbd` | `3146 / 46343633da353aa7fdcde60e6e2b61304ef2084324083dccf4c5e3dcbc433f93` | `178 / 29e57bc7bf05b9b21970b10834c9493a0d9ddaee4a184538f50cfdf614c8a70b` |
| current | `InGamePlanningMode` | `29998 / 20003cc09ef9b209bdb4c25a0ec3da9842abbc41a7cb5229e69a7b3f4b01330e` | `1633 / e180509285f59f53484bf28a5351d2b26047a4b4b3501660415318e74b766731` | `195 / d51ddc3707f8a47eb91d66dd1025b014515775a97701e7c24fceb8e043cf515d` |
| current | `InGameSideCommandBar` | `14082 / 84d58c67c5cab9a3bf690125cbf1a0cbf3f4bc58ccc29ffa33b992a924eca6ef` | `3364 / 5f21b405a8121edb689365441177b38b32386f101a1bb06418336dbac815975a` | `50 / 892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1` |
| current | `InGameSpellBook` | `27966 / 24f82808dadd151ffed47284ee92800af18db22894cac4f2479e32b90913f1f4` | `2406 / 40fa111c2cc8bbd05e979ea9b8b5c7fce34654c9c8c8d2b1deaf0399368b1639` | `50 / 892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1` |
| current | `libInGameImagesMain` | `9068 / ad5bb65d3ae84a85934c931764c4ed2a24cefce4db1a996fdd73add388897d24` | `32 / b1fb2aca40af93325888ee9077825df275627f55cb1e1d29938e103290228703` | `414 / fbcb53e6acc3be69461fa8066743dcd179abde8cfee22899f30aa1ce9258da0f` |
| current | `libInGameUI` | `58462 / 305bdfabca3a815f8c373419978ca080a7f28561b2ca9d36eeeb7f35992ba392` | `2876 / 717a03669f47944f9933e829e8d5d1193e375cadbbdfc5804ee131631a7176cd` | `50 / 892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1` |
| current | `Palantir` | `378173 / c1f500847f0c77d4c6504edf79113b5723300165bebd42b4dafda479516f5140` | `10260 / f07e24e3b70e286d491652cc827aef904a2ccabf54107d4f1bfc3030beee8fd9` | `586 / d8e8964711e4061b0643dd0dd3de1876b7326cee6d60e11214793b5d483f3ae4` |
| current | `PalantirExport` | `1716 / 2c35dc2671e316d6d2101b3d8790bea7f9f7b06a597abe6937862396f188391c` | `32 / 708c329be95e34edd70c1a13a82ccc58f8bad534f86ecf2c268b51467dcb21bf` | `224 / 6a45a2b1445b034f369fed28d2f29791abb82e026b0a32b45718788636433b4a` |
| excluded comparison | `StrategicHUD` | `19115 / 9b1bf4f832db1925ff3a4ee1eff49a2f11db87bf12d6394211f19c4f6570221a` | `2465 / d575e2b1e8e542b620ee7bb58d20d6edfb7b1123b079245823579c101977e8c5` | `50 / 892429fd2c0e9dc1305897fb9bf7ab41f629f1d39b4139afa8ca4f29212d18f1` |

### Run and acceptance

```powershell
$env:PYTHONPATH = "importer"
python -m openbfme_importer.retail_hud_libingameui_content_oracle `
  .private/retail-work/cache/effective-assets `
  F:/BFME2/game.dat `
  --output .private/scratch/hud-libingameui-content-oracle/contract.json
python -m pytest -q importer/tests/test_retail_hud_libingameui_content_oracle.py
python -m ruff check `
  importer/openbfme_importer/retail_hud_libingameui_content_oracle.py `
  importer/tests/test_retail_hud_libingameui_content_oracle.py
```

For deterministic A/B acceptance, write a second contract to another file in
the same `.private/scratch/hud-libingameui-content-oracle` directory and compare
the files byte-for-byte.

---

<!-- merged from docs/RETAIL_HUD_TEXT_RASTER_ORACLE.md -->

## Retail HUD text raster and layout oracle

This oracle seals the BFME2 1.06 Palantir resource, multiplier, and command-point text inputs and the retail layout path without copying retail payloads into the report. It is not a claim of rendered parity: seven font-backend and compositor questions remain explicit capture gates.

### Exact source closure

| Source | Retail identity | SHA-256 |
| --- | --- | --- |
| `Palantir.apt` | `apt/palantir.big`, precedence 51, offset 3,204, 378,173 bytes | `c1f500847f0c77d4c6504edf79113b5723300165bebd42b4dafda479516f5140` |
| `Palantir.const` | `apt/palantir.big`, precedence 51, offset 381,380, 10,260 bytes | `f07e24e3b70e286d491652cc827aef904a2ccabf54107d4f1bfc3030beee8fd9` |
| `Palantir.dat` | `apt/palantir.big`, precedence 51, offset 391,640, 586 bytes | `d8e8964711e4061b0643dd0dd3de1876b7326cee6d60e11214793b5d483f3ae4` |
| `albertusmt.otf` | `_patch103.big`, precedence 0, offset 2,510, 24,712 bytes | `6a1990e17f14ce5be199dde10f56dac3efd66aaa8e91d46119952cf55a9d9ba0` |
| `game.dat` | BFME2 1.06 executable, 10,969,600 bytes | `f008b587570bad693981dc7218588c81d192a1e064b0f7f861539c51156a7640` |

The APT root is authored at 1024x768. Font character 63 is `Albertus MT`, has zero embedded glyphs, and selects the winning `albertusmt.otf`: Albertus MT Regular / `AlbertusMT`, CFF outlines, 1,000 units per em, 298 glyphs, and no bitmap strikes. No Arial, placeholder, or synthetic-glyph fallback is parity-legal.

### Exact Palantir text records

| Character | Runtime value | Bounds | Alignment | Placeholder | Instance path | Placement |
| --- | --- | --- | --- | --- | --- | --- |
| 130 | `$PalantirResources` | `[-2,-2,50.2000008,21.1499996]` | raw 0, right | `999999` | `layer:1:Palantir/102/5/3` | translate `(56.7000006,722.2000244)` |
| 132 | `$PalantirResourceMultiplier` | `[-2,-2,25.5,21.1499996]` | raw 2, left | `x99` | `layer:1:Palantir/102/9/3` | translate `(111.6000023,722.0000244)` |
| 134 | `$PalantirCommandPoints` | `[-2,-2,58.9500008,21.1499996]` | raw 1, center | `999/999` | `layer:1:Palantir/102/13/3` | translate `(141.6000023,722.2000244)` |

All three records request font height 14, opaque RGBA `(0,204,255,255)` / packed ABGR `0xffffcc00`, and are read-only, non-multiline, and non-wrapped. Their text leaves are depth 3 inside wrappers at depths 5, 9, and 13.

### Retail-static findings

- The external-font path keeps bounds, placements, and font height as authored pixel floats. The exact `0.05` twip conversion belongs to the embedded-glyph advance branch, which character 63 cannot use because `glyphCount=0`.
- Bounds are the horizontal-alignment and vertical-centering rectangle. Raw alignments are 0 right, 1 center, and 2 left; center consumes exactly half the remaining width.
- These three strings use `top + (boxHeight - measuredTextHeight) * 0.5`, then the host truncates final x/y toward zero before drawing.
- The source color is transformed on all four ABGR channels with multiplicative and additive components.
- The external draw call receives an integer origin, not the text rectangle as a leaf scissor. Preserve APT display-list depth order.

The sealed executable evidence is:

| Purpose | Virtual-address range | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| APT external text layout | `0x00AE1260..0x00AE1532` | 722 | `7fe24c251afa315e50ace1acb0aa93f42600379c82b197dcf6f4346280587c93` |
| Dynamic text draw dispatch | `0x00AE18A2..0x00AE18EE` | 76 | `26efd3866819c0b68adfccb3e4e688a0d5fcd4702f12991131f8a51305b27f32` |
| Host external string draw | `0x004A9570..0x004A97A9` | 569 | `88c5a87809067576f37b0ec5391638be95a4493e18dc809223a075aae1d2ad80` |
| Host external string creation | `0x004AA966..0x004AACA9` | 835 | `28686cca7802610bac59a6a180b00936cecaaae7980cadd03c5683a28f1a3aed` |
| Host ABGR transform | `0x004A90B0..0x004A9192` | 226 | `4714822691806323c6188c758994c54b74c6334489d077cd496e9f6a6c3330ea` |
| Excluded embedded-glyph advance | `0x00AE19D7..0x00AE1B31` | 346 | `8a8b03103edab42bfa3f9399d51e1397b9c2e9997a0a01f042d00a5082f37c48` |

### Rendered gates still required

Retail-static evidence does not reveal the opaque font backend and final GPU state. A retail capture must still seal:

1. Height-14 device mapping at authored and scaled viewports.
2. Baseline-relative glyph origin and pixel bounds.
3. CFF hinting and antialiasing for digits, `x`, slash, and space.
4. Final alpha blend, color space, and gamma over transparent and textured HUD pixels.
5. Whether a reachable ancestor mask clips these leaves.
6. Complete-frame composite order.
7. The live registered font handle selecting the source-identity winner.

Until those captures pass, `parityReady` remains false.

### Reproduce the payload-free contract

With retail payloads confined to `.private`:

```powershell
$env:PYTHONPATH = 'importer'
python -m openbfme_importer.retail_hud_text_raster_oracle `
  --apt .private/retail-work/cache/effective-assets/Palantir.apt `
  --const .private/retail-work/cache/effective-assets/Palantir.const `
  --dat .private/retail-work/cache/effective-assets/Palantir.dat `
  --otf .private/retail-work/cache/effective-assets/albertusmt.otf `
  --game-dat F:/BFME2/game.dat `
  --opensage-root .private/scratch/opensage-hud-semantics `
  --output .private/scratch/hud-text-raster-oracle/contract.json
```

OpenSAGE commit `588ac477367a0022adf29f20a084e8873014e6ce` is observation-only. Its Arial substitution and forced-center behavior are explicitly rejected.

---

<!-- merged from docs/RETAIL_HUD_LIVE_TEXT_ORACLE.md -->

## Retail HUD live-text oracle

The BFME2 1.06 `game.dat` code, not a placeholder UI, defines the three live
Palantir strings used by text characters 130, 132, and 134.

| APT variable | Inputs | Exact rendered value |
|---|---|---|
| `APT:PalantirResources` | resource integer | `%d`; one space when negative |
| `APT:PalantirCommandPoints` | current, cap | `%d/%d`; current only when cap is negative; one space when current is negative |
| `APT:PalantirResourceMultiplier` | multiplier float | `x%g`; one space when exactly `1.0` |

The three verified retail code ranges are `0x006D46AD..0x006D4748`,
`0x007FEECB..0x007FEF80`, and `0x007FEF80..0x007FF02E`. The private oracle
verifies the complete BFME2 1.06 `game.dat` identity plus a SHA-256 for each
routine before emitting a payload-free contract. Non-finite multipliers are
outside the gameplay-facing contract and fail closed.

Generate the private evidence contract with:

```powershell
$env:PYTHONPATH = 'importer'
python -m openbfme_importer.retail_hud_live_text_oracle `
  F:\BFME2\game.dat `
  --output .private\scratch\hud-live-text\contract.json
```

The Godot bridge must feed the existing deterministic Men/Fords simulation
values into these formatters. It must not render the APT placeholders
`999999`, `x99`, or `999/999` as fallbacks.

---

<!-- merged from docs/RETAIL_HUD_RESOURCE_FLASH_ORACLE.md -->

## Retail HUD resource-flash oracle

This oracle closes the static semantics of the small blocked Palantir script
`palantir:332504` without adding a generic ActionScript VM or choosing a sound
by resemblance.

The only typed input is the zero-argument Palantir API call
`PlayCommandPointEffect()`. Its exact retail body calls
`CommandPointsFlash.gotoAndPlay("_go")`. `CommandPointsFlash` is the single
character-309 instance placed at depth 148. Its 58-frame timeline is stopped
at frame 0 (`_stop`), enters at frame 8 (`_go`), and returns through frame 57
with `goto _stop; play`. Entry-to-return spans 49 authored 33-ms frame
intervals; this is source timing metadata, not a claim about runtime wall-clock
timing. Replaying during that sequence rewinds the same visual instance; it
does not create a parallel effect.

The exact 26-byte frame-8 script first executes `play`, then calls
`_root.PlaySound("Gui_PalantirResourceBarFlash")`. The root bridge requires
the initialized Palantir state and emits the exact `FSCommand:PlaySound`
command with that event ID. BFME2 1.06 `game.dat` registers the handler at
`0x00812A51`. Its hash-pinned 162 bytes look up the event through audio-service
vslot `0x12C`, construct playback request mode `2`, and submit the request
through vslot `0x64`. The native zero-semantic-argument APT stub is independently
hash-pinned at `0x007FE9BB`; its two callers are exact, while the stripped
semantic names of the counters that lead to those calls remain unresolved.

The retail event has one exact leaf:

- `Gui_PalantirResourceBarFlash`
- `Sounds = UCommandPoints`
- volume 50, type `ui player`, `SoundFX` submix, reverb level 0
- `data/audio/sounds/ucommandpoints.wav`
- SHA-256
  `f2d3aff531ecfd3616069d53551823f92aee92f009382d3bf39d4ec8e2eca350`
- stereo 44.1 kHz, 4-bit IMA ADPCM with 2,048-byte blocks, 93,951 decoded
  sample frames, 2.130408 seconds

Every retrigger re-enters frame 8 and therefore submits one new native audio
request. The handler has no already-playing suppression branch. The only
remaining dynamic question is below that handler: whether the audio service
or mixer simultaneously mixes, coalesces, or voice-steals two requests for the
same event. The private contract retains a minimal two-trigger breakpoint
trace at `0x00812A51` and `0x00812AC5`; it does not invent an overlap policy.

Generate two payload-free contracts and verify them:

```powershell
$env:PYTHONPATH = 'importer'
python -m openbfme_importer.retail_hud_resource_flash_oracle `
  --scene-contract .private/scratch/hud-apt-clip-actions/bundle-a/data/ui/palantir/scene-contract.json `
  --effective-assets .private/retail-work/cache/effective-assets `
  --manifest .private/retail-work/cache/effective-assets/.openbfme/manifest.json `
  --game-dat F:/BFME2/game.dat `
  --output .private/scratch/hud-resource-flash-oracle/contract-a.json
python -m pytest importer/tests/test_retail_hud_resource_flash_oracle.py -q
python -m ruff check importer/openbfme_importer/retail_hud_resource_flash_oracle.py importer/tests/test_retail_hud_resource_flash_oracle.py
```

This is an oracle only. It does not change the converter, Godot runtime,
profiles, builds, or retail payload closure.

---

<!-- merged from docs/RETAIL_HUD_EXTERNAL_ATTACHMENT_ORACLE.md -->

## Retail HUD external attachment oracle

The four previously blocked Palantir movies are exact child attachments, not
independent HUD roots. `Palantir.InitialSetup` issues the retail loads in this
order:

1. `InGameSpellBook.swf` into `Palantir.root.frame0/SpellBookUI`;
2. `InGameSideCommandBar.swf` into `Palantir.root.frame0/SideCommandBar`;
3. `InGameHelpBox.swf` into `Palantir.root.frame0/helpBox`;
4. `InGameHeroSelect.swf` into `Palantir.root.frame0/HeroSelectUI`;
5. `InGamePlanningMode.swf` into `Palantir.root.frame0/planningModeUI`.

The oracle covers the four targets other than the already root-bound side bar.
Each target is an authored instance of Palantir character 41, an empty one-frame
sprite. Loading replaces that child clip's content while retaining its parent,
depth, transform, color transform, and name.

| Target | Depth | Palantir translation | Source entry | Normal Men-v-Men conclusion |
| --- | ---: | ---: | --- | --- |
| `SpellBookUI` | 3 | `(0, 0)` | `_hide` frame 0; stop frame 8 | Loaded but dormant until the spell-book path shows it |
| `helpBox` | 176 | `(585, 607)` | child `box._hide`; stop frame 0 | Loaded but dormant until help content calls `Show(height)` |
| `HeroSelectUI` | 174 | `(375, 700)` | `_hide` frame 0; stop frame 8 | Requires one trace because `ShowHeroSelectInterface()` is called unconditionally but its body is absent |
| `planningModeUI` | 180 | `(512, 30)` | `_init` frame 0; stop frame 8 | Loaded but dormant until planning mode calls `Open()` |

No general ActionScript VM is required to attach any of the four. The Godot
boundary should be four typed `RetailHudMovieSlot` children with the exact
movie-specific methods and lifecycle signals emitted by the contract. A
generic root loader would lose authored coordinate inheritance and teardown
semantics.

### Lifecycle and teardown

SpellBook owns its `OnAptInGameSpellBookLoaded` and `...Unloaded` FSCommands.
Help and Planning call their named parent methods on load and from source-owned
`onUnload` handlers. Hero Select calls its parent load method; Palantir then
installs the clip's unload closure before dispatching `OnHeroSelectLoaded`.

Retail x86 retains separate handles for Hero Select at owner offset `+0xC4`,
Help at `+0xC8`, and Planning at `+0xCC`. The exact native reset path clears
those handles Hero -> Help -> Planning. SpellBook uses a separate FSCommand
registration path, so its relative removal order is intentionally unresolved.

The Hero Select record at source offset `166756` remains separate from the
movie attachment. It has flags `0xB6`, a null clip-action pointer, and record
SHA-256
`7cf6432cbd91629acd5252c69aa957a08cadffd61214ae49ed0e078dec99a135`.
It must not acquire a fabricated initialize or unload handler.

`ControlBar.wnd` remains an active semantic/control companion. None of the four
APT target names appears in that WND payload, so it is not attachment authority
for them and must not be used to invent alternate roots.

### Remaining traces

Only four narrow runtime observations remain:

- callback completion order after the five no-wait `getURL2` instructions;
- Hero Select's frame/visibility immediately around `ShowHeroSelectInterface`;
- the four unload callbacks and child removals during one Palantir teardown;
- HelpBox clip and alternate-anchor coordinates during its load callback.

These traces do not block the exact target paths, placements, source entry
frames, lifecycle identities, or typed Godot attachment interfaces.

### Verification

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_external_attachment_oracle.py -q
python -m ruff check importer/openbfme_importer/retail_hud_external_attachment_oracle.py importer/tests/test_retail_hud_external_attachment_oracle.py
python -m ruff format --check importer/openbfme_importer/retail_hud_external_attachment_oracle.py importer/tests/test_retail_hud_external_attachment_oracle.py
```

The private A/B outputs live only under
`.private/scratch/hud-external-attachment-oracle/` and contain hashes and typed
metadata, never retail payload bytes.

---

<!-- merged from docs/RETAIL_HUD_EXTERNAL_MOVIES.md -->

## Retail HUD external movies and renderer callbacks

This oracle closes the two bounded gaps left by the Palantir host-bridge
inventory: five authored external movie loads and five privileged native draw
callbacks. It is evidence and a typed API proposal only. No movie converter,
callback implementation, generic ActionScript bridge, or profile change is
included.

The payload-free contract is generated twice at
`.private/scratch/hud-external-movies/contract-{a,b}.json`. The two files are
byte-identical. Retail payloads stay under `.private`.

### Exact movie loads

`Palantir.InitialSetup` executes five unconditional `getURL2` / Flash
`loadMovie` operations in this exact authored order:

| Order | Movie | Exact target | Current sealed closure |
|---:|---|---|---|
| 0 | `InGameSpellBook.swf` | `SpellBookUI` | add archive |
| 1 | `InGameSideCommandBar.swf` | `SideCommandBar` | already present |
| 2 | `InGameHelpBox.swf` | `helpBox` | add archive |
| 3 | `InGameHeroSelect.swf` | `HeroSelectUI` | add archive |
| 4 | `InGamePlanningMode.swf` | `planningModeUI` | add archive |

All five are load-reachable in the Men/Fords slice. “Optional movie” was an
incorrect shorthand for feature visibility: spellbook, help, hero-select, and
planning-mode may remain hidden until invoked, but their packages are still
loaded during Palantir initialization. The side command bar is both loaded and
visibly used by normal selection play.

The lifecycle behavior is not one generic unload hook:

- Spellbook and side bar call their exact `OnApt...Loaded(GetFullName(this))`
  FSCommand and install a corresponding `onUnload` notification.
- Help box calls `_parent.OnHelpBoxMovieLoaded(this)` and installs
  `_parent.OnHelpBoxMovieUnloaded(this)`.
- Hero select calls `_parent.OnHeroSelectMovieLoaded(this)`; the Palantir
  parent installs the clip-specific unload callback.
- Planning mode calls `_parent.OnPlanningModeUILoaded(this)` and installs the
  matching parent unload callback.

The runtime must preserve those individual paths and the authored order. It
must not route a guessed shared load/unload event.

### Exact source delta

Only four new BIG closures are required. Their imports already resolve into
the sealed `libInGameImagesMain`, `libInGameUI`, and `PalantirExport` libraries.

| Archive | Files | Payload bytes | Contents |
|---|---:|---:|---|
| `apt/ingamespellbook.big` | 4 | 30,453 | APT, CONST, DAT, 1 RU geometry |
| `apt/ingamehelpbox.big` | 13 | 9,377 | APT, CONST, DAT, 10 RU geometry |
| `apt/ingameheroselect.big` | 27 | 2,282,905 | APT, CONST, DAT, 23 RU geometry, 1 TGA |
| `apt/ingameplanningmode.big` | 28 | 1,083,153 | APT, CONST, DAT, 24 RU geometry, 1 TGA |

The exact 72 path/size/SHA rows, archive SHA-256 values, catalog directory
hashes, parsed APT roots/imports, DAT image mappings, RU identities, and TGA
identities are in the private contract. The four archives add 3,405,888 source
bytes.

The profile extends the existing
`men-hud-apt-runtime-bundle`; a second generic movie resource would duplicate
the same APT libraries and lifecycle. The measured result is:

- prior closure: 189 sources;
- added: 72 sources;
- sealed runtime bundle: 261 sources / 10,700,284 bytes;
- sealed source aggregate:
  `f62347fb78065726715618ed9c73f152c678fec5646ddf7b0855825d1cb23599`.

The profile planner validates the exact private oracle contract, all 72
path/size/SHA rows, and the real catalog before emitting the source closure.
The production converter now consumes all 261 sources, emits the two added
atlases, and inventories the five authored load edges. The side command bar is
already a bound root layer. The four new movie targets remain exact
`external-movie-target-attachment-not-bound` blockers; they are not flattened
as guessed independent layers. Their measured newly runtime-reachable draw,
timeline, action, and clip-action counts are zero until attachment executes.
The completion plan is source-resolvable with zero missing inputs. Renderer-
callback parity remains a separate fail-closed gate.

### Renderer callbacks

Retail `game.dat` registers all five callbacks as 32-bit native member
functions with 16 bytes of stack arguments. The oracle hashes each exact code
range and binds it to its exact Palantir clip action.

| Callback | APT target | Proven responsibility | Slice reachability |
|---|---|---|---|
| `AptPalantir::ClipRadar` | `Radar/RadarClip` | Round two float2 rectangles and store clip bounds; draws no pixels | active with radar |
| `AptPalantir::RenderRadar` | `Radar` | Render radar/minimap inside the authored rectangle | active |
| `AptPalantir::RenderRadarViewBox` | `RadarPings` view-box child | Dispatch the camera view-box overlay renderer | active with radar overlay |
| `AptPalantir::RenderMovie` | `MoviePlayback` | Fit and render the current movie frame | surface loaded; dormant until playback |
| `AptPalantir::RenderGlobe` | `GlobeSwirlRender`, `BigGlobeSwirlRender` | Dispatch the engine globe renderer in the authored rectangle | reachable when either `_show` timeline is active |

The static APT records attach each `_type` callback through an event record
decoded by the existing parser as `unload`. That exact event identity is
retained in the oracle; implementation must not silently reinterpret its
timing.

Local OpenSAGE is observation evidence, not retail authority. It confirms the
radar surface split by binding the APT `RadarClip` to
`Scene3D.RadarDrawUtil.DrawRadarMinimap` and the sibling surface to
`DrawRadarOverlay`. Its radar utility consumes the map minimap texture,
heightmap, visible radar items/game objects, and camera for the view footprint.
The retail code-range hashes remain the authority for callback ABI and bounds.

### Smallest typed Godot boundary

The proposed interface is intentionally five typed calls, with no dynamic
callback name dispatch:

```text
set_radar_clip(origin: Vector2, size: Vector2)
render_radar(origin: Vector2, size: Vector2, state: RadarFrameState)
render_radar_view_box(state: CameraRadarFootprint)
render_movie(origin: Vector2, size: Vector2, frame: MovieFrameHandle)
render_globe(origin: Vector2, size: Vector2, state: GlobeRenderState)
```

`RadarFrameState` and `CameraRadarFootprint` can be supplied from authoritative
map/radar/camera state. The retail internal layouts behind `GlobeRenderState`
and `MovieFrameHandle` are not exposed by static names; those two remain
precise typed blockers rather than guessed dictionaries.

### Verification

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_external_movies_oracle.py -q
python -m ruff check importer/openbfme_importer/retail_hud_external_movies_oracle.py importer/tests/test_retail_hud_external_movies_oracle.py
```

The focused gate is three passing tests plus clean Ruff. Two private CLI runs
must remain byte-identical.

---

<!-- merged from docs/RETAIL_HUD_MEN_FORDS_REACHABILITY.md -->

## Retail HUD Men/Fords reachability oracle

This fail-closed oracle challenges two HUD requirements against the exact
BFME2 1.06 executable, authored APT, retail INIs, and the current Men/Fords
selection sources. It emits hashes and control-flow facts only; retail payload
stays under `.private`.

### Decision

The side-command research blocker is closed enough to implement. The Palantir
selector blocker remains, and one deliberately narrow retail trace gate remains
for exact native-alias parity.

| Requirement | Decision | Exact reason |
|---|---|---|
| `palantir-nondefault-selection-not-bound` | Retain | The native chooser can return `_good`, `_goodSingle`, `_evil`, or `_evilSingle`. The Fords setup does not bind its unidentified selector bytes to Men-vs-Men. Forcing `_good` would be a guess. |
| `side-command-bar-fade-in-not-bound` | Delete the broad research blocker after binding | Existing typed simulation fields resolve all four battalions and five structures to nonempty authored command sets. Exact FadeIn target and completion behavior are sealed; this oracle does not edit the runtime binding. |
| `side-command-native-row-alias-trace` | Retain as one narrow gate | One retail trace must attach exact semantic aliases to definition bit `0x40`, local-player `+0x750`, and accepted-row byte `+0x101`. It does not block the typed Godot implementation, only a claim of exact native-alias parity. |
| "FadeIn must execute unconditionally at match load/start" | Delete this overbroad wording | The loaded callback only stores the movie root and changes native state `0 -> 1`. Eligible selection dispatches FadeIn; automatic first-tick selection is not proved. |

The active slice is a human Men versus AI Men skirmish on Fords of Isen II.
Network multiplayer is not used as a premise.

### Exact Palantir route

The chooser at `0x006d2e57` is called at `0x006d666b`. When its result differs
from owner offset `+0xe8`, the caller passes it to `0x007fea18`, which indexes
the state-string table and invokes APT `SetPalantirFrameState`.

The chooser priority is:

1. If `[0x00dfef10]` exists and `0x0044253a` sees both raw bytes `+0xb4` and
   `+0xb5` nonzero, raw byte `[[0x00e02d6c]+0x2c]` selects `_goodSingle`
   (zero, index 2) or `_evilSingle` (nonzero, index 4).
2. Otherwise, if `[0x00dfe78c]` exists and `0x00442219` succeeds, raw byte
   `((0x006a7e14([0x00dfeee8]))+0x34)+0x1bc` selects `_good` (zero, index 1)
   or `_evil` (nonzero, index 3).
3. Otherwise return `_good` (index 1).

Those offsets remain semantically unnamed. Static executable evidence proves
the branches, not the meanings of the raw selectors.

| Native index | APT state | Authored variant | APT frame (zero-based) |
|---:|---|---|---:|
| 1 | `_good` | `PalantirFrame_GoodDouble` | 19 |
| 2 | `_goodSingle` | `PalantirFrame_GoodSingle` | 9 |
| 3 | `_evil` | `PalantirFrame_EvilDouble` | 39 |
| 4 | `_evilSingle` | `PalantirFrame_EvilSingle` | 29 |

Minimum implementation is one typed four-result chooser and a fail-closed
trace/assertion for the two raw selectors. No generic APT dispatcher is needed.

### Exact side-command FadeIn route

The authored root timeline uses one-based bounds 12-22 for FadeIn and 32-42
for FadeOut. `FadeIn` is defined at APT offset 8809, body `[8836,9009)`.
Outside current frames `[32,42)`, it calls `gotoAndPlay("_fadeIn")`, targeting
one-based frame 12. Inside the range it calls
`gotoAndPlay(12 + 42 - currentframe)`, reversing FadeOut: 32 maps to 22, 37 to
17, and 41 to 13. Boundary frames 31 and 42 map to 12.

The exact interaction order is:

1. `OnAptInGameSideCommandBarLoaded` (`0x009283c9`) stores the root at owner
   `+0x18`, then writes state 1.
2. Update (`0x009287ea`) resolves the selected ID and validates an eligible,
   locally owned object plus predicate `0x009285ef`.
3. In a state other than 2 or 3, `0x00928349` invokes `FadeIn`, then writes
   state 2.
4. APT `FadeIn` runs `root.gotoAndPlay(target)`.
5. On zero-based frame 21 (one-based 22), authored bytecode issues
   `FSCommand:OnAptInGameSideCommandBarFadeInComplete`.
6. Callback `0x00928240` changes state 2 to settled-visible state 3. Playback
   continues through settled frames and stops at zero-based 30 (one-based 31).

This proves FadeIn is selection-reachable, not unconditional at load/start.

### Exact selection and command eligibility

Fanout `0x006d363e` passes the selected object to `0x0092854c`, which copies ID
`+0x74` into side-command owner `+0x1c`. Update resolves the ID through
`[0x00dfe78c]+0xb4`, requires definition byte `+0x109 & 0x40`, resolves owner
through object `+0x304`, requires the local player, and requires local-player
dword `+0x750 == 0`.

Eligibility function `0x009285ef` scans 32 command candidates from
`[0x00e01cfc]+0xdc` through `+0x158`. Candidate `+0x2c` and then row `+0x14`
resolve the row; byte `+0x101` must be nonzero. At most 15 rows are materialized
and shown. It returns true exactly when at least one row is accepted.

The typed Godot contract uses existing mutually exclusive selection state:
sorted `selected_ids` plus entity `team`, `health`, `unit_type`; or
`selected_structure_id` plus structure `team`, `health`, `structure_kind`; and
`winner` plus local team 0. Living, local, in-progress selections map as follows:

| Godot selector | Retail object | Retail command set |
|---|---|---|
| `bfme2.object.gondor-fighter-horde` | `GondorFighterHorde` | `GondorFighterHordeCommandSet` |
| `bfme2.object.gondor-tower-guard` | `GondorTowerShieldGuardHorde` | `GondorTowerShieldGuardCommandSet` |
| `bfme2.object.gondor-archer` | `GondorArcherHorde` | `GondorArcherHordeCommandSet` |
| `bfme2.object.gondor-knight` | `GondorKnightHorde` | `GondorKnightHordeCommandSet` |
| `fortress` | `MenFortressCitadel` | `MenFortressCommandSet` |
| `farm` | `GondorFarm` | `SellableCommandSet` |
| `barracks` | `GondorBarracks` | `GondorBarracksCommandSet` |
| `archery_range` | `GondorArcherRange` | `GondorArcheryCommandSet` |
| `stable` | `GondorStable` | `GondorStablesCommandSet` |

All nine object-to-command-set links and every referenced command button are
validated against pinned retail INIs. Each set has an `InPalantir=Yes` row.
The four battalion sets share authored `OK_FOR_MULTI_SELECT` Toggle Stance,
Attack Move, and Stop rows, so mixed battalion selection is also eligible.

### Run

```powershell
$env:PYTHONPATH = "importer"
python -m openbfme_importer.retail_hud_men_fords_reachability_oracle `
  --frame-contract .private/scratch/hud-frame-selection/contract-a.json `
  --map .private/content-packs/bfme2-five-maps-106-private/maps/fords-of-isen-ii/map.json `
  --setup .private/content-packs/bfme2-five-maps-106-private/maps/fords-of-isen-ii/setup.json `
  --game-dat F:/BFME2/game.dat `
  --side-apt .private/retail-work/cache/effective-assets/InGameSideCommandBar.apt `
  --output .private/scratch/hud-men-fords-reachability/contract.json
python -m pytest -q importer/tests/test_retail_hud_men_fords_reachability_oracle.py
python -m ruff check importer/openbfme_importer/retail_hud_men_fords_reachability_oracle.py importer/tests/test_retail_hud_men_fords_reachability_oracle.py
```

The oracle pins `game.dat`, relevant native ranges, Fords map/setup, HUD frame
contract, nine retail object/command-set sources, current Godot selection
sources, and side-command APT FadeIn/completion bytecode. Any drift fails closed.

---

