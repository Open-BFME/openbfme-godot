# Retail HUD side-command actions

`retail_hud_side_command_oracle.py` seals the three remaining small
`InGameSideCommandBar` ActionScripts against the BFME2 1.06 retail
APT/CONST/DAT triplet. It emits hashes and typed effects only; no retail bytes
leave `.private`.

## Exact behavior

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

## Reachability and native closure

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

## Verification

```powershell
$env:PYTHONPATH = "importer"
& "<HOME>\AppData\Local\Programs\Python\Python312\python.exe" -m pytest importer/tests/test_retail_hud_side_command_oracle.py -q
& "<HOME>\AppData\Local\Programs\Python\Python312\python.exe" -m ruff check importer/openbfme_importer/retail_hud_side_command_oracle.py importer/tests/test_retail_hud_side_command_oracle.py
```

To emit the payload-free contract directly:

```powershell
$env:PYTHONPATH = "importer"
python -m openbfme_importer.retail_hud_side_command_oracle `
  .private/retail-work/cache/effective-assets `
  <BFME2>/game.dat `
  --output .private/scratch/hud-side-command-oracle/contract.json
```
