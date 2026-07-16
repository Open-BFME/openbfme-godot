# Retail HUD Palantir command actions

`retail_hud_palantir_command_oracle.py` seals the three small remaining
Palantir command-interface scripts against the BFME2 1.06 retail sources and
`game.dat`. It emits hashes and typed effects only. Retail payloads stay under
`.private`, and the result does not require a general ActionScript VM.

## Script 167296: skill-upgrade show entry

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

## Script 169224: declaration-only lifecycle registrations

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

## Script 169256: per-button method registrations

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

## Implementation decision

- `palantir:169224`: safe to promote as declaration-only typed lifecycle
  registration. Host callback consumption remains a separate lifecycle gate.
- `palantir:169256`: safe to promote as typed per-button method registration.
- `palantir:167296`: promote only as a typed host-call intent until the one
  root-method trace is captured or an authoritative host adapter is bound.

No script needs a generic VM. The oracle retains two narrow traces: one
skill-upgrade show entry and one command-child show-hide lifecycle.

## Verification

```powershell
$env:PYTHONPATH = "importer"
python -m pytest importer/tests/test_retail_hud_palantir_command_oracle.py -q
python -m ruff check importer/openbfme_importer/retail_hud_palantir_command_oracle.py importer/tests/test_retail_hud_palantir_command_oracle.py

python -m openbfme_importer.retail_hud_palantir_command_oracle `
  .private/retail-work/cache/effective-assets `
  F:/BFME2/game.dat `
  --output .private/scratch/hud-palantir-command-oracle/contract.json
```
