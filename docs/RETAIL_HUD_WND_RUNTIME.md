# Retail HUD WND companion runtime

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
