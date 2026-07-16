# Retail HUD WND callback oracle

The active `ControlBar.wnd` closure contains 21 callback identities. Retail
`game.dat` has exact `{name, handler, null}` descriptors for 17 of them. Four
shared `GameWinDefault*` callbacks are authored in the WND but have no ASCII,
UTF-16, or descriptor identity in `game.dat`, `lotrbfme2.exe`, or the other
install executables. They remain WND-library built-ins with addresses requiring
one narrow dynamic trace; behavior from Generals or OpenSAGE is not substituted.

## Slice classification

- 19 callbacks are baseline-bound or frame-reachable in an ordinary
  BFME2 1.06 Men-v-Men player skirmish.
- `BeaconWindowInput` is event-dormant until the multiplayer beacon editor is
  opened.
- `ControlBarObserverSystem` is outside the declared player-versus-player slice.

This is an implementation order, not permission to remove the latter two.
All 21 identities and their exact control bindings remain in the converted
closure.

## Exact retail handlers

| Callback | Handler | Slice role |
|---|---:|---|
| `BeaconWindowInput` | `0x0090DBAA` | beacon-event dormant |
| `ControlBarInput` | `0x004D43D0` | bound exact no-op |
| `ControlBarObserverSystem` | `0x0090E6A6` | observer-only |
| `ControlBarSystem` | `0x00802455` | central selected-button/control-bar dispatcher |
| `GameWinBlockInput` | `0x0071435A` | active input blocking/reset |
| `LeftHUDInput` | `0x008020CE` | active radar/camera/order input |
| `PassSelectedButtonsToParentSystem` | `0x006C0978` | active typed parent forwarding |
| `W3DCommandBarBackgroundDraw` | `0x0049FA82` | frame-reachable draw |
| `W3DCommandBarForegroundDraw` | `0x0049FB63` | frame-reachable draw |
| `W3DCommandBarGenExpDraw` | `0x0049ED8D` | frame-reachable progress draw |
| `W3DCommandBarGridDraw` | `0x0049DE26` | frame-reachable command grid |
| `W3DCommandBarTopDraw` | `0x0049DF9A` | frame-reachable top overlay |
| `W3DGadgetPushButtonImageDraw` | `0x004A6019` | frame-reachable button image |
| `W3DLeftHUDDraw` | `0x0049DCEE` | frame-reachable radar/HUD draw |
| `W3DNoDraw` | `0x004B3FD0` | exact one-byte `ret` |
| `W3DPowerDraw` | `0x0049E365` | frame-reachable power draw |
| `W3DRightHUDDraw` | `0x0049DDE6` | frame-reachable production/right HUD |

The private contract records each descriptor VA and SHA-256, handler entry VA,
entry-prefix length and SHA-256, exact WND control names/indexes/types/status,
binding kind, resizer reference coverage, compared message constants, static
side-effect boundary, and proposed typed Godot interface.

The message callbacks use the retail cdecl-shaped boundary
`(window, message, data1, data2) -> handled`; draw callbacks use the observed
`(window, instanceData) -> void` shape. These are typed adapter boundaries, not
a generic name-to-callable map.

## Four built-in addresses

The unresolved built-ins are `GameWinDefaultInput`, `GameWinDefaultSystem`,
`GameWinDefaultTooltip`, and `W3DGameWinDefaultDraw`. Their smallest trace is:

1. Break after the exact WND load returns at `0x0069C25F` in one BFME2 1.06
   Men-v-Men skirmish.
2. Inspect only the callback slot for the exact controls listed in the private
   contract: root, `LeftHUD`, `LeftHUD1Input`, and `ProductionQueueWindow`.
3. Trigger one default input, system, tooltip-hover, or draw event. Record the
   indirect target, four entry stack words, return `EAX`, and touched services.
4. Hash the discovered handler entry before promoting its typed adapter.

No activation trace, broad frame capture, or observer match is required.

## Verification

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_wnd_callback_oracle.py -q
python -m ruff check importer/openbfme_importer/retail_hud_wnd_callback_oracle.py importer/tests/test_retail_hud_wnd_callback_oracle.py
```

The focused gate is three passing tests, clean Ruff, and byte-identical private
`contract-a.json` / `contract-b.json`.
