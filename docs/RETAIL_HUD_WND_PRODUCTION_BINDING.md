# Retail HUD WND production binding

The production HUD bundle now embeds a compact, typed companion for the exact
active BFME2 `window/controlbar.wnd`. The companion is derived during the same
transaction as the APT scene contract, and the Godot APT runtime owns the WND
runtime only after the complete scene contract validates. A rejected companion
leaves no partially configured WND runtime behind.

## Frozen identity

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

## Blocker delta

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

## Verification

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
