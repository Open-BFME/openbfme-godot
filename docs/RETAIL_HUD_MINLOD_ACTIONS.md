# Retail HUD MinLOD actions

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
