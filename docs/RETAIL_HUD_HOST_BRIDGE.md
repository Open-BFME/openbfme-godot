# Retail HUD host-bridge oracle

This document records the bounded host bridge required by the BFME2 1.06
Men/Fords Palantir closure. It is an oracle and API proposal only. It does not
enable arbitrary ActionScript, FSCommand, WND callbacks, URLs, or native
renderer callbacks.

## Exact closure

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

## Script mapping

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

## Exact host arguments

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

## Clip-event ordering

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

## WND boundary

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

## Minimal runtime surface

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

## Verification

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_host_bridge_oracle.py -q
python -m ruff check importer/openbfme_importer/retail_hud_host_bridge_oracle.py importer/tests/test_retail_hud_host_bridge_oracle.py
```

The focused gate is three passing tests plus clean Ruff. Two private CLI runs
must produce byte-identical contracts; the byte count and aggregate are
recorded in the private report.
