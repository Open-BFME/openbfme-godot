# Retail HUD WND draw semantics

This oracle freezes the ten descriptor-known draw handlers used by the active
BFME2 1.06 control-bar WND. Every complete handler body, WND control binding,
destination rectangle, authored image literal, draw order, downstream service,
and unresolved state alias is retained in a deterministic private contract.

Exact authored literals include `ControlBar.wnd:BackgroundMarker`,
`ControlBar.wnd:ButtonGeneral`, `GenExpBarTop1`, `GenExpBarBottom1`,
`GenExpBar1`, `PowerPointY`, `PowerPointG`, and `PowerBarSlider`. No substitute
images, inferred materials, OpenSAGE behavior, or Generals behavior are used.

Important command boundaries:

- Background and foreground use distinct pre-state/draw functions even though
  both resolve through the authored background marker.
- Grid emits up to four ordered cell draws from retail-computed rectangles.
- Push-button image draw preserves its three-way delegate/image/fallback branch.
- Left HUD delegates world/radar pixels and clipping to the exact radar service.
- Power and experience handlers preserve authored image order and clipped live
  progress geometry; stripped blend/state aliases remain trace gates.
- Right HUD is an exact conditional delegation to the retail default helper.
- `W3DNoDraw` at `0x004B3FD0` is exactly one byte, `C3` (`RET`). It performs no
  reads, writes, calls, draws, or state changes.

The proposed boundary is ten allowlisted typed functions returning ordered
`DrawCommand` values. Generic callback dispatch and procedural fallback visuals
are forbidden. Remaining traces break only on the exact handler needing an
alias, recording control/instance pointers, mapped-image identity, destination
rect, state/color words, and ordered downstream calls.

Verification:

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_wnd_draw_semantics.py -q
python -m ruff check importer/openbfme_importer/retail_hud_wnd_draw_semantics.py importer/tests/test_retail_hud_wnd_draw_semantics.py
```
